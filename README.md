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
**1.1.15-rc.5 — the sixth audit response round (the current release candidate):**

- **The IME and the hotkeys** — the report asked whether the shortcuts could fight the system's own keys (they cannot: the app registers no global hotkeys, no hooks and no accelerator tables, and every key match happens window-locally, consumed only on a hit). The real fight was with the input method editor: an open Chinese IME rewrites letter keys into `VK_PROCESSKEY`, so bindings went dead on the canvas and the key-capture dialogs could store keys that can never be pressed again. The windows that never compose text — the canvas, the zoom pill and its digits-only editor, the settings window, the edit-key capture — now run dissociated from the IME; the text-input dialogs (rename, jump-to, the Everything search) keep theirs, Chinese filenames are real input there.
- **The initialization and the reply queue** — a refused window-class registration used to vanish into a void return: the app would sit windowless forever, no error, no exit. Every init step answers now — class, menu, window — each failure a message box with the last error and a clean teardown. The loader's completion notice used to post exactly once and never retry: one refused `PostMessage` and the queue stalled until the exit timeout. The wakeup is a duty flag now — a refused post hands the duty back, and the drain re-posts for entries that arrived mid-loop.
- **The security and hygiene batch** — the copydata validators take offsets instead of pre-built pointers (the range is proven before any pointer forms), the uninstaller's second stage runs from an unpredictable fresh directory instead of a fixed `%TEMP%` name (a pre-planted file could ride the elevated re-run), the cancel flag rides interlocked forms (plain `volatile` carries no barrier on the ARM legs), COM and GDI+ teardown pairs with startup success, the webp delay write matches the read side's null guard, and the license set ships complete: QOI's MIT attribution joins the notices and both license files install alongside the binary.
- Full narrative: `Changes.txt`.

**1.1.14 — the stable promotion round (the current stable):**

- **The cache-set ceiling** — the memory budget priced one image at a time while the viewer holds up to three (the current image, the last-image cache, the preload slot). Every fill point now prices the whole set against the working-set ceiling: a preload that would overflow is abandoned silently (it is the one load nobody asked for), and the settle point after every load drops the cache — never the image on screen. The clipboard paste path trims the same way.
- **The defaults, pinned** — preloading the next image and caching the last one have shipped on by default since the upstream introduction, and the preloader follows the navigation direction both ways; the promotion makes the promise a guard instead of an accident (a user's explicit off in the ini stays off).
- **The already-loading answer** — asking for the file an in-flight preload was decoding used to be swallowed when that preload had already been displaced; the request now queues as the normal load it is.
- **The settings blank** — the retired startup row's leftover advance drew a blank band inside the General page; the rows below move up, and the footer, the window frame and the navigation column stay exactly where they were.
- Full narrative: `Changes.txt`.

Recent versions, one line each — full per-round detail in [Changes.txt](Changes.txt):
- **1.1.15-rc.4** — the command picker round: the shortcut editor's cascade (118 of 118 commands bindable, the thirty-two-row caps retired), the six-surface entry-point census.

- **1.1.15-rc.3** — the fifth audit response round: the navigation trio (the sort pollution at all three scan sites, the random-first gate order, the playlist file-name identity), the ten one-liners, the guard blind spots, the golden bootstrap refusing the fake green at the source.
- **1.1.15-rc.2** — the fourth audit response round: the golden set learns to discriminate (the textured png, the three-way-distinct contract, the one-way ratchet), the nav pair unifies on the neighbor predicate.
- **1.1.15-rc.1** — the image slot architecture round: the three physically separated frame-set families (19 loose globals, three hand-written field-by-field moves) become one typed slot and three instances; the lifecycle is two primitives; the nav-index cache keys on the file name.
- **1.1.14-rc.10** — the corrections round: the byte-invariant class gates (`.editorconfig`, the byte suite, the pushed-range whitespace check), the re-runnable split conservation proof (290/290), the QOI fuzz smoke (36 deterministic mutants), the ARM64 compile leg, the renderer fallback naming itself on the status line.
- **1.1.14-rc.9** — the navigation faces round: the toolbar's previous/next enable rule reads the navigation's own two paths (the playlist items, the folder fact the preload scan records) instead of a cache only the Jump-To dialog ever fills.
- **1.1.14-rc.8** — the renderer parity round: the GDI+ frames answer as DIB sections so the hardware renderers actually render every decoder family, the shape dimension reaches the pixel oracle (two non-power-of-two fixtures), the ceiling gate proves the refusal through the export oracle.
- **1.1.14-rc.7** — the input ceiling round: `GetFileSizeEx` and the 1 GB/512 MB whole-file ceilings, the hard kill retiring into the recorded process exit, thread-creation unwinding, the CodeQL full attack-surface leg, the sparse 4-GB-plus smoke stage.
- **1.1.14-rc.6** — the budget and baseline round: the working-set and animation frame budgets, the recorded exit timeout, the v145 security baseline with PE binary assertions, the release trust chain (attestation, CodeQL, the collaboration pack).
- **1.1.14-rc.5** — the pixel oracle round: the GL context rebuild across window changes, the renderer pad edge replication, the hidden render export and the golden hash CI.
- **1.1.14-rc.4** — the corner and audit response round: the fullscreen sharp-corner fix, the caption text color (attribute 36) and the evidence-first audit response (the GL pad zero, the per-window pixel format, the D3D same-size reuse, the SDL elevation catches).
- **1.1.14-rc.3** — the navigation visibility round: the next/previous fix for the new formats — a supported-extension table carries the viewer's open universe and the navigation filter answers it.
- **1.1.14-rc.2** — the fixture round: the test sample set commits (38 anomaly samples plus eight real imagery fixtures, two of them hand-encoded animated GIFs) and the QOI magic fix — the host verification caught a shipped constant spelled in the wrong byte order.
- **1.1.14-rc.1** — the todo closure round: the upstream TODO list closes — the OpenGL and Direct3D renderers, the toolbar customization and the shell context menu all land.
- **1.1.13** — the format horizons round: the WIC layer defers to the system codecs when GDI+ and libwebp both decline a file (JPEG-XR, DDS, HEIF/AVIF wherever the OS carries them), and a hand-rolled QOI decoder opens `.qoi` with no codec dependency at all; the fork's first stable to carry code.
- **1.1.13-rc.7** — the field sweep round: the cold-start slideshow fix, the startup shortcut retirement, the no-image menu gate, the single keyboard focus ring and the icon payload diet (the ico drops 80%).
- **1.1.13-rc.6** — the peripheral residue round: the closing sweep retires the never-built Wine DPI probe, four zero-reference api functions, the year-stringize pair and three never-requested localization strings.
- **1.1.13-rc.5** — the dead residue round: the carpet sweep retires three dead functions, four dead macro families and fourteen VS-generated resource ids; the frozen corners stay pinned by the round-91 guard.
- **1.1.13-rc.4** — the high dpi icons round: the ico grows the DPI ladder (Lanczos-resampled frames down from 256px, all alpha-carrying) and the frame icons ride the window's own DPI (`WM_SETICON` pair, `WM_DPICHANGED` re-pin).
- **1.1.13-rc.3** — the halftone palette round: 256-color mode gets `graphics::GetHalftonePalette` — the classic palette contract (foreground/background realization, the paint-DC selection, the display-change resync).
- **1.1.13-rc.2** — the association guard round: `.bmp`/`.jpg` taken over only when the effective default is the Windows canonical class or already ours — a foreign viewer's association is left completely alone.
- **1.1.13-rc.1** — the readme diet round: the news section demoted to the one-line list (full treatment only for the current stable and candidate; [Changes.txt](Changes.txt) stays the archive of record); the 1.1.13 TODO arc opens.
- **1.1.12** — the remake arc: the full GUI remake, the structure split, the platform guardrails and the field fixes — seventeen candidates, the stable promotion carried no code.
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
