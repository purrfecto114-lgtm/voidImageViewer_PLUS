# PROMPT — the voidImageViewer_PLUS GUI redesign, for the next implementer

> Read this first, then `audit/2026-10-27-gui-redesign.md`, then the
> previews in `audit/previews/`.
>
> baseline: `origin/main` @ `17c9dae` — **1.1.18-rc.1, build 118**
> (2026-10-31). This bundle's source tree is exactly that commit.

---

## 0. The discipline, before anything else

**The code is the only evidence.** Not `Changes.txt`, not the commit
messages, not the README's "What's new" section, not the previous
audit's report. Those are *claims*; `src/` is the *fact*.

This is not a stylistic preference — it has already paid off twice in
this exact project:

- The previous round's headline P0 was the NSIS installer's
  `IntCmp $0 9` (9 = `PROCESSOR_ARCHITECTURE_AMD64`, where the getter
  answers `IMAGE_FILE_MACHINE_AMD64` = 34404). The suite pinned the
  **neighbouring** arm64 line and nothing on the broken one, so the
  bug shipped green through a full release. The upstream rc has since
  replaced the magic numbers with x64.nsh's own named predicates.
- The same round's second and third findings (the GL path's missing
  NPOT probe, the export readback's missing disarm) were **also**
  closed upstream in the rc — and the upstream versions are *better*
  than the drafted patches. The drafted patch would have regressed
  the rc's wrap-mode gate and undone its caller-side disarm fix.

So: **before you change anything, re-verify that the problem still
exists in the tree you are holding.** The design document's §0.1 lists
the four upstream commits that landed under the design, and §0
explains what each one invalidated.

Two more rules, both earned the hard way in this tree:

- **A pin on a line is a pin on a spelling.** `tests/` pins source
  *text* with regexes; it does not compile anything. A mutation that
  changes the meaning but not the characters passes every suite. So
  a new guard should pin **behaviour** where it can.
- **Do not trust a green suite.** The suites are honest about what
  they check, but they check text. The gate is: compile it, run it,
  and *look at the pixels*.

---

## 1. What this bundle contains

```
PROMPT.md                         this file
BUNDLE-CHANGES.md                 exactly what this bundle changed against
                                  upstream 17c9dae — read it second
audit/2026-10-27-gui-redesign.md  the design: the evidence, the spec, the
                                  change map, and the Win7 feasibility check
audit/2026-10-27-hwgl-findings.md the two GL-backend findings — BOTH CLOSED
                                  UPSTREAM; kept for the reasoning, not for
                                  the patch
audit/hwgl-npot-and-export-disarm.patch   SUPERSEDED — do not apply
audit/previews/*.png              the visual target, before and after
tools/nsis_arch_gate_test.py      a NEW behaviour-level guard (see §5)
tools/mockup.py, tools/mockup_glyph.py   the mockup renderer (see §6)
src/, tests/, nsis/, build-zig/, vs2019/, vs2026/, scripts/, sim/, res/,
libwebp/, .github/, *.md, *.props      the upstream tree at 17c9dae
```

`.git/` is not in the bundle. The upstream remote is
`https://github.com/purrfecto114-lgtm/voidImageViewer_PLUS.git` and the
upstream baseline is `voidtools/voidImageViewer`.

---

## 2. The task

### 2.0 First, a half-day task that is already diagnosed

Two test suites exist in the tree and **neither is wired into any
workflow**, and one of them does not even compile. Both are diagnosed
to the line in `BUNDLE-CHANGES.md`:

- `tests/qoi_decode_test.py` — **passes today**, in this sandbox, on
  this baseline. It is simply never run. One CI step.
- `tests/gl_runtime_regression_test.py` — **fails to compile**. The
  upstream rc added a file-scope global to `src/hwgl.c`
  (`static GLint _viv_gl_wrap = GL_CLAMP;`, `src/hwgl.c:108`) and the
  extracted upload body now reads it, but the test's stub side
  (`tests/repair/gl_runtime.c:68`) never declared it. **This bundle
  already carries the one-line fix** — verify it, then wire the suite
  in (it also needs the Mesa EGL runtime package, which is an OS
  dependency the CI job has to install).

Do this one first. It is a clean, self-contained win, and it is the
same class of defect the whole project keeps hitting: **a gate that
nobody runs is not a gate.**

### 2.1 Then, the redesign

Implement the redesign in `audit/2026-10-27-gui-redesign.md` §2.

> **Quiet chrome.** Two bands become one. Every fact gets exactly one
> persistent home; a transient overlay may echo it, never own it. The
> control belongs on the label's line.

In short:

1. **One 48 dip command bar** in place of the menu bar strip (22 dip)
   plus the toolbar strip (40 dip). The menus go on the left as plain
   text, a hairline divider, then the command cluster on the right.
   Both existing strips are already custom-drawn `WS_CHILD` windows
   (`src/viv_menubar.c:729`, `src/viv_toolbar.c:1596`), so this is a
   paint-and-layout change, not new machinery.
2. **An icon-first command cluster** — `Open`, previous / next /
   play, best fit / actual size, a grouped `[−] 100% [+]` zoom
   cluster, rotate, image info. Icon-only because the labels are
   localized (the strip's width is a property of the language), and
   because a 48 dip band is what makes a **44 × 44 dip** touch target
   possible — today's button is 36 × 40 on this baseline.
3. **The zoom cluster owns the zoom** — the two handles and the
   percentage they move, in one object. The percentage leaves the
   status bar.
4. **The status bar becomes a fact strip** (26 dip, three panes):
   file position, dimensions, frame counter. The pixel position and
   pixel RGB panes move out of the strip and become a chip that
   follows the cursor. The date pane goes.
5. **The settings row system** — the control rides the label line, and
   the description hangs under the label inside one fixed measure.

The design document is the spec: §2 has the decisions, §3 has the
exact file and function each change lands in, and §4 has the previews.

---

## 3. The two findings that are load-bearing

### 3.1 The settings row misalignment (measured, and still open)

`src/viv_settings.c:4996` puts the label of a two-line row in the
row's **top half**:

```c
label_rect.bottom = desc_id ? ctl->rect.top + (ctl->rect.bottom - ctl->rect.top) / 2
                            : ctl->rect.bottom;
```

and the label is drawn `DT_VCENTER` inside it → the label's optical
centre is at `row_top + 13`.

`src/viv_settings.c:927` centres the switch in the **whole** row:

```c
value.top = y + ((row_high = dip(_VIV_SETTINGS_ROW_HIGH_DESC)) - dip(_VIV_SETTINGS_SWITCH_HIGH)) / 2;
/* = y + (52 - 24) / 2 = y + 14  ->  centre at y + 26 */
```

So the control sits **13 dip below the label it belongs to** — 39 px at
the 300% dpi the field report came in at. The arithmetic closes
exactly on the measured 39 px.

The fix is one line of geometry: for a row with a description, the
control's vertical centre becomes the label line's centre.

### 3.2 The duplicate zoom fact

The zoom percentage is shown in **two** persistent places — the status
bar's zoom pane (`src/viv_chrome.c`'s `_VIV_STATUS_ZOOM`) and the
floating bar's percentage cell (`src/zoomui.c`'s
`_ZOOMUI_CELL_PCT`) — while the buttons that change it live in the
toolbar. Three homes for one fact.

The redesign gives it one persistent home (the command bar's zoom
cluster) and lets the floating bar echo it transiently.

---

## 4. The constraints

- **C89, plain Win32, no new dependencies.** The tree is C89 against
  the Win32 API and nothing newer is relied on.
- **Windows 7 and later must keep working.** The app still ships a
  32-bit build for the Win7 crowd. The design document §7 is the
  full check: every new element lands on an API the binary already
  links, the DPI ladder already has its `SetProcessDPIAware()` tier
  for Win7, and the touch support is the Win7 legacy gesture stack.
  **Read §7 before you write a line of layout code.**
- **Everything is in dip.** `os_logical_wide` / `os_logical_high` are
  the current dpi, and every metric in the tree scales through
  `dip()`. No hardcoded pixel constants.
- **The byte invariants are real.** `src/` and `Changes.txt` are CRLF
  (the changelog also carries a UTF-8 BOM); the python, powershell,
  markdown, yaml and shell layers are LF; python and the markdown use
  space indentation, the C uses tabs. `tests/byte_invariant_test.py`
  enforces this and it has caught real corruption four recorded times.
- **The comment style is lowercase prose that explains *why*.** The
  whole tree reads that way; match it.
- **Do not create a `docs/` directory.** `tests/menu_structure_test.py:11026`
  requires the whole directory to be absent — the split spec was
  deliberately retired. Working documents live in `audit/`.
- **Do not apply `audit/hwgl-npot-and-export-disarm.patch`.** Both of
  its findings are closed upstream and its version is the worse one.
  The reasoning is in `audit/2026-10-27-hwgl-findings.md`.

---

## 5. The new guard, and why it is in this bundle

`tools/nsis_arch_gate_test.py` is a **behaviour-level** guard for the
installer's native-architecture gate. It lifts each `!ifdef x64` /
`!ifdef arm64` block out of `nsis/installer.nsi`, models the
instructions (both the raw `IntCmp $0 <machine value>` form and
x64.nsh's `${If} ${IsNativeAMD64}` predicates), runs every value
`${GetNativeMachineArchitecture}` can answer through it, and compares
the result against the policy.

It exists because the old guard suite pinned the gate by **quoting** a
line, which is how a wrong-enumeration literal survived a release.

What it catches, verified by running it against mutated trees:

| the mutation | caught |
|---|---|
| `IntCmp $0 9` (the old bug: `PROCESSOR_ARCHITECTURE_AMD64` where the getter answers `IMAGE_FILE_MACHINE_AMD64`) | yes — wrong enum **and** wrong behaviour |
| `${IfNot} ${IsNativeAMD64}` → `${IfNot} ${IsNativeIA32}` (a predicate swap: x86 would be admitted to the x64 installer) | yes — **no text pin would see this** |
| the shipped gate | passes |

It is already wired into `.github/workflows/tests.yml` (the step is in
the bundle's workflow file). If you change `nsis/installer.nsi`, it
must stay green.

---

## 6. The mockup renderer

`tools/mockup.py` and `tools/mockup_glyph.py` draw the previews.
They are not decoration — they are how the design's numbers stay
honest:

- `tools/mockup_glyph.py` parses the vector geometry straight out of
  `src/glyphs.c` (the 48×48 design grid, the polyline / ellipse / arc
  primitives, the 1.25 device-pixel pen floor, the round caps, the
  GDI+ angle convention) and replays it in Pillow. **The icons in the
  previews are the real icons.**
- `tools/mockup.py` reads the **live layout constants** out of the C
  sources at render time (`live_metrics()`), takes the palette from
  `sim/theme-tokens.ts` (which is generated from `src/viv_theme.c`),
  and takes the strings from `src/localization_en_us.h` /
  `_zh_cn.h`.

So the "before" picture cannot drift away from the code it claims to
draw: change a constant in the C and re-run the renderer.

```
python3 tools/mockup.py        # writes audit/previews/*.png
python3 tools/mockup_glyph.py  # writes audit/previews/glyph-sheet.png
```

It needs Pillow (`pip install pillow`). It is a maintainer tool, not a
CI leg — the byte invariant suite still covers it, because it lives in
the flat `tools/` class.

**Use it.** If you change the layout, re-render the previews and look
at them before you call the work done.

---

## 7. How to verify

The gates, in the order they will bite:

```
python3 tests/zoom_math_test.py
python3 tests/menu_structure_test.py
python3 tests/simulation_test.py
python3 tests/pixel_golden_test.py
python3 tests/byte_invariant_test.py
python3 tests/native_regression_test.py mru gesture memory ini
python3 tests/qoi_decode_test.py
python3 tests/refine_regression_test.py theme hold status decimal typography corners
python3 tests/repair_regression_test.py
python3 tests/repair_integration_test.py
python3 tools/nsis_arch_gate_test.py
python3 tools/link_scan.py
```

`tests/native_regression_test.py` and `tests/refine_regression_test.py`
take their suite names as arguments and print their own usage when
given none — the lists above are the full sets. All of these are
**green on this baseline** (verified 2026-11-04), except
`tests/gl_runtime_regression_test.py`, which needs the Mesa EGL
runtime and is not wired in — see §2.0.

`tests/gl_runtime_regression_test.py` needs `libEGL.so.1`; without it
the leg compiles and then fails at the link step.

Then, on a Windows machine:

```
msbuild vs2019\voidImageViewer.vcxproj /p:Configuration=Release /p:Platform=x64 /p:PlatformToolset=v143
powershell -ExecutionPolicy Bypass -File tests\smoke_test.ps1 -ExePath build\x64\voidImageViewer.exe -SamplesDir tests\samples
powershell -ExecutionPolicy Bypass -File tests\render_golden.ps1 -ExePath build\x64\voidImageViewer.exe -SamplesDir tests\samples
```

The golden manifest is pinned, so **any change to the chrome that
changes a pixel will turn the golden leg red**. That is the point: if
the redesign changes the rendered pixels, the manifest has to be
re-bootstrapped through the dispatch flow `CONTRIBUTING.md` describes
(the `bootstrap-golden` workflow input) — **never by hand**.

Two things the suites cannot tell you, so check them yourself:

1. **Look at the pixels.** The renderer is the only way to see whether
   the row alignment, the separator height and the band heights are
   actually right.
2. **Look at a real window on Win7 and on a scaled display.** The
   300% dpi case is where the 13 dip defect was measured; the Win7
   case is where the touch target matters.

---

## 8. If you disagree with the design

The design is a proposal, not a decree. If you think a part of it is
wrong, say why **against the code**, change it, and re-render the
previews so the change is visible. The parts most worth arguing
with:

- **(a) icon-first.** It costs discoverability for language
  independence and the touch target. The app already has an "icons
  only" mode, so the alternative is to make icon-only a *setting*
  rather than the default of the merged bar.
- **(b) the zoom cluster in the command bar** rather than only in the
  floating bar. The floating bar already carries a zoom pair; the
  design's argument is that a fact and its two handles should share a
  home, not that the floating bar's zoom should disappear.
- **(c) the pixel chip.** It moves two panes out of the status bar
  into a cursor-following overlay — and it forces the settings row
  and the `statusbar_pixel_info` ini key to be re-labelled, because
  both would otherwise lie. That is a change-list item, not a
  blocker, but do not forget it.

Everything in §1 and §7 of the design document is evidence-backed and
should be re-verified rather than re-litigated. Everything in §2 is a
design choice and is open to argument.
