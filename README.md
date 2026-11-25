# void Image Viewer (Touch + Languages)

[![stable](https://img.shields.io/badge/status-stable-brightgreen.svg)](https://github.com/purrfecto114-lgtm/voidImageViewer_PLUS/releases)
[![release](https://img.shields.io/github/v/release/purrfecto114-lgtm/voidImageViewer_PLUS.svg?display_name=tag)](https://github.com/purrfecto114-lgtm/voidImageViewer_PLUS/releases)
[![license](https://img.shields.io/badge/license-MIT-blue.svg)](LICENSE)

**English** | [简体中文](README_CN.md)

> A stable fork of [voidtools/voidImageViewer](https://github.com/voidtools/voidImageViewer) with **touch optimizations**, **on-screen zoom controls**, a **complete dark UI**, and a **bilingual installer + UI language switcher**. Issues welcome in the [issue tracker](https://github.com/purrfecto114-lgtm/voidImageViewer_PLUS/issues).

A lightweight Windows image viewer (BMP, GIF, ICO, PNG, JPG, TIF, WEBP, JPEG-XR, HEIF, AVIF, DDS, QOI, EMF, WMF — animated GIF/WEBP included; JPEG-XR (Win7+) and DDS (Win8.1+) ride the WIC codecs Windows itself carries, HEIF/AVIF ride the store's image extensions wherever they are installed, QOI is built in) that opens and displays images as fast as possible.

[Download](#download) · [Upgrade & uninstall notes](#upgrade--uninstall-notes) · [What's new](#whats-new) · [Touch & zoom](#touch--zoom-controls) · [Canvas & backdrop](#canvas-backdrop--dark-mode) · [Memory & renderers](#memory--renderers) · [Diagnostics](#diagnostics) · [Recent files](#recent-files) · [Languages](#languages) · [Build](#build-from-source)

Download
--------
Stable binaries (setup + zip, x86/x64/arm64, SHA-256 checksums):

https://github.com/purrfecto114-lgtm/voidImageViewer_PLUS/releases

The binaries are unsigned (an open-source signing account is on the roadmap) — verify each download against the release's `sha256.txt` before running.
Every release artifact also carries a build-provenance attestation: `gh attestation verify <file> --repo purrfecto114-lgtm/voidImageViewer_PLUS`
answers the workflow run and commit each file was built from (signing would still be its own round — attestation proves origin, not publisher identity).

### 1.1.18

The command strip drops text labels as the window narrows. **More** lists the remaining commands. The filename at the bottom opens its folder, and the adjacent info button opens the file properties. Enter fullscreen from the toolbar and leave it from the floating controls.

Rotation works on a temporary copy and shows the rotated preview immediately after it is ready. **Save As** suggests a separate name. Loading the original again restores its disk contents. For an animation, the preview rotates every loaded frame and Save As exports the frame currently shown.

Choose GDI, Direct3D 9, Direct3D 11, or Direct3D 12 (11On12) under **View → Renderer**. Unsupported images and device failures use GDI. Old Direct3D settings select Direct3D 9 and the retired OpenGL setting migrates to GDI.

Upgrade & uninstall notes
--------

**Upgrading from 1.1.16 or earlier to 1.1.18:**

- The renderer story is all Direct3D: GDI stays the default, Direct3D 9, Direct3D 11 and Direct3D 12 (11On12) are opt-in under **View → Renderer**, and the retired OpenGL setting reads GDI. An old `renderer` ini value maps to Direct3D 9.
- Rotation works on a temporary copy: the original never changes, the preview turns as soon as it is ready, and **Save As** suggests a `-rotated` name.
- Touch works in every renderer; the pinch follows the fingers from their first real motion.
- `VIV_RENDER_TRACE=1` arms a renderer refusal trace (back end, site name, HRESULT) in the shipping binaries — set it before reproducing a render problem.
- This round tightens the settings-file integrity checks, so the **first launch after an upgrade may show a "Settings file problem" dialog - with 1.1.17-rc.2 and later only for a genuinely damaged file** (see the rc.2 fix below): the session then runs on the built-in defaults, and the original settings file is kept as a `.corrupt.date-time` copy in the same folder. **Your settings are not lost.**
- The dialog's three buttons: **Cancel** = leave everything untouched (you will be asked again next launch); **Restore backup** = write back the last settings that loaded cleanly; **Keep** = save the defaults this session ran on.
- Fixed in 1.1.17-rc.2: settings written by an older release could be misjudged by the stricter reader (the rc.1 regression). rc.2 reads files written by every writer of this line cleanly, so the dialog now appears only for genuinely damaged files.
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

**1.1.18 — the touch and hardware arc (the current stable):**

- Touch answers from the first motion: the pinch baselines at the fingers' real distance (the close-contact floor is gone), a garbage crossing re-bases without jumping, fast spreads keep their motion, and the zoomed pan — two-finger, inertia and mouse drag alike — repaints in every renderer, the Direct3D legs included.

- The hardware story is all Direct3D: Direct3D 9, Direct3D 11 and Direct3D 12 (11On12, WARP by default) sit under **View → Renderer**, GDI stays the default, a per-image pixel self-check hands a lying accelerator to GDI, and `VIV_RENDER_TRACE=1` names every refusal with its site and HRESULT.

- The chrome went quiet and responsive: one 48-dip strip carries the menus and the command cluster (labels fold to icons and into **More** as the window narrows), the zoom cluster works as one grouped control with an in-place percent editor, and the status bar keeps the facts with the pixel read-out following the cursor as its own chip.

- Rotation moved to an async worker on a temporary copy: a 4K image turns without holding the UI thread, the original never changes, and **Save As** suggests the `-rotated` name.

- The status panes stopped ellipsizing their own text (the position indicator paints "1 / 3" whole), the capacity ladder grew its terabyte rung, and the memory round answered the field's numbers with the ledger, the memory lane and the churn bounds. Full narrative: `Changes.txt`.
- The settings file gained a corruption gate: a file that no longer reads cleanly is kept as a `.corrupt.` copy, the session runs on built-in defaults, and the exit asks before writing anything (Cancel / Restore backup / Keep). The rc.2 round fixed the gate's own false ask — the malformed-line check sat inside the character loop and read every settings file ever written as damaged; files written by every writer of this line now read clean, and genuinely damaged lines still report.

- The ask speaks your language: its caption, its keep and restore faces and its message body moved into the localization tables (English and Simplified Chinese).

- File, Add File rides its parent directory — the rc.1 call queued playlist rows at `...\a.png\a.png`; the row now points at the file you picked. The attributes helper zeroes the whole find-data layout at entry, holding the stack-garbage class dead for good.

- A cleanup uninstall now takes the preserved copies (`.corrupt.`, `.auditbak`) with the delete answer, and the guard suites grew with the arc: the ini reader walk rides the native regression gate host-side. Full narrative: `Changes.txt`.

**1.1.17 — the previous stable** — the corrupted-settings arc: the open paths answer one whitelist with named refusals, the settings file gains a corruption gate (kept as a `.corrupt.` copy, the session on defaults, the exit asks before writing), the ask speaks both languages, and a cleanup uninstall takes the preserved copies with it. Full narrative: `Changes.txt`.

**The 1.1.18 arc — eleven release candidates:** the touch hand-off, the quiet chrome, the responsive and hardware round (rc.3 and rc.4), the 4K rotation, the status panes, the position indicator, the Direct3D field round, the memory round, the touch gesture round, and the pan repaint. Full narrative: `Changes.txt`.

**1.1.16 — the previous stable** — the arm64 platform joins the release (a native aarch64 build, installer and portable zip), the performance round's five wins and a courtesy, the settings page answering at every size, the jpg family's takeover guard corrected, and the uninstall that leaves nothing behind. Full narrative: `Changes.txt`.

**The 1.1.16 arc — twelve release candidates:** the fourth fusion response (the zombie class tree falls), the fifth report's judgment, the parallel sweep, the field feedback round (the poison probe retires, the Windows registration, the hardware-acceleration offer), the layout migration (the upgrade never opens with the pill), the field feedback round II (the caption catches up, the installer's elevation honesty), the default programs round (one name everywhere, the nineteen-format installer), the documentation round, the codeql hardening (five libwebp widenings), the instruments round (the suites audit themselves), the audit response (the relay whitelist and eight memory fixes), and the review verdict (the completion event, the pid-named temp save, the safe sums). Full narrative: `Changes.txt`.

**The 1.1.15 arc — nineteen candidates:** full narrative in `Changes.txt`.

Touch & zoom controls
--------

| Gesture / control | Action |
| --- | --- |
| Two finger pinch | Zoom in / out (anchored at finger center); responds from the first spread, even when the fingers land close together |
| Two finger drag | Pan / scroll image (with inertia) |
| Two finger tap | Reset zoom (also leaves the fill-window stretch, like the Best fit command) |
| Double tap (touch) | Toggle 1:1 / best fit |
| Toolbar zoom buttons | Zoom in / out |
| Floating zoom bar | One seven-cell row in both modes — prev / play-pause / next / zoom out / percent / zoom in (bottom center; fullscreen fades it out when idle) |

Gestures need Windows 7+ with touch hardware. Single-finger input stays mouse-compatible, so configured click actions are unaffected. **Best fit** always shows the image at its fit size — switching it on leaves the fill-window stretch, and the two-finger tap and the reset-zoom shortcut do the same. Toggle the floating controls via **View → Floating Control Bar**. A pinch keeps shrinking below the windowed fit, down to about fit/16 (mirroring the 16× zoom cap) — `Allow shrinking` in Settings keeps its meaning.

Canvas, backdrop & dark mode
--------

- **Windowed / fullscreen background color** — Settings → View, or the View menu picker; the mat around the image and the empty-window canvas.
- **Backdrop under transparency** — View → Transparency backdrop: follow the window background color, black, white, custom color, or checkerboard. Alpha images (PNG/GIF/WEBP) composite over it at load time. This is *not* the canvas around the image — that color is the windowed background above.
- **Dark UI rule** — the light UI always shows your exact colors. In the dark UI a light mat keeps its hue but drops into the dark range (the default white maps to the dark palette canvas), and the same rule applies to the custom backdrop, so nothing glares out of the dark chrome. The Win11 caption tint follows the mat.
- **The dark UI is complete** — dialogs, Settings pages and the menu bar all follow the theme (owner-drawn where the theme API falls short on older builds); light dialogs stay light.
- Dark mode itself: Settings → General → Theme — automatic (follow Windows), light, or dark. Theme flips repaint the whole window and re-assert the dark chrome unconditionally (a delayed re-check heals any system-side light repaint), and the open dialogs re-theme live.

Memory & renderers
--------

The renderer choice lives under **View → Renderer**: GDI plus three hardware back ends — Direct3D 9, Direct3D 11 and Direct3D 12 (through D3D11On12). A back end that cannot take a frame hands that frame to GDI and says so on the status line; an rc.8 round also ended the touch regression where the first pinch-zoom retired the session's hardware renderer behind that note.

**Why a small file can hold a lot of memory.** The viewer prices canvases, never compressed file bytes: a 4.7 MB JPEG that decodes to 12 megapixels holds, at the default settings, roughly three decoded copies (the image, one preload seat and one cache seat), the GPU's 32-bit texture, the swap chain's two buffers and the D3D runtime's own commit — a few hundred MB is the design's arithmetic, not a leak (CI's memory lane churns 100 synchronous paints and bounds the growth). Every part carries its own ceiling: a load refuses past 12 bytes/pixel (~2.4 GB of priced working set), the hardware path past 512 MB of texture + viewport, and the preload+cache set past 1.2 GB (the cache ring trims oldest-first over that line). Reduce the residency with **Settings → Preload count / Cache count = 0**.

Diagnostics
--------

- `VIV_RENDER_TRACE=1` (any build): the first 128 hardware-renderer refusals append the back end, a stable site name (probe, apis, create-device, pipeline, targets, upload, present, frame-contract, byte-budget, max-texture, viewport, reset, scene among them) and the HRESULT to `%TEMP%\viv_render_trace.log` — a field report that names its gate.
- `-render-d3d9 | -render-d3d11 | -render-d3d12` (without `-render-export`) pins that back end for the session over the ini — the field diagnostic and the test surface the CI lanes ride. `-render-warp` forces the software rasterizer. The ini keeps its own value; the exit save restores it.
- `-render-export <path> -render-size WxH` renders a file headlessly through any back end (the pixel-oracle harness the release pipeline runs).

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

The zig cross build needs no Visual Studio: first run `python3 -m pip install ziglang==0.16.0` (the CI-pinned version), then `sh build-zig/build.sh` (105 translation units; binary size varies with the toolchain and build options); `sh build-zig/build-arm64.sh` compiles the same tree for aarch64. The ARM64 release payload itself is the MSVC v143 build (`Platform=ARM64`) - the zig leg is the compile gate that proves the tree still translates for the target. The vs linker embeds `res/voidImageViewer.Manifest` (per-monitor v2); the zig build keeps no embedded manifest, so it writes `viv.exe.manifest` next to the exe and the runtime claim in `os_init` covers even a stripped copy — keep the two files together, or build through vs when you need a single-file binary.

The zig scripts explicitly select the Windows GUI subsystem; CI checks the x64 and ARM64 PE machine/subsystem fields.

The source layout: one core (`src/viv.c` — the startup, the command line, the teardown) plus eighteen domain modules (`src/viv_<domain>.c/.h`), the decoder modules (`src/webp.c`, `src/qoi.c`, `src/wic.c`) and the shared-context header (`src/viv_state.h`). `src/viv_selfshot.c` is an additional test harness linked only by `build-zig/build-selfshot.sh`.

GitHub Actions compiles every push (pinned `windows-2022`/v143 + `windows-2025-vs2026`/v145 legs); tag pushes run the tests, verify SHA-256 end to end and publish the release assets.

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
