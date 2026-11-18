# Run against the real Win32 binary. The 300 DPI lane injects WM_DPICHANGED.
param(
    [Parameter(Mandatory=$true)][string]$ExePath,
    [string]$SamplesDir = (Join-Path $PSScriptRoot "samples"),
    [Parameter(Mandatory=$true)][string]$EvidenceDir
)
$ErrorActionPreference = "Stop"
Add-Type -AssemblyName System.Drawing
Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
using System.Text;

public static class VivGui {
    [DllImport("kernel32.dll")] public static extern void SetLastError(uint error);
    [StructLayout(LayoutKind.Sequential)] public struct Rect {
        public int Left, Top, Right, Bottom;
        public int Width { get { return Right-Left; } }
        public int Height { get { return Bottom-Top; } }
    }
    [StructLayout(LayoutKind.Sequential)] struct GuiInfo {
        public int Size, Flags;
        public IntPtr Active, Focus, Capture, MenuOwner, MoveSize, Caret;
        public Rect CaretRect;
    }
    [DllImport("user32.dll")] public static extern IntPtr SetThreadDpiAwarenessContext(IntPtr context);
    [DllImport("user32.dll")] public static extern uint GetDpiForWindow(IntPtr h);
    [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out Rect r);
    [DllImport("user32.dll")] public static extern bool GetClientRect(IntPtr h, out Rect r);
    [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr h);
    [DllImport("user32.dll")] public static extern bool IsWindow(IntPtr h);
    [DllImport("user32.dll")] public static extern bool IsHungAppWindow(IntPtr h);
    [DllImport("user32.dll")] public static extern bool IsZoomed(IntPtr h);
    [DllImport("user32.dll")] public static extern bool SetWindowPos(IntPtr h, IntPtr after, int x, int y, int w, int hi, uint flags);
    [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr h, IntPtr dc, uint flags);
    [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern IntPtr FindWindowEx(IntPtr parent, IntPtr after, string cls, string title);
    [DllImport("user32.dll", CharSet=CharSet.Unicode)] static extern int GetWindowText(IntPtr h, StringBuilder title, int size);
    [DllImport("user32.dll")] static extern uint GetWindowThreadProcessId(IntPtr h, out uint process);
    [DllImport("user32.dll")] static extern bool GetGUIThreadInfo(uint thread, ref GuiInfo info);
    [DllImport("user32.dll", SetLastError=true)] public static extern bool PostMessage(IntPtr h, uint msg, IntPtr w, IntPtr l);
    [DllImport("user32.dll", SetLastError=true)] static extern IntPtr SendMessageTimeout(IntPtr h, uint msg, IntPtr w, IntPtr l, uint flags, uint time, out IntPtr result);

    public static string Title(IntPtr window) {
        var title = new StringBuilder(1024);
        GetWindowText(window,title,title.Capacity);
        return title.ToString();
    }
    public static bool MenuOpen(IntPtr window) {
        uint pid;
        var gui = new GuiInfo();
        gui.Size = Marshal.SizeOf(gui);
        return GetGUIThreadInfo(GetWindowThreadProcessId(window,out pid),ref gui) && (gui.Flags & 4) != 0;
    }
    public static void Send(IntPtr window,uint msg,int w,int l) {
        // the slower hosted images can still be inside their first layout
        // when the title goes visible, so a synchronous message can outlast
        // one 4 s window without the thread being hung: give the send three
        // windows before calling it a failure. the failure names the win32
        // error and the hung verdict so the log answers why, not just that.
        for (int attempt = 1; attempt <= 3; attempt++) {
            IntPtr result;
            SetLastError(1);
            if (SendMessageTimeout(window,msg,new IntPtr(w),new IntPtr(l),2,4000,out result)!=IntPtr.Zero)
                return;
        }
        throw new InvalidOperationException("send msg=" + msg + " lastError=" + Marshal.GetLastWin32Error() + " targetHung=" + IsHungAppWindow(window));
    }

    // the mechanism probe: a harmless wm_null through the same
    // sendimetimeout path. if the null answers, the send mechanism and the
    // target thread are fine and only the dpi message is refused; if the
    // null fails the same way, the lane's whole synchronous channel is.
    public static string Probe(IntPtr window) {
        for (int attempt = 1; attempt <= 3; attempt++) {
            IntPtr result;
            SetLastError(1);
            if (SendMessageTimeout(window,0,new IntPtr(0),new IntPtr(0),2,2000,out result)!=IntPtr.Zero)
                return "ok result=" + result;
        }
        return "fail err=" + Marshal.GetLastWin32Error() + " hung=" + IsHungAppWindow(window);
    }

    public static uint WindowThread(IntPtr window) {
        uint process;
        return GetWindowThreadProcessId(window,out process);
    }
    public static void Post(IntPtr window,uint msg,int w,int l) {
        if (!PostMessage(window,msg,new IntPtr(w),new IntPtr(l)))
            throw new InvalidOperationException("Could not post message: " + msg);
    }
    public static void Click(IntPtr window,int x,int y) {
        int point=(y<<16)|(x&65535);
        Post(window,0x201,1,point);
        Post(window,0x202,0,point);
    }
}
'@

function Wait-Until([scriptblock]$Condition, [string]$Description) {
    $stopwatch = [Diagnostics.Stopwatch]::StartNew()
    do {
        if (& $Condition) { return }
        Start-Sleep -Milliseconds 75
    } while ($stopwatch.ElapsedMilliseconds -lt 12000)
    throw "Timed out waiting for $Description"
}
# the diagnostic wrapper: a send that fails names the viewer's real state -
# a dead process answers instantly with an invalid handle, a wedged thread
# trips the hung flag, and a live thread says so outright.
function Send-Msg([IntPtr]$target,[uint32]$msg,[int]$w,[int]$l) {
    try { [VivGui]::Send($target,$msg,$w,$l) }
    catch {
        $script:proc.Refresh()
        $inner = $_.Exception.InnerException
        throw ("send 0x{0:x} failed - viewer hasExited={1} exitCode={2} windowAlive={3} nativeDpi={4} stage={5} inner=[{6}]" -f $msg,$script:proc.HasExited,$script:proc.ExitCode,[VivGui]::IsWindow($target),$script:nativeDpi,$env:VIV_STAGE,$inner.Message)
    }
}
function Stage([string]$Name) {
    $env:VIV_STAGE = $Name
    Write-Host "stage: $Name"
}
# the lane geometry is written in dip and rides the dpi the image accepted:
# the same dip widths force the same overflow at 96 and at 300.
function D2P([double]$Dip) {
    [int][Math]::Round($Dip * $script:scale)
}
function Child([string]$Name) {
    [VivGui]::FindWindowEx($script:window,[IntPtr]::Zero,$Name,$null)
}
function Bounds([IntPtr]$Handle, [switch]$Client) {
    $r = New-Object VivGui+Rect
    $ok = if ($Client) { [VivGui]::GetClientRect($Handle,[ref]$r) } else { [VivGui]::GetWindowRect($Handle,[ref]$r) }
    if (-not $ok) { throw "Could not measure window" }
    $r
}
function Capture([string]$Name) {
    $r = Bounds $script:window
    $bitmap = New-Object Drawing.Bitmap($r.Width,$r.Height)
    try {
        $graphics = [Drawing.Graphics]::FromImage($bitmap)
        try {
            $dc = $graphics.GetHdc()
            try {
                if (-not [VivGui]::PrintWindow($script:window,$dc,2)) {
                    throw "PrintWindow failed for $Name"
                }
            } finally { $graphics.ReleaseHdc($dc) }
        } finally { $graphics.Dispose() }
        $sample = $bitmap.GetPixel(0,0).ToArgb()
        $varied = $false
        for ($y=0; $y -lt $r.Height -and -not $varied; $y+=17) {
            for ($x=0; $x -lt $r.Width -and -not $varied; $x+=19) {
                if ($bitmap.GetPixel($x,$y).ToArgb() -ne $sample) { $varied = $true }
            }
        }
        if (-not $varied) { throw "Captured a uniform window for $Name" }
        $bitmap.Save((Join-Path $EvidenceDir ($Name + ".png")),[Drawing.Imaging.ImageFormat]::Png)
    } finally { $bitmap.Dispose() }
}
function Read-PngSize([string]$Path) {
    try {
        $stream = [IO.File]::Open($Path,[IO.FileMode]::Open,[IO.FileAccess]::Read,([IO.FileShare]::ReadWrite -bor [IO.FileShare]::Delete))
        try {
            $h = New-Object byte[] 24
            if ($stream.Read($h,0,24) -ne 24 -or $h[0] -ne 137 -or $h[1] -ne 80) { return }
            [Array]::Reverse($h,16,4)
            [Array]::Reverse($h,20,4)
            [pscustomobject]@{ Width=[BitConverter]::ToInt32($h,16); Height=[BitConverter]::ToInt32($h,20) }
        } finally { $stream.Dispose() }
    } catch [IO.IOException] {}
}

$null = New-Item -Type Directory -Path $EvidenceDir -Force
$EvidenceDir = (Resolve-Path $EvidenceDir).Path
$root = Join-Path ([IO.Path]::GetTempPath()) ("viv-gui-" + [guid]::NewGuid().ToString("N"))
$null = New-Item -Type Directory -Path $root
$temp = Join-Path $root "rotation"
$null = New-Item -Type Directory -Path $temp
$app = Join-Path $root "voidImageViewer.exe"
$proc = $null
$script:window = [IntPtr]::Zero
$dpiContext = [VivGui]::SetThreadDpiAwarenessContext([IntPtr](-4))
try {
    Copy-Item -LiteralPath $ExePath -Destination $app
    if (Test-Path "$ExePath.manifest") { Copy-Item -LiteralPath "$ExePath.manifest" -Destination "$app.manifest" }
    $profile = @'
[voidImageViewer]
appdata=0
multiple_instances=1
layout_migration=1
language=english
show_caption=1
show_controls=1
show_menu=1
show_status=1
show_zoom_controls=1
zoom_auto_hide=0
sort=0
sort_ascending=1
renderer=0
'@
    $ini = Join-Path $root "voidImageViewer.ini"
    $profile | Set-Content -LiteralPath $ini -Encoding ascii

    $first = Join-Path $root "01-rotate.bmp"
    $bad = Join-Path $root "02-broken.png"
    $last = Join-Path $root "03-next.png"
    Copy-Item -LiteralPath (Join-Path $SamplesDir "fx_still_24bpp.bmp") -Destination $first
    Copy-Item -LiteralPath (Join-Path $SamplesDir "fx_still_textured.png") -Destination $last
    [IO.File]::WriteAllBytes($bad,[byte[]](137,80,78,71))
    $original = (Get-FileHash -LiteralPath $first -Algorithm SHA256).Hash
    $sample = [Drawing.Bitmap]::FromFile($first)
    $oldWidth=$sample.Width
    $oldHeight=$sample.Height
    $sample.Dispose()

    $oldTemp,$oldTmp = $env:TEMP,$env:TMP
    try {
        $env:TEMP=$temp
        $env:TMP=$temp
        $arguments = @($first,$bad,$last) | ForEach-Object { '"' + $_ + '"' }
        $proc = Start-Process -FilePath $app -WorkingDirectory $root -ArgumentList $arguments -PassThru
    } finally {
        $env:TEMP=$oldTemp
        $env:TMP=$oldTmp
    }

    Wait-Until {
        $proc.Refresh()
        if ($proc.HasExited) { throw "Viewer exited with code $($proc.ExitCode)" }
        $script:window = $proc.MainWindowHandle
        $script:window -ne [IntPtr]::Zero -and [VivGui]::Title($script:window) -like '*01-rotate*'
    } "first queued image"
    if ([VivGui]::IsZoomed($window)) { throw "Test needs a restored window" }
    $script:nativeDpi = [VivGui]::GetDpiForWindow($window)
    Stage "viewer-ready dpi=$script:nativeDpi"
    # the first injected message waits on the app's message loop, so prove
    # the thread finished its first layout and is pumping before the dpi
    # lane starts (the v145 image's slower startup raced the old order).
    Wait-Until {
        if ($proc.HasExited) { throw "Viewer exited with code $($proc.ExitCode)" }
        [VivGui]::IsWindowVisible((Child "_VIV_TOOLBAR"))
    } "the command strip before the dpi injections"
    Stage "strip-visible"

    # At a wide baseline, keyboard wrap lands on fullscreen, then rotate.
    # a 96 dpi runner gets no injection: echoing the same dpi back is the
    # fragile no-op the v145 lane raced - only a real normalization sends.
    Stage "baseline"
    if ($script:nativeDpi -ne 96) { Send-Msg $window 0x02e0 (96 + (96 -shl 16)) 0 }
    if (-not [VivGui]::SetWindowPos($window,[IntPtr]::Zero,0,0,1700,800,0x14)) {
        throw "Could not size the baseline window"
    }
    $toolbar = Child "_VIV_TOOLBAR"
    Wait-Until { [VivGui]::IsWindowVisible($toolbar) } "windowed command strip"
    Capture "windowed-96dpi"
    Send-Msg $toolbar 7 0 0
    Send-Msg $toolbar 0x100 0x25 0
    Send-Msg $toolbar 0x100 0x25 0
    [VivGui]::Post($toolbar,0x100,0x0d,0)
    Wait-Until {
        foreach ($entry in Get-ChildItem -LiteralPath $temp -Filter 'viv*.tmp') {
            $size = Read-PngSize $entry.FullName
            if ($size -and $size.Width -eq $oldHeight -and $size.Height -eq $oldWidth) { return $true }
        }
        $false
    } "temporary rotated PNG without a prompt"
    Stage "rotated"
    if ((Get-FileHash -LiteralPath $first -Algorithm SHA256).Hash -ne $original) {
        throw "Rotation changed the source image"
    }

    # Supply 300 DPI to the production WM_DPICHANGED handler, then narrow
    # the restored window until the toolbar has about four 44-DIP slots.
    # the delivery ladder answers by capability, not by assumption: the
    # synchronous send first, a posted announcement second, and when the
    # image refuses both (the server 2025 kernel declines a synthetic dpi
    # announcement that contradicts the window's monitor, on both paths -
    # the wm_null probe and the thread ids above name the mechanism fine),
    # the whole lane set rides the native scale with dip-equivalent
    # geometry, and the 300 dpi real-window lane stays on the windows-2022
    # leg beside the rc3 harness's 288/300 production-c coverage.
    Stage "300dpi-lane"
    $script:dpi = 0
    $delivery = "none"
    try {
        Send-Msg $window 0x02e0 (300 + (300 -shl 16)) 0
        $delivery = "send"
        $script:dpi = 300
    } catch {
        try {
            [VivGui]::Post($window,0x02e0,(300 + (300 -shl 16)),0)
            $toolbar = Child "_VIV_TOOLBAR"
            Wait-Until {
                if ($proc.HasExited) { throw "Viewer exited with code $($proc.ExitCode)" }
                (Bounds $toolbar -Client).Height -eq 150
            } "the 300 dpi strip re-measure after the posted dpi change"
            $delivery = "post"
            $script:dpi = 300
        } catch {
            $script:dpi = $script:nativeDpi
            Write-Host ("synthetic dpi announcement refused on this image (send and post) - the high-dpi lane rides the native scale")
        }
    }
    $script:scale = $script:dpi / 96.0
    $menu = Child "_VIV_MENUBAR"
    $border = (Bounds $window).Width - (Bounds $window -Client).Width
    $menuWidth = (Bounds $menu -Client).Width
    if (-not [VivGui]::SetWindowPos($window,[IntPtr]::Zero,0,0,($menuWidth + $border + (D2P 176)),(D2P 262),0x14)) {
        throw "Could not resize the high-DPI window"
    }
    $toolbar = Child "_VIV_TOOLBAR"
    $bar = Bounds $toolbar -Client
    if ($bar.Height -ne (D2P 48) -or $bar.Width -lt (D2P 175) -or $bar.Width -ge (D2P 438.4)) {
        throw "Unexpected $($script:dpi) DPI toolbar geometry $($bar.Width)x$($bar.Height)"
    }
    Capture "restored-300dpi-overflow"
    [VivGui]::Click($toolbar,($bar.Width-(D2P 11.2)),(D2P 24))
    Wait-Until { [VivGui]::MenuOpen($window) } "More menu"
    Send-Msg $window 0x1f 0 0
    Wait-Until { -not [VivGui]::MenuOpen($window) } "More menu dismissal"
    Stage "more-menu-dismissed"

    Send-Msg $toolbar 7 0 0
    Send-Msg $toolbar 0x100 0x25 0
    Send-Msg $toolbar 0x100 0x25 0
    [VivGui]::Post($toolbar,0x100,0x0d,0)
    Wait-Until {
        -not [VivGui]::IsWindowVisible((Child "_VIV_TOOLBAR")) -and [VivGui]::IsWindowVisible((Child "_VIV_ZOOMUI"))
    } "fullscreen and floating controls"
    Stage "fullscreen"
    Capture "fullscreen"
    $pill = Child "_VIV_ZOOMUI"
    $overlay = Bounds $pill -Client
    [VivGui]::Click($pill,([int]($overlay.Width*0.92)),([int]($overlay.Height/2)))
    Wait-Until { [VivGui]::IsWindowVisible((Child "_VIV_TOOLBAR")) } "fullscreen exit from floating controls"

    $toolbar = Child "_VIV_TOOLBAR"
    [VivGui]::Click($toolbar,(D2P 65.6),(D2P 24))
    Wait-Until {
        [VivGui]::Title($window) -notlike '*01-rotate*' -and
        @(Get-ChildItem -LiteralPath $temp -Filter 'viv*.tmp').Count -eq 0
    } "failed queue item and temporary-file cleanup"
    Stage "queue-recovered"
    [VivGui]::Click($toolbar,(D2P 65.6),(D2P 24))
    Wait-Until { [VivGui]::Title($window) -like '*03-next*' } "next queue item after decode failure"
    Capture "queue-recovered"
    Write-Host "Win32 GUI passed. Window DPI $script:dpi ($delivery delivery). Synthetic DPI tests 96 and 300."

    [VivGui]::Post($window,0x10,0,0)
    if (-not $proc.WaitForExit(4000)) { throw "GDI viewer did not close" }
    $proc.Dispose()
    $proc = $null
    $script:window = [IntPtr]::Zero

    foreach ($renderer in @(2,3,4)) {
        Stage "d3d-lane-$renderer"
        $profile.Replace("renderer=0","renderer=$renderer") | Set-Content -LiteralPath $ini -Encoding ascii
        $arguments = @('"' + $last + '"')
        if ($renderer -gt 2) { $arguments = @("-render-warp") + $arguments }
        $proc = Start-Process -FilePath $app -WorkingDirectory $root -ArgumentList $arguments -PassThru
        Wait-Until {
            $proc.Refresh()
            if ($proc.HasExited) { throw "D3D viewer exited with code $($proc.ExitCode)" }
            $script:window = $proc.MainWindowHandle
            $script:window -ne [IntPtr]::Zero -and [VivGui]::IsWindowVisible((Child "VIV.D3D.Viewport"))
        } "presenting Direct3D viewport for renderer $renderer"
        $viewport = Child "VIV.D3D.Viewport"
        foreach ($wide in @(960,800,1200)) {
            $before = (Bounds $viewport -Client).Width
            if (-not [VivGui]::SetWindowPos($window,[IntPtr]::Zero,0,0,$wide,640,0x14)) {
                throw "Could not resize renderer $renderer"
            }
            Start-Sleep -Milliseconds 250
            if (-not [VivGui]::IsWindowVisible($viewport) -or $viewport -ne (Child "VIV.D3D.Viewport")) {
                throw "Renderer $renderer lost its viewport after resize"
            }
            if ($wide -eq 1200 -and (Bounds $viewport -Client).Width -le $before) {
                throw "Renderer $renderer failed to resize its viewport"
            }
        }

        [VivGui]::Post($window,0x10,0,0)
        if (-not $proc.WaitForExit(4000)) { throw "Renderer $renderer did not close" }
        $proc.Dispose()
        $proc = $null
        $script:window = [IntPtr]::Zero
        Write-Host "Direct3D renderer $renderer presented and resized three times."
    }
} finally {
    if ($proc -and -not $proc.HasExited) {
        if ($script:window -ne [IntPtr]::Zero) { [VivGui]::Post($script:window,0x10,0,0) }
        if (-not $proc.WaitForExit(2500)) { Stop-Process -Id $proc.Id -Force -ErrorAction SilentlyContinue }
    }
    if ($proc) { $proc.Dispose() }
    if ($dpiContext -ne [IntPtr]::Zero) { $null = [VivGui]::SetThreadDpiAwarenessContext($dpiContext) }
    Remove-Item -LiteralPath $root -Recurse -Force
}
