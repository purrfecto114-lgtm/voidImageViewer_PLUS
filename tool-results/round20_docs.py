#!/usr/bin/env python3
"""R20 docs round: UPSTREAM_PR_DESCRIPTION.md - short link (the 414 fix),
release identity 1.0.03, CI anchor 98a7e9a, version-display story."""
import pathlib

ROOT = pathlib.Path(__file__).resolve().parent.parent
P = ROOT / ".github" / "UPSTREAM_PR_DESCRIPTION.md"
d = P.read_bytes()
s = d.decode()

SHORT_LINK = "https://github.com/voidtools/voidImageViewer/compare/master...purrfecto114-lgtm:voidImageViewer_PLUS:upstream-plus?quick_pull=1&title=Dark%20UI%20pack%20%28title%20bar%2C%20menu%20bar%2C%20toolbar%2C%20status%2C%20dialogs%29%20%2B%20PerMonitorV2%2C%20vector%20icons%2C%20touch%20hardening"

def rep(old, new):
    global s
    assert s.count(old) == 1, f"anchor not found or not unique: {old[:70]!r} (count={s.count(old)})"
    s = s.replace(old, new)
    print(f"  replaced: {old[:58]!r}...")

# 1. the one-click block: drop the 8918-char body-in-URL (GitHub 414s it),
#    keep only the 288-char title-prefilled compare link.
lines = s.split("\n")
link_idx = next(i for i, l in enumerate(lines) if l.startswith("https://github.com/voidtools/voidImageViewer/compare"))
assert len(lines[link_idx]) > 8000, f"expected the 414 link, got {len(lines[link_idx])} chars"
old_block = "> **One-click create (form pre-filled with the compressed title + description below)**"
new_block = "> **One-click create (title pre-filled; the ~6 KB description is pasted separately — GitHub answers URLs over ~6 KB with 414)**"
rep(old_block, new_block)
s = s.replace(lines[link_idx], SHORT_LINK)
print(f"  replaced the {len(lines[link_idx])}-char 414 link with the {len(SHORT_LINK)}-char short link")
rep("> (If the one-click link is unwieldy, open the plain compare page, click **Create pull request**, and paste this file as the description.)",
    "> Open the link, paste the description below as the body (the fork's dashboard ships a one-click copy button for exactly this body), then **Create pull request**.")

# 2. CI evidence anchor
rep("Code-round head `eee7a07` (the 1.0.02 third-audit round - the pixel budget on both decoders, the uninstaller image-name verification, the anomaly sample generator, the real machine smoke test, and the follow-up that moved the Vista image-name api behind a lazy GetProcAddress resolver for the xp-era header set)",
    "Code-round head `98a7e9a` (the 1.0.03 version-display round - every user-visible version now shows the release identity string: the about dialog printed the four-segment 1.0.2.25, the add/remove entry printed 1.0.2 and the installer version keys printed 1.0.02.x64 while the tag said 1.0.02; five new test guards pin every display point to VERSION_STRING)")

# 3. summary identity
rep("Shipped to users as the fork's **1.0.02** (x64/x86 installer + zip, NSIS, the three-times-audited tree).",
    "Shipped to users as the fork's **1.0.03** (x64/x86 installer + zip, NSIS, the three-times-audited tree with one version identity everywhere).")

# 4. robustness bullet
rep("- **Uninstaller identity (third round)** — closing the old instance no longer trusts the window class name alone: the process image name is verified as `voidImageViewer.exe` (`QueryFullProcessImageNameW`) before any close or terminate is sent, so a foreign program reusing the class name is left alone.",
    "- **Uninstaller identity (third round)** — closing the old instance no longer trusts the window class name alone: the process image name is verified as `voidImageViewer.exe` (`QueryFullProcessImageNameW`) before any close or terminate is sent, so a foreign program reusing the class name is left alone.\n- **Version identity (display round)** — every user-visible version now derives from the single `VERSION_STRING` (the tag identity): the About dialog printed the four-segment `1.0.2.25`, the Settings→Apps uninstall entry printed `1.0.2` and the installer's PE version keys printed `1.0.02.x64` while the tag, the exe resources and the branding text all said `1.0.02`; five test guards pin every display point to the identity so the formats can never drift apart again.")

# 5. test plan + final CI line
rep("- Hostile-input sweep (reusable kit): `tests/make_anomaly_samples.py` generates the 37-sample anomaly set (truncated files, lying headers, zero-delay animations, frame floods, broken chunk order, trailing garbage, over-budget canvases) and `tests/smoke_test.ps1` opens every sample on a real machine, failing on any crash — the over-budget canvas proves the pixel budget refuses instead of dying.",
    "- Hostile-input sweep (reusable kit): `tests/make_anomaly_samples.py` generates the 37-sample anomaly set (truncated files, lying headers, zero-delay animations, frame floods, broken chunk order, trailing garbage, over-budget canvases) and `tests/smoke_test.ps1` opens every sample on a real machine, failing on any crash — the over-budget canvas proves the pixel budget refuses instead of dying.\n- Version surfaces: the tag, the exe properties, the installer properties, the About dialog and the Settings→Apps uninstall entry all show the identical identity string (e.g. `1.0.03`).")
rep("CI on the code-round head (`eee7a07`): tests (Python suites: zoom math + menu structure/dark wiring/version consistency, the 13-guard first-audit suite, the 16-guard second-audit suite and the 10-guard third-audit suite) + v143 + v145 compile matrix, all green.",
    "CI on the code-round head (`98a7e9a`): tests (Python suites: zoom math + menu structure/dark wiring/version consistency, the 13-guard first-audit suite, the 16-guard second-audit suite, the 10-guard third-audit suite and the 5-guard version-display suite) + v143 + v145 compile matrix, all green.")

P.write_bytes(s.encode())
print("docs round done")
