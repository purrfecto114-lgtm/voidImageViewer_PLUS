# void Image Viewer (Touch + Languages)

[![stable](https://img.shields.io/badge/status-stable-brightgreen.svg)](https://github.com/purrfecto114-lgtm/voidImageViewer_PLUS/releases)
[![release](https://img.shields.io/github/v/release/purrfecto114-lgtm/voidImageViewer_PLUS&display_name=tag)](https://github.com/purrfecto114-lgtm/voidImageViewer_PLUS/releases)
[![license](https://img.shields.io/badge/license-MIT-blue.svg)](LICENSE)

> A stable fork of [voidtools/voidImageViewer](https://github.com/voidtools/voidImageViewer) with **touch optimizations**, **on-screen zoom controls**, a **complete dark UI**, and a **bilingual installer + UI language switcher**. Issues welcome in the [issue tracker](https://github.com/purrfecto114-lgtm/voidImageViewer_PLUS/issues).

A lightweight Windows image viewer (BMP, GIF, ICO, PNG, JPG, TIF, WEBP — animated GIF/WEBP included) that opens and displays images as fast as possible.

[Download](#download) · [What's new](#whats-new) · [Touch & zoom](#touch--zoom-controls) · [Languages](#languages) · [Build](#build-from-source)

Download
--------
Stable binaries (setup + zip, x86/x64, SHA-256 checksums):

https://github.com/purrfecto114-lgtm/voidImageViewer_PLUS/releases

What's new
--------
**1.1.03 — second security audit round:**

- **Safe-size wiring completed** — the overflow-checked `safe_size` helpers now guard every allocation arithmetic in the codebase (playlist/nav pointer arrays, rotate pixel buffers, backdrop bits, Everything IPC query/reply sizes, relaunch buffer, string/utf8/ini/glyph buffers, clipboard globals). A wrap now fails the allocation cleanly instead of producing an undersized block.
- **GDI+ frame-dimension count validated** — the same trust gap as the 1.1.02 frame-delay fix, 15 lines apart in the same function: a zero or oversized count from GDI+ is rejected before the dimension list is read, and the dead GUID diagnostic string is gone.
- **Smaller hardening** — the four dark-chrome brushes release on shutdown, the initial playlist shuffle frees the previous index array, save-as trims the base name so the filter extension always fits on a full buffer, and the shuffle/random-search seeds mix both halves of the performance counter.

**1.1.02 — the dark UI reaches the last white strips:**

- **Dark menu bar (all builds)** — Windows 11 and pre-1903 never darken a Win32 menu bar. The top-level items are owner-drawn in the dark palette (face `0x202020`, hover `0x454545`); the system keeps layout, clicks, keyboard and dropdowns. The bar gaps now fill from the *drawn item rects*, so a theme switch no longer leaves a white right half.
- **Complete dark options dialog (pre-1903 too)** — the tab strip, checkboxes, buttons and combo boxes paint dark even on builds without the dark explorer control classes (owner-draw fallback), and an open dialog re-themes live when the dark setting changes.
- **Dark toolbar button states** — hover/checked no longer flash the light-blue highlight over the dark strip.
- **Crisp toolbar icons** — glyphs draw as float-point line art with a minimum stroke width; the two magnifiers are redrawn with visible strokes.
- **DPI-aware menu font** — labels measure with `SystemParametersInfoForDpi`; a DPI change re-measures the bar.

**Earlier releases, one line each:**

- **1.1.01** — first stable: dark dialogs, auto language default, faster folder navigation, upstream-style tags.
- **1.1.0-rc.7** — field feedback: pinch floor, 1600% zoom ceiling, dark status bar/toolbar, manifest GUID fixes.
- **1.1.0-rc.6** — modern chrome: PerMonitorV2 DPI, Win11 rounded corners, vector toolbar icons, fullscreen overlay fade.
- **1.1.0-rc.5** — engineering: CI pinned runners, four-stage release pipeline, single-source version, libwebp 1.6.0.
- **1.1.0-rc.1..4** — zoom/save gestures, robustness fixes, installer autodetect.
- **1.1.0-beta.1..13** — touch gestures, floating zoom bar, 1% zoom steps, bilingual installer, language switcher, Save As, single-instance fix.

Full history: [Changes.txt](Changes.txt).

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

Gestures need Windows 7+ with touch hardware. Single-finger input stays mouse-compatible, so configured click actions are unaffected. Toggle the floating controls via **View → Zoom Controls**.

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

GitHub Actions compiles every push (pinned `windows-2022`/v143 + `windows-2025`/v145 legs); tag pushes run the tests, verify SHA-256 end to end and publish the release assets.

![Void Image Viewer Image View](https://www.voidtools.com/voidImageViewer.Image.View10.gif)

See also
--------
Upstream project: https://github.com/voidtools/voidImageViewer
