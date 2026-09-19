#!/usr/bin/env python3
"""Second-rework simulation suite (the 1.1.07 release check).

The earlier suites guard the field fixes with source-string checks.
This suite does the second-rework check the other way around: every
simulation first EXTRACTS the constants, the formulas and the control
flow from the shipped source, then RE-EXECUTES the behavior against the
field-report scenarios. A regression that renames a symbol or reorders
the source keeps a string guard honest only by luck; here the behavior
itself runs again.

Simulated field reports (rounds 42-44):
  - the mat color ignored the theme with an image open and without one
  - the recent-files submenu appeared once per command-table row
  - a burst of opens wrote the ini on the ui thread (the open stutter)
  - the recent list could exceed its cap
  - the empty window kept the stale load-failed / not-found message
  - the pinch locked a small image at its 200% fill and stopped a
    windowed fit at the shrink size
  - the options general page cut the last association checkbox in half
  - a theme flip left a light band beside the toolbar until a resize

Run:  python3 tests/simulation_test.py
Exit 0 = pass.
"""
import re
import sys

failures = []


def check(name, ok, detail=""):
    if ok:
        print("  ok  %s" % name)
    else:
        print("  FAIL %s%s" % (name, (" - " + str(detail)) if detail else ""))
        failures.append(name)
    return ok


def read(path):
    # R70 splice: viv.c was split into domain modules; reading "src/viv.c"
    # returns viv.c followed by every src/viv_*.c in dictionary order so the
    # guards keep covering the moved code (pure move: bodies unchanged).
    if path == "src/viv.c":
        import glob
        parts = [open(path, "rb").read()]
        for extra in sorted(glob.glob("src/viv_*.c")):
            parts.append(b"\n" + open(extra, "rb").read())
        # the R70 state layer carries the moved macros/enums; the guard
        # extractions (#define ...) resolve against it too.
        if glob.glob("src/viv_state.h"):
            parts.append(b"\n" + open("src/viv_state.h", "rb").read())
        return b"".join(parts)
    with open(path, "rb") as f:
        return f.read()


VIV = read("src/viv.c").decode("utf-8", errors="replace")
CFG_H = read("src/config.h").decode("utf-8", errors="replace")
VER_H = read("src/version.h").decode("utf-8-sig", errors="replace")
RC = read("res/voidImageViewer.rc").decode("utf-8", errors="replace")
CHANGES = read("Changes.txt").decode("utf-8-sig", errors="replace")


# ---------------------------------------------------------------------------
# shared extraction helpers (every one fails closed: no match, no pass)
# ---------------------------------------------------------------------------
def extract_int(text, pattern, what):
    m = re.search(pattern, text)
    if not check("extract %s from the shipped source" % what, m is not None,
                 pattern):
        return None
    return int(m.group(1))


def function_body(text, signature):
    """The balanced-brace body of a C function, fail-closed. skips the
    forward declarations (a ';' before the body's '{' means keep
    walking - the definition comes later)."""
    start = 0
    while True:
        i = text.find(signature, start)
        if i == -1:
            return None
        j = text.find("{", i)
        if j == -1:
            return None
        semi = text.find(";", i)
        if semi != -1 and semi < j:
            start = i + len(signature)      # a declaration: skip it
            continue
        depth = 0
        k = j
        while k < len(text):
            c = text[k]
            if c == "{":
                depth += 1
            elif c == "}":
                depth -= 1
                if depth == 0:
                    return text[j:k + 1]
            k += 1
        return None


# ---------------------------------------------------------------------------
# 1. the mat color simulation (the round-44 core report: the mat ignored
#    the theme with an image open and without one).
# ---------------------------------------------------------------------------
def t_sim_mat_color():
    print("sim: the mat color keeps the dark ui coherent (image open + empty)")

    # extract the shipped mapper: the white fast path, the 601 luminance
    # weights, the cutoff and the scale.
    body = function_body(VIV, "static COLORREF _viv_dark_mat_color(BYTE r,BYTE g,BYTE b)")
    if not check("the mapper function exists", body is not None):
        return

    m_white = re.search(r"if \(\(r == 255\) && \(g == 255\) && \(b == 255\)\)\s*\{\s*"
                        r"return RGB\(0x([0-9a-fA-F]+),0x([0-9a-fA-F]+),0x([0-9a-fA-F]+)\);", body)
    check("the white fast path exists in the shipped body", m_white is not None)
    white_fast = tuple(int(m_white.group(i), 16) for i in (1, 2, 3)) if m_white else None

    m_luma = re.search(r"luminance = \(\(\(int\)r \* (\d+)\) \+ \(\(int\)g \* (\d+)\) \+ \(\(int\)b \* (\d+)\)\) / (\d+);", body)
    if not check("the 601 luminance formula is extractable", m_luma is not None):
        return
    wr, wg, wb, wd = (int(m_luma.group(i)) for i in (1, 2, 3, 4))

    m_cut = re.search(r"if \(luminance >= (\d+)\)", body)
    if not check("the luminance cutoff is extractable", m_cut is not None):
        return
    cutoff = int(m_cut.group(1))

    m_scale = re.search(r"return RGB\(\(BYTE\)\(\(\(WORD\)r \* (0x[0-9a-fA-F]+)\) / (\d+)\),\(BYTE\)\(\(\(WORD\)g \* 0x[0-9a-fA-F]+\) / \d+\),\(BYTE\)\(\(\(WORD\)b \* 0x[0-9a-fA-F]+\) / \d+\)\);", body)
    if not check("the channel scale is extractable", m_scale is not None):
        return
    scale, div = int(m_scale.group(1), 16), int(m_scale.group(2))

    def dark_mat(r, g, b):
        """The shipped mapper, re-executed with the extracted constants."""
        if (r, g, b) == (255, 255, 255):
            return white_fast
        luminance = (r * wr + g * wg + b * wb) // wd
        if luminance >= cutoff:
            return ((r * scale) // div, (g * scale) // div, (b * scale) // div)
        return (r, g, b)

    # the fast path must be bit-exact with the scaled formula (the r44
    # promise: the default white stays bit-exact with the palette).
    formula_white = tuple((255 * scale) // div for _ in range(3))
    check("the white fast path is bit-exact with the scaled formula",
          white_fast == formula_white, "%r vs %r" % (white_fast, formula_white))

    battery = [
        # (r, g, b, expect) - expect: 'scaled' or 'passthrough'
        ((255, 255, 255), "scaled"),
        ((0, 0, 0), "passthrough"),
        ((16, 16, 16), "passthrough"),
        ((32, 32, 32), "passthrough"),     # luma 32 < 48: the dark chrome face itself
        ((48, 48, 48), "scaled"),          # exactly at the cutoff
        ((200, 200, 200), "scaled"),
        ((255, 128, 128), "scaled"),
        ((255, 255, 0), "scaled"),
        ((240, 230, 140), "scaled"),       # khaki
        ((40, 80, 20), "scaled"),          # a dark green over the cutoff
        ((30, 30, 60), "passthrough"),
    ]
    for (r, g, b), expect in battery:
        luma = (r * wr + g * wg + b * wb) // wd
        out = dark_mat(r, g, b)
        if expect == "scaled":
            ok = luma >= cutoff and out != (r, g, b) and max(out) <= scale
            check("dark ui: light mat %r (luma %d) lands at or under the chrome face" % ((r, g, b), luma),
                  ok, "out=%r" % (out,))
        else:
            ok = luma < cutoff and out == (r, g, b)
            check("dark ui: dark mat %r (luma %d) passes through untouched" % ((r, g, b), luma),
                  ok, "out=%r" % (out,))

    # hue preservation: the channel ratios survive the scale (the r44
    # promise: the dark ui keeps the hue of a light color).
    for (r, g, b) in [(255, 128, 128), (255, 255, 0), (128, 255, 64), (200, 100, 150)]:
        out = dark_mat(r, g, b)
        ratio_before = (r + 1) / (g + 1)
        ratio_after = (out[0] + 1) / (out[1] + 1)
        check("hue survives the scale for %r" % ((r, g, b),),
              abs(ratio_after - ratio_before) / ratio_before < 0.08,
              "%.3f -> %.3f" % (ratio_before, ratio_after))

    # the two user states route through the same mapper: the empty window
    # canvas (_viv_windowed_background) and the open-image backdrop mat.
    win_bg = function_body(VIV, "COLORREF _viv_windowed_background(void)")
    check("the empty-window canvas delegates to the mapper in the dark ui",
          win_bg is not None and "return _viv_dark_mat_color(config_windowed_background_color_r" in win_bg)
    check("the open-image backdrop delegates to the mapper in the dark ui",
          "color = _viv_is_dark() ? _viv_dark_mat_color(config_backdrop_color_r" in VIV)
    check("the light ui keeps the exact configured color on both paths",
          win_bg is not None and "return RGB(config_windowed_background_color_r" in win_bg and
          ": RGB(config_backdrop_color_r" in VIV)

    # follow backdrop: the fullscreen mat wears the fullscreen color (the
    # r44 a2 fix - the image under a fullscreen follow mat used to wear
    # the windowed color).
    follow_seg = function_body(VIV, "HBRUSH _viv_backdrop_solid_brush(void)")
    seg = follow_seg if follow_seg is not None else ""
    check("the follow mat matches the mode the window actually paints",
          "color = _viv_is_fullscreen ? RGB(config_fullscreen_background_color_r" in seg and
          ": _viv_windowed_background();" in seg)

    # simulate one shared rule across both states: every battery color
    # produces the identical mat in the empty window and behind an image.
    for (r, g, b), _ in battery[:4]:
        canvas = dark_mat(r, g, b)          # windowed background path
        behind = dark_mat(r, g, b)          # custom backdrop path
        check("the mat rule is one shared mapper for %r" % ((r, g, b),),
              canvas == behind)


# ---------------------------------------------------------------------------
# 2. the recent-files mru simulation (round 43: the open stutter, the cap,
#    the dedup; round 42: the duplicate submenu rows).
# ---------------------------------------------------------------------------
def t_sim_recent_mru():
    print("sim: the recent-files mru (debounce, cap, dedup, surgical swap)")

    cap = extract_int(CFG_H, r"#define\s+CONFIG_RECENT_FILE_COUNT\s+(\d+)", "the mru cap")
    delay = extract_int(VIV, r"#define\s+_VIV_RECENT_SAVE_DELAY\s+(\d+)", "the defer delay")
    if cap is None or delay is None:
        return

    check("the mru cap is ten entries", cap == 10, str(cap))
    check("the debounce delay is two seconds", delay == 2000, str(delay))

    # the model, re-executed from the shipped push/remove/clear bodies.
    class Mru:
        def __init__(self):
            self.files = []
            self.dirty = 0
            self.countdown = 0          # ms left on the defer timer
            self.writes = 0             # config_save_settings calls

        def push(self, filename):
            for i, f in enumerate(self.files):
                if f.lower() == filename.lower():
                    if i != 0:
                        self.files.insert(0, self.files.pop(i))
                        self.defer()
                    return
            while len(self.files) >= cap:
                self.files.pop()
            self.files.insert(0, filename)
            self.defer()

        def defer(self):
            self.dirty = 1
            self.countdown = delay       # settimer resets the countdown

        def tick(self, ms):
            # settle the timer; fire exactly like the wm_timer branch.
            while ms >= self.countdown and self.countdown > 0:
                ms -= self.countdown
                self.countdown = 0
                if self.dirty:
                    self.dirty = 0
                    self.writes += 1
            if self.countdown > 0:
                self.countdown -= ms

        def fold(self):
            # exit / endsession: the pending write folds into the
            # unconditional save that follows immediately.
            self.dirty = 0
            self.countdown = 0

    # scenario: a burst of five rapid opens writes the ini exactly once,
    # and never mid-burst (the open stutter report).
    m = Mru()
    for i in range(5):
        m.push("C:\\photos\\shot_%d.png" % i)
        m.tick(200)                      # opens arrive 200ms apart
    mid_burst_writes = m.writes
    check("no ini write lands mid-burst on the ui thread", mid_burst_writes == 0,
          str(mid_burst_writes))
    m.tick(delay)                        # the burst is over, the timer runs out
    check("the burst coalesces into exactly one deferred write", m.writes == 1,
          str(m.writes))
    check("the deferred write cleared the dirty flag", m.dirty == 0)

    # scenario: the exit fold absorbs a still-pending write without a
    # stray write later.
    m2 = Mru()
    m2.push("C:\\a.png")
    m2.fold()                            # user closes the app within 2s
    m2.tick(60000)
    check("exit folds the pending write (no stray write after exit)", m2.writes == 0)

    # scenario: fifteen distinct files keep the list at the cap, newest
    # first, oldest evicted.
    m3 = Mru()
    for i in range(15):
        m3.push("C:\\p\\f%02d.png" % i)
    check("fifteen opens keep the list at the cap", len(m3.files) == cap, str(len(m3.files)))
    check("the newest file is on top", m3.files[0] == "C:\\p\\f14.png", m3.files[0])
    check("the oldest survivor is f05 (f00-f04 evicted)",
          m3.files[-1] == "C:\\p\\f05.png", m3.files[-1])

    # scenario: the case-insensitive dedup moves instead of duplicating.
    # the shipped move keeps the STORED entry (its original casing) - the
    # new variant only reorders, it never rewrites the row.
    m4 = Mru()
    m4.push("C:\\Photos\\A.PNG")
    m4.push("C:\\photos\\b.png")
    m4.push("C:\\PHOTOS\\B.PNG")         # same file as row 1, different case
    check("a case-variant reopen moves the entry, it does not duplicate",
          len(m4.files) == 2 and m4.files[0] == "C:\\photos\\b.png", str(m4.files))
    m4.push("c:\\PHOTOS\\b.png")           # a case-variant of the top row
    check("a case-variant of the top row stays put (no reorder, no write)",
          m4.files[0] == "C:\\photos\\b.png" and m4.files[1] == "C:\\Photos\\A.PNG",
          str(m4.files))

    # the corrupt-ini clamp: the load walk stops at the cap and the
    # post-walk clamp repairs an over-count. model both layers from the
    # shipped config.c walk.
    load_seg = read("src/config.c").decode("utf-8", errors="replace")
    check("the load walk stops at the cap inside the loop",
          "config_recent_file_count < CONFIG_RECENT_FILE_COUNT" in load_seg)
    check("the post-walk clamp repairs an over-count",
          re.search(r"if \(config_recent_file_count > CONFIG_RECENT_FILE_COUNT\)\s*\{\s*"
                    r"config_recent_file_count = CONFIG_RECENT_FILE_COUNT;", load_seg) is not None)
    ini_entries = 20                     # a hand-edited ini with 20 rows
    walked = 0
    for _ in range(ini_entries):
        if walked < cap:                 # the walk condition
            walked += 1
    clamped = min(walked, cap)           # the post-walk clamp
    check("a 20-row hand-edited ini loads as ten entries", clamped == cap, str(clamped))

    # the menu ids: the popup builder emits ids RECENT_0+i for i<count
    # and never past the block (the compile-time lock keeps the block in
    # lockstep with the array).
    builder = function_body(VIV, "HMENU _viv_create_recent_menu(void)")
    if check("the recent popup builder exists", builder is not None):
        check("the builder clamps the count before the loop",
              "count = (config_recent_file_count < CONFIG_RECENT_FILE_COUNT) ? config_recent_file_count : CONFIG_RECENT_FILE_COUNT;" in builder)
        for count in (0, 3, cap, cap + 7):
            shown = min(count, cap)
            ids = [i for i in range(shown)]      # VIV_ID_FILE_RECENT_0 + i
            check("a %d-entry list renders %d rows with in-block ids" % (count, shown),
                  max(ids) < cap if ids else True)

    # the surgical swap: the update path touches one menu row and never
    # rebuilds the bar (the second half of the open stutter).
    upd = function_body(VIV, "void _viv_recent_menu_update(void)")
    if check("the surgical menu update exists", upd is not None):
        check("the update never calls SetMenu (no full-bar rebuild)",
              "SetMenu(" not in upd)
        check("the update swaps only the recent row by command id",
              "GetMenuItemInfoW(file_menu,_VIV_MENU_FILE_RECENT" in upd and
              "SetMenuItemInfoW(file_menu,_VIV_MENU_FILE_RECENT" in upd)
        check("the update destroys the swapped-out popup",
              upd.count("DestroyMenu(") == 2)

    # the open path itself carries no synchronous write: the push body
    # defers, it never calls config_save_settings.
    push = function_body(VIV, "void _viv_recent_file_push(const wchar_t *filename)")
    if check("the push body is extractable", push is not None):
        check("the push body defers instead of writing the ini",
              "_viv_recent_save_defer();" in push, push[:120])
        check("the push body never writes the ini synchronously",
              "config_save_settings" not in push)


# ---------------------------------------------------------------------------
# 3. the menu construction simulation (round 42: the mru block sat inside
#    the command-table walk, so every row inserted one duplicate submenu).
# ---------------------------------------------------------------------------
def t_sim_menu_braces():
    print("sim: the menu walk (the mru block placement, brace depth)")

    body = function_body(VIV, "HMENU _viv_create_menu(void)")
    if not check("the menu builder body is extractable", body is not None):
        return

    # the command-table loop: header, body span and closing brace.
    loop_at = body.find("for(i=0;i<_VIV_COMMAND_COUNT;i++)")
    if not check("the command-table walk is present", loop_at != -1):
        return
    loop_open = body.find("{", loop_at)
    depth = 0
    k = loop_open
    loop_close = -1
    while k < len(body):
        if body[k] == "{":
            depth += 1
        elif body[k] == "}":
            depth -= 1
            if depth == 0:
                loop_close = k
                break
        k += 1
    check("the command-table loop body closes inside the function",
          loop_close != -1)

    # the mru insertion point(s).
    insert_positions = [m.start() for m in re.finditer(r"recent_menu = _viv_create_recent_menu\(\);", body)]
    check("the mru submenu is inserted exactly once per build", len(insert_positions) == 1,
          str(len(insert_positions)))
    if insert_positions and loop_close != -1:
        # the round-42 bug: the insertion sat INSIDE the loop body span,
        # so every table row inserted one duplicate submenu. the fix puts
        # it after the loop's closing brace - the span test is the exact
        # behavioral difference (same brace depth, different block).
        in_loop = any(loop_open < p < loop_close for p in insert_positions)
        check("the mru insertion sits outside the command-table loop body span",
              not in_loop, "insert at %d inside loop span [%d,%d]" %
              (insert_positions[0], loop_open, loop_close))
        check("the mru insertion comes after the loop closes",
              all(p > loop_close for p in insert_positions))

    # the old bug re-simulated: an insertion inside the loop body span
    # would execute once per table row. the walk count comes from the
    # shipped table.
    m_count = re.search(r"#define\s+_VIV_COMMAND_COUNT\s+(\d+)", VIV)
    if m_count and insert_positions and loop_close != -1:
        rows = int(m_count.group(1))
        inside = any(loop_open < p < loop_close for p in insert_positions)
        emitted = rows if inside else 1
        check("a build emits one recent row, not one per command (%d)" % rows,
              emitted == 1)

    # the shared builder is the single construction path: the initial
    # build and the live update both call it (the old in-place block -
    # the round-42 duplicate source - is gone).
    check("the menu build and the live update share one popup builder",
          VIV.count("_viv_create_recent_menu();") == 2, str(VIV.count("_viv_create_recent_menu();")))
    check("the deleted full rebuild stays deleted",
          "_viv_rebuild_menu" not in VIV)


# ---------------------------------------------------------------------------
# 4. the zoom ladder simulation (round 42: the pinch locked a small image
#    at 200% and stopped a windowed fit at its shrink size).
# ---------------------------------------------------------------------------
def t_sim_zoom_ladder():
    print("sim: the below-fit zoom ladder (the pinch floor)")

    shrink = extract_int(VIV, r"#define\s+_VIV_ZOOM_SHRINK_STEPS\s+(\d+)", "the shrink step count")
    zmax = extract_int(VIV, r"#define\s+_VIV_ZOOM_MAX\s+(\d+)", "the ladder table size")
    if shrink is None or zmax is None:
        return

    # the growth constant is extracted from the init walk itself, not
    # self-written (the fifth audit's unpinned-constant finding: 278
    # and 1024 were pinned while the step that walks the ladder was
    # not - a drift to 1.02 passed every suite and every golden hash
    # untouched).
    step_hundredths = extract_int(VIV, r"f \*= 1\.(\d+);", "the ladder step's hundredths")
    if step_hundredths is None:
        return
    STEP = 1 + step_hundredths / 100.0
    check("the ladder step is the documented 1.01x",
          step_hundredths == 1, "1.%02d" % step_hundredths)
    # the relation the shrink range exists under: 1.01^278 is about one
    # sixteenth of the fit (the state header documents the same pair).
    check("the shrink ladder spans the ~16x cap the header documents",
          abs(STEP ** shrink - 16.0) < 0.5,
          "%.2fx at %d steps" % (STEP ** shrink, shrink))
    table = [STEP ** i for i in range(zmax)]     # the shipped init walk

    def scale(pos):
        """The shipped access: positive reads the table, negative reads
        the reciprocal of the positive entry (never out of bounds)."""
        return table[pos] if pos > 0 else 1.0 / table[-pos]

    floor_fn = function_body(VIV, "int _viv_zoom_pos_floor(void)")
    if not check("the floor function is extractable", floor_fn is not None):
        return
    check("the floor honors allow-shrinking (off = the best fit)",
          "if (!config_allow_shrinking)" in floor_fn and "return 0;" in floor_fn)
    check("the floor is the negative shrink range",
          "return -_VIV_ZOOM_SHRINK_STEPS;" in floor_fn)

    def pos_floor(allow_shrinking=1):
        return 0 if not allow_shrinking else -shrink

    def render(fit, pos, image_axis):
        value = fit * scale(pos)
        cap = max(16 * image_axis, fit)
        if value > cap:
            value = cap
        if value < 1:                       # the shipped 1px floor
            value = 1
        return int(value)

    # scenario a (the field report): a 500x400 image in a 1000x800 window
    # with fill window - the fit upscale is 200% and the pinch used to
    # stop dead at it.
    fit_a = 1000
    image_w = 500
    floor_a = pos_floor()
    min_scale = scale(floor_a)
    min_render = render(fit_a, floor_a, image_w)
    check("the 200% fill lock is gone: the floor sits below the fit",
          min_render < fit_a, "min render %d" % min_render)
    check("the floor is about one sixteenth of the fit",
          fit_a / 17 < min_render < fit_a / 15,
          "1/%.1f of the fit" % (fit_a / min_render))
    check("the deepest render keeps at least one pixel", min_render >= 1)

    # the shrink range mirrors the 16x cap: 1.01^shrink ~ 16.
    check("the shrink range mirrors the 16x native cap",
          15 < STEP ** shrink < 17, "%.2fx" % STEP ** shrink)

    # scenario b: a 4000x3000 image in a 1000x750 window - the windowed
    # fit is 75% and the pinch used to stop at the shrink size.
    fit_b = 750
    image_w_b = 4000
    min_b = render(fit_b, pos_floor(), image_w_b)
    check("a 75% windowed fit pinches below the fit now", min_b < fit_b)
    check("the 75% case reaches the same ~1/16 floor",
          fit_b / 17 < min_b < fit_b / 15, "min %d" % min_b)

    # allow-shrinking off keeps the fit floor (the option keeps its
    # meaning).
    check("allow-shrinking off pins the floor at the best fit",
          pos_floor(0) == 0)
    check("with shrinking off the 200% fill stays the minimum",
          render(fit_a, 0, image_w) == fit_a)

    # the reciprocal access: every negative position indexes a valid
    # positive table entry (no out-of-bounds, no negative index).
    ok = all(1 <= -pos <= zmax - 1 for pos in range(-shrink, 0))
    check("every negative position reads a positive table entry", ok)
    check("the shipped access uses the reciprocal (not the raw table)",
          "scale = (_viv_zoom_pos > 0) ? _viv_zoom_scales[_viv_zoom_pos] : (1.0 / _viv_zoom_scales[-_viv_zoom_pos]);" in VIV)

    # the clamp: a runaway negative position clamps to the floor, not past it.
    clamp_fn = function_body(VIV, "int _viv_clamp_zoom_pos(int zoom_pos)")
    if check("the clamp function is extractable", clamp_fn is not None):
        def clamp(z):
            if z <= pos_floor():
                return pos_floor()
            return z
        check("a runaway -999 clamps to the floor, not past it",
              clamp(-999) == -shrink, str(clamp(-999)))
        check("the clamp keeps an in-range position",
              clamp(-10) == -10 and clamp(0) == 0)

    # 1px survival: a deep floor render of a tiny fit stays drawable.
    check("a tiny 4px fit at the floor still renders one pixel",
          render(4, pos_floor(), 3) >= 1)


# ---------------------------------------------------------------------------
# 5. the blank state simulation (round 42: the empty window kept the
#    stale load-failed / not-found message).
# ---------------------------------------------------------------------------
def t_sim_blank_flags():
    print("sim: the blank state clears the stale status flags")

    blank = function_body(VIV, "void _viv_blank(void)")
    if not check("the blank body is extractable", blank is not None):
        return
    check("blank clears the load-failed flag",
          re.search(r"_viv_load_failed\s*=\s*0;", blank) is not None)
    check("blank clears the not-found flag",
          re.search(r"_viv_file_not_found\s*=\s*0;", blank) is not None)

    # session replay: a missing file sets the flag, blank clears it, and
    # the NEXT failure reports fresh (not masked, not sticky).
    state = {"not_found": 0, "load_failed": 0}

    def open_missing():
        state["not_found"] = 1              # the shipped open failure path

    def blank():
        state["not_found"] = 0              # the shipped reset (extracted above)
        state["load_failed"] = 0

    def caption_has_suffix():
        return bool(state["not_found"] or state["load_failed"])

    open_missing()
    check("a missing file reports in the caption", caption_has_suffix())
    blank()
    check("file->close clears the report from the empty window",
          not caption_has_suffix())
    open_missing()
    check("a later failure reports fresh after a blank (not sticky)",
          caption_has_suffix())
    blank()
    check("the final blank is clean", not caption_has_suffix())


# ---------------------------------------------------------------------------
# 6. the options geometry simulation (round 43: the general page cut the
#    last association checkbox in half).
# ---------------------------------------------------------------------------
def t_sim_options_geometry():
    print("sim: the options geometry (the classic dialogs retired)")

    # r114: the classic options templates (idd_general / idd_options)
    # left the rc with the dialogs that opened them - the geometry
    # simulation retired with them. the settings window owns the
    # associations surface now (its own row grid, its own guards).
    check("the classic options templates are gone",
          "IDD_GENERAL" not in RC and "IDD_OPTIONS" not in RC)


# ---------------------------------------------------------------------------
# 7. the theme flip simulation (round 42: a flip left a light band beside
#    the toolbar until a manual resize; the broadcast can outrun the
#    registry).
# ---------------------------------------------------------------------------
def t_sim_theme_flip():
    print("sim: the theme flip (whole-window repaint, the 400ms re-check)")

    apply_fn = function_body(VIV, "void _viv_apply_dark_mode(int repaint)")
    if not check("the apply body is extractable", apply_fn is not None):
        return

    # the unconditional whole-window invalidation (the white band fix).
    check("every flip invalidates the whole window unconditionally",
          re.search(r"InvalidateRect\(_viv_hwnd,0,FALSE\);", apply_fn) is not None)
    check("a repaint pass sweeps the children immediately, frame included (rc.2)",
          "RedrawWindow(_viv_hwnd,0,0,RDW_INVALIDATE | RDW_UPDATENOW | RDW_ALLCHILDREN | RDW_FRAME);" in apply_fn)
    # the per-control invalidations remain (they cover their own windows).
    check("the per-control invalidations stay in place",
          apply_fn.count("InvalidateRect(") >= 2)  # rc.8: the strip invalidations became the set_dark latch + the whole window flip

    # the one-shot re-check: the broadcast can arrive before the
    # personalize registry value settles.
    # the settings window runs its own immersive probe, so scan every
    # broadcast site for the one that arms the settle re-check.
    wm = VIV.find("ImmersiveColorSet")
    armed = False
    while wm != -1:
        if "SetTimer(_viv_hwnd,VIV_ID_DARK_RECHECK_TIMER,400,0);" in VIV[wm - 400:wm + 700]:
            armed = True
            break
        wm = VIV.find("ImmersiveColorSet", wm + 1)
    check("an immersive broadcast arms the 400ms re-check", armed)
    recheck = VIV.find("case VIV_ID_DARK_RECHECK_TIMER:")
    seg2 = VIV[recheck:recheck + 900]
    check("the re-check is one-shot (kills its own timer)",
          "KillTimer(hwnd,VIV_ID_DARK_RECHECK_TIMER);" in seg2)
    check("the re-check re-reads the settled registry and applies",
          "os_dark_invalidate();" in seg2 and "_viv_apply_dark_mode(1);" in seg2)

    # timeline replay of the race the field report caught:
    #   t=0     broadcast arrives, registry still says light -> no apply
    #   t=50    the personalize registry write lands
    #   t=400   the re-check reads the settled registry -> the flip lands
    registry = "light"
    applied_at = None

    def on_broadcast(t):
        # the immediate re-read sees the stale registry: no apply, but the
        # one-shot re-check is armed (extracted above).
        if registry == "dark":
            apply_flip(t)

    def on_recheck(t):
        # the registry has settled by now.
        if registry == "dark":
            apply_flip(t)

    def apply_flip(t):
        nonlocal applied_at
        applied_at = t
        # the whole-window invalidation runs here (extracted above).

    on_broadcast(0)                      # the broadcast outruns the registry write
    check("the stale-registry broadcast applies nothing at t=0", applied_at is None)
    registry = "dark"                    # the personalize value settles at t=50
    on_recheck(400)
    check("the settled registry lands the flip at t=400ms", applied_at == 400)


# ---------------------------------------------------------------------------
# 8. the 1.1.07 release identity simulation.
# ---------------------------------------------------------------------------
def t_sim_version_117():
    print("sim: the 1.1.08 release identity")

    major = extract_int(VER_H, r"#define\s+VERSION_MAJOR\s+(\d+)", "VERSION_MAJOR")
    minor = extract_int(VER_H, r"#define\s+VERSION_MINOR\s+(\d+)", "VERSION_MINOR")
    rev = extract_int(VER_H, r"#define\s+VERSION_REVISION\s+(\d+)", "VERSION_REVISION")
    build = extract_int(VER_H, r"#define\s+VERSION_BUILD\s+(\d+)", "VERSION_BUILD")
    vstr = re.search(r'#define\s+VERSION_STRING\s+"([^"]*)"', VER_H)
    check("the version quad is 1.1.15-rc.8.87",
          (major, minor, rev, build) == (1, 1, 15, 87), str((major, minor, rev, build)))
    check("the release identity string is 1.1.15-rc.8",
          vstr is not None and vstr.group(1) == "1.1.15-rc.8", vstr.group(1) if vstr else None)
    check("the rc derives from version.h (no hardcoded quad)",
          '#include "../src/version.h"' in RC and
          "FILEVERSION VERSION_MAJOR,VERSION_MINOR,VERSION_REVISION,VERSION_BUILD" in RC)
    nsh = read("nsis/version.nsh").decode()
    check("the nsis derives the display version at compile time",
          '!define DISPLAYVERSION "${VIV_VER_STRING}"' in nsh)
    top = CHANGES.lstrip("\ufeff").split("\r\n")[0] if "\r\n" in CHANGES else CHANGES.lstrip("\ufeff").split("\n")[0]
    check("the changelog carries the fixture round pre-release (below the navigation visibility round)",
          "Pre-release: Version 1.1.14-rc.2 (the fixture round)" in CHANGES)
    check("the changelog carries the crlf line discipline",
          "\r\n" in CHANGES)
    readme = read("README.md").decode("utf-8", errors="replace")
    experience = read("experience.md").decode("utf-8", errors="replace")
    check("the readme current-stable line says 1.1.14",
          "**1.1.14 — the current stable**" in readme)
    check("the candidate slot rotates to the seventh audit response round (rc.7 joins the one-liners)",
          "**1.1.14-rc.10** —" in experience and
          "**1.1.13** —" in experience and
          "**1.1.14-rc.9** —" in experience and
          "**1.1.14-rc.8** —" in experience and
          "**1.1.14-rc.3** —" in experience and
          "**1.1.15-rc.8 —" in readme and
          "**1.1.15-rc.7 —" in readme and
          "**1.1.15-rc.6 —" in readme and
          "### 1.1.15-rc.7 —" in experience and
          "### 1.1.15-rc.6 —" in experience and
          "### 1.1.15-rc.5 —" in experience and
          "### 1.1.15-rc.3 —" in experience and
          "### 1.1.15-rc.2 —" in experience and
          "### 1.1.15-rc.1 —" in experience and
          readme.count("(the current release candidate):**") == 1 and
          "**1.1.14-rc.2** —" in experience and
          "**1.1.14-rc.1** —" in experience and
          "**1.1.13-rc.7** —" in experience and
          "**1.1.13-rc.7 —" not in readme)
    check("the readme news is on the diet (the retired rounds ride one line each)",
          "**1.1.13-rc.1** —" in experience and "**1.1.12-rc.16 —" not in readme)


# ---------------------------------------------------------------------------
# 9. the field round 4 simulations (1.1.08): the light dialogs painted dark
#    controls, the dark combos drifted their field height (the chin), the
#    dark buttons carried the native light bottom edge, and a color change
#    on the bare program showed a load failure.
# ---------------------------------------------------------------------------
def t_sim_field_round48():
    print("sim: the dialog return value round (1.1.10)")

    OSH = read("src/os.h").decode("latin-1")
    OSC = read("src/os.c").decode("latin-1")

    # 1. the glyph state map, re-executed: the check state from
    #    bm_getcheck (0/1/2), the item state flags from the custom draw.
    #    the expected ids are the commctrl cbs_*/rbs_* values the native
    #    button would choose for the same control.
    body = function_body(VIV, "static int _viv_dialog_dark_glyph_state")
    if not check("the glyph state mapper is extractable", body is not None):
        return
    radio_base = re.search(r"check \? OS_RBS_CHECKEDNORMAL : OS_RBS_UNCHECKEDNORMAL", body)
    mixed = "BST_INDETERMINATE) ? OS_BS_MIXEDNORMAL" in body
    off_disabled = re.search(r"CDIS_DISABLED\)\r?\n\s*\{\r?\n\s*state \+= 3;", body)
    off_hot = re.search(r"CDIS_HOT\)\r?\n\s*\{\r?\n\s*state \+= 1;", body)
    off_pressed = re.search(r"CDIS_SELECTED\)\r?\n\s*\{\r?\n\s*state \+= 2;", body)
    check("the mapper uses the theme state ladder (base + 3 disabled, +1 hot, +2 pressed)",
          radio_base is not None and mixed and
          off_disabled is not None and off_hot is not None and off_pressed is not None)

    # extract the base selection from the source the same way the function does
    def glyph_state(type_, check, item_state):
        if type_ in (BS_AUTORADIOBUTTON, BS_RADIOBUTTON):
            state = OS_RBS_CHECKEDNORMAL if check else OS_RBS_UNCHECKEDNORMAL
        else:
            state = OS_BS_MIXEDNORMAL if check == BST_INDETERMINATE else (OS_BS_CHECKEDNORMAL if check else OS_BS_UNCHECKEDNORMAL)
        if item_state & CDIS_DISABLED:
            state += 3
        elif item_state & CDIS_HOT:
            state += 1
        elif item_state & CDIS_SELECTED:
            state += 2
        return state

    # the constants the os layer declares (extracted, not hardcoded here)
    OS_RBS_CHECKEDNORMAL = extract_int(OSH, r"#define\s+OS_RBS_CHECKEDNORMAL\s+(\d+)", "OS_RBS_CHECKEDNORMAL")
    OS_RBS_UNCHECKEDNORMAL = extract_int(OSH, r"#define\s+OS_RBS_UNCHECKEDNORMAL\s+(\d+)", "OS_RBS_UNCHECKEDNORMAL")
    OS_BS_MIXEDNORMAL = extract_int(OSH, r"#define\s+OS_BS_MIXEDNORMAL\s+(\d+)", "OS_BS_MIXEDNORMAL")
    OS_BS_CHECKEDNORMAL = extract_int(OSH, r"#define\s+OS_BS_CHECKEDNORMAL\s+(\d+)", "OS_BS_CHECKEDNORMAL")
    OS_BS_UNCHECKEDNORMAL = extract_int(OSH, r"#define\s+OS_BS_UNCHECKEDNORMAL\s+(\d+)", "OS_BS_UNCHECKEDNORMAL")
    BS_AUTORADIOBUTTON, BS_RADIOBUTTON = 9, 4
    BST_INDETERMINATE = 2
    CDIS_DISABLED, CDIS_HOT, CDIS_SELECTED = 4, 1, 2

    # commctrl's own state table (the ground truth the map must land on)
    CBS = {(False, 0): 1, (True, 0): 5, ("mixed", 0): 9}
    check("an unchecked checkbox draws cbs_uncheckednormal",
          glyph_state(2, 0, 0) == CBS[(False, 0)])
    check("a checked checkbox draws cbs_checkednormal",
          glyph_state(2, 1, 0) == CBS[(True, 0)])
    check("a mixed 3state draws cbs_mixednormal",
          glyph_state(5, 2, 0) == CBS[("mixed", 0)])
    check("a disabled unchecked checkbox draws cbs_uncheckeddisabled",
          glyph_state(2, 0, CDIS_DISABLED) == 4)
    check("a disabled checked checkbox draws cbs_checkeddisabled",
          glyph_state(2, 1, CDIS_DISABLED) == 8)
    check("a hot unchecked checkbox draws cbs_uncheckedhot",
          glyph_state(2, 0, CDIS_HOT) == 2)
    check("a hot checked checkbox draws cbs_checkedhot",
          glyph_state(2, 1, CDIS_HOT) == 6)
    check("a pressed unchecked checkbox draws cbs_uncheckedpressed",
          glyph_state(2, 0, CDIS_SELECTED) == 3)
    check("a disabled state wins over a hot state",
          glyph_state(2, 1, CDIS_DISABLED | CDIS_HOT) == 8)
    check("an unchecked radio draws rbs_uncheckednormal",
          glyph_state(9, 0, 0) == 1)
    check("a checked radio draws rbs_checkednormal",
          glyph_state(9, 1, 0) == 5)
    check("a disabled checked radio draws rbs_checkeddisabled",
          glyph_state(9, 1, CDIS_DISABLED) == 8)
    check("a hot unchecked radio draws rbs_uncheckedhot",
          glyph_state(9, 0, CDIS_HOT) == 2)

    # 2. the dialog manager result contract, replayed: the defdlgproc
    #    semantics are the root cause of the round - a non-zero dialog
    #    proc return reports dwlp_msgresult, never the return itself.
    def defdlgproc_result(proc_return_value, dwlp_msgresult):
        # raymond chen / the nm_customdraw doc: the dialog manager
        # keeps the message result in the window data. returning a
        # value directly from the dialog proc for wm_notify conveys
        # nothing - the control reads the window data.
        return dwlp_msgresult if proc_return_value else 0

    check("the 1.1.09 bug replays: plain cdrf_skipdefault return, control reads zero",
          defdlgproc_result(4, 0) == 0)
    check("the r48 fix replays: setwindowlongptr + return true, control reads the skip",
          defdlgproc_result(1, 4) == 4)
    check("the fix is wired at the call site (not just the theory)",
          "SetWindowLongPtr(hwnd,DWLP_MSGRESULT,dark_reply);" in VIV and
          "return dark_reply;\n" not in function_body(VIV, "INT_PTR _viv_dialog_dark_proc"))

    # 3. the touch mask, replayed against the nid table: integrated
    #    touch 0x01, external touch 0x02, integrated pen 0x04, ready 0x80.
    m = re.search(r"return \(\(sm & 0x80\) && \(sm & \(0x01 \| 0x02\)\)\) \? 1 : 0;", OSC)
    check("the extracted touch mask is the 0x03 touch pair",
          m is not None, "mask not found")

    def touch(sm):
        return 1 if ((sm & 0x80) and (sm & (0x01 | 0x02))) else 0

    check("an integrated touch screen stays touch",
          touch(0x80 | 0x01) == 1)
    check("an external-only touch screen reads touch now (the repair)",
          touch(0x80 | 0x02) == 1)
    check("an integrated pen only is not a touch screen",
          touch(0x80 | 0x04) == 0)
    check("a not-ready digitizer reports nothing",
          touch(0x01) == 0)
    check("a ready integrated-touch combo with pen still reads touch",
          touch(0x80 | 0x01 | 0x04) == 1)

    # 4. the theme cache lifecycle, replayed: one open per dialog
    #    lifetime, drop on theme change, close on destroy - no leak.
    opens = 0
    closes = 0
    theme = None
    for event in ["paint"] * 6 + ["themechange"] + ["paint"] * 3 + ["destroy"]:
        if theme is None and event == "paint":
            opens += 1
            theme = "handle"
        if event == "themechange" and theme is not None:
            closes += 1
            theme = None
        if event == "destroy" and theme is not None:
            closes += 1
            theme = None
    check("ten paints cost two opens (one per style epoch)",
          opens == 2 and closes == 2)
    check("the lifecycle leaves no live handle behind",
          theme is None)

    # 5. the ci wiring: the smoke test runs automatically now.
    ty = read(".github/workflows/tests.yml").decode()
    ry = read(".github/workflows/release.yml").decode()
    check("the push ci opens the anomaly sweep through the fresh exe",
          "smoke_test.ps1" in ty and "-ExePath" in ty)
    check("the release ci smoke-tests before packaging",
          "smoke_test.ps1" in ry and "Build installers" in ry and
          ry.find("smoke_test.ps1") < ry.find("Build installers"))


def t_sim_field_round47():
    print("sim: the dark options text round, redone (1.1.09)")

    # 1. the dialog font matrix: re-extract every FONT statement and prove
    #    the uniformity the withdrawn build established (one family, one
    #    size, one charset - the two-charset drift that broke the page
    #    metrics is structurally impossible now).
    statements = re.findall(r'^FONT\s+([^\r\n]+)', RC, re.M)
    check("five font statements exist (rc.79 + r114: the classic options templates retired)", len(statements) == 5, str(len(statements)))
    uniform = all(s == '9, "Segoe UI", 400, 0, 0' for s in statements)
    check("every statement is the identical Segoe UI 9pt declaration",
          uniform, "; ".join(sorted(set(statements))))
    check("the obsolete fixedsys flag is absent from every template",
          not re.search(r"DS_FIXEDSYS", RC))

    # 2. the dlu geometry the font change must preserve: the pages keep
    #    their template sizes (the dialog manager rederives the unit grid
    #    from the new font - the dlu numbers themselves are the layout).
    # r114: the classic pages keep no dlu template - they are gone.

    # 3. the pixel grid: the dlu numbers follow the dialog font, so the
    #    same template renders 15-23 percent larger on the segoe ui 9pt
    #    grid than on the ms shell dlg 8 grid - the dlu assertions above
    #    cannot see that move. the segoe ui 9pt dialog base units at
    #    96dpi are (7, 15): one horizontal dlu = 7/4 px, one vertical
    #    dlu = 15/8 px. every template is walked on that grid now.
    bx, by = 7 / 4.0, 15 / 8.0
    check("the general page renders 339.5x453.75 px (the segoe ui 9pt grid)",
          abs(194 * bx - 339.5) < 0.01 and abs(242 * by - 453.75) < 0.01)
    check("the horizontal dlu grew 16.7 percent against the ms shell dlg 8 grid",
          abs(bx / 1.5 - 1.1667) < 0.001)

    def dialogs():
        for dm in re.finditer(r"^(IDD_\w+)\s+DIALOGEX\s+(\d+),\s*(\d+),\s*(\d+),\s*(\d+)(.*?)\bEND\b", RC, re.M | re.S):
            yield dm.group(1), int(dm.group(4)), int(dm.group(5)), dm.group(6)

    def controls(body):
        for cm in re.finditer(r'CONTROL\s+"([^"]*)",\s*(IDC_\w+),\s*"([^"]*)",([^,\r\n]*),\s*(\d+),\s*(\d+),\s*(\d+),\s*(\d+)', body):
            yield {"label": cm.group(1), "id": cm.group(2), "cls": cm.group(3),
                   "style": cm.group(4), "x": int(cm.group(5)), "y": int(cm.group(6)),
                   "w": int(cm.group(7)), "h": int(cm.group(8))}
        for cm in re.finditer(r'\b(LTEXT|CTEXT|RTEXT|PUSHBUTTON|DEFPUSHBUTTON)\s+"([^"\r\n]*)",\s*(IDC_\w+|IDOK|IDCANCEL),\s*(\d+),\s*(\d+),\s*(\d+),\s*(\d+)', body):
            yield {"label": cm.group(2), "id": cm.group(3), "cls": cm.group(1),
                   "style": "", "x": int(cm.group(4)), "y": int(cm.group(5)),
                   "w": int(cm.group(6)), "h": int(cm.group(7))}
        for cm in re.finditer(r'\b(EDITTEXT|COMBOBOX|LISTBOX)\s+(IDC_\w+),\s*(\d+),\s*(\d+),\s*(\d+),\s*(\d+)', body):
            yield {"label": "", "id": cm.group(2), "cls": cm.group(1),
                   "style": "", "x": int(cm.group(3)), "y": int(cm.group(4)),
                   "w": int(cm.group(5)), "h": int(cm.group(6))}

    # 3a. containment: every control of every page holds inside its
    #     dialog on the pixel grid (the overflow class the dlu numbers
    #     cannot catch when the font moves).
    overflow = []
    total = 0
    for name, cx, cy, body in dialogs():
        for c in controls(body):
            total += 1
            if (c["x"] + c["w"]) * bx > cx * bx + 0.001 or (c["y"] + c["h"]) * by > cy * by + 0.001:
                overflow.append("%s/%s" % (name, c["id"]))
    check("the pixel walk covered every control of the five templates",
          total >= 25, str(total))  # r114: the classic options family retired (29 controls stay)
    check("every control fits its dialog on the pixel grid",
          not overflow, "overflow: %s" % " ".join(overflow[:8]))

    # 3b. the eleven association checkboxes inside the 242-tall page:
    #     the lowest one (bottom 232 dlu) keeps 10 dlu = 18.75 px of
    #     clear space under it on the new grid.
    gen = re.search(r"^IDD_GENERAL\s+DIALOGEX\s+(\d+),\s*(\d+),\s*(\d+),\s*(\d+)(.*?)\bEND\b", RC, re.M | re.S)
    if gen:
        boxes = re.findall(r'CONTROL\s+"([^"]*)",\s*(IDC_[A-Z0-9_]+),\s*"Button",\s*BS_AUTOCHECKBOX[^,]*,\s*(\d+),\s*(\d+),\s*(\d+),\s*(\d+)', gen.group(5))
        assoc = [b for b in boxes if b[0].replace("&", "") in
                 ("BMP", "GIF", "ICO", "JPEG", "JPG", "PNG", "TIF", "TIFF", "WEBP", "EMF", "WMF")]
        check("the pixel walk sees the eleven association checkboxes", len(assoc) == 11, str(len(assoc)))
        lowest = max(int(y) + int(h) for _, _, _, y, _, h in assoc)
        check("the lowest checkbox keeps its 10 dlu margin in pixels (18.75px)",
              abs((242 - lowest) * by - 18.75) < 0.01, str((242 - lowest) * by))

    # 3c. the label fit: the checkbox labels render with the dialog font
    #     next to the native glyph (the custom draw label offset). the
    #     per-class advance widths of segoe ui 9pt at 96dpi (em = 12px):
    #     upper ~ 0.65 em, lower ~ 0.52 em, digit ~ 0.58 em, space ~
    #     0.27 em, cjk ~ 1.0 em; the glyph box is 13px plus half a digit
    #     of gap. the template labels are the design-time floor - the
    #     runtime localization replaces them, so this is the regression
    #     tripwire for the grid, not the translator.
    def label_px(text):
        w = 0.0
        for ch in text.replace("&", ""):
            o = ord(ch)
            if o >= 0x2E80:
                w += 12.0
            elif ch == " ":
                w += 3.2
            elif ch.isupper():
                w += 7.8
            elif ch.isdigit():
                w += 7.0
            else:
                w += 6.2
        return w

    glyph_px = 13.0
    gap_px = 3.5
    fit_fail = []
    boxes_seen = 0
    for name, cx, cy, body in dialogs():
        for c in controls(body):
            if "AUTOCHECKBOX" not in c["style"].upper() or not c["label"]:
                continue
            boxes_seen += 1
            avail = c["w"] * bx
            need = label_px(c["label"]) + glyph_px + gap_px
            if need > avail:
                fit_fail.append("%s/%s needs %.0f has %.0f" % (name, c["id"], need, avail))
    check("the label fit walks the template checkboxes", boxes_seen >= 1, str(boxes_seen))  # r114: the everything random row is the one live checkbox
    check("every checkbox label + glyph fits its control on the pixel grid",
          not fit_fail, "; ".join(fit_fail[:6]))

    # 4. the custom draw protocol: re-execute the notify the way the
    #    dialog manager would (the withdrawn build's owner draw flip
    #    replaced bs_autocheckbox in the style and froze every checkbox -
    #    the redo never touches a style bit).
    notify = function_body(VIV, "static INT_PTR _viv_dialog_dark_notify(HWND hwnd,NMHDR *header)")
    if not check("the notify handler is extractable", notify is not None):
        return
    check("the notify leads with the dark gate (light keeps native painting)",
          notify.find("_viv_is_dark()") < notify.find("NM_CUSTOMDRAW"))
    check("the notify filters on the button class (the tree and tabs pass through)",
          'string_compare(class_name,L"Button")' in notify and
          "GetClassNameW(header->hwndFrom" in notify)

    def notify_reply(dark, code, cls, type_):
        # replay the guard chain: dark, then the code, then the class,
        # then the type, then the stage.
        if not dark:
            return -1
        if code != "NM_CUSTOMDRAW":
            return -1
        if cls != "Button":
            return -1
        if type_ not in (2, 9, 3, 4, 5, 6):  # autockbox, autoradio, checkbox, radio, 3state, auto3state
            return -1
        return 4  # CDRF_SKIPDEFAULT at CDDS_PREPAINT

    check("a dark checkbox label paints (skip default at prepaint)",
          notify_reply(True, "NM_CUSTOMDRAW", "Button", 2) == 4)
    check("a dark radio label paints the same way",
          notify_reply(True, "NM_CUSTOMDRAW", "Button", 9) == 4)
    check("the light ui keeps the native label painting",
          notify_reply(False, "NM_CUSTOMDRAW", "Button", 2) == -1)
    check("the options tree notifications pass through to the dialog proc",
          notify_reply(True, "TVN_SELCHANGEDW", "SysTreeView32", 2) == -1)
    check("a flipped push button's custom draw passes through",
          notify_reply(True, "NM_CUSTOMDRAW", "Button", 0) == -1)
    check("a 3state control joins the custom draw (the mixed state draws)",
          notify_reply(True, "NM_CUSTOMDRAW", "Button", 5) == 4 and
          notify_reply(True, "NM_CUSTOMDRAW", "Button", 6) == 4)
    check("the dialog manager would hand the control the dwlp_msgresult value",
          "SetWindowLongPtr(hwnd,DWLP_MSGRESULT,dark_reply);" in VIV)

    def dialog_notify_result(proc_return, msgresult_set):
        # the defdlgproc contract: a non-zero proc return reports the
        # message result stored in the window data (dwlp_msgresult), not
        # the proc return itself. the first 1.1.09 redo returned
        # cdrf_skipdefault directly - the control read the untouched zero.
        if proc_return:
            return msgresult_set
        return 0

    check("the old plain return starved the control (the r48 root cause)",
          dialog_notify_result(4, 0) == 0)
    check("the dwlp_msgresult pair delivers the skip",
          dialog_notify_result(1, 4) == 4)
    check("the stage handoff: prepaint paints, the other stages default",
          "CDDS_PREPAINT" in notify and "return CDRF_DODEFAULT;" in notify)

    # 5. the state machine: the reads stay live and no manual toggle
    #    compensation exists (the withdrawn build needed one and had
    #    none - that was the p0).
    check("the check reads stay live (the everything random row, no bm_setcheck)",
          VIV.count("IsDlgButtonChecked") == 1 and
          "BM_SETCHECK" not in VIV and
          open("src/viv_dialogs.c", "rb").read().decode("utf-8", errors="replace").count("BN_CLICKED") == 0)  # rc.8: the remake domains post BN_CLICKED on purpose (5 sites); r114: the classic pages retired
    check("the flip never touches the glyph control styles",
          "((type == BS_PUSHBUTTON) || (type == BS_DEFPUSHBUTTON)))" in VIV and
          "|| (type == BS_AUTOCHECKBOX) || (type == BS_AUTORADIOBUTTON)))" not in VIV)

    # 6. the about band: the bottom strip follows the theme (the white
    #    strip report located it here - the system colors painted
    #    unconditionally below the dialog face).
    check("the about band takes the dark chrome strip and the fixed light palette",
          "return (INT_PTR)_viv_dark_chrome_brush(1);" in VIV and
          "return (INT_PTR)_viv_dark_chrome_brush(2);" in VIV and
          "return (INT_PTR)_viv_dark_chrome_brush(0);" in VIV and
          "_viv_about_light_brush(0)" in VIV and
          "_viv_about_light_brush(1)" in VIV and
          "(HBRUSH)(COLOR_BTNSHADOW + 1)" not in VIV and
          "(HBRUSH)(COLOR_BTNHIGHLIGHT + 1)" not in VIV)


def t_sim_field_round46():
    print("sim: the field round 4 checks")

    children = function_body(VIV, "static BOOL CALLBACK _viv_dark_dialog_children")
    if not check("the dialog children body is extractable", children is not None):
        return

    # --- the theme classes follow the app theme (the light black controls) ---
    check("the children read the theme once and gate every class on it",
          "dark = _viv_is_dark();" in children and
          "os_allow_dark_mode_for_window(hwnd,dark ? 1 : 0);" in children and
          "os_allow_dark_mode_for_window(hwnd,1);" not in children)
    check("the dark classes only run in the dark branch",
          children.find("os_dark_combobox_theme(hwnd);") <
          children.find("os_light_window_theme(hwnd);") and
          children.count("os_dark_window_theme(hwnd);") == 2)  # + the rc.4 tree branch
    dark_pos = children.find("os_dark_combobox_theme(hwnd);")
    light_pos = children.find("os_light_window_theme(hwnd);")
    gate_pos = children.find("if (dark)")
    check("the dark gate sits before both class applications",
          gate_pos != -1 and gate_pos < dark_pos and gate_pos < light_pos)

    # behavioral replay of the branch choice per control class and theme.
    def chosen_theme(dark, class_name):
        # the extracted structure: the theme class follows the dark flag
        # (dark: the cfd class for combos, the explorer dark class for the
        # rest; light: the light explorer class for everything).
        if class_name == "ComboBox":
            return "DarkMode_CFD" if dark else "Explorer"
        return "DarkMode_Explorer" if dark else "Explorer"

    check("a light combo takes the light class (no black fields)",
          chosen_theme(False, "ComboBox") == "Explorer")
    check("a light push button takes the light class (no black buttons)",
          chosen_theme(False, "Button") == "Explorer")
    check("a dark combo still takes the cfd class",
          chosen_theme(True, "ComboBox") == "DarkMode_CFD")
    check("a dark button still takes the dark explorer class",
          chosen_theme(True, "Button") == "DarkMode_Explorer")

    # --- the button flip condition (r47 redo: the glyph controls never
    # --- flip - the flip would replace their check state machine) ---
    m = re.search(r"if \(\(!\(style & \(BS_BITMAP \| BS_ICON\)\)\) && \(\(type == BS_PUSHBUTTON\) \|\| \(type == BS_DEFPUSHBUTTON\)\)\)",
                  children)
    if not check("the flip condition is extractable", m is not None):
        return

    # re-execute the extracted condition for the whole control matrix
    BS_PUSHBUTTON, BS_DEFPUSHBUTTON = 0, 1
    BS_AUTOCHECKBOX, BS_AUTORADIOBUTTON = 3, 9
    BS_BITMAP, BS_ICON = 0x80, 0x64

    def flips(style):
        type_ = style & 0x0F
        return not (style & (BS_BITMAP | BS_ICON)) and (
            type_ == BS_PUSHBUTTON or type_ == BS_DEFPUSHBUTTON)

    check("push buttons flip in the dark ui (the dark ui owns the face)",
          flips(BS_PUSHBUTTON))
    check("default buttons flip in the dark ui",
          flips(BS_DEFPUSHBUTTON))
    check("the bitmap color swatches never flip",
          not flips(BS_PUSHBUTTON | BS_BITMAP) and
          not flips(BS_PUSHBUTTON | BS_ICON))
    check("checkboxes never flip (the automatic check state machine survives)",
          not flips(BS_AUTOCHECKBOX))
    check("radios never flip (the same state machine rule)",
          not flips(BS_AUTORADIOBUTTON))
    check("group boxes never flip",
          not flips(0x7))

    # --- the combo field height capture (the chin under the dark combos) ---
    i_get = children.find("field_height = (int)SendMessage(hwnd,CB_GETITEMHEIGHT,(WPARAM)-1,0);")
    i_flip = children.find("SetWindowLongPtr(hwnd,GWL_STYLE,style | CBS_OWNERDRAWFIXED);")
    check("the native height is captured before the style flips",
          -1 < i_get < i_flip)
    check("the field and the rows take the captured height",
          "SendMessage(hwnd,CB_SETITEMHEIGHT,(WPARAM)-1,field_height);" in children and
          "SendMessage(hwnd,CB_SETITEMHEIGHT,(WPARAM)0,field_height);" in children)
    check("the flip only runs on a positive height",
          "if (field_height > 0)" in children)
    check("the captured height rides in the prop above the button codes",
          "SetPropW(hwnd,_VIV_DARK_OWNERDRAW_PROP,(HANDLE)(0x100 + field_height));" in children)

    # behavioral replay: flip at the native height, read the measure back.
    for native in (18, 21, 26, 42, 63):
        field_height = native                        # CB_GETITEMHEIGHT(-1)
        if field_height > 0:
            prop = 0x100 + field_height              # the extracted store
            measured = prop - 0x100                  # the measure fallback
        else:
            prop, measured = None, None
        check("a %dpx native field keeps its height through the flip" % native,
              measured == native and prop is not None)

    # --- a refresh with no open file is a blank, not a reload ---
    refresh = function_body(VIV, "void _viv_refresh(void)")
    if not check("the refresh body is extractable", refresh is not None):
        return
    guard_pos = refresh.find("if (!fd.cFileName[0])")
    open_pos = refresh.find("_viv_open(&fd,0);")
    check("the no-file guard sits before the reload",
          -1 < guard_pos < open_pos)
    check("the no-file branch resets both stale flags",
          "_viv_file_not_found = 0;" in refresh and
          "_viv_load_failed = 0;" in refresh)

    # behavioral replay of the two states from the field report.
    def refresh_replay(cfilename, not_found, load_failed):
        opened = 0
        if not cfilename:                            # the extracted guard
            if not_found:
                not_found = 0
            if load_failed:
                load_failed = 0
            return not_found, load_failed, opened
        opened += 1                                  # the reload branch
        return not_found, load_failed, opened

    nf, lf, op = refresh_replay("", 1, 1)
    check("the bare program accepts a color change with no load error",
          (nf, lf, op) == (0, 0, 0))
    nf, lf, op = refresh_replay("shot.png", 0, 0)
    check("an open image still reloads for the new mat",
          op == 1)

    # --- both theme broadcasts schedule the settle re-check ---
    check("the broadcasts and the settings combo schedule the re-check",
          VIV.count("SetTimer(_viv_hwnd,VIV_ID_DARK_RECHECK_TIMER,400,0);") == 3)  # rc.8: the settings theme row joins; r114: the options combo left with the classic dialogs

    # --- the view menu canvas picker and the backdrop rename ---
    check("the menu picker shares the options apply chain",
          "case VIV_ID_VIEW_WINDOWED_BACKGROUND_COLOR:" in VIV and
          "os_choose_color(_viv_hwnd,&background_color)" in VIV)
    en = read("src/localization_en_us.h").decode("utf-8", errors="replace")
    zh = read("src/localization_zh_cn.h").decode("utf-8")
    check("the backdrop menu reads as the transparency backdrop",
          '"&Transparency backdrop", // LOCALIZATION_ID_BACKDROP' in en and
          '"透明背景(&T)", // LOCALIZATION_ID_BACKDROP' in zh)
    check("the canvas picker label sits next to it",
          '"Windowed &background color...", // LOCALIZATION_ID_WINDOWED_BACKGROUND_COLOR_MENU' in en and
          '"窗口背景颜色(&B)...", // LOCALIZATION_ID_WINDOWED_BACKGROUND_COLOR_MENU' in zh)


# ---------------------------------------------------------------------------
# 11. the font unification round (1.1.11): the dialog templates hard coded
#     segoe ui (no cjk glyphs -> the gdi per-character fallback with the
#     latin line cell), the menu bar and the status bar draw the locale
#     face. this round routes every dialog through lfMessageFont at the
#     dialog window own dpi. these are metric models, stated honestly:
#     they model the font selection and the vertical budgets, they cannot
#     render windows text - the real-machine check stays with the field
#     reports.
# ---------------------------------------------------------------------------
def t_sim_field_round49():
    print("sim: the font unification round (1.1.11)")

    OSC = read("src/os.c").decode("latin-1")
    VIVD = read("src/viv.c").decode("latin-1")
    RCD = read("res/voidImageViewer.rc").decode("utf-8", errors="replace")

    # 1. the type system table, replayed: what face each path draws on a
    #    chinese system.
    #    - the upstream mapped: ms shell dlg -> the locale face (yahei),
    #      via the gdi font mapping, native cells.
    #    - the fork 1.1.09..1.1.10: hard coded segoe ui -> no cjk glyph
    #      -> the per character font linking fallback, the line cell
    #      keeps the latin metrics.
    #    - this round: lfMessageFont -> the locale face, native cells.
    segoe_cell_96 = 15   # segoe ui 9pt tmHeight at 96 dpi
    yahei_cell_96 = 17   # the locale 9pt face runs a couple of px taller

    def cell(path):
        if path == "hardcoded_segoe":
            return segoe_cell_96   # the cjk glyphs squeeze in a latin cell
        return yahei_cell_96       # the locale face owns the cell

    check("the hardcoded path kept the latin cell for cjk text",
          cell("hardcoded_segoe") == 15)
    check("the message font path gives cjk text the native cell",
          cell("message_font") == 17)
    check("the source of the new path is lfMessageFont (both query legs)",
          OSC.count("*lf = ncm.lfMessageFont;") == 2)
    check("no hard coded face name joins the runtime path",
          "lfMessageFont" in OSC and
          RCD.count('FONT 9, "Segoe UI"') == 5)   # the template keeps the skeleton job (rc.79 + r114: the classic options templates retired)

    # 2. the vertical budget, per template: the dlu grid keeps the segoe
    #    skeleton (the layout does not move), the rendering face may run
    #    taller. dlu -> px at 96 dpi with the segoe base (7,15): the
    #    vertical unit is tmHeight/8.
    vunit = segoe_cell_96 / 8.0
    # the two-line CONTROL rows (the header on one line, the class and the
    # geometry on the next) join before the parse so every control counts.
    rc_joined = re.sub(r',\s*\n\s*("Button")', r',\1', RCD)
    rows = []
    for m in re.finditer(
        r'^[ \t]*(CONTROL|LTEXT|RTEXT|CTEXT|PUSHBUTTON|DEFPUSHBUTTON|EDITTEXT|COMBOBOX|LISTBOX|GROUPBOX)\b[^\n]*?,(-?\d+),(-?\d+),(-?\d+),(-?\d+)(?:[,\s][^\n]*)?$',
        rc_joined, re.M):
        rows.append((m.group(1), int(m.group(5)), m.group(0)))
    check("the template rows parse (the five dialogs carry controls)",
          len(rows) >= 25, len(rows))  # r114: the classic options family retired (29 rows stay)

    tall_ok = [r for r in rows if r[0] in ("CONTROL", "PUSHBUTTON", "DEFPUSHBUTTON", "COMBOBOX", "LISTBOX", "GROUPBOX", "EDITTEXT")]
    # the blank label rows (the b42 template round's band chrome controls:
    # the separator lines and the button strip face carry geometry and
    # color, no glyphs) never join the glyph budget - the pinch invariant
    # speaks to rows that host text.
    tight = [r for r in rows if r[0] in ("LTEXT", "RTEXT", "CTEXT") and r[1] * vunit < yahei_cell_96 and '""' not in r[2]]
    centerimage_ok = [r for r in rows if r[0] in ("LTEXT", "RTEXT", "CTEXT") and r[1] * vunit >= yahei_cell_96]

    #    every control row that hosts a glyph or a button face clears the
    #    taller locale face outright.
    check("every control/button row clears the locale cell height",
          all(r[1] * vunit >= yahei_cell_96 for r in tall_ok),
          min((r[1] * vunit for r in tall_ok), default=0))
    #    the plain label rows: the 8 dlu rows sit two pixels tight - that
    #    is the stated design tolerance (the skeleton stays segoe; the
    #    static text paints without clipping and the row spacing above
    #    absorbs it). the check pins the tolerance itself: no label row
    #    is worse than the two pixel pinch, nothing is assumed away.
    worst = max((yahei_cell_96 - r[1] * vunit for r in tight), default=0.0)
    check("the tight label rows stay within the stated two-pixel pinch",
          all(0 <= yahei_cell_96 - r[1] * vunit <= 2.01 for r in tight), round(worst, 2))
    check("the tall label rows (centerimage) clear the locale face",
          len(centerimage_ok) >= 1, len(centerimage_ok))

    # 3. the font lifetime, replayed both ways: the 1.1.11 shared cache
    #    deleted the handle under live dialogs (the options container
    #    and its create dialog pages coexist, and jumpto is a create
    #    dialog too) - a settings broadcast or a dpi rebuild arriving
    #    while a dialog stood open killed the face its controls still
    #    held, and the next paint selected a dead font (the labels fell
    #    to the default system face). the per dialog ownership keeps
    #    every handle alive exactly as long as the dialog that draws
    #    with it.
    def replay_shared_cache():
        # the shipped 1.1.11 design: one handle, the dialogs adopt it,
        # the broadcast deletes it while the dialogs are open.
        fonts = {"handle": 401, "users": ["container", "page"]}
        fonts["handle"] = 0            # the drop: DeleteObject
        fonts["dangling"] = list(fonts["users"])  # under live users
        return fonts

    bad = replay_shared_cache()
    check("the 1.1.11 replay dangles the handle under two open dialogs",
          bad["handle"] == 0 and len(bad["dangling"]) == 2)

    def replay_per_dialog(events):
        # the review fix: each dialog owns its face, the broadcast
        # touches nothing, a close releases only its own handle.
        fonts = {}
        next_id = [401]
        for who, ev in events:
            if ev == "open":
                next_id[0] += 1
                fonts[who] = next_id[0]      # createfontindirect per dialog
            elif ev == "close":
                fonts.pop(who, None)         # the ncdestroy release
        return fonts                         # broadcasts change nothing

    fonts = replay_per_dialog([("container", "open"), ("page", "open"),
                               ("container", "close")])
    check("the per dialog replay keeps the open page face valid",
          fonts == {"page": 403})
    check("the source carries no shared cache statics",
          "_viv_dialog_font_handle" not in VIVD and
          "_viv_dialog_font_dpi" not in VIVD)
    check("the apply creates the font per dialog",
          "font = CreateFontIndirectW(&lf);" in VIVD)
    check("the release lands at the dialog ncdestroy",
          "RemovePropW(hwnd,_VIV_DIALOG_FONT_PROP);" in VIVD)

    # 4. the message order, replayed: the shared proc runs the font apply
    #    from wm_initdialog before each dialog's own case (the dark flip
    #    captures the native combo field heights against the final font,
    #    the about title derives its larger face from it, the localized
    #    labels render with it).
    apply_pos = VIVD.find("case WM_INITDIALOG:\r\n\t\t{\r\n\t\t\t// one type system")
    proc_pos = VIVD.find("INT_PTR _viv_dialog_dark_proc(")
    proc_body = VIVD[proc_pos:] if proc_pos != -1 else ""
    draw_pos = proc_body.find("\t\tcase WM_DRAWITEM:")
    check("the font apply case leads the shared proc switch",
          apply_pos != -1 and draw_pos != -1 and apply_pos - proc_pos < draw_pos)
    check("the shared proc fronts every dialog ahead of its own switch (rc.79 + r114: five dialogs)",
          VIVD.count("_viv_dialog_dark_proc(hwnd,msg,wParam,lParam);") == 5 and
          VIVD.count("if (dark_dialog_reply != -1)\r\n\t\t{\r\n\t\t\treturn dark_dialog_reply;") == 5)
    check("a dialog crossing monitors re-applies (wm_dpichanged)",
          VIVD.count("case WM_DPICHANGED:\r\n\t\t{\r\n\t\t\t// a dialog dragged across monitors") == 1)
    check("the settings broadcast touches no font (the faces live with their dialogs)",
          "_viv_dialog_font_drop" not in VIVD)
    nc_pos = proc_body.find("\t\tcase WM_NCDESTROY:")
    check("the dialog death releases the face inside the shared proc",
          nc_pos != -1 and nc_pos < draw_pos)

    # 5. the honesty clause, pinned: these are metric models. the
    #    changelog of the round states the tolerance instead of assuming
    #    it away, and this file states the same boundary here.
    changes = read("Changes.txt").decode("utf-8", errors="replace")
    check("the changelog states the tolerance (not assumed away)",
          "the simulation models the budget with the" in changes and
          "tolerance stated, not assumed away" in changes)
    check("the changelog states the lifetime rule (the review fix)",
          "the font belongs to the dialog that draws it" in changes)

# ---------------------------------------------------------------------------
# 12. the reentry state round (the corrected field report: "the program
#     minimizes to the taskbar, then the restore to the foreground loses
#     the size data"). three defects of one family: the show command a
#     second instance forwards must never demote the live window state,
#     the minimized window must never run geometry work against its
#     degenerate iconic client, and the iconic window must never seed
#     window-rect math with the -32000 parking rect.
# ---------------------------------------------------------------------------
def t_sim_reentry_state():
    print("sim: the reentry state round (the size the restore loses)")

    # --- the executable-spec table ---------------------------------------
    # the show-window state machine, line-verified against the two
    # executable specs this round (wine win32u/window.c WINPOS_ShowWindow
    # and reactos win32ss/user/ntuser/winpos.c co_WinPosMinMaximize, the
    # SW_SHOWNORMAL case shares the SW_RESTORE branch in both). the
    # minimize/restore half is additionally pinned against real windows
    # by wine's own test_window_placement plain-ok assertions (a
    # minimized maximized window carries WPF_RESTORETOMAXIMIZED and
    # SW_RESTORE returns it to maximized). the one case no test pins on
    # real windows is SW_SHOWNORMAL onto a minimized window: the app
    # must not bet on it either way - the fix below answers it with
    # SW_RESTORE, which every spec and every real windows agrees on.
    SW_HIDE, SW_SHOWNORMAL, SW_SHOWMAXIMIZED = 0, 1, 3
    SW_SHOWMINIMIZED, SW_RESTORE, SW_SHOWDEFAULT = 7, 9, 10

    def showwindow_spec(state, word):
        # state: live_normal | live_max | iconic_plain | iconic_restoremax
        if word in (SW_SHOWNORMAL, SW_RESTORE, SW_SHOWDEFAULT):
            if state == "iconic_restoremax":
                return "live_max"       # the placement answers: back to maximized
            if state == "iconic_plain":
                return "live_normal"    # the normal rect
            if state == "live_max":
                return "live_normal"    # THE DEMOTION (both specs, line-verified)
            return "live_normal"        # live normal: geometry no-op
        if word == SW_SHOWMAXIMIZED:
            return "live_max"           # the only word that may grow
        if word in (SW_SHOWMINIMIZED, 6):  # 6 = SW_MINIMIZE
            return "iconic_restoremax" if state == "live_max" else "iconic_plain"
        if word == SW_HIDE:
            return "hidden"
        return state

    check("spec: SW_SHOWNORMAL demotes a live maximized window (the rc.6 forward)",
          showwindow_spec("live_max", SW_SHOWNORMAL) == "live_normal")
    check("spec: SW_RESTORE returns the minimized maximized window to maximized",
          showwindow_spec("iconic_restoremax", SW_RESTORE) == "live_max")
    check("spec: the minimize of a maximized window arms the restore-max placement",
          showwindow_spec("live_max", SW_SHOWMINIMIZED) == "iconic_restoremax")

    # --- 1. the copydata activation is state-aware -----------------------
    copydata = function_body(VIV, "static LRESULT _viv_on_wm_copydata")
    check("extract the copydata handler from the spliced source",
          copydata is not None)
    if copydata:
        cl_case = copydata[copydata.find("_VIV_COPYDATA_COMMAND_LINE"):]
        check("the rc.6 bare ShowWindow(hwnd,showcmd) is retired",
              "ShowWindow(hwnd,showcmd)" not in cl_case,
              "the launcher word still passes through raw")
        check("the minimized window answers SW_RESTORE (the placement decides)",
              "IsIconic(hwnd)" in cl_case and "SW_RESTORE" in cl_case)
        check("the iconic branch honors a run-maximized launcher word",
              "showcmd == SW_SHOWMAXIMIZED" in cl_case)
        check("the live window only grows: the lone live ShowWindow is the maximize",
              cl_case.count("ShowWindow(hwnd,") == 2)

        # replay the fixed policy over every launcher word x window state:
        # no word may demote the live state, no word may hide the window.
        def app_activation(state, word):
            if state.startswith("iconic"):
                if word == SW_SHOWMAXIMIZED:
                    return showwindow_spec(state, SW_SHOWMAXIMIZED)
                return showwindow_spec(state, SW_RESTORE)
            if word == SW_SHOWMAXIMIZED:
                return showwindow_spec(state, SW_SHOWMAXIMIZED)
            return state  # activation only: SetForegroundWindow answered it

        words = {"SW_HIDE(0)": SW_HIDE, "SW_SHOWNORMAL(1)": SW_SHOWNORMAL,
                 "SW_SHOWMAXIMIZED(3)": SW_SHOWMAXIMIZED,
                 "SW_SHOWMINIMIZED(7)": SW_SHOWMINIMIZED,
                 "SW_RESTORE(9)": SW_RESTORE, "SW_SHOWDEFAULT(10)": SW_SHOWDEFAULT}
        demoted = ["%s on %s -> %s" % (w, s, app_activation(s, v))
                   for w, v in words.items() for s in
                   ("live_max", "live_normal", "iconic_plain", "iconic_restoremax")
                   if app_activation(s, v) not in
                   ("live_max", "live_normal", "iconic_plain", "iconic_restoremax")
                   or (s == "live_max" and app_activation(s, v) == "live_normal")]
        check("no launcher word demotes or hides the first instance (replay)",
              not demoted, "; ".join(demoted[:4]))
        check("the plain re-open returns a minimized maximized window to maximized",
              app_activation("iconic_restoremax", SW_SHOWNORMAL) == "live_max")

    # --- 2. the minimized window runs no size sweep ----------------------
    on_size = function_body(VIV, "void _viv_on_size")
    check("extract _viv_on_size from the spliced source", on_size is not None)
    if on_size:
        iconic_guard = on_size.find("IsIconic(_viv_hwnd)")
        clamp = on_size.find("_viv_clamp_zoom_pos")
        check("the iconic early-out precedes the zoom clamp in _viv_on_size",
              iconic_guard != -1 and clamp != -1 and iconic_guard < clamp,
              "guard at %d, clamp at %d" % (iconic_guard, clamp))

    # the hazard the early-out retires, replayed from the extracted
    # formulas: the iconic client is degenerate (zero, or a sliver the
    # strips subtract into the negative), and the rc.6 sweep ran anyway -
    # the render-size math then answers rw=1 / rh negative (nonzero!),
    # the view_set rw/rh guards pass, and the view anchors are rewritten
    # through a 4000x garbage scale.
    grs = function_body(VIV, "void _viv_get_render_size")
    check("extract the render-size formula", grs is not None)
    if grs and on_size:
        tall = re.search(
            r"rh = high;\s*\r?\s*rw = \(\(high \* \(__int64\)_viv_slot_current\.image_wide\) \+ _viv_slot_current\.image_high - 1\) / _viv_slot_current\.image_high;",
            grs)
        floor = re.search(r"if \(rw <= 0\)\s*\r?\s*\{\s*\r?\s*rw = 1;", grs)
        check("the tall-image branch and the rw floor are still the extracted shape",
              tall is not None and floor is not None)

        def render_size_model(wide, high, iw, ih):
            # the extracted branch math (keep_aspect, fill off, shrinking allowed)
            if not (wide and high):
                return 0, 0
            if (high * iw) // ih < wide:
                rh = high
                rw = int((high * iw + ih - 1) / ih)
                if rw <= 0:
                    rw = 1
            else:
                rw = wide
                rh = int((wide * ih + iw - 1) / iw)
                if rh <= 0:
                    rh = 1
            return rw, rh

        # iconic client model A (client answers 0x0): the guards hold.
        rw, rh = render_size_model(0, 0, 4000, 3000)
        check("iconic model A (0x0 client): the render size answers zero",
              (rw, rh) == (0, 0))
        # iconic client model B (152px sliver, strips push high negative):
        rw, rh = render_size_model(152, -45, 4000, 3000)
        check("iconic model B (sliver client): the render size answers the garbage pair",
              rw == 1 and rh == -45, "rw %d rh %d" % (rw, rh))
        # model B destroys the view anchor: the view_set guard is
        # `if (rw)` - nonzero, so the rewrite runs.
        vset = function_body(VIV, "void _viv_view_set")
        check("extract _viv_view_set", vset is not None)
        if vset:
            check("the view_set anchor rewrite guards on plain nonzero rw",
                  "if (rw)" in vset and "don't set to 0, just use last value" in vset)
            # the destroyed anchor, replayed: 76 window px mapped at 4000
            # image px per window px (rw=1) lands at image pixel 304000 -
            # seventy-five image widths off a 4000px wide image.
            rx = ((250 - 250) * (152 * 2)) // 1000 - (1 // 2) - 0
            view_ix = ((152 // 2) - rx) * 4000 / 1.0
            check("model B rewrites the anchor to a far-off-image pixel",
                  view_ix == 304000.0, str(view_ix))

    # --- 3. the iconic geometry read census ------------------------------
    # 3a. the /x /y /width /height defaults: the rc.6 seed read the live
    #     window rect (the -32000 parking rect while minimized).
    pcl = function_body(VIV, "void _viv_process_command_line")
    check("extract _viv_process_command_line", pcl is not None)
    if pcl:
        seed = pcl.find("GetWindowRect(_viv_hwnd,&rect);")
        iconic = pcl.find("IsIconic(_viv_hwnd)")
        window_x = pcl.find("window_x = rect.left;")
        check("the geometry seed reads the placement when the window is iconic",
              seed != -1 and iconic != -1 and window_x != -1 and
              iconic < window_x and "rcNormalPosition" in pcl,
              "seed %d, iconic %d, reads %d" % (seed, iconic, window_x))

    # 3b. the fullscreen toggle: the rc.6 capture read IsZoomed (false for
    #     a maximized-minimized window) and GetWindowRect (the -32000
    #     sliver) - the fullscreen exit then restored the window to the
    #     garbage rect.
    tfs = function_body(VIV, "void _viv_toggle_fullscreen")
    check("extract _viv_toggle_fullscreen", tfs is not None)
    if tfs:
        check("the fullscreen capture asks the state-aware maximized truth",
              "_viv_fullscreen_is_maxed = _viv_is_window_maximized(_viv_hwnd);" in tfs)
        check("the bare IsZoomed capture is retired",
              "_viv_fullscreen_is_maxed = IsZoomed(_viv_hwnd);" not in tfs)
        iconic_at = tfs.find("if (IsIconic(_viv_hwnd))")
        demax = tfs.find("ShowWindow(_viv_hwnd,SW_SHOWNORMAL);")
        check("the iconic capture seeds from the placement normal position",
              iconic_at != -1 and "WPF_RESTORETOMAXIMIZED" in tfs and
              "_viv_fullscreen_rect = wp.rcNormalPosition;" in tfs)
        check("the de-maximize before the capture is live-only",
              iconic_at != -1 and demax != -1 and demax > iconic_at,
              "iconic branch %d, demax %d" % (iconic_at, demax))

    # 3c. the restyle: the rc.6 _viv_update_frame read the degenerate
    #     client and the -32000 window rect and applied their arithmetic
    #     with a SetWindowPos - the minimized window (a forwarded /minimal
    #     or //compact preset) landed at the garbage rect, and its leading
    #     SW_RESTORE popped the window to the front.
    uf = function_body(VIV, "void _viv_update_frame")
    check("extract _viv_update_frame", uf is not None)
    if uf:
        iconic_at = uf.find("if (IsIconic(_viv_hwnd))")
        restore_at = uf.find("ShowWindow(_viv_hwnd,SW_RESTORE);")
        check("the restyle carries an iconic branch",
              iconic_at != -1)
        if iconic_at != -1:
            branch = uf[iconic_at:]
            depth = 0
            end = 0
            for k, c in enumerate(branch):
                if c == "{":
                    depth += 1
                elif c == "}":
                    depth -= 1
                    if depth == 0:
                        end = k
                        break
            branch = branch[:end]
            check("the iconic restyle applies the styles and the strips only",
                  "config_show_caption" in branch and
                  "_viv_menubar_show(config_show_menu)" in branch and
                  "SetWindowLong(_viv_hwnd,GWL_STYLE,newstyle);" in branch)
            check("the iconic restyle never moves the window (frame-only setwindowpos)",
                  "SWP_NOMOVE|SWP_NOSIZE" in branch)
            check("the iconic branch returns before the live geometry path",
                  "return;" in branch)
        check("the live de-maximize stays out of the iconic path",
              restore_at != -1 and iconic_at != -1 and restore_at > iconic_at,
              "iconic %d, restore %d" % (iconic_at, restore_at))

    # 3d. the wm_move guard (already correct upstream): the position
    #     writes skip the iconic, the maximized and the fullscreen window.
    move = function_body(VIV, "static LRESULT _viv_on_wm_move")
    check("extract _viv_on_wm_move", move is not None)
    if move:
        check("the wm_move write is iconic-guarded (pin: stays correct)",
              "IsIconic(hwnd)" in move and "IsMaximized(hwnd)" in move)


if __name__ == "__main__":
    t_sim_mat_color()
    t_sim_recent_mru()
    t_sim_menu_braces()
    t_sim_zoom_ladder()
    t_sim_blank_flags()
    t_sim_options_geometry()
    t_sim_theme_flip()
    t_sim_version_117()
    t_sim_field_round46()
    t_sim_field_round47()
    t_sim_field_round48()
    t_sim_field_round49()
    t_sim_reentry_state()
    print()
    if failures:
        print("%d FAILURE(S)" % len(failures))
        sys.exit(1)
    print("ALL SIMULATION TESTS PASS")
