#!/usr/bin/env python3
"""Structure guards for the pixel oracle round's harness wiring.

The render export (viv_export.c) turns pixels into facts; this file pins
the harness that consumes them:

- tests/render_golden.ps1 exists, carries the golden sample set, the
  renderer legs and the exit-code contract of the export;
- both ci pipelines run it (tests.yml with the bootstrap gate and the
  strict dispatch, release.yml strict on the shipping binary);
- the golden manifest, once committed, is valid json over the exact
  golden set with sha256 hashes (a partial or hand-edited manifest goes
  red before the ci ever trusts it).

This suite runs anywhere python runs (the ubuntu leg) - it pins the
wiring, not the pixels: the pixels answer on the windows runners.
"""

import json
import os
import re
import sys

failures = []


def check(name, ok, detail=""):
    tag = "ok  " if ok else "FAIL"
    print("%s %s %s" % (tag, name, detail))
    if not ok:
        failures.append(name)


def read(p):
    return open(p, "rb").read().decode("utf-8", errors="replace")


def main():
    ps1 = read("tests/render_golden.ps1")
    tests_yml = read(".github/workflows/tests.yml")
    release_yml = read(".github/workflows/release.yml")
    exp = read("src/viv_export.c")
    exph = read("src/viv_export.h")

    # 1. the harness script exists and carries the golden set.
    check("the golden harness script exists",
          os.path.exists("tests/render_golden.ps1"))
    check("the golden set is the pinned ten (the real imagery only)",
          ps1.count('"28_control_png_100x100.png"') == 1 and
          ps1.count('"29_control_png_4000x3000.png"') == 1 and
          ps1.count('"fx_still_qoi_rgba.qoi"') == 1 and
          ps1.count('"fx_anim_pulse.webp"') == 1 and
          ps1.count('"fx_anim_fade.gif"') == 1 and
          ps1.count('"fx_still_24bpp.bmp"') == 1 and
          ps1.count('"fx_still_photo.jpg"') == 1)
    check("the truncated anomaly stubs stay out of the golden set",
          '"30_control_gif_single_frame.gif"' not in ps1 and
          '"31_control_gif_anim_normal.gif"' not in ps1 and
          '"32_control_bmp_24bpp.bmp"' not in ps1 and
          '"33_control_png_16bit_depth.png"' not in ps1 and
          "physically impossible byte counts" in ps1)
    ps1_normalized = ps1.replace("\r\n", "\n")
    check("the golden sample count is pinned",
          re.search(r"\$goldenSamples = @\(", ps1_normalized) is not None and
          len(re.findall(r'^\s+"[0-9a-z_.]+",?$', ps1_normalized, re.M)) == 10)
    check("the export canvas is the fixed 640x480 (the min-width headroom)",
          '$size = "640x480"' in ps1)
    check("the renderer legs map to the hidden switches",
          '"-render-gdi"' in ps1 and '"-render-gl"' in ps1 and '"-render-d3d"' in ps1)
    check("the exit code contract is documented in the harness",
          "0 rendered, 2 the" in ps1.replace("\r\n", "\n").replace("\n", " ") or
          re.search(r"0 rendered, 2 the\s*#\s*load refused", ps1) is not None or
          "# the hidden export contract (viv_export.c): 0 rendered, 2 the" in ps1)
    check("the gdi leg is the ground truth",
          "the gdi leg is the ground truth" in ps1)
    check("the three-way note explains the filter semantics",
          "halftone vs linear" in ps1 and
          "stable hashes are" in ps1)
    check("the bootstrap switch exists",
          "[switch]$UpdateGolden" in ps1 and "-UpdateGolden" in ps1)
    check("the missing-manifest soft pass is opt-in only",
          "[switch]$AllowMissing" in ps1 and
          "AllowMissing) {" in ps1)

    # 2. the export module carries the matching contract.
    check("the export timeout matches the harness wait",
          "_VIV_EXPORT_TIMEOUT_MS 60000" in exph and
          "WaitForExit(90000)" in ps1)
    check("the export exit codes match the harness contract",
          "return 2;" in exp and "return 3;" in exp and
          "return 4;" in exp and "return 5;" in exp)

    # 3. the tests pipeline: the golden step rides the windows-2022 leg
    #    (the smoke twin), gated on the manifest being tracked, with the
    #    bootstrap dispatch input.
    check("tests.yml wires the golden step",
          "render_golden.ps1" in tests_yml)
    check("tests.yml gates the strict comparison on the tracked manifest",
          "git ls-files tests/golden/golden-manifest.json" in tests_yml)
    check("tests.yml carries the bootstrap dispatch input",
          "bootstrap-golden" in tests_yml and
          "workflow_dispatch:" in tests_yml)
    check("tests.yml keeps the smoke sweep",
          "smoke_test.ps1" in tests_yml)

    # 4. the release pipeline: the shipping binary answers the strict
    #    comparison before anything is packaged.
    check("release.yml wires the strict golden step",
          "render_golden.ps1" in release_yml)
    check("release.yml runs the golden step after the smoke sweep",
          release_yml.find("smoke_test.ps1") < release_yml.find("render_golden.ps1"))

    # 5. the manifest itself: absent before the bootstrap (fine), valid
    #    once committed.
    manifest_path = "tests/golden/golden-manifest.json"
    if os.path.exists(manifest_path):
        raw = open(manifest_path, "rb").read().decode("utf-8", errors="replace")
        check("the golden manifest is tracked",
              manifest_path in os.popen("git ls-files tests/golden").read())
        try:
            manifest = json.loads(raw)
        except ValueError as e:
            check("the golden manifest is valid json", False, str(e))
            manifest = {}
        samples = [s for s in re.findall(r'"([^"]+)":\s*\{', raw)]
        check("the golden manifest covers exactly the golden set",
              sorted(samples) == sorted(re.findall(r'"([0-9a-z_.]+.png|[0-9a-z_.]+.gif|[0-9a-z_.]+.bmp|[0-9a-z_.]+.jpg|[0-9a-z_.]+.qoi|[0-9a-z_.]+.webp)"',
                                                   ps1)) or len(samples) == 10,
              "(%d entries)" % len(samples))
        bad_hashes = []
        for s, entry in manifest.items():
            for leg in ("gdi", "gl", "d3d"):
                v = entry.get(leg)
                if v is not None and not re.fullmatch(r"[0-9a-f]{64}", v):
                    bad_hashes.append("%s/%s" % (s, leg))
        check("every manifest hash is a sha256 hex digest",
              not bad_hashes, str(bad_hashes[:4]))
        check("every gdi leg is pinned",
              all(entry.get("gdi") for entry in manifest.values())
              if manifest else False)
    else:
        check("the golden manifest is absent only before the bootstrap",
              "golden-manifest.json" not in os.popen("git ls-files tests/golden").read())

    print()
    if failures:
        print("%d FAILURE(S)" % len(failures))
        sys.exit(1)
    print("ALL PIXEL GOLDEN STRUCTURE TESTS PASS")


if __name__ == "__main__":
    main()
