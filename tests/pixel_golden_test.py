#!/usr/bin/env python3
"""Validate the committed legacy pixel oracles and their fixture set."""
import json
from pathlib import Path
import re

root = Path(__file__).resolve().parent
manifest = json.loads((root / "golden/golden-manifest.json").read_text())
script = (root / "render_golden.ps1").read_text()
sample_section = script.split("$goldenSamples = @(", 1)[1].split(")", 1)[0]
unpinned_section = script.split("$unpinnedSamples = @(", 1)[1].split(")", 1)[0]
rendered = set(re.findall(r'"([^"]+)"', sample_section))
unpinned = set(re.findall(r'"([^"]+)"', unpinned_section))
# the wic jpeg decode is lossy and version-sensitive across windows builds
# (the v143/v145 image pair answered different pixels for the same fixture) -
# the sample still renders every run, but its hashes must stay out of the
# manifest: no committed value can satisfy two windows builds at once.
assert "fx_still_photo.jpg" in unpinned, "the wic jpeg sample must stay unpinned"
assert "fx_still_photo.jpg" not in manifest, "the jpeg's hashes cannot pin across windows builds"
assert set(manifest) == rendered - unpinned
assert len(manifest) == 12
for name, hashes in manifest.items():
    assert (root / "samples" / name).is_file(), name
    assert set(hashes) == {"gdi", "d3d9"}, name
    assert all(re.fullmatch(r"[0-9a-f]{64}", value) for value in hashes.values()), name
assert manifest["fx_still_qoi_sliver.qoi"]["gdi"] != manifest["fx_still_qoi_sliver.qoi"]["d3d9"]
print("12 GDI/D3D9 pixel baselines and fixtures PASS (the wic jpeg renders unpinned)")
