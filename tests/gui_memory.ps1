# Strict Win32 memory lane: the hardware renderers' resident-set behavior on
# known canvases. The ledger (scripts/rc9_memory_ledger.py) prices the chain
# from the shipped policy - three held dib seats, the lazy mipmap chain, one
# dynamic bgra8 texture, the swap chain and the d3 runtime commit - and this
# lane measures the real numbers per back end with warp, then pins two bounds
# the future must keep: a repaint churn bound (100 synchronous paints may not
# grow the private bytes - the upload is cached against the frame dib and a
# churn leak would answer here first) and a generous absolute bound that
# catches gross double-holds. The fixtures are generated in the lane's own
# temp (deterministic lcg gradient noise, 24bpp, uncompressible content -
# memory prices the canvas, never the compressed file bytes).
param(
    [Parameter(Mandatory=$true)][string]$ExePath,
    [Parameter(Mandatory=$true)][string]$EvidenceDir
)
$ErrorActionPreference = "Stop"
Add-Type -AssemblyName System.Drawing
# the c# compile must name its own references (powershell 5.1's Add-Type
# does not pass the already-loaded assemblies on)
Add-Type -ReferencedAssemblies System.Drawing -TypeDefinition @'
using System;
using System.Drawing;
using System.Drawing.Imaging;
using System.Runtime.InteropServices;
using System.Text;

public static class VivMem {
    [StructLayout(LayoutKind.Sequential)] public struct Rect {
        public int Left, Top, Right, Bottom;
        public int Width { get { return Right-Left; } }
        public int Height { get { return Bottom-Top; } }
    }
    [DllImport("user32.dll")] public static extern bool IsWindow(IntPtr h);
    [DllImport("user32.dll")] public static extern bool SetWindowPos(IntPtr h, IntPtr after, int x, int y, int w, int hi, uint flags);
    [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out Rect r);
    [DllImport("user32.dll")] public static extern bool RedrawWindow(IntPtr h, IntPtr rect, IntPtr rgn, uint flags);
    [DllImport("user32.dll", CharSet=CharSet.Unicode)] static extern int GetWindowText(IntPtr h, StringBuilder title, int size);
    public static string Title(IntPtr window) {
        var title = new StringBuilder(1024);
        GetWindowText(window,title,title.Capacity);
        return title.ToString();
    }

    // The fixture: one deterministic gradient-noise canvas - complex enough
    // that every decoder path works for its pixels, deterministic enough
    // that the runner's numbers answer the same bytes every run.
    public static void Fill(Bitmap bmp) {
        int wide = bmp.Width;
        int high = bmp.Height;
        var data = bmp.LockBits(new Rectangle(0,0,wide,high), ImageLockMode.WriteOnly, PixelFormat.Format24bppRgb);
        try {
            int stride = data.Stride;
            byte[] row = new byte[stride];
            uint seed = 0x1234567u;
            for (int y = 0; y < high; y++) {
                int i = 0;
                for (int x = 0; x < wide; x++) {
                    seed = seed * 1103515245u + 12345u;
                    row[i++] = (byte)((x * 255 / (wide > 1 ? wide - 1 : 1)) ^ (seed >> 16));
                    row[i++] = (byte)((y * 255 / (high > 1 ? high - 1 : 1)) ^ (seed >> 24));
                    row[i++] = (byte)(seed >> 8);
                }
                Marshal.Copy(row, 0, data.Scan0 + y * stride, stride);
            }
        } finally { bmp.UnlockBits(data); }
    }
}
'@

if (-not (Test-Path $EvidenceDir)) { New-Item -ItemType Directory -Force -Path $EvidenceDir | Out-Null }

# The redraw churn flags: invalidate the frame and every child, update now.
$rdwFlags = 0x1 -bor 0x80 -bor 0x100 -bor 0x400

function New-MemoryFixture([string]$Path, [int]$Wide, [int]$High) {
    $bitmap = New-Object Drawing.Bitmap($Wide,$High,([Drawing.Imaging.PixelFormat]::Format24bppRgb))
    try {
        [VivMem]::Fill($bitmap)
        $bitmap.Save($Path,[Drawing.Imaging.ImageFormat]::Bmp)
    } finally { $bitmap.Dispose() }
}

function Measure-Process([System.Diagnostics.Process]$proc) {
    $proc.Refresh()
    return @{
        PrivateMb = [Math]::Round($proc.PrivateMemorySize64 / 1MB, 1)
        WorkingSetMb = [Math]::Round($proc.WorkingSet64 / 1MB, 1)
    }
}

# gdi carries no renderer words (the control); the gpu passes pin their back
# end and ride warp. the second fixture seat exercises the preload slot the
# default config holds (preload 1) - the steady state the ledger prices.
$passes = @(
    @{ Name = "gdi"; Words = @(); Wide = 4000; High = 3000; PrivateMaxMb = 600 },
    @{ Name = "d3d9"; Words = @("-render-d3d9","-render-warp"); Wide = 4000; High = 3000; PrivateMaxMb = 600 },
    @{ Name = "d3d11"; Words = @("-render-d3d11","-render-warp"); Wide = 4000; High = 3000; PrivateMaxMb = 600 },
    @{ Name = "d3d12"; Words = @("-render-d3d12","-render-warp"); Wide = 4000; High = 3000; PrivateMaxMb = 600 },
    @{ Name = "d3d12-27mp"; Words = @("-render-d3d12","-render-warp"); Wide = 6000; High = 4500; PrivateMaxMb = 750 }
)

$rows = @()

foreach ($pass in $passes) {
    $name = $pass.Name
    Write-Host "=== memory lane: $name ==="
    $temp = Join-Path ([IO.Path]::GetTempPath()) ("viv-memory-" + $name + "-" + [GUID]::NewGuid().ToString("N"))
    New-Item -ItemType Directory -Force -Path $temp | Out-Null
    $fixtureA = Join-Path $temp "memory-lane-a.bmp"
    $fixtureB = Join-Path $temp "memory-lane-b.bmp"
    New-MemoryFixture $fixtureA $pass.Wide $pass.High
    New-MemoryFixture $fixtureB $pass.Wide $pass.High

    $oldTemp,$oldTmp,$oldTrace = $env:TEMP,$env:TMP,$env:VIV_RENDER_TRACE
    $proc = $null
    try {
        $env:TEMP = $temp
        $env:TMP = $temp
        $env:VIV_RENDER_TRACE = "1"
        $arguments = @($pass.Words) + @(('"' + $fixtureA + '"'),('"' + $fixtureB + '"'))
        $proc = Start-Process -FilePath $ExePath -WorkingDirectory $temp -ArgumentList $arguments -PassThru

        $deadline = [DateTime]::UtcNow.AddSeconds(40)
        $window = [IntPtr]::Zero
        while ([DateTime]::UtcNow -lt $deadline) {
            $proc.Refresh()
            if ($proc.HasExited) { throw "$name`: viewer exited with code $($proc.ExitCode) before a window answered" }
            $window = $proc.MainWindowHandle
            if ($window -ne [IntPtr]::Zero -and [VivMem]::Title($window) -like '*memory-lane*') { break }
            Start-Sleep -Milliseconds 200
        }
        if ($window -eq [IntPtr]::Zero) { throw "$name`: no window titled memory-lane answered" }

        if (-not [VivMem]::SetWindowPos($window,[IntPtr]::Zero,0,0,900,650,0x14)) {
            throw "$name`: could not size the window"
        }
        # the settle covers both decodes (36 mb dibs at 12 mp), the texture
        # upload and the preload thread's own seat fill
        Start-Sleep -Seconds 5

        $proc.Refresh()
        if ($proc.HasExited) { throw "$name`: viewer exited with code $($proc.ExitCode) during the settle" }

        $m1 = Measure-Process $proc

        # the churn probe: 100 synchronous paints answer the cached-upload
        # contract - a per-paint allocation leak shows here first.
        for ($i=0; $i -lt 100; $i++) {
            if (-not [VivMem]::RedrawWindow($window,[IntPtr]::Zero,[IntPtr]::Zero,$rdwFlags)) {
                throw "$name`: RedrawWindow refused at paint $i"
            }
        }
        Start-Sleep -Milliseconds 800
        $m2 = Measure-Process $proc

        $growth = [Math]::Round($m2.PrivateMb - $m1.PrivateMb, 1)
        $tracePath = Join-Path $temp "viv_render_trace.log"
        $traceLines = @()
        if (Test-Path $tracePath) {
            $traceLines = @(Get-Content -LiteralPath $tracePath | Where-Object { $_.Length -gt 0 })
        }
        if ($traceLines.Count -gt 0) {
            throw "$name`: the render trace named refusals:`n" + ($traceLines -join "`n")
        }
        if ($growth -gt 12) {
            throw ("$name`: the churn probe grew private bytes by $growth mb (bound 12)")
        }
        if ($m2.PrivateMb -gt $pass.PrivateMaxMb) {
            throw ("$name`: private bytes $([Math]::Round($m2.PrivateMb,0)) mb past the gross bound $($pass.PrivateMaxMb) mb")
        }

        $rows += [ordered]@{
            pass = $name; canvas = "$($pass.Wide)x$($pass.High)"
            private_mb = $m2.PrivateMb; private_growth_mb = $growth
            working_set_mb = $m2.WorkingSetMb
        }
        Write-Host "$name`: private $($m2.PrivateMb) mb (churn +$growth), working set $($m2.WorkingSetMb) mb"
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

$rows | ConvertTo-Json -Depth 3 | Set-Content (Join-Path $EvidenceDir "memory-lane.json")
Write-Host "ALL MEMORY LANES PASS"
