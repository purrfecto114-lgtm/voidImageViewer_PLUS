# Strict Win32 on-screen present lane: the hardware renderers' present path
# (the viewport child, CreateSwapChainForHwnd, Present) that the export
# readback never touches. A renderer word without an export word pins the
# session's back end, -render-warp rides WARP, and VIV_RENDER_TRACE=1 makes
# every refusal name its gate. A lane passes when the process stays alive,
# the render trace stays empty, the viewport child window exists (the gpu
# leg really ran) and the composed screen really shows the fixture's own
# pixels - a gdi control pass with no renderer words answers the load
# question first, so a present failure can never wear a load failure's face.
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
    [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern IntPtr FindWindowEx(IntPtr parent, IntPtr after, string cls, string title);
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

# The gdi control pass carries no renderer words; the gpu passes pin their
# back end for the session. the control isolates the load question: a
# present failure can never wear a load failure's face.
$passes = @(
    @{ Name = "gdi"; Words = @() },
    @{ Name = "d3d9"; Words = @("-render-d3d9","-render-warp") },
    @{ Name = "d3d11"; Words = @("-render-d3d11","-render-warp") },
    @{ Name = "d3d12"; Words = @("-render-d3d12","-render-warp") }
)

function Sample-Fixture([Drawing.Bitmap]$capture, [VivGui+Rect]$r) {
    # The central band belongs to the fitted image alone (the chrome rides
    # the edges): a 7x7 grid inside the middle fifth must answer the
    # fixture colour back.
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
    return @($hits,$samples)
}

foreach ($pass in $passes) {
    $name = $pass.Name
    Write-Host "=== present lane: $name ==="
    $temp = Join-Path ([IO.Path]::GetTempPath()) ("viv-present-" + $name + "-" + [GUID]::NewGuid().ToString("N"))
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
        $arguments = @($pass.Words) + @('"' + $fixture + '"')
        $proc = Start-Process -FilePath $ExePath -WorkingDirectory $temp -ArgumentList $arguments -PassThru

        $deadline = [DateTime]::UtcNow.AddSeconds(30)
        $window = [IntPtr]::Zero
        while ([DateTime]::UtcNow -lt $deadline) {
            $proc.Refresh()
            if ($proc.HasExited) { throw "$name`: viewer exited with code $($proc.ExitCode) before a window answered" }
            $window = $proc.MainWindowHandle
            if ($window -ne [IntPtr]::Zero -and [VivGui]::Title($window) -like '*present-lane*') { break }
            Start-Sleep -Milliseconds 200
        }
        if ($window -eq [IntPtr]::Zero) { throw "$name`: no window titled present-lane answered" }

        if (-not [VivGui]::SetWindowPos($window,[IntPtr]::Zero,0,0,900,650,0x14)) {
            throw "$name`: could not size the window"
        }

        # the gpu legs create the viewport child before any present can
        # answer; its absence after the settle is the gpu leg never running.
        if ($name -ne "gdi") {
            $settled = $false
            $deadline = [DateTime]::UtcNow.AddSeconds(8)
            $child = [IntPtr]::Zero
            while ([DateTime]::UtcNow -lt $deadline) {
                $child = [VivGui]::FindWindowEx($window,[IntPtr]::Zero,"VIV.D3D.Viewport",$null)
                if ($child -ne [IntPtr]::Zero) { $settled = $true; break }
                Start-Sleep -Milliseconds 250
            }
            if (-not $settled) { throw "$name`: the viewport child window never answered (the gpu leg did not run)" }
        }

        # The capture reads the composed screen, not PrintWindow's redirection
        # surface: the d3d9 leg's blt-model present never rides that surface
        # (the first run captured the parent's own background where the child
        # had presented), and the screen is the truth the user sees for every
        # back end - the window sits at (0,0) in the foreground.
        $r = New-Object 'VivGui+Rect'
        $hits = 0
        $samples = 0
        $deadline = [DateTime]::UtcNow.AddSeconds(10)
        while ($true) {
            if (-not [VivGui]::GetWindowRect($window,[ref]$r)) { throw "$name`: GetWindowRect failed" }
            $capture = New-Object Drawing.Bitmap($r.Width,$r.Height)
            try {
                $graphics = [Drawing.Graphics]::FromImage($capture)
                try {
                    $hg = $graphics.GetHdc()
                    $screen = [VivGui]::GetDC([IntPtr]::Zero)
                    try {
                        if (-not [VivGui]::BitBlt($hg,0,0,$r.Width,$r.Height,$screen,$r.Left,$r.Top,0x00CC0020)) {
                            throw "$name`: the screen capture failed"
                        }
                    } finally {
                        [void][VivGui]::ReleaseDC([IntPtr]::Zero,$screen)
                        $graphics.ReleaseHdc($hg)
                    }
                } finally { $graphics.Dispose() }

                $hits,$samples = Sample-Fixture $capture $r
                if (($hits * 100) / $samples -ge 80) {
                    $capture.Save((Join-Path $EvidenceDir ("present-" + $name + ".png")),[Drawing.Imaging.ImageFormat]::Png)
                    break
                }
                if ([DateTime]::UtcNow -ge $deadline) {
                    $capture.Save((Join-Path $EvidenceDir ("present-" + $name + "-failed.png")),[Drawing.Imaging.ImageFormat]::Png)
                    throw ("$name`: the screen did not show the fixture pixels ($hits of $samples samples matched)")
                }
            } finally { $capture.Dispose() }

            Start-Sleep -Milliseconds 500
        }

        $proc.Refresh()
        if ($proc.HasExited) { throw "$name`: viewer exited with code $($proc.ExitCode) after the present answered" }

        $tracePath = Join-Path $temp "viv_render_trace.log"
        if (Test-Path $tracePath) {
            $lines = @(Get-Content -LiteralPath $tracePath | Where-Object { $_.Length -gt 0 })
            if ($lines.Count -gt 0) {
                throw "$name`: the render trace named refusals:`n" + ($lines -join "`n")
            }
        }

        Write-Host "$name`: present path answered ($hits/$samples samples)"
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
