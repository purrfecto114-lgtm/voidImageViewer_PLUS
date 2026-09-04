# void Image Viewer (Touch + Languages Beta)

[![pre-release](https://img.shields.io/badge/status-beta-orange.svg)](https://github.com/purrfecto114-lgtm/voidImageViewer_PLUS/releases)
[![release](https://img.shields.io/github/v/release/purrfecto114-lgtm/voidImageViewer_PLUS?include_prereleases&display_name=tag)](https://github.com/purrfecto114-lgtm/voidImageViewer_PLUS/releases)
[![license](https://img.shields.io/badge/license-MIT-blue.svg)](LICENSE)

> **⚠️ BETA** — This is an experimental fork of [voidtools/voidImageViewer](https://github.com/voidtools/voidImageViewer) with new **touch optimizations**, **on-screen zoom controls** and a **bilingual installer + UI language switcher**. It is a pre-release for testing. Please report issues in the [issue tracker](https://github.com/purrfecto114-lgtm/voidImageViewer_PLUS/issues).

A lightweight image viewer for Windows with animated GIF/WEBP support.  
Opens and displays BMP, GIF, ICO, PNG, JPG, TIF and WEBP images as fast as possible.  
Animate GIF/WEBP files as accurately as possible.  

[Download](#download)<br/>
[What's new in this beta](#whats-new-in-this-beta)<br/>
[Touch & zoom controls](#touch--zoom-controls)<br/>
[Languages](#languages)<br/>
[Build from source](#build-from-source)<br/>
[See also](#see-also)<br/>
<br/><br/><br/>



Download
--------
Pre-release binaries are published on the GitHub Releases page:

https://github.com/purrfecto114-lgtm/voidImageViewer_PLUS/releases

Latest stable release of the upstream project:

https://github.com/voidtools/voidImageViewer/releases

https://www.voidtools.com/forum/viewtopic.php?t=5623
<br/><br/><br/>



What's new in this release candidate
--------
**Version 1.1.0-rc.3** is the second review pass (every re-review claim re-verified against evidence before touching code):

- **One re-review claim rejected with evidence** - the two finger tap gesture id was claimed wrong (`GID_TWOFINGERTAP` "should be 5"). Verified against the real winuser.h and Microsoft Learn: it really is 6 (5 is `GID_ROTATE`, 7 is `GID_PRESSANDTAP`), so the rc.2 values were already correct. A comment now guards the value so the claim does not resurface. The `last_stretch_mode` warning suppression was also verified harmless (every read is guarded).
- **WebP + transposed EXIF orientation** - webp files with orientation 5-8 reported the un-rotated canvas dimensions while the bitmap was already rotated; the status bar showed swapped sizes and the mipmap picked the wrong aspect. The dimensions now swap exactly like the GDI+ path.
- **Uninstall string bounds** - the quoted uninstall command was copied at a +1 offset with the full buffer size, which could write one wchar past the buffer for a maximum length install path.
- **Jump-to dialog no longer swallows WM_QUIT** - if Windows asks the app to exit while the jump-to dialog is open, the quit request is re-posted so the app actually terminates instead of blocking forever.
- **Zero-file drops** - a drop with zero files (a cancelled drag) no longer clears the current playlist.

**Version 1.1.0-rc.2** is a review-fixes release (external code audit, every fix verified against the source):

- **Touch gestures actually enabled** - the SetGestureConfig wrapper had shifted parameter types, so the app asked Windows to configure *zero* gestures, and the gesture ids were wrong (2 is GID_END, not a gesture). Pinch zoom, two finger pan (with inertia, single finger pan still the mouse) and two finger tap are now really configured.
- **Installed programs list** - the app now writes its own uninstall key (display name, version, icon, quoted uninstall command; HKLM for admin installs, HKCU otherwise) during /install and removes it on uninstall. This completes the beta.13 fix: the crash was fixed then, but the registration itself was never written anywhere.
- **Security hardening** - all 10 find-data filename copies are bounded to MAX_PATH (deep directories could smash the stack), the command line word parser takes a buffer size, Everything IPC WM_COPYDATA replies are validated field by field before trusting sender offsets, and the pasted CF_DIB is checked against the clipboard global size before its bits are read.
- **WebP transparency honors the backdrop** - transparent WebP pixels are composited over the selected backdrop (checkerboard, black, white, custom color, or the dark-aware window background) exactly like PNG/GIF, instead of being pre-flattened onto the plain window background color.
- **Smaller fixes** - zero duration frames can no longer stall the animation jump loop, the mipmap stop condition compares the height axis (wide images at 50-99% zoom were one level too blurry), the status POS/RGB panes test their content instead of the pointer, save-as refuses to save the progressive preview thumbnail, stale preload events can not write past the frame array, and the RTL layout query loads GetLayout from gdi32 (it was loaded from user32 and never resolved).

**Version 1.1.0-rc.1** adds percent based zoom stepping, an always visible zoom pane and fixes the dark dialogs:

- **Zoom percent stepping** - the zoom in/out buttons now step whole 10 percents. If the current zoom is not a multiple of 10 (after a wheel or pinch gesture), the first click snaps to the nearest multiple of 10. The wheel and gestures keep their smooth proportional stepping.
- **Zoom pane** - the zoom percent is always visible as the leftmost status bar pane. Clicking it (hand cursor) opens a small dialog to type an exact percent; typing 100 enters the pixel perfect 1:1 mode.
- **Dark dialogs fix** - the beta.10 dark dialog color handler was dead code (placed before the first case label inside switch(msg), unreachable in C), so dialog backgrounds and text never actually painted dark. It now runs on every message, and the Jump To dialog is wired too.
- **Translations** - the two new zoom strings are translated in both languages (254/254 aligned).

What's new in this beta
--------
**Version 1.1.0-beta.13** fixes the beta.12 startup crash and regroups the right click menu:

- **Startup crash fix** - beta.12 called the gdi+ thumbnail API by a name that does not exist in any gdiplus.dll and treated the missing export as fatal: the exe died at startup with "missing proc GdipGetImageThumbnailImage", which also broke the installer (setup runs the exe to install itself, so nothing landed in the installed programs list). The load is now optional, uses the real export name and the correct parameter order.
- **Right click menu** - the image context menu is regrouped: zoom commands in one Zoom submenu, a slim slideshow rate ladder (the full list stays in the menu bar) and paste in the copy group.
- **Clipboard paste** - Ctrl+V with an image on the clipboard (from a browser, screenshot tool or paint) now displays it. Pasted images have no filename, so file commands (save as, delete, rename) stay disabled while it is shown.

**Version 1.1.0-beta.12** fixes the zoom stall around half size and adds progressive display:

- **Zoom stall fix** — zooming in from around half the image size no longer stalls: crossing the half-size mipmap boundary used to switch to the full image running the slow HALFTONE shrink filter over every pixel on every paint. The half mipmap is now magnified up to the full size instead (4x cheaper, looks the same).
- **Progressive display** — big images with an embedded exif thumbnail (cameras, phones) appear instantly as a low resolution preview while the full decode continues, then sharpen. Images without a thumbnail take the normal path with zero extra cost, and the preview never pollutes the last-image slot.

**Version 1.1.0-beta.11** adds the image backdrop and fixes the installer language dialog:

- **Image backdrop** — View > Backdrop selects what shows under the transparent pixels of PNG/GIF/WebP images: follow the window background (default, dark ui aware), black, white, a custom color or a checkerboard. Persisted to the ini. Built for the load-thread hot path: cached brushes, and the checkerboard is a single-FillRect pattern brush.
- **Installer language dialog** — the language selection dialog now always shows (preselecting the remembered language). Upgrade installs used to silently reuse the remembered language with no visible way to switch it.
- Also a small win for animated images with alpha: the per-frame CreateSolidBrush/FillRect/DeleteObject chain under the alpha frames is now one cached-brush FillRect.

**Version 1.1.0-beta.10** completes the dark mode coverage with dark dialogs:

- **Dark dialogs** — options (and its pages), about, rename, edit key, custom rate and the everything search now draw with the dark palette when the dark ui is active: a dark title bar, the dark explorer control style, dark text/backgrounds for statics, edits and lists, and a dark background fill. No more fully light dialogs popping out of a dark window.
- **Options navigation** — the tree view gets dark item colors, the tab controls switch to the dark explorer style, and the light tab dialog texture is skipped while dark (it would clash).
- **Cheap replies** — the dark replies use the cached dark state (beta.9) and a single reused background brush; the light path is untouched.

**Version 1.1.0-beta.9** makes the dark mode detection reliable and follows the theme live in more situations:

- **Registry-based detection** — the system theme is read from the documented `AppsUseLightTheme` registry value (the source the shell itself follows) instead of the undocumented uxtheme ordinal 132 probe, which is known to return wrong values on some Windows 10 1903+ builds. The probe stays as a fallback.
- **Live in more situations** — any `WM_SETTINGCHANGE` (not only `ImmersiveColorSet`) and `WM_THEMECHANGED` re-read the theme; high contrast on/off updates the chrome immediately; and an elevated (run-as-admin) viewer now receives the theme broadcasts too — the UIPI message filter used to block them.
- **Cached detection** — the UI used to call `SystemParametersInfo` and the uxtheme probe on every paint and status bar custom draw. The system is now probed once and the cache drops on setting changes (also a small paint-path performance win). Repaints on a theme flip are gated on the dark state actually changing.
- **Dark tooltips** — the floating zoom controls and the toolbar hover hints now tint dark with the palette (comctl tooltips have no dark theme of their own); the tint survives the toolbar recreation on a language switch.

**Version 1.1.0-beta.8** makes zooming cheaper on the input path (performance):

- **Ladder-top cache** — the reachable top of the zoom ladder is now cached behind an O(1) signature (image + viewport + layout settings), so every wheel tick and window resize stops re-walking the 1024-entry ladder and saving/restoring the zoom globals.
- **Binary-search 1:1 exits** — leaving 1:1 mode with the wheel finds the re-entry zoom level with ~10 render measurements instead of up to 1024 full render-size computations (the old linear scan was measurable work right when the zoom started moving).
- **Cached background brush** — paints no longer allocate and free a GDI brush each frame; it is rebuilt only when its color changes (config or dark theme switch).
- No behavior change: the regression tests prove the binary searches return exactly what the linear scans returned for every tested geometry.

**Version 1.1.0-beta.7** fixes the zoom display, widens the zoom range, and adds dark mode:

- **Zoom percent fixed** — the status bar showed garbage (a huge negative percent): a double pan position was passed where the printf read an int. It now always shows the real zoom (rendered pixels ÷ native pixels); 1:1 reads 100%.
- **Zoom range: exactly 1600%, and deep zoom restored** — the cap was 16× the *best fit* size: a confusing "1590%" for images that fit the window, and photos larger than the window lost deep zoom entirely (a 4000 px photo in a 1600 px window topped out at 477%). The cap is now 16× the native size for every image, the ladder is long enough to reach it, and the wheel never spins in a dead zone (the live top of the ladder is measured).
- **Dark mode (Windows 10 1809+)** — follows the Windows light/dark theme live, no restart: dark title bar, dark menus, dark status bar, dark floating zoom controls, and a dark image canvas (unless you picked your own background color). Choose Automatic / Light / Dark in Options → General. High-contrast themes and older Windows keep the light UI. Known limits: the toolbar band and the options dialog contents stay light (a dark icon set is future work), and the status-bar size grip is theme-drawn.
- **Daily scheduled regression tests** — a GitHub Actions workflow runs the zoom math, menu structure and dark-mode wiring test suites on every push and once a day.

**Version 1.1.0-beta.6** fixes the aspect ratio, zoom lag, and declutters the menus:

- **Aspect ratio can no longer change while zooming** — the legacy *Pan && Scan* feature could stretch the image independently in x and y (19 confusing menu entries such as "Increase Width" and 18 numpad shortcuts). It is now removed: the zoom ladder always scales both axes together, so the aspect stays locked at every zoom level.
- **No more ~0.5 s zoom lag at deep zoom** — a magnified paint used to stretch the *entire* destination rectangle (up to 16× the window ≈ hundreds of megapixels) even though only the visible part mattered (GDI's magnified `StretchBlt` ignores the clip region). The stretch is now limited to the visible area; every zoom step repaints in milliseconds.
- **Regrouped the View menu** — the UI toggles (caption, frame, menu bar, status bar, toolbar, zoom controls and layout presets) now live in a **View → Layout** submenu, leaving a short, scannable top level: fullscreen, slideshow and refresh sit together, zoom keeps its own submenu, and the pan && scan submenu is gone.
- **One honest zoom percent** — the status bar used to print two separate x/y percents (which read as if the aspect ratio could drift). It now shows a single zoom percentage.
- **Clearer Chinese translations** — "允许缩小图片" (was misleadingly "允许窗口缩小"), "拉伸填满窗口" (now says it stretches), interpolation filter labels are less technical, and several status strings read more naturally.
- Note: custom hotkeys for commands that moved into View → Layout reset to defaults once (hotkeys are stored by menu path); default hotkeys are unaffected.
- Regression test suite added under `tests/` (zoom math + menu structure) — run `python3 tests/zoom_math_test.py && python3 tests/menu_structure_test.py`.

**Version 1.1.0-beta.5** fixes the zoom experience:

- **Pinch zoom follows your fingers** — the zoom now exactly tracks the finger movement, in or out. The beta.3/beta.4 zoom steps were accidentally 3–4× larger than intended on images larger than the window, so fast pinches overshot wildly. Corrupted gesture distances (a Windows multi-monitor quirk) are rejected, no single gesture message can change the zoom by more than 2×, and zoom updates are applied in one pass per message so fast pinches stay smooth.
- **Fixed the zoom buttons** — a single click on the toolbar or floating zoom buttons used to jump all the way to the maximum or minimum zoom (a delta-encoding bug). Each click is now one visible ~10% step, and mouse-wheel notches step ~10% too.
- **Simplified floating zoom bar** — just two clear buttons now: zoom out and zoom in, drawn with the same icons as the toolbar. The 1:1, best fit and close buttons are gone (those commands remain in View → Zoom and the right-click menu; hide the bar via View → Zoom Controls).
- **JPEG Save As really uses quality 90** — the encoder parameter GUID was wrong, so Windows silently used its default quality.
- The status bar no longer shows garbage for extreme aspect-ratio images.

**Version 1.1.0-beta.4** adds quality of life improvements:

- **Save As (Ctrl+S)** — save the current image as **PNG, JPEG or BMP** from the File menu. Uses the built-in Windows GDI+ encoders: no new dependencies, no size cost. In-memory rotations are included, and JPEG saves at quality 90.
- **Intuitive default sort** — images are now sorted **by filename in natural order** (upstream default was: newest first). Next/previous now walks the folder in the same order you see in Explorer. Existing installations keep their saved setting (View → Navigation → Sort).
- **Hover feedback on the floating zoom bar** — the button under the cursor is highlighted. Repaints are limited to a single button and no image resources were added, keeping the executable small.

**Version 1.1.0-beta.3** makes zooming smooth and fixes drag tearing:

- **Fine 1% zoom steps** — the mouse wheel, toolbar buttons, menu commands and pinch zoom now step the zoom by 1% instead of the old coarse jumps. Fast wheel flicks zoom proportionally further in one event.
- **Smoother pinch zoom** — pinch zooming now follows your fingers with the same 1% granularity.
- **Real zoom level in the status bar** — the status bar now shows the actual zoom percentage of the image on screen, and it updates live while you zoom.
- **No more tearing while dragging** — the area exposed by a drag is repainted immediately instead of lingering stale, every frame is composed in a double buffer and presented in a single blit, and scrolling no longer smears the status bar or toolbar.
- **Closable floating zoom bar** — the floating zoom controls now have a close (X) button. Re-enable them from **View → Zoom Controls**.
- **Cleaner menus** — all zoom commands now live in one **View → Zoom** submenu (zoom in, zoom out, 1:1, best fit, reset), best fit is visible in the menu again, and the right click menu gained a zoom section.

**Version 1.1.0-beta.2** adds a bilingual installer and a UI language switcher:

- **Bilingual installer** — one setup for everyone: the first page lets you pick **English or 简体中文**. All pages, messages and the license are localized, and the choice is remembered for future installs and the uninstaller.
- **Language follows the installer** — the application starts in the language you picked in the setup.
- **In-app language switcher** — *Options → General → Language*: Auto (system), English or 简体中文. The change is applied immediately, no restart needed.
- **`language` ini setting** and a `/language auto|english|chinese` command line option for unattended installs.
- **Refreshed Simplified Chinese translation** — the entire UI now uses plain, natural Chinese (menus, dialogs, status bar and Explorer file type descriptions).
- **Run after setup** — the installer offers to launch void Image Viewer when it finishes.

**Version 1.1.0-beta.1** adds touch and zoom UI improvements on top of upstream 1.0.0.15:

- **Pinch to zoom** — two finger pinch zooms in and out, anchored at the pinch center. Uses the same zoom steps as the mouse wheel for consistent behavior.
- **Two finger pan** — drag with two fingers to scroll around a zoomed image, with inertia.
- **Two finger tap** — tap with two fingers to reset the zoom.
- **Double tap** — double tap on a touch screen to toggle between 1:1 and best fit.
- **Single finger stays mouse compatible** — single finger taps, drags and clicks keep their existing mouse semantics, so all configured click actions continue to work.
- **Zoom buttons on the toolbar** — dedicated zoom in / zoom out buttons have been added to the toolbar.
- **Floating zoom controls** — a touch friendly zoom control bar (zoom out, zoom in, 1:1, best fit) that is also available in fullscreen mode where the toolbar is hidden.
- **Touch aware toolbar** — toolbar buttons and icons are automatically enlarged on touch devices.
- **View → Zoom Controls menu setting** — show or hide the floating zoom controls, persisted in the `show_zoom_controls` ini setting (defaults to on for touch devices).
- Simplified Chinese and English localization for all new UI.
<br/><br/><br/>



Touch & zoom controls
--------

| Gesture / control | Action |
| --- | --- |
| Two finger pinch | Zoom in / out (anchored at finger center) |
| Two finger drag | Pan / scroll image (with inertia) |
| Two finger tap | Reset zoom |
| Double tap (touch) | Toggle 1:1 / best fit |
| Toolbar zoom buttons | Zoom in / out |
| Floating zoom bar | Zoom out / zoom in — visible in windowed and fullscreen modes |

Notes:

- Gestures require Windows 7 or later with touch hardware.
- Single finger touch input is intentionally translated to mouse input so existing click actions (`left_click_action`, `right_click_action`...) are unaffected.
- The floating zoom controls can be toggled from the **View → Zoom Controls** menu item.
<br/><br/><br/>



Languages
--------
The UI ships with English and Simplified Chinese.

- The **setup** asks for the language on its first page and installs the app in that language.
- Inside the app, **Options → General → Language** switches between Auto (system), English and 简体中文 on the fly.
- The setting is stored as `language=auto|english|chinese` in `voidImageViewer.ini`.
- Unattended installs can pass `/language english` (or `chinese` / `auto`) to `voidImageViewer.exe`.

<br/><br/>



Build from source
--------
The project is plain C + Win32 API and builds with Visual Studio:

1. Open `vs2019/voidImageViewer.sln` (VS2019+, v142 toolset) or `vs2026/voidImageViewer.sln` (VS2026, v145 toolset).
2. Build the `voidImageViewer` project (x64 or Win32).
3. `voidImageViewer.exe` is output to the configured output directory.
4. Optional: build the setup with NSIS 3 — `nsis\build_installer.ps1` (see `nsis\` for details). The source files are compiled with `/utf-8`, so localized strings build correctly on any system locale.

A GitHub Actions workflow (`.github/workflows/release.yml`) builds the executable automatically on `windows-latest` and attaches it to GitHub releases.
<br/><br/><br/>



void Image Viewer main window:

![Void Image Viewer Image View](https://www.voidtools.com/voidImageViewer.Image.View10.gif)
<br/><br/><br/>



void Image Viewer General Options:

![Void Image Viewer Options General](https://www.voidtools.com/voidImageViewer.Options.General10.png)
<br/><br/><br/>



void Image Viewer View Options:

![Void Image Viewer Options View](https://www.voidtools.com/voidImageViewer.Options.View10.png)
<br/><br/><br/>



void Image Viewer Controls Options:

![Void Image Viewer Image Controls](https://www.voidtools.com/voidImageViewer.Options.Controls10.png)
<br/><br/><br/>



See also
--------
Upstream project:

https://github.com/voidtools/voidImageViewer

https://www.voidtools.com/forum/viewtopic.php?t=5623
