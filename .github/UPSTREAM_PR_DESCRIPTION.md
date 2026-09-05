# Upstream PR: dark UI pack (title bar, menu bar, strips, dialogs) + modern chrome

**Fork**: purrfecto114-lgtm/voidImageViewer_PLUS · **Branch**: `upstream-plus` · **Base**: `master`

## What this PR brings

A complete, zero-dependency dark mode and modern chrome pack for voidImageViewer, built entirely on public or dynamically-resolved Windows APIs. Nothing is forced: the light UI is untouched — every dark path activates only when the system (or the user) asks for dark, and every modern API is resolved at runtime with silent fallbacks on older builds.

### 1. Dark title bar + dark frame (Windows 10 1809+)
`DWMWA_USE_IMMERSIVE_DARK_MODE` (attribute 20, with the 1809-1909 attribute 19 fallback) applied with the dark switch. Win11 additionally gets rounded corners (attribute 33) and a canvas-matched caption color (attribute 35).

### 2. Dark menu bar that works everywhere (new in this update)
The one strip the immersive dark mode never reaches: on Windows 11 and pre-1903 builds a win32 menu bar keeps the system light color even in a dark session. The top-level items are now **owner-drawn while dark is active** — the system keeps the layout, click tracking, keyboard navigation and dropdown menus; each item paints with the dark palette (face `0x202020`, hover `0x454545`, label `0xE8E8E8`), and the `WM_NCPAINT` pass fills the empty strip right of the last item. Light mode hands the items back to the system draw. Labels measure with the **DPI-aware menu font** (`SystemParametersInfoForDpi` when available) and a DPI change re-measures the bar.

### 3. Dark status bar, toolbar strip, zoom bar
Status panes owner-drawn (`WM_DRAWITEM`, `SBT_OWNERDRAW`), the toolbar strip custom-painted with a three-tone chrome palette, the fullscreen zoom bar a layered overlay with idle fade-out — all sharing the same palette. The toolbar erase paints the strip face (a claim-only erase could leave a fresh white back-buffer behind the transparent toolbar buttons).

### 4. Dark dialogs
`AllowDarkModeForWindow` + `DarkMode_Explorer` applied per control via `EnumChildWindows` at `WM_INITDIALOG` — comboboxes, check glyphs and push buttons inside the options pages draw dark instead of the half-dark dialog the plain class theme leaves.

### 5. PerMonitorV2 DPI
Manifest declares `PerMonitorV2` (+ `PerMonitor` fallback) in the SMI/2016 namespace; `WM_DPICHANGED` re-reads the DPI, accepts the suggested rect and rebuilds every scaled resource (toolbar glyphs, menu font, zoom bar metrics, layout). Mixed-DPI dragging stays sharp instead of bitmap-stretched.

### 6. Vector toolbar icons
The eight toolbar/zoom icons are drawn at runtime as GDI+ line art (48-unit grid, round caps) in the current theme color at any size — no `.ico` frames left to blur at high DPI (net −34KB of icons).

### 7. Field-feedback hardening
Pinch-zoom distance floor (collapsed fingers freeze + re-baseline instead of exploding the zoom), 1600% zoom ceiling (16× native, never below the fit floor), Options moved to File (the Windows convention) with the Layout submenu completed, status bar layout with the resolution pinned bottom-right and a right-cluster fallback chain, and the manifest `supportedOS` GUID list (without the Windows 10 GUID the immersive dark menus refuse to theme).

## Compatibility
- Light mode: byte-identical drawing paths (all dark paths gated).
- Windows 7/8: every dark/modern API is resolved dynamically and silently skipped; the app keeps its classic look.
- No new dependencies; C89 discipline; the full double test suite (508 + zoom math guards) and three CI legs (tests, v143, v145) run green.

## Test plan
- Dark system + Win11: the menu bar goes dark (the headline fix — previously the last white strip).
- Dark system + Win10 1903+: the bar was already dark via uxtheme; the owner-draw renders the same palette (consistent either way).
- Light system: menu bar drawn by the system exactly as before.
- Mixed DPI: drag across monitors — the menu bar re-measures with the new DPI font.
- Alt+access keys still open the menus; hover/open highlight state follows the owner draw.
