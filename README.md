# void Image Viewer (Touch + Languages)

[![stable](https://img.shields.io/badge/status-stable-brightgreen.svg)](https://github.com/purrfecto114-lgtm/voidImageViewer_PLUS/releases)
[![release](https://img.shields.io/github/v/release/purrfecto114-lgtm/voidImageViewer_PLUS.svg?display_name=tag)](https://github.com/purrfecto114-lgtm/voidImageViewer_PLUS/releases)
[![license](https://img.shields.io/badge/license-MIT-blue.svg)](LICENSE)

**English** | [简体中文](README_CN.md)

> A stable fork of [voidtools/voidImageViewer](https://github.com/voidtools/voidImageViewer) with **touch optimizations**, **on-screen zoom controls**, a **complete dark UI**, and a **bilingual installer + UI language switcher**. Issues welcome in the [issue tracker](https://github.com/purrfecto114-lgtm/voidImageViewer_PLUS/issues).

A lightweight Windows image viewer (BMP, GIF, ICO, PNG, JPG, TIF, WEBP, JPEG-XR, HEIF, AVIF, DDS, QOI, EMF, WMF — animated GIF/WEBP included; JPEG-XR (Win7+) and DDS (Win8.1+) ride the WIC codecs Windows itself carries, HEIF/AVIF ride the store's image extensions wherever they are installed, QOI is built in) that opens and displays images as fast as possible.

[Download](#download) · [What's new](#whats-new) · [Touch & zoom](#touch--zoom-controls) · [Canvas & backdrop](#canvas-backdrop--dark-mode) · [Recent files](#recent-files) · [Languages](#languages) · [Build](#build-from-source)

Download
--------
Stable binaries (setup + zip, x86/x64/arm64, SHA-256 checksums):

https://github.com/purrfecto114-lgtm/voidImageViewer_PLUS/releases

The binaries are unsigned (an open-source signing account is on the roadmap) — verify each download against the release's `sha256.txt` before running.
Every release artifact also carries a build-provenance attestation: `gh attestation verify <file> --repo purrfecto114-lgtm/voidImageViewer_PLUS`
answers the workflow run and commit each file was built from (signing would still be its own round — attestation proves origin, not publisher identity).

What's new
--------
**1.1.16 — the field response round IV (the current stable):**

- The stable was rebuilt at build 113. The settings page answers at every size — the content rows scroll under the pinned footer and their hit rects clip to the same viewport band the paint clips to (the invisible rows used to eat the Apply/Cancel/OK clicks and silently toggle checkboxes through the registry); the scroll rests on the row lattice so no row paints half-cut at the title band, the scrollbar rides a 12-dip strip the resize band no longer shadows with a thumb you can see, and a track press pages toward the click the way Windows does.
- The formats you select, associate — the takeover guard misspelled the jpg family's stock owner (`jpgfile`; Windows ships `jpegfile`), so every stock machine read as a foreign viewer and the checkbox silently bounced back; the table is corrected, and every explicit selection (a single click, select-all, the installer's wizard checkboxes) now takes the extension over directly, previous owner or not. The UserChoice lock wears a padlock with a two-line caption under the grid; the locked boxes spell the extension in full and deep-link to the viewer's own row in Windows' default-apps page.
- The uninstall leaves nothing behind — failed-save temps are swept from both homes, empty OpenWithProgids keys go when truly empty, a UserChoice naming our own removed ProgID goes with it, the extension's own key goes when bare or a redundant shadow of what HKLM already answers, the FileExts parents and the RegisteredApplications key take the same bare-key rule, and a legacy autolaunch value retires too. Your settings get the question Windows apps owe — keep them for a future install or delete them now; keep is the default, and a silent uninstall carries the keep word.
- The installed footprint drops a quarter megabyte — `Changes.txt` stays in the repository and the release notes (an upgrade still cleans the copy an older setup laid down), and the Apps list shows the measured size.
- Hardware acceleration answers the machine's own answer — a capability probe gates the settings switch (greyed with an unavailable face where Direct3D cannot come up), the menu radios, and the installer's checkbox; the runtime GDI fallback stays as the safety net. The shortcut page explains itself — a persistent instruction row (idle flow, live capture states), buttons that answer the selection, an empty keys list that starts the add capture, and the page opening with the command's first binding shown.
- The zoom readout shows clean numbers — a rapid click chain lands on scale pairs the geometric ladder cannot hit exactly (121, 131, 219 in the measured chains); the shown number rounds to the nearest multiple of ten within two points, never below 20%, and the render position itself never moves.

**1.1.15 — the previous stable** — the cache-set ceiling, the honest faces (both Open With homes, the UserChoice ask), every frame delivered (multi-frame HEIF/AVIF sequences), the frame counter that counts either way, the cache ring and preload chain you size, and the narrow-window toolbar paging. Full narrative: `Changes.txt`.

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

The zig cross build needs no Visual Studio: `sh build-zig/build.sh` (104 translation units, about 480 KB x64 with `-Os`); `sh build-zig/build-arm64.sh` compiles the same tree for aarch64. The ARM64 release payload itself is the MSVC v143 build (`Platform=ARM64`) - the zig leg is the compile gate that proves the tree still translates for the target. The vs linker embeds `res/voidImageViewer.Manifest` (per-monitor v2); the zig build keeps no embedded manifest, so it writes `viv.exe.manifest` next to the exe and the runtime claim in `os_init` covers even a stripped copy — keep the two files together, or build through vs when you need a single-file binary.

The source layout: one core (`src/viv.c` — the startup, the command line, the teardown) plus eighteen domain modules (`src/viv_<domain>.c/.h`), the decoder modules (`src/webp.c`, `src/qoi.c`, `src/wic.c`) and the shared-context header (`src/viv_state.h`); see `docs/architecture/viv-split-spec.md`.

GitHub Actions compiles every push (pinned `windows-2022`/v143 + `windows-2025`/v145 legs); tag pushes run the tests, verify SHA-256 end to end and publish the release assets.

![Void Image Viewer Image View](https://www.voidtools.com/voidImageViewer.Image.View10.gif)

Credits
--------
Upstream: **voidtools / David Carpenter** — [voidImageViewer](https://github.com/voidtools/voidImageViewer), MIT.

This fork: the original fork, the Chinese localization and the modern UI
rewrite by **hesphoros** (2026, as recorded in the git history), carried
forward by the current maintainer under the same MIT terms. The complete
author record lives in the repository history; `THIRD_PARTY_NOTICES.md`
covers the vendored components (Google's libwebp among them).

See also
--------
Upstream project: https://github.com/voidtools/voidImageViewer
