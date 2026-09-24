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
Stable binaries (setup + zip, x86/x64, SHA-256 checksums):

https://github.com/purrfecto114-lgtm/voidImageViewer_PLUS/releases

The binaries are unsigned (an open-source signing account is on the roadmap) — verify each download against the release's `sha256.txt` before running.
Every release artifact also carries a build-provenance attestation: `gh attestation verify <file> --repo purrfecto114-lgtm/voidImageViewer_PLUS`
answers the workflow run and commit each file was built from (signing would still be its own round — attestation proves origin, not publisher identity).

What's new
--------
**1.1.15-rc.17 — the default-app honesty round (the current release candidate):**

- The association checkbox goes honest: Windows 10/11 keep the default app behind the UserChoice hash a third party cannot sign, so ticking an extension now registers the viewer in the Open With list (both registry homes) and, when the system default stays with another app, says so and offers the Settings > Default Apps page.
- The shell hears the registration the moment it lands (SHChangeNotify — the docs say the Explorer may not notice until reboot otherwise), and the uninstall sweeps the new homes symmetrically.
- A failed load has a face in every preset: the minimal and compact modes hide the status bar, so the failure line (not found, over-limit, over-budget, plain failure) now rides the title bar until the next successful load.

**1.1.15-rc.16 — the parallel evaluation round:** four read-only agents swept the rc.15 tree, the main thread re-read every claim, and the five behavior survivors landed (the ghost drag, the delete queue, the silent-ini boxes, the rotate ask, the usage pair) with seven guard-teeth closures. See [Changes.txt](Changes.txt).

**1.1.15-rc.15 — the pill field round:** the floating bar at 90 percent, the wall-clock fade (flicker gone at the mechanism, not the guess), and the windowed ghost fix. See [Changes.txt](Changes.txt).

**1.1.15-rc.14 — the settings footer round:** the 4K-300% report — the silently dropped footer buttons (the capacity now counts forty with a per-page census), the settings content scroll under the pinned footer, the Apply button with its dirty lamp, and the keyboard repairs. See [Changes.txt](Changes.txt).

**1.1.15-rc.13 — the field response round:** the nine-item field report — the upgrade's lost cache seat, the successor window, the animation toolbar, the renderer flip, the pill's layered-window rework, the menu retirements and the settings regrouping. See [Changes.txt](Changes.txt).

**1.1.15-rc.12 — the report fusion round:** the third fusion report's ledger — the two P1 guard teeth, the sixteen P2 line fixes, and the ci/installer trust work. See [Changes.txt](Changes.txt).

**1.1.15-rc.11 — the resume and chain round:** the resume switch's pill, the preload chain's seat gate, and the guard suite's control-machine matrix walk. See [Changes.txt](Changes.txt).

**1.1.15-rc.10 — the gui limits round:** the breathing bottom-right pair, the resurrected count dropdowns, the narrow-window give-way order, the four-digit zoom editor, and the full command-line usage page. See [Changes.txt](Changes.txt).

**1.1.15-rc.9 — the memory and cache round:** the cache ring and the preload chain you size, the resume switch, the recent/rename identity fixes, and the animation memory work. See [Changes.txt](Changes.txt).
- The image cache is a ring you size — Settings → View → **Cache count** (off / 1–8 images); walk back through what you just saw without reloading.
- Every settings row applies live as you flip it; **OK** (or Enter) persists and closes, **Apply** persists and stays, **Cancel** (or Esc, or the title ✕) rewinds to the last persisted baseline. On short work areas the page scrolls under the footer — the wheel, the page keys and the scrollbar thumb all move it.
- Preloading walks a chain you size — **Preload count** (off / 1–5 images ahead); the chain's images sit in the ring, ready before you arrive.
- The cache set holds its own memory line (half the image budget), the trim drops the oldest first, and animations stop building a mipmap chain for every frame nobody looks at — the field's high-memory reading lands at a fraction of the old residency.
- Recent files no longer reorder when a forwarded re-open of the file on screen rides an in-flight load; rename binds to the file you are looking at.
- **Resume where I left off** (Settings → General) reopens the last session's file on a blank start.

**1.1.15-rc.8 — the seventh audit response round:** clipboard/identity/threading hardening, the bilingual readme, and the release-chain pins. See [Changes.txt](Changes.txt).


- **The clipboard paste can no longer read past its own data** — a pasted CF_DIB proves the global's length before any header field is read and the whole bitmap (masks, palette, bits) before the DIB section is created; the stride rides overflow-checked math, and a wrapped width×depth product or an `INT_MIN` height falls through to the plain-bitmap path instead of feeding the size check.
- **The plain-bitmap paste answers to the same pixel budget as the decoders** — a gigabyte-sized CF_BITMAP is refused before `CopyImage` ever allocates.
- **Destructive commands bind to the image on screen** — delete, copy, move-to, rotate and the shell verbs read the displayed slot's file, so a fast next-then-delete during a load can never land on the file that has not displayed yet (navigation keeps chaining from the newest request).
- **Cross-thread state rides proper forms** — the preload flag is an immutable job snapshot at thread start, the stage marker answers Interlocked pointer forms, and the reply queue's tail repost hands a refused post's duty back (the rc.5 fix closed the enqueue side).
- **The settings save stops swallowing failures** — a refused or short write aborts the replace and keeps the last good ini; the move runs write-through behind a flush.
- **Build and release trust** — the zig builds wipe their object directories (no ghost objects from deleted sources), CI's `actions:write` lives on the one uploading job, releases serialize per tag, NSIS is pinned to 3.12.0, the bilingual encoding check runs check-only in CI, `sha256.txt` joins the attested subjects, and the installer parameters are whitelisted.
- Full narrative: `Changes.txt`.

**1.1.15-rc.7 — the reentry state round** — re-opening keeps the window's size: the state-aware activation, the minimized size-sweep retirement and the placement-backed geometry reads. Full narrative: `Changes.txt`.

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
| Floating zoom bar | One seven-cell row in both modes — prev / play-pause / next / zoom out / percent / zoom in (bottom center; fullscreen fades it out when idle) |

Gestures need Windows 7+ with touch hardware. Single-finger input stays mouse-compatible, so configured click actions are unaffected. Toggle the floating controls via **View → Floating Control Bar**. A pinch keeps shrinking below the windowed fit, down to about fit/16 (mirroring the 16× zoom cap) — `Allow shrinking` in Options keeps its meaning.

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
