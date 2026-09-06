# Upstream PR: dark UI pack (title bar, menu bar, strips, dialogs) + modern chrome

**Fork**: purrfecto114-lgtm/voidImageViewer_PLUS · **Branch**: `upstream-plus` · **Base**: `master`

> **One-click create (title pre-filled; the ~6 KB description is pasted separately — GitHub answers URLs over ~6 KB with 414)**
>
> ```
https://github.com/voidtools/voidImageViewer/compare/master...purrfecto114-lgtm:voidImageViewer_PLUS:upstream-plus?quick_pull=1&title=Dark%20UI%20pack%20%28title%20bar%2C%20menu%20bar%2C%20toolbar%2C%20status%2C%20dialogs%29%20%2B%20PerMonitorV2%2C%20vector%20icons%2C%20touch%20hardening
> ```
>
> Open the link, paste the description below as the body (the fork's dashboard ships a one-click copy button for exactly this body), then **Create pull request**.

## CI evidence

Code-round head `98a7e9a` (the 1.0.03 version-display round - every user-visible version now shows the release identity string: the about dialog printed the four-segment 1.0.2.25, the add/remove entry printed 1.0.2 and the installer version keys printed 1.0.02.x64 while the tag said 1.0.02; five new test guards pin every display point to VERSION_STRING): `tests` (Python suites + v143 + v145 compile matrix) all green — the visible Actions history is all green (stale superseded runs were deleted earlier).

## Summary

A complete, zero-dependency dark mode and modern chrome pack for voidImageViewer, built entirely on public or dynamically-resolved Windows APIs. Nothing is forced: the light UI is untouched — every dark path activates only when the system (or the user) asks for dark, and every modern API is resolved at runtime with silent fallbacks on older builds. Shipped to users as the fork's **1.0.03** (x64/x86 installer + zip, NSIS, the three-times-audited tree with one version identity everywhere).

## The headline fix: the menu bar - the last white strip

Even with the immersive dark mode, a win32 menu bar keeps the system light color on Windows 11 and pre-1903 builds. The top-level items are now **owner-drawn while dark is active** — the system keeps the layout, click tracking, keyboard navigation and dropdown menus; each item paints the dark palette (face `0x202020`, hover `0x454545`, label `0xE8E8E8`). The `WM_NCPAINT` pass fills the strip around the items from the **drawn item rects** (a rect union recorded during the item draw — the `GetMenuBarInfo` item rects can be stale after a layout change, which left a white right half after a theme switch). Light mode hands the items back to the system draw. Labels measure with the **DPI-aware menu font** (`SystemParametersInfoForDpi` when available) and a DPI change re-measures the bar.

## Dark layers

- **Title bar / frame** — `DWMWA_USE_IMMERSIVE_DARK_MODE` (attribute 20, with the 1809-1909 attribute 19 fallback); Win11 additionally gets rounded corners (attribute 33) and a canvas-matched caption color (attribute 35).
- **Status bar / toolbar strip / zoom bar** — status panes owner-drawn (`WM_DRAWITEM`, `SBT_OWNERDRAW`), the toolbar strip custom-painted with a three-tone chrome palette, the fullscreen zoom bar a layered overlay with idle fade-out. The toolbar erase paints the strip face (a claim-only erase could leave a fresh white back-buffer behind the transparent toolbar buttons).
- **Dialogs** — `AllowDarkModeForWindow` + `DarkMode_Explorer` applied per control via `EnumChildWindows` at `WM_INITDIALOG`. On builds older than 1903 the explorer dark classes do not exist: the classic text controls **fall back to owner-drawn dark painting** (the original button type rides in a window property so the light UI can flip them back), the options **tab strip** (never dark on any build) is subclassed for a dark body and custom-drawn items, and the **open dialogs re-theme live** when the dark setting itself changes.
- **Vector toolbar icons** — the eight toolbar/zoom icons are drawn at runtime as GDI+ line art (48-unit grid, round caps, **float-point coordinates** with a minimum stroke width of 1.25 device pixels) in the current theme color at any size — no `.ico` frames left to blur at high DPI (net −34KB of icons). Toolbar **button states** (hover/pressed/checked) custom-painted dark.

## Hardening

Pinch-zoom distance floor (collapsed fingers freeze + re-baseline instead of exploding the zoom), 1600% zoom ceiling (16× native, never below the fit floor), Options moved to File (the Windows convention) with the Layout submenu completed, status bar layout with the resolution pinned bottom-right and a right-cluster fallback chain, and the manifest `supportedOS` GUID list (without the Windows 10 GUID the immersive dark menus refuse to theme).

## User-audited robustness (three full-codebase rounds)

Three user audit rounds rescanned all 30 source files plus the viv.c playlist, rotation, mipmap, gesture, save-as and IPC paths: **no new high-severity issue**; the confirmed fixes landed, 5 earlier claims were retracted after re-verification (the retraction discipline is itself evidence — the audit paths were re-checked against sandbox re-implementations and the libgdiplus source). Landed:

- **GDI+ frame-delay read hardened** — the size is seeded, both return values checked, the value pointer validated against the property buffer and the buffer freed when GDI+ did not fill it; **short delay arrays reused modulo their count** (the GIF GCE block is optional — one delay shared by all frames is common, previously every frame past the array end silently degraded to 100ms).
- **GDI+ frame-dimension count bounds-checked** before the dimension list is read (the same trust gap as the frame-delay fix, 15 lines apart in the same function: a zero or oversized count from GDI+ is rejected before any multiplication or dereference).
- **safe_size wiring completed** — the checked helpers (`safe_size_add` / `safe_size_mul` / ...) existed in the tree but almost no allocation used them (4 of ~40 sites). Every allocation with arithmetic now goes through them: the playlist and navigation pointer arrays, the rotate pixel buffers, the backdrop checker bits, the Everything IPC query/reply sizes, the relaunch COPYDATA size, the string/utf8/ini/glyph buffers and the clipboard globals — an overflow returns SIZE_MAX and fails the allocation cleanly instead of wrapping to an undersized block.
- Smaller hardening — save-as stores the frame on screen (a multi-frame GIF paused on frame N wrote frame 0) and reserves the extension so a full buffer can never silently drop it; the uninstall registry string reserves its suffix; the string helpers terminate on conversion failure and guard bufsize 0; ini loading rejects INVALID_FILE_SIZE; a failed SetClipboardData frees the bitmap; the four dark-chrome brushes release on shutdown; the initial shuffle frees the previous index array; the shuffle/random-search seeds mix both halves of the performance counter.
- **Pixel budget (third round)** — both decoders (GDI+ and libwebp, animation and still) refuse a canvas over 100 MP *before any allocation happens*: a 428 KB hostile header claiming 20000×20000 used to reach the allocator and die with a fatal dialog on 32-bit builds; it now fails like any unloadable file.
- **Uninstaller identity (third round)** — closing the old instance no longer trusts the window class name alone: the process image name is verified as `voidImageViewer.exe` (`QueryFullProcessImageNameW`) before any close or terminate is sent, so a foreign program reusing the class name is left alone.
- **Version identity (display round)** — every user-visible version now derives from the single `VERSION_STRING` (the tag identity): the About dialog printed the four-segment `1.0.2.25`, the Settings→Apps uninstall entry printed `1.0.2` and the installer's PE version keys printed `1.0.02.x64` while the tag, the exe resources and the branding text all said `1.0.02`; five test guards pin every display point to the identity so the formats can never drift apart again.

## DPI

The manifest declares `PerMonitorV2` (+ `PerMonitor` fallback) in the SMI/2016 namespace; `WM_DPICHANGED` re-reads the DPI, accepts the suggested rect and rebuilds every scaled resource (toolbar glyphs, menu font, zoom bar metrics, layout). Mixed-DPI dragging stays sharp instead of bitmap-stretched.

## Compatibility

- Light mode: byte-identical drawing paths (all dark paths gated).
- Windows 7/8: every dark/modern API is resolved dynamically and silently skipped; the app keeps its classic look.
- No new dependencies; C89 discipline.

## Test plan

- Dark system + Win11: menu bar dark (the headline), hover/open highlight, Alt+access keys.
- Dark system + Win10 1903+: same palette via the uxtheme path — consistent either way.
- Light system: menu bar drawn by the system exactly as before.
- Mixed DPI: drag across monitors — the bar re-measures with the new DPI font.
- Hostile-input sweep (reusable kit): `tests/make_anomaly_samples.py` generates the 37-sample anomaly set (truncated files, lying headers, zero-delay animations, frame floods, broken chunk order, trailing garbage, over-budget canvases) and `tests/smoke_test.ps1` opens every sample on a real machine, failing on any crash — the over-budget canvas proves the pixel budget refuses instead of dying.
- Version surfaces: the tag, the exe properties, the installer properties, the About dialog and the Settings→Apps uninstall entry all show the identical identity string (e.g. `1.0.03`).

CI on the code-round head (`98a7e9a`): tests (Python suites: zoom math + menu structure/dark wiring/version consistency, the 13-guard first-audit suite, the 16-guard second-audit suite, the 10-guard third-audit suite and the 5-guard version-display suite) + v143 + v145 compile matrix, all green.
