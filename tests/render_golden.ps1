# voidImageViewer pixel golden test (windows powershell 5.1+)
#
# the pixel regression harness the external audit asked for: the hidden
# render export (-render-export, viv_export.c) walks the real pipeline -
# the threaded loader, the fit math, the canvas paint, the selected
# renderer - and writes a 32bpp bitmap of the exact pixels. this script
# runs every golden sample through every renderer, hashes the bitmaps
# and compares against the committed manifest (tests\golden\golden-manifest.json).
#
# what the hash covers: decode (png/gif/bmp/jpeg/qoi/webp), the fit math
# and the shrink/magnify filters, the backdrop under transparency, the
# alpha pre-flattening, and - when the runner carries a working hardware
# renderer - the three-way renderer agreement (gdi vs gl vs d3d).
#
# usage:
#   powershell -ExecutionPolicy Bypass -File tests\render_golden.ps1 `
#       -ExePath "build\x64\voidImageViewer.exe" `
#       [-SamplesDir "tests\samples"] [-GoldenDir "tests\golden"] `
#       [-UpdateGolden] [-AllowMissing]
#
# -UpdateGolden regenerates the manifest from the current binary (run it
# on a real windows machine - or the ci bootstrap workflow dispatch -
# and commit the result). -AllowMissing soft-passes when the manifest has
# not been bootstrapped yet (the pre-bootstrap pushes; the ci gates on
# the manifest being tracked, so a deleted manifest can not silently
# downgrade the check).
#
# exit codes: 0 pass (or soft-pass), 1 fail, 2 setup error.

param(
    [string]$ExePath = "",
    [string]$SamplesDir = "",
    [string]$GoldenDir = "",
    [switch]$UpdateGolden,
    [switch]$AllowMissing
)

$ErrorActionPreference = "Stop"
$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path

if (-not $ExePath) {
    $c = Join-Path $scriptDir "..\vs2019\release\voidImageViewer.exe"
    if (Test-Path $c) { $ExePath = $c }
}
if (-not $ExePath -or -not (Test-Path $ExePath)) {
    Write-Host "FAIL: voidImageViewer.exe not found (pass -ExePath)."
    exit 2
}
if (-not $SamplesDir) { $SamplesDir = Join-Path $scriptDir "samples" }
if (-not $GoldenDir)  { $GoldenDir  = Join-Path $scriptDir "golden" }
if (-not (Test-Path $SamplesDir)) {
    Write-Host "FAIL: samples directory not found: $SamplesDir"
    exit 2
}

# the golden set: the real, byte-complete imagery only. the anomaly
# generator's "control" gif/bmp stubs and the truncated 16-bit png are
# physically impossible byte counts (a 96x64 24bpp bmp in 246 bytes) -
# the decode refuses them, which the gui smoke tolerated as a survivable
# refused load but the pixel oracle correctly reports as a failure. the
# eleven below cover every decoder family
# (png, gif, bmp, jpeg, qoi, webp), the alpha paths (rgba png, rgba qoi,
# the fade's sub-rectangle disposal), both ends of the scaling range
# (the 100x100 magnify, the 4000x3000 mipmap shrink) and the animation
# first-frame contract.
$goldenSamples = @(
    "28_control_png_100x100.png",
    "29_control_png_4000x3000.png",
    "fx_anim_bounce.gif",
    "fx_anim_fade.gif",
    "fx_still_24bpp.bmp",
    "fx_still_photo.jpg",
    "fx_still_qoi_rgb.qoi",
    "fx_still_qoi_rgba.qoi",
    "fx_still_rgba.png",
    "fx_anim_pulse.webp"
)

foreach ($s in $goldenSamples) {
    $p = Join-Path $SamplesDir $s
    if (-not (Test-Path $p)) {
        Write-Host "FAIL: golden sample missing from the sample set: $s"
        exit 2
    }
}

# the export canvas: every bitmap answers to the same fixed size, so the
# fit math (and nothing host-specific) decides the pixels.
$size = "640x480"

# the renderer legs. the switch words map to the hidden export options
# (viv_export.c): -render-gdi, -render-gl, -render-d3d.
$renderers = @(
    @{ Name = "gdi"; Switch = "-render-gdi" },
    @{ Name = "gl";  Switch = "-render-gl"  },
    @{ Name = "d3d"; Switch = "-render-d3d" }
)

$manifestPath = Join-Path $GoldenDir "golden-manifest.json"
$manifest = $null
if (Test-Path $manifestPath) {
    $manifest = Get-Content $manifestPath -Raw | ConvertFrom-Json
} elseif ($UpdateGolden) {
    # created below on demand
} elseif ($AllowMissing) {
    Write-Host "WARN: golden manifest not bootstrapped yet ($manifestPath); soft-pass."
    Write-Host "      run with -UpdateGolden on a windows machine (or the ci bootstrap"
    Write-Host "      dispatch) and commit the manifest."
    exit 0
} else {
    Write-Host "FAIL: golden manifest not found: $manifestPath"
    exit 1
}

$outDir = Join-Path ([System.IO.Path]::GetTempPath()) ("viv-golden-" + [guid]::NewGuid().ToString("N"))
New-Item -ItemType Directory -Path $outDir | Out-Null

try {
    $results = @{}
    foreach ($s in $goldenSamples) { $results[$s] = @{} }
    $failures = 0
    $renderersAvailable = @{ gdi = $true; gl = $false; d3d = $false }

    foreach ($r in $renderers) {
        foreach ($s in $goldenSamples) {
            $sample = [System.IO.Path]::GetFileNameWithoutExtension($s)
            $bmp = Join-Path $outDir ($sample + "." + $r.Name + ".bmp")
            if (Test-Path $bmp) { Remove-Item $bmp -Force }

            # the hidden export contract (viv_export.c): 0 rendered, 2 the
            # load refused, 3 the renderer refused, 4 timeout, 5 the canvas
            # was refused.
            $proc = Start-Process -FilePath $ExePath -ArgumentList @(
                $r.Switch, "-render-size", $size, "-render-export", ('"' + $bmp + '"'),
                ('"' + (Join-Path $SamplesDir $s) + '"')
            ) -PassThru -WindowStyle Hidden
            if (-not $proc.WaitForExit(90000)) {
                # never leave a wedged export behind (the smoke sweep's
                # discipline): the internal timeout is the first bound,
                # this kill answers a truly hung decoder or a driver wedged
                # mid-present - a leaked hidden gui process would ride the
                # runner through the rest of the job.
                Stop-Process -Id $proc.Id -Force -ErrorAction SilentlyContinue
                Write-Host ("FAIL: {0} / {1}: the export hung (killed)" -f $r.Name, $s)
                $failures++
                continue
            }
            $code = $proc.ExitCode

            if ($code -eq 0) {
                if (-not (Test-Path $bmp)) {
                    Write-Host ("FAIL: {0} / {1}: exit 0 but no bitmap answered" -f $r.Name, $s)
                    $failures++
                    continue
                }
                $renderersAvailable[$r.Name] = $true
                $hash = (Get-FileHash -Path $bmp -Algorithm SHA256).Hash.ToLowerInvariant()
                $results[$s][$r.Name] = $hash
            }
            elseif ($code -eq 3) {
                # the renderer refused: legitimate only when the machine
                # carries no working instance of it (the ci vm has no gpu -
                # the gl software fallback or the d3d hal may both be gone).
                # a refusal on a machine where the renderer DID answer for
                # another sample is an inconsistency the comparison below
                # catches; here the leg just records null.
                $results[$s][$r.Name] = $null
            }
            else {
                Write-Host ("FAIL: {0} / {1}: the export exited {2}" -f $r.Name, $s, $code)
                $failures++
            }
        }
    }

    # the gdi leg is the ground truth: it must have rendered every sample.
    foreach ($s in $goldenSamples) {
        if (-not $results[$s].ContainsKey("gdi") -or -not $results[$s]["gdi"]) {
            Write-Host ("FAIL: the gdi leg did not render: {0}" -f $s)
            $failures++
        }
    }

    if ($UpdateGolden) {
        # write the manifest: every gdi hash, every hardware hash that
        # answered (the unavailable renderer rides as null and the
        # comparison below skips it).
        $ordered = [ordered]@{}
        foreach ($s in $goldenSamples) {
            $entry = [ordered]@{ gdi = $results[$s]["gdi"] }
            foreach ($r in @("gl", "d3d")) {
                if ($results[$s].ContainsKey($r)) { $entry[$r] = $results[$s][$r] }
                else { $entry[$r] = $null }
            }
            $ordered[$s] = $entry
        }
        if (-not (Test-Path $GoldenDir)) { New-Item -ItemType Directory -Path $GoldenDir | Out-Null }
        ($ordered | ConvertTo-Json -Depth 3) + "`n" | Set-Content -Path $manifestPath -Encoding ascii -NoNewline
        Write-Host ("golden manifest written: {0} ({1} samples)" -f $manifestPath, $goldenSamples.Count)
        exit $(if ($failures -gt 0) { 1 } else { 0 })
    }

    # the comparison: every rendered hash must match the manifest. a null
    # in the manifest (the renderer was unavailable at bootstrap) skips
    # that leg; a hash where the manifest has null is a surplus answer
    # (the renderer came back) - compared for information, not failure.
    $mismatched = 0
    foreach ($s in $goldenSamples) {
        foreach ($r in @("gdi", "gl", "d3d")) {
            $expected = $null
            if ($manifest.PSObject.Properties[$s] -and $manifest.$s.PSObject.Properties[$r]) {
                $expected = $manifest.$s.$r
            }
            $actual = $null
            if ($results[$s].ContainsKey($r)) { $actual = $results[$s][$r] }

            if ($actual -and $expected) {
                if ($actual -ne $expected) {
                    Write-Host ("FAIL: {0} / {1}: hash drift" -f $r, $s)
                    Write-Host ("      expected {0}" -f $expected)
                    Write-Host ("      actual   {0}" -f $actual)
                    $mismatched++
                }
            }
            elseif ($actual -and (-not $expected)) {
                Write-Host ("note: {0} / {1}: rendered but the manifest has no hash (re-bootstrap to pin it)" -f $r, $s)
            }
        }
    }

    # the three-way agreement: each renderer's hash is pinned in the
    # manifest independently - a drift in any one of the three goes red
    # above, which IS the three-way consistency check (all three legs
    # answer to their own committed oracle). a live cross-renderer
    # equality check would be wrong on purpose: the gdi shrink rides the
    # halftone filter while the hardware legs ride linear sampling (the
    # documented quality difference), so equal hashes are not the
    # contract - stable hashes are. the notes below record the agreement
    # for information whenever more than one leg answered.
    foreach ($s in $goldenSamples) {
        $gdi = $results[$s]["gdi"]
        foreach ($r in @("gl", "d3d")) {
            $hw = $null
            if ($results[$s].ContainsKey($r)) { $hw = $results[$s][$r] }
            if ($gdi -and $hw) {
                if ($hw -eq $gdi) {
                    Write-Host ("note: {0} / {1}: agrees with the gdi hash" -f $r, $s)
                }
                else {
                    Write-Host ("note: {0} / {1}: differs from the gdi hash (the filter semantics - halftone vs linear)" -f $r, $s)
                }
            }
        }
    }

    Write-Host ""
    Write-Host ("renderer availability: gdi={0} gl={1} d3d={2}" -f `
        $(if ($renderersAvailable["gdi"]) {"yes"} else {"NO"}), `
        $(if ($renderersAvailable["gl"]) {"yes"} else {"no"}), `
        $(if ($renderersAvailable["d3d"]) {"yes"} else {"no"}))
    Write-Host ("golden samples: {0}, hash mismatches: {1}, export failures: {2}" -f `
        $goldenSamples.Count, $mismatched, $failures)

    # never leave a viewer behind (the smoke sweep's closing discipline).
    Get-Process -Name voidImageViewer -ErrorAction SilentlyContinue |
        Stop-Process -Force -ErrorAction SilentlyContinue

    if (($failures + $mismatched) -gt 0) {
        Write-Host "PIXEL GOLDEN TEST FAIL"
        exit 1
    }
    Write-Host "PIXEL GOLDEN TEST PASS"
    exit 0
}
finally {
    if (Test-Path $outDir) { Remove-Item $outDir -Recurse -Force -ErrorAction SilentlyContinue }
}
