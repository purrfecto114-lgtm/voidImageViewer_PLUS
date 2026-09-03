#!/usr/bin/env python3
"""Regression tests for the zoom / render math in src/viv.c.

The formulas are mirrored here exactly (see the cited viv.c functions).
Run:  python3 tests/zoom_math_test.py
Exit 0 = pass.

beta.7 model: the ladder is geometric (render = fit * 1.01^pos) and each
axis is capped at 16x the LARGER of the best fit and the native size
(upstream semantics: deep zoom for large photos, exactly 1600% for images
that fit the window). the live top position _viv_zoom_pos_max() is the
first ladder entry that reaches the cap.
"""
import math
import sys

STEP = 1.01            # _VIV_ZOOM_MAX ladder step (viv.c: "each zoom step grows 1.01x")
ZOOM_MAX = 1024        # _VIV_ZOOM_MAX
STEPS_PER_NOTCH = 10   # _VIV_ZOOM_STEPS_PER_NOTCH


# ---------------------------------------------------------------------------
# viv.c _viv_get_render_size(): render = fit * 1.01^pos, per axis capped at
# 16 * max(fit, native), each truncated with (int).
# ---------------------------------------------------------------------------
def render_axis(fit, native, pos):
    value = fit * (STEP ** pos)
    cap = 16 * max(fit, native)
    if value > cap:
        value = cap
    return int(value)


def render(fit_w, fit_h, pos, image_w, image_h):
    return render_axis(fit_w, image_w, pos), render_axis(fit_h, image_h, pos)


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


def pos_max(fit_w, fit_h, image_w, image_h):
    """viv.c _viv_zoom_pos_max(): the first pos that reaches the cap."""
    cap_w = 16 * max(fit_w, image_w)
    cap_h = 16 * max(fit_h, image_h)
    for pos in range(ZOOM_MAX):
        if fit_w * (STEP ** pos) >= cap_w:
            return pos
        if fit_h * (STEP ** pos) >= cap_h:
            return pos
    return ZOOM_MAX - 1


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
        top = pos_max(fw, fh, iw, ih)
        for pos in range(0, top + 1, 7):
            rw, rh = render(fw, fh, pos, iw, ih)
            ar_img = iw / ih
            ar_r = rw / rh
            drift = abs(ar_r - ar_img) / ar_img
            worst = max(worst, drift)
            if drift > fit_allow + (2.0 / min(rw, rh)) + 1e-12:
                check("aspect", False, f"{iw}x{ih}@{cw}x{ch} pos {pos}: {ar_r:.4f} vs {ar_img:.4f}")
                return
    check("aspect invariant (all geometries, all levels)", True, f"worst drift {worst:.5%}")


def t_geometric_ladder():
    """The render ladder must match fit * 1.01^pos within rounding below the
    cap (beta.5 fix retained; the cap only snaps the last step)."""
    for (iw, ih, cw, ch) in GEOMETRIES:
        fw, fh = fit_size(iw, ih, cw, ch)
        top = pos_max(fw, fh, iw, ih)
        for pos in (0, 1, 2, 12, 70, 140, top - 1 if top >= 141 else 140):
            exact_w = fw * (STEP ** pos)
            exact_h = fh * (STEP ** pos)
            rw, rh = render(fw, fh, pos, iw, ih)
            assert abs(rw - exact_w) <= 2.0, (iw, ih, pos, rw, exact_w)
            assert abs(rh - exact_h) <= 2.0, (iw, ih, pos, rh, exact_h)
    check("geometric ladder = fit * 1.01^pos (+/-2px)", True)


def t_sixteen_x_cap():
    """The zoom ceiling is exactly 16x the LARGER of fit and native:
    - a photo that fits the window tops out at exactly 1600% (beta.6 showed
      a confusing 1590% = 1.01^278)
    - a photo larger than the window keeps deep zoom: 1600% of native
      (beta.6 lost this: a 4000px photo in a 1600px window capped at 477%)"""
    # small image: fit == native -> 1600% exactly
    iw, ih, cw, ch = 800, 600, 1600, 900
    fw, fh = fit_size(iw, ih, cw, ch)
    top = pos_max(fw, fh, iw, ih)
    rw, rh = render(fw, fh, top, iw, ih)
    pct = rw / iw * 100
    check("small image max zoom is exactly 1600%", pct == 1600.0, f"{pct:.1f}%")

    # large photo: fit < native -> still 1600% of native
    iw, ih, cw, ch = 4000, 3000, 1600, 900
    fw, fh = fit_size(iw, ih, cw, ch)
    top = pos_max(fw, fh, iw, ih)
    rw, rh = render(fw, fh, top, iw, ih)
    pct = rw / iw * 100
    check("large photo deep zoom restored (1600%)", pct == 1600.0, f"{pct:.1f}%")

    # the ladder is long enough for extreme cases
    for (iw, ih, cw, ch) in GEOMETRIES:
        fw, fh = fit_size(iw, ih, cw, ch)
        top = pos_max(fw, fh, iw, ih)
        rw, rh = render(fw, fh, top, iw, ih)
        assert rw == 16 * max(fw, iw), (iw, ih, top, rw)
        assert rh == 16 * max(fh, ih), (iw, ih, top, rh)
    check("cap = 16x max(fit, native) for every geometry", True)


def t_pos_max_no_dead_zone():
    """Positions beyond pos_max render identically; the wheel clamps at
    pos_max instead (a dead zone of identical sizes would eat wheel events)."""
    for (iw, ih, cw, ch) in GEOMETRIES:
        fw, fh = fit_size(iw, ih, cw, ch)
        top = pos_max(fw, fh, iw, ih)
        r_top = render(fw, fh, top, iw, ih)
        r_beyond = render(fw, fh, top + 5, iw, ih)
        assert r_top == r_beyond, (iw, ih, r_top, r_beyond)
        # the step below the top is still growing (no dead zone below)
        if top > 0:
            r_below = render(fw, fh, top - 1, iw, ih)
            assert r_below[0] < r_top[0] or r_below[1] < r_top[1], (iw, ih)
    check("pos_max = first cap position, no dead wheel below", True)


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
        top = pos_max(fw, fh, iw, ih)
        for pos in (50, 150, top):
            rw, rh = render(fw, fh, pos, iw, ih)
            work = paint_work_megapixels(rw, rh, cw, ch)
            assert work <= (cw * ch) / 1e6 + 1e-9, (iw, ih, pos, work)
    # and the worst case really is huge without the fix (sanity of the model):
    iw, ih, cw, ch = 4000, 3000, 1600, 900
    fw, fh = fit_size(iw, ih, cw, ch)
    rw, rh = render(fw, fh, pos_max(fw, fh, iw, ih), iw, ih)
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
    rw0, rh0 = render(fw, fh, 0, iw, ih)
    rw2, rh2 = render(fw, fh, pos2, iw, ih)
    check("2x fingers -> ~2x render", abs(rw2 / rw0 - 2.0) < 0.02,
          f"{rw2 / rw0:.3f}")


def t_status_percent_is_native_relative():
    """The displayed percent = render / native: 1:1 reads 100%, fit of a large
    photo reads below 100, and the status call passes exactly one int (the
    beta.7 fix for the negative garbage percent)."""
    # 1:1 at pos 0 for a small image (fit == native): 100%
    iw, ih, cw, ch = 800, 600, 1600, 900
    fw, fh = fit_size(iw, ih, cw, ch)
    rw, rh = render(fw, fh, 0, iw, ih)
    pct = (rw / iw + rh / ih) / 2 * 100
    check("fit of a small image displays 100%", abs(pct - 100.0) < 1.0, f"{pct:.1f}%")

    # large photo at fit: below 100%
    iw, ih, cw, ch = 4000, 3000, 1600, 900
    fw, fh = fit_size(iw, ih, cw, ch)
    rw, rh = render(fw, fh, 0, iw, ih)
    pct = (rw / iw + rh / ih) / 2 * 100
    check("fit of a large photo displays < 100%", 0 < pct < 100, f"{pct:.1f}%")


if __name__ == "__main__":
    t_aspect_invariant()
    t_geometric_ladder()
    t_sixteen_x_cap()
    t_pos_max_no_dead_zone()
    t_pinch_steps()
    t_paint_work_bound()
    t_status_single_percent()
    t_pinch_follows_fingers()
    t_status_percent_is_native_relative()
    print()
    if failures:
        print(f"{len(failures)} FAILURE(S)")
        sys.exit(1)
    print("ALL ZOOM MATH TESTS PASS")
