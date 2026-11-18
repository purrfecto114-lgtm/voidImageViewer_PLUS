#!/usr/bin/env python3
"""Validate the committed legacy pixel oracles and their fixture set."""
import json
from pathlib import Path
import re

root = Path(__file__).resolve().parent
manifest = json.loads((root / "golden/golden-manifest.json").read_text())
script = (root / "render_golden.ps1").read_text()
sample_section = script.split("$goldenSamples = @(", 1)[1].split(")", 1)[0]
assert set(manifest) == set(re.findall(r'"([^"]+)"', sample_section))
assert len(manifest) == 13
for name, hashes in manifest.items():
    assert (root / "samples" / name).is_file(), name
    assert set(hashes) == {"gdi", "d3d9"}, name
    assert all(re.fullmatch(r"[0-9a-f]{64}", value) for value in hashes.values()), name
assert manifest["fx_still_qoi_sliver.qoi"]["gdi"] != manifest["fx_still_qoi_sliver.qoi"]["d3d9"]
print("13 GDI/D3D9 pixel baselines and fixtures PASS")
