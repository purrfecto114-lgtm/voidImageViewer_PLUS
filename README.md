# void Image Viewer (Touch + Languages)

[![stable](https://img.shields.io/badge/status-stable-brightgreen.svg)](https://github.com/purrfecto114-lgtm/voidImageViewer_PLUS/releases)
[![release](https://img.shields.io/github/v/release/purrfecto114-lgtm/voidImageViewer_PLUS.svg?display_name=tag)](https://github.com/purrfecto114-lgtm/voidImageViewer_PLUS/releases)
[![license](https://img.shields.io/badge/license-MIT-blue.svg)](LICENSE)

**English** | [简体中文](README_CN.md)

> A stable fork of [voidtools/voidImageViewer](https://github.com/voidtools/voidImageViewer) with **touch optimizations**, **on-screen zoom controls**, a **complete dark UI**, and a **bilingual installer + UI language switcher**. Issues welcome in the [issue tracker](https://github.com/purrfecto114-lgtm/voidImageViewer_PLUS/issues).

A lightweight Windows image viewer (BMP, GIF, ICO, PNG, JPG, TIF, WEBP, JPEG-XR, HEIF, AVIF, DDS, QOI, EMF, WMF — animated GIF/WEBP included; JPEG-XR (Win7+) and DDS (Win8.1+) ride the WIC codecs Windows itself carries, HEIF/AVIF ride the store's image extensions wherever they are installed, QOI is built in) that opens and displays images as fast as possible.

[Download](#download) · [Upgrade & uninstall notes](#upgrade--uninstall-notes) · [What's new](#whats-new) · [Touch & zoom](#touch--zoom-controls) · [Canvas & backdrop](#canvas-backdrop--dark-mode) · [Recent files](#recent-files) · [Languages](#languages) · [Build](#build-from-source)

Download
--------
Stable binaries (setup + zip, x86/x64/arm64, SHA-256 checksums):

https://github.com/purrfecto114-lgtm/voidImageViewer_PLUS/releases

The binaries are unsigned (an open-source signing account is on the roadmap) — verify each download against the release's `sha256.txt` before running.
Every release artifact also carries a build-provenance attestation: `gh attestation verify <file> --repo purrfecto114-lgtm/voidImageViewer_PLUS`
answers the workflow run and commit each file was built from (signing would still be its own round — attestation proves origin, not publisher identity).

Upgrade & uninstall notes
--------

**Upgrading from 1.1.16 or earlier to 1.1.17:**

- This round tightens the settings-file integrity checks, so the **first launch after an upgrade may show a "Settings file problem" dialog - with rc.2 only for a genuinely damaged file** (see the rc.2 fix below): the session then runs on the built-in defaults, and the original settings file is kept as a `.corrupt.date-time` copy in the same folder. **Your settings are not lost.**
- The dialog's three buttons: **Cancel** = leave everything untouched (you will be asked again next launch); **Restore backup** = write back the last settings that loaded cleanly; **Keep** = save the defaults this session ran on.
- Fixed in rc.2: settings written by an older release could be misjudged by the stricter reader (the rc.1 regression). rc.2 reads files written by every writer of this line cleanly, so the dialog now appears only for genuinely damaged files.
- The dialog and its backup copy only touch the settings folder (`%APPDATA%\voidImageViewer`); images are never affected.

**Uninstall residue:**

- Fixed in rc.2: a cleanup uninstall now takes the `.corrupt.` backup copies and the `.auditbak` with the delete answer, so `%APPDATA%\voidImageViewer` is no longer held back by them (they used to hold the folder — `RemoveDirectory` cannot take one that is not empty).
- Per-user installs live in `%LOCALAPPDATA%\Programs\voidImageViewer`; if an upgrade moved the install to another path, the old folder may linger holding the previous version's files — safe to delete once nothing runs from it.
- The uninstall's cleanup of file associations, shortcuts and registry entries is not affected by either of the above.

**Picking the right package:**

- The Setup program itself runs on any architecture, but **only the viewer that matches your device's architecture starts**: Intel/AMD 64-bit → x64, 32-bit Windows → x86, Snapdragon/ARM devices → arm64.
- Standing note: an admin install may show warnings or the elevation prompt may fail. If elevation fails, use the zip from the same release instead.

What's new
--------

**1.1.17 — the corrupted-settings round (the current stable):**

- The two-round arc takes its stable. The p0 round made every open path answer one whitelist and name its refusals: the command line, drag-drop, folder walks and file->open agree on what is openable, what is not is named with its reason on the status line, and a release-blocking reader defect that shipped with that round was caught by the smoke gate before publishing.

- The settings file gained a corruption gate: a file that no longer reads cleanly is kept as a `.corrupt.` copy, the session runs on built-in defaults, and the exit asks before writing anything (Cancel / Restore backup / Keep). The rc.2 round fixed the gate's own false ask — the malformed-line check sat inside the character loop and read every settings file ever written as damaged; files written by every writer of this line now read clean, and genuinely damaged lines still report.

- The ask speaks your language: its caption, its keep and restore faces and its message body moved into the localization tables (English and Simplified Chinese).

- File, Add File rides its parent directory — the rc.1 call queued playlist rows at `...\a.png\a.png`; the row now points at the file you picked. The attributes helper zeroes the whole find-data layout at entry, holding the stack-garbage class dead for good.

- A cleanup uninstall now takes the preserved copies (`.corrupt.`, `.auditbak`) with the delete answer, and the guard suites grew with the arc: the ini reader walk rides the native regression gate host-side. Full narrative: `Changes.txt`.

- Open-from-anywhere now answers one whitelist with one named refusal: the command line, drag-drop, folder walks and file->open agree on what is openable, and what is not is named on the status line instead of vanishing. A release-blocking reader defect shipped with this reading was caught by the smoke gate and fixed before publishing.
- The settings file gains a corruption gate: a file that no longer reads cleanly is kept as a `.corrupt.` copy, the session runs on defaults, and the exit asks before writing anything (Cancel / Restore backup / Keep). Upgrade friction from that gate is documented above.
- The slideshow rate clamp, the installer's settings-write honesty, the strict safety sums and the eleven memory/consistency fixes from the audit response round all ride here. Full narrative: `Changes.txt`.

**1.1.16 — the previous stable** — the arm64 platform joins the release (a native aarch64 build, installer and portable zip), the performance round's five wins and a courtesy, the settings page answering at every size, the jpg family's takeover guard corrected, and the uninstall that leaves nothing behind. Full narrative: `Changes.txt`.

**The 1.1.16 arc — twelve release candidates:** the fourth fusion response (the zombie class tree falls), the fifth report's judgment, the parallel sweep, the field feedback round (the poison probe retires, the Windows registration, the hardware-acceleration offer), the layout migration (the upgrade never opens with the pill), the field feedback round II (the caption catches up, the installer's elevation honesty), the default programs round (one name everywhere, the nineteen-format installer), the documentation round, the codeql hardening (five libwebp widenings), the instruments round (the suites audit themselves), the audit response (the relay whitelist and eight memory fixes), and the review verdict (the completion event, the pid-named temp save, the safe sums). Full narrative: `Changes.txt`.

**The 1.1.15 arc — nineteen candidates:** full narrative in `Changes.txt`.

Touch & zoom controls
--------

| Gesture / control | Action |
| --- | --- |
| Two finger pinch | Zoom in / out (anchored at finger center) |
| Two finger drag | Pan / scroll image (with inertia) |
| Two finger tap | Reset zoom |
| Double tap (touch) | Toggle 1:1 / best fit |
| Toolbar zoom buttons | Zoom in / out |
| Floating zoom bar | One seven-cell row in both modes — prev / play-pause / next / zoom out / percent / zoom in (bottom center; fullscreen fades it out when idle) |

Gestures need Windows 7+ with touch hardware. Single-finger input stays mouse-compatible, so configured click actions are unaffected. Toggle the floating controls via **View → Floating Control Bar**. A pinch keeps shrinking below the windowed fit, down to about fit/16 (mirroring the 16× zoom cap) — `Allow shrinking` in Settings keeps its meaning.

Canvas, backdrop & dark mode
--------

- **Windowed / fullscreen background color** — Settings → View, or the View menu picker; the mat around the image and the empty-window canvas.
- **Backdrop under transparency** — View → Transparency backdrop: follow the window background color, black, white, custom color, or checkerboard. Alpha images (PNG/GIF/WEBP) composite over it at load time. This is *not* the canvas around the image — that color is the windowed background above.
- **Dark UI rule** — the light UI always shows your exact colors. In the dark UI a light mat keeps its hue but drops into the dark range (the default white maps to the dark palette canvas), and the same rule applies to the custom backdrop, so nothing glares out of the dark chrome. The Win11 caption tint follows the mat.
- **The dark UI is complete** — dialogs, Settings pages and the menu bar all follow the theme (owner-drawn where the theme API falls short on older builds); light dialogs stay light.
- Dark mode itself: Settings → General → Theme — automatic (follow Windows), light, or dark. Theme flips repaint the whole window and re-assert the dark chrome unconditionally (a delayed re-check heals any system-side light repaint), and the open dialogs re-theme live.

Recent files
--------
File → Recent keeps the last ten opened paths (deduplicated case-insensitively; missing files drop out on the next open; Clear empties the list). The list is capped at ten on every path and writes are debounced off the open path.

Languages
--------
English and 简体中文 ship built-in.

- The setup picks the language on its first page.
- **Settings → General → Language** switches Auto / English / 简体中文 on the fly (no restart).
- Stored as `language=auto|english|chinese` in `voidImageViewer.ini`; unattended installs may pass `/language english|chinese|auto`.

Build from source
--------
Plain C + Win32 API, Visual Studio:

1. Open `vs2019/voidImageViewer.sln` (VS2022+, v143 toolset) or `vs2026/voidImageViewer.sln` (v145 toolset). Both share one file list (`voidImageViewer.files.props`). VS2019 works with `/p:PlatformToolset=v142` (not CI-covered).
2. Build the `voidImageViewer` project (x64, Win32 or ARM64).
3. Optional setup: NSIS 3 via `nsis\build_installer.ps1` (auto-detects the VS version; sources compile with `/utf-8`).

The zig cross build needs no Visual Studio: first run `python3 -m pip install ziglang==0.16.0` (the CI-pinned version), then `sh build-zig/build.sh` (104 translation units; binary size varies with the toolchain and build options); `sh build-zig/build-arm64.sh` compiles the same tree for aarch64. The ARM64 release payload itself is the MSVC v143 build (`Platform=ARM64`) - the zig leg is the compile gate that proves the tree still translates for the target. The vs linker embeds `res/voidImageViewer.Manifest` (per-monitor v2); the zig build keeps no embedded manifest, so it writes `viv.exe.manifest` next to the exe and the runtime claim in `os_init` covers even a stripped copy — keep the two files together, or build through vs when you need a single-file binary.

The zig scripts explicitly select the Windows GUI subsystem; CI checks the x64 and ARM64 PE machine/subsystem fields.

The source layout: one core (`src/viv.c` — the startup, the command line, the teardown) plus eighteen domain modules (`src/viv_<domain>.c/.h`), the decoder modules (`src/webp.c`, `src/qoi.c`, `src/wic.c`) and the shared-context header (`src/viv_state.h`). `src/viv_selfshot.c` is an additional test harness linked only by `build-zig/build-selfshot.sh`.

GitHub Actions compiles every push (pinned `windows-2022`/v143 + `windows-2025`/v145 legs); tag pushes run the tests, verify SHA-256 end to end and publish the release assets.

![Void Image Viewer Image View](https://www.voidtools.com/voidImageViewer.Image.View10.gif)

Credits
--------
Upstream: **voidtools / David Carpenter** — [voidImageViewer](https://github.com/voidtools/voidImageViewer), MIT.

This fork: the original fork, the Chinese localization and the modern UI
rewrite by **hesphoros** (2026, as recorded in the git history), carried
forward by the current maintainer under the same MIT terms.
Author/committer metadata is retained in the repository history; it is
self-reported and does not by itself authenticate contributor identity.
`THIRD_PARTY_NOTICES.md`
covers the vendored components (Google's libwebp among them).

See also
--------
Upstream project: https://github.com/voidtools/voidImageViewer
