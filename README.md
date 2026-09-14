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
**1.1.13-rc.3 — the halftone palette round (the current release candidate):**

- **256 color mode gets its halftone palette** — the second upstream TODO item lands: `graphics::GetHalftonePalette` via the GDI+ flat API (`GdipCreateHalftonePalette`), loaded beside the other entries and optional like the thumbnail export. On a palettized desktop — the 8bpp remote session, the safe-mode desktop, the legacy VM — GDI maps every blit through the hardware palette, and a viewer that realizes no palette of its own dithers through the 20 static VGA colors.
- **The classic palette contract rides the main window** — the need is read from the window's own DC (bits × planes = 8) and the palette is created or released lazily; `WM_QUERYNEWPALETTE` realizes it in the foreground on activation, `WM_PALETTECHANGED` realizes it in the background when another window claims the hardware first (never answering its own change — that loops), `WM_DISPLAYCHANGE` re-reads the need when the mode flips, and the paint path keeps it selected in the paint DC so both the direct and the backbuffer present path map through it.

**1.1.12 — the remake arc: the full GUI remake, the platform guardrails, the field fixes and the review absorption (the current stable):**

- **The stable promotion carries no code** — every fix already shipped as 1.1.12-rc.17 and passed the full gate on real Windows. The stable mark is the verdict on the whole rc arc, not a new behavior.
- **The arc in one breath** — the GUI remake (the theme token system, the owner-drawn menus, the remade settings window and message boxes, the self-drawn zoom row), the structure split (the 2,208-line window procedure became 42 per-message handlers), the crash the remake planted and the smoke sweep caught, the platform guardrails, the field arcs, and the review absorption. Seventeen release candidates, each landed changelog-first and green on the CI legs before its tag.
- Full narrative: `Changes.txt`.

Recent versions, one line each — full per-round detail in [Changes.txt](Changes.txt):

- **1.1.13-rc.2** — the association guard round: `.bmp`/`.jpg` taken over only when the effective default is the Windows canonical class or already ours — a foreign viewer's association is left completely alone.
- **1.1.13-rc.1** — the readme diet round: the news section demoted to the one-line list (full treatment only for the current stable and candidate; [Changes.txt](Changes.txt) stays the archive of record); the 1.1.13 TODO arc opens.
- **1.1.12-rc.17** — the review absorption round: the external carpet review absorbed (the itemID carrier, the token strip, the settings DPI correction, the resize escape hatch, the startup DPI sync).
- **1.1.12-rc.16** — the open intent round: the recent-list trade refunded (the open intent declared where it is knowable — a reload is not a recent open).
- **1.1.12-rc.15** — the field report round: the rotate-into-recent reordering, the 4K 225% text proportions, the capture-hint mojibake, the menu process check.
- **1.1.12-rc.14** — the zoom pane editor round: the corner percent becomes an in-place editor (the drag moved off the pane, the 1998 dialog retired).
- **1.1.12-rc.13** — the non-win11 field round: the play/pause face, the first-zoom snap and the dead gate, the non-Win11 look (drop shadow, owner-drawn dropdowns, token recalibration).
- **1.1.12-rc.12** — the platform guardrails round: the Windows 8.1 manifest GUID, the UnicoWS clean, the WM_DPICHANGED lParam guard.
- **1.1.12-rc.11** — the carpet repair round: the menu bar roots and the status date pane, the view-top origin, the theme-flip flush, the Win7 DPI ladder.
- **1.1.12-rc.10** — the dpi correctness round: the per-monitor-v2 claim fixed at the root (three guarded layers plus the zig-side manifest).
- **1.1.12-rc.9** — the gui remake round 2: one palette for everything (the 17 theme tokens, the owner-drawn menus, the themed message boxes and dialogs).
- **1.1.12-rc.7** — the zoom pill rework: one self-drawn window, stadium caps at every DPI, click-through outside the capsules.
- **1.1.12-rc.6** — the top bar remake: the menu bar is a fully self-drawn child window (`viv_menubar`), the status bar owns its dark face, the toolbar button spacing is pinned uniform in both themes.
- **1.1.12-rc.5** — the structure round: the gesture cluster home in the view domain, the 2,208-line window procedure split into 42 per-message handlers in the new `viv_wndproc.c`, the splice guard's file manifest pinned.
- **1.1.12-rc.4** — the white band after opening Options or switching the theme fixed at the root (the dark re-apply lost its flip gate; the sweep is now unconditional and idempotent); the Options navigation tree reads in the dark UI.
- **1.1.12-rc.3** — the monolith is gone: `viv.c` (21,129 lines) is now a 4,718-line core plus eleven domain modules and a `viv_state.h` shared-context layer — a pure physical move (function bodies byte-identical; `/GL` whole-program optimization keeps cross-module inlining, performance unchanged); the setup offers the EMF/WMF associations; the vendored libwebp pruned of non-Windows build systems, fuzzers and docs (62 files — `COPYING`/`PATENTS`/`AUTHORS` kept).
- **1.1.12-rc.2** — the About dialog's band and title move into the resource template (correct at every DPI and in both themes); the post-theme-flip white band fixed (the frame repaint joined the sweep, the toolbar strip relayouts with the system metrics).
- **1.1.11** — one type system for the whole UI (the system message font per dialog at its own window DPI — CJK-safe, no more fallback-font mismatch); EMF/WMF in every association surface (metafiles rasterize via GDI+ — display-level support); recent files, transparency backdrop, complete dark dialogs.
- **1.1.02–1.1.10** — the fork's early arc: the touch gestures and pinch zoom, the floating zoom controls, the dark UI reaching every strip, the bilingual localization with the live language switcher, and the field-fix rounds (the full per-version narrative lives in `Changes.txt` at each tag).

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

The zig cross build needs no Visual Studio: `sh build-zig/build.sh` (98 translation units, about 465 KB x64 with `-Os`). The vs linker embeds `res/voidImageViewer.Manifest` (per-monitor v2); the zig build keeps no embedded manifest, so it writes `viv.exe.manifest` next to the exe and the runtime claim in `os_init` covers even a stripped copy — keep the two files together, or build through vs when you need a single-file binary.

The source layout: one core (`src/viv.c` — the window procedure, startup, the command line, the state definitions) plus eleven domain modules (`src/viv_<domain>.c/.h`) and the shared-context header (`src/viv_state.h`); see `docs/architecture/viv-split-spec.md`.

GitHub Actions compiles every push (pinned `windows-2022`/v143 + `windows-2025`/v145 legs); tag pushes run the tests, verify SHA-256 end to end and publish the release assets.

![Void Image Viewer Image View](https://www.voidtools.com/voidImageViewer.Image.View10.gif)

See also
--------
Upstream project: https://github.com/voidtools/voidImageViewer
