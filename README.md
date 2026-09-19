# void Image Viewer (Touch + Languages)

[![stable](https://img.shields.io/badge/status-stable-brightgreen.svg)](https://github.com/purrfecto114-lgtm/voidImageViewer_PLUS/releases)
[![release](https://img.shields.io/github/v/release/purrfecto114-lgtm/voidImageViewer_PLUS.svg?display_name=tag)](https://github.com/purrfecto114-lgtm/voidImageViewer_PLUS/releases)
[![license](https://img.shields.io/badge/license-MIT-blue.svg)](LICENSE)

> A stable fork of [voidtools/voidImageViewer](https://github.com/voidtools/voidImageViewer) with **touch optimizations**, **on-screen zoom controls**, a **complete dark UI**, and a **bilingual installer + UI language switcher**. Issues welcome in the [issue tracker](https://github.com/purrfecto114-lgtm/voidImageViewer_PLUS/issues).

A lightweight Windows image viewer (BMP, GIF, ICO, PNG, JPG, TIF, WEBP, JPEG-XR, HEIF, AVIF, DDS, QOI, EMF, WMF — animated GIF/WEBP included; JPEG-XR (Win7+) and DDS (Win8.1+) ride the WIC codecs Windows itself carries, HEIF/AVIF ride the store's image extensions wherever they are installed, QOI is built in) that opens and displays images as fast as possible.

[Download](#download) · [What's new](#whats-new) · [Touch & zoom](#touch--zoom-controls) · [Canvas & backdrop](#canvas-backdrop--dark-mode) · [Recent files](#recent-files) · [Languages](#languages) · [Build](#build-from-source)

Download
--------
Stable binaries (setup + zip, x86/x64, SHA-256 checksums):

https://github.com/purrfecto114-lgtm/voidImageViewer_PLUS/releases

The binaries are unsigned (an open-source signing account is on the roadmap) — verify each download against the release's `sha256.txt` before running.
Every release artifact also carries a build-provenance attestation: `gh attestation verify <file> --repo purrfecto114-lgtm/voidImageViewer_PLUS`
answers the workflow run and commit each file was built from (signing would still be its own round — attestation proves origin, not publisher identity).

What's new
--------
**1.1.15-rc.7 — the reentry state round (the current release candidate):**

- **Re-opening keeps the window's size** — a second instance's forwarded show command no longer demotes the first instance's window: a minimized window answers `SW_RESTORE` (its placement decides — maximized comes back maximized), a live window only grows, and the launcher word never hides or restores-down the visible window.
- **Minimize runs no size sweep** — the iconic client is degenerate, and the old sweep rewrote the view anchors through a garbage render size; every zoom and view value now survives the minimize for the restore to answer with.
- **Iconic geometry reads all go through the placement** — the `/x /y /width /height` defaults, the fullscreen capture (IsZoomed answers false for a maximized-minimized window) and the `/minimal` `//compact` restyle no longer seed their math with the -32000 parking rect.
- Full narrative: `Changes.txt`.

**1.1.15-rc.6 — the judged-fixes round** — the message box buttons' UTF-8 bridge, the borderless corner grip, the icon-only toolbar, the fullscreen binary searches, the paste path fallback and the classic Options retirement. Full narrative: `Changes.txt`.

**1.1.14 — the current stable** — the cache-set ceiling prices all three held images together at every fill point (a preload that would overflow is abandoned silently; the settle point drops the cache, never the image on screen), and the preloading/caching defaults are pinned as a guard. Full narrative: `Changes.txt`.

The full round archive — every earlier candidate one line each, plus the lessons that outlived their rounds — lives in [experience.md](experience.md); the complete per-version narrative is [Changes.txt](Changes.txt).

Touch & zoom controls
--------

| Gesture / control | Action |
| --- | --- |
| Two finger pinch | Zoom in / out (anchored at finger center) |
| Two finger drag | Pan / scroll image (with inertia) |
| Two finger tap | Reset zoom |
| Double tap (touch) | Toggle 1:1 / best fit |
| Toolbar zoom buttons | Zoom in / out |
| Floating zoom bar | Windowed: zoom pill. Fullscreen: prev / play / pause / next / zoom (bottom center, idle fade) |

Gestures need Windows 7+ with touch hardware. Single-finger input stays mouse-compatible, so configured click actions are unaffected. Toggle the floating controls via **View → Zoom Controls**. A pinch keeps shrinking below the windowed fit, down to about fit/16 (mirroring the 16× zoom cap) — `Allow shrinking` in Options keeps its meaning.

Canvas, backdrop & dark mode
--------

- **Windowed / fullscreen background color** — Options → View, or the View menu picker; the mat around the image and the empty-window canvas.
- **Backdrop under transparency** — View → Transparency backdrop: follow the window background color, black, white, custom color, or checkerboard. Alpha images (PNG/GIF/WEBP) composite over it at load time. This is *not* the canvas around the image — that color is the windowed background above.
- **Dark UI rule** — the light UI always shows your exact colors. In the dark UI a light mat keeps its hue but drops into the dark range (the default white maps to the dark palette canvas), and the same rule applies to the custom backdrop, so nothing glares out of the dark chrome. The Win11 caption tint follows the mat.
- **The dark UI is complete** — dialogs, options pages, the menu bar and the navigation tree all follow the theme (owner-drawn where the theme API falls short on older builds); light dialogs stay light.
- Dark mode itself: Options → General → Dark mode — light, dark, or follow Windows. Theme flips repaint the whole window and re-assert the dark chrome unconditionally (a delayed re-check heals any system-side light repaint), and the open dialogs re-theme live.

Recent files
--------
File → Recent keeps the last ten opened paths (deduplicated case-insensitively; missing files drop out on the next open; Clear empties the list). The list is capped at ten on every path and writes are debounced off the open path.

Languages
--------
English and 简体中文 ship built-in.

- The setup picks the language on its first page.
- **Options → General → Language** switches Auto / English / 简体中文 on the fly (no restart).
- Stored as `language=auto|english|chinese` in `voidImageViewer.ini`; unattended installs may pass `/language english|chinese|auto`.

Build from source
--------
Plain C + Win32 API, Visual Studio:

1. Open `vs2019/voidImageViewer.sln` (VS2022+, v143 toolset) or `vs2026/voidImageViewer.sln` (v145 toolset). Both share one file list (`voidImageViewer.files.props`). VS2019 works with `/p:PlatformToolset=v142` (not CI-covered).
2. Build the `voidImageViewer` project (x64 or Win32).
3. Optional setup: NSIS 3 via `nsis\build_installer.ps1` (auto-detects the VS version; sources compile with `/utf-8`).

The zig cross build needs no Visual Studio: `sh build-zig/build.sh` (104 translation units, about 480 KB x64 with `-Os`). The vs linker embeds `res/voidImageViewer.Manifest` (per-monitor v2); the zig build keeps no embedded manifest, so it writes `viv.exe.manifest` next to the exe and the runtime claim in `os_init` covers even a stripped copy — keep the two files together, or build through vs when you need a single-file binary.

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
