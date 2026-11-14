# What this bundle changes against upstream

> upstream baseline: `17c9dae` — 1.1.18-rc.1, build 118 (2026-10-31)
> bundle assembled: 2026-11-04

The source tree in this bundle is upstream `17c9dae` **plus** the
changes below. Everything else is upstream verbatim. Nothing in
`src/` was touched.

| # | the change | where | why |
|---|---|---|---|
| 1 | **new** — the installer's native-machine gate, proven as behaviour | `tools/nsis_arch_gate_test.py` | the old guard pinned the gate by quoting a line, which is how a wrong-enumeration literal survived a release. This one models the gate and runs every machine value through it. Verified against the old bug and against a predicate swap. |
| 2 | **new** — the CI step that runs it | `.github/workflows/tests.yml` | one step, after the byte invariant leg. The suite it guards is the installer's architecture gate; nothing else in CI covered it. |
| 3 | **new** — the mockup renderer | `tools/mockup.py`, `tools/mockup_glyph.py` | re-renders the previews from the tree's own geometry, tokens and strings, so the "before" picture cannot drift away from the code. Needs Pillow. |
| 4 | **fixed** — the GL runtime regression test's stub was missing a symbol | `tests/repair/gl_runtime.c:69` | see below. |
| 5 | **new** — the design and the two findings | `audit/2026-10-27-gui-redesign.md`, `audit/2026-10-27-hwgl-findings.md`, `audit/hwgl-npot-and-export-disarm.patch` | the design document is the spec; the hwgl note records two findings the upstream rc has since closed, and the patch is kept **for diffing only — it must not be applied**. |

---

## Change 4 in detail — a broken gate that CI never noticed

`tests/gl_runtime_regression_test.py` extracts `_viv_gl_texture_upload`
and `_viv_hwgl_render` out of `src/hwgl.c` and splices them into
`tests/repair/gl_runtime.c` at a `/* PRODUCTION */` marker, then
compiles the result against a real Mesa EGL context.

The upstream rc added a file-scope global to `src/hwgl.c`:

```c
static GLint _viv_gl_wrap = GL_CLAMP;          /* src/hwgl.c:108 */
```

and the extracted upload body now reads it
(`src/hwgl.c:562-563`). But the stub side never declared it:

```c
static int _viv_gl_failed,_viv_gl_max_texture,_viv_gl_bgra=1,_viv_gl_npot=1;
```

so the spliced translation unit **fails to compile**:

```
gl_runtime.c:315:71: error: '_viv_gl_wrap' undeclared (first use in this function);
                        did you mean '_viv_gl_bgra'?
```

**The fix is the one line this bundle already carries:**

```c
static GLint _viv_gl_wrap = GL_CLAMP;
```

Verified: with that line, the compile error is gone and the suite
reaches the link step. (It then needs `libEGL.so.1` — the Mesa EGL
runtime — which is an OS package, so the CI leg has to install it.)

**The reason nobody noticed: the suite is not in any workflow.**
`tests/gl_runtime_regression_test.py` and `tests/qoi_decode_test.py`
both exist and neither appears in `.github/workflows/`. The second one
**passes as it stands** — it was simply never wired up.

So there are two follow-ups, and they are the natural first task for
whoever picks this up:

1. wire `tests/qoi_decode_test.py` into the CI matrix (it is green
   today, in this sandbox, on this baseline);
2. wire `tests/gl_runtime_regression_test.py` in too, and give the job
   the Mesa EGL package it needs.

---

## What is in `audit/previews/`

Ten rendered screens plus the icon sheet, all produced by
`tools/mockup.py` against **this** baseline:

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
| `glyph-sheet.png` | the 18 icons, replayed from `src/glyphs.c` |

Re-render them with:

```
python3 tools/mockup.py        # the screens
python3 tools/mockup_glyph.py  # the icon sheet
```

The renderer reads the layout constants out of the C sources at
render time (`live_metrics()` in `tools/mockup.py`), so a constant
that moves in the tree moves in the picture.
