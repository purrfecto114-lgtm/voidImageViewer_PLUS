# The two OpenGL findings — SUPERSEDED by the upstream rc

> baseline: `origin/main` @ `17c9dae` — 1.1.18-rc.1, build 118
> date: 2026-11-04 (first drafted 2026-10-27 against 1.1.17 build 115)
> state: **both findings are closed upstream. Do not apply the patch.**

This note records what the paused round found, and then what the
upstream rc did about it. It stays in the tree because the *reasoning*
is reusable and because the patch is kept for diffing — but the patch
must not land: upstream's version of the same work is **more
thorough**, and landing the draft on top of it would regress two
things the rc got right.

---

## Finding 1 — the GL path padded to a power of two without asking

**Status: CLOSED UPSTREAM.** `src/hwgl.c:326` now reads:

```c
const char *version = (const char *)_viv_gl_getstring(GL_VERSION);
int major = 0,minor = 0;
if (version) sscanf(version,"%d.%d",&major,&minor);
_viv_gl_npot = (major >= 2) || _viv_gl_has_extension("GL_ARB_texture_non_power_of_two");
```

and `src/hwgl.c:392` gates the pad on it:

```c
pot_wide = wide;
pot_high = high;
if (!_viv_gl_npot)
{
    pot_wide = 1;
    while ((pot_wide < wide) && (pot_wide < _viv_gl_max_texture)) pot_wide <<= 1;
    pot_high = 1;
    while ((pot_high < high) && (pot_high < _viv_gl_max_texture)) pot_high <<= 1;
}
```

### Why the upstream version is better than the drafted patch

The draft probed the version string **and** both extension names
(`GL_ARB_texture_non_power_of_two` and `GL_EXT_texture_non_power_of_two`).
Upstream probes the version string **and** `GL_ARB_texture_non_power_of_two`
only. On its face that looks like a gap — a GL 1.x driver that shipped
only the *EXT* form would still pad.

It is not a gap, and it is the right call: **padding is always safe,
NPOT is not.** A driver that answers a wrong version string would get
an NPOT texture it cannot take. The upstream version errs toward the
path that cannot fail.

What the draft actually **missed**, and upstream got right, is the
wrap mode. The draft left the wrap at whatever the strip had; the rc
adds a second capability gate right beside the NPOT one
(`src/hwgl.c:327-334`):

```c
if ((major > 1) || ((major == 1) && (minor >= 2)) ||
    _viv_gl_has_extension("GL_SGIS_texture_edge_clamp") || _viv_gl_has_extension("GL_EXT_texture_edge_clamp"))
{
    _viv_gl_wrap = GL_CLAMP_TO_EDGE;
}
else
{
    // the 1.1 mode keeps the leg honest where it can be (the
    // pad answers the sub-rectangle border) and names the ...
}
```

That matters: `GL_CLAMP` is `GL_CLAMP_TO_BORDER` in core desktop GL,
and the border's behaviour against an NPOT texture is a spec corner
the draft never checked. The rc's gate answers it from the driver's
own capability bits.

**If the NPOT work ever needs re-auditing, start from the rc's
implementation, not from the draft.**

---

## Finding 2 — the export readback did not disarm on the refusal path

**Status: CLOSED UPSTREAM, from the caller side.** The draft patched
the readback's early return. The rc closed it one level up, in the
caller — and that is the better place.

`src/viv_export.c` now routes every free through one helper:

```c
static void _viv_export_free(BYTE *bits)
{
    _viv_hwgl_export_end();
    _viv_hwd3d_export_end();
    mem_free(bits);
}
```

and `src/hwgl.c` / `src/hwd3d.c` each clear their pointer in their
`_export_end()`.

Why this is strictly better than the draft:

| | the draft | the rc |
|---|---|---|
| where the disarm lives | in the readback's refusal branch | in the free path, before the free |
| what it covers | the one refusal the draft saw | **every** refusal path, including ones added later |
| who owns the invariant | the backend has to remember | the **caller**, which is the one that frees the buffer |

The invariant the comment at `hwgl.c:656` states — *"the buffer belongs
to the caller (freed right after the run) - a second render must never
touch it again"* — is a statement about the **free**, so the disarm
belongs next to the free. The draft put it in the wrong place and
would have needed to be re-audited every time a new refusal path
appeared.

The rc also changed the refusal contract on the way: a GL refusal now
reports a **stage** through the exit code (`30 + stage`) rather than a
flat `3`, so the harness can tell *which* stage refused.

---

## The patch

`hwgl-npot-and-export-disarm.patch` is kept **for diffing only**. It
applies cleanly to the rc (`git apply --check` passes) because
`src/hwgl.c` is close to unchanged in the relevant regions — but
**applying it would**:

- re-introduce a second NPOT probe next to the rc's own (two sources
  of truth for one answer);
- bypass the rc's wrap-mode gate;
- move the disarm back into the readback, undoing the rc's
  caller-side fix and its "every refusal path" coverage.

**Do not apply it.** If a future round wants to extend the NPOT work,
the starting point is `src/hwgl.c:315-340` (the probe) and
`:385-400` (the pad gate).

---

## What is still open, and is not a GL-backend question

The rc's work closed the two hardware-path defects. The *memory* story
is still open, and it is not in `hwgl.c`:

- **the D3D managed texture's mirror** — `D3DPOOL_MANAGED` holds a
  system-memory copy beside the VRAM copy, so the D3D path pays about
  twice the texture. That is a D3D9 contract, not a code defect, and
  the honest fix is a byte-priced budget, not a smaller texture;
- **the GL staging buffer only grows** (`hwgl.c`'s `if (size <
  _viv_gl_stage_size)` shape) — it never shrinks, so the session's
  high-water mark is permanent;
- **the budget is measured on the original pixels** while the real
  cost is the padded canvas plus the staging buffer plus the managed
  mirror.

The upstream diagnosis (`audit/`'s own round notes, and the memory
ledger the rc added in `tools/memory_ledger.py`) is where those live.
This note covers only the two GL-backend findings, both of which are
now closed.
