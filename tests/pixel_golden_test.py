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
    check("the golden set is the pinned thirteen (the real imagery, the shape dimension and the discrimination texture)",
          # the discrimination gate names the solid controls once
          # more (the bootstrap exemption list - round 111), so the
          # count is two for the pair and one for every textured
          # sample: a third mention of anything is a drift.
          ps1.count('"28_control_png_100x100.png"') == 2 and
          ps1.count('"29_control_png_4000x3000.png"') == 2 and
          ps1.count('"fx_still_qoi_rgba.qoi"') == 1 and
          ps1.count('"fx_anim_pulse.webp"') == 1 and
          ps1.count('"fx_anim_fade.gif"') == 1 and
          ps1.count('"fx_still_24bpp.bmp"') == 1 and
          ps1.count('"fx_still_photo.jpg"') == 1 and
          ps1.count('"fx_still_webp_odd.webp"') == 1 and
          ps1.count('"fx_still_qoi_sliver.qoi"') == 1 and
          ps1.count('"fx_still_textured.png"') == 1)
    check("the truncated anomaly stubs stay out of the golden set",
          '"30_control_gif_single_frame.gif"' not in ps1 and
          '"31_control_gif_anim_normal.gif"' not in ps1 and
          '"32_control_bmp_24bpp.bmp"' not in ps1 and
          '"33_control_png_16bit_depth.png"' not in ps1 and
          "physically impossible byte counts" in ps1)
    ps1_normalized = ps1.replace("\r\n", "\n")
    check("the golden sample count is pinned",
          re.search(r"\$goldenSamples = @\(", ps1_normalized) is not None and
          len(re.findall(r'^\s+"[0-9a-z_.]+",?$', ps1_normalized, re.M)) == 13)
    check("the shape dimension rides the golden set (the non power of two webp and the sliver, the texture beside them)",
          '"fx_still_webp_odd.webp"' in ps1 and
          '"fx_still_qoi_sliver.qoi"' in ps1 and
          "two defects can" in ps1)
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
    # round 111: the bootstrap refuses the fake green at the source -
    # a textured sample where two legs answered the same hash never
    # reaches the manifest (the verification pass stays notes-only by
    # design; the pinned hashes carry the discrimination).
    check("the bootstrap carries the discrimination gate",
          "the fake-green signature the discrimination gate exists to refuse" in ps1 and
          "$solidControls" in ps1)
    check("the solid exemption rides the gate, not the contract",
          "$solidControls -contains" in ps1)

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
    # round 111: an untracked manifest is a deletion, and a deletion
    # fails the push gate (the old soft branch wore the pre-bootstrap
    # era's clothes - the audit caught the comments claiming
    # otherwise).
    check("tests.yml fails the untracked manifest red",
          "a deletion is a downgrade, not a bootstrap window" in tests_yml)
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
    # a source archive (the github zip download) carries no .git directory:
    # git-tracked state is a checkout-only fact, so the tracked checks gate
    # on the repository being present while the content checks below run
    # everywhere (the audit's archive-safe finding: a complete zip must not
    # fail a test that mixes git state into content).
    in_git_checkout = os.path.isdir(".git")
    if os.path.exists(manifest_path):
        raw = open(manifest_path, "rb").read().decode("utf-8", errors="replace")
        if in_git_checkout:
            check("the golden manifest is tracked",
                  manifest_path in os.popen("git ls-files tests/golden").read())
        else:
            print("skip  the golden manifest is tracked (source archive, not a git checkout)")
        try:
            manifest = json.loads(raw)
        except ValueError as e:
            check("the golden manifest is valid json", False, str(e))
            manifest = {}
        samples = [s for s in re.findall(r'"([^"]+)":\s*\{', raw)]
        # the bootstrap window is explicit: the manifest is either the
        # full set or exactly the pre-textured twelve (the hashes land
        # by the ci bootstrap dispatch, never by hand). any other
        # divergence - a missing fixture, an extra entry - fails both
        # forms and goes red.
        # the extraction is set-shaped: the bootstrap's solid-exemption
        # list quotes the two controls a second time (round 111), and
        # the manifest covers each sample exactly once regardless.
        ps1_samples = sorted(set(re.findall(r'"([0-9a-z_.]+.png|[0-9a-z_.]+.gif|[0-9a-z_.]+.bmp|[0-9a-z_.]+.jpg|[0-9a-z_.]+.qoi|[0-9a-z_.]+.webp)"',
                                        ps1)))
        window_form = sorted(s for s in ps1_samples
                             if s != "fx_still_textured.png")
        check("the golden manifest covers exactly the golden set",
              sorted(samples) == ps1_samples or sorted(samples) == window_form,
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
        # the renderer parity floor: every parity-sized sample (a power
        # of two padding that fits the software renderers' 1024 texture
        # ceiling) must answer on every leg. the one exemption is the
        # 4000x3000 control's gl leg - its 4096x4096 padding exceeds the
        # software gl ceiling, a capability refusal the max-texture gate
        # exists to make (its d3d leg answers; the sample's own role is
        # the gdi mipmap shrink). any other null is the "no renderer on
        # this machine" reading that hid whole decoder families while
        # the per-image refusals wore it.
        parity_exempt = {("29_control_png_4000x3000.png", "gl")}
        check("every parity-sized golden sample answers on every leg (a null read as an environment gap while whole decoder families were being refused)",
              all(manifest[s].get(leg) for s in manifest
                  for leg in ("gdi", "gl", "d3d")
                  if (s, leg) not in parity_exempt) if manifest else False)
        # the fourth audit's discrimination finding: the two control
        # pngs are single solid fills - their role is the scaling
        # extremes, not filter discrimination, and a solid fill can
        # legitimately answer identically under halftone and linear
        # sampling (28's gl hash does differ; the solid pair's d3d
        # agreement is the coincidence the audit could not rule out
        # from linux). every textured sample is held to the stronger
        # contract: the three legs must disagree somewhere, because an
        # identical d3d hash on real imagery is the fake-green
        # signature - a leg that never drew, reading the gdi result
        # back. the textured png is the adjudicator: its hashes land by
        # the bootstrap, and this check arms itself the moment they do.
        solid_exempt = {"28_control_png_100x100.png",
                        "29_control_png_4000x3000.png"}
        check("every textured golden sample answers three-way distinct (the identical-d3d fake green the fourth audit flagged)",
              all(len({manifest[s].get(leg) for leg in ("gdi", "gl", "d3d")}) == 3
                  for s in manifest
                  if s not in solid_exempt)
              if manifest else False)
    else:
        if in_git_checkout:
            check("the golden manifest is absent only before the bootstrap",
                  "golden-manifest.json" not in os.popen("git ls-files tests/golden").read())
        else:
            print("skip  the manifest-absent check (source archive, not a git checkout)")

    print()
    if failures:
        print("%d FAILURE(S)" % len(failures))
        sys.exit(1)
    print("ALL PIXEL GOLDEN STRUCTURE TESTS PASS")


if __name__ == "__main__":
    main()
