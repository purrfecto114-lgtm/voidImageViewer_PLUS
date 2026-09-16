# voidImageViewer PE security header check (windows powershell 5.1+)
#
# the machine verification the external audit asked for: after the build
# run dumpbin and assert the ASLR, DEP and CFG bits are present - do not
# merely assert the exe exists. this script is that check without the
# dumpbin dependency (no vcvars environment to drag onto the runner): a
# pure powershell parse of the PE headers asserting the hardening bits
# the linker is supposed to stamp into the image.
#
# asserted (DllCharacteristics bits, optional header offset 70 - the same
# offset for PE32 and PE32+, the arithmetic is pinned in the comment at
# the constants below):
#   0x0040 DYNAMIC_BASE     aslr
#   0x0100 NX_COMPAT        dep
#   0x4000 GUARD_CF         control flow guard
#   0x0020 HIGH_ENTROPY_VA  additionally required on PE32+ (x64)
#
# reported for information (never asserted): the COFF machine, the
# subsystem and whether a LOAD_CONFIG data directory rides the image
# (data directory index 10 - the cfg completeness signal: the guard cf
# bit without a load config table is a stamp without the table).
#
# usage:
#   powershell -ExecutionPolicy Bypass -File tools\pe_security_check.ps1 `
#       -ExePath build\x64\voidImageViewer.exe
#   multiple files ride -Command with the call operator: a -File
#   command line delivers "a","b" as one joined string and the
#   [string[]] binding never splits it (the step's first ci run
#   failed on exactly that), while 'a','b' inside -Command
#   parses as the array the parameter wants:
#   powershell -ExecutionPolicy Bypass -Command `
#       "& tools\pe_security_check.ps1 -ExePath 'exe1.exe','exe2.exe'"
#
# a missing mitigation prints a github actions annotation
# (::error::<file> is missing <bits>) and exits 1. exit 0 only when
# every file carries every required bit. exit 2 is a setup error (a
# path that is not a file).

param(
    [Parameter(Mandatory = $true)]
    [string[]]$ExePath
)

$ErrorActionPreference = "Stop"

# ---------------------------------------------------------------- constants

# DllCharacteristics lives at optional header offset 70 for BOTH magics:
#   magic(2) linker version(2) size of code(4) size of initialized data(4)
#   size of uninitialized data(4) entry point(4) base of code(4)
#   [PE32 only: base of data(4)] image base(4 in PE32, 8 in PE32+ - the
#   pair sums to 8 bytes either way, both magics land on offset 32 here)
#   section alignment(4) file alignment(4) os version(4) image version(4)
#   subsystem version(4) win32 version(4) size of image(4) size of
#   headers(4) checksum(4) subsystem(2 at 68) - dll characteristics at 70.
$DllCharacteristicsOffset = 70

$MaskHighEntropyVA = 0x0020    # IMAGE_DLLCHARACTERISTICS_HIGH_ENTROPY_VA
$MaskDynamicBase   = 0x0040    # IMAGE_DLLCHARACTERISTICS_DYNAMIC_BASE
$MaskNxCompat      = 0x0100    # IMAGE_DLLCHARACTERISTICS_NX_COMPAT
$MaskGuardCf       = 0x4000    # IMAGE_DLLCHARACTERISTICS_GUARD_CF

$machineNames = @{
    0x014C = "i386";  0x8664 = "AMD64"; 0xAA64 = "ARM64"
    0x01C4 = "ARMNT"; 0x0200 = "IA64"
}
$subsystemNames = @{
    1 = "NATIVE";              2 = "WINDOWS_GUI"
    3 = "WINDOWS_CUI";         5 = "OS2_CUI"
    7 = "POSIX_CUI";           9 = "WINDOWS_CE_GUI"
    10 = "EFI_APPLICATION";    11 = "EFI_BOOT_SERVICE_DRIVER"
    12 = "EFI_RUNTIME_DRIVER"; 13 = "EFI_ROM"
}

# ------------------------------------------------------------------ parsing

# reads one PE image and answers every field this check needs. throws on
# structural damage (a truncated header, a missing signature, an optional
# header magic this parser does not know) - the caller decides how to
# report it.
function Get-PeSecurityInfo {
    param([string]$Path)

    $bytes = [System.IO.File]::ReadAllBytes($Path)

    # dos header: the mz magic, then e_lfanew (int32) at 0x3C
    if ($bytes.Length -lt 0x40) { throw "too small for a dos header" }
    if ($bytes[0] -ne 0x4D -or $bytes[1] -ne 0x5A) { throw "no MZ magic" }
    $peOff = [BitConverter]::ToInt32($bytes, 0x3C)
    if ($peOff -le 0) { throw "e_lfanew out of range" }
    # room for the pe signature, the 20 byte coff header and the optional
    # header through DllCharacteristics (offset 70 + 2 bytes)
    if (($peOff + 4 + 20 + 72) -gt $bytes.Length) {
        throw "pe headers truncated"
    }
    if ($bytes[$peOff] -ne 0x50 -or $bytes[$peOff + 1] -ne 0x45 -or
        $bytes[$peOff + 2] -ne 0x00 -or $bytes[$peOff + 3] -ne 0x00) {
        throw "no PE signature"
    }

    # coff header (20 bytes): machine at +0, size of optional header at +16
    $machine = [int][BitConverter]::ToUInt16($bytes, $peOff + 4)
    $sizeOfOptional = [BitConverter]::ToUInt16($bytes, $peOff + 4 + 16)
    $optOff = $peOff + 4 + 20

    $magic = [int][BitConverter]::ToUInt16($bytes, $optOff)
    if ($magic -eq 0x20B) { $pe32plus = $true }
    elseif ($magic -eq 0x10B) { $pe32plus = $false }
    else { throw ("unsupported optional header magic 0x{0:X4}" -f $magic) }

    if ($sizeOfOptional -lt 72) {
        throw "optional header too small for DllCharacteristics"
    }

    # subsystem at optional header offset 68, DllCharacteristics at 70
    $subsystem = [int][BitConverter]::ToUInt16($bytes, $optOff + 68)
    $dllChars = [int][BitConverter]::ToUInt16(
        $bytes, $optOff + $DllCharacteristicsOffset)

    # data directories: PE32 starts at optional header offset 96, PE32+ at
    # 112 (the wider stack and heap reserve/commit fields); the count sits
    # right before them. index 10 is LOAD_CONFIG - present when its
    # address or size is non-zero.
    $ddOff = $optOff + 96
    if ($pe32plus) { $ddOff = $optOff + 112 }
    $loadConfig = $false
    $numDirs = 0
    if (($ddOff + 4) -le $bytes.Length) {
        $numDirs = [BitConverter]::ToUInt32($bytes, $ddOff - 4)
    }
    if ($numDirs -ge 11 -and ($ddOff + 80 + 8) -le $bytes.Length) {
        $va = [BitConverter]::ToUInt32($bytes, $ddOff + 80)
        $sz = [BitConverter]::ToUInt32($bytes, $ddOff + 84)
        if ($va -ne 0 -or $sz -ne 0) { $loadConfig = $true }
    }

    return @{
        Pe32Plus   = $pe32plus
        Machine    = $machine
        Subsystem  = $subsystem
        DllChars   = $dllChars
        LoadConfig = $loadConfig
    }
}

# ---------------------------------------------------------------- the check

# pre-flight: every path must name a file - a broken invocation is a
# setup error (exit 2, the smoke test's convention), not a verdict
foreach ($p in $ExePath) {
    if (-not $p -or -not (Test-Path -LiteralPath $p -PathType Leaf)) {
        Write-Host ("FAIL: file not found: " + $p)
        exit 2
    }
}

$failed = 0

foreach ($p in $ExePath) {
    try {
        $pe = Get-PeSecurityInfo -Path $p
    } catch {
        $failed++
        Write-Host ("FAIL: {0}: {1}" -f $p, $_.Exception.Message)
        Write-Host ("::error::{0} is not a parsable PE image ({1})" -f `
            $p, $_.Exception.Message)
        continue
    }

    $dll = $pe.DllChars
    $aslr = ($dll -band $MaskDynamicBase) -ne 0
    $heva = ($dll -band $MaskHighEntropyVA) -ne 0
    $dep  = ($dll -band $MaskNxCompat) -ne 0
    $cfg  = ($dll -band $MaskGuardCf) -ne 0

    $missing = @()
    if (-not $aslr) { $missing += "DYNAMIC_BASE" }
    if ($pe.Pe32Plus -and -not $heva) { $missing += "HIGH_ENTROPY_VA" }
    if (-not $dep) { $missing += "NX_COMPAT" }
    if (-not $cfg) { $missing += "GUARD_CF" }

    $peKind = "PE32+"
    if (-not $pe.Pe32Plus) { $peKind = "PE32" }
    $machineText = $machineNames[$pe.Machine]
    if (-not $machineText) { $machineText = "unknown" }
    $machineText = ("{0}(0x{1:X4})" -f $machineText, $pe.Machine)
    $subsysText = $subsystemNames[$pe.Subsystem]
    if (-not $subsysText) { $subsysText = "unknown" }
    $subsysText = ("{0}({1})" -f $subsysText, $pe.Subsystem)
    $hevaText = "n/a"
    if ($pe.Pe32Plus) { $hevaText = $(if ($heva) { "yes" } else { "no" }) }
    $lcText = $(if ($pe.LoadConfig) { "present" } else { "absent" })

    Write-Host (("{0}: {1} machine={2} ASLR={3} high-entropy-VA={4}" +
                 " DEP={5} CFG={6} subsystem={7} load-config={8}") -f `
        $p, $peKind, $machineText, `
        $(if ($aslr) { "yes" } else { "no" }), $hevaText, `
        $(if ($dep) { "yes" } else { "no" }), `
        $(if ($cfg) { "yes" } else { "no" }), `
        $subsysText, $lcText)

    if ($missing.Count -gt 0) {
        $failed++
        Write-Host ("      missing: {0}" -f ($missing -join ", "))
        Write-Host ("::error::{0} is missing {1}" -f $p, ($missing -join ", "))
        if ($pe.LoadConfig -and -not $cfg) {
            Write-Host ("      note: a LOAD_CONFIG data directory is present " +
                        "but the GUARD_CF bit is not stamped")
        }
        continue
    }

    if ($cfg -and -not $pe.LoadConfig) {
        Write-Host ("      note: GUARD_CF is stamped but no LOAD_CONFIG data " +
                    "directory rides the image (the bit without the table)")
    }
}

Write-Host ""
if ($failed -gt 0) {
    Write-Host ("PE SECURITY CHECK FAIL ({0} of {1} files failed)" -f `
        $failed, $ExePath.Count)
    exit 1
}
Write-Host ("PE SECURITY CHECK PASS ({0} files carry every required bit)" -f `
    $ExePath.Count)
exit 0
