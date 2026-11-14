# the quiet chrome round - what landed, and what the verification turned up

> date: 2026-11-14
> baseline: the redesign bundle on `17c9dae` (1.1.18-rc.1, build 118)
> scope: the bundle's half-day task (the lost gates) plus the full
> section 2 redesign, verified in this sandbox as far as the sandbox
> reaches: every python gate, the zig cross compiles (x64 and arm64),
> and the mockup renderer's pixels. the Windows-only legs (the smoke
> test, the golden manifest, a real window at 300% dpi and on Win7)
> stay with whoever holds a Windows machine - the design's own rule
> about looking at pixels applies to them unchanged.

## the three findings the verification turned up

1. **the bundle's own changes list carried a claim the tree refuted.**
   `BUNDLE-CHANGES.md` change 2 says the installer's behaviour-level
   gate is "wired into `.github/workflows/tests.yml` (the step is in
   the bundle's workflow file)". The tree the bundle shipped never had
   the step. The gate ran green in the sandbox the whole time - it was
   just a gate nobody ran. It rides in the workflow now, after the byte
   invariant leg, exactly where the changes list said it already was.

2. **the gl runtime suite's carried fix was a compile fix, not a
   run fix.** the bundle's one-line stub declaration gets the splice
   to compile and link; on a real Mesa EGL context (surfaceless
   llvmpipe, GL 4.5) the white-upscale pin then fails at run time -
   the edge pixels read 143/191, not 255. The root cause: the stub's
   `_viv_gl_context_create` never mirrors the production probe
   (`src/hwgl.c:326` sets `GL_CLAMP_TO_EDGE` on GL >= 1.2), so the
   harness rendered with the 1.1 `GL_CLAMP` default and the filter
   blended the texture border into the edges. The stub's context
   create now sets what the production probe answers on the machine
   the harness builds; the suite runs green end to end (verified
   against real EGL here, red before, green after).

3. **the design's §7.4 is wrong on one claim.** "there is no EDIT
   child of the status bar in the tree" - there is, and it is alive:
   `_viv_set_zoom_dialog` (`src/viv_dialogs.c`) creates it over the
   zoom pane on click, and `WM_CTLCOLOREDIT` colours it. The editor is
   not dead code, so the redesign did not delete it - it moved with
   the value: the editor is the command bar's child now, anchored to
   the cluster's percent cell, and the colour case rides the toolbar's
   proc. The design's own discipline ("the code is the only evidence")
   applies to the design too.

## what landed

- **the lost gates** (above), plus the qoi leg riding `--sanitize`.
- **the command band**: `_VIV_TOOLBAR_BAR_HIGH 48`, the menubar's
  floor at 48 dip, `_viv_menubar_strip_wide()` answering the handover,
  the toolbar anchoring beside the menus, `_viv_get_view_top()` and
  the anim window-fit counting the band once, the seam hairline, both
  surfaces on the chrome token, `config_toolbar_icon_only` defaulting
  on. the icon-only button is `_VIV_TOOLBAR_TARGET 44` - 20 dip icon,
  12 dip padding a side, 2 dip of air top and bottom - and the paging
  arrows ride the same target. the separators speak the icon's height
  (20 dip).
- **the zoom cluster**: one `_VIV_TOOLBAR_TYPE_ZOOM` seat in the item
  table replacing the pair; the capsule face (`VIV_TK_INPUT` + the
  hairline, capsule radius), the painted percent (`VIV_UI_FONT_PERCENT`,
  the format string keeping its home), the zones with the arrows'
  press/release contract, `WM_SETCURSOR`'s hand on the cell, and the
  editor's full life (open, layout, refont, commit, ime dissociation)
  re-anchored.
- **the fact strip**: the role set is index/preload/message/frame/
  dimension; the give-way is one fact deep; the zoom, date, pos and
  rgb builders, measurements, widths and texts are gone; the frame
  pane's click and hand cursor stay.
- **the pixel chip**: `_viv_pixel_chip_follow` / `_viv_pixel_chip_hide`
  - a `WS_EX_TOOLWINDOW|WS_EX_NOACTIVATE|WS_EX_TRANSPARENT` topmost
  popup, the shared radius token's round region, the pos/rgb formats,
  hooked into the read path's change branch and the leave path, and
  dismissed by both of the settings row's config paths. the row reads
  "Show pixel info near the cursor" (zh: 在光标旁显示像素信息); the
  `statusbar_pixel_info` key never moved.
- **the row system**: `ROW_HIGH 36`, `ROW_HIGH_DESC 56`,
  `LABEL_HIGH 20`, `DESC_MID 32`, `LABEL_COL 320`, the two desc-row
  control centres on the label line, the fixed label column floored at
  the control's edge, the section caption on the label line with its
  hairline and 6 dip of air. the general page's bottom walks 756 -> 798,
  the shortfall 166 -> 208, the view page scrolls 32 raw dip, the
  controls page still fits whole, the wheel step walks 34x3 -> 36x3.

## the pins that moved with the changes

every pin move is narrated in its own suite: the menu suite's row-band
define, its erase/face tokens (the band is one surface), its 48-dip
floor spelling (the 96-dpi literal hunt caught the first cut - the
floor rides `VIV_UI_SCALE` now), the icon-only default, the 44-dip
target, the fourteen-seat table, the editor's anchor, the splice growth
window (measured 37633); the simulation suite's page bottoms, wheel
step, give-way replay, budget arithmetic and the walk model (which now
rides the c's own targets and clamps its fast path home like the code
does - the old synthetic widths hid that drift); the refine suite's
status probe, rewritten against the fact strip's role set (2344 layout
cases replay the real extracted C); the changelog and stamp guards
that retired with the panes they pinned.

## one more repo note: the superseded patch stays in the bundle

`audit/hwgl-npot-and-export-disarm.patch` does not ride in the repo:
its quoted code predates the whitespace gate (space-before-tab in the
recorded hunks), and `git diff --check` reads every added byte. the
reasoning it carried lives in `audit/2026-10-27-hwgl-findings.md`, and
the patch itself stays verbatim in the bundle zip it shipped in - it
was always "for diffing only, never to be applied".

## what this round did not do

- the golden manifest is untouched: it pins image renders, and the
  image pipeline never moved.
- the gear / best-fit / actual-size glyph legibility notes (design
  section 1.5) remain open, as the design leaves them.
- the windows-only verification (a real window, a scaled display, the
  smoke and golden legs) belongs to whoever runs Windows next.
