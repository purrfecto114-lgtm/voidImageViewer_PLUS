# void Image Viewer (Touch Beta)

[![pre-release](https://img.shields.io/badge/status-beta-orange.svg)](https://github.com/purrfecto114-lgtm/voidImageViewer/releases)
[![release](https://img.shields.io/github/v/release/purrfecto114-lgtm/voidImageViewer?include_prereleases&display_name=tag)](https://github.com/purrfecto114-lgtm/voidImageViewer/releases)
[![license](https://img.shields.io/badge/license-MIT-blue.svg)](LICENSE)

> **⚠️ BETA** — This is an experimental fork of [voidtools/voidImageViewer](https://github.com/voidtools/voidImageViewer) with new **touch optimizations** and **on-screen zoom controls**. It is a pre-release for testing. Please report issues in the [issue tracker](https://github.com/purrfecto114-lgtm/voidImageViewer/issues).

A lightweight image viewer for Windows with animated GIF/WEBP support.  
Opens and displays BMP, GIF, ICO, PNG, JPG, TIF and WEBP images as fast as possible.  
Animate GIF/WEBP files as accurately as possible.  

[Download](#download)<br/>
[What's new in this beta](#whats-new-in-this-beta)<br/>
[Touch & zoom controls](#touch--zoom-controls)<br/>
[Build from source](#build-from-source)<br/>
[See also](#see-also)<br/>
<br/><br/><br/>



Download
--------
Pre-release binaries are published on the GitHub Releases page:

https://github.com/purrfecto114-lgtm/voidImageViewer/releases

Latest stable release of the upstream project:

https://github.com/voidtools/voidImageViewer/releases

https://www.voidtools.com/forum/viewtopic.php?t=5623
<br/><br/><br/>



What's new in this beta
--------
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
| Floating zoom bar | Zoom out, zoom in, 1:1, best fit — visible in windowed and fullscreen modes |

Notes:

- Gestures require Windows 7 or later with touch hardware.
- Single finger touch input is intentionally translated to mouse input so existing click actions (`left_click_action`, `right_click_action`...) are unaffected.
- The floating zoom controls can be toggled from the **View → Zoom Controls** menu item.
<br/><br/><br/>



Build from source
--------
The project is plain C + Win32 API and builds with Visual Studio:

1. Open `vs2019/voidImageViewer.sln` (VS2019+, v142 toolset) or `vs2026/voidImageViewer.sln` (VS2026, v145 toolset).
2. Build the `voidImageViewer` project (x64 or Win32).
3. `viv.exe` is output to the configured output directory.

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
