# void Image Viewer (Touch + Languages)

[![stable](https://img.shields.io/badge/status-stable-brightgreen.svg)](https://github.com/purrfecto114-lgtm/voidImageViewer_PLUS/releases)
[![release](https://img.shields.io/github/v/release/purrfecto114-lgtm/voidImageViewer_PLUS.svg?display_name=tag)](https://github.com/purrfecto114-lgtm/voidImageViewer_PLUS/releases)
[![license](https://img.shields.io/badge/license-MIT-blue.svg)](LICENSE)

> A stable fork of [voidtools/voidImageViewer](https://github.com/voidtools/voidImageViewer) with **touch optimizations**, **on-screen zoom controls**, a **complete dark UI**, and a **bilingual installer + UI language switcher**. Issues welcome in the [issue tracker](https://github.com/purrfecto114-lgtm/voidImageViewer_PLUS/issues).

A lightweight Windows image viewer (BMP, GIF, ICO, PNG, JPG, TIF, WEBP, EMF, WMF — animated GIF/WEBP included) that opens and displays images as fast as possible.

[Download](#download) · [What's new](#whats-new) · [Touch & zoom](#touch--zoom-controls) · [Canvas & backdrop](#canvas-backdrop--dark-mode) · [Recent files](#recent-files) · [Languages](#languages) · [Build](#build-from-source)

Download
--------
Stable binaries (setup + zip, x86/x64, SHA-256 checksums):

https://github.com/purrfecto114-lgtm/voidImageViewer_PLUS/releases

What's new
--------
**1.1.12-rc.7 — the zoom pill rework (the current release candidate):**

- **The zoom pill is one self-drawn window** — no nested owner-drawn buttons fighting the layered tray: `zoomui.c` paints the tray and every capsule in a single `WM_PAINT` pass (true stadium caps at every DPI), hit tests the button rects itself, tracks presses with capture (no command when the drag leaves the button), and passes clicks through everything outside the capsules so a fading pill never blocks the image.

Recent versions, one line each — full per-round detail in [Changes.txt](Changes.txt):

- **1.1.12-rc.6** — the top bar remake: the menu bar is a fully self-drawn child window (`viv_menubar`), the status bar owns its dark face, the toolbar button spacing is pinned uniform in both themes.
- **1.1.12-rc.5** — the structure round: the gesture cluster home in the view domain, the 2,208-line window procedure split into 42 per-message handlers in the new `viv_wndproc.c`, the splice guard's file manifest pinned.
- **1.1.12-rc.4** — the white band after opening Options or switching the theme fixed at the root (the dark re-apply lost its flip gate; the sweep is now unconditional and idempotent); the Options navigation tree reads in the dark UI.
- **1.1.12-rc.3** — the monolith is gone: `viv.c` (21,129 lines) is now a 4,718-line core plus eleven domain modules and a `viv_state.h` shared-context layer — a pure physical move (function bodies byte-identical; `/GL` whole-program optimization keeps cross-module inlining, performance unchanged); the setup offers the EMF/WMF associations; the vendored libwebp pruned of non-Windows build systems, fuzzers and docs (62 files — `COPYING`/`PATENTS`/`AUTHORS` kept).
- **1.1.12-rc.2** — the About dialog's band and title move into the resource template (correct at every DPI and in both themes); the post-theme-flip white band fixed (the frame repaint joined the sweep, the toolbar strip relayouts with the system metrics).
- **1.1.11** — one type system for the whole UI (the system message font per dialog at its own window DPI — CJK-safe, no more fallback-font mismatch); EMF/WMF in every association surface (metafiles rasterize via GDI+ — display-level support); recent files, transparency backdrop, complete dark dialogs.

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

The source layout: one core (`src/viv.c` — the window procedure, startup, the command line, the state definitions) plus eleven domain modules (`src/viv_<domain>.c/.h`) and the shared-context header (`src/viv_state.h`); see `docs/architecture/viv-split-spec.md`.

GitHub Actions compiles every push (pinned `windows-2022`/v143 + `windows-2025`/v145 legs); tag pushes run the tests, verify SHA-256 end to end and publish the release assets.

![Void Image Viewer Image View](https://www.voidtools.com/voidImageViewer.Image.View10.gif)

See also
--------
Upstream project: https://github.com/voidtools/voidImageViewer
