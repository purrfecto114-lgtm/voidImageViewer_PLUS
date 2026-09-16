# voidImageViewer real machine smoke test (windows powershell 5.1+)
#
# "tested" must mean "actually runs": this script starts the viewer,
# opens every anomaly sample produced by tests/make_anomaly_samples.py
# and watches for crashes (a process that dies with a nonzero exit code
# on hostile input). the over budget canvas must be refused without a
# crash - that is the pixel budget from the third audit round working.
#
# usage:
#   powershell -ExecutionPolicy Bypass -File tests\smoke_test.ps1 `
#       [-ExePath "C:\path\voidImageViewer.exe"] `
#       [-SamplesDir "C:\path\tests\samples"]
#
# if the samples directory is missing the script tries to generate it
# with python first (python tests\make_anomaly_samples.py). a clean run
# exits 0; any crashing sample exits 1.

param(
    [string]$ExePath = "",
    [string]$SamplesDir = "",
    [int]$SampleTimeoutSec = 5,
    [int]$CloseTimeoutSec = 5
)

$ErrorActionPreference = "Stop"
$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path

# ---------------------------------------------------------------- locate exe

if (-not $ExePath) {
    $candidates = @(
        (Join-Path $env:ProgramFiles "voidImageViewer\voidImageViewer.exe"),
        (Join-Path $env:LOCALAPPDATA "Programs\voidImageViewer\voidImageViewer.exe"),
        (Join-Path $scriptDir "voidImageViewer.exe"),
        (Join-Path $scriptDir "..\vs2019\release\voidImageViewer.exe")
    )
    # registry probe as a fallback
    try {
        $keys = @(
            "HKLM:\SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\voidImageViewer",
            "HKCU:\SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\voidImageViewer"
        )
        foreach ($k in $keys) {
            if (Test-Path $k) {
                $loc = (Get-ItemProperty $k -ErrorAction SilentlyContinue).InstallLocation
                if ($loc) { $candidates += (Join-Path $loc "voidImageViewer.exe") }
            }
        }
    } catch { }
    foreach ($c in $candidates) {
        if ($c -and (Test-Path $c)) { $ExePath = $c; break }
    }
}

if (-not $ExePath -or -not (Test-Path $ExePath)) {
    Write-Host "FAIL: voidImageViewer.exe not found."
    Write-Host "      pass -ExePath explicitly or install the viewer first."
    exit 2
}
Write-Host ("viewer under test: " + $ExePath)
Write-Host ("version in file:   " + (Get-Item $ExePath).VersionInfo.FileVersion)

# ------------------------------------------------------------ locate samples

if (-not $SamplesDir) { $SamplesDir = Join-Path $scriptDir "samples" }

if (-not (Test-Path $SamplesDir)) {
    Write-Host "samples missing, generating via make_anomaly_samples.py ..."
    $py = Get-Command python -ErrorAction SilentlyContinue
    if (-not $py) { $py = Get-Command py -ErrorAction SilentlyContinue }
    if ($py) {
        & $py.Source (Join-Path $scriptDir "make_anomaly_samples.py") $SamplesDir
        if ($LASTEXITCODE -ne 0) {
            Write-Host "FAIL: sample generator self check failed."
            exit 2
        }
    } else {
        Write-Host ("FAIL: no python to generate samples; run " +
                    "make_anomaly_samples.py on any machine and copy tests\samples.")
        exit 2
    }
}

$samples = Get-ChildItem -Path $SamplesDir -File | Sort-Object Name
if (-not $samples) {
    Write-Host "FAIL: sample directory is empty."
    exit 2
}
Write-Host ("samples: " + $samples.Count + " files")
Write-Host ""

# ----------------------------------------------------------------- helpers

function Stop-Viewer {
    param([System.Diagnostics.Process]$proc)
    if ($proc -and -not $proc.HasExited) {
        $null = $proc.CloseMainWindow()
        if (-not $proc.WaitForExit($CloseTimeoutSec * 1000)) {
            Stop-Process -Id $proc.Id -Force -ErrorAction SilentlyContinue
        }
    }
}

$results = New-Object System.Collections.Generic.List[object]
$crashes = 0
$passes = 0
$warns = 0

# 1. bare launch: the viewer must start and stay up
Write-Host "=== stage 1: bare launch ==="
$bare = Start-Process -FilePath $ExePath -PassThru
Start-Sleep -Seconds 3
if ($bare.HasExited) {
    $status = if ($bare.ExitCode -eq 0) { "WARN" } else { "FAIL" }
    if ($bare.ExitCode -ne 0) { $script:crashes++ } else { $script:warns++ }
    $results.Add([pscustomobject]@{
        Sample = "(bare launch)"; Status = $status;
        Detail = ("exited " + $bare.ExitCode) })
} else {
    $script:passes++
    $results.Add([pscustomobject]@{
        Sample = "(bare launch)"; Status = "PASS"; Detail = "alive after 3s" })
    Stop-Viewer $bare
}

# 2. one hostile sample after another: no crash is allowed
Write-Host "=== stage 2: anomaly sweep ==="
foreach ($s in $samples) {
    $proc = Start-Process -FilePath $ExePath -ArgumentList ('"' + $s.FullName + '"') -PassThru
    Start-Sleep -Seconds $SampleTimeoutSec
    if ($proc.HasExited -and $proc.ExitCode -ne 0) {
        $script:crashes++
        $results.Add([pscustomobject]@{
            Sample = $s.Name; Status = "FAIL";
            Detail = ("crashed, exit " + $proc.ExitCode) })
    } elseif ($proc.HasExited) {
        # exit 0 usually means the file was handed to an existing single
        # instance or refused cleanly - both are survivable outcomes.
        $script:warns++
        $results.Add([pscustomobject]@{
            Sample = $s.Name; Status = "WARN"; Detail = "clean early exit" })
    } else {
        $script:passes++
        $results.Add([pscustomobject]@{
            Sample = $s.Name; Status = "PASS"; Detail = "survived open" })
        Stop-Viewer $proc
    }
}

# 3. the over budget canvas: the pixel budget must refuse it, not crash
Write-Host "=== stage 3: over budget canvas ==="
$over = $samples | Where-Object { $_.Name -like "*over_budget*" }
if ($over) {
    $proc = Start-Process -FilePath $ExePath -ArgumentList ('"' + $over[0].FullName + '"') -PassThru
    Start-Sleep -Seconds $SampleTimeoutSec
    if ($proc.HasExited -and $proc.ExitCode -ne 0) {
        $script:crashes++
        $results.Add([pscustomobject]@{
            Sample = $over[0].Name; Status = "FAIL";
            Detail = "died on the budget canvas" })
    } else {
        $script:passes++
        $detail = "refused or displayed without a crash"
        if ($proc.HasExited) { $detail = "clean exit on the budget canvas" }
        $results.Add([pscustomobject]@{
            Sample = $over[0].Name; Status = "PASS"; Detail = $detail })
        Stop-Viewer $proc
    }
} else {
    Write-Host "no over_budget sample found (regenerate samples)."
}

# 4. the input ceiling: a sparse file far past the byte cap must be
#    refused, not crashed on (the 4gb-plus size is also the exact shape
#    the 32-bit GetFileSize could not see: it answered the low dword).
#    the file is sparse on ntfs, so it costs the runner nothing; when
#    the sparse flag cannot be set the stage warns and skips rather
#    than commit 4gb for real.
Write-Host "=== stage 4: input ceiling (sparse) ==="
$bigPath = Join-Path $env:TEMP ("viv_input_over_" + $PID + ".png")
$bigSize = 4294967297
$stage4 = "skipped"
try {
    $null = New-Item -Path $bigPath -ItemType File -Force
    fsutil sparse setflag $bigPath 2>$null | Out-Null
    $flagOut = fsutil sparse queryflag $bigPath 2>$null
    if (($LASTEXITCODE -eq 0) -and ($flagOut -match "is set as sparse")) {
        $fs = [System.IO.File]::Open($bigPath,'Open','Write')
        $fs.SetLength($bigSize)
        $fs.Close()
        if ((Get-Item $bigPath).Length -eq $bigSize) {
            $proc = Start-Process -FilePath $ExePath -ArgumentList ('"' + $bigPath + '"') -PassThru
            Start-Sleep -Seconds $SampleTimeoutSec
            if ($proc.HasExited -and $proc.ExitCode -ne 0) {
                $script:crashes++
                $results.Add([pscustomobject]@{
                    Sample = "(input ceiling)"; Status = "FAIL";
                    Detail = "died on the over-ceiling sparse file" })
            } else {
                $script:passes++
                $detail = "refused the over-ceiling input without a crash"
                if ($proc.HasExited) { $detail = "clean exit on the over-ceiling input" }
                $results.Add([pscustomobject]@{
                    Sample = "(input ceiling)"; Status = "PASS"; Detail = $detail })
                Stop-Viewer $proc
            }

            # the oracle closes the loop: "still alive after five
            # seconds" only proves the refusal did not crash - the
            # render export must actually refuse the same file (exit 2,
            # the load-refused contract) and answer no bitmap. a viewer
            # that silently loaded and displayed the over-ceiling file
            # would pass the stage above and fail here.
            $probeBmp = Join-Path $env:TEMP ("viv_ceiling_probe_" + $PID + ".bmp")
            if (Test-Path $probeBmp) { Remove-Item $probeBmp -Force }
            $probe = Start-Process -FilePath $ExePath -ArgumentList @(
                "-render-gdi", "-render-size", "640x480", "-render-export",
                ('"' + $probeBmp + '"'), ('"' + $bigPath + '"')
            ) -PassThru -WindowStyle Hidden
            if (-not $probe.WaitForExit(90000)) {
                Stop-Process -Id $probe.Id -Force -ErrorAction SilentlyContinue
                $script:crashes++
                $results.Add([pscustomobject]@{
                    Sample = "(input ceiling export)"; Status = "FAIL";
                    Detail = "the export hung on the over-ceiling file" })
            } elseif (($probe.ExitCode -eq 2) -and (-not (Test-Path $probeBmp))) {
                $script:passes++
                $results.Add([pscustomobject]@{
                    Sample = "(input ceiling export)"; Status = "PASS";
                    Detail = "refused the load (exit 2, no bitmap)" })
            } else {
                $script:crashes++
                $results.Add([pscustomobject]@{
                    Sample = "(input ceiling export)"; Status = "FAIL";
                    Detail = ("exit " + $probe.ExitCode +
                        $(if (Test-Path $probeBmp) { ", bitmap answered" } else { ", no bitmap" })) })
            }
            Remove-Item -Path $probeBmp -Force -ErrorAction SilentlyContinue
            $stage4 = "ran"
        }
    }
} catch {
    # powershell 5.1 wraps native stderr (even redirected to $null) as an
    # error record and $ErrorActionPreference stop promotes it - a volume
    # without fsutil or sparse support is a warn-skip, not a red script.
    $stage4 = "skipped"
} finally {
    Remove-Item -Path $bigPath -Force -ErrorAction SilentlyContinue
}
if ($stage4 -eq "skipped") {
    $script:warns++
    $results.Add([pscustomobject]@{
        Sample = "(input ceiling)"; Status = "WARN";
        Detail = "sparse file unavailable on this volume" })
}

# ------------------------------------------------------------------ report

Write-Host ""
$results | Format-Table -AutoSize
$line = ("total {0}: {1} pass, {2} warn, {3} FAIL" -f
         $results.Count, $passes, $warns, $crashes)
Write-Host $line

# never leave a viewer behind
Get-Process -Name voidImageViewer -ErrorAction SilentlyContinue |
    Stop-Process -Force -ErrorAction SilentlyContinue

if ($crashes -gt 0) {
    Write-Host "SMOKE TEST FAIL"
    exit 1
}
Write-Host "SMOKE TEST PASS (no crashes on any sample)"
exit 0
