# voidImageViewer_PLUS — GUI redesign proposal

> baseline: `origin/main` @ `17c9dae` — version 1.1.18-rc.1, build 118
> date: 2026-11-04 (first drafted against 1.1.17 build 115, re-verified
> against the rc)
> scope: the window chrome (menu bar, toolbar, status bar, floating bar)
> and the settings window's row system.
>
> **Every claim below is taken from the tree, not from the changelog.**
> The fork's `Changes.txt` and its commit messages are not evidence — a
> round that was described as fixed can still be broken in the code, and
> this rewrite found exactly that (see §0).

---

## 0. What changed under us, and what it invalidated

### 0.1 The rc that landed under the design

The baseline moved from `8362669` (1.1.17 build 115) to `17c9dae`
(1.1.18-rc.1 build 118) — twelve commits, 85 files, +4564/-1391. Four
of those commits touch this design directly, and **three of the four
premises of the redesign died on contact with the code**:

| what upstream landed | what it invalidates |
|---|---|
| **the GL probe round** (`eca4852`) — `src/hwgl.c:326` now sets `_viv_gl_npot` off the GL version and the ARB extension, and `:392` gates the pad on it | finding 1 of the paused patch (§ the hwgl note): the NPOT probe **now exists upstream**, and it is *better* than the drafted patch — it also gates the wrap mode (`GL_CLAMP_TO_EDGE` on GL ≥ 1.2 / with the edge-clamp extensions), which the draft had missed |
| **the readback answers** (`6e4b72b`) — `src/viv_export.c` now routes every free through `_viv_export_free()`, which calls `_viv_hwgl_export_end()` / `_viv_hwd3d_export_end()` **before** `mem_free(bits)` | finding 2 of the paused patch: the dangling pointer is closed **from the caller side**, which is the better place for it — it disarms on every refusal path, not just the one the draft patched |
| **the touch hand-off round** (`0f8a415`) and the shared UI tokens (`src/viv_theme.h:41-48`: `VIV_UI_PAD 8`, `VIV_UI_GAP 4`, `VIV_UI_RADIUS 6`, `VIV_UI_STATUS_HIGH 26`) — the toolbar's pad and gap now ride the shared tokens instead of their own `12` / `8` | **§1.1 below**: the toolbar's width crisis is **closed**. Measured on the rc, the ten labelled buttons want **1052 dip** — they now fit a 1180-dip window with 128 dip to spare. The premise "the strip does not fit" is no longer true, and the redesign's argument has to move off it. |

So §1.1 was rewritten, the icon-first rationale was re-argued (§2.1),
and the touch-target finding got *worse*, not better: with the pad down
to 8, today's button is **36 × 40 dip** — eight dip short of a 44 dip
square, where it was four dip short before.

The new baseline also brings a lot of new test surface: `tests/repair/`
(allocator, cache_budget, d3d, d3d_upload, gl, gl_lifecycle, gl_runtime,
mipmap, orientation, rotate_atomic, shell), `tests/native/qoi.c`,
`tests/qoi_decode_test.py`, `tests/refine_regression_test.py`,
`tests/repair_regression_test.py`, `tools/memory_ledger.py`,
`tools/verify_package.py`. Any work landing here has to clear those
too.

### 0.2 The previous audit's headline finding

The previous audit's headline finding was a P0 in the NSIS installer:

```nsis
IntCmp $0 9 x64_ok x64_is32 x64_is32     ; nsis/installer.nsi:338
```

`${GetNativeMachineArchitecture}` answers an `IMAGE_FILE_MACHINE_*`
value (NSIS's own `x64.nsh`: `IsNativeIA32 332 / IsNativeAMD64 34404 /
IsNativeARM64 43620`). `9` is `PROCESSOR_ARCHITECTURE_AMD64` — a
different enumeration entirely. The gate therefore matched **no
machine**, and every real x64 machine was told it was not x64.

**The 1.1.17 remote already fixed this**, and fixed it better than the
proposed patch: the magic numbers are gone, replaced by x64.nsh's own
predicates.

```nsis
${If} ${IsNativeARM64}      ; 43620
${IfNot} ${IsNativeAMD64}    ; 34404
```

So the proposed one-character patch is **obsolete** and was dropped.
What survived from the audit is the *class* of bug, and the guard that
now covers it: `tools/nsis_arch_gate_test.py` reads the gate, models it,
and runs every machine value through it. It catches the old `9` (wrong
enum **and** wrong behaviour) and it catches a predicate swap that no
quote of the line would ever see.

> The lesson generalises: a pin on a line is a pin on a spelling. A
> pin on a behaviour survives a rewrite.

*(A note on the retired spec: an earlier draft of this document listed
`docs/architecture/viv-split-spec.md` as a missing document. It is not
missing — it was **deliberately retired**, and `tests/menu_structure_test.py:11026`
now requires the whole `docs/` directory to be absent. The 1.1.17 README
and CONTRIBUTING no longer link it. The design documents that live in
this repository belong in `audit/`, and this one is here.)*

---

## 1. The problems, in the order the code shows them

### 1.1 The toolbar's geometry is still a function of the language — and the touch target shrank

> **Rewritten on 1.1.18-rc.1.** The original finding was that the strip
> does not fit: on 1.1.17 it wanted 1232 dip. The rc's shared UI tokens
> took the toolbar's pad from 12 to 8 and its gap from 8 to 4, and the
> same measurement now wants **1052 dip** — it fits. That part of the
> finding is closed, and the redesign's argument does not rest on it.

What is left is two things, and both are still true:

**(a) The geometry is still locale-coupled.** `src/viv_toolbar.c:53-59`
fixes the metrics and `:662` measures each button as
`pad*2 + icon + gap + label`. The label is localized, so the strip's
width is a property of the language — measured on the rc:

| locale | the ten labelled buttons want |
|---|---|
| English | **1052 dip** |
| Chinese | ~880 dip |

(The Chinese is narrower because the labels are short, not wider — a
draft of this document claimed the opposite and was wrong.) A layout
that has to be reasoned about in one locale is a layout that will be
wrong in the other one, and the *worst* locale is the one nobody
tests in.

**(b) The touch target regressed.** With the pad down to 8, today's
button is `pad*2 + icon` = **36 dip wide**, inside a **40 dip** bar —
a 36 × 40 target, **eight dip short** of the 44 dip square (it was
44 × 40, four dip short, on 1.1.17). And the app's touch support is
the Win7 legacy gesture stack (§7.1), so the touch case is exactly
where this app's users are. The rc made the strip narrower and the
target smaller at the same time.

The redesign answers both: an **icon-first cluster** (same width in
every language) inside a **48 dip band** (a 44 × 44 target fits), with
the hierarchy carried by grouping and weight instead of by label
length.

### 1.2 The separators do not speak the icon's height

`viv_toolbar.c:58` — `_VIV_TOOLBAR_SEP_HIGH 16`, inside a 40-dip bar,
so the line spans y 12..28. The icon box is 20 dip, spanning y 10..30
(`:1083`). A 16-dip line between 20-dip icons reads as a stub, and the
user-reported *"the line sits high, the spacing is uneven"* is what a
4-dip-short separator looks like next to the glyphs it divides.

### 1.3 Three bands, two of them carrying the same fact

The client area stacks a menu bar (`_viv_menubar_bar_high` = font
height + 6 dip), the toolbar (40 dip) and the status bar (a native
comctl status control, ~22 dip) — **84 dip of chrome** around a
picture.

Meanwhile the zoom percentage is displayed in **two** persistent
places: the status bar's zoom pane (`viv_chrome.c:2084`) and the
floating bar's percentage cell (`zoomui.c:95-102`), while the two
buttons that change it live in the toolbar. Three homes for one fact.

The status bar carries **eight pane slots** (`viv_state.h:91`,
`_VIV_STATUS_PART_MAX 8`); the live set is the zoom %, the preload /
text slot, the pixel position, the pixel RGB, the frame counter and
the **date** (`viv_chrome.c:2137`) — which has no business in an image
viewer. The two pixel panes hold a hover-only fact, and their width
is the reason the strip is wide at all.

### 1.4 The settings page orphans the label of a two-line row

This is the finding with a measurement behind it, and the code explains
it exactly.

`viv_settings.c:4994` — the label of a row with a description occupies
the row's **top half**:

```c
label_rect.bottom = desc_id ? ctl->rect.top + (ctl->rect.bottom - ctl->rect.top) / 2
                            : ctl->rect.bottom;
```

and the label is drawn `DT_VCENTER` inside it, so the label's optical
centre is at **row_top + 13**.

`viv_settings.c:927` — the switch is centred in the **whole** row:

```c
value.top = y + ((row_high = dip(_VIV_SETTINGS_ROW_HIGH_DESC)) - dip(_VIV_SETTINGS_SWITCH_HIGH)) / 2;
                 /* = y + (52 - 24) / 2 = y + 14  ->  centre at y + 26 */
```

So the control sits **13 dip below the label it belongs to** — 39 px
at the 300% dpi the field report came in at. The arithmetic closes
exactly on the measured 39 px.

The visible consequence: the label becomes an orphan (the switch reads
as belonging to the *description*, which is the line it is level with),
and the description's right edge is `ctl->value.left - 12`
(`:5010`) — a different width on every row, because the controls do not
share a column.

### 1.5 Two more, for the record

- The **gear** glyph (`glyphs.c:270-280`) is eight round-capped teeth
  on a ring. At the 20-dip toolbar size it reads as an asterisk or a
  sun, not a gear.
- The **best-fit** and **actual-size** glyphs (`:172-196`) differ only
  in the arrow heads, and at 20 dip they are hard to tell apart.

---

## 2. The redesign

> **Quiet chrome.** Two bands become one. Every fact gets exactly one
> persistent home; a transient overlay may echo it, never own it. The
> control belongs on the label's line.

### 2.1 One command bar (48 dip) in place of a menu bar + a toolbar (84 dip)

```
┌──────────────────────────────────────────────────────────────────────────┐
│ File  Edit…  View  Slideshow  Navigate  Help  │  ▱ ⓘ │ [🔍100%🔍] │ ⛶1:1 │ ◀ ▶ ▶‖ │
└──────────────────────────────────────────────────────────────────────────┘
```

- **Menus left, commands right**, in one band. The band is 48 dip
  against the 84 it replaces: **−36 dip of chrome**, one seam instead
  of two, and the image gets the room.
- **The command cluster is icon-first.** Three reasons, and the first
  one changed when the rc landed:
  1. the labels are localized, so a labelled strip's width is a
     property of the locale (§1.1a) — an icon-only cluster is the same
     width in every language, and the layout stops being a function of
     the translation;
  2. an icon button is `icon + padding` wide, so the 48 dip band can
     give it a **44 × 44 dip** touch target — today's strip cannot
     (§1.1b);
  3. the hierarchy is carried by grouping and weight rather than by
     how long a word happens to be in the current language.
  The app already ships an "icons only" mode
  (`config_toolbar_icon_only`); this makes it the default of the merged
  bar and keeps the labels in the tooltips the strip already owns.
- **Hierarchy without labels**: the transport and the view verbs ride at
  full text weight, the tools ride muted (`VIV_TK_TEXT2`), and the
  zoom cluster is the one control that owns a value — so it is the one
  control with a face (`VIV_TK_INPUT` + hairline, capsule radius).
- **The separators speak the icon's height** (20 dip, not 16) and sit
  in a 13-dip slot.
- **Every button is a 44 × 44 dip touch target.** The current strip's
  buttons are 44 × 40 — four dip short of a square — and the app's
  touch support is the Win7 legacy gesture stack (§7.1), so the band
  is 48 dip precisely so the target can be.

### 2.2 The zoom cluster owns the zoom

`[ − ] 100% [ + ]` is one grouped control: the two handles and the
number they move, in one object, next to each other. The percentage
leaves the status bar. The floating bar's percentage cell stays, but
as an **echo** of the command bar's number while the mouse is over the
canvas — a transient surface may mirror a fact, not own it.

### 2.3 The status bar becomes a fact strip (26 dip, three panes)

```
3 / 27 │ 5712 × 4284 · 24.5 MP │ Frame 1 / 1            27 images
```

- The **pixel position and pixel RGB** panes move out of the strip and
  become a **chip that follows the cursor** over the canvas. They are
  hover facts; a permanent pane is the wrong home for them. The
  settings row that switches them on (`LOCALIZATION_ID_SETTINGS_PIXEL_INFO`)
  keeps working — it still decides whether the chip appears.
- The **date** pane is dropped. A file's own dates live in the
  properties dialog.

### 2.4 The settings row system

| | today | redesigned |
|---|---|---|
| single-line row | 34 dip | 36 dip |
| two-line row | 52 dip | 56 dip |
| label line | — | 20 dip, the row's first line |
| description line | the row's bottom half, centred | 32 dip from the row top |
| **control centre** | **row centre (26 dip)** | **label line centre (10 dip)** |
| **label ↔ control** | **13 dip apart** | **0 dip apart** |
| description measure | `value.left - 12`, per row | one fixed 320-dip label column |

The control rides the label line, so the two are one object again. The
description hangs under the label inside a **fixed measure**, so every
description on the page starts and ends on the same two lines instead
of running to whatever the control's x happened to be.

Section headers get a hairline under them and 6 dip of air before their
first row, instead of floating in a gap.

### 2.5 What deliberately did not change

- The **icon family** (`src/glyphs.c`) stays. It is the app's identity
  and it draws well. §1.5's two legibility notes are recorded as
  follow-ups, not as part of this round.
- The **theme tokens** (`src/viv_theme.c` → `sim/theme-tokens.ts`) stay
  as they are. They are already a proper token system with five
  accents, and the redesign consumes them without adding one.
- The **floating bar's** shape and its 7 cells stay; only its role
  changes (transport + a zoom echo, not a second owner).

---

## 3. Where the change lands in the C

| the change | the file | the place |
|---|---|---|
| one band instead of two | `src/viv_menubar.c`, `src/viv_toolbar.c`, `src/viv_chrome.c:535` (`_viv_on_size`) | the layout owns the band heights today; it has to own one |
| icon-first cluster | `src/viv_toolbar.c:85` (the item table), `:662` (the width walk) | the width walk already has an icon-only branch — it becomes the default |
| the separators' height | `src/viv_toolbar.c:58`, `:1046-1056` | `_VIV_TOOLBAR_SEP_HIGH` and the fill |
| the zoom cluster | `src/viv_toolbar.c:85`, `src/zoomui.c` | a new grouped item in the item table |
| the fact strip | `src/viv_chrome.c:1589-2141` (the status panes) | the pane table and the paint |
| the pixel chip | `src/viv_chrome.c` | a canvas overlay next to the cursor read |
| the row system | `src/viv_settings.c:82-88` (the constants), `:927` (the switch's top), `:4994-5013` (the label and the description) | the three lines the 13 dip comes from |

---

## 4. The previews

Every pixel in these previews is derived from the tree, not drawn from
memory: the palette comes from `sim/theme-tokens.ts` (generated from
`src/viv_theme.c`), the icons are replayed from the vector geometry in
`src/glyphs.c` through the same pipeline the binary uses (48-grid
scale, 1.25 px pen floor, round caps, gdi+ arc/ellipse primitives), the
strings come from `src/localization_en_us.h` / `_zh_cn.h`, and the
metrics are the layout constants in the C.

`tools/mockup_glyph.py` parses the glyph geometry out of the C
source; `tools/mockup.py` composes the screens. The previews live in
`audit/previews/`, beside this document.

| file | what it shows |
|---|---|
| `01-current-main-dark.png` | the chrome as the binary draws it today |
| `02-redesign-main-dark.png` | the redesigned chrome |
| `03-current-settings-dark.png` | the settings general page today |
| `04-redesign-settings-dark.png` | the redesigned settings page |
| `05-row-alignment-detail.png` | the 13 dip, drawn and measured |
| `06-redesign-main-light.png` | the redesign in the light theme |
| `07-redesign-settings-light.png` | the redesign in the light theme |
| `08-redesign-main-zh.png` | the redesign in Chinese |
| `09-compare-main.png` | current \| redesign, side by side |
| `10-compare-settings.png` | current \| redesign, side by side |

> Note on `01` and `03`: these are a **reconstruction** from the
> constants and the draw order in the C. They are a faithful drawing
> of the code's geometry, not a screenshot of a running binary — the
> binary is windows-only and was not run here. The numbers they
> illustrate (the 13 dip, the 1232 dip strip) are the ones the code
> computes, and they were measured, not eyeballed.

---

## 5. What is shipped alongside the design

- `tools/nsis_arch_gate_test.py` — the installer's native-machine gate,
  proven as behaviour. Wired into `.github/workflows/tests.yml`.
- `tools/mockup_glyph.py` — the icon set, replayed from the C.
- `tools/mockup.py` — the screen composer.

The arch gate needs nothing but python. The mockup pair needs Pillow
(the icon set is drawn, not bitmapped), so they are a maintainer tool
rather than a CI leg — the byte invariant suite still covers them,
because they live in the flat `tools/` class.

## 6. Still open

- The gear / best-fit / actual-size glyph legibility notes (§1.5).
- **Three change-list items the redesign itself creates** (they come out
  of the Win7 check in §7, not out of a taste call):
  1. the pixel-info row and the `statusbar_pixel_info` ini key have to
     be re-labelled if the chip replaces the status panes (§7.3c);
  2. the dead `WM_CTLCOLOREDIT` case in `_viv_status_proc` goes with
     the status bar's zoom pane (§7.4);
  3. the icon-only cluster's touch targets have to be specified as
     44 × 44 dip, not inherited from the current 44 × 40 (§7.3b).
- Whether the icon-only command bar is the right default, or whether it
  should stay a setting. The measurement (§1.1) says the *labelled*
  strip cannot fit; the icon-only strip fits at every width the window
  can be.

---

## 7. Windows 7 and later — feasibility, checked against the code

The app still ships a 32-bit build for the Windows 7 crowd
(`.github/workflows/release.yml:236` builds `Platform=Win32`), so
"works on Win7" is a live constraint, not a courtesy. Every claim
below is read out of the tree.

### 7.1 What the app already does on Win7

| the mechanism | the code | the Win7 path |
|---|---|---|
| the manifest | `res/voidImageViewer.Manifest` | declares **comctl32 v6** (the themed status bar and tooltips), the `supportedOS` GUIDs for 7 / 8 / 8.1 / 10, and a **dual DPI declaration**: the legacy `<dpiAware>true</dpiAware>` for the pre-1607 loaders plus the 2016-namespace `PerMonitorV2, PerMonitor` (which those loaders ignore). Both can be present without fighting — the old loader takes the old tag, the new one takes the new. |
| the DPI claim | `os.c:1021-1056` | a runtime ladder, every tier `GetProcAddress`-resolved and null-guarded: `SetProcessDpiAwarenessContext` (Win10 1703+) → `SetThreadDpiAwarenessContext` (Win10 1607) → `SetProcessDpiAwareness(PER_MONITOR_AWARE)` (Win8.1 shcore) → **`SetProcessDPIAware()` (Win7 / Vista)**. |
| the DPI scale | `os.c:942`, `os.c:1766-1794`, `os.c:1849-1866` | `os_logical_wide/high` start at `GetDeviceCaps(LOGPIXELSX/Y)`; `os_window_update_dpi` **returns 0 and changes nothing** when `GetDpiForWindow` is absent (pre-1607), so the init constants stay; `os_GetSystemMetricsForDpi` falls back to plain `GetSystemMetrics`. **Every `dip()` in the tree therefore scales identically on Win7.** |
| dark mode | `os.c:1059-1079` | every Win10-only entry point is resolved from `UxTheme.dll` by **ordinal** (104 `RefreshImmersiveColorPolicyState`, 132 `ShouldAppsUseDarkMode`, 133 `AllowDarkModeForWindow`, 135 `SetPreferredAppMode`, 136 `FlushMenuThemes`) and `DwmSetWindowAttribute`. On Win7 the ordinals do not exist, the calls are skipped, and the classic theme stands. |
| touch | `os.c:988`, `viv_wndproc.c:45-50`, `viv_wndproc.c:1864-1888` | the **legacy real-time gesture** stack: `WM_GESTURE` (0x0119), `WM_GESTURENOTIFY` (0x011A) and `SetGestureConfig` — all **Windows 7**. The app does **not** use `WM_POINTER` / `GetPointerInfo` / `GetPointerMessage` (Win8.1+). The README's "gestures need Windows 7+" claim holds against the code. |
| the menu popups | `viv_menubar.c:238` | `TrackPopupMenuEx` with `TPM_LEFTALIGN | TPM_RIGHTBUTTON` (Win2000+), and the rows are **owner-drawn** (`MF_OWNERDRAW` + `dwItemData`), so the popup follows the app's own palette rather than the system's. |
| the tooltips | `viv_toolbar.c:758-760` | a comctl32 `TOOLTIPS_CLASSA` control — it wants comctl32 **v6**, which the manifest declares and Win7 ships. |

### 7.2 The design's new surface, item by item

| the design element | what it needs | Win7 |
|---|---|---|
| one 48-dip command bar | a `WS_CHILD` window painted with GDI — the menu bar and the toolbar are **already** custom-drawn child windows (`viv_menubar.c:729`, `viv_toolbar.c:1596`), so merging them is painting, not new machinery | ✓ |
| the menu titles inside it | the existing `HMENU` and `TrackPopupMenuEx` | ✓ Win2000+ |
| the icon-only cluster | `DrawIconEx` + the existing comctl32 v6 tooltip | ✓ |
| the zoom cluster | `RoundRect` + `DrawTextW` on painted text — **no EDIT control** (§7.4) | ✓ |
| the fact strip | the existing comctl32 v6 status control, three panes instead of eight | ✓ |
| the pixel chip | a top-level `WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE` window following the cursor, `SetWindowRgn` for the rounded shape, `WS_EX_TRANSPARENT` to let the mouse through, `TrackMouseEvent` for the leave | ✓ Win2000+ |
| the settings row system | `RoundRect` / `FillRect` / `DrawTextW` with `DT_END_ELLIPSIS` | ✓ |

**No API the binary does not already link.** Nothing in the redesign
needs Win8, and nothing needs a per-monitor-v2 path.

### 7.3 Three things the check turned up

**(a) The 13-dip defect is a Win7 defect too.** On Win7 the process is
system-DPI aware, so `os_logical_wide` is the *system* DPI and every
`dip()` scales by the same factor — on a Win7 machine at 300% the
13-dip gap is 39 px, exactly as measured. The fix is pure GDI and
needs no per-monitor path.

**(b) The redesign improves the Win7 touch case — and the rc made it
more urgent.** On 1.1.18-rc.1 the toolbar's pad is the shared
`VIV_UI_PAD` = **8**, so today's button is `pad*2 + icon` = 16 + 20 =
**36 dip wide**, inside a **40 dip** bar — a 36 × 40 target, **eight
dip short** of a 44 dip square (on 1.1.17 it was 44 × 40, four dip
short). And Win7 is exactly where the legacy gesture stack matters
(Win7 tablets). The 48-dip command bar is what makes a **44 × 44**
target possible: 20 dip icon + 12 dip padding each side, with 4 dip
of air above and below.

**(c) The pixel-info setting's wording becomes wrong if the chip
replaces the status panes.** `config_pixel_info` is persisted as the
ini key `statusbar_pixel_info` (`config.c:988`, `:1469`) and the
settings row reads *"Show pixel info in the status bar"*
(`LOCALIZATION_ID_SETTINGS_PIXEL_INFO`). Moving the readout to a
cursor chip makes both the label and the key lie. The redesign has to
re-label the row — or keep the panes. This is a change-list item, not
a blocker.

### 7.4 A correction to the design: the zoom readout is painted text

`viv_chrome.c:1956` calls status-bar pane 0 *"the in place zoom
editor's home"*, and `_viv_status_proc` carries a `WM_CTLCOLOREDIT`
case for it (`viv_chrome.c:298`). But **there is no EDIT child of the
status bar in the tree** — the only `EDIT` control is the zoom
dialog (`viv_dialogs.c:670`, `VIV_ID_VIEW_ZOOM_SET`). The in-place
editor is retired; the comment and the colour case are left over.

Two consequences for the redesign:

1. Moving the zoom % into the command bar is a **move of painted
   text**, not the reparenting of a live control — simpler, and it
   sidesteps the EDIT-on-Win7 DPI question entirely (Win7 EDIT
   controls do not scale, though at system-DPI awareness that is a
   non-issue).
2. The dead `WM_CTLCOLOREDIT` case in `_viv_status_proc` can go with
   the status bar's zoom pane.

### 7.5 The one caveat that is Windows' fault, not the design's

On Win7 the app is **system-DPI aware only**, so dragging the window
between a 96 dpi and a 192 dpi monitor does not re-scale the UI. That
is a Windows limitation, and the redesign inherits it unchanged: the
48-dip band, the 44 dip targets and every other metric are dip-based,
so they behave identically on Win7 at whatever the system DPI is. The
redesign does not make the Win7 DPI story worse — it gives the image
36 dip more room inside it.

And on Win7 the dark theme covers the app's own surfaces (they are
owner-drawn) while the **native menu popup frame and the title bar
stay classic light** — the manifest says so itself, and that is
already today's behaviour, so nothing regresses.
