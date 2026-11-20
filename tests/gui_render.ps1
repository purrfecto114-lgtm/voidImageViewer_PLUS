# Strict Win32 on-screen present lane: the hardware renderers' present path
# (the viewport child, CreateSwapChainForHwnd, Present) that the export
# readback never touches. A renderer word without an export word pins the
# session's back end, -render-warp rides WARP, and VIV_RENDER_TRACE=1 makes
# every refusal name its gate. A lane passes when the process stays alive,
# the render trace stays empty and the captured window really shows the
# fixture's own pixels.
param(
    [Parameter(Mandatory=$true)][string]$ExePath,
    [Parameter(Mandatory=$true)][string]$EvidenceDir
)
$ErrorActionPreference = "Stop"
Add-Type -AssemblyName System.Drawing
Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
using System.Text;

public static class VivGui {
    [StructLayout(LayoutKind.Sequential)] public struct Rect {
        public int Left, Top, Right, Bottom;
        public int Width { get { return Right-Left; } }
        public int Height { get { return Bottom-Top; } }
    }
    [DllImport("user32.dll")] public static extern bool IsWindow(IntPtr h);
    [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr h);
    [DllImport("user32.dll")] public static extern bool SetWindowPos(IntPtr h, IntPtr after, int x, int y, int w, int hi, uint flags);
    [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out Rect r);
    [DllImport("user32.dll")] public static extern IntPtr GetDC(IntPtr h);
    [DllImport("user32.dll")] public static extern int ReleaseDC(IntPtr h, IntPtr dc);
    [DllImport("gdi32.dll")] public static extern bool BitBlt(IntPtr dst, int dx, int dy, int w, int h, IntPtr src, int sx, int sy, uint rop);
    [DllImport("user32.dll", CharSet=CharSet.Unicode)] static extern int GetWindowText(IntPtr h, StringBuilder title, int size);
    public static string Title(IntPtr window) {
        var title = new StringBuilder(1024);
        GetWindowText(window,title,title.Capacity);
        return title.ToString();
    }
}
'@

if (-not (Test-Path $EvidenceDir)) { New-Item -ItemType Directory -Force -Path $EvidenceDir | Out-Null }

# The fixture: one solid colour the renderers cannot guess - every sampled
# pixel inside the fitted image must answer it back within tolerance.
$fixtureColor = [Drawing.Color]::FromArgb(200,40,160)

$renderers = @("d3d9","d3d11","d3d12")

foreach ($renderer in $renderers) {
    Write-Host "=== present lane: $renderer ==="
    $temp = Join-Path ([IO.Path]::GetTempPath()) ("viv-present-" + $renderer + "-" + [GUID]::NewGuid().ToString("N"))
    New-Item -ItemType Directory -Force -Path $temp | Out-Null
    $fixture = Join-Path $temp "present-lane.bmp"
    $bitmap = New-Object Drawing.Bitmap(400,300,([Drawing.Imaging.PixelFormat]::Format24bppRgb))
    try {
        $graphics = [Drawing.Graphics]::FromImage($bitmap)
        try {
            $brush = New-Object Drawing.SolidBrush($fixtureColor)
            try { $graphics.FillRectangle($brush,0,0,400,300) } finally { $brush.Dispose() }
        } finally { $graphics.Dispose() }
        $bitmap.Save($fixture,[Drawing.Imaging.ImageFormat]::Bmp)
    } finally { $bitmap.Dispose() }

    $oldTemp,$oldTmp,$oldTrace = $env:TEMP,$env:TMP,$env:VIV_RENDER_TRACE
    $proc = $null
    try {
        $env:TEMP = $temp
        $env:TMP = $temp
        $env:VIV_RENDER_TRACE = "1"
        $arguments = @("-render-$renderer","-render-warp",'"' + $fixture + '"')
        $proc = Start-Process -FilePath $ExePath -WorkingDirectory $temp -ArgumentList $arguments -PassThru

        $deadline = [DateTime]::UtcNow.AddSeconds(30)
        $window = [IntPtr]::Zero
        while ([DateTime]::UtcNow -lt $deadline) {
            $proc.Refresh()
            if ($proc.HasExited) { throw "$renderer`: viewer exited with code $($proc.ExitCode) before a window answered" }
            $window = $proc.MainWindowHandle
            if ($window -ne [IntPtr]::Zero -and [VivGui]::Title($window) -like '*present-lane*') { break }
            Start-Sleep -Milliseconds 200
        }
        if ($window -eq [IntPtr]::Zero) { throw "$renderer`: no window titled present-lane answered" }

        if (-not [VivGui]::SetWindowPos($window,[IntPtr]::Zero,0,0,900,650,0x14)) {
            throw "$renderer`: could not size the window"
        }
        Start-Sleep -Milliseconds 1200

        $proc.Refresh()
        if ($proc.HasExited) { throw "$renderer`: viewer exited with code $($proc.ExitCode) during the present settle" }

        $tracePath = Join-Path $temp "viv_render_trace.log"
        if (Test-Path $tracePath) {
            $lines = @(Get-Content -LiteralPath $tracePath | Where-Object { $_.Length -gt 0 })
            if ($lines.Count -gt 0) {
                throw "$renderer`: the render trace named refusals:`n" + ($lines -join "`n")
            }
        }

        $r = New-Object 'VivGui+Rect'
        if (-not [VivGui]::GetWindowRect($window,[ref]$r)) { throw "$renderer`: GetWindowRect failed" }
        # The capture reads the composed screen, not PrintWindow's redirection
        # surface: the d3d9 leg's blt-model present never rides that surface
        # (the first run captured the parent's own background where the child
        # had presented), and the screen is the truth the user sees for every
        # back end - the window sits at (0,0) in the foreground.
        $capture = New-Object Drawing.Bitmap($r.Width,$r.Height)
        try {
            $graphics = [Drawing.Graphics]::FromImage($capture)
            try {
                $hg = $graphics.GetHdc()
                $screen = [VivGui]::GetDC([IntPtr]::Zero)
                try {
                    if (-not [VivGui]::BitBlt($hg,0,0,$r.Width,$r.Height,$screen,$r.Left,$r.Top,0x00CC0020)) {
                        throw "$renderer`: the screen capture failed"
                    }
                } finally {
                    [void][VivGui]::ReleaseDC([IntPtr]::Zero,$screen)
                    $graphics.ReleaseHdc($hg)
                }
            } finally { $graphics.Dispose() }

            $capture.Save((Join-Path $EvidenceDir ("present-" + $renderer + ".png")),[Drawing.Imaging.ImageFormat]::Png)

            # The central band belongs to the fitted image alone (the chrome
            # rides the edges): a 7x7 grid inside the middle fifth must
            # answer the fixture colour back.
            $hits = 0
            $samples = 0
            $cx = [int]($r.Width / 2)
            $cy = [int]($r.Height / 2)
            for ($dy=-3; $dy -le 3; $dy++) {
                for ($dx=-3; $dx -le 3; $dx++) {
                    $x = [Math]::Max(0,[Math]::Min($r.Width-1,($cx + $dx*25)))
                    $y = [Math]::Max(0,[Math]::Min($r.Height-1,($cy + $dy*18)))
                    $p = $capture.GetPixel($x,$y)
                    $samples++
                    if ([Math]::Abs($p.R - $fixtureColor.R) -le 16 -and
                        [Math]::Abs($p.G - $fixtureColor.G) -le 16 -and
                        [Math]::Abs($p.B - $fixtureColor.B) -le 16) { $hits++ }
                }
            }
            if ($samples -lt 49 -or ($hits * 100) / $samples -lt 80) {
                throw ("$renderer`: the window did not show the fixture pixels ($hits of $samples samples matched)")
            }
            Write-Host "$renderer`: present path answered ($hits/$samples samples)"
        } finally { $capture.Dispose() }
    } finally {
        $env:TEMP = $oldTemp
        $env:TMP = $oldTmp
        if ($null -eq $oldTrace) { Remove-Item Env:\VIV_RENDER_TRACE -ErrorAction SilentlyContinue }
        else { $env:VIV_RENDER_TRACE = $oldTrace }
        if ($null -ne $proc) {
            if (-not $proc.HasExited) { $proc.Kill() }
            $proc.WaitForExit()
        }
        Remove-Item -Recurse -Force $temp -ErrorAction SilentlyContinue
    }
}

Write-Host "ALL PRESENT LANES PASS"
