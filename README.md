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
**1.0.02 — third audit round: decode budget + real machine smoke test:**

- **Pixel budget** — both decoders refuse canvases over 100 MP *before* any allocation. A 428 KB hostile header claiming 20000×20000 used to hit the allocator and die with a fatal dialog; it now fails like any unloadable file.
- **Uninstaller identity** — closing the old instance verifies the process image name (`voidImageViewer.exe`) instead of trusting the window class name alone; a foreign program reusing the class name is left alone.
- **Anomaly samples + Windows smoke test** — `tests/make_anomaly_samples.py` generates 37 hostile samples (truncation, lying headers, zero-delay animation, frame floods, broken chunk order, trailing garbage, over-budget canvases); `tests/smoke_test.ps1` opens every one on a real machine and fails on any crash.

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
