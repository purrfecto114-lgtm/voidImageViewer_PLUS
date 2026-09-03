#!/usr/bin/env python3
"""Regression tests for the zoom / render math in src/viv.c.

The formulas are mirrored here exactly (see the cited viv.c functions).
Run:  python3 tests/zoom_math_test.py
Exit 0 = pass.
"""
import math
import sys

STEP = 1.01            # _VIV_ZOOM_MAX ladder step (viv.c: "each zoom step grows 1.01x")
ZOOM_MAX = 279         # _VIV_ZOOM_MAX
STEPS_PER_NOTCH = 10   # _VIV_ZOOM_STEPS_PER_NOTCH


# ---------------------------------------------------------------------------
# viv.c _viv_get_render_size(): render = fit + (16*fit - fit) * preset
# per axis, each truncated with (int). presets[i] = (1.01^i - 1) / 15.
# ---------------------------------------------------------------------------
def render_axis(fit, pos):
    preset = (STEP ** pos - 1.0) / 15.0
    return int(fit + int((16 * fit - fit) * preset))


def render(fit_w, fit_h, pos):
    return render_axis(fit_w, pos), render_axis(fit_h, pos)


def fit_size(image_w, image_h, client_w, client_h, fill_window=0,
             keep_aspect=1, allow_shrinking=1):
    """viv.c windowed best-fit size (aspect preserved, clamped to native)."""
    if not keep_aspect:
        rw = client_w if fill_window else min(client_w, image_w)
        rh = client_h if fill_window else min(client_h, image_h)
        return rw, rh
    if (client_h * image_w) / image_h < client_w:      # tall image
        rh = client_h
        rw = (client_h * image_w + image_h - 1) // image_h
    else:                                              # long image
        rw = client_w
        rh = (client_w * image_h + image_w - 1) // image_w
    if not fill_window and (rw > image_w or rh > image_h):
        return image_w, image_h
    if not allow_shrinking and (rw < image_w or rh < image_h):
        return image_w, image_h
    return rw, rh


# ---------------------------------------------------------------------------
# _viv_do_mousewheel_action(): steps = round(|delta| * STEPS_PER_NOTCH / 120)
# ---------------------------------------------------------------------------
def steps_from_delta(delta):
    if delta == 0:
        return 1
    s = (abs(delta) * STEPS_PER_NOTCH + 60) // 120
    return s if s else 1


# ---------------------------------------------------------------------------
# paint magnify path (viv.c WM_PAINT): whole-destination StretchBlt is only
# taken when the destination is fully on screen; otherwise the clip limited
# stretch bounds the work by the client area.
# ---------------------------------------------------------------------------
def paint_work_megapixels(rw, rh, client_w, client_h):
    if rw <= client_w and rh <= client_h:
        return (rw * rh) / 1e6
    return (client_w * client_h) / 1e6


# ============================================================ test geometry
GEOMETRIES = [
    # (image_w, image_h, client_w, client_h)
    (4000, 3000, 1600, 900),     # photo larger than window (the common case)
    (800, 600, 1600, 900),       # photo smaller than window
    (3840, 2160, 1920, 1080),    # 16:9 in 16:9
    (1234, 777, 1600, 900),      # awkward sizes, rounding stress
    (101, 100, 1600, 900),       # near-square tiny, ceil stress
    (6000, 400, 1600, 900),      # panorama
    (400, 6000, 1600, 900),      # tower
    (321, 241, 500, 400),        # small everything
]

failures = []


def check(name, ok, detail=""):
    if not ok:
        failures.append(f"{name}: {detail}")
        print(f"FAIL {name} {detail}")
    else:
        print(f"ok   {name} {detail}")


def t_aspect_invariant():
    """Rendered aspect must track the image aspect (<= 2px rounding) at every
    zoom level for every geometry. This is the beta.6 aspect-ratio guarantee:
    with pan&scan removed there is no code path that can decouple x from y."""
    worst = 0.0
    for (iw, ih, cw, ch) in GEOMETRIES:
        fw, fh = fit_size(iw, ih, cw, ch)
        # the fit itself is integer-rounded (upstream): allow 1px on the small
        # fit axis, plus 2px of render truncation on the small render axis.
        fit_allow = 1.0 / min(fw, fh)
        for pos in range(0, ZOOM_MAX, 7):
            rw, rh = render(fw, fh, pos)
            ar_img = iw / ih
            ar_r = rw / rh
            drift = abs(ar_r - ar_img) / ar_img
            worst = max(worst, drift)
            if drift > fit_allow + (2.0 / min(rw, rh)) + 1e-12:
                check("aspect", False, f"{iw}x{ih}@{cw}x{ch} pos {pos}: {ar_r:.4f} vs {ar_img:.4f}")
                return
    check("aspect invariant (all geometries, all levels)", True, f"worst drift {worst:.5%}")


def t_geometric_ladder():
    """The render ladder must match fit * 1.01^pos within rounding (beta.5
    fix retained). comparing against the exact formula is robust to the
    per-axis (int) truncation on tiny images."""
    for (iw, ih, cw, ch) in GEOMETRIES:
        fw, fh = fit_size(iw, ih, cw, ch)
        for pos in (0, 1, 2, 12, 70, 140, 278):
            exact_w = fw * (STEP ** pos)
            exact_h = fh * (STEP ** pos)
            rw, rh = render(fw, fh, pos)
            assert abs(rw - exact_w) <= 2.0, (iw, ih, pos, rw, exact_w)
            assert abs(rh - exact_h) <= 2.0, (iw, ih, pos, rh, exact_h)
    check("geometric ladder = fit * 1.01^pos (+/-2px)", True)


def t_pinch_steps():
    """Pinch encoding: n steps are passed as n * (120/STEPS_PER_NOTCH) delta
    units and must decode back to exactly n steps."""
    ok = True
    for n in (1, 3, 7, 12, 40, 120):
        delta = n * (120 // STEPS_PER_NOTCH)
        if steps_from_delta(delta) != n:
            check(f"pinch n={n} roundtrip", False, f"delta {delta} -> {steps_from_delta(delta)}")
            ok = False
    if ok:
        check("pinch step roundtrip (1..120)", True)
    check("wheel notch = 10 steps", steps_from_delta(120) == 10)
    check("wheel double-flick = 20 steps", steps_from_delta(240) == 20)
    check("high-res 40 = 3 steps", steps_from_delta(40) == 3)


def t_paint_work_bound():
    """Magnified paint work must be bounded by the client area once the
    destination is (partly) off screen — the beta.6 zoom-lag fix."""
    for (iw, ih, cw, ch) in GEOMETRIES:
        fw, fh = fit_size(iw, ih, cw, ch)
        for pos in (50, 150, ZOOM_MAX - 1):
            rw, rh = render(fw, fh, pos)
            work = paint_work_megapixels(rw, rh, cw, ch)
            assert work <= (cw * ch) / 1e6 + 1e-9, (iw, ih, pos, work)
    # and the worst case really is huge without the fix (sanity of the model):
    rw, rh = render(1200, 900, ZOOM_MAX - 1)
    assert (rw * rh) / 1e6 > 100
    check("paint work bounded by client", True,
          f"deep zoom {rw}x{rh} -> {1600*900/1e6:.2f} MP work")


def t_status_single_percent():
    """The status format must show exactly ONE zoom percent (the beta.6 fix:
    two independent percents implied x/y zoom could diverge)."""
    for name, path in (("en", "src/localization_en_us.h"),
                       ("zh", "src/localization_zh_cn.h")):
        n = 0
        for line in open(path, "rb"):
            if b"POS_ZOOM_FORMAT" in line:
                n = line.count(b"%d%%")
        check(f"{name} status has exactly one percent", n == 1, f"found {n}")


def t_pinch_follows_fingers():
    """2x finger spread must roughly double the render (beta.5 guarantee kept)."""
    iw, ih, cw, ch = 4000, 3000, 1600, 900
    fw, fh = fit_size(iw, ih, cw, ch)
    pos2 = int(math.log(2.0) / math.log(STEP))
    rw0, rh0 = render(fw, fh, 0)
    rw2, rh2 = render(fw, fh, pos2)
    check("2x fingers -> ~2x render", abs(rw2 / rw0 - 2.0) < 0.02,
          f"{rw2 / rw0:.3f}")


if __name__ == "__main__":
    t_aspect_invariant()
    t_geometric_ladder()
    t_pinch_steps()
    t_paint_work_bound()
    t_status_single_percent()
    t_pinch_follows_fingers()
    print()
    if failures:
        print(f"{len(failures)} FAILURE(S)")
        sys.exit(1)
    print("ALL ZOOM MATH TESTS PASS")
