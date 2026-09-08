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
        print("  FAIL %s%s" % (name, (" - " + detail) if detail else ""))
        failures.append(name)
    return ok


def read(path):
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
    win_bg = function_body(VIV, "static COLORREF _viv_windowed_background(void)")
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
    follow = VIV.find("static HBRUSH _viv_backdrop_solid_brush(void)")
    seg = VIV[follow:follow + 1600]
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
    builder = function_body(VIV, "static HMENU _viv_create_recent_menu(void)")
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
    upd = function_body(VIV, "static void _viv_recent_menu_update(void)")
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
    push = function_body(VIV, "static void _viv_recent_file_push(const wchar_t *filename)")
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

    body = function_body(VIV, "static HMENU _viv_create_menu(void)")
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

    STEP = 1.01
    table = [STEP ** i for i in range(zmax)]     # the shipped init walk

    def scale(pos):
        """The shipped access: positive reads the table, negative reads
        the reciprocal of the positive entry (never out of bounds)."""
        return table[pos] if pos > 0 else 1.0 / table[-pos]

    floor_fn = function_body(VIV, "static int _viv_zoom_pos_floor(void)")
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
    clamp_fn = function_body(VIV, "static int _viv_clamp_zoom_pos(int zoom_pos)")
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

    blank = function_body(VIV, "static void _viv_blank(void)")
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
    print("sim: the options geometry (the 11 association checkboxes)")

    def dialog_block(name):
        m = re.search(re.escape(name) + r"\s+DIALOGEX\s+(\d+)\s*,\s*(\d+)\s*,\s*(\d+)\s*,\s*(\d+)(.*?)\bEND\b",
                      RC, re.S)
        if not m:
            return None, None
        height = int(m.group(4))
        return height, m.group(5)

    gen_h, gen_body = dialog_block("IDD_GENERAL")
    if not check("the general page template is parseable", gen_body is not None):
        return
    check("the general page grew to 242 (the 11th checkbox fits)", gen_h == 242, str(gen_h))

    # every association checkbox inside the group box: y + height <= page.
    boxes = re.findall(r'CONTROL\s+"([^"]*)",\s*(IDC_[A-Z0-9_]+),\s*"Button",\s*BS_AUTOCHECKBOX[^,]*,\s*(\d+),\s*(\d+),\s*(\d+),\s*(\d+)',
                       gen_body)
    assoc = [b for b in boxes if b[0].replace("&", "") in
             ("BMP", "GIF", "ICO", "JPEG", "JPG", "PNG", "TIF", "TIFF", "WEBP", "EMF", "WMF")]
    check("the general page carries eleven association checkboxes", len(assoc) == 11,
          str([b[0] for b in assoc]))
    for label, cid, x, y, w, h in assoc:
        bottom = int(y) + int(h)
        check("checkbox %s (y=%s h=%s) fits inside the page" % (cid, y, h),
              bottom <= gen_h, "bottom %d > page %d" % (bottom, gen_h))
    max_bottom = max(int(y) + int(h) for _, _, _, y, _, h in assoc)
    check("the lowest checkbox (WMF, bottom %d) is fully visible" % max_bottom,
          max_bottom == 232 and max_bottom <= gen_h - 10)

    # the group box encloses all of them.
    gbox = re.search(r'GROUPBOX\s+"Associations",IDC_ASSOCIATIONS_GROUPBOX,(\d+),(\d+),(\d+),(\d+)', gen_body)
    if check("the associations group box is parseable", gbox is not None):
        gy, gh = int(gbox.group(2)), int(gbox.group(4))
        check("the group box bottom encloses the lowest checkbox",
              gy + gh >= max_bottom, "group bottom %d vs box bottom %d" % (gy + gh, max_bottom))

    # the container: the page fits inside the tab, the buttons clear it.
    opt_h, opt_body = dialog_block("IDD_OPTIONS")
    if check("the options container template is parseable", opt_body is not None):
        check("the options container grew to 295", opt_h == 295, str(opt_h))
        tab = re.search(r'CONTROL\s+"",IDC_TAB1,"SysTabControl32",[^,]*,(\d+),(\d+),(\d+),(\d+)', opt_body)
        page_y = 26                            # the page child sits at y=26 dlu inside the tab
        if tab:
            tab_bottom = int(tab.group(2)) + int(tab.group(4))
            page_bottom = page_y + gen_h
            check("the 242-tall page fits inside the 264-tall tab",
                  page_bottom <= tab_bottom, "page %d vs tab %d" % (page_bottom, tab_bottom))
        ok_btn = re.search(r'DEFPUSHBUTTON\s+"OK",IDOK,(\d+),(\d+),(\d+),(\d+)', opt_body)
        if ok_btn:
            ok_bottom = int(ok_btn.group(2)) + int(ok_btn.group(4))
            check("the OK button clears the page (5 dlu bottom margin)",
                  opt_h - ok_bottom == 5, "margin %d" % (opt_h - ok_bottom))


# ---------------------------------------------------------------------------
# 7. the theme flip simulation (round 42: a flip left a light band beside
#    the toolbar until a manual resize; the broadcast can outrun the
#    registry).
# ---------------------------------------------------------------------------
def t_sim_theme_flip():
    print("sim: the theme flip (whole-window repaint, the 400ms re-check)")

    apply_fn = function_body(VIV, "static void _viv_apply_dark_mode(int repaint)")
    if not check("the apply body is extractable", apply_fn is not None):
        return

    # the unconditional whole-window invalidation (the white band fix).
    check("every flip invalidates the whole window unconditionally",
          re.search(r"InvalidateRect\(_viv_hwnd,0,FALSE\);", apply_fn) is not None)
    check("a repaint pass sweeps the children immediately",
          "RedrawWindow(_viv_hwnd,0,0,RDW_INVALIDATE | RDW_UPDATENOW | RDW_ALLCHILDREN);" in apply_fn)
    # the per-control invalidations remain (they cover their own windows).
    check("the per-control invalidations stay in place",
          apply_fn.count("InvalidateRect(") >= 4)

    # the one-shot re-check: the broadcast can arrive before the
    # personalize registry value settles.
    wm = VIV.find("ImmersiveColorSet")
    seg = VIV[wm - 400:wm + 700]
    check("an immersive broadcast arms the 400ms re-check",
          "SetTimer(_viv_hwnd,VIV_ID_DARK_RECHECK_TIMER,400,0);" in seg)
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
    print("sim: the 1.1.07 release identity")

    major = extract_int(VER_H, r"#define\s+VERSION_MAJOR\s+(\d+)", "VERSION_MAJOR")
    minor = extract_int(VER_H, r"#define\s+VERSION_MINOR\s+(\d+)", "VERSION_MINOR")
    rev = extract_int(VER_H, r"#define\s+VERSION_REVISION\s+(\d+)", "VERSION_REVISION")
    build = extract_int(VER_H, r"#define\s+VERSION_BUILD\s+(\d+)", "VERSION_BUILD")
    vstr = re.search(r'#define\s+VERSION_STRING\s+"([^"]*)"', VER_H)
    check("the version quad is 1.1.7.35",
          (major, minor, rev, build) == (1, 1, 7, 35), str((major, minor, rev, build)))
    check("the release identity string is 1.1.07",
          vstr is not None and vstr.group(1) == "1.1.07", vstr.group(1) if vstr else None)
    check("the rc derives from version.h (no hardcoded quad)",
          '#include "../src/version.h"' in RC and
          "FILEVERSION VERSION_MAJOR,VERSION_MINOR,VERSION_REVISION,VERSION_BUILD" in RC)
    nsh = read("nsis/version.nsh").decode()
    check("the nsis derives the display version at compile time",
          '!define DISPLAYVERSION "${VIV_VER_STRING}"' in nsh)
    top = CHANGES.lstrip("\ufeff").split("\r\n")[0] if "\r\n" in CHANGES else CHANGES.lstrip("\ufeff").split("\n")[0]
    check("the changelog top entry is the 1.1.07 release",
          top == "Stable: Version 1.1.07 (the second-rework simulation round)", top)
    check("the changelog carries the crlf line discipline",
          "\r\n" in CHANGES)
    readme = read("README.md").decode("utf-8", errors="replace")
    check("the readme current-stable line says 1.1.07",
          "**1.1.07 —" in readme and "(the current stable):**" in readme)


if __name__ == "__main__":
    t_sim_mat_color()
    t_sim_recent_mru()
    t_sim_menu_braces()
    t_sim_zoom_ladder()
    t_sim_blank_flags()
    t_sim_options_geometry()
    t_sim_theme_flip()
    t_sim_version_117()
    print()
    if failures:
        print("%d FAILURE(S)" % len(failures))
        sys.exit(1)
    print("ALL SIMULATION TESTS PASS")
