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
**1.1.12 — the About title hard code fix (the current stable):**

- **The About title font scales with the DPI (1.1.12)** — the About dialog's title kept the hard-coded 32-pixel height of the 96-DPI design point: at 150% display scale the title read two-thirds of its intended proportion, at 200% half of it. The title now derives from the live dialog font the shared dialog proc applies at the dialog's own window DPI — family, weight and scale all travel with the font the dialog actually draws — with the height at the 8/3 ratio of the message font (the design point as a ratio instead of a literal), created through the wide pipeline (a face name survives whole instead of borrowing the wide path from the project-level Unicode define), and rebuilt at every open, so a DPI change between two About opens no longer draws the stale face. A creation failure keeps the previous face drawing.

- **One type system for the whole UI (1.1.11)** — the dialog templates sized their dialog-unit grid with a hard-coded Segoe UI 9pt face (the 1.1.09 font unification), but Segoe UI holds no CJK glyphs: on a Chinese system every dialog label rendered through the GDI font-linking fallback with the Latin line metrics, while the menu bar and the status bar draw the system locale face (Microsoft YaHei UI) — two type systems in one window, with mismatched line heights and baselines. Every dialog now takes the system **message font** at its own window DPI in `WM_INITDIALOG` (the shared dark proc applies it to the dialog and every child before each dialog's own initialization runs): Segoe UI on an English system, the locale-native UI face on a Chinese one — the same family the menu and status bar already draw, so the whole UI shares one type system in both themes. A dialog dragged across monitors re-reads the font at the new DPI, and the font lives exactly as long as the dialog that draws with it (created per dialog, replaced inside the same message on a monitor crossing, released at the dialog's destruction — no settings broadcast can invalidate an open dialog's face), and the dark combo field heights, the dark label metrics and the About title face all derive from the final font (the template face only sized the DLU grid; the layout stays).

- **EMF & WMF open everywhere** — association table, open-dialog filter, Everything search, command line and Options checkboxes all carry the two metafile extensions (metafiles rasterize through GDI+ at the display level — not a native metafile loader, so treat them as display support rather than full-format parity with BMP/PNG).
- **Recent files** — a persistent File → Recent submenu (MRU of ten paths, deduplicated, stale entries dropped, cleared from the menu). Saves are debounced off the open path, and the count is capped on every path (compile-time id lock).
- **Canvas & transparency backdrop** — two different mats: **View → Windowed background color…** sets the canvas color around the image (and the empty-window canvas); **View → Transparency backdrop** sets what shows under the transparent pixels of alpha images (PNG/GIF/WEBP): follow the window background color, black, white, custom color or checkerboard. In the dark UI a light custom mat keeps its hue but lands in the dark range, so the canvas never glares out of the chrome (image open or not); the light UI always shows the exact color. Changing either color re-tints the Win11 caption and reloads transparency immediately — with no image open the change is quiet (no load error on the blank window).
- **Light dialogs stay light, dark dialogs stay complete (1.1.08)** — the dark theme classes follow the app theme instead of being applied unconditionally (the light options page no longer shows black comboboxes and black buttons); the dark combos keep the exact captured native field height (no chin, no size drift against the label rows), and the dark push buttons paint the flat dark face on every build (the native dark button's light bottom edge is gone).
- **The dark options text reads (1.1.10)** — the checkbox and radio faces paint complete in the dark UI through custom draw: the dark dialog background, the theme glyph in the control's own checked, mixed, disabled and hot state, the light label and the focus frame drawn exactly once. The skip reaches the control through `DWLP_MSGRESULT` the way the NM_CUSTOMDRAW documentation prescribes for dialog procedures — a plain dialog-proc return value never reaches the control (the first 1.1.09 redo shipped that: the skip was lost, the system painted its default face on top, the labels read black on black and the two XOR focus frames cancelled each other). Every dialog template declares the same font now — Segoe UI 9pt, one weight, one charset (the pages and the containers used to measure their dialog units with two different charsets behind the obsolete DS_FIXEDSYS flag) — and every drawn control selects its font before it draws, so the light and the dark UI render the same face. The About dialog's bottom band follows the theme too (no more light strip in the dark UI), and the geometry tests walk the pixel grid the font sets.
- **Pan && Scan is gone by design** — the upstream View submenu that stretched the image independently in x and y (19 entries, 18 numpad shortcuts) was removed at 1.1.0-beta.6 on purpose: it broke the aspect ratio lock, and the zoom ladder always scales both axes together. Recorded in the beta.6 Changes.txt entry (and pinned by the menu-structure tests); not a regression.
- **Dark dialogs are complete** — the options pages owner-draw their comboboxes on every Windows build (the explorer dark style has no combo parts, so language / dark-mode / blit-mode fields used to stay light), and the tab strip, tree and static text all follow the theme.
- **Zoom ladder below best fit** — a touch pinch shrinks to about fit/16 (mirroring the 16× cap) instead of locking at the windowed fit; `Allow shrinking` keeps its meaning.
- **Field fixes** — duplicate recent-files rows, stale "failed to load" status on a closed image, the light strip beside the toolbar after a theme flip (full invalidation + a 400 ms re-check for the registry race), status-bar size units (B/KB/MB/GB), wallpaper-change confirmation, arrow navigation without a slideshow, Ctrl+Comma for options.
- **Second-rework simulation check (1.1.07)** — the rework is verified by re-running it: the new suite extracts the constants, formulas and control flow from the shipped source and replays every field report against the extracted logic (the mat-color battery, the MRU burst/cap/dedup/load-clamp, the menu-span walk, the below-fit zoom ladder, the blank-state flags, the options geometry, the theme-flip registry race, the release identity). It runs in CI beside the two existing suites on every push and every tag.

Full per-round detail: [Changes.txt](Changes.txt).

**1.0.04 — pointer-width pixel budget (load performance fix):**

- **Big images load again on 64-bit** — the 1.0.02 pixel budget refused *any* canvas over 100 MP on *every* build, which quietly broke panoramic stitches and large flatbed scans on x64/ARM64 (where 400 MP is a perfectly safe single-frame allocation). The ceiling now follows the pointer width: 100 MP on 32-bit, **400 MP on 64-bit**; hostile headers are still refused before the decoder runs.
- **Refusals are no longer silent** — both loaders print the claimed megapixels and the ceiling through the debug channel, so "why won't this open" is answerable by turning on the debug banner.
- **Test kit grows to 38** — the 110 MP sample doubles as the x64 acceptance boundary, a new 625 MP lying-header sample pins the x64 refusal point, and the guard suite anchors both branches of the split.

**1.0.03 — version display unification:**

- **One identity everywhere** — the About dialog (was `1.0.2.25 (x64)`), the Settings→Apps uninstall entry (was `1.0.2`) and the installer's version keys (was `1.0.02.x64`) now all show the release identity that matches the git tag — e.g. `1.0.03 (x64)` in About. The debug banner prints the identity with the build counter (`viv 1.0.03 (build 26) (x64)`), and five new test guards keep every display point anchored to `VERSION_STRING` so the formats can never drift apart again.

**1.0.02 — third audit round: decode budget + real machine smoke test:**

- **Pixel budget** — both decoders refuse canvases over 100 MP *before* any allocation. A 428 KB hostile header claiming 20000×20000 used to hit the allocator and die with a fatal dialog; it now fails like any unloadable file.
- **Uninstaller identity** — closing the old instance verifies the process image name (`voidImageViewer.exe`) instead of trusting the window class name alone; a foreign program reusing the class name is left alone.
- **Anomaly samples + Windows smoke test** — `tests/make_anomaly_samples.py` generates 37 hostile samples (truncation, lying headers, zero-delay animation, frame floods, broken chunk order, trailing garbage, over-budget canvases); `tests/smoke_test.ps1` opens every one and fails on any crash — now run by CI on every push and every tag against the freshly compiled exe (a Windows runner is a real Windows machine), and still runnable on any desktop: `powershell -ExecutionPolicy Bypass -File tests\smoke_test.ps1`.

**1.0.01 — the fork restarts its version line:**

- **A clean public history** — the retired 1.1.x tags and releases (3 stables, 7 RCs, 13 betas) are removed; the fork now numbers from **1.0.01** and records one entry per development stage. No code change ships with the reset: these binaries are the twice-audited tree.
- **The stage story** — touch & zoom core with the floating zoom bar and bilingual installer (foundation) · PerMonitorV2 DPI, Win11 chrome, vector glyphs (engineering) · the complete dark UI, including the owner-drawn menu bar that never themes on Win11/pre-1903 (dark completion) · GDI+ frame guards plus overflow-checked `safe_size` arithmetic at every allocation (audit hardening).

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

Canvas, backdrop & dark mode
--------

- **Windowed / fullscreen background color** — Options → View, or the View menu picker (1.1.08); the mat around the image and the empty-window canvas.
- **Backdrop under transparency** — View → Transparency backdrop: follow the window background color, black, white, custom color, or checkerboard. Alpha images (PNG/GIF/WEBP) composite over it at load time. This is *not* the canvas around the image — that color is the windowed background above.
- **Dark UI rule** — the light UI always shows your exact colors. In the dark UI a light mat keeps its hue but drops into the dark range (the default white maps to the dark palette canvas), and the same rule applies to the custom backdrop, so nothing glares out of the dark chrome. The Win11 caption tint follows the mat.
- Dark mode itself: Options → General → Dark mode — light, dark, or follow Windows. Theme flips repaint the whole window (with a settle-window re-check), and the open dialogs re-theme live.

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

GitHub Actions compiles every push (pinned `windows-2022`/v143 + `windows-2025`/v145 legs); tag pushes run the tests, verify SHA-256 end to end and publish the release assets.

![Void Image Viewer Image View](https://www.voidtools.com/voidImageViewer.Image.View10.gif)

See also
--------
Upstream project: https://github.com/voidtools/voidImageViewer
