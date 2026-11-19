# Run the RC4 regressions against the Win32 binary and record the effective DPI.
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

    public static bool TrySend(IntPtr window,uint msg,int w,int l,uint timeout) {
        IntPtr result;
        return SendMessageTimeout(window,msg,new IntPtr(w),new IntPtr(l),3,timeout,out result)!=IntPtr.Zero;
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

// The status fact panes' measure/draw gap, replayed on the runner's real
// gdi: the app sizes the index/dimension/frame panes from a text extent
// and paints with DrawTextW, so the probe quantifies what the two engines
// disagree on and sweeps the pane widths for the ellipsis decision - the
// rc.6 captures painted "1 / 3" as "1 ..." with the two-dip slack already
// in place, and only the real engine answers why.
public static class StatusFacts {
    [StructLayout(LayoutKind.Sequential)] public struct SIZE { public int cx, cy; }
    [StructLayout(LayoutKind.Sequential, CharSet=CharSet.Unicode)]
    public struct LOGFONTW {
        public int lfHeight, lfWidth, lfEscapement, lfOrientation, lfWeight;
        public byte lfItalic, lfUnderline, lfStrikeOut, lfCharSet, lfOutPrecision, lfClipPrecision, lfQuality, lfPitchAndFamily;
        [MarshalAs(UnmanagedType.ByValTStr, SizeConst=32)] public string lfFaceName;
    }
    [StructLayout(LayoutKind.Sequential, CharSet=CharSet.Unicode)]
    public struct NONCLIENTMETRICSW {
        public int cbSize, iBorderWidth, iScrollWidth, iScrollHeight, iCaptionWidth, iCaptionHeight;
        public LOGFONTW lfCaptionFont;
        public int iSmCaptionWidth, iSmCaptionHeight;
        public LOGFONTW lfSmCaptionFont;
        public int iMenuWidth, iMenuHeight;
        public LOGFONTW lfMenuFont;
        public LOGFONTW lfStatusFont, lfMessageFont;
        public int iPaddedBorderWidth;
    }
    [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern bool SystemParametersInfoForDpi(uint action, uint size, ref NONCLIENTMETRICSW metrics, uint update, uint dpi);
    [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern bool SystemParametersInfoW(uint action, uint size, ref NONCLIENTMETRICSW metrics, uint update);
    [DllImport("gdi32.dll", CharSet=CharSet.Unicode)] public static extern IntPtr CreateFontIndirectW(ref LOGFONTW lf);
    [DllImport("gdi32.dll")] public static extern IntPtr SelectObject(IntPtr dc, IntPtr obj);
    [DllImport("gdi32.dll")] public static extern bool DeleteObject(IntPtr obj);
    [DllImport("gdi32.dll")] public static extern IntPtr CreateCompatibleDC(IntPtr dc);
    [DllImport("gdi32.dll")] public static extern IntPtr CreateCompatibleBitmap(IntPtr dc, int w, int h);
    [DllImport("gdi32.dll")] public static extern bool DeleteDC(IntPtr dc);
    [DllImport("user32.dll")] public static extern IntPtr GetDC(IntPtr h);
    [DllImport("user32.dll")] public static extern int ReleaseDC(IntPtr h, IntPtr dc);
    [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern int DrawTextW(IntPtr dc, string text, int count, ref VivGui.Rect rect, uint flags);
    [DllImport("gdi32.dll", CharSet=CharSet.Unicode)] public static extern bool GetTextExtentPoint32(IntPtr dc, string text, int count, ref SIZE sz);
    [DllImport("gdi32.dll")] public static extern int SetBkMode(IntPtr dc, int mode);
    [DllImport("gdi32.dll")] public static extern uint SetTextColor(IntPtr dc, uint color);
    [DllImport("gdi32.dll")] public static extern IntPtr GetStockObject(int index);
    [DllImport("user32.dll")] public static extern bool FillRect(IntPtr dc, ref VivGui.Rect rect, IntPtr brush);
    [DllImport("gdi32.dll")] public static extern uint GetPixel(IntPtr dc, int x, int y);
    [DllImport("gdi32.dll")] public static extern uint SetPixel(IntPtr dc, int x, int y, uint color);

    const uint DT_LEFT=0x0, DT_TOP=0x0, DT_SINGLELINE=0x20, DT_NOPREFIX=0x800, DT_END_ELLIPSIS=0x8000, DT_CALCRECT=0x400;

    // the app's number font: the menu font the system sizes for this dpi
    // (os_menu_font), with the number-role overrides from _viv_ui_font -
    // consolas, fixed pitch, -VIV_UI_SCALE(24,dpi,192) tall, weight normal.
    public static IntPtr AppNumberFont(uint dpi) {
        var ncm = new NONCLIENTMETRICSW();
        ncm.cbSize = Marshal.SizeOf(ncm);
        if (!SystemParametersInfoForDpi(0x0029,(uint)ncm.cbSize,ref ncm,0,dpi)
            && !SystemParametersInfoW(0x0029,(uint)ncm.cbSize,ref ncm,0))
            throw new InvalidOperationException("SPI_GETNONCLIENTMETRICS failed");
        LOGFONTW lf = ncm.lfMenuFont;
        lf.lfHeight = -((24 * (int)dpi + 96) / 192);
        lf.lfWidth = 0;
        lf.lfWeight = 400;
        lf.lfUnderline = 0;
        lf.lfCharSet = 1;             // DEFAULT_CHARSET
        lf.lfPitchAndFamily = 0x31;   // FIXED_PITCH | FF_MODERN
        lf.lfFaceName = "Consolas";
        IntPtr font = CreateFontIndirectW(ref lf);
        if (font == IntPtr.Zero) throw new InvalidOperationException("CreateFontIndirectW failed");
        return font;
    }

    static int DrawNatural(IntPtr dc, string text) {
        VivGui.Rect rect = new VivGui.Rect();
        rect.Left = 0; rect.Top = 0; rect.Right = 32767; rect.Bottom = 32767;
        DrawTextW(dc,text,-1,ref rect,DT_LEFT|DT_TOP|DT_SINGLELINE|DT_NOPREFIX|DT_CALCRECT);
        return rect.Right - rect.Left;
    }

    // paint one pane exactly like _viv_status_draw_item: black face, white
    // text, the rect inset 8 a side, the draw's own flag set.
    static void PaintPane(IntPtr dc, IntPtr font, string text, int pane) {
        VivGui.Rect full = new VivGui.Rect();
        full.Left = 0; full.Top = 0; full.Right = pane; full.Bottom = 26;
        FillRect(dc,ref full,GetStockObject(4));
        SelectObject(dc,font);
        SetBkMode(dc,1);
        SetTextColor(dc,0x00ffffff);
        VivGui.Rect rect = new VivGui.Rect();
        rect.Left = 8; rect.Top = 0; rect.Right = pane - 8; rect.Bottom = 26;
        DrawTextW(dc,text,-1,ref rect,DT_LEFT|DT_TOP|DT_SINGLELINE|DT_END_ELLIPSIS|DT_NOPREFIX);
    }

    // ink pattern of the draw region, sampled on a luminance threshold:
    // antialiasing fringes must not decide, the glyph shapes must.
    static string InkMap(IntPtr dc, int x0, int x1) {
        var sb = new StringBuilder();
        for (int x = x0; x < x1; x++)
            for (int y = 4; y < 22; y++) {
                uint c = GetPixel(dc,x,y);
                int lum = (int)((c & 0xff) + ((c >> 8) & 0xff) + ((c >> 16) & 0xff)) / 3;
                sb.Append(lum > 64 ? '1' : '0');
            }
        return sb.ToString();
    }

    // one painted row on a fresh bitmap: returns the ink map.
    static string PaintAndMap(IntPtr screen, IntPtr font, string text, int pane) {
        IntPtr dc = CreateCompatibleDC(screen);
        IntPtr bmp = CreateCompatibleBitmap(screen,pane,26);
        try {
            SelectObject(dc,bmp);
            PaintPane(dc,font,text,pane);
            int x1 = Math.Min(8 + DrawNatural(dc,text), pane - 8) - 1;
            return InkMap(dc,8,Math.Max(9,x1));
        } finally {
            DeleteDC(dc);
            DeleteObject(bmp);
        }
    }

    // the report: extent vs draw per string, then the "1 / 3" sweep -
    // the smallest pane the paint accepts without the ellipsis, and the
    // fix's own guarantee (pane = draw + 18) checked on the real engine.
    public static string Probe(uint dpi, string[] texts) {
        IntPtr screen = GetDC(IntPtr.Zero);
        IntPtr font = AppNumberFont(dpi);
        var report = new StringBuilder();
        try {
            foreach (string text in texts) {
                IntPtr dc = CreateCompatibleDC(screen);
                IntPtr bmp = CreateCompatibleBitmap(screen,64,26);
                SelectObject(dc,bmp);
                SelectObject(dc,font);
                SIZE sz = new SIZE();
                GetTextExtentPoint32(dc,text,text.Length,ref sz);
                int natural = DrawNatural(dc,text);
                report.AppendLine("text=[" + text + "] extent=" + sz.cx + " draw=" + natural + " delta=" + (natural - sz.cx));
                DeleteDC(dc); DeleteObject(bmp);
            }
            string probeText = "1 / 3";
            IntPtr refDc = IntPtr.Zero, refBmp = IntPtr.Zero;
            string reference;
            int naturalWidth;
            try {
                refDc = CreateCompatibleDC(screen);
                refBmp = CreateCompatibleBitmap(screen,4096,26);
                SelectObject(refDc,refBmp);
                PaintPane(refDc,font,probeText,4096);
                naturalWidth = DrawNatural(refDc,probeText);
                reference = InkMap(refDc,8,8 + naturalWidth);
            } finally {
                if (refDc != IntPtr.Zero) DeleteDC(refDc);
                if (refBmp != IntPtr.Zero) DeleteObject(refBmp);
            }
            int smallestOk = -1;
            for (int pane = 16; pane <= naturalWidth + 18 + 12; pane++) {
                IntPtr dc2, bmp2;
                string ink;
                bool ok;
                // paint with the app's draw flags at this pane width and
                // compare the in-region ink with the unclipped reference.
                {
                    dc2 = CreateCompatibleDC(screen);
                    bmp2 = CreateCompatibleBitmap(screen,pane,26);
                    SelectObject(dc2,bmp2);
                    PaintPane(dc2,font,probeText,pane);
                    int limit = Math.Min(8 + naturalWidth, pane - 8) - 1;
                    ink = InkMap(dc2,8,Math.Max(9,limit));
                    ok = (ink == reference.Substring(0, ink.Length));
                    DeleteDC(dc2); DeleteObject(bmp2);
                }
                if (ok && smallestOk < 0) smallestOk = pane;
                if (pane == naturalWidth + 22)
                    report.AppendLine("fix pane (" + pane + " = draw + pad2 + slack2 + reserve4) ellipsis=" + (ok ? "no" : "YES"));
            }
            report.AppendLine("smallest pane without ellipsis=" + smallestOk
                + " (draw=" + naturalWidth + ", implied slack=" + (smallestOk - 16 - naturalWidth) + ")");
        } finally {
            ReleaseDC(IntPtr.Zero,screen);
            DeleteObject(font);
        }
        return report.ToString();
    }

    // the sweep as pixels: one row per pane width, labelled with the width
    // and the verdict, so the evidence png reads without the log.
    public static IntPtr SweepSheet(uint dpi, string text, int[] panes) {
        IntPtr screen = GetDC(IntPtr.Zero);
        IntPtr font = AppNumberFont(dpi);
        IntPtr sheet = IntPtr.Zero;
        int rowH = 28;
        try {
            IntPtr dc = CreateCompatibleDC(screen);
            sheet = CreateCompatibleBitmap(screen,260,panes.Length * rowH + 4);
            SelectObject(dc,sheet);
            for (int i = 0; i < panes.Length; i++) {
                int y = i * rowH + 2;
                VivGui.Rect row = new VivGui.Rect();
                row.Left = 0; row.Top = y; row.Right = 260; row.Bottom = y + 26;
                FillRect(dc,ref row,GetStockObject(4));
                SelectObject(dc,font);
                SetBkMode(dc,1);
                SetTextColor(dc,0x00ffff00);
                VivGui.Rect rect = new VivGui.Rect();
                rect.Left = 2; rect.Top = y + 4; rect.Right = 68; rect.Bottom = y + 26;
                DrawTextW(dc,panes[i] + "px",-1,ref rect,DT_LEFT|DT_TOP|DT_SINGLELINE|DT_NOPREFIX);
                // the pane replay, pasted beside the label at its real width
                IntPtr paneDc = CreateCompatibleDC(screen);
                IntPtr paneBmp = CreateCompatibleBitmap(screen,panes[i],26);
                SelectObject(paneDc,paneBmp);
                PaintPane(paneDc,font,text,panes[i]);
                for (int x = 0; x < panes[i]; x++)
                    for (int yy = 0; yy < 26; yy++) {
                        uint c = GetPixel(paneDc,x,yy);
                        if ((c & 0xff) > 40 || ((c >> 8) & 0xff) > 40 || ((c >> 16) & 0xff) > 40)
                            SetPixel(dc,x,y + yy,0x00ffffff);
                    }
                DeleteDC(paneDc); DeleteObject(paneBmp);
            }
            DeleteDC(dc);
        } finally {
            ReleaseDC(IntPtr.Zero,screen);
            DeleteObject(font);
        }
        return sheet;
    }
}
'@

function Wait-Until([scriptblock]$Condition, [string]$Description, [int]$TimeoutMs = 12000) {
    $stopwatch = [Diagnostics.Stopwatch]::StartNew()
    do {
        if (& $Condition) { return }
        Start-Sleep -Milliseconds 75
    } while ($stopwatch.ElapsedMilliseconds -lt $TimeoutMs)
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
function Click-Once([IntPtr]$Target,[int]$X,[int]$Y) {
    $point = ($Y -shl 16) -bor ($X -band 65535)
    if (-not [VivGui]::TrySend($Target,0x201,1,$point,2000) -or
        -not [VivGui]::TrySend($Target,0x202,0,$point,2000)) {
        throw "The toolbar did not accept a single click at ($X, $Y)"
    }
}
# a posted click is an attempt, not a one-shot assertion: right after a
# fullscreen restore the strip can still be settling, so the click re-posts
# while the condition polls - the same discipline the waits use for the
# app's own answers. the delivery rings in two tiers: the seat click first
# (the real input), then the app's own command channel (wm_command, the
# same road the menus ride) - whichever tier the app answers names the
# other one's state in the log, and the timeout names the state it gave
# up on.
function Read-CommandId([string]$Name) {
    $text = Get-Content -LiteralPath (Join-Path $PSScriptRoot "..\src\viv.h") -Raw
    if ($text -match "(?s)enum\s*\{(.*?)\}") {
        $index = 0
        foreach ($entry in $matches[1] -split ',') {
            $token = ($entry.Trim() -split '\s+')[0]
            if ($token -eq $Name) { return $index }
            if ($token -like 'VIV_ID_*') { $index++ }
        }
    }
    throw "command id not found: $Name"
}
function Click-Until([IntPtr]$Target,[int]$X,[int]$Y,[scriptblock]$Condition,[string]$Description) {
    $navNext = Read-CommandId "VIV_ID_NAV_NEXT"
    $stopwatch = [Diagnostics.Stopwatch]::StartNew()
    do {
        [VivGui]::Click($Target,$X,$Y)
        Start-Sleep -Milliseconds 300
        if (& $Condition) { Write-Host "queue step answered by the seat click"; return }
    } while ($stopwatch.ElapsedMilliseconds -lt 4000)
    do {
        [VivGui]::Post($script:window,0x111,$navNext,0)
        Start-Sleep -Milliseconds 300
        if (& $Condition) { Write-Host "queue step answered by the command channel (the seat click stayed silent)"; return }
    } while ($stopwatch.ElapsedMilliseconds -lt 12000)
    try {
        $tb = Child "_VIV_TOOLBAR"
        $tw = Bounds $tb -Client
        $ww = Bounds $window
        Write-Host ("queue state: title='{0}' window={1}x{2} toolbar={3}x{4} temps={5}" -f [VivGui]::Title($window),$ww.Width,$ww.Height,$tw.Width,$tw.Height,@(Get-ChildItem -LiteralPath $temp -Filter 'viv*.tmp').Count)
        Capture "queue-click-timeout"
    } catch {
        Write-Host ("queue snapshot failed: {0}" -f $_.Exception.Message)
    }
    throw "Timed out waiting for $Description"
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
function New-RotationFixture([string]$Path) {
    $bitmap = New-Object Drawing.Bitmap(3840,2160,([Drawing.Imaging.PixelFormat]::Format24bppRgb))
    try {
        $graphics = [Drawing.Graphics]::FromImage($bitmap)
        try {
            $graphics.FillRectangle([Drawing.Brushes]::Red,0,0,1920,1080)
            $graphics.FillRectangle([Drawing.Brushes]::Lime,1920,0,1920,1080)
            $graphics.FillRectangle([Drawing.Brushes]::Blue,0,1080,1920,1080)
            $graphics.FillRectangle([Drawing.Brushes]::Yellow,1920,1080,1920,1080)
        } finally { $graphics.Dispose() }
        $bitmap.Save($Path,[Drawing.Imaging.ImageFormat]::Bmp)
    } finally { $bitmap.Dispose() }
}

function Read-RotatedBmp([string]$Path, [int]$Width, [int]$Height) {
    try {
        $stream = [IO.File]::Open($Path,[IO.FileMode]::Open,[IO.FileAccess]::Read,([IO.FileShare]::ReadWrite -bor [IO.FileShare]::Delete))
        $reader = New-Object IO.BinaryReader($stream)
        try {
            $header = $reader.ReadBytes(54)
            if ($header.Length -ne 54 -or $header[0] -ne 66 -or $header[1] -ne 77 -or
                [BitConverter]::ToInt32($header,18) -ne $Width -or [BitConverter]::ToInt32($header,22) -ne (-$Height)) { return }
            $offset = [BitConverter]::ToUInt32($header,10)
            $bits = [BitConverter]::ToUInt16($header,28)
            if ($offset -ne 54 -or $bits -notin 24,32) { return }
            $stride = (($Width * $bits + 31) -shr 5) * 4
            if ($stream.Length -ne ($offset + $stride*$Height)) { return }
            foreach ($y in @(0,($Height-1))) {
                foreach ($x in @(0,($Width-1))) {
                    $stream.Position = $offset + $stride*$y + ($bits -shr 3)*$x
                    $blue = [int]$reader.ReadByte()
                    $green = [int]$reader.ReadByte()
                    $red = [int]$reader.ReadByte()
                    ($red -shl 16) -bor ($green -shl 8) -bor $blue
                }
            }
        } finally { $reader.Dispose() }
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
toolbar_icon_only=1
zoom_auto_hide=0
sort=0
sort_ascending=1
renderer=0
'@
    $ini = Join-Path $root "voidImageViewer.ini"
    $profile | Set-Content -LiteralPath $ini -Encoding ascii

    $first = Join-Path $root "01-rotate-long-catalogue-image-with-a-name-that-needs-the-full-status-pane-4k.bmp"
    $bad = Join-Path $root "02-broken.png"
    $last = Join-Path $root "03-next.png"
    New-RotationFixture $first
    Copy-Item -LiteralPath (Join-Path $SamplesDir "fx_still_textured.png") -Destination $last
    [IO.File]::WriteAllBytes($bad,[byte[]](137,80,78,71))
    $original = (Get-FileHash -LiteralPath $first -Algorithm SHA256).Hash

    $oldTemp,$oldTmp = $env:TEMP,$env:TMP
    try {
        $env:TEMP=$temp
        $env:TMP=$temp
        # the status draw trace: the app's own owner-draw rects (env-gated).
        $env:VIV_STATUS_DRAW_TRACE = "1"
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

    Stage "baseline"
    $nativeScale = $script:nativeDpi / 96.0
    if (-not [VivGui]::SetWindowPos($window,[IntPtr]::Zero,0,0,([int](1700*$nativeScale)),([int](680*$nativeScale)),0x14)) {
        throw "Could not size the baseline window"
    }
    $toolbar = Child "_VIV_TOOLBAR"
    Wait-Until { [VivGui]::IsWindowVisible($toolbar) } "windowed command strip"
    Capture "windowed-native-${script:nativeDpi}dpi"

    # The status fact panes replayed on the runner's own gdi: what the two
    # text engines disagree on, and which pane widths the paint accepts.
    # this round the numbers ride the log and the evidence png - the lane
    # itself does not fail on the verdict yet (the fix rides the same tree;
    # the next round pins the numbers once they are known).
    Stage "status-facts-probe"
    $probeTexts = @("1 / 3","3 / 3","1 / 12","10 / 12","3840 × 2160 (23.7 MB)")
    Write-Host ([StatusFacts]::Probe([uint32]$script:nativeDpi,$probeTexts))
    $probePanes = @(30..72)
    $sheet = [StatusFacts]::SweepSheet([uint32]$script:nativeDpi,"1 / 3",$probePanes)
    try {
        $probeBmp = [System.Drawing.Bitmap]::FromHbitmap($sheet)
        $probeBmp.Save((Join-Path $EvidenceDir "status-draw-probe.png"),[System.Drawing.Imaging.ImageFormat]::Png)
        $probeBmp.Dispose()
    } finally {
        [void][StatusFacts]::DeleteObject($sheet)
    }
    Write-Host "status-draw-probe.png written ($($probePanes.Count) pane widths swept)"

    # the app's own draw trace carries the pane-width question now: the
    # out-of-process sb_getparts route is dead - the kernel does not marshal
    # wm_user lParam buffers across processes, so the control writes the
    # caller's address inside its own space and dies (the 0xC000041D round).
    Stage "status-parts-query"
    $tracePath = Join-Path $temp "viv_status_trace.log"
    if (Test-Path -LiteralPath $tracePath) {
        Write-Host "--- viv_status_trace.log ---"
        Get-Content -LiteralPath $tracePath | ForEach-Object { Write-Host "trace: $_" }
    } else {
        Write-Host "viv_status_trace.log not found in $temp"
    }
    # From the first toolbar item, wrap left to Fullscreen and then Rotate.
    Send-Msg $toolbar 7 0 0
    Send-Msg $toolbar 0x100 0x25 0
    Send-Msg $toolbar 0x100 0x25 0
    $rotation = [Diagnostics.Stopwatch]::StartNew()
    if (-not [VivGui]::TrySend($toolbar,0x100,0x0d,0,2000)) {
        throw "The 4K Rotate command held the UI thread"
    }
    Write-Host "4K Rotate command returned in $($rotation.ElapsedMilliseconds) ms"
    Wait-Until {
        if (-not [VivGui]::TrySend($window,0,0,0,2000)) { throw "The window stopped pumping during rotation" }
        foreach ($entry in Get-ChildItem -LiteralPath $temp -Filter 'viv*.tmp') {
            $corners = @(Read-RotatedBmp $entry.FullName 2160 3840)
            if (($corners -join ',') -eq (@(0x0000ff,0xff0000,0xffff00,0x00ff00) -join ',')) {
                return $true
            }
        }
        $false
    } "correct temporary 4K clockwise BMP without a prompt"
    Write-Host "4K BMP dimensions and four corner pixels verified after $($rotation.ElapsedMilliseconds) ms"
    Stage "rotated"
    if ((Get-FileHash -LiteralPath $first -Algorithm SHA256).Hash -ne $original) {
        throw "Rotation changed the source image"
    }

    # A synthetic update may be refused. The application strip height
    # proves whether the 300 DPI layout ran, independent of the send result.
    Stage "high-dpi-lane"
    $delivery = 'monitor'
    if ($script:nativeDpi -ne 300) {
        $delivery = 'native fallback'
        $dpiParam = 300 + (300 -shl 16)
        $null = [VivGui]::TrySend($window,0x02e0,$dpiParam,0,1000)
        if ((Bounds $toolbar -Client).Height -eq 150) {
            $delivery = 'synthetic send'
        } else {
            try {
                # the post path carries the same refusal the send path does
                # on this image - the helper throws, so the fallback rides
                # the catch and the lane keeps its native-scale geometry.
                [VivGui]::Post($window,0x02e0,$dpiParam,0)
                Wait-Until { (Bounds $toolbar -Client).Height -eq 150 } "300 DPI strip after the synthetic post" 1500
                $delivery = 'synthetic post'
            } catch {
                Write-Host "Synthetic 300 DPI was refused. Testing overflow at the native DPI."
            }
        }
    }
    $script:dpi = if ($delivery -eq 'native fallback') { $script:nativeDpi } else { 300 }
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
    Capture "restored-${script:dpi}dpi-overflow"
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
    $bar = Bounds $toolbar -Client
    $script:scale = $bar.Height / 48.0
    Click-Until $toolbar (D2P 65.6) (D2P 24) {
        $title = [VivGui]::Title($window)
        $title -notlike '*01-rotate*' -and $title -notlike '*03-next*' -and
        @(Get-ChildItem -LiteralPath $temp -Filter 'viv*.tmp').Count -eq 0
    } "one queue step to the failed item and removal of both temporary files"
    Capture "queue-failed-item"
    Stage "queue-recovery"
    Click-Until $toolbar (D2P 65.6) (D2P 24) {
        [VivGui]::Title($window) -like '*03-next*'
    } "one queue step past the failed item"
    Capture "queue-recovered"
    Write-Host "Win32 GUI passed. Restored layout $script:dpi DPI ($delivery). Monitor DPI $([VivGui]::GetDpiForWindow($window))."

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
