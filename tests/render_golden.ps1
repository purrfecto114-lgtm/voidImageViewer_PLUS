# Strict Win32 render export. D3D11 and D3D12 use WARP by default on CI.
param(
    [Parameter(Mandatory=$true)][string]$ExePath,
    [string]$SamplesDir = (Join-Path $PSScriptRoot "samples"),
    [string]$GoldenDir = (Join-Path $PSScriptRoot "golden"),
    [string]$EvidenceDir = "",
    [switch]$Hardware,
    [switch]$UpdateGolden
)

$ErrorActionPreference = "Stop"
$exe = (Resolve-Path $ExePath).Path
$samples = (Resolve-Path $SamplesDir).Path
$manifestPath = Join-Path $GoldenDir "golden-manifest.json"
if (-not $UpdateGolden -and -not (Test-Path $manifestPath)) {
    throw "Golden manifest is missing: $manifestPath"
}
$goldenSamples = @(
    "28_control_png_100x100.png",
    "29_control_png_4000x3000.png",
    "fx_anim_bounce.gif",
    "fx_anim_fade.gif",
    "fx_anim_pulse.webp",
    "fx_still_24bpp.bmp",
    "fx_still_photo.jpg",
    "fx_still_qoi_rgb.qoi",
    "fx_still_qoi_rgba.qoi",
    "fx_still_qoi_sliver.qoi",
    "fx_still_rgba.png",
    "fx_still_textured.png",
    "fx_still_webp_odd.webp"
)
$atNativeSize = @("fx_still_24bpp.bmp", "fx_still_qoi_rgba.qoi", "fx_still_textured.png")
$scaledTexture = "fx_still_qoi_sliver.qoi"
$size = "640x480"
$renderers = @("gdi", "d3d9", "d3d11", "d3d12")
Add-Type -TypeDefinition @'
using System;
using System.IO;

public static class VivGoldenPixels {
    public static int MaxError(string left, string right) {
        byte[] a = File.ReadAllBytes(left), b = File.ReadAllBytes(right);
        if (a.Length != b.Length) return 256;
        int max = 0;
        for (int i = 54; i < a.Length; i += 4) {
            for (int channel = 0; channel < 3; channel++) {
                max = Math.Max(max, Math.Abs(a[i+channel] - b[i+channel]));
            }
        }
        return max;
    }
}
'@
$temp = Join-Path ([IO.Path]::GetTempPath()) ("viv-golden-" + [guid]::NewGuid().ToString("N"))
New-Item -ItemType Directory -Path $temp | Out-Null

if ($EvidenceDir) {
    $null = New-Item -Type Directory -Force -Path $EvidenceDir
    $EvidenceDir = (Resolve-Path $EvidenceDir).Path
    Add-Type -AssemblyName System.Drawing
}

function Render-Image([string]$renderer, [string]$source, [string]$target) {
    # Exit 3 is a backend refusal. A GDI fallback cannot satisfy any D3D leg.
    $arguments = @("-render-$renderer", "-render-size", $size, "-render-export",
        ('"' + $target + '"'), ('"' + $source + '"'))
    if (-not $Hardware -and ($renderer -eq "d3d11" -or $renderer -eq "d3d12")) {
        $arguments = @("-render-warp") + $arguments
    }
    $proc = Start-Process -FilePath $exe -ArgumentList $arguments -PassThru -WindowStyle Hidden
    try {
        if (-not $proc.WaitForExit(90000)) {
            Stop-Process -Id $proc.Id -Force -ErrorAction SilentlyContinue
            throw "$renderer / $(Split-Path -Leaf $source): render timed out"
        }
        if ($proc.ExitCode -ne 0 -or -not (Test-Path $target)) {
            throw "$renderer / $(Split-Path -Leaf $source): export exit $($proc.ExitCode)"
        }
    } finally {
        $proc.Dispose()
    }
    $bytes = [IO.File]::ReadAllBytes($target)
    if ($bytes.Length -ne (54 + 640 * 480 * 4) -or
        $bytes[0] -ne 66 -or $bytes[1] -ne 77 -or
        [BitConverter]::ToInt32($bytes,18) -ne 640 -or
        [Math]::Abs([BitConverter]::ToInt32($bytes,22)) -ne 480 -or
        [BitConverter]::ToInt16($bytes,28) -ne 32) {
        throw "$renderer / $(Split-Path -Leaf $source): invalid export bitmap"
    }
    if ($EvidenceDir -and ((Split-Path -Leaf $source) -in ($atNativeSize + $scaledTexture))) {
        $bitmap = [Drawing.Bitmap]::FromFile($target)
        try {
            $bitmap.Save((Join-Path $EvidenceDir ((Split-Path -Leaf $source) + ".$renderer.png")), [Drawing.Imaging.ImageFormat]::Png)
        } finally {
            $bitmap.Dispose()
        }
    }
    (Get-FileHash -LiteralPath $target -Algorithm SHA256).Hash.ToLowerInvariant()
}

try {
    $legacy = if ($UpdateGolden) { $null } else { Get-Content -Raw $manifestPath | ConvertFrom-Json }
    $updated = [ordered]@{}
    foreach ($sample in $goldenSamples) {
        $results = @{}
        foreach ($renderer in $renderers) {
            $target = Join-Path $temp "$sample.$renderer.bmp"
            $results[$renderer] = Render-Image $renderer (Join-Path $samples $sample) $target
        }

        # Allow rounding differences in linear sampling while checking
        # every pixel. Keep native-size fixtures exact against the GDI oracle.
        if (-not $Hardware) {
            $errorLimit = 2
            $difference = [VivGoldenPixels]::MaxError(
                (Join-Path $temp "$sample.d3d11.bmp"), (Join-Path $temp "$sample.d3d12.bmp"))
            if ($difference -gt $errorLimit) {
                throw "${sample}: D3D11 and D3D12 WARP channel error $difference"
            }
        }
        if ($sample -in $atNativeSize) {
            foreach ($renderer in @("d3d9","d3d11","d3d12")) {
                if ($results[$renderer] -ne $results.gdi) {
                    throw "$sample / ${renderer}: native-size pixels differ from GDI"
                }
            }
        }
        if ($sample -eq $scaledTexture) {
            foreach ($renderer in @("d3d9","d3d11","d3d12")) {
                if ($results[$renderer] -eq $results.gdi) {
                    throw "$sample / ${renderer}: scaled texture matched the GDI sampling result"
                }
            }
        }

        if ($UpdateGolden) {
            $updated[$sample] = [ordered]@{gdi=$results.gdi; d3d9=$results.d3d9}
        } else {
            foreach ($renderer in @("gdi","d3d9")) {
                if ($results[$renderer] -ne $legacy.$sample.$renderer) {
                    throw "$sample / ${renderer}: committed pixel hash drift ($($results[$renderer]))"
                }
            }
        }
        Write-Host "PASS $sample (GDI, D3D9, D3D11, D3D12)"
    }
    if ($UpdateGolden) {
        $null = New-Item -Type Directory -Force -Path $GoldenDir
        ($updated | ConvertTo-Json -Depth 3) + "`n" | Set-Content -LiteralPath $manifestPath -Encoding ascii -NoNewline
    }
} finally {
    Remove-Item -LiteralPath $temp -Recurse -Force
}
