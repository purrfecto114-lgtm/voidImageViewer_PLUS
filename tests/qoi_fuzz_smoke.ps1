param(
    [Parameter(Mandatory = $true)]
    [string]$ExePath,

    [Parameter(Mandatory = $true)]
    [string]$SamplesDir
)

# QOI fuzz smoke: deterministic mutation corpus against the shipped
# decoder, through the export oracle's exit-code contract.
#
# exit codes: 0 pass, 1 fail, 2 setup error (the smoke twins'
# convention - a missing exe or seed is not a mutant's verdict).
#
# The corrections review's finding: the QOI decoder is the only format
# in the tree with no system codec behind it - every boundary it gets
# wrong is ours alone - and it carried no adversarial input testing at
# all. A coverage-guided fuzzer wants its own harness round; what lands
# here is the honest cheap form: thirty-six deterministic mutants of
# the three tracked QOI fixtures (header field corruption, payload
# flips at three depths, truncation, appended bytes), each opened in
# the real viewer through the hidden export path.
#
# The contract under test is not "decodes" but "answers": a mutant may
# legitimately decode (exit 0, bitmap answered - QOI is tolerant of
# some payload damage by design) or be refused (exit 2, the load gate
# rejecting the header, the budget refusing a bomb, the decoder
# rejecting a torn stream). Anything else is a failure: a crash exit
# code, a wedge past the kill timeout, or an exit 0 with no bitmap.
#
# The corpus is seeded and index-stable: the same mutant set runs on
# every machine and every push, so a red here is reproducible by
# running the stage locally with the same three fixtures.

$ErrorActionPreference = "Stop"

if (-not (Test-Path $ExePath)) {
    Write-Host "FAIL: exe not found: $ExePath"
    exit 2
}

$seeds = @(
    "fx_still_qoi_rgb.qoi",
    "fx_still_qoi_rgba.qoi",
    "fx_still_qoi_sliver.qoi"
)
foreach ($s in $seeds) {
    if (-not (Test-Path (Join-Path $SamplesDir $s))) {
        Write-Host "FAIL: seed fixture missing: $s (in $SamplesDir)"
        exit 2
    }
}

# the corpus builder: given a seed's bytes and a mutant index
# (0..11), returns the mutant's bytes. deterministic per index.
function New-Mutant {
    param([byte[]]$src, [int]$index)

    $bytes = [byte[]]::new($src.Length)
    [Array]::Copy($src, $bytes, $src.Length)
    $rng = [System.Random]::new(0x514F49 + $index)

    switch ($index % 12) {
        0  { $bytes[0] = 0xFF }                                  # magic byte 0
        1  { $bytes[3] = 0x00 }                                  # magic byte 3
        2  { $bytes[4] = 0x7F }                                  # width, high byte
        3  { $bytes[8] = 0x7F }                                  # height, high byte
        4  { $bytes[12] = 0x00 }                                 # channels: 0
        5  { $bytes[12] = 0x09 }                                 # channels: 9
        6  { $bytes[13] = 0xFF }                                 # colorspace
        7  { $i = [int]($bytes.Length / 4); $bytes[$i] = $bytes[$i] -bxor 0xFF }
        8  { $i = [int]($bytes.Length / 2); $bytes[$i] = $bytes[$i] -bxor 0xFF }
        9  { $i = [int]((3 * $bytes.Length) / 4); $bytes[$i] = $bytes[$i] -bxor 0xFF }
        10 {
            # truncation at seventy percent
            $keep = [int]($bytes.Length * 0.7)
            $cut = [byte[]]::new($keep)
            [Array]::Copy($bytes, $cut, $keep)
            return ,$cut
        }
        11 {
            # sixty-four appended seeded-random bytes
            $grown = [byte[]]::new($bytes.Length + 64)
            [Array]::Copy($bytes, $grown, $bytes.Length)
            $tail = [byte[]]::new(64)
            $rng.NextBytes($tail)
            [Array]::Copy($tail, 0, $grown, $bytes.Length, 64)
            return ,$grown
        }
    }
    return ,$bytes
}

$outDir = Join-Path ([System.IO.Path]::GetTempPath()) ("viv-qoifuzz-" + [guid]::NewGuid().ToString("N"))
New-Item -ItemType Directory -Path $outDir | Out-Null

$failures = 0
$runs = 0
try {
    foreach ($seed in $seeds) {
        $src = [System.IO.File]::ReadAllBytes((Join-Path $SamplesDir $seed))
        $stem = [System.IO.Path]::GetFileNameWithoutExtension($seed)

        for ($i = 0; $i -lt 12; $i++) {
            $mutant = New-Mutant $src $i
            $qoi = Join-Path $outDir ("{0}.m{1:d2}.qoi" -f $stem, $i)
            $bmp = Join-Path $outDir ("{0}.m{1:d2}.bmp" -f $stem, $i)
            [System.IO.File]::WriteAllBytes($qoi, $mutant)
            if (Test-Path $bmp) { Remove-Item $bmp -Force }

            $proc = Start-Process -FilePath $ExePath -ArgumentList @(
                "-render-gdi", "-render-size", "640x480",
                "-render-export", ('"' + $bmp + '"'),
                ('"' + $qoi + '"')
            ) -PassThru -WindowStyle Hidden

            if (-not $proc.WaitForExit(45000)) {
                Stop-Process -Id $proc.Id -Force -ErrorAction SilentlyContinue
                Write-Host ("FAIL: {0} m{1:d2}: the decoder wedged (killed)" -f $stem, $i)
                $failures++
                $runs++
                continue
            }
            $code = $proc.ExitCode
            $runs++

            if ($code -eq 0) {
                if (-not (Test-Path $bmp)) {
                    Write-Host ("FAIL: {0} m{1:d2}: exit 0 but no bitmap answered" -f $stem, $i)
                    $failures++
                    continue
                }
                Write-Host ("ok  : {0} m{1:d2}: decoded (bitmap answered)" -f $stem, $i)
            }
            elseif ($code -eq 2) {
                Write-Host ("ok  : {0} m{1:d2}: refused by the load gate (exit 2)" -f $stem, $i)
            }
            else {
                Write-Host ("FAIL: {0} m{1:d2}: exit {2} outside the decode-or-refuse contract" -f $stem, $i, $code)
                $failures++
            }
        }
    }
}
finally {
    Remove-Item -Recurse -Force $outDir -ErrorAction SilentlyContinue
}

Write-Host ""
Write-Host ("qoi fuzz smoke: {0} mutants, {1} failure(s)" -f $runs, $failures)
if ($failures -ne 0) {
    exit 1
}
Write-Host "QOI FUZZ SMOKE PASS"
exit 0
