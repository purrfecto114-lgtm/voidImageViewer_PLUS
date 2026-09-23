#!/usr/bin/env python3
"""Structure regression tests: menu table, pan&scan removal, localization
alignment, dark mode wiring, status-bar call safety, then one guard group
per development stage (beta foundation -> engineering rc rounds -> dark UI
completion -> the two full-codebase audit rounds), each frozen against
regression. The version guards pin src/version.h as the single source of
truth for the release identity.

Run:  python3 tests/menu_structure_test.py
Exit 0 = pass.
"""
import os
import re
import sys

failures = []


def check(name, ok, detail=""):
    if not ok:
        failures.append(f"{name}: {detail}")
        print(f"FAIL {name} {detail}")
    else:
        print(f"ok   {name} {detail}")


def read(p):
    # R70 splice: viv.c was split into domain modules in one round. Reading
    # "src/viv.c" returns viv.c followed by every src/viv_*.c in dictionary
    # order, so the 140+ shape guards below keep covering the moved code
    # without a single per-guard edit (the pure-move discipline keeps the
    # bodies byte-identical; only declarations were added or de-static'ed).
    if p == "src/viv.c":
        import glob
        parts = [open(p, "rb").read()]
        for extra in sorted(glob.glob("src/viv_*.c")):
            parts.append(b"\n" + open(extra, "rb").read())
        # the R70 state layer carries the moved macros and shared types;
        # the guards (#define / struct field pins) resolve against it too.
        if glob.glob("src/viv_state.h"):
            parts.append(b"\n" + open("src/viv_state.h", "rb").read())
        return b"".join(parts)
    return open(p, "rb").read()


def t_second_review_round40():
    """Guards for the second-review fix round: the confirmed findings from
    the re-verified first audit (the two withdrawn findings and the
    downgraded small-pool note are deliberately not 'fixed' - the mem tail
    magic sits fully inside the allocation and the two-step GetRegionData
    is the documented pattern)."""
    viv = read("src/viv.c").decode("utf-8", errors="replace")
    webp = read("src/webp.c").decode("utf-8", errors="replace")
    osc = read("src/os.c").decode("utf-8", errors="replace")
    osh = read("src/os.h").decode("utf-8", errors="replace")
    glyphs = read("src/glyphs.c").decode("utf-8", errors="replace")
    memc = read("src/mem.c").decode("utf-8", errors="replace")

    # gestures: GID_BEGIN/GID_END must reach DefWindowProc (msdn UB note)
    gi = viv.find("case 1: // GID_BEGIN")
    check("gid_begin/gid_end reach defwindowproc",
          gi != -1 and "case 2: // GID_END" in viv[gi:gi + 60] and
          "return 0;" in viv[gi:gi + 500])

    # tablet gestures: press-and-hold and flicks suppressed for the window
    check("press-and-hold and flicks are suppressed",
          "case 0x2C4: // WM_TABLET_QUERYSYSTEMGESTURESTATUS (winuser.h)" in viv and
          "return 0x00000001 | 0x00010000;" in viv)

    # registry: the arp key is written to the 64-bit view and uninstalled
    # from it explicitly (regdeletekeyw cannot reach the alternate view)
    check("arp entry writes the 64-bit view",
          "KEY_QUERY_VALUE|KEY_SET_VALUE|KEY_WOW64_64KEY" in viv)
    check("arp uninstall sweeps both views",
          viv.count("os_reg_delete_key_ex(HKEY") == 2 and
          viv.count("RegDeleteKeyW(HKEY_") == 2)
    check("os_reg_delete_key_ex resolves regdeletekeyexw lazily",
          "RegDeleteKeyExW" in osc and
          "GetModuleHandleA(\"advapi32.dll\")" in osc and
          "int os_reg_delete_key_ex(HKEY hkey,const wchar_t *name,REGSAM access);" in osh)

    # dib paste: pixel budget before the allocation, size_t copy length
    check("clipboard dib paste applies the pixel budget",
          "if (!_viv_pixel_budget_refused(safe_size_mul((SIZE_T)bih->biWidth,(SIZE_T)height)))" in viv and
          "os_copy_memory(bits,src,pixels_size);" in viv)

    # gestures/touch: the digitizer probe asks for a touch screen
    check("touch probe requires an actual touch screen (integrated or external)",
          "return ((sm & 0x80) && (sm & (0x01 | 0x02))) ? 1 : 0;" in osc and
          "NID_INTEGRATED_PEN = 0x04 - the pen does not belong in the" in osc)

    # webp: per-frame durations from the container scan, frame's own delay
    check("webp frames carry their own duration",
          "WebPDemuxGetFrame(demux,1,&iter)" in webp and
          "frame_delays[delay_index] = iter.duration ? (DWORD)iter.duration : 1;" in webp and
          "delay = frame_delays[frame_index];" in webp and
          "last_timestamp" not in webp)

    # glyphs: overlapping cache eviction uses memmove semantics
    check("glyph cache eviction moves memory",
          "os_move_memory(&_glyphs_cache[0],&_glyphs_cache[1]," in glyphs and
          "os_copy_memory(&_glyphs_cache[0]" not in glyphs)

    # mem: the null-free guard runs before the header dereference
    mi = memc.find("void mem_free_debug(const char *file,int line,void *p)")
    seg = memc[mi:mi + 700]
    check("mem_free_debug rejects null before heapsize",
          mi != -1 and
          0 <= seg.find("if (!p)") < seg.find("mem_usage -= HeapSize") and
          "debug_fatal(\"INVALID FREE from %s(%d): %p\",file,line,p);" in seg)

    # os_copy_memory / os_move_memory take size_t lengths
    check("os memory copies take size_t lengths",
          "void os_copy_memory(void *d,const void *s,SIZE_T size);" in osh and
          "void os_move_memory(void *d,const void *s,SIZE_T size);" in osh)

    # ini reader caps the file size before allocating
    ini = read("src/ini.c").decode("utf-8", errors="replace")
    check("ini reader caps the allocation size",
          "file_size.QuadPart <= 0x1000000" in ini)

    # config: the utf-8 conversion is checked before the buffer is used
    cfg = read("src/config.c").decode("utf-8", errors="replace")
    check("config utf-8 conversion is checked",
          "ret = WideCharToMultiByte(CP_UTF8,0,s,-1,(char *)buf,STRING_SIZE*3,0,0);" in cfg and
          "if ((ret <= 0) || (ret >= (int)sizeof(buf)))" in cfg)

    # localization: bad ids clamp in release builds too
    loc = read("src/localization.c").decode("utf-8", errors="replace")
    check("localization ids clamp to a fallback",
          "return _localization_string_array_en_us[0];" in loc and
          loc.count("localization_id >= LOCALIZATION_ID_COUNT") == 2)


# ---------------------------------------------------------------------------
# 1. pan&scan must stay gone: no command ids, no menu rows, no key bindings,
#    no handlers, and (beta.7) no leftover zoom state values.
# ---------------------------------------------------------------------------
def t_panscan_gone():
    viv = read("src/viv.c").decode("utf-8", errors="replace")
    vh = read("src/viv.h").decode()
    for needle in ("VIV_ID_VIEW_PANSCAN",
                   "_viv_dst_zoom_set", "_viv_dst_pos_set",
                   "_VIV_MENU_VIEW_PANSCAN",
                   "_viv_dst_zoom_values", "_VIV_DST_ZOOM_ONE", "_VIV_DST_ZOOM_MAX"):
        check(f"viv.c/viv.h free of {needle}",
              (needle not in viv) and (needle not in vh))
    for name in ("LOCALIZATION_ID_PAN_SCAN", "LOCALIZATION_ID_INCREASE_SIZE",
                 "LOCALIZATION_ID_DECREASE_SIZE", "LOCALIZATION_ID_INCREASE_WIDTH",
                 "LOCALIZATION_ID_DECREASE_WIDTH", "LOCALIZATION_ID_INCREASE_HEIGHT",
                 "LOCALIZATION_ID_DECREASE_HEIGHT", "LOCALIZATION_ID_MOVE_UP",
                 "LOCALIZATION_ID_MOVE_DOWN", "LOCALIZATION_ID_MOVE_LEFT",
                 "LOCALIZATION_ID_MOVE_RIGHT", "LOCALIZATION_ID_MOVE_UP_LEFT",
                 "LOCALIZATION_ID_MOVE_UP_RIGHT", "LOCALIZATION_ID_MOVE_DOWN_LEFT",
                 "LOCALIZATION_ID_MOVE_DOWN_RIGHT", "LOCALIZATION_ID_MOVE_CENTER",
                 "LOCALIZATION_ID_PANSCAN_RESET"):
        gone = all(name not in read(p).decode("utf-8", errors="replace")
                   for p in ("src/viv.c", "src/localization.h",
                             "src/localization_en_us.h", "src/localization_zh_cn.h"))
        check(f"{name} fully removed", gone)


# ---------------------------------------------------------------------------
# 2. the View menu layout: a Layout submenu owns the ui toggles; the top level
#    is short; zoom items stay in their own submenu.
# ---------------------------------------------------------------------------
def t_view_menu_shape():
    viv = read("src/viv.c").decode()
    m = re.search(r"(?:static\s+)?_viv_command_t _viv_commands\[\]\s*=\s*\{(.*?)\n\};",
                  viv, re.S)
    assert m, "menu table not found"
    table = m.group(1)
    rows = re.findall(r"\{(LOCALIZATION_ID_[A-Z0-9_]+),([^,]*),([^,]*),([^}]*)\}", table)

    view_rows = [r for r in rows
                 if r[2].strip() == "_VIV_MENU_VIEW" and "MF_SEPARATOR" not in r[1]]
    layout_rows = [r for r in rows if r[2].strip() == "_VIV_MENU_VIEW_LAYOUT"]

    check("Layout submenu exists and is populated", len(layout_rows) >= 7,
          f"{len(layout_rows)} rows")
    for want in ("LOCALIZATION_ID_CAPTION", "LOCALIZATION_ID_FRAME",
                 "LOCALIZATION_ID_MENU", "LOCALIZATION_ID_STATUS_BAR",
                 "LOCALIZATION_ID_CONTROLS", "LOCALIZATION_ID_ZOOM_CONTROLS",
                 "LOCALIZATION_ID_PRESET"):
        check(f"{want} lives in Layout",
              any(r[0] == want for r in layout_rows))
    check("View top level is decluttered (<= 13 rows; r96: the renderer submenu joins)",
          len(view_rows) <= 13, f"{len(view_rows)} rows")
    for want in ("LOCALIZATION_ID_FULLSCREEN", "LOCALIZATION_ID_SLIDESHOW"):
        check(f"{want} stays in View", any(r[0] == want for r in view_rows))
    check("Options left the View menu for the File menu (rc.7)",
          not any(r[0] == "LOCALIZATION_ID_OPTIONS" for r in view_rows))
    for want in ("LOCALIZATION_ID_CAPTION", "LOCALIZATION_ID_FRAME"):
        check(f"{want} is visible in Layout (rc.7 unhide)",
              any(r[0] == want and "MF_OWNERDRAW" not in r[1]
                  for r in layout_rows))
    zoom_rows = [r for r in rows if r[2].strip() == "_VIV_MENU_VIEW_ZOOM"]
    for want in ("LOCALIZATION_ID_ZOOM_IN", "LOCALIZATION_ID_ZOOM_OUT",
                 "LOCALIZATION_ID_RESET"):
        check(f"{want} lives in Zoom submenu", any(r[0] == want for r in zoom_rows))
    enum_ids = set(re.findall(r"(_VIV_MENU_[A-Z_]+),", viv))
    used_ids = set(re.findall(r",\s*(_VIV_MENU_[A-Z_]+)\s*[,}]", table))
    check("every menu id used in the table is declared in the enum",
          used_ids <= enum_ids, str(used_ids - enum_ids))


# ---------------------------------------------------------------------------
# 3. localization alignment: enum order must match en and zh arrays exactly.
# ---------------------------------------------------------------------------

def localization_count(path):
    """Count the LOCALIZATION_ID enum entries (the alignment test's
    census, exposed for the round guards)."""
    enum = read(path).decode()
    ids = re.findall(r"^\s*(LOCALIZATION_ID_[A-Z0-9_]+)\s*(?:=[^,]*)?,\s*$", enum, re.M)
    return len([i for i in ids if i not in ("LOCALIZATION_ID_INVALID", "LOCALIZATION_ID_COUNT")])


def t_localization_alignment():
    enum = read("src/localization.h").decode()
    ids = re.findall(r"^\s*(LOCALIZATION_ID_[A-Z0-9_]+)\s*(?:=[^,]*)?,\s*$", enum, re.M)
    ids = [i for i in ids if i not in ("LOCALIZATION_ID_INVALID", "LOCALIZATION_ID_COUNT")]

    def array_entries(path):
        entries = []
        for line in read(path).decode("utf-8", errors="replace").splitlines():
            s = line.strip()
            if s.startswith('"') and '",' in s:
                m = re.search(r"//\s*(LOCALIZATION_ID_[A-Z0-9_]+)", s)
                entries.append(m.group(1) if m else None)
        return entries

    en = array_entries("src/localization_en_us.h")
    zh = array_entries("src/localization_zh_cn.h")
    check("en entry count matches enum", len(en) == len(ids),
          f"en {len(en)} vs enum {len(ids)}")
    check("zh entry count matches enum", len(zh) == len(ids),
          f"zh {len(zh)} vs enum {len(ids)}")

    bad = [(i, a, ids[i]) for i, a in enumerate(en)
           if a is not None and i < len(ids) and a != ids[i]]
    check("en markers align positionally", not bad, str(bad[:3]))
    bad = [(i, a, ids[i]) for i, a in enumerate(zh)
           if a is not None and i < len(ids) and a != ids[i]]
    check("zh markers align positionally", not bad, str(bad[:3]))

    # LAYOUT must sit directly after VIEW in all three
    for name, arr in (("enum", ids), ("en", en), ("zh", zh)):
        i = arr.index("LOCALIZATION_ID_VIEW")
        check(f"{name} LAYOUT after VIEW",
              arr[i + 1] == "LOCALIZATION_ID_LAYOUT")
    # the last ids must line up everywhere: the dark mode ids followed by
    # the six backdrop ids.
    # r41: the modern ux round ids (the rc.79 editor round retired the zoom
    # dialog's two ids from this tail)
    # (mru submenu, wallpaper confirmation, adaptive size units).
    # rc.12: the fusion round retires thirteen orphaned ids the rc.6
    # retirement (and the count dropdowns' arrival) had already left
    # without consumers; the dark-mode label was one of them, so the
    # window's first id is the save-as bmp row now.
    tail = ("LOCALIZATION_ID_SAVE_AS_BMP",
            "LOCALIZATION_ID_DARK_MODE_AUTO",
            "LOCALIZATION_ID_DARK_MODE_LIGHT",
            "LOCALIZATION_ID_DARK_MODE_DARK",
            "LOCALIZATION_ID_BACKDROP",
            "LOCALIZATION_ID_BACKDROP_FOLLOW",
            "LOCALIZATION_ID_BACKDROP_BLACK",
            "LOCALIZATION_ID_BACKDROP_WHITE",
            "LOCALIZATION_ID_BACKDROP_CUSTOM",
            "LOCALIZATION_ID_BACKDROP_CHECKERBOARD",
            "LOCALIZATION_ID_RECENT_FILES",
            "LOCALIZATION_ID_RECENT_FILES_EMPTY",
            "LOCALIZATION_ID_RECENT_FILES_CLEAR",
            "LOCALIZATION_ID_SET_DESKTOP_WALLPAPER_CAPTION",
            "LOCALIZATION_ID_SET_DESKTOP_WALLPAPER_MESSAGE",
            # round-128: the rotate ask and the silent-ini boxes
            "LOCALIZATION_ID_ROTATE_IMAGE_CAPTION",
            "LOCALIZATION_ID_ROTATE_IMAGE_MESSAGE",
            "LOCALIZATION_ID_CONFIG_SAVE_FAILED_CAPTION",
            "LOCALIZATION_ID_CONFIG_SAVE_FAILED_MESSAGE",
            "LOCALIZATION_ID_STATUS_BAR_SIZE_BYTES_FORMAT",
            "LOCALIZATION_ID_STATUS_BAR_SIZE_KB_FORMAT",
            "LOCALIZATION_ID_STATUS_BAR_SIZE_MB_FORMAT",
            "LOCALIZATION_ID_STATUS_BAR_SIZE_GB_FORMAT",
            "LOCALIZATION_ID_WINDOWED_BACKGROUND_COLOR_MENU",
            # the gui remake round appends the settings window, toolbar label
            # and status format ids after the modern ux group.
            "LOCALIZATION_ID_SETTINGS",
            "LOCALIZATION_ID_SETTINGS_PAGE_GENERAL",
            "LOCALIZATION_ID_SETTINGS_PAGE_VIEW",
            "LOCALIZATION_ID_SETTINGS_PAGE_CONTROLS",
            "LOCALIZATION_ID_SETTINGS_SECTION_INTERFACE",
            "LOCALIZATION_ID_SETTINGS_LANGUAGE",
            "LOCALIZATION_ID_SETTINGS_LANGUAGE_FOLLOW_SYSTEM",
            "LOCALIZATION_ID_SETTINGS_THEME",
            "LOCALIZATION_ID_SETTINGS_SECTION_STARTUP",
            "LOCALIZATION_ID_SETTINGS_ALLOW_MULTIPLE",
            "LOCALIZATION_ID_SETTINGS_ALLOW_MULTIPLE_DESC",
            "LOCALIZATION_ID_SETTINGS_SECTION_ASSOCIATIONS",
            "LOCALIZATION_ID_SETTINGS_ASSOCIATIONS_DESC",
            "LOCALIZATION_ID_SETTINGS_SELECT_ALL",
            "LOCALIZATION_ID_SETTINGS_OK",
            "LOCALIZATION_ID_SETTINGS_CANCEL",
            "LOCALIZATION_ID_TOOLBAR_OPEN",
            "LOCALIZATION_ID_TOOLBAR_ROTATE",
            "LOCALIZATION_ID_TOOLBAR_IMAGE_INFO",
            "LOCALIZATION_ID_STATUS_BAR_POSITION_FORMAT",
            "LOCALIZATION_ID_STATUS_BAR_RGB_FORMAT",
            "LOCALIZATION_ID_MSGBOX_YES",
            "LOCALIZATION_ID_MSGBOX_NO",
            "LOCALIZATION_ID_ACCENT_COLOR",
            "LOCALIZATION_ID_RENDERER",
            "LOCALIZATION_ID_RENDERER_GDI",
            "LOCALIZATION_ID_RENDERER_OPENGL",
            "LOCALIZATION_ID_RENDERER_DIRECT3D",
            "LOCALIZATION_ID_TOOLBAR_CUSTOMIZE",
            "LOCALIZATION_ID_TOOLBAR_GROUP_OPEN",
            "LOCALIZATION_ID_TOOLBAR_GROUP_NAV",
            "LOCALIZATION_ID_TOOLBAR_GROUP_FIT",
            "LOCALIZATION_ID_TOOLBAR_GROUP_ZOOM",
            "LOCALIZATION_ID_TOOLBAR_GROUP_ROTATE",
            "LOCALIZATION_ID_TOOLBAR_GROUP_INFO",
            "LOCALIZATION_ID_TOOLBAR_SHOW_ALL",
            "LOCALIZATION_ID_INIT_FAILED",
            "LOCALIZATION_ID_TOOLBAR_ICON_ONLY",
            "LOCALIZATION_ID_SETTINGS_TOOLBAR_ICON_ONLY",
            "LOCALIZATION_ID_SETTINGS_PRELOAD_COUNT",
            "LOCALIZATION_ID_SETTINGS_CACHE_COUNT",
            "LOCALIZATION_ID_SETTINGS_RESUME_LAST",
            "LOCALIZATION_ID_SETTINGS_COUNT_OFF",
            "LOCALIZATION_ID_SETTINGS_COUNT_ONE",
            "LOCALIZATION_ID_SETTINGS_COUNT_MANY",
            # round-125: the settings remake tail - the four view page
            # section captions, the three interface switch labels and the
            # ctrl+wheel row label.
            "LOCALIZATION_ID_SETTINGS_SECTION_RENDERING",
            "LOCALIZATION_ID_SETTINGS_SECTION_WINDOW",
            "LOCALIZATION_ID_SETTINGS_SECTION_ANIMATION",
            "LOCALIZATION_ID_SETTINGS_SECTION_PERFORMANCE",
            "LOCALIZATION_ID_SETTINGS_FLOATING_BAR",
            "LOCALIZATION_ID_SETTINGS_AUTO_HIDE_BAR",
            "LOCALIZATION_ID_SETTINGS_PIXEL_INFO",
            "LOCALIZATION_ID_CTRL_WHEEL_ACTION_STATIC",
            # round-126: the apply button rides the settings tail.
            "LOCALIZATION_ID_SETTINGS_APPLY")
    check("enum ends with the dark+backdrop+ux+remake+closure ids (rc.79: the zoom ids retired; rc.6: the dimensions format retired; rc.7: the startup shortcut retired; r96: the renderer and toolbar ids ride the tail; r113: the init-failed id rides the tail; r114: the icon-only pair rides the tail)", tuple(ids[-78:]) == tail)  # round-125: the remake tail is eight ids longer; round-128: four more
    check("en ends with the dark+backdrop+ux+remake ids (rc.79: the zoom ids retired; rc.6: the dimensions format retired; rc.7: the startup shortcut retired; r113: the init-failed id rides the tail; r114: the icon-only pair rides the tail)", tuple(en[-78:]) == tail)  # round-125: the remake tail is eight ids longer; round-128: four more
    check("zh ends with the dark+backdrop+ux+remake ids (rc.79: the zoom ids retired; rc.6: the dimensions format retired; rc.7: the startup shortcut retired; r113: the init-failed id rides the tail; r114: the icon-only pair rides the tail)", tuple(zh[-78:]) == tail)  # round-125: the remake tail is eight ids longer; round-128: four more
    # every panscan id must be absent everywhere
    for name in ("LOCALIZATION_ID_PAN_SCAN", "LOCALIZATION_ID_PANSCAN_RESET",
                 "LOCALIZATION_ID_MOVE_CENTER", "LOCALIZATION_ID_INCREASE_SIZE"):
        check(f"{name} gone from arrays", name not in ids and name not in en and name not in zh)


# ---------------------------------------------------------------------------
# 4. the magnify paint guard: the whole-destination StretchBlt branch must
#    require the destination to be fully on screen.
# ---------------------------------------------------------------------------
def t_paint_guard():
    viv = read("src/viv.c").decode()
    needle = "if ((rw <= wide) && (rh <= high) && (rect_p->right - rect_p->left >= paint_wide) && (rect_p->bottom - rect_p->top >= paint_high))"
    check("paint whole-dest branch guarded by on-screen size",
          viv.count(needle) == 1)


# ---------------------------------------------------------------------------
# 5. version consistency across the three version files.
# ---------------------------------------------------------------------------
def t_version():
    vh = read("src/version.h").decode()
    rc = read("res/voidImageViewer.rc").decode("utf-8", errors="replace")
    nsh = read("nsis/version.nsh").decode()

    # src/version.h is the single source of truth: rc and nsh derive from it
    def num(name):
        m = re.search(r'#define\s+' + name + r'\s+(\d+)', vh)
        check("version.h defines " + name, m is not None)
        return m.group(1) if m else None
    major, minor, rev, build = (num("VERSION_MAJOR"), num("VERSION_MINOR"),
                                num("VERSION_REVISION"), num("VERSION_BUILD"))
    tm = re.search(r'#define\s+VERSION_TYPE\s+"([^"]*)"', vh)
    vtype = tm.group(1) if tm else None
    sm = re.search(r'#define\s+VERSION_STRING\s+"([^"]*)"', vh)
    vstr = sm.group(1) if sm else None
    check("version.h = 1.1.15-rc.16.95 (the parallel evaluation round)",
          (major, minor, rev, build) == ("1", "1", "15", "95") and vtype == "")
    check("VERSION_STRING is the release identity (the 1.1.15-rc.16 tag)",
          vstr == "1.1.15-rc.16")
    check("rc derives everything from version.h",
          '#include "../src/version.h"' in rc and
          "FILEVERSION VERSION_MAJOR,VERSION_MINOR,VERSION_REVISION,VERSION_BUILD" in rc and
          'VALUE "FileVersion", VERSION_STRING' in rc and
          'VALUE "ProductVersion", VERSION_STRING' in rc)
    check("rc has no hardcoded version left",
          "1,1,0," not in rc and "1,0,1," not in rc and '"1.1.0-rc.' not in rc)
    check("nsh derives from src/version.h at compile time",
          "!searchparse" in nsh and "..\\src\\version.h" in nsh and
          '!define VERSION "${VIV_VER_MAJOR}.${VIV_VER_MINOR}.${VIV_VER_REVISION}.${VIV_VER_BUILD}"' in nsh and
          '!define DISPLAYVERSION "${VIV_VER_STRING}"' in nsh)
    check("nsh parses VERSION_STRING for the release identity",
          '`#define VERSION_STRING "` VIV_VER_STRING' in nsh)
    check("nsh has no hardcoded version left",
          '"1.1.0.' not in nsh and '"-rc.' not in nsh and '"1.1.01"' not in nsh
          and '"1.0.01"' not in nsh)

    # r20: every user-visible version display must anchor to VERSION_STRING,
    # not to a separately formatted %d sequence (the about dialog showed
    # 1.0.2.25, the uninstall entry showed 1.0.2, while the tag said 1.0.02).
    viv = read("src/viv.c").decode()
    check("about dialog shows the release identity string",
          'string_printf(version_wbuf,"%s%s %s",VERSION_STRING,VERSION_TYPE,VERSION_TARGET_MACHINE);' in viv)
    check("uninstall DisplayVersion shows the release identity string",
          'string_printf(version_wbuf,"%s%s",VERSION_STRING,VERSION_TYPE);' in viv)
    check("debug banner shows the identity string with the build counter",
          'debug_printf("viv %s%s (build %d) %s\\n",VERSION_STRING,VERSION_TYPE,VERSION_BUILD,VERSION_TARGET_MACHINE);' in viv)
    check("no %d.%d.%d version formatting survives in viv.c",
          re.findall(r"%d\.%d\.%d", viv) == [])
    nsi = read("nsis/installer.nsi").decode()
    check("installer version keys carry the identity, not the machine suffix",
          'VIAddVersionKey "FileVersion" "${DISPLAYVERSION}"' in nsi and
          'VIAddVersionKey "ProductVersion" "${DISPLAYVERSION}"' in nsi and
          'VIAddVersionKey "FileVersion" "${DISPLAYVERSION}.${TARGETMACHINE}"' not in nsi and
          'VIAddVersionKey "ProductVersion" "${DISPLAYVERSION}.${TARGETMACHINE}"' not in nsi)


# ---------------------------------------------------------------------------
# 6. THE beta.6 bug class, guarded forever: the status bar zoom call must
#    pass exactly one int to the one-%d format. (beta.6 passed five varargs
#    starting with a double pan position; %d read the double's bits and the
#    status bar showed garbage like -755914244%.)
# ---------------------------------------------------------------------------
def t_pixel_budget():
    # 1.0.04: the ceiling is pointer-width dependent. the guards pin both
    # branches of the split plus the refusal diagnostics on both loaders.
    vh = read("src/viv.h").decode()
    wp = read("src/webp.c").decode("latin-1")
    viv = read("src/viv.c").decode("latin-1")

    check("viv.h splits the ceiling by pointer width",
          "#if defined(_WIN64)" in vh and
          vh.count("#define VIV_MAX_IMAGE_PIXELS") == 2 and
          "400000000" in vh and "100000000" in vh and
          vh.index("400000000") < vh.index("#else") < vh.index("100000000"))
    check("webp loader refuses through the budget helper with a diagnostic",
          "_pixel_budget_refused" in wp and
          wp.count("VIV_MAX_IMAGE_PIXELS") == 1 and
          wp.count("VIV_MAX_ANIMATION_PIXELS") == 1)
    check("gdi+ loader refuses through the budget helper with a diagnostic",
          "_viv_pixel_budget_refused" in viv and
          "pixel budget: refusing a %u mp canvas" in viv)


def t_status_vararg_safety():
    viv = read("src/viv.c").decode()
    # the zoom pane call: exactly one string_printf uses the zoom format,
    # inside _viv_status_update, passing exactly one int-returning call.
    calls = re.findall(
        r"string_printf\(\s*zoom_buf,\s*localization_get_string\(LOCALIZATION_ID_STATUS_BAR_POS_ZOOM_FORMAT\)\s*,([^;]*)\);",
        viv, re.S)
    check("exactly one zoom pane format call", len(calls) == 1, repr(calls))
    if calls:
        args = calls[0].strip()
        check("zoom pane call passes exactly one vararg",
              args == "_viv_zoom_percent()", repr(args))
    # _viv_zoom_percent must be declared int and round the double average
    m = re.search(r"(?:static\s+)?int _viv_zoom_percent\(void\)\s*\{(.*?)\n\}",
                  viv, re.S)
    assert m, "_viv_zoom_percent not found"
    body = m.group(1)
    check("zoom percent returns the rounded int average",
          "return (int)((((zoom_x + zoom_y) / 2.0) * 100.0) + 0.5);" in body)
    check("the old five vararg call is gone",
          re.search(r"_viv_status_update_temp_pos_zoom\(void\)\s*\{[^}]*string_printf", viv, re.S) is None)


# ---------------------------------------------------------------------------
# 7. dark mode wiring: the whole chain must be present and consistent.
# ---------------------------------------------------------------------------
def t_dark_mode_wiring():
    osc = read("src/os.c").decode()
    osh = read("src/os.h").decode()
    viv = read("src/viv.c").decode()
    cc = read("src/config.c").decode()
    ch = read("src/config.h").decode()
    zc = read("src/zoomui.c").decode()
    zh_ = read("src/zoomui.h").decode()
    rc = read("res/voidImageViewer.rc").decode("utf-8", errors="replace")
    rh = read("res/resource.h").decode()

    # os layer: ordinals + the four functions
    for needle in ('MAKEINTRESOURCEA(135)', 'MAKEINTRESOURCEA(136)',
                   'MAKEINTRESOURCEA(132)', 'MAKEINTRESOURCEA(133)',
                   '"DwmSetWindowAttribute"',
                   "int os_dark_system_dark(void)",
                   "void os_dark_set_app_mode(int mode)",
                   "void os_dark_titlebar(HWND hwnd,int dark)",
                   "void os_dark_refresh(void)",
                   "HCF_HIGHCONTRASTON",
                   "SystemParametersInfoW(SPI_GETHIGHCONTRAST"):
        check(f"os.c has {needle[:44]}", needle in osc)
    for needle in ("os_dark_system_dark", "os_dark_set_app_mode",
                   "os_dark_titlebar", "os_dark_refresh"):
        check(f"os.h exports {needle}", needle in osh)
    # DWMWA 20 with the 19 fallback (E_INVALIDAG retry)
    check("os.c retries attr 19 on E_INVALIDARG",
          "_os_DwmSetWindowAttribute(hwnd,19,&value,sizeof(value));" in osc)

    # config layer
    check("config.c defines config_dark_mode default 2",
          "BYTE config_dark_mode = 2;" in cc)
    check("config.c loads dark_mode string",
          'ini_get_string(ini,(const utf8_t *)"dark_mode")' in cc)
    check("config.c saves dark_mode string",
          '_config_write_string(h,"dark_mode"' in cc)
    check("config.h externs config_dark_mode",
          "extern BYTE config_dark_mode;" in ch)

    # viv.c integration
    check("viv.c handles WM_SETTINGCHANGE ImmersiveColorSet",
          'case WM_SETTINGCHANGE:' in viv and 'L"ImmersiveColorSet"' in viv)
    check("viv.c dark status bar custom draw",
          "case NM_CUSTOMDRAW:" in viv and "CDDS_ITEMPREPAINT" in viv
          and "CDRF_NOTIFYITEMDRAW" in viv)
    check("viv.c sets the app mode before window creation",
          "os_dark_set_app_mode(config_dark_mode);" in viv)
    check("viv.c applies the dark chrome after creation",
          "_viv_apply_dark_mode(0);" in viv)
    # r114: the options dialog dark-combo read retired with the classic
    # dialogs (the settings window owns the theme combo now).
    check("viv.c dark canvas default",
          "_viv_windowed_background()" in viv
          and "return RGB(0x20,0x20,0x20);" in viv)

    # zoomui palette
    check("zoomui.c has zoomui_set_dark + dark palette",
          "void zoomui_set_dark(int dark)" in zc and "_zoomui_dark" in zc)
    check("zoomui.h declares zoomui_set_dark",
          "void zoomui_set_dark(int dark);" in zh_)
    check("viv.c pushes dark to the zoom controls",
          "zoomui_set_dark(dark);" in viv)

    # resources
    # r114: the dark combo rows, the idd_general template and the darkmode
    # resource ids retired with the classic options dialogs.


# ---------------------------------------------------------------------------
# 8. the zoom ladder code shape: scale table + live pos_max + clamps.
# ---------------------------------------------------------------------------
def t_ladder_shape():
    viv = read("src/viv.c").decode()
    check("zoom max constant is 1024",
          "#define _VIV_ZOOM_MAX 1024" in viv)
    check("scale table replaces presets",
          "_viv_zoom_scales[_VIV_ZOOM_MAX]" in viv
          and "_viv_zoom_presets" not in viv)
    check("pos_max walks the ladder",
          re.search(r"(?:static\s+)?int _viv_zoom_pos_max\(void\)", viv) is not None
          and "_viv_zoom_scales[pos]" in viv)
    check("wheel clamp uses _viv_clamp_zoom_pos",
          "_viv_zoom_pos = _viv_clamp_zoom_pos(_viv_zoom_pos);" in viv)
    check("clamp_zoom_pos measures the live top",
          "pos_max = _viv_zoom_pos_max();" in viv)
    check("1:1 exit search starts at the live top",
          "hi = _viv_zoom_pos_max() + 1; // exclusive upper bound" in viv)
    # r114: the fullscreen toggle joins the binary family - two more
    # boundaries (the fill offset and the fullscreen-fill offset) ride the
    # same monotonic ladder, and the 1024-entry precompute arrays are gone.
    check("1:1 exit and percent searches are binary (O(log n) measurements)",
          viv.count("mid = lo + ((hi - lo) / 2);") == 5
          and "for(_viv_zoom_pos = 0;_viv_zoom_pos<_VIV_ZOOM_MAX;_viv_zoom_pos++)" not in viv
          and "for(_viv_zoom_pos=0;_viv_zoom_pos<_VIV_ZOOM_MAX;_viv_zoom_pos++)" not in viv
          and "zoom_wide_array" not in viv and "zoom_high_array" not in viv)
    check("ladder top cache signature present",
          "_viv_zoom_pos_max_cache >= 0" in viv
          and "_viv_zoom_pos_max_cache_view_wide == wide" in viv)
    check("background brush cached across paints",
          "HBRUSH _viv_background_hbrush = 0;" in viv
          and "CreateSolidBrush(brush_color)" in viv)
    check("render capped at 16x native in double space (rc.7)",
          "max_w = 16.0 * (double)_viv_slot_current.image_wide;" in viv)
    check("int overflow guard for extreme panoramas",
          "_viv_clamp_double" in viv)


# ---------------------------------------------------------------------------
# 9. the beta.9 dark mode detection hardening: registry source, cache,
#    broadened broadcast handling, uipi filter and dark tooltips.
# ---------------------------------------------------------------------------
def t_dark_detection_wiring():
    osc = read("src/os.c").decode()
    osh = read("src/os.h").decode()
    viv = read("src/viv.c").decode()
    zc = read("src/zoomui.c").decode()

    # registry primary source + ordinal fallback
    check("os.c reads AppsUseLightTheme from the registry",
          'L"AppsUseLightTheme"' in osc)
    check("os.c opens the Personalize key",
          'L"Software\\\\Microsoft\\\\Windows\\\\CurrentVersion\\\\Themes\\\\Personalize"' in osc)
    check("os.c keeps the uxtheme probe only as the fallback",
          osc.find("_os_ShouldAppsUseDarkMode()") > osc.find("AppsUseLightTheme") >= 0)
    check("os.c validates the registry type",
          "type == REG_DWORD" in osc)

    # cache + invalidation
    check("os.c caches the dark state",
          "_os_dark_cache_valid" in osc and "_os_dark_cache_dark" in osc)
    check("os.h exports os_dark_invalidate",
          "void os_dark_invalidate(void);" in osh)
    check("os.c implements os_dark_invalidate",
          "void os_dark_invalidate(void)" in osc)

    # broadened broadcast handling
    check("viv.c handles WM_THEMECHANGED",
          "case WM_THEMECHANGED:" in viv)
    check("viv.c invalidates the dark cache on setting changes",
          viv.count("os_dark_invalidate();") >= 2)
    check("viv.c gates the re-apply on the dark state flip",
          "if (was_dark != is_dark)" in viv)
    check("viv.c still flushes the immersive color policy",
          'string_compare((const wchar_t *)lParam,L"ImmersiveColorSet") == 0' in viv)

    # uipi filter for elevated runs
    check("viv.c allows the theme broadcasts through uipi",
          "os_ChangeWindowMessageFilterEx(_viv_hwnd,WM_SETTINGCHANGE,1,0);" in viv
          and "os_ChangeWindowMessageFilterEx(_viv_hwnd,WM_THEMECHANGED,1,0);" in viv)

    # dark tooltips
    check("the comctl toolbar tooltip tint retired with the comctl toolbar",
          "TB_GETTOOLTIPS" not in open("src/viv_chrome.c", "rb").read().decode("utf-8", errors="replace") and
          "_viv_toolbar_set_dark(dark);" in viv)
    check("zoomui.c tints its tooltip",
          "_zoomui_apply_tooltip_colors" in zc)
    check("zoomui.c re-tints on every palette call",
          zc.count("_zoomui_apply_tooltip_colors();") >= 2)

    # message fallback defines for older SDKs
    check("viv.c defines the tooltip message fallbacks",
          "#define TTM_SETTIPBKCOLOR (WM_USER+19)" in viv
          and "#define WM_THEMECHANGED 0x031A" in viv)

    # the toolbar recreate on language switch re-applies the dark chrome
    check("language switch re-tints the recreated toolbar tooltip",
          viv.find("_viv_apply_dark_mode(0);",
                   viv.find("_viv_controls_show(config_show_controls);")) != -1)

# ---------------------------------------------------------------------------
# 10. the beta.10 dark dialogs: shared dispatcher wiring, options navigation,
#     about paint and the light texture skip.
# ---------------------------------------------------------------------------
def t_dark_dialogs_wiring():
    viv = read("src/viv.c").decode()
    osh = read("src/os.h").decode()
    osc = read("src/os.c").decode()

    # os support
    check("os.h exports os_dark_window_theme",
          "extern int os_dark_window_theme(HWND hwnd);" in osh)
    check("os.c loads SetWindowTheme by name",
          'GetProcAddress(_os_UxTheme_hmodule,"SetWindowTheme")' in osc)
    check("os.c implements os_dark_window_theme",
          "int os_dark_window_theme(HWND hwnd)" in osc)
    check("os.c applies the DarkMode_Explorer style",
          'L"DarkMode_Explorer"' in osc)

    # shared helpers + dispatcher in all 9 dialog procs
    check("viv.c has the dark dialog helpers",
          "_viv_dialog_dark_ctlcolor" in viv and
          "_viv_dialog_dark_erase" in viv and
          "_viv_dialog_dark_brush" in viv)
    # r114: the classic options family left the tree - the dispatcher
    # fronts the five live dialogs (custom rate, about, rename, jumpto,
    # everything) and the dark-chrome init count dropped with it.
    check("all 5 dialog procs route through the dispatcher (rc.79: the zoom dialog retired; r114: the options family retired)",
          viv.count("_viv_dialog_dark_proc(hwnd,msg,wParam,lParam);") == 5)
    check("all 5 dialogs get the dark chrome at init (rc.79: the zoom dialog retired; r114: the options family retired)",
          viv.count("_viv_dark_dialog(hwnd);") == 6)
    # rc.1 regression guard: the dispatcher must NOT sit inside switch(msg)
    # before the first case label - that placement is unreachable dead code
    # (the beta.10 bug: gcc warned "statement will never be executed").
    dead = viv.count("switch(msg)\r\n\t{\r\n\t\t{\r\n\t\t\tINT_PTR dark_dialog_reply;")
    check("no dispatcher dead placement inside switch(msg)", dead == 0, str(dead))
    live = viv.count("{\r\n\t\tINT_PTR dark_dialog_reply;")
    check("dispatcher runs before the switch in every proc (rc.79; r114: 5 procs)", live == 5, str(live))
    check("the dispatcher handles the color and erase messages",
          "case WM_CTLCOLORSTATIC:" in viv and
          "case WM_CTLCOLOREDIT:" in viv and
          "case WM_CTLCOLORLISTBOX:" in viv and
          "_viv_dialog_dark_erase(hwnd,(HDC)wParam)" in viv)

    # r114: the options tree / tab dark-family pins retired with the
    # classic dialogs.

    # about colors (the b42 template round: the wm_paint passes are gone,
    # the same palettes travel the control color replies)
    check("about answers the dark palette through the control color replies",
          "return (INT_PTR)_viv_dialog_dark_brush();" in viv)

    # brush lifetime
    check("the dialog brush is deleted at kill",
          "DeleteObject(_viv_dialog_dark_hbrush);" in viv)

    # TVM fallback defines for older SDKs
    check("viv.c defines the TVM color message fallbacks",
          "#define TVM_SETBKCOLOR (TV_FIRST+29)" in viv and
          "#define TVM_SETTEXTCOLOR (TV_FIRST+30)" in viv)


# ---------------------------------------------------------------------------
# 11. the beta.11 image backdrop + installer language dialog.
# ---------------------------------------------------------------------------
def t_backdrop_wiring():
    viv = read("src/viv.c").decode()
    vh = read("src/viv.h").decode()
    ch = read("src/config.h").decode()
    cc = read("src/config.c").decode()
    nsi = read("nsis/installer.nsi").decode()

    # config
    check("config.h defines the backdrop modes",
          "CONFIG_BACKDROP_MODE_FOLLOW" in ch and
          "CONFIG_BACKDROP_MODE_CHECKERBOARD" in ch)
    check("config.c persists the backdrop",
          '"backdrop_mode"' in cc and '"backdrop_color_r"' in cc)
    check("config.c default follows the window background",
          "CONFIG_BACKDROP_MODE_FOLLOW; // backdrop" in cc)

    # menu + commands
    check("viv.c has the backdrop menu",
          "_VIV_MENU_VIEW_BACKDROP" in viv)
    check("five backdrop radio entries exist",
          viv.count("MFT_RADIOCHECK,_VIV_MENU_VIEW_BACKDROP,") == 5)
    check("five backdrop check radios exist",
          viv.count("CheckMenuItem(hmenu,VIV_ID_VIEW_BACKDROP_") == 5)
    check("custom color uses the existing chooser",
          "os_choose_color(_viv_hwnd,&backdrop_color)" in viv)
    check("backdrop changes reload the image",
          "_viv_backdrop_apply();" in viv and "_viv_refresh();" in viv)

    # the paint hook: single cached-brush fill, no per-frame allocation
    check("the alpha fill hook calls the backdrop",
          "_viv_fill_backdrop(mem_hdc,load_wide,load_high);" in viv)
    check("the old per-frame solid brush chain is gone",
          "CreateSolidBrush(RGB(config_windowed_background_color_r,config_windowed_background_color_g,config_windowed_background_color_b))" not in viv)
    check("the checkerboard is a pattern brush",
          "CreatePatternBrush(_viv_backdrop_checker_hbitmap)" in viv)
    check("the solid brush is cached",
          "_viv_backdrop_solid_color != color" in viv)
    check("the brushes are released at kill",
          "DeleteObject(_viv_backdrop_checker_hbrush);" in viv and
          "DeleteObject(_viv_backdrop_checker_hbitmap);" in viv)

    # installer: the language dialog always shows
    check("installer defines MUI_LANGDLL_ALWAYSSHOW",
          "!define MUI_LANGDLL_ALWAYSSHOW" in nsi)


# ---------------------------------------------------------------------------
# 12. the beta.12 zoom stall fix + progressive display.
# ---------------------------------------------------------------------------
def t_progressive_wiring():
    viv = read("src/viv.c").decode()
    osh = read("src/os.h").decode()
    osc = read("src/os.c").decode()

    # mipmap boundary fix
    check("the full image is only used when magnified",
          "if ((render_wide >= image_wide) || (render_high >= image_high))" in viv)
    check("the top-level half-size boundary is gone",
          "if ((render_wide >= image_wide) || (render_high >= image_high))" in viv and
          viv.count("if ((render_wide >= mip_wide) || (render_high >= mip_high))") == 1)

    # progressive preview plumbing
    check("os.h exports the thumbnail function",
          "os_GdipGetImageThumbnail" in osh and
          "os_GdipGetImageThumbnailImage" not in osh)
    check("os.c loads the real gdiplus export (no Image suffix)",
          'GetProcAddress(_os_gdiplus_hmodule,"GdipGetImageThumbnail")' in osc)
    check("the thumbnail load is non fatal (plain GetProcAddress)",
          '_os_get_proc_address(_os_gdiplus_hmodule,"GdipGetImageThumbnail")' not in osc)
    check("the call passes the out image as the 4th argument",
          "os_GdipGetImageThumbnail(image,160,120,&thumb_image,NULL,NULL)" in viv)
    check("the reply struct carries is_low_res",
          "BYTE is_low_res; // 1 = progressive preview frame" in viv)
    check("the thread posts a low res first frame",
          "low_res_first_frame.is_low_res = 1;" in viv)
    check("only images with an embedded thumbnail take the path",
          "os_GdipGetPropertyItemSize(image,0x501A,&thumb_data_size)" in viv)
    check("big images only (2MP threshold)",
          "2000000)" in viv)
    check("the main thread protects the last image slot",
          "if ((!(first_frame->is_low_res)) && (!(_viv_image_is_low_res)))" in viv)
    check("the low res flag is tracked globally",
          "_viv_image_is_low_res = first_frame->is_low_res ? 1 : 0;" in viv)
    check("the webp first frame is marked full res",
          "first_frame.is_low_res = 0;" in viv)


# ---------------------------------------------------------------------------
# beta.13: the thumbnail export name must never regress (GdipGetImageThumbnail
# exists in real gdiplus.dll, GdipGetImageThumbnailImage exists nowhere).
# ---------------------------------------------------------------------------
def t_thumbnail_api():
    viv = read("src/viv.c").decode()
    osh = read("src/os.h").decode()
    osc = read("src/os.c").decode()

    for path, txt in (("src/viv.c", viv), ("src/os.h", osh), ("src/os.c", osc)):
        check(f"{path} never mentions the bogus export name",
              "GdipGetImageThumbnailImage" not in txt)
    check("os.c loads by the real export name",
          '"GdipGetImageThumbnail"' in osc)
    check("the load is optional (no fatal helper)",
          "_os_get_proc_address(_os_gdiplus_hmodule" not in osc or
          "_os_get_proc_address(_os_gdiplus_hmodule,\"GdipGetImageThumbnail\")" not in osc)
    check("os.h documents the real parameter order",
          "void **thumb_image,void *callback,void *callback_data" in osh)
    check("the guard uses the renamed pointer",
          "(os_GdipGetImageThumbnail)" in viv)


# ---------------------------------------------------------------------------
# beta.13: the image context menu is regrouped.
# ---------------------------------------------------------------------------
def t_context_menu_shape():
    viv = read("src/viv.c").decode()
    m = re.search(r"WORD _viv_context_menu_items\[\] = \r?\n\{(.*?)\r?\n\};",
                  viv, re.S)
    assert m, "context menu array not found"
    body = m.group(1)

    items = re.findall(r"(_VIV_MENU_[A-Z0-9_]+|VIV_ID_[A-Z0-9_]+)", body)
    check("zoom submenu opens and closes (marker twice)",
          items.count("_VIV_MENU_VIEW_ZOOM") == 2)
    check("rate submenu opens and closes (marker twice)",
          items.count("_VIV_MENU_SLIDESHOW_RATE") == 2)
    check("sort submenu opens and closes (marker twice)",
          items.count("_VIV_MENU_NAVIGATE_SORT") == 2)

    # the slim rate ladder: 1s/3s/5s/10s/30s/60s + custom only
    rates = [i for i in items if i.startswith("VIV_ID_SLIDESHOW_RATE_")]
    wanted = {"VIV_ID_SLIDESHOW_RATE_DEC", "VIV_ID_SLIDESHOW_RATE_INC",
              "VIV_ID_SLIDESHOW_RATE_1000", "VIV_ID_SLIDESHOW_RATE_3000",
              "VIV_ID_SLIDESHOW_RATE_5000", "VIV_ID_SLIDESHOW_RATE_10000",
              "VIV_ID_SLIDESHOW_RATE_30000", "VIV_ID_SLIDESHOW_RATE_60000",
              "VIV_ID_SLIDESHOW_RATE_CUSTOM"}
    check("rate ladder is slimmed to the wanted set", set(rates) == wanted,
          f"{sorted(set(rates) ^ wanted)}")

    # zoom group contents live between the zoom markers
    zpos = [i for i, x in enumerate(items) if x == "_VIV_MENU_VIEW_ZOOM"]
    zoom_items = items[zpos[0] + 1:zpos[1]]
    for want in ("VIV_ID_VIEW_ZOOM_IN", "VIV_ID_VIEW_ZOOM_OUT", "VIV_ID_VIEW_1TO1",
                 "VIV_ID_VIEW_BESTFIT", "VIV_ID_VIEW_FILL_WINDOW",
                 "VIV_ID_VIEW_ALLOW_SHRINKING", "VIV_ID_VIEW_KEEP_ASPECT_RATIO"):
        check(f"{want} lives in the zoom submenu", want in zoom_items)

    check("paste is offered in the context menu",
          "VIV_ID_EDIT_PASTE" in items)
    check("the full menu stays navigable (next/prev first)",
          items[:2] == ["VIV_ID_NAV_NEXT", "VIV_ID_NAV_PREV"])
    check("the menu bar fallback stays (view menu when the bar is hidden)",
          "VIV_ID_VIEW_MENU" in items)
    # top level = the entries outside every submenu span (markers toggle it)
    top = []
    inside = None
    for x in items:
        if x.startswith("_VIV_MENU_"):
            if inside == x:
                inside = None  # pop
            elif inside is None:
                inside = x     # push
            continue
        if inside is None:
            top.append(x)
    check("the top level list is short (<= 26 entries)", len(top) <= 26,
          f"{len(top)}: {top}")


# ---------------------------------------------------------------------------
# beta.13: paste shows a clipboard image.
# ---------------------------------------------------------------------------
def t_paste_wiring():
    viv = read("src/viv.c").decode()

    check("WM_PASTE falls back to an image branch",
          re.search(r"else\s*\{\s*// no filenames on the clipboard", viv) is not None)
    # r114: the helper answers a verdict now - the image branch keeps it
    # as the first reader and the text path takes the miss.
    check("the image branch calls the paste helper",
          "if (!_viv_paste_clipboard_image())" in viv)
    check("dib is the primary paste format",
          "GetClipboardData(CF_DIB)" in viv)
    check("bitmap is the fallback paste format",
          "GetClipboardData(CF_BITMAP)" in viv)
    check("the clipboard owns the original, we copy it",
          "CopyImage(hbitmap,IMAGE_BITMAP,0,0,LR_CREATEDIBSECTION)" in viv)
    check("a pasted image clears the filename",
          "_viv_current_fd->cFileName[0] = 0;" in viv)
    check("an in flight load can not clobber a paste",
          "_viv_load_image_allow_draw = 0;" in viv and
          "InterlockedExchange(&_viv_load_image_terminate,1);" in viv)
    check("the pasted frame starts the normal first frame path",
          "_viv_start_first_frame();" in viv)
    check("the mipmap is built lazily (NULL is a supported frame state)",
          "_viv_slot_current.frames[0].mipmap = 0; // built lazily on the first paint." in viv)
    check("the paste helpers have prototypes",
          re.search(r"(?:static\s+)?BOOL _viv_paste_clipboard_image\(void\);", viv) is not None)  # r114: the helper answers a verdict now
    check("only 40 byte dib headers take the dib path",
          "bih->biSize == sizeof(BITMAPINFOHEADER)" in viv)
    check("the dib stride math is overflow safe",
          "safe_size_mul((SIZE_T)bih->biWidth,(SIZE_T)bih->biBitCount)" in viv)




# ---------------------------------------------------------------------------
# rc.1: percent based zoom stepping + the always visible zoom pane.
# ---------------------------------------------------------------------------
def t_zoom_percent_wiring():
    viv = read("src/viv.c").decode()
    osh = read("src/os.h").decode()
    rh = read("res/resource.h").decode()
    rct = read("res/voidImageViewer.rc").decode(errors="replace")
    lh = read("src/localization.h").decode()
    le = read("src/localization_en_us.h").decode()
    lz = read("src/localization_zh_cn.h").decode()

    # stepping: snap to the nearest multiple of 10 first, then 10% per click
    m = re.search(r"(?:static\s+)?void _viv_zoom_in\(int out,int have_xy,int x,int y\)\s*\{(.*?)\n\}",
                  viv, re.S)
    assert m, "_viv_zoom_in not found"
    body = m.group(1)
    check("button zoom uses the percent stepper",
          "_viv_zoom_percent();" in body and "_viv_zoom_set_percent(target" in body)
    # rc.13: the snap is direction strict (the field report - the rc.1
    # nearest-multiple snap stepped a wheel-stopped 31% zoom-in down to 30
    # and the first click looked dead).
    check("the out snap lands on the multiple strictly below",
          "target = ((percent - 1) / 10) * 10;" in body)
    check("the in snap lands on the multiple strictly above",
          "target = ((percent / 10) * 10) + 10;" in body)
    check("the snap target never drops below 1 percent",
          "if (target < 1)" in body and "target = 1;" in body)
    check("buttons no longer delegate to the wheel action",
          "_viv_do_mousewheel_action" not in body)
    check("percent steps are skipped without an image",
          "if (!_viv_slot_current.image_wide)" in body)

    # the percent -> ladder position search
    m = re.search(r"(?:static\s+)?void _viv_zoom_set_percent\(int percent,int screen_x,int screen_y,int force\)\s*\{(.*?)\n\}",
                  viv, re.S)
    assert m, "_viv_zoom_set_percent not found"
    body = m.group(1)
    check("the percent search is a binary search over the ladder",
          "lo + ((hi - lo) / 2)" in viv and "_viv_zoom_pos_max() + 1" in viv
          and re.search(r"(?:static\s+)?int _viv_zoom_pos_for_percent\(int percent,int strict\)", viv) is not None
          and "_viv_zoom_pos_for_percent(percent,0)" in viv
          and "_viv_zoom_pos_for_percent(next,1)" in viv)
    check("exact 100 percent enters the 1:1 mode",
          "if (percent == 100)" in body and "_viv_1to1 = 1;" in body and
          "_viv_old_zoom_pos = _viv_zoom_pos;" in body)
    check("leaving 1:1 mode clears the flag",
          "_viv_1to1 = 0;" in body)
    check("the result is clamped to the live ladder",
          "_viv_clamp_zoom_pos(_viv_zoom_pos);" in body)
    check("the anchor math keeps the point under the cursor fixed",
          "new_cursor_x = ((__int64)old_cursor_px * (__int64)new_rw) / (__int64)old_rw;" in body)
    check("the view is invalidated and the status refreshed",
          "InvalidateRect(_viv_hwnd,0,FALSE);" in body and
          "_viv_status_update_temp_pos_zoom();" in body)
    check("button clicks force visible progress when the target is unreachable",
          "if (force && (!_viv_1to1))" in body and
          "next = ((old_percent / 10) * 10) + ((force > 0) ? 10 : -10);" in body and
          "_viv_zoom_pos_for_percent(next,1)" in body)
    check("a zoom out click at the true ladder floor is a no-op",
          "if (out && (!_viv_1to1) && (_viv_zoom_pos <= _viv_zoom_pos_floor()))" in viv)
    check("buttons pass the direction, the editor does not force",
          "_viv_zoom_set_percent(target,pt.x,pt.y,out ? -1 : 1);" in viv and
          "_viv_zoom_set_percent(percent,pt.x,pt.y,0);" in viv)

    # the status bar zoom pane (part 0, always visible, clickable). the
    # checks below query the whole file (the body extraction this slot once
    # attempted was born dead - its sentinel never matched - and the
    # always-true assert that carried it retired in round 113).
    check("the parts array rides the state cap",
          "int part_array[_VIV_STATUS_PART_MAX];" in viv)
    check("zoom text is built for the pane",
          "wchar_t zoom_buf[STRING_SIZE];" in viv and "*zoom_buf = 0;" in viv)
    check("the zoom pane is the leftmost fixed part",
          "part_array[parti] = zoom_wide;" in viv)
    check("the pane is measured like the other parts",
          "GetTextExtentPoint32(hdc,zoom_buf,string_get_length(zoom_buf),&size)" in viv)
    check("the message pane moved to part 1",
          "_viv_status_set(1,text);" in viv and "_viv_status_set(0,zoom_buf);" in viv)
    check("the right parts start after the message pane (rc.7 layout)",
          "parti = preload_wide ? 3 : 2;" in viv)
    check("the pane width is never below the minimum",
          "if (zoom_wide < minwide)" in viv)

    # rc.79: the click opens the in place editor on the pane (the 1998
    # centered dialog box is retired with its template, ids and strings)
    check("status click case 0 opens the editor",
          "_viv_set_zoom_dialog();" in viv)
    check("the frame toggle pane is now located dynamically",
          "SendMessage(_viv_status_hwnd,SB_GETPARTS,0,0) - 2" in viv)
    check("hand cursor over the zoom pane",
          "case WM_SETCURSOR:" in viv and "SB_GETRECT" in viv and "IDC_HAND" in viv)
    check("the editor commits the clamped range",
          "if (percent > 1600)" in viv and "if (percent >= 1)" in viv)
    check("the zoom dialog is fully retired",
          "IDD_SET_ZOOM" not in rct and "IDC_SET_ZOOM" not in rh and
          "LOCALIZATION_ID_SET_ZOOM" not in lh and
          "SET_ZOOM" not in le and "SET_ZOOM" not in lz)

    # the temp zoom flash is replaced by the permanent pane
    m = re.search(r"(?:static\s+)?void _viv_status_update_temp_pos_zoom\(void\)\s*\{(.*?)\n\}",
                  viv, re.S)
    assert m
    body = m.group(1)
    check("temp zoom flash now just refreshes the status bar",
          "_viv_status_update();" in body and "string_printf" not in body and
          "_viv_status_set_temp_text" not in body)



# ---------------------------------------------------------------------------
# 17. rc.2 review fixes: every finding from the external code review that
#     was verified real gets a permanent regression guard here.
# ---------------------------------------------------------------------------
def t_review_fixes():
    viv = read("src/viv.c").decode()
    osh = read("src/os.h").decode()
    osc = read("src/os.c").decode()
    stc = read("src/string.c").decode()
    sth = read("src/string.h").decode()
    nsi = read("nsis/installer.nsi").decode(errors="replace")

    # H1: the gesture config wrapper must match winuser.h
    check("os gesture wrapper signature matches winuser.h",
          "UINT cIDs,os_GestureConfig_t *configs,UINT cbSize" in osh and
          "UINT cIDs,os_GestureConfig_t *configs,UINT cbSize" in osc)
    check("gesture config call passes 3 configs + sizeof",
          "os_SetGestureConfig(hwnd,0,3,gesture_configs,sizeof(os_GestureConfig_t));" in viv)
    check("gesture ids are the real GID_* values",
          "gesture_configs[0].dwID = 3;" in viv and
          "gesture_configs[1].dwID = 4;" in viv and
          "gesture_configs[2].dwID = 6;" in viv)
    check("the broken zero config call is gone",
          "os_SetGestureConfig(hwnd,0,0,gesture_configs,3)" not in viv)

    # H2: string_get_word is bounded now
    check("string_get_word takes a buffer size",
          "wchar_t *string_get_word(wchar_t *p,wchar_t *buf,int buf_size)" in sth and
          "wchar_t *string_get_word(wchar_t *p,wchar_t *buf,int buf_size)" in stc)
    check("string_get_word clamps both copy branches",
          stc.count("if (d - buf < buf_size - 1)") == 2)
    check("all viv.c callers pass STRING_SIZE",
          viv.count("string_get_word(p,buf,STRING_SIZE)") == 17 and
          "string_get_word(p,install_path,STRING_SIZE)" in viv and
          "string_get_word(p,language_wbuf,STRING_SIZE)" in viv)

    # H3: every fd.cFileName copy is bounded to MAX_PATH (round 111:
    # the scan hoists collapsed the six late builds into three early
    # ones, 12 -> 9).
    check("all 9 fd.cFileName copies are bounded",
          viv.count("string_copy_with_bufsize(fd.cFileName,MAX_PATH") == 9)
    check("no unbounded fd.cFileName copy remains",
          "string_copy(fd.cFileName," not in viv)

    # H4: the add/remove programs registration exists
    check("arp install helper writes the uninstall key",
          "static void _viv_install_add_remove_programs(const wchar_t *install_path)" in viv and
          "Uninstall\\\\voidImageViewer" in viv)
    check("arp uninstall helper removes both hives",
          "static void _viv_uninstall_add_remove_programs(void)" in viv and
          viv.count("RegDeleteKeyW(HKEY_") == 2)
    check("install/uninstall call the arp helpers",
          "_viv_install_add_remove_programs(install_path);" in viv and
          "_viv_uninstall_add_remove_programs();" in viv)
    check("nsis .onInit strips the quoted uninstall string",
          ("StrCmp $R3 " + "'" + chr(34) + "'" + " 0 +2") in nsi and
          'StrCpy $R2 $R2 "" 1' in nsi)

    # M1: zero delay frames can not stall the frame skip loop
    check("frame skip guards zero delay frames",
          viv.count("(_viv_slot_current.frames[_viv_frame_position].delay > 0) ?") == 2)

    # M2: webp frames composite over the backdrop like the gdi+ path
    check("webp frame is drawn into a dib section",
          "CreateDIBSection(viv_webp->screen_hdc,&bmi,DIB_RGB_COLORS,&bits,NULL,0);" in viv)
    check("webp frames get the backdrop painted first",
          "_viv_fill_backdrop(viv_webp->mem_hdc,viv_webp->wide,viv_webp->high);" in viv)
    check("the webp pre-flatten onto the window background is gone",
          "config_windowed_background_color_b + ((b - config_windowed_background_color_b)" not in viv)

    # M3: everything ipc replies are validated field by field
    check("copydata helpers exist",
          re.search(r"(?:static\s+)?SIZE_T _viv_copydata_read\(const COPYDATASTRUCT \*cds,", viv) is not None and
          re.search(r"(?:static\s+)?int _viv_everything_item_to_fd\(const COPYDATASTRUCT \*cds,", viv) is not None)
    check("both everything cases validate the list header first",
          viv.count("if (_viv_safe_copy_data(cds->lpData,cds->cbData,0,&list,sizeof(list)))") == 2)
    check("no raw trust of sender offsets remains",
          "filename_len = *(DWORD *)p;" not in viv)

    # M4: the mipmap stop condition compares the right axis
    check("mipmap stop condition uses mip_high",
          "(render_wide >= mip_wide) || (render_high >= mip_high)" in viv and
          "render_high >= mip_wide)" not in viv)

    # M5: the pasted dib size is validated against the clipboard global
    check("paste dib validates GlobalSize before copying",
          "GlobalSize(hglobal)" in viv)

    # M7: the status panes test the buffer content, not the pointer
    # (rc.7: the pane EXISTENCE is width driven - the layout can drop a
    #  pane to make room - but the buffer content still decides the text.)
    check("pos/rgb panes dereference their buffers",
          viv.count("if (*pixel_pos_buf)") >= 1 and
          viv.count("if (*pixel_rgb_buf)") >= 1 and
          "if (pixel_pos_buf)" not in viv and
          "if (pixel_rgb_buf)" not in viv and
          "if (pixel_pos_wide)" in viv and
          "if (pixel_rgb_wide)" in viv)

    # preload OOB: the additional frame write is bounds checked
    check("preload additional frame write is bounds checked",
          "if (_viv_slot_preload.frame_loaded_count < _viv_slot_preload.frame_count)" in viv)

    # save as refuses to save the progressive preview thumbnail
    check("save as refuses the low res preview",
          viv.count("if (_viv_image_is_low_res)") == 1 and
          "do not save while the progressive preview is on screen" in viv)

    # GetLayout lives in gdi32, not user32
    check("GetLayout loads from gdi32",
          'GetProcAddress(_os_gdi32_hmodule,"GetLayout")' in osc and
          'GetProcAddress(_os_user32_hmodule,"GetLayout")' not in osc)


# ---------------------------------------------------------------------------
# 18. rc.3 second review pass: guards for this round's verified fixes, plus
#     a tripwire documenting the rejected gesture id claim (GID_TWOFINGERTAP
#     is 6 in winuser.h; 5 is GID_ROTATE - verified against the mingw-w64
#     header and microsoft learn).
# ---------------------------------------------------------------------------
def t_review_fixes_round2():
    viv = read("src/viv.c").decode()

    # R1 rejected: the gesture id claim was false, the comment marks the trap
    check("gesture id tripwire documents the winuser.h truth",
          'GID_TWOFINGERTAP 6 (5 is GID_ROTATE' in viv and
          'gesture_configs[2].dwID = 6;' in viv)

    # R4: the quoted uninstall string copy is bounded to the remaining space,
    # and (round 14) reserves the trailing suffix so string_cat can never
    # truncate "\Uninstall.exe\"" mid-way on a long install path.
    check("arp uninstall path copy is bounded",
          "string_copy_with_bufsize(uninstall_wbuf + 1,STRING_SIZE - 1 - 16,install_path);" in viv and
          "string_copy(uninstall_wbuf + 1" not in viv)

    # L5: the jumpto modal pump re-injects a consumed WM_QUIT
    i = viv.find("if (!GetMessageW(&msg,NULL,0,0))")
    check("jumpto pump re-posts a consumed WM_QUIT",
          i != -1 and "PostQuitMessage((int)msg.wParam);" in viv[i:i+400])

    # L1/R40: a zero file drop is a no-op before the playlist is touched.
    # the drop handling moved into _viv_drop_files so the clipboard paste
    # can share it without faking a WM_DROPFILES message (the shell hdrop
    # must be DragFinish-ed, the clipboard one must not).
    fstart = viv.find("static void _viv_drop_files(HWND hwnd,HDROP hdrop)")
    drop = viv[fstart:fstart + 1400]
    check("zero file drop is a no-op",
          fstart != -1 and
          "count = DragQueryFile(hdrop,0xFFFFFFFF,0,0);" in drop and
          "if (!count)" in drop and
          0 <= drop.find("count = DragQueryFile") < drop.find("if (!count)") < drop.find("is_shift = (GetKeyState"))
    check("shell drops are DragFinish-ed, the clipboard drop is not",
          "DragFinish((HDROP)wParam);" in viv and
          "_viv_drop_files(hwnd,hdrop);" in viv and
          "SendMessage(hwnd,WM_DROPFILES,(WPARAM)hdrop,0);" not in viv)

    # R2: webp first frame reports transposed dimensions for 5-8
    i = viv.find("first_frame.wide = viv_webp->wide;")
    check("webp first frame swaps axes for orientation 5-8",
          i != -1 and "switch (viv_webp->orientation)" in viv[i:i+900] and
          "temp = first_frame.wide;" in viv[i:i+900])

    # R2: webp additional frames pick mipmap dims after the orientation swap
    check("webp additional frame builds no mipmap (the paint path is lazy)",
          "_viv_get_mipmap(hbitmap,frame_wide,frame_high," not in viv and
          "_viv_get_mipmap(hbitmap,viv_webp->wide," not in viv and
          viv.count("_viv_get_mipmap(hbitmap") == 2)

    # R5 hardening: the reply consumer clamps the frame count
    check("first frame reply clamps zero frame counts",
          "if (!first_frame->frame_count)" in viv and
          "first_frame->frame_count = 1;" in viv)

    # L4 rejected: the 4701 suppression stays because the reads are guarded
    check("last_stretch_mode read stays guarded by did_set_stretch_blt_mode",
          "if (did_set_stretch_blt_mode)" in viv)


# ---------------------------------------------------------------------------
# 17. rc.4 release engineering pass, batch 1 guards: no infinite waits,
#     the installer script auto-detects sanely, the rc mojibake is gone,
#     the repo junk is untracked and the review hardening is in place.
# ---------------------------------------------------------------------------
def t_review_fixes_round4():
    viv = read("src/viv.c").decode("utf-8", errors="replace")
    rc = read("res/voidImageViewer.rc").decode("utf-8", errors="replace")
    ps1 = read("nsis/build_installer.ps1").decode("utf-8", errors="replace")
    gi = read(".gitignore").decode()

    # F3: closing instances and exit no longer wait forever
    check("close existing uses a timeout-aware send",
          "SendMessageTimeoutA(hwnd,WM_CLOSE,0,0,SMTO_ABORTIFHUNG,5000,0);" in viv and
          "SendMessage(hwnd,WM_CLOSE,0,0);" not in viv)
    check("close existing has a last resort terminate",
          "TerminateProcess(process_handle,1);" in viv)
    body = viv[viv.rfind("static void _viv_close_existing_process(void)"):
               viv.rfind("static void _viv_uninstall_delete_file")]
    check("close existing retries are bounded",
          "for(attempts = 0;attempts < 16;attempts++)" in body and
          "for(;;)" not in body)
    check("no INFINITE wait remains anywhere in viv.c",
          "INFINITE" not in viv)
    check("kill waits bounded for the load thread, the hard exit replaced the hard kill",
          "WaitForSingleObject(_viv_load_image_thread,10000) != WAIT_OBJECT_0" in viv and
          "ExitProcess(1);" in viv and
          "TerminateThread(_viv_load_image_thread,1);" not in viv)

    # the rc mojibake is gone, the copyright is plain ascii like upstream
    check("rc has no utf-8 replacement character",
          "\ufffd" not in rc)
    check("rc copyright is the ascii (C) form",
          'VALUE "LegalCopyright", "Copyright (C) 2026 voidtools"' in rc)

    # ps1: auto detect prefers built exes then vswhere, not directory existence
    check("ps1 no longer auto-picks by plain directory existence",
          '$VsVersion = "vs2026"' not in ps1)
    check("ps1 probes for a built exe per project dir",
          'foreach ($vs in @("vs2026", "vs2019"))' in ps1 and
          "Test-Path $candidate" in ps1)
    check("ps1 falls back to the installed toolchain via vswhere",
          "vswhere.exe" in ps1 and "installationVersion" in ps1)

    # repo hygiene: the junk is gone and gitignore covers the classes
    check("pax headers directory is gone",
          not os.path.exists("libwebp/PaxHeaders.X"))
    check("binary resource editor state is gone",
          not os.path.exists("res/voidImageViewer.aps"))
    check("unreferenced 1to1-32bit.ico is gone",
          not os.path.exists("res/1to1-32bit.ico"))
    check("gitignore covers aps, pax headers, user state, link intermediates",
          "*.aps" in gi and "PaxHeaders.X/" in gi and
          "*.user" in gi and "*.iobj" in gi)

    # F2: the ipc reply item count is clamped to the message size
    check("ipc reply item count is clamped to the message size",
          "max_items = (DWORD)((cds->cbData - sizeof(EVERYTHING_IPC_LIST2)) / sizeof(EVERYTHING_IPC_ITEM2));" in viv and
          "for(i=0;(i < list.numitems) && (i < max_items);i++)" in viv)

    # F1: the frame count is clamped before the UINT -> int store
    check("first frame count is clamped to a sane maximum",
          "if (first_frame->frame_count > 0x10000)" in viv and
          "first_frame->frame_count = 0x10000;" in viv)

    # F1: the rotate buffer allocations multiply in SIZE_T
    # (round 16 upgraded the multiplication to the checked safe_size helpers)
    check("rotate buffer allocations multiply through the checked helpers",
          "mem_alloc(safe_size_mul(safe_size_mul((SIZE_T)bitmap.bmWidth,(SIZE_T)bitmap.bmHeight),sizeof(DWORD)));" in viv and
          "mem_alloc(safe_size_mul(safe_size_mul((SIZE_T)ret_wide,(SIZE_T)ret_high),sizeof(DWORD)));" in viv)
    check("the uncast rotate allocations are gone",
          "mem_alloc(bitmap.bmWidth * bitmap.bmHeight" not in viv and
          "mem_alloc(ret_wide * ret_high" not in viv)


# ---------------------------------------------------------------------------
# rc.5 round: the release engineering batch 2 (user-approved D1-D8).
# ---------------------------------------------------------------------------
def t_release_engineering_round5():
    viv = read("src/viv.c").decode("utf-8", errors="replace")
    gi = read(".gitignore").decode()
    ry = read(".github/workflows/release.yml").decode()
    ty = read(".github/workflows/tests.yml").decode()

    # the frame array multiplications go through safe_size_mul (the idle
    # wrench from safe_size.h; a 32 bit sizeof*count could wrap before
    # reaching the allocator even with the clamped count).
    check("preload frame array allocation uses safe_size_mul",
          "mem_alloc(safe_size_mul(sizeof(_viv_frame_t),(SIZE_T)_viv_slot_preload.frame_count));" in viv)
    check("frame array allocation uses safe_size_mul",
          "mem_alloc(safe_size_mul(sizeof(_viv_frame_t),(SIZE_T)_viv_slot_current.frame_count));" in viv)
    check("the raw frame array multiplications are gone",
          "sizeof(_viv_frame_t) * _viv_slot_preload.frame_count" not in viv and
          "sizeof(_viv_frame_t) * _viv_slot_current.frame_count" not in viv)

    # the everything FILE_SIZE-indexed branch requests SIZE only; date
    # modified is requested by its own indexed check right below (the
    # duplicated request was a typo, in both search senders).
    size_lines = [l for l in viv.splitlines()
                  if "EVERYTHING_IPC_QUERY2_REQUEST_SIZE" in l and "|=" in l]
    check("exactly two SIZE request sites remain",
          len(size_lines) == 2, repr(size_lines))
    check("the FILE_SIZE branch no longer piggybacks DATE_MODIFIED",
          all("REQUEST_DATE_MODIFIED" not in l for l in size_lines))

    # the gpl-licensed crt.c is gone and nothing references it
    check("the gpl crt.c is deleted",
          not os.path.exists("src/crt.c"))
    for proj in ("vs2019/voidImageViewer.vcxproj", "vs2026/voidImageViewer.vcxproj"):
        p = read(proj).decode("utf-8", errors="replace")
        check(proj + " does not reference crt.c",
              'ClCompile Include="..\\src\\crt.c"' not in p and
              "crt.c" not in p)

    # gitignore: python bytecode was the one uncovered class
    check("gitignore covers python bytecode",
          "__pycache__/" in gi)

    # release workflow: no clobber, hash chain, gate, whitelist, least privilege
    check("release has no upload/clobber path (create only, no overwrite)",
          "gh release upload" not in ry and "gh release edit" not in ry)
    check("release refuses to overwrite an existing release",
          "Refuse to overwrite an existing release" in ry)
    check("release job order is validate -> tests -> build -> publish",
          all(j in ry for j in ("  validate:", "  tests:", "  build:", "  publish:")))
    check("release publishes only with contents: write, workflow default is read",
          "    permissions:\n      contents: write" in ry and
          "permissions:\n  contents: read" in ry)
    check("artifact hashes are re-verified after download",
          "Re-verify artifact SHA-256" in ry and "sha256sum -c sha256.txt" in ry)
    check("tag whitelist regex accepts 3 or 4 numeric segments",
          "grep -Eq '^v?[0-9]+\\.[0-9]+(\\.[0-9]+){1,2}(-(beta|rc)\\.[0-9]+)?$'" in ry)
    check("tag must match src/version.h",
          "Verify the tag matches src/version.h" in ry)
    check("release notes are generated from Changes.txt, not embedded",
          "Generate release notes from Changes.txt" in ry and
          "Recent changes" not in ry and
          "beta.13" not in ry)
    check("user input reaches shells only through env",
          "${{ inputs.tag }}" not in ry.replace("INPUT_TAG: ${{ inputs.tag }}", "") and
          "INPUT_TAG: ${{ inputs.tag }}" in ry and
          "${{ github.ref_name }}" not in ry.replace("REF_NAME: ${{ github.ref_name }}", "") and
          "REF_NAME: ${{ github.ref_name }}" in ry)
    check("the prerelease input is derived from the version phase, not typed in",
          "inputs.prerelease" not in ry)

    # libwebp 1.6.0 import: provenance, decode-only tree and build set
    vi = read("libwebp/VERSION.imported").decode("utf-8", errors="replace")
    check("libwebp provenance records 1.6.0 + tarball sha256",
          "version:         1.6.0" in vi and
          "93a852c2b3efafee3723efd4636de855b46f9fe1efddd607e1f42f60fc8f2136" in vi)
    for d in ("webp_js", "examples", "imageio", "swig", "man", "gradle",
              "infra", "extras", "sharpyuv", "src/enc", "src/mux"):
        check("libwebp/%s is pruned" % d, not os.path.exists("libwebp/" + d))
    for f in ("cost.c", "enc_sse2.c", "lossless_enc.c", "ssim.c",
              "bit_writer_utils.c", "huffman_encode_utils.c",
              "quant_levels_utils.c"):
        check("encoder-side file gone: %s" % f,
              not os.path.exists("libwebp/src/dsp/" + f) or
              not os.path.exists("libwebp/src/utils/" + f))
    for d in ("src/dec", "src/demux", "src/dsp", "src/utils", "src/webp"):
        check("libwebp/%s kept" % d, os.path.exists("libwebp/" + d))
    # R70 housekeeping: the non-Windows build systems, fuzzers and docs
    # left the vendored tree (configure.ac and VERSION.imported stay as the
    # version and provenance pins this suite reads above).
    for d in ("tests", "doc", "cmake"):
        check("libwebp/%s pruned by the R70 housekeeping" % d,
              not os.path.exists("libwebp/" + d))
    for f in (".cmake-format.py", ".pylintrc", ".style.yapf"):
        check("libwebp/%s pruned (build-system lint config)" % f,
              not os.path.exists("libwebp/" + f))
    fp_list = read("voidImageViewer.files.props").decode("utf-8", errors="replace")
    cc = [f for f in re.findall(r"<ClCompile Include=\"([^\"]+)\"", fp_list) if "libwebp" in f]
    check("the shared file list compiles the 66-file decode-only set",
          len(cc) == 66, "%d entries" % len(cc))
    check("the shared file list adds the new avx2 lossless variant",
          "lossless_avx2.c" in fp_list)
    check("no project compiles any encoder-side file",
          "cost.c" not in fp_list and "enc_sse2.c" not in fp_list and
          "lossless_enc.c" not in fp_list and "ssim.c" not in fp_list and
          "bit_writer_utils.c" not in fp_list and "huffman_encode_utils.c" not in fp_list)
    ac = read("libwebp/configure.ac").decode()
    check("vendored tree is libwebp 1.6.0",
          "[1.6.0]" in ac)

    # structure: vs2005 deleted, config families trimmed, shared file list
    check("vs2005 project directory is deleted",
          not os.path.exists("vs2005"))
    for f, needle in (("nsis/build_installer.ps1", "vs2005"),
                      ("nsis/installer.nsi", "Supported versions: vs2005"),
                      ("src/viv.c", "vs2005 and vs2019 solutions")):
        t = read(f).decode("utf-8", errors="replace")
        check("%s no longer references vs2005" % f, needle not in t)
    fp = read("voidImageViewer.files.props").decode("utf-8", errors="replace")
    check("shared props carries the full compile list",
          len(re.findall(r"<ClCompile ", fp)) == 104)  # 81 + 11 R70 domains + wndproc + the R77 menubar module + the rc.8 toolbar/settings domains + the theme core and the msgbox + the R94 qoi and wic decoders + the R96 gl, d3d and shell menu modules + the R102 export module
    check("shared props has no phantom res\\resource reference",
          'res\\resource"' not in fp)
    check("shared props has the resource script and the app icon only",
          "voidImageViewer.rc" in fp and "voidImageViewer.ico" in fp
          and "1to1-8bit.ico" not in fp and "prev.ico" not in fp)
    for proj, toolset in (("vs2019/voidImageViewer.vcxproj", "v143"),
                          ("vs2026/voidImageViewer.vcxproj", "v145")):
        p = read(proj).decode("utf-8", errors="replace")
        check(proj + " imports the shared file list",
              'Import Project="..\\voidImageViewer.files.props"' in p)
        check(proj + " carries no file items of its own",
              "<ClCompile " not in p and "<ClInclude " not in p)
        check(proj + " has only Debug and Release configurations",
              sorted(set(re.findall(r"<Configuration>([^<]+)</Configuration>", p))) == ["Debug", "Release"])
        check(proj + " has no ALPHA/BETA/LITE remains",
              "ALPHA" not in p and "BETA" not in p and "LITE" not in p)
        check(proj + " toolset adjudicated to " + toolset,
              sorted(set(re.findall(r"<PlatformToolset>([^<]+)</PlatformToolset>", p))) == [toolset])
        check(proj + " still defines 8 configuration groups",
              len(re.findall(r"<ItemDefinitionGroup ", p)) == 8)
    readme = read("README.md").decode("utf-8", errors="replace")
    check("README build section documents the v143 adjudication",
          "VS2022+, v143 toolset" in readme and "/p:PlatformToolset=v142" in readme)
    check("README documents the pinned runner matrix",
          "windows-2022" in readme and "windows-2025" in readme)

    # tests workflow: pinned runners, drift matrix, schedule compile only
    check("compile pins windows-2022 for the shipping v143 path",
          "windows-2022" in ty and "runner: windows-2022" in ty)
    check("compile adds the windows-2025 v145 compatibility leg",
          "windows-2025" in ty and "project: vs2026" in ty and "toolset: v145" in ty)
    check("windows-latest is no longer used by any job",
          "runs-on: windows-latest" not in ty and "runs-on: windows-latest" not in ry)
    check("the daily schedule skips the python suites",
          "if: github.event_name != 'schedule'" in ty)
    check("tags run the tests workflow too (both styles)",
          "tags: ['v*', '[0-9]*']" in ty)
    check("actions are pinned to commit shas",
          "actions/checkout@3d3c42e5aac5ba805825da76410c181273ba90b1" in ty and
          "microsoft/setup-msbuild@30375c66a4eea26614e0d39710365f22f8b0af57" in ty)




# ---------------------------------------------------------------------------
# rc.6 modernization round: per monitor v2, win11 chrome, vector glyphs,
# the fullscreen overlay bar with the idle fade, and the 4-layer icon sync.
# ---------------------------------------------------------------------------
def t_modernization_round6():
    viv = read("src/viv.c").decode("utf-8", errors="replace")
    vh = read("src/viv.h").decode()
    osc = read("src/os.c").decode("utf-8", errors="replace")
    osh = read("src/os.h").decode()
    zc = read("src/zoomui.c").decode("utf-8", errors="replace")
    zh_ = read("src/zoomui.h").decode()
    gc = read("src/glyphs.c").decode("utf-8", errors="replace")
    gh = read("src/glyphs.h").decode()
    mf = read("res/voidImageViewer.Manifest").decode()
    rh = read("res/resource.h").decode()
    rct = read("res/voidImageViewer.rc").decode("utf-8", errors="replace")
    fp = read("voidImageViewer.files.props").decode("utf-8", errors="replace")
    cc = read("src/config.c").decode()
    ch = read("src/config.h").decode()
    lh = read("src/localization.h").decode()
    le = read("src/localization_en_us.h").decode("utf-8", errors="replace")
    lz = read("src/localization_zh_cn.h").decode("utf-8", errors="replace")

    # per monitor v2 (the real mixed-dpi fix)
    check("manifest declares PerMonitorV2 with the PerMonitor fallback",
          "PerMonitorV2, PerMonitor" in mf and "dpiAwareness" in mf)
    check("viv.c defines the WM_DPICHANGED fallback",
          "#define WM_DPICHANGED 0x02E0" in viv)
    check("viv.c handles WM_DPICHANGED",
          "case WM_DPICHANGED:" in viv)
    check("the handler accepts the suggested rect",
          "suggested_rect" in viv and "os_window_update_dpi(hwnd)" in viv)
    check("the dpi handler re-lays the strip through the size sweep",
          viv.find("_viv_toolbar_build_image_list();",
                   viv.find("static LRESULT _viv_on_wm_dpichanged(")) == -1 and
          viv.find("_viv_on_size();",
                   viv.find("static LRESULT _viv_on_wm_dpichanged(")) != -1)  # rc.8: the self drawn strip owns its dpi metrics
    check("os.c loads GetDpiForWindow from user32",
          'GetProcAddress(_os_user32_hmodule,"GetDpiForWindow")' in osc)
    check("os.c implements os_window_update_dpi with the 96 floor",
          "int os_window_update_dpi(HWND hwnd)" in osc and "dpi < 96" in osc)
    check("os.h exports os_window_update_dpi",
          "int os_window_update_dpi(HWND hwnd);" in osh)

    # win11 chrome
    # rc.13: the chrome calls return int and double as the platform probe
    # (the attribute 33 result latches the win11 answer).
    check("os.c implements os_window_modern_chrome",
          "int os_window_modern_chrome(HWND hwnd,COLORREF caption_color,COLORREF text_color)" in osc)
    check("chrome sets corner preference 33 to round and probes the platform",
          "if (_os_DwmSetWindowAttribute(hwnd,33,&corner,sizeof(corner)) != 0)" in osc
          and "corner = 2;" in osc and "_os_win11_chrome = 2;" in osc)
    check("chrome sets caption color 35",
          "_os_DwmSetWindowAttribute(hwnd,35,&color,sizeof(color));" in osc)
    check("os.h exports os_window_modern_chrome",
          "int os_window_modern_chrome(HWND hwnd,COLORREF caption_color,COLORREF text_color);" in osh)
    check("viv.c applies the chrome from apply_dark_mode",
          "os_window_modern_chrome(_viv_hwnd,_viv_windowed_background(),viv_theme_color(VIV_TK_TEXT));" in viv)

    # vector glyphs
    check("glyphs.c/h exist and are in the shared props",
          os.path.exists("src/glyphs.c") and os.path.exists("src/glyphs.h")
          and "glyphs.c" in fp and "glyphs.h" in fp)
    check("the props counts grew by the glyphs pair, the R70 domains, the wndproc pair, the remake pair, the R96 renderers and the R102 export module",
          len(re.findall(r"<ClCompile ", fp)) == 104
          and len(re.findall(r"<ClInclude ", fp)) == 73)
    check("glyphs.c loads its own gdi+ flat api table",
          '"GdipCreatePen1"' in gc and '"GdipDrawLinesI"' in gc
          and '"GdipCreateBitmapFromScan0"' in gc
          and '"GdipCreateHICONFromBitmap"' in gc)
    check("glyphs.c renders through an alpha bitmap",
          "0x26200A" in gc and "mem_alloc" in gc)
    check("glyphs.c bakes both theme colors",
          "0xFFE8E8E8" in gc and "0xFF3C4043" in gc)
    check("glyphs.c uses round caps and antialiasing",
          "_glyphs_gdipSetPenStartCap(pen,_GLYPHS_LINE_CAP_ROUND)" in gc
          and "_glyphs_gdipSetSmoothingMode(graphics,_GLYPHS_SMOOTHING_ANTIALIAS)" in gc
          and "#define _GLYPHS_LINE_CAP_ROUND 2" in gc
          and "#define _GLYPHS_SMOOTHING_ANTIALIAS 4" in gc)
    check("glyphs.h exports the icon cache api",
          "HICON glyphs_icon(int glyph_id,int dark,int size);" in gh
          and "void glyphs_flush_cache(void);" in gh and "GLYPH_COUNT" in gh)
    tb_src = open("src/viv_toolbar.c", "rb").read().decode("utf-8", errors="replace")
    check("the toolbar draws its icons from the glyph domain",
          "glyphs_icon(" in tb_src and "GLYPH_FOLDER_OPEN" in tb_src)
    check("the old ico frames are gone from disk",
          all(not os.path.exists("res/" + n + ".ico") for n in
              ("1to1-8bit", "bestfit", "next", "pause", "play", "prev",
               "zoomin", "zoomout")))
    check("the app icon stays",
          os.path.exists("res/voidImageViewer.ico"))
    check("the rc keeps exactly one icon resource",
          len(re.findall(r"^\s*IDI_[A-Z0-9_]+\s+ICON", rct, re.M)) == 1
          and "IDI_ICON1               ICON" in rct)
    check("resource.h drops the 9 toolbar icon defines",
          all(("#define IDI_" + n + " ") not in rh for n in
              ("PREV", "PLAY", "NEXT", "PAUSE", "ICON2", "1TO1",
               "BESTFIT", "ZOOMOUT", "ZOOMIN"))
          and "#define IDI_ICON1" in rh)
    check("the props image list keeps only the app icon",
          len(re.findall(r"<Image Include=", fp)) == 1
          and "voidImageViewer.ico" in fp)
    check("the comctl image list builder is gone",
          "_viv_toolbar_build_image_list" not in viv and
          "_viv_toolbar_pin_button_sizes" not in viv and
          "LoadIcon(os_hinstance,(LPCTSTR)IDI_PREV)" not in viv and
          "MAKEINTRESOURCE(IDI_ZOOMOUT)" not in viv)
    check("the old LoadIcon toolbar icons are gone",
          "LoadIcon(os_hinstance,(LPCTSTR)IDI_PREV)" not in viv
          and "MAKEINTRESOURCE(IDI_ZOOMOUT)" not in viv)

    # the zoomui rewrite
    check("zoomui master table carries the six fullscreen buttons",
          "VIV_ID_NAV_PREV" in zc and "VIV_ID_SLIDESHOW_PLAY_ONLY" in zc
          and "VIV_ID_SLIDESHOW_PAUSE_ONLY" in zc and "VIV_ID_NAV_NEXT" in zc)
    check("the pill is one seven cell row in both modes",
          "#define _ZOOMUI_CELL_COUNT 7" in zc
          and "_ZOOMUI_CELL_PCT" in zc)
    check("zoomui declares the fullscreen switch",
          "void zoomui_set_fullscreen(int fullscreen);" in zh_
          and "void zoomui_set_fullscreen(int fullscreen)" in zc)
    check("zoomui declares the activity hook",
          "void zoomui_activity(void);" in zh_
          and "void zoomui_activity(void)" in zc)
    check("viv.c syncs the fullscreen mode",
          "zoomui_set_fullscreen(_viv_is_fullscreen);" in viv)
    check("the bar is layered with alpha fading",
          "WS_EX_LAYERED" in zc and "LWA_ALPHA" in zc)
    check("the layered probe removes the style on failure",
          "~WS_EX_LAYERED" in zc)
    check("the fade timer steps the alpha",
          "SetTimer(_zoomui_hwnd,_ZOOMUI_TIMER_ID,_ZOOMUI_FADE_INTERVAL,0)" in zc
          and "#define _ZOOMUI_FADE_MS 225" in zc  # rc.15: the fixed step is retired, the sweep is wall clock
          and "#define _ZOOMUI_IDLE_MS 2000" in zc)
    check("a fully transparent bar is hidden for real (it still eats clicks)",
          zc.find("ShowWindow(_zoomui_hwnd,SW_HIDE);",
                  zc.find("_zoomui_alpha == 0")) != -1)
    check("glyph icons feed the zoom buttons",
          "glyphs_icon(" in zc and "glyphs_flush_cache();" in zc)
    check("fullscreen centers the bar at the bottom",
          "(wide - container_wide) / 2" in zc)
    check("config gates the auto hide",
          "config_zoom_auto_hide" in zc)
    check("mouse, key and command activity wake the bar",
          viv.count("zoomui_activity();") >= 3)

    # config / menu / localization three line sync
    check("config.c defaults zoom_auto_hide to 1",
          "BYTE config_zoom_auto_hide = 1;" in cc)
    check("config.c persists zoom_auto_hide",
          '"zoom_auto_hide"' in cc
          and cc.count('_config_write_int(h,"zoom_auto_hide"') == 1)
    check("config.h externs zoom_auto_hide",
          "extern BYTE config_zoom_auto_hide;" in ch)
    check("the menu gains the auto hide row",
          "LOCALIZATION_ID_ZOOM_AUTO_HIDE,MF_STRING,_VIV_MENU_VIEW_LAYOUT,VIV_ID_VIEW_ZOOM_AUTO_HIDE" in viv)
    check("the command and check states exist",
          "case VIV_ID_VIEW_ZOOM_AUTO_HIDE:" in viv
          and "CheckMenuItem(hmenu,VIV_ID_VIEW_ZOOM_AUTO_HIDE," in viv)
    check("viv.h appends the command id",
          "VIV_ID_VIEW_ZOOM_AUTO_HIDE," in vh)
    for name in ("LOCALIZATION_ID_ZOOMUI_TOOLTIP_PREV",
                 "LOCALIZATION_ID_ZOOMUI_TOOLTIP_PLAY",
                 "LOCALIZATION_ID_ZOOMUI_TOOLTIP_PAUSE",
                 "LOCALIZATION_ID_ZOOMUI_TOOLTIP_NEXT",
                 "LOCALIZATION_ID_ZOOM_AUTO_HIDE"):
        check(f"{name} in all three localization lines",
              name in lh and name in le and name in lz)


# ---------------------------------------------------------------------------
# rc.7 field feedback round: pinch floor, 1600% ceiling, File > Options +
# complete Layout, the status hud layout, the owner drawn dark panes, the
# manifest compatibility section and the SMI/2016 namespace.
# ---------------------------------------------------------------------------
def t_round7():
    viv = read("src/viv.c").decode("utf-8", errors="replace")
    mf = read("res/voidImageViewer.Manifest").decode()
    menubar = read("src/viv_menubar.c").decode("utf-8", errors="replace")

    # the pinch floor: a collapsed pinch freezes and re-baselines.
    check("gesture floor uses 36 logical px, dpi scaled",
          "min_dist = (DWORD)((36 * os_logical_wide) / 96);" in viv)
    check("collapsed pinch drops the baseline and skips the sample",
          viv.count("_viv_gesture_zoom_dist = 0;") >= 3)
    i = viv.find("case 3: // GID_ZOOM")
    zoom_case = viv[i:viv.find("case 4: // GID_PAN", i)]
    check("the floor sits inside the GID_ZOOM case before GF_BEGIN",
          "min_dist" in zoom_case and
          0 <= zoom_case.find("min_dist") < zoom_case.find("GF_BEGIN"))

    # the ceiling: 16x native in BOTH copies of the cap math.
    check("render size cap = 16x native",
          "max_w = 16.0 * (double)_viv_slot_current.image_wide;" in viv)
    check("pos_max cap mirrors the render size cap",
          viv.count("max_w = 16.0 * (double)_viv_slot_current.image_wide;") == 2)
    check("the cap floors at the pos 0 fit (fill window)",
          viv.count("if (max_w < (double)rw)") == 2)
    check("the old 16x-the-LARGER rule is gone",
          "16.0 * (double)((rw > _viv_slot_current.image_wide)" not in viv)

    # the menu restoration.
    check("File > Options row exists (before the Exit separator)",
          "{LOCALIZATION_ID_OPTIONS,MF_STRING,_VIV_MENU_FILE,VIV_ID_VIEW_OPTIONS}," in viv)
    check("the View > Options row is gone",
          "{LOCALIZATION_ID_OPTIONS,MF_STRING,_VIV_MENU_VIEW,VIV_ID_VIEW_OPTIONS}" not in viv)
    check("Caption/Frame rows carry no MF_OWNERDRAW",
          "MF_STRING|MF_OWNERDRAW,_VIV_MENU_VIEW_LAYOUT" not in viv)

    # the status hud layout: preload left, resolution pinned right.
    check("the preload pane is capped at a quarter of the bar",
          "if (preload_wide > (avail_wide / 4))" in viv)
    check("the right cluster gives way (frame, rgb, date, pos)",
          "while ((zoom_wide + preload_wide + dimension_wide + frame_wide + pixel_pos_wide + pixel_rgb_wide + date_wide > avail_wide)" in viv)
    check("the resolution pane clips instead of vanishing",
          "dimension_wide = avail_wide - zoom_wide - preload_wide;" in viv)
    check("the panes use cumulative coordinates",
          "part_array[parti] = part_array[parti - 1] + flex_wide;" in viv)
    check("the message pane index follows the preload pane",
          "parti = preload_wide ? 3 : 2;" in viv)
    check("right cluster panes are width driven, not buffer driven",
          "if (pixel_pos_wide)\n" in viv.replace("\r\n", "\n"))

    # the owner drawn dark panes.
    # R70: the pane text store left the static prefix (chrome reads it
    # across the module boundary; the state layer carries the extern).
    check("the pane text store exists",
          "wchar_t _viv_status_part_text[_VIV_STATUS_PART_MAX][STRING_SIZE];" in viv and
          "static wchar_t _viv_status_part_text" not in viv)
    check("SB_SETTEXTW uses SBT_OWNERDRAW with the pane index as data",
          "SendMessage(_viv_status_hwnd,SB_SETTEXTW,(WPARAM)(part | SBT_OWNERDRAW),(LPARAM)part);" in viv)
    check("the old SB_GETTEXTW compare is gone",
          "SB_GETTEXTW" not in viv)
    check("the draw function paints through the chrome tokens",
          "_viv_status_draw_item(DRAWITEMSTRUCT" in viv and
          "text_color = viv_theme_color(VIV_TK_TEXT);" in viv)
    check("the main proc routes WM_DRAWITEM for the status bar",
          "if ((wParam == VIV_ID_STATUS) && (_viv_status_draw_item((DRAWITEMSTRUCT *)lParam)))" in viv)
    check("the status subclass routes WM_DRAWITEM too (the dialog dispatcher adds the second site)",
          viv.count("case WM_DRAWITEM:") == 4)  # rc.8: the rebar route retired, the status proc came home; rc.13: the settings dropdown owner joined

    # the dark chrome strips.
    check("the dark chrome brush palette routes through the theme cache",
          "static const int tokens[4] = {VIV_TK_CHROME,VIV_TK_CHROME_LINE,VIV_TK_CHROME_MUTED,VIV_TK_FRAME};" in viv)
    check("the menubar paint and erase follow the theme",
          menubar.count("viv_theme_brush(VIV_TK_FRAME)") == 3)  # remake-2: the strip face rides the shared theme brush cache (fill + ternary + erase)
    check("apply_dark flips the status bar and latches the strip",
          "os_dark_titlebar(_viv_status_hwnd,dark);" in viv and
          "_viv_toolbar_set_dark(dark);" in viv and
          "zoomui_set_dark(dark);" in viv)
    check("apply_dark nudges a frame change for the menu bar",
          "SWP_FRAMECHANGED" in viv)
    check("the status bar creation picks up the dark flags",
          viv.find("os_dark_titlebar(_viv_status_hwnd,1);",
                   viv.find("_viv_status_show(int show)")) != -1)

    # the manifest: supportedOS list + the real PMv2 namespace.
    check("manifest declares the windows 10 supportedOS guid",
          "{8e0f7a12-bfb3-4fe8-b9a5-48fd50a15a9a}" in mf)
    check("manifest declares windows 7 / 8 / 8.1 too",
          all(g in mf for g in ("{35138b9a-5d96-4fbd-8e2d-a2440225f93a}",
                                "{4a2f28e3-53b9-4441-ba9c-d69d4a4a6e38}",
                                "{1f676c76-80e1-4239-95bb-83d0f6d0da78}")))
    check("manifest has a compatibility section",
          "<compatibility" in mf and mf.count("<supportedOS") == 4)
    check("dpiAwareness lives in the SMI/2016 namespace",
          'xmlns="http://schemas.microsoft.com/SMI/2016/WindowsSettings"' in mf)
    check("the dual dpi declaration covers the pre-1607 loaders",
          "<dpiAware>true</dpiAware>" in mf
          and mf.count("<asmv3:windowsSettings") == 2)
    check("PerMonitorV2 declaration retained",
          "<dpiAwareness>PerMonitorV2, PerMonitor</dpiAwareness>" in mf)




# ---------------------------------------------------------------------------
# stable round (1.1.01): dark dialog child controls, language default auto,
# navigation scan cost, upstream style release tags.
# ---------------------------------------------------------------------------
def t_stable_round():
    viv = read("src/viv.c").decode()
    osh = read("src/os.h").decode()
    osc = read("src/os.c").decode()
    loc = read("src/localization.c").decode()
    ins = read("nsis/installer.nsi").decode()
    nsh = read("nsis/version.nsh").decode()
    ry = read(".github/workflows/release.yml").decode()
    vh = read("src/version.h").decode()

    # dark dialogs: the explorer dark style does not cascade, the fork now
    # opts every child control in (this is what the field screenshot showed:
    # dark dialog + light comboboxes / check glyphs).
    check("os.h exports os_allow_dark_mode_for_window",
          "extern int os_allow_dark_mode_for_window(HWND hwnd,int allow);" in osh)
    check("os.c implements the per-window allow",
          "int os_allow_dark_mode_for_window(HWND hwnd,int allow)" in osc)
    check("viv.c has the dark dialog child enumerator",
          "static BOOL CALLBACK _viv_dark_dialog_children(HWND hwnd,LPARAM lParam)" in viv)
    check("_viv_dark_dialog enumerates its children",
          "EnumChildWindows(hwnd,_viv_dark_dialog_children,0);" in viv)
    i = viv.find("static BOOL CALLBACK _viv_dark_dialog_children")
    seg_enum = viv[i:viv.find("\\n}", i)]
    check("the enumerator opts each control in before theming",
          seg_enum.find("os_allow_dark_mode_for_window(hwnd,1);") <
          seg_enum.find("os_dark_window_theme(hwnd);"))

    # language default auto: the installer no longer pins the app language.
    check("installer no longer forwards a language to the app",
          ins.count("/language ") == 0 and "forward_english" not in ins)
    check("installer documents the auto default",
          "NOT forwarded from the" in ins and 'starts in "auto"' in ins)
    check("auto detection covers all chinese ui locales",
          "0x1004" in loc and "0x1404" in loc and "0x0804" in loc)

    # navigation scan cost: the wrap target (start) tracking is deferred to a
    # second pass that only runs when the primary direction has no candidate.
    i = viv.find("int _viv_next(")
    i = viv.find("int _viv_next(", i + 10)
    # R70: the next col-0 function after an exported def may not be static,
    # so bound the segment by the def's own closing brace at column 0.
    j = viv.find("\n}", i)
    seg = viv[i:j]
    check("the playlist scan dropped its per-file start compare",
          "// compare with start" not in seg[:seg.find("if (!got_best)")])
    check("the folder scan dropped its per-file start compare",
          seg.count("compare with start") == 0)
    # round-125: the successor window's serve wrapper is the third
    # !got_best gate (the pop answers the step, the scan never runs).
    check("both deferred wrap passes exist (and the window serve gate)",
          seg.count("if (!got_best)") == 3)
    check("the deferred folder pass rebuilds the search pattern",
          seg.count("string_cat_utf8(search_wbuf,(const utf8_t *)"+chr(34)+chr(92)+chr(92)+"*.*"+chr(34)) == 2)
    check("both scans still find the primary candidate",
          seg.count("_viv_fd_compare(&d->fd,_viv_current_fd)") == 1 and
          seg.count("_viv_fd_compare(&fd,_viv_current_fd)") == 1)

    # release identity: upstream style tag 1.1.01 == VERSION_STRING.
    check("release workflow triggers on upstream style numeric tags",
          "'[0-9]*'" in ry)
    check("the tag whitelist accepts the optional v prefix",
          "^v?[0-9]+" in ry)
    check("validate compares the tag to VERSION_STRING",
          "VERSION_STRING" in ry and "tag.lstrip('v') != expected" in ry)
    check("version.nsh carries the display version",
          '!define DISPLAYVERSION "${VIV_VER_STRING}"' in nsh)
    check("installer names assets with the release identity",
          'OutFile "voidImageViewer-${DISPLAYVERSION}-${TARGETMACHINE}-Setup.exe"' in ins)
    check("VERSION_TYPE is empty for the stable line",
          '#define VERSION_TYPE ""' in vh)

def t_dark_menu_bar():
    viv = read("src/viv.c").decode()
    osh = read("src/os.h").decode()
    osc = read("src/os.c").decode()
    raw_viv = open("src/viv.c", "rb").read().decode("utf-8", errors="replace")
    menubar = open("src/viv_menubar.c", "rb").read().decode("utf-8", errors="replace")
    props = read("voidImageViewer.files.props").decode("utf-8-sig")
    import os

    # os layer: the dpi aware menu font.
    check("os.c resolves SystemParametersInfoForDpi",
          'GetProcAddress(_os_user32_hmodule,"SystemParametersInfoForDpi")' in osc)
    check("os.h exports os_menu_font",
          "int os_menu_font(LOGFONTW *lf);" in osh)
    i = osc.find("int os_menu_font(LOGFONTW *lf)")
    seg = osc[i:osc.find("\n}", i)]
    check("os_menu_font asks at the current window dpi",
          "os_logical_wide))" in seg and "SPI_GETNONCLIENTMETRICS" in seg)
    check("the ForDpi path runs before the plain fallback",
          seg.find("_os_SystemParametersInfoForDpi(SPI_GETNONCLIENTMETRICS") <
          seg.find("SystemParametersInfoW(SPI_GETNONCLIENTMETRICS"))
    check("the menu font is the lfMenuFont metric",
          seg.count("*lf = ncm.lfMenuFont;") == 2)

    # the menu font cache: dpi keyed, dropped on theme and dpi change.
    check("the menu font cache is dpi keyed",
          "_viv_menu_font_dpi != os_logical_wide" in viv)
    i = viv.find("static LRESULT _viv_on_wm_themechanged(")
    seg = viv[i:viv.find("static LRESULT _viv_on_wm_setcursor(", i)]
    check("WM_THEMECHANGED drops the cached menu font",
          "_viv_menu_font_drop();" in seg)
    i = viv.find("static LRESULT _viv_on_wm_dpichanged(")
    seg = viv[i:viv.find("static LRESULT _viv_on_wm_move(", i)]
    check("WM_DPICHANGED drops the menu font with the glyph cache",
          seg.count("_viv_menu_font_drop();") == 1)
    check("WM_DPICHANGED re-reads the top bar layout",
          "_viv_menubar_layout();" in seg)

    # the remake (R77): the top bar is a fully self drawn child window -
    # no frame menu, no owner draw patching, one paint path per theme.
    check("the top bar module exists and registers once in the props",
          os.path.exists("src/viv_menubar.c") and
          props.count('src\\viv_menubar.c" />') == 1 and
          props.count('src\\viv_menubar.h" />') == 1)
    check("the bar registers its own window class",
          '"_VIV_MENUBAR"' in menubar and "os_RegisterClassEx(" in menubar)
    check("the window carries no frame menu anywhere",
          "0,NULL,os_hinstance,NULL);" in raw_viv and "SetMenu(" not in viv)
    check("the frame menu sites became bar show/hide calls",
          viv.count("_viv_menubar_show(") == 6)  # 5 call sites + the module definition (r117: the iconic restyle branch in _viv_update_frame carries its own strip-visibility call)
    check("the layout air is a fixed dpi-scaled constant (both themes)",
          "pad = (4 * os_logical_wide) / 96;" in menubar)
    check("the slots are label extent plus the air",
          "_viv_menubar_item_wide[_viv_menubar_item_count] = size.cx + (pad * 2);" in menubar)
    check("the strip height floors at the remake bar height (28 dip)",
          "min_high = (28 * os_logical_high) / 96;" in menubar)
    check("the layout re-reads the labels from the menu tree (owner drawn rows re-derive)",
          "mii.fMask = MIIM_SUBMENU | MIIM_STRING | MIIM_FTYPE | MIIM_DATA;" in menubar
          and "_viv_menu_row_item_text((void *)mii.dwItemData" in menubar)

    # the painting.
    check("the draw separates the hover and press faces on the tokens",
          "viv_theme_brush(VIV_TK_DOWN)" in menubar and "viv_theme_brush(VIV_TK_HOVER)" in menubar
          and "viv_theme_brush(VIV_TK_FRAME)" in menubar)
    check("the label color rides the theme tokens",
          "viv_theme_color(inactive ? VIV_TK_TEXT2 : VIV_TK_TEXT)" in menubar)
    check("inactive windows dim the label",
          "GetActiveWindow() != _viv_hwnd" in menubar)
    check("the underline follows the system no-accel policy",
          "GetKeyState(VK_MENU)" in menubar and "DT_HIDEPREFIX" in menubar)
    check("the light face falls back to the system colors",
          "case VIV_TK_FACE: return GetSysColor(COLOR_BTNFACE);" in read("src/viv_theme.c").decode("utf-8", errors="replace"))
    check("the erase paints the strip face",
          "FillRect((HDC)wParam,&rect,viv_theme_brush(VIV_TK_FRAME));" in menubar)

    # the popups and the keyboard entry points.
    check("the popups open from the app menu tree",
          "GetSubMenu(_viv_hmenu,itemi)" in menubar and
          "TrackPopupMenuEx(popup,TPM_LEFTALIGN | TPM_RIGHTBUTTON" in menubar and
          "ClientToScreen(_viv_menubar_hwnd,&pt);" in menubar)
    check("the popup state refresh runs on open",
          "_viv_check_menus(_viv_hmenu);" in menubar)
    check("wm_syschar routes the alt mnemonics",
          "_viv_menubar_open_mnemonic((int)wParam)" in viv)
    check("f10 opens the first menu",
          "if (wParam == VK_F10)" in viv and "_viv_menubar_open_first();" in viv)
    check("the popup route refreshes the state too",
          "static LRESULT _viv_on_wm_initmenupopup(" in viv)

    # the old machinery is gone with the seam it lived on; the remake-2
    # round brings the owner draw routes home on the theme tokens (the
    # popup layer is app drawn now, the nc paint route stays retired).
    check("the owner draw menu routes came home on the tokens",
          "_viv_menu_draw_root_item" not in viv and
          "_viv_menu_measure_root_item" not in viv and
          "static LRESULT _viv_on_wm_ncpaint(" not in viv and
          "static LRESULT _viv_on_wm_measureitem(" in viv and
          "static LRESULT _viv_on_wm_menuchar(" in viv and
          "_viv_menu_draw_item((DRAWITEMSTRUCT *)lParam)" in viv)
    check("the pad capture and the tail fill are gone",
          "_viv_menu_bar_capture_pad" not in viv and
          "_viv_menu_bar_nc_fill" not in viv and
          "_viv_menu_bar_fill_gap" not in viv)
    check("the owner draw theme toggle is gone",
          "_viv_menu_bar_theme" not in viv and "_viv_menu_bar_state" not in viv)
    check("the root item tagging is gone",
          "mii.dwItemData = _viv_commands[i].localization_id;" not in viv)

    # the wiring: apply dark repaints the bar; the rebuilds re-lay it out.
    i = viv.find("void _viv_apply_dark_mode(int repaint)")
    i = viv.find("void _viv_apply_dark_mode(int repaint)", i + 10)
    seg = viv[i:viv.find("\nstatic ", i + 10)]
    check("apply dark repaints the remade bar",
          "_viv_menubar_repaint();" in seg)
    # r114: the options page left the layout family (6 -> 5).
    check("the re-layout sites are creation, dpi, theme, the language rebuild and the bar itself",
          viv.count("_viv_menubar_layout();") == 5)  # rc.8: the settings window re-lays the bar after its language switch

    # the chrome palette and the rebar erase hardening.
    check("the chrome brush cache carries the remake menu bar face",
          "VIV_TK_CHROME_MUTED,VIV_TK_FRAME};" in viv and
          "hbrushes[4]" in viv)
    tb_src2 = open("src/viv_toolbar.c", "rb").read().decode("utf-8", errors="replace")
    mb_src = open("src/viv_menubar.c", "rb").read().decode("utf-8", errors="replace")
    check("the strip erase paints the strip face (both strips on the theme cache)",
          "viv_theme_brush(VIV_TK_FRAME)" in mb_src and
          "WM_ERASEBKGND" in tb_src2 and
          "return viv_theme_brush(VIV_TK_CHROME);" in tb_src2)
    check("the light strip face stays the system menu color",
          "case VIV_TK_FRAME: return GetSysColor(COLOR_MENU);" in read("src/viv_theme.c").decode("utf-8", errors="replace"))


def t_dark_layers_round():
    viv = read("src/viv.c").decode("latin-1")
    osc = read("src/os.c").decode("latin-1")
    glyphs = read("src/glyphs.c").decode("latin-1")

    # os: the build number decides whether the dark explorer control
    # classes exist (1903 = 18362).
    check("os.c records the real build number",
          "os_build_number = osvi.dwBuildNumber;" in osc)
    check("os.c gates the dark control classes on build 18362",
          "os_build_number >= 18362" in osc and
          "int os_dark_controls_supported(void)" in osc)
    check("os.h declares the capability probe",
          "int os_dark_controls_supported(void);" in read("src/os.h").decode("latin-1"))

    # menu bar: the remake owns the strip - the old seam (the recorded
    # item union, the one-shot frame repaint retry, the rect clamping)
    # is gone with the owner draw that fed it.
    check("the item rect union recording is gone with the owner draw",
          "_viv_menu_bar_items_rect" not in viv)
    check("the no-item frame repaint retry is gone",
          "_viv_menu_bar_nc_force" not in viv and
          "RedrawWindow(_viv_hwnd,0,0,RDW_FRAME | RDW_INVALIDATE | RDW_UPDATENOW);" not in viv)

    # toolbar: the button states paint with the dark palette.
    tb_src_d = open("src/viv_toolbar.c", "rb").read().decode("utf-8", errors="replace")
    check("the toolbar paints its own hover and press states",
          "_viv_toolbar_hover" in tb_src_d and "_viv_toolbar_pressed" in tb_src_d and
          "SetCapture" in tb_src_d)
    check("the toolbar separators are self drawn hairlines",
          tb_src_d.count("_VIV_TOOLBAR_SEP") >= 1 or "_sep" in tb_src_d or "separator" in tb_src_d)

    # r114: the options tab pins retired with the classic dialogs.
    check("the old tab custom draw is gone",
          "_viv_options_tab_draw" not in viv)

    # the dark dialog owner draw: the push buttons and the combos (the
    # r47 redo: the glyph controls never flip - bs_ownerdraw sits in the
    # bs_typemask field, the flip replaced bs_autocheckbox itself and
    # killed the automatic check state machine. their labels paint
    # through the custom draw notify instead).
    check("the dialog children flip the push buttons and combos to owner draw",
          "| BS_OWNERDRAW);" in viv and
          "| CBS_OWNERDRAWFIXED);" in viv and
          "(!os_dark_controls_supported())" not in viv)
    flip_line = next((l for l in viv.splitlines() if "&& ((type == BS_PUSHBUTTON)" in l and "BS_BITMAP" in l), "")
    check("the glyph controls never flip (the check state machine survives)",
          "BS_AUTOCHECKBOX" not in flip_line and "BS_AUTORADIOBUTTON" not in flip_line and
          "BS_PUSHBUTTON" in flip_line and "BS_DEFPUSHBUTTON" in flip_line)
    check("the glyph control faces paint through the custom draw notify",
          "static INT_PTR _viv_dialog_dark_notify(HWND hwnd,NMHDR *header)" in viv and
          "return CDRF_SKIPDEFAULT;" in viv)
    check("the flip carries the original button type",
          "SetPropW(hwnd,_VIV_DARK_OWNERDRAW_PROP,(HANDLE)(type + 1));" in viv)
    check("the light ui unflips the owner draw fallback",
          "(style & ~((LONG_PTR)BS_TYPEMASK)) | type" in viv and
          "style & ~((LONG_PTR)CBS_OWNERDRAWFIXED)" in viv)
    check("the owner drawn buttons paint the dark palette",
          "_viv_dialog_dark_draw_item(hwnd,(DRAWITEMSTRUCT *)lParam);" in viv and
          "face_brush = _viv_dark_chrome_brush(1);" in viv and
          "DrawTextW(draw_item->hDC,text,-1,&rect,DT_SINGLELINE | DT_CENTER | DT_VCENTER);" in viv)
    check("the owner drawn combos measure and paint",
          "case WM_MEASUREITEM:" in viv and
          "_viv_dialog_dark_combo_item_height" in viv and
          "SendMessageW(draw_item->hwndItem,CB_GETLBTEXT" in viv)
    check("the open dialogs re-theme on a live switch",
          "EnumThreadWindows(GetCurrentThreadId(),_viv_dark_dialogs_enum,0);" in viv and
          "_viv_dark_dialogs_refresh();" in viv)
    check("the color swatch buttons are excluded from the flip",
          "(!(style & (BS_BITMAP | BS_ICON)))" in viv and
          "type == BS_PUSHBUTTON" in viv and
          "type == BS_DEFPUSHBUTTON" in viv)

    # glyphs: float coordinates + the stroke width floor.
    check("the glyph drawing uses the float point api",
          "_glyphs_gdipDrawLinesF" in glyphs and
          "pts[pi].x = ((float)stroke->points[pi].x) * scale;" in glyphs)
    check("no stroke renders below 1.25 device pixels",
          "if (pen_width < 1.25f)" in glyphs)
    check("the magnifier strokes carry visible widths",
          "{0,6,0,&_glyphs_zoom_ring}," in glyphs and
          "{2,5,_glyphs_zoomout_handle}," in glyphs and
          "{2,5,_glyphs_zoomin_plus}" in glyphs)




def t_audit_round14():
    """User-audited fix round: frame delay hardening, save-current-frame,
    uninstall suffix reservation, string terminators, ini size check,
    clipboard ownership. Each guard locks one audited fix in place."""
    viv = read("src/viv.c").decode("latin-1")
    stringc = read("src/string.c").decode("latin-1")
    ini = read("src/ini.c").decode("latin-1")

    # issue 1: the gdi+ frame-delay read must seed size and check both
    # return values, and roll the buffer back when gdi+ did not fill it.
    check("frame delay size is seeded before the query",
          "\t\t\t\t\t\t\t\t\tsize = 0;" in viv)
    check("frame delay size query return is checked",
          "(os_GdipGetPropertyItemSize(image,0x5100,&size) == 0)" in viv and
          "(size >= sizeof(os_PropertyItem_t))" in viv)
    check("frame delay fetch return is checked with rollback",
          "os_GdipGetPropertyItem(image,0x5100,size,frame_delay) != 0" in viv and
          "mem_free(frame_delay);" in viv)

    # issue 4 + pointer validation live in the helper
    check("frame delay helper validates the value pointer",
          re.search(r"(?:static\s+)?UINT _viv_frame_delay_at\(const os_PropertyItem_t \*pd,SIZE_T pd_size,DWORD i\)", viv) is not None and
          "(SIZE_T)(v - (const BYTE *)pd) >= pd_size" in viv)
    check("frame delay helper reuses short delay arrays",
          "i % count" in viv)
    check("both consumers go through the helper",
          viv.count("_viv_frame_delay_at(frame_delay,frame_delay_size,i)") == 2 and
          "frame_delay[0].value" not in viv)

    # issue 2: save-as must save the frame on screen
    check("save-as saves the current frame",
          "os_save_hbitmap(_viv_slot_current.frames[_viv_frame_position].hbitmap,tobuf,format)" in viv and
          "os_save_hbitmap(_viv_slot_current.frames[0].hbitmap" not in viv)

    # issue 3: the uninstall suffix is reserved
    check("uninstall copy reserves the suffix",
          "string_copy_with_bufsize(uninstall_wbuf + 1,STRING_SIZE - 1 - 16,install_path);" in viv)

    # issue 5: failed utf8 conversions still terminate
    check("string_copy_utf8_string terminates on failure",
          "if (!MultiByteToWideChar(CP_UTF8,0,s,-1,buf,STRING_SIZE))" in stringc and
          "buf[0] = 0;" in stringc)

    # issue 6: bufsize 0 must not wrap
    check("string_copy_with_bufsize guards bufsize 0",
          "void string_copy_with_bufsize(wchar_t *d,SIZE_T bufsize,const wchar_t *s)\r\n{\r\n\tuintptr_t size;\r\n\t\r\n\t// bufsize 0 would wrap to SIZE_MAX below.\r\n\tif (!bufsize)\r\n\t{\r\n\t\treturn;\r\n\t}" in stringc)

    # issue 7a: GetFileSize failure must not become an allocation size
    check("ini reads its size through GetFileSizeEx with the 16 mb ceiling",
          "if ((GetFileSizeEx(h,&file_size)) && (file_size.QuadPart > 0) && (file_size.QuadPart <= 0x1000000))" in ini and
          "GetFileSize(h,0);" not in ini)

    # issue 7b: clipboard ownership on failure
    check("clipboard bitmap freed when SetClipboardData fails",
          "if (!SetClipboardData(CF_BITMAP,mem1_hbitmap))" in viv and
          "DeleteObject(mem1_hbitmap);" in viv)

    # the recalled findings stay recalled: no churn was added around them
    check("config write buffer untouched (recalled item)",
          read("src/config.c").decode("latin-1").count("WideCharToMultiByte(CP_UTF8,0,s,-1,(char *)buf,STRING_SIZE*3,0,0);") == 1)



def t_audit_round16():
    """Second user audit round (all 30 source files rescanned): frame
    dimensions count validation, safe_size wiring at every allocation
    arithmetic, dark chrome brush release, shuffle old array release,
    save-as extension reservation, wider shuffle seeds. Each guard locks
    one audited fix in place."""
    viv = read("src/viv.c").decode("latin-1")
    stringc = read("src/string.c").decode("latin-1")
    ini = read("src/ini.c").decode("latin-1")
    utf8c = read("src/utf8.c").decode("latin-1")
    glyphs = read("src/glyphs.c").decode("latin-1")

    # issue 1: the frame dimensions count is validated before set #0 is read
    check("frame dimensions count is validated",
          "if ((count >= 1) && (count <= (SIZE_MAX / sizeof(GUID))))" in viv)
    check("frame dimensions dead guid string removed",
          "StringFromGUID2" not in viv and "strGuid" not in viv)
    check("frame dimensions alloc and free stay inside the guard",
          "DimensionIDs = mem_alloc(sizeof(GUID) * count);" in viv and
          viv.count("mem_free(DimensionIDs);") == 1)

    # issue 2: every allocation arithmetic goes through the safe size helpers
    check("playlist pointer arrays multiply through the helper",
          viv.count("mem_alloc(safe_size_mul_sizeof_pointer((SIZE_T)_viv_playlist_shuffle_allocated))") == 2)
    check("nav pointer array multiplies through the helper",
          "mem_alloc(safe_size_mul_sizeof_pointer((SIZE_T)_viv_nav_item_count))" in viv)
    check("rotate pixel buffers multiply through the helper",
          "mem_alloc(safe_size_mul(safe_size_mul((SIZE_T)bitmap.bmWidth,(SIZE_T)bitmap.bmHeight),sizeof(DWORD)))" in viv and
          "mem_alloc(safe_size_mul(safe_size_mul((SIZE_T)ret_wide,(SIZE_T)ret_high),sizeof(DWORD)))" in viv)
    check("ipc reply and query sizes go through the helpers",
          "mem_alloc(safe_size_add(sizeof(_viv_reply_t),size))" in viv and
          viv.count("safe_size_mul_sizeof_wchar(safe_size_add_one(string_get_length(new_search)))") == 2)
    check("relaunch size and backdrop bits go through the helpers",
          "safe_size_add_one(string_get_length(cwd))" in viv and
          "mem_alloc(safe_size_mul(safe_size_mul((SIZE_T)size,(SIZE_T)size),4))" in viv)
    check("clipboard globals multiply and add through the helpers",
          "GlobalAlloc(GMEM_MOVEABLE,safe_size_add(safe_size_mul_sizeof_wchar(safe_size_add(safe_size_add_one(wlen),1)),sizeof(DROPFILES)))" in viv and
          "GlobalAlloc(GMEM_MOVEABLE,safe_size_mul_sizeof_wchar(safe_size_add_one(wlen)))" in viv)
    check("string allocs multiply through the wchar helper",
          stringc.count("mem_alloc(safe_size_mul_sizeof_wchar(safe_size_add_one(wlen)))") == 2)
    check("utf8 and ini allocations add through the helpers",
          "mem_alloc(safe_size_add_one(size_in_bytes))" in utf8c and
          "mem_alloc(safe_size_add_one(size))" in ini and
          "mem_alloc(safe_size_mul_sizeof_pointer((SIZE_T)count))" in ini)
    check("glyph buffers multiply through the helper",
          "mem_alloc(safe_size_mul((size_t)stride,(size_t)size))" in glyphs and
          "mem_alloc(safe_size_mul(sizeof(_glyphs_point_f_t),(size_t)stroke->point_count))" in glyphs)

    # issue 3: the dark chrome brushes release on shutdown
    check("dark chrome brushes live at file scope and release on kill",
          re.search(r"(?:static\s+)?HBRUSH _viv_dark_chrome_hbrushes\[4\];", viv) is not None and
          "DeleteObject(_viv_dark_chrome_hbrushes[i]);" in viv and
          "static HBRUSH hbrushes[4];" not in viv)

    # issue 4: the initial shuffle releases the previous index array
    check("initial shuffle frees the old index array",
          viv.count("mem_free(_viv_playlist_shuffle_indexes);") == 4)

    # issue 5: the save-as extension always fits
    check("save-as reserves room for the extension",
          "tobuf[(STRING_SIZE - 1) - string_get_length(extension)] = 0;" in viv and
          "string_cat(tobuf,extension);" in viv)

    # low: the shuffle seeds mix both counter halves (two sites)
    check("shuffle seeds mix both counter halves",
          viv.count("srand((unsigned int)(counter.LowPart ^ counter.HighPart));") == 2)

def t_ux_round41():
    """Guards for the modern ux round: the low-cost upstream wishlist items
    (emf/wmf, adaptive size units, recent files, the edit shortcut) plus the
    audit consistency fixes (safe-multiplied webp allocation, the 32-bit
    animation ceiling) and the ux hardening (wallpaper confirm, arrow
    navigation, ctrl+comma options)."""
    viv = read("src/viv.c").decode("latin-1")
    vivh = read("src/viv.h").decode("latin-1")
    webp = read("src/webp.c").decode("latin-1")
    cfgh = read("src/config.h").decode("latin-1")
    cfg = read("src/config.c").decode("latin-1")
    rc = read("res/voidImageViewer.rc").decode("utf-8", errors="replace")
    rh = read("res/resource.h").decode("latin-1")

    # wmf/emf: association table, dialog checkboxes, filter, search, help
    check("emf/wmf live in the association table",
          '\t"emf",' in viv and '\t"wmf",' in viv and
          "LOCALIZATION_ID_ASSOCIATION_DESCRIPTION_EMF," in viv)
    # r114: the emf/wmf checkbox pins retired with the general page.
    check("open dialog filter includes the metafiles",
          "*.webp;*.emf;*.wmf" in viv)
    check("everything default search includes the metafiles",
          viv.count("ext:avif;bmp;dds;gif;hdp;heic;heif;ico;jpeg;jpg;jxr;png;qoi;tif;tiff;wdp;webp;emf;wmf <") == 2)

    # rc.3: the setup association page grows the metafile pair. the exe-side
    # table already carried emf/wmf since 1.1.12, but the page offered ten
    # checkboxes and silently skipped the two on a setup install.
    io2 = read("nsis/InstallOptions2.ini").decode("utf-8", errors="replace")
    io2c = open("nsis/InstallOptions2_Chinese.ini", "rb").read().decode("utf-16")
    ins = read("nsis/installer.nsi").decode()
    check("the association page carries thirteen fields in both languages",
          "NumFields=13" in io2 and "NumFields=13" in io2c)
    check("the metafile checkboxes exist in both languages",
          "[Field 12]" in io2 and "Text=EMF" in io2 and "[Field 13]" in io2 and "Text=WMF" in io2 and
          "[Field 12]" in io2c and "Text=EMF" in io2c and "[Field 13]" in io2c and "Text=WMF" in io2c)
    f12 = io2.split("[Field 12]")[1].split("[Field 13]")[0]
    check("the metafile checkboxes sit in the second column",
          "Left=76" in f12 and "Top=22" in f12 and "State=1" in f12)
    check("the installer forwards the metafile switches",
          'MUI_INSTALLOPTIONS_READ $R0 "InstallOptions2.ini" "Field 12" "State"' in ins and
          'MUI_INSTALLOPTIONS_READ $R0 "InstallOptions2.ini" "Field 13" "State"' in ins and
          '$user_install_options /emf"' in ins and
          '$user_install_options /noemf"' in ins and
          '$user_install_options /wmf"' in ins and
          '$user_install_options /nowmf"' in ins)

    # key table: edit takes ctrl+shift+e, everything-add moves to ctrl+alt+e
    check("edit command has a default shortcut again",
          "{VIV_ID_FILE_EDIT,CONFIG_KEYFLAG_CTRL | CONFIG_KEYFLAG_SHIFT | 'E'}," in viv)
    check("everything-add moved out of ctrl+shift+e",
          "{VIV_ID_FILE_ADD_EVERYTHING_SEARCH,CONFIG_KEYFLAG_CTRL | CONFIG_KEYFLAG_ALT | 'E'}," in viv)
    check("options is ctrl+comma now, not a bare o",
          "{VIV_ID_VIEW_OPTIONS,CONFIG_KEYFLAG_CTRL | VK_OEM_COMMA}," in viv and
          "{VIV_ID_VIEW_OPTIONS,'O'}," not in viv)
    check("wallpaper ctrl+d is back behind a confirmation",
          "{VIV_ID_FILE_SET_DESKTOP_WALLPAPER,CONFIG_KEYFLAG_CTRL | 'D'}," in viv and
          "MB_OKCANCEL | MB_ICONQUESTION" in viv and
          "LOCALIZATION_ID_SET_DESKTOP_WALLPAPER_MESSAGE" in viv)

    # arrows navigate when no slideshow is running
    seg = viv.find("case VIV_ID_SLIDESHOW_RATE_DEC:")
    check("down arrow navigates on a still image",
          seg != -1 and "if (!_viv_is_slideshow)" in viv[seg:seg + 200] and
          "_viv_next(0,1,0,is_key_repeat);" in viv[seg:seg + 600])
    seg = viv.find("case VIV_ID_SLIDESHOW_RATE_INC:")
    check("up arrow navigates on a still image",
          seg != -1 and "_viv_next(1,1,0,is_key_repeat);" in viv[seg:seg + 600])

    # recent files mru
    check("mru ids exist at the enum tail",
          "VIV_ID_FILE_RECENT_CLEAR," in vivh and "VIV_ID_FILE_RECENT_9," in vivh)
    check("mru persists through config",
          re.search(r"#define CONFIG_RECENT_FILE_COUNT\s+10", cfgh) and
          "wchar_t *config_recent_files[CONFIG_RECENT_FILE_COUNT] = {0};" in cfg and
          "recent_filename = string_alloc_utf8(recent_value);" in cfg and
          '_config_write_string(h,key_buf,(i < config_recent_file_count) ? config_recent_files[i] : L"");' in cfg)
    check("mru feeds from the single-file open path",
          "_viv_recent_file_push(full_path_and_filename);" in viv and
          re.search(r"(?:static\s+)?void _viv_recent_file_push\(const wchar_t \*filename\);", viv) is not None)
    check("mru submenu is inserted before exit",
          "InsertMenuItemW(menus[_VIV_MENU_FILE],insert_pos,TRUE,&mii);" in viv and
          "_VIV_MENU_FILE_RECENT," in viv)
    check("the mru no longer rebuilds the whole menu bar",
          "_viv_rebuild_menu" not in viv and
          "_viv_recent_menu_update();" in viv)
    check("stale mru entries drop when the file is gone",
          "_viv_recent_file_remove(recent_index);" in viv)

    # adaptive size units
    check("status bar picks size units by magnitude",
          "size_unit_id = LOCALIZATION_ID_STATUS_BAR_SIZE_BYTES_FORMAT;" in viv and
          "LOCALIZATION_ID_STATUS_BAR_SIZE_KB_FORMAT" in viv and
          "LOCALIZATION_ID_STATUS_BAR_SIZE_MB_FORMAT" in viv and
          "LOCALIZATION_ID_STATUS_BAR_SIZE_GB_FORMAT" in viv and
          "string_cat_utf8(dimension_buf,localization_get_string(size_unit_id));" in viv)

    # webp: the frame-delay allocation multiplies through the safe helpers
    check("webp frame-delay allocation is safe-multiplied",
          "frame_delay_bytes = safe_size_mul((SIZE_T)anim_info.frame_count,sizeof(DWORD));" in webp and
          "frame_delays = (DWORD *)mem_alloc(frame_delay_bytes);" in webp and
          "os_zero_memory(frame_delays,(int)frame_delay_bytes);" in webp)
    check("animated webp canvases honor a separate ceiling",
          "VIV_MAX_ANIMATION_PIXELS" in webp and
          "VIV_MAX_ANIMATION_PIXELS\t25000000" in vivh and
          "VIV_MAX_ANIMATION_PIXELS\t150000000" in vivh)

    # the spelling fix and the dead-variable note
    check("context menu count macro spelled right",
          "_VIV_CONTEXT_MENU_ITEM_COUNT" in viv and
          "_VIV_CONEXT_MENU_ITEM_COUNT" not in viv)
    check("the webp timestamp out-param is documented as required",
          "int timestamp; // out-param of webpanimdecodergetnext" in webp)


def t_audit_round18():
    """Third user audit round: a decode time pixel budget on both loader
    paths (gdi+ and webp, animation and still), the uninstaller checks
    the process image name instead of trusting the window class alone,
    and the debug frame delay print matches its size_t cast. The audit
    also ships a generated anomaly sample set and a windows smoke test
    so a passing suite can be followed by proof on a real machine."""
    viv = read("src/viv.c").decode("latin-1")
    vivh = read("src/viv.h").decode("latin-1")
    webpc = read("src/webp.c").decode("latin-1")

    # issue 1: a single image pixel budget guards both decode paths
    check("pixel budget constant is defined once in viv.h",
          vivh.count("#define VIV_MAX_IMAGE_PIXELS\t100000000") == 1)
    check("gdi+ path checks the budget before any frame allocation",
          "(os_GdipGetImageHeight(image,&first_frame.high) == 0) && (!_viv_pixel_budget_refused(safe_size_mul((SIZE_T)first_frame.wide,(SIZE_T)first_frame.high)))" in viv)
    check("webp animation path checks the canvas budget before decoding",
          "(!_pixel_budget_refused(safe_size_mul((SIZE_T)anim_info.canvas_width,(SIZE_T)anim_info.canvas_height),VIV_MAX_ANIMATION_PIXELS))" in webpc)
    check("webp still path checks the budget before the decode allocates",
          "if (!_pixel_budget_refused(safe_size_mul((SIZE_T)features.width,(SIZE_T)features.height),VIV_MAX_IMAGE_PIXELS))" in webpc)

    # issue 2: the uninstaller verifies the process image name
    check("process image name verification helper exists",
          "static int _viv_is_voidimageviewer_process(DWORD process_id)" in viv)
    check("image name query uses the limited information right",
          "OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION,FALSE,process_id)" in viv and
          "_viv_query_full_process_image_name(process_handle,0,image_name,&image_name_length)" in viv)
    check("the image name query is resolved at run time like every other modern api",
          'GetProcAddress(GetModuleHandleA("kernel32.dll"),"QueryFullProcessImageNameW")' in viv and
          "static BOOL (WINAPI *_viv_query_full_process_image_name)(HANDLE,DWORD,wchar_t *,DWORD *);" in viv)
    check("close existing process refuses foreign windows",
          "if ((process_id) && (!_viv_is_voidimageviewer_process(process_id)))" in viv)

    # low: the debug format spec matches the cast
    check("frame delay debug print casts to unsigned with a matching spec",
          'debug_printf("frame delay size %u\\n",(unsigned int)frame_delay_size);' in viv)

    # delivery: the anomaly samples and the real machine smoke test exist
    samples = read("tests/make_anomaly_samples.py").decode("latin-1")
    smoke = read("tests/smoke_test.ps1").decode("latin-1")
    check("anomaly sample generator writes the audit classes",
          samples.count("def make_") >= 3 and samples.count("emit(") >= 30 and
          "def self_check" in samples)
    check("windows smoke test opens each sample and reports pass or fail",
          "Start-Process" in smoke and "ExitCode" in smoke and
          "make_anomaly_samples" in smoke)



def t_field_fixes_round42():
    """Guards for the field-fix re-release of 1.1.06: the mru insertion bug
    (the submenu block sat inside the command-table loop, so the file menu
    grew one duplicate "recent files" row per table entry), the stale error
    flags after close, the light-mode toolbar chrome, the dark-flip repaint
    hardening (full invalidation plus the immersive color set re-check
    timer), the options tab dark WM_PAINT takeover, and the below-fit zoom
    range for the touch pinch."""
    viv = read("src/viv.c").decode("latin-1")
    vivh = read("src/viv.h").decode("latin-1")

    # --- the mru insertion: brace depth simulation ---
    # the field screenshot showed dozens of duplicate "recent files" rows:
    # the insertion block has to run once per rebuild, after the walk.
    i = viv.find("HMENU _viv_create_menu(void)")
    i = viv.find("HMENU _viv_create_menu(void)", i + 10)
    j = viv.find("\nstatic ", i + 10)
    seg = viv[i:j]
    for_idx = seg.find("for(i=0;i<_VIV_COMMAND_COUNT;i++)")
    check("the create menu walk exists", for_idx != -1)
    loop_open = seg.find("{", for_idx)
    depth = 0
    loop_end = -1
    k = loop_open
    while k < len(seg):
        c = seg[k]
        if c == "{":
            depth += 1
        elif c == "}":
            depth -= 1
            if depth == 0:
                loop_end = k
                break
        k += 1
    check("the command table loop closes inside the function", loop_end != -1)
    mru_idx = seg.find("the recent-files mru submenu is dynamic")
    check("the mru block exists in create menu", mru_idx != -1)
    check("the mru block sits outside the command table loop", mru_idx > loop_end)
    check("the mru insertion runs exactly once per rebuild",
          seg.count("InsertMenuItemW(menus[_VIV_MENU_FILE],insert_pos,TRUE,&mii);") == 1)
    check("the mru popup builds one submenu per rebuild",
          seg.count("recent_menu = _viv_create_recent_menu();") == 1)

    # --- the blank state clears the stale error flags ---
    i = viv.find("void _viv_blank(void)")
    i = viv.find("void _viv_blank(void)", i + 10)
    j = viv.find("\nstatic ", i + 10)
    seg = viv[i:j]
    check("blank clears the file-not-found flag",
          "_viv_file_not_found = 0;" in seg)
    check("blank clears the load-failed flag",
          "_viv_load_failed = 0;" in seg)

    # --- the light-mode toolbar chrome follows the light menu bar ---
    check("the rebar era light brush cache is retired (the tokens own the faces)",
          "_viv_light_chrome_hbrushes" not in viv and
          "_viv_light_chrome_brush" not in viv)
    check("the dark chrome brushes are released on kill",
          viv.count("DeleteObject(_viv_dark_chrome_hbrushes[i]);") == 1)
    check("the light strip faces stay the system menu color",
          "case VIV_TK_FRAME: return GetSysColor(COLOR_MENU);" in read("src/viv_theme.c").decode("utf-8", errors="replace"))

    # --- the dark flip repaint hardening ---
    i = viv.find("void _viv_apply_dark_mode(int repaint)")
    i = viv.find("void _viv_apply_dark_mode(int repaint)", i + 10)
    j = viv.find("\nstatic ", i + 10)
    seg = viv[i:j]
    check("every flip invalidates the whole client",
          0 <= seg.find("InvalidateRect(_viv_hwnd,0,FALSE);") < seg.find("if (repaint)"))
    check("forced repaints sweep the children with the frame (rc.2)",
          "RedrawWindow(_viv_hwnd,0,0,RDW_INVALIDATE | RDW_UPDATENOW | RDW_ALLCHILDREN | RDW_FRAME);" in seg)
    check("the immersive color set gets a delayed re-check",
          "VIV_ID_DARK_RECHECK_TIMER" in vivh and
          "SetTimer(_viv_hwnd,VIV_ID_DARK_RECHECK_TIMER,400,0);" in viv and
          "case VIV_ID_DARK_RECHECK_TIMER:" in viv)
    i = viv.find("case VIV_ID_DARK_RECHECK_TIMER:")
    seg = viv[i:viv.find("case VIV_ID_STATUS_TEMP_TEXT_TIMER:", i)]
    check("the recheck re-reads and re-applies on a change",
          "os_dark_invalidate();" in seg and
          "_viv_apply_dark_mode(1);" in seg and
          "KillTimer(hwnd,VIV_ID_DARK_RECHECK_TIMER);" in seg)

    # --- the below-fit zoom range ---
    check("the shrink ladder constant exists",
          "#define _VIV_ZOOM_SHRINK_STEPS 278" in viv)
    check("the floor helper is wired",
          re.search(r"(?:static\s+)?int _viv_zoom_pos_floor\(void\);", viv) is not None and
          "return -_VIV_ZOOM_SHRINK_STEPS;" in viv and
          "if (!config_allow_shrinking)" in viv)
    check("the clamp respects the below-fit floor",
          "pos_floor = _viv_zoom_pos_floor();" in viv and
          "if (zoom_pos <= pos_floor)" in viv)
    check("negative positions never index the scale table",
          "scale = (_viv_zoom_pos > 0) ? _viv_zoom_scales[_viv_zoom_pos] : (1.0 / _viv_zoom_scales[-_viv_zoom_pos]);" in viv)
    check("the render size keeps a 1px minimum below the fit",
          "if (rw < 1)" in viv and "if (rh < 1)" in viv)
    check("both percent searches reach below the fit",
          viv.count("lo = _viv_zoom_pos_floor();") == 2)


def t_field_fixes_round43():
    """Guards for the second field-fix round on 1.1.06: the mru count limit
    hardening (compile-time id lock, load clamp, trimming push, clamped popup
    builder), the open-stutter fix (the deferred ini save with debounce timer
    plus the live recent-submenu swap instead of the whole-bar rebuild), and
    the options dialog height repair after the emf/wmf checkboxes pushed the
    association list past the template bottom."""
    viv = read("src/viv.c").decode("latin-1")
    vivh = read("src/viv.h").decode("latin-1")
    cfg = read("src/config.c").decode("latin-1")
    rc = read("res/voidImageViewer.rc").decode("utf-8", errors="replace")

    # --- the mru count limit ---
    check("the mru id block and the array count are locked at compile time",
          "typedef char _viv_recent_id_block_matches_count[(VIV_ID_FILE_RECENT_9 - VIV_ID_FILE_RECENT_0 + 1 == CONFIG_RECENT_FILE_COUNT) ? 1 : -1];" in viv)
    check("the load walk clamps the count to the cap",
          "if (config_recent_file_count > CONFIG_RECENT_FILE_COUNT)" in cfg and
          cfg.count("config_recent_file_count = CONFIG_RECENT_FILE_COUNT;") == 1)
    check("the push trims with a while, not a single if",
          "while(config_recent_file_count >= CONFIG_RECENT_FILE_COUNT)" in viv and
          "if (config_recent_file_count == CONFIG_RECENT_FILE_COUNT)" not in viv)
    i = viv.find("HMENU _viv_create_recent_menu(void)")
    i = viv.find("HMENU _viv_create_recent_menu(void)", i + 10)
    j = viv.find("\nstatic ", i + 10)
    seg = viv[i:j]
    check("the popup builder clamps its own loop bound",
          "count = (config_recent_file_count < CONFIG_RECENT_FILE_COUNT) ? config_recent_file_count : CONFIG_RECENT_FILE_COUNT;" in seg and
          "for(i=0;i<count;i++)" in seg and
          "for(i=0;i<config_recent_file_count;i++)" not in seg)

    # --- the deferred ini save ---
    check("the debounce timer id exists",
          "VIV_ID_RECENT_SAVE_TIMER," in vivh)
    check("the save delay is defined",
          "#define _VIV_RECENT_SAVE_DELAY" in viv and
          "2000" in viv[viv.find("#define _VIV_RECENT_SAVE_DELAY"):viv.find("#define _VIV_RECENT_SAVE_DELAY") + 60])
    check("the mru mutations defer instead of writing",
          re.search(r"(?:static\s+)?void _viv_recent_save_defer\(void\)", viv) is not None and
          viv.count("_viv_recent_save_defer();") == 5 and  # r80: the rename helper defers too
          "_viv_recent_save_defer();\r\n\t_viv_recent_menu_update();" in viv)
    i = viv.find("void _viv_recent_file_push(const wchar_t *filename)\r\n{")
    i = viv.find("void _viv_recent_file_push(const wchar_t *filename)\r\n{", i + 10)
    j = viv.find("static void _viv_recent_file_remove", i)
    seg = viv[i:j]
    check("the push path never writes the ini synchronously",
          "config_save_settings" not in seg and
          "_viv_rebuild_menu" not in seg)
    check("the wm_timer case writes once when the burst is over",
          "case VIV_ID_RECENT_SAVE_TIMER:" in viv and
          "KillTimer(hwnd,VIV_ID_RECENT_SAVE_TIMER);" in viv)
    i = viv.find("case VIV_ID_RECENT_SAVE_TIMER:")
    seg = viv[i:viv.find("case VIV_ID_STATUS_TEMP_TEXT_TIMER:", i)]
    check("the timer save is dirty-gated (and never fires for a render export)",
          "if ((!_viv_export_mode) && (_viv_recent_save_dirty))" in seg and
          "config_save_settings(config_appdata);" in seg)
    check("the exit path folds the pending write",
          re.search(r"(?:static\s+)?void _viv_recent_save_fold\(void\)", viv) is not None and
          0 <= viv.find("_viv_recent_save_fold();") < viv.find("config_save_settings(config_appdata);") and
          viv.find("string_copy_with_bufsize(config_last_file,MAX_PATH,_viv_slot_current.fd.cFileName);") != -1)
    check("the session end folds the pending write",
          0 <= viv.find("static LRESULT _viv_on_wm_endsession(") < viv.find("_viv_recent_save_fold();", viv.find("static LRESULT _viv_on_wm_endsession(")) and
          viv.find("string_copy_with_bufsize(config_last_file,MAX_PATH,_viv_slot_current.fd.cFileName);", viv.find("static LRESULT _viv_on_wm_endsession(")) != -1)

    # --- the live submenu swap ---
    check("the whole-bar rebuild function is gone",
          "_viv_rebuild_menu" not in viv)
    i = viv.find("static void _viv_recent_menu_update(void)\r\n{")
    j = viv.find("\nstatic ", i + 10)
    seg = viv[i:j]
    check("the swap finds the file menu by the recent row id",
          "GetMenuState(sub_menu,_VIV_MENU_FILE_RECENT,MF_BYCOMMAND) != (UINT)-1" in seg)
    check("the swap replaces only the popup",
          "GetMenuItemInfoW(file_menu,_VIV_MENU_FILE_RECENT,FALSE,&mii)" in seg and
          "SetMenuItemInfoW(file_menu,_VIV_MENU_FILE_RECENT,FALSE,&mii)" in seg and
          "SetMenu(" not in seg)
    check("the swapped-out popup is destroyed",
          seg.count("DestroyMenu(") == 2)

    # r114: the classic options template geometry pins retired with
    # the templates themselves.


def t_field_fixes_round44():
    """Guards for the third field-fix round on 1.1.06: the canvas mat color
    follows the dark ui (a light custom windowed or backdrop color keeps its
    hue but lands in the dark range instead of glaring out of the chrome -
    the field report read it as the background mat being dead with an image
    open and without one), the follow backdrop matches the fullscreen mat,
    the options ok chain re-tints the caption and reloads for the backdrop,
    and the dark dialogs owner draw their comboboxes on every windows build
    (the explorer dark class has no combo parts, so the language and dark
    mode fields stayed light inside the dark pages)."""
    viv = read("src/viv.c").decode("latin-1")
    osc = read("src/os.c").decode("latin-1")
    osh = read("src/os.h").decode("latin-1")

    # --- the dark-adaptive mat color ---
    check("the shared mat mapper exists with the white fast path",
          viv.count("static COLORREF _viv_dark_mat_color(BYTE r,BYTE g,BYTE b)") == 2 and
          "return RGB(0x20,0x20,0x20);" in viv)
    i = viv.find("static COLORREF _viv_dark_mat_color(BYTE r,BYTE g,BYTE b)")
    i = viv.find("static COLORREF _viv_dark_mat_color(BYTE r,BYTE g,BYTE b)", i + 10)
    j = viv.find("\nstatic ", i + 10)
    seg = viv[i:j]
    check("the mapper measures luminance with the 601 weights",
          "luminance = (((int)r * 30) + ((int)g * 59) + ((int)b * 11)) / 100;" in seg and
          "if (luminance >= 48)" in seg)
    check("light mats keep the hue but land under the chrome face",
          "(BYTE)(((WORD)r * 0x20) / 255)" in seg and
          "(BYTE)(((WORD)g * 0x20) / 255)" in seg and
          "(BYTE)(((WORD)b * 0x20) / 255)" in seg)
    check("an already dark mat passes through unchanged",
          "return RGB(r,g,b);\r\n}" in seg and "else" not in seg)
    check("the windowed background delegates to the mapper in the dark ui",
          "if (_viv_is_dark())\r\n\t{\r\n\t\treturn _viv_dark_mat_color(config_windowed_background_color_r,config_windowed_background_color_g,config_windowed_background_color_b);" in viv)
    check("the light ui still shows the exact configured color",
          "return RGB(config_windowed_background_color_r,config_windowed_background_color_g,config_windowed_background_color_b);" in viv and
          "a customized color always wins" not in viv)

    # --- the backdrop follows the same rules ---
    i = viv.find("static HBRUSH _viv_backdrop_solid_brush(void)")
    # R70: the domain files add forward declarations, so skip the proto.
    i = viv.find("static HBRUSH _viv_backdrop_solid_brush(void)", i + 10)
    j = viv.find("\nstatic ", i + 10)
    seg = viv[i:j] if i != -1 else ""
    check("a custom backdrop color keeps its hue but lands in the dark range",
          "color = _viv_is_dark() ? _viv_dark_mat_color(config_backdrop_color_r,config_backdrop_color_g,config_backdrop_color_b) : RGB(config_backdrop_color_r,config_backdrop_color_g,config_backdrop_color_b);" in seg)
    check("the follow backdrop matches the mat the window paints (fullscreen aware)",
          "color = _viv_is_fullscreen ? RGB(config_fullscreen_background_color_r,config_fullscreen_background_color_g,config_fullscreen_background_color_b) : _viv_windowed_background();" in seg)

    # r114: the options ok chain for the mat color retired with the
    # classic dialogs (the settings color rows answer the same chain).
    # r114: the options ok chain left with the classic dialogs (5 -> 4).
    check("the caption tint follows the mat from startup, the view menu and the settings window",
          viv.count("os_window_modern_chrome(_viv_hwnd,_viv_windowed_background(),viv_theme_color(VIV_TK_TEXT));") == 4)  # rc.8: the settings theme row adds two

    # --- the dark comboboxes on every build ---
    check("comboboxes take the common dialog dark class in the dark ui only",
          "os_dark_combobox_theme(hwnd);" in viv and
          "os_allow_dark_mode_for_window(hwnd,dark ? 1 : 0);" in viv and
          "os_allow_dark_mode_for_window(hwnd,1);" not in viv)
    check("every other control keeps the explorer dark class in the dark ui only",
          viv.count("os_dark_window_theme(hwnd);") == 3 and  # + the rc.4 tree branch
          "os_light_window_theme(hwnd);" in viv and
          "os_light_window_theme(_viv_status_hwnd);" in viv)
    check("no dialog control remains gated on the legacy dark controls check",
          'if ((string_compare(class_name,L"ComboBox") == 0) && (!os_dark_controls_supported()))' not in viv and
          viv.count("(!os_dark_controls_supported())") == 0)
    check("the cfd theme helper lives in os.c and is declared in os.h",
          "int os_dark_combobox_theme(HWND hwnd)" in osc and
          'L"DarkMode_CFD"' in osc and
          "extern int os_dark_combobox_theme(HWND hwnd);" in osh)

def t_field_fixes_round48():
    """Guards for the sixth field-fix round (1.1.10): the dialog return value.

    The 1.1.09 redo painted the labels through the button custom draw and
    returned CDRF_SKIPDEFAULT straight from the dialog procedure - but a
    dialog proc cannot return a notify result: the dialog manager keeps
    the message result in the window data, so the buttons all received
    zero (= CDRF_DODEFAULT) and painted their full default face on top.
    This round routes the result through DWLP_MSGRESULT, draws the whole
    face (the skip now genuinely skips everything), and caches the button
    theme per dialog.
    """
    viv = read("src/viv.c").decode("latin-1")
    osc = read("src/os.c").decode("latin-1")
    osh = read("src/os.h").decode("latin-1")

    # --- the return value contract ---
    check("the dark proc sets dwlp_msgresult and returns true",
          "SetWindowLongPtr(hwnd,DWLP_MSGRESULT,dark_reply);" in viv)
    check("the notify signature carries the dialog hwnd (the theme cache key)",
          "static INT_PTR _viv_dialog_dark_notify(HWND hwnd,NMHDR *header)" in viv)
    check("the plain cdrf return path is gone (the bug itself)",
          "_viv_dialog_dark_notify((NMHDR *)lParam);" not in viv and
          "if (dark_reply != -1)\n                {\n                    return dark_reply;" not in viv)

    # --- the self-drawn glyph ---
    check("the glyph state maps check + item state onto cbs_*/rbs_*",
          "static int _viv_dialog_dark_glyph_state(int type,int check,UINT item_state)" in viv and
          "state = (check == BST_INDETERMINATE) ? OS_BS_MIXEDNORMAL : (check ? OS_BS_CHECKEDNORMAL : OS_BS_UNCHECKEDNORMAL);" in viv and
          "state = check ? OS_RBS_CHECKEDNORMAL : OS_RBS_UNCHECKEDNORMAL;" in viv and
          "state += 3;" in viv and
          "state += 1;" in viv and
          "state += 2;" in viv)
    check("the disabled state wins over hot and pressed",
          viv.find("if (item_state & CDIS_DISABLED)\r\n\t{\r\n\t\tstate += 3;") <
          viv.find("if (item_state & CDIS_HOT)\r\n\t{\r\n\t\tstate += 1;"))
    check("the check state comes from the control message",
          "check = (int)SendMessage(header->hwndFrom,BM_GETCHECK,0,0);" in viv)
    check("the 3state glyph controls join the filter",
          "(type != BS_3STATE) && (type != BS_AUTO3STATE)" in viv)
    check("the glyph rect sits at the left edge, vertically centered",
          "glyph_rect.right = glyph_rect.left + glyph_wide;" in viv and
          "glyph_rect.top = glyph_rect.top + ((glyph_rect.bottom - glyph_rect.top - glyph_high) / 2);" in viv and
          "os_theme_draw_part(theme,custom_draw->hdc,part,state,&glyph_rect);" in viv)
    check("no usable theme falls back to the native painting",
          "return CDRF_DODEFAULT;\r\n\t\t}\r\n\t\t\r\n\t\t// the label text uses the control font" in viv)
    check("the item background takes the dark dialog face",
          "FillRect(custom_draw->hdc,&custom_draw->rc,_viv_dialog_dark_brush());" in viv)

    # --- the focus frame, drawn once ---
    i = viv.find("static INT_PTR _viv_dialog_dark_notify(HWND hwnd,NMHDR *header)")
    # R70: skip the forward declaration, bound on the exported def.
    i = viv.find("static INT_PTR _viv_dialog_dark_notify(HWND hwnd,NMHDR *header)", i + 10) if i != -1 else -1
    notify = viv[i:] if i != -1 else ""
    notify = notify[:notify.find("\nINT_PTR _viv_dialog_dark_proc")]
    check("the focus frame draws exactly once inside the notify",
          notify.count("DrawFocusRect") == 1)

    # --- the cached button theme ---
    check("the theme prop caches the handle on the dialog",
          '#define _VIV_DARK_BUTTON_THEME_PROP L"VIV_DARK_BT"' in viv and
          "static HANDLE _viv_dialog_dark_theme(HWND hwnd)" in viv and
          "SetPropW(hwnd,_VIV_DARK_BUTTON_THEME_PROP,theme);" in viv)
    check("the theme drops on destroy and on theme change",
          "case WM_DESTROY:" in viv and
          "case WM_THEMECHANGED:" in viv and
          viv.count("_viv_dialog_dark_theme_drop(hwnd);") == 2)
    check("the lazy open survives a light-born dialog",
          "theme = os_theme_open_button(hwnd);" in viv)

    # --- the os layer grows the draw channel ---
    check("drawthemebackground joins the dynamic uxtheme table",
          'os_DrawThemeBackground = (void *)GetProcAddress(_os_UxTheme_hmodule,"DrawThemeBackground");' in osc and
          "static OS_DrawThemeBackground_fn _os_DrawThemeBackground = 0;" in osc)
    check("the os wrappers gate on the handle and the import",
          "if ((theme) && (_os_CloseThemeData))" in osc and
          "if ((!theme) || (!_os_GetThemePartSize))" in osc and
          "if ((!theme) || (!_os_DrawThemeBackground))" in osc)
    check("the state ladder carries the full cbs/rbs table",
          "#define OS_BS_CHECKEDDISABLED 8" in osh and
          "#define OS_BS_MIXEDDISABLED 12" in osh and
          "#define OS_RBS_UNCHECKEDDISABLED 4" in osh and
          "#define OS_RBS_CHECKEDDISABLED 8" in osh)

    # --- the touch mask repair ---
    check("the external touch bit replaced the pen bit",
          "return ((sm & 0x80) && (sm & (0x01 | 0x02))) ? 1 : 0;" in osc and
          "(0x01 | 0x04)" not in osc)

    # --- the smoke test runs in ci now ---
    ty = read(".github/workflows/tests.yml").decode("latin-1")
    ry = read(".github/workflows/release.yml").decode("latin-1")
    check("the push workflow opens the anomaly sweep on the windows runner",
          "tests\\smoke_test.ps1" in ty and "Real machine smoke test" in ty)
    check("the release workflow smoke-tests the binary before packaging",
          "tests\\smoke_test.ps1" in ry and "Real machine smoke test" in ry)


def t_field_fixes_round47():
    """Guards for the fifth field-fix round, redone (1.1.09 re-release).

    The withdrawn first 1.1.09 build flipped the checkboxes and the
    radios to owner draw on every machine: bs_ownerdraw occupies the
    bs_typemask field, the flip replaced bs_autocheckbox itself and the
    automatic check state machine died with it (every options checkbox
    frozen in the dark ui, bm_getcheck reading zero). The redo never
    touches a style bit: the labels paint through NM_CUSTOMDRAW (label
    whole face + CDRF_SKIPDEFAULT through DWLP_MSGRESULT - the r48
    return-value repair), the fonts stay unified from the withdrawn
    build, and the about band follows the theme.
    """
    viv = read("src/viv.c").decode("latin-1")
    rc = read("res/voidImageViewer.rc").decode("utf-8", errors="replace")
    osc = read("src/os.c").decode("latin-1")
    osh = read("src/os.h").decode("latin-1")

    # --- the dialog font: one family, one size, one charset everywhere ---
    font_statements = [s.rstrip("\r") for s in re.findall(r'^FONT[^\r\n]*', rc, re.M)]
    # r114: the five classic options templates retired (10 -> 5).
    check("every dialog declares the same font statement (rc.79 + r114: 5 templates)",
          font_statements.count('FONT 9, "Segoe UI", 400, 0, 0') == 5 and
          len(font_statements) == 5,
          "%d font statements" % len(font_statements))
    check("the obsolete DS_FIXEDSYS flag is gone from every template",
          "DS_FIXEDSYS" not in rc)
    check("the legacy MS Shell Dlg mapping is gone",
          "MS Shell Dlg" not in rc)

    # --- the glyph controls never flip to owner draw ---
    check("the flip covers the push buttons only (the state machine survives)",
          "((type == BS_PUSHBUTTON) || (type == BS_DEFPUSHBUTTON)))" in viv and
          "|| (type == BS_AUTOCHECKBOX) || (type == BS_AUTORADIOBUTTON)))" not in viv)
    check("the manual glyph painter is gone (the glyph stays native)",
          "RoundRect(draw_item->hDC,box_rect.left,box_rect.top,box_rect.right,box_rect.bottom,radius,radius);" not in viv and
          "CreateSolidBrush(GetSysColor(COLOR_HOTLIGHT))" not in viv and
          "SetDCBrushColor(draw_item->hDC,RGB(0xE8,0xE8,0xE8));" not in viv)

    # --- the labels paint through the custom draw notify ---
    check("the shared dark proc owns the custom draw notify",
          "static INT_PTR _viv_dialog_dark_notify(HWND hwnd,NMHDR *header)" in viv and
          "_viv_dialog_dark_notify(hwnd,(NMHDR *)lParam);" in viv)
    check("the custom draw result travels through dwlp_msgresult",
          "SetWindowLongPtr(hwnd,DWLP_MSGRESULT,dark_reply);" in viv and
          "return TRUE;" in viv and
          "if (dark_reply != -1)\r\n\t\t\t{\r\n\t\t\t\treturn dark_reply;" not in viv)
    check("the custom draw paints the whole face (label + glyph + background)",
          "CDDS_PREPAINT" in viv and
          "return CDRF_SKIPDEFAULT;" in viv and
          "DrawTextW(custom_draw->hdc,text,-1,&rect,DT_SINGLELINE | DT_LEFT | DT_VCENTER | DT_END_ELLIPSIS);" in viv and
          "os_theme_draw_part(theme,custom_draw->hdc,part,state,&glyph_rect);" in viv and
          "FillRect(custom_draw->hdc,&custom_draw->rc,_viv_dialog_dark_brush());" in viv)
    check("the notify passes the foreign notifications through (the options tree)",
          'string_compare(class_name,L"Button")' in viv and
          "if (header->code != NM_CUSTOMDRAW)" in viv and
          "if (!_viv_is_dark())" in viv)
    check("the glyph state rides with the control state",
          "SendMessage(header->hwndFrom,BM_GETCHECK,0,0);" in viv and
          "BST_INDETERMINATE) ? OS_BS_MIXEDNORMAL" in viv and
          "GetTextExtentPoint32W(custom_draw->hdc,L\"0\",1,&digit);" in viv)
    check("the drawn label takes the control font and the theme color",
          "SendMessage(header->hwndFrom,WM_GETFONT,0,0);" in viv and
          "(style & WS_DISABLED) ? viv_theme_color(VIV_TK_TEXTOFF) : viv_theme_color(VIV_TK_TEXT)" in viv)
    check("the theme metrics and drawing live in the os layer (the dynamic uxtheme pattern)",
          "int os_theme_part_size(HANDLE theme,HDC hdc,int part,int state,int *wide,int *high)" in osc and
          "int os_theme_draw_part(HANDLE theme,HDC hdc,int part,int state,const RECT *rect)" in osc and
          "\"OpenThemeData\"" in osc and
          "\"DrawThemeBackground\"" in osc and
          "extern int os_theme_part_size(HANDLE theme,HDC hdc,int part,int state,int *wide,int *high);" in osh and
          "extern int os_theme_draw_part(HANDLE theme,HDC hdc,int part,int state,const RECT *rect);" in osh and
          "#define OS_BP_CHECKBOX 3" in osh and
          "#define OS_BS_MIXEDNORMAL 9" in osh and
          "#define OS_RBS_CHECKEDNORMAL 5" in osh)

    # --- the state machine survives untouched ---
    # r114: the classic options pages retired; the one live check read is
    # the everything dialog random row (the settings window reads its own
    # switches, the main window reads the drop handler).
    dlg_src = open("src/viv_dialogs.c", "rb").read().decode("utf-8", errors="replace")
    check("the check reads stay live (no manual toggle compensation)",
          viv.count("IsDlgButtonChecked") == 1 and
          "BM_SETCHECK" not in viv and
          dlg_src.count("IsDlgButtonChecked") == 0 and
          dlg_src.count("BN_CLICKED") == 0)  # rc.8: the toolbar and the settings domains post BN_CLICKED to the main window on purpose (5 sites)

    # --- the owner drawn label uses the control font ---
    check("the button draw picks the control font before the text",
          viv.count("font = (HFONT)SendMessage(draw_item->hwndItem,WM_GETFONT,0,0);") == 2 and
          "old_font = font ? (HFONT)SelectObject(draw_item->hDC,font) : 0;" in viv)
    check("the font is restored after the drawn text",
          viv.count("SelectObject(draw_item->hDC,old_font);") >= 2)

    # --- the about band follows the theme (the white strip report) ---
    check("the about bottom band answers the dark chrome in the dark ui",
          "return (INT_PTR)_viv_dark_chrome_brush(1);" in viv and
          "return (INT_PTR)_viv_dark_chrome_brush(2);" in viv and
          "return (INT_PTR)_viv_dark_chrome_brush(0);" in viv and
          "(HBRUSH)(COLOR_BTNSHADOW + 1)" not in viv and
          "(HBRUSH)(COLOR_BTNHIGHLIGHT + 1)" not in viv)
    check("the about light band takes the fixed win11 palette",
          "_viv_about_light_brush(0)" in viv and "_viv_about_light_brush(1)" in viv and
          "RGB(0xEC,0xEC,0xEC)" in viv and "RGB(0xFF,0xFF,0xFF)" in viv)
    check("the about light brushes are released with the chrome brushes",
          "_viv_about_light_hbrushes[i] = 0;" in viv)

    # --- the option inventory survives the rewrite (the r47 baseline) ---
    baseline = {
        # r114: idd_general / idd_options / idd_view / idd_controls retired
        # with the classic options dialogs.
        "IDD_CUSTOM_RATE": "IDCANCEL IDC_CUSTOM_RATE_EDIT IDC_CUSTOM_RATE_STATIC IDC_CUSTOM_RATE_TYPE_COMBO IDOK",
        "IDD_ABOUT": "IDCANCEL IDC_ABOUTBACK IDC_ABOUTCOPYRIGHT IDC_ABOUTEMAIL IDC_ABOUTTITLE IDC_ABOUTVERSION IDC_ABOUTVOIDIMAGEVIEWER IDC_ABOUTWEBSITE IDOK",
        "IDD_RENAME": "IDCANCEL IDC_RENAME_EDIT IDC_RENAME_OLD_EDIT IDOK",
        "IDD_JUMPTO": "IDCANCEL IDC_JUMPTO_EDIT IDC_JUMPTO_LIST IDOK",
        "IDD_EVERYTHING": "IDCANCEL IDC_EVERYTHING_EDIT IDC_SEARCH_EVERYTHING_RANDOM IDOK",
    }
    for dialog, ids in baseline.items():
        m = re.search(r"^" + dialog + r"\s+DIALOGEX.*?\n(.*?)\nEND", rc, re.M | re.S)
        check("the template " + dialog + " is present", m is not None)
        if not m:
            continue
        body = m.group(1)
        missing = [i for i in ids.split() if not re.search(r"[,\s]" + re.escape(i) + r"[,\s]", "," + body + "\n")]
        check("every option control survives in " + dialog + " (the r47 baseline)",
              not missing, "missing: %s" % " ".join(missing))


def t_field_fixes_round46():
    """Guards for the fourth field-fix round (1.1.08): the light dialogs
    stopped painting dark controls (the dark theme classes were applied
    whatever the app theme - the light options page showed black
    comboboxes and black select-all buttons), the dark combo owner draw
    keeps the captured native field height (the metric rebuild drifted
    the field size - the chin under the dark combos), the dark push
    buttons owner draw on every build (the native dark button carries a
    light bottom edge), a refresh with no open file no longer reloads
    (changing the backdrop or the canvas color on the bare program
    showed a load failure at the bottom left), and the view menu gains
    the windowed background color picker next to the renamed
    transparency backdrop submenu."""
    viv = read("src/viv.c").decode("latin-1")
    vh = read("src/viv.h").decode("latin-1")
    osc = read("src/os.c").decode("latin-1")
    osh = read("src/os.h").decode("latin-1")
    en = read("src/localization_en_us.h").decode("latin-1")
    zh = read("src/localization_zh_cn.h").decode("utf-8")

    # --- the theme classes follow the app theme ---
    check("the immersive flag and the theme classes follow the app theme",
          "dark = _viv_is_dark();" in viv and
          "os_allow_dark_mode_for_window(hwnd,dark ? 1 : 0);" in viv and
          "os_allow_dark_mode_for_window(hwnd,1);" not in viv)
    check("the light flip restores the light class on every control",
          viv.count("os_light_window_theme(hwnd);") >= 2 and
          "os_dark_titlebar(hwnd,0);" in viv)
    check("the light theme helper lives in os.c and is declared in os.h",
          "int os_light_window_theme(HWND hwnd)" in osc and
          'L"Explorer"' in osc and
          "extern int os_light_window_theme(HWND hwnd);" in osh)

    # --- the combo field height is captured, not rebuilt ---
    check("the owner draw flip captures the native field height",
          "field_height = (int)SendMessage(hwnd,CB_GETITEMHEIGHT,(WPARAM)-1,0);" in viv and
          "SendMessage(hwnd,CB_SETITEMHEIGHT,(WPARAM)-1,field_height);" in viv and
          "SendMessage(hwnd,CB_SETITEMHEIGHT,(WPARAM)0,field_height);" in viv and
          "SetPropW(hwnd,_VIV_DARK_OWNERDRAW_PROP,(HANDLE)(0x100 + field_height));" in viv)
    check("the flip only runs on a positive captured height",
          "if (field_height > 0)" in viv)
    check("the measure fallback reads the captured height",
          "captured_height = combo_hwnd ? (int)(LONG_PTR)GetPropW(combo_hwnd,_VIV_DARK_OWNERDRAW_PROP) : 0;" in viv and
          "((MEASUREITEMSTRUCT *)lParam)->itemHeight = captured_height - 0x100;" in viv)

    # --- the push buttons owner draw; the glyph controls never flip ---
    # (r47 redo: the withdrawn build flipped the glyph controls too, which
    # replaced bs_autocheckbox in the style and froze every checkbox.)
    check("push buttons owner draw on every build, the glyph controls never",
          "((type == BS_PUSHBUTTON) || (type == BS_DEFPUSHBUTTON)))" in viv and
          "|| (type == BS_AUTOCHECKBOX) || (type == BS_AUTORADIOBUTTON)))" not in viv and
          "(!os_dark_controls_supported())" not in viv)
    check("the bitmap color swatches keep their own painting",
          "(!(style & (BS_BITMAP | BS_ICON)))" in viv)

    # --- a refresh with no file is a blank, not a reload ---
    i = viv.find("void _viv_refresh(void)")
    i = viv.find("void _viv_refresh(void)", i + 10)
    j = viv.find("\nstatic ", i + 10)
    seg = viv[i:j]
    check("the refresh skips the reload when no file is open",
          "if (!fd.cFileName[0])" in seg and
          "_viv_open(&fd,0);" in seg)
    check("the no-file refresh resets the stale error flags",
          "_viv_file_not_found = 0;" in seg and
          "_viv_load_failed = 0;" in seg and
          "return;" in seg)

    # --- the theme change broadcast gets the re-check too ---
    # r114: the options combo left with the classic dialogs (4 -> 3).
    check("the theme broadcasts and the settings combo schedule the one shot re-check",
          viv.count("SetTimer(_viv_hwnd,VIV_ID_DARK_RECHECK_TIMER,400,0);") == 3)  # rc.8: the settings theme row joins

    # --- the view menu canvas color picker + the backdrop rename ---
    check("the view menu canvas color command id exists",
          "VIV_ID_VIEW_WINDOWED_BACKGROUND_COLOR," in vh)
    check("the command table row sits before the backdrop popup",
          "{LOCALIZATION_ID_WINDOWED_BACKGROUND_COLOR_MENU,MF_STRING,_VIV_MENU_VIEW,VIV_ID_VIEW_WINDOWED_BACKGROUND_COLOR}," in viv)
    i = viv.find("case VIV_ID_VIEW_WINDOWED_BACKGROUND_COLOR:")
    check("the menu picker applies the color the same way as the options ok",
          i != -1 and
          "os_window_modern_chrome(_viv_hwnd,_viv_windowed_background(),viv_theme_color(VIV_TK_TEXT));" in viv[i:i+1600] and
          "_viv_refresh();" in viv[i:i+1600])
    check("the backdrop menu is renamed to the transparency backdrop",
          '"&Transparency backdrop", // LOCALIZATION_ID_BACKDROP' in en and
          '"透明背景(&T)", // LOCALIZATION_ID_BACKDROP' in zh and
          '"Follow &window background color", // LOCALIZATION_ID_BACKDROP_FOLLOW' in en and
          '"跟随窗口背景色(&F)", // LOCALIZATION_ID_BACKDROP_FOLLOW' in zh)
    check("the new canvas color strings exist in both languages",
          '"Windowed &background color...", // LOCALIZATION_ID_WINDOWED_BACKGROUND_COLOR_MENU' in en and
          '"窗口背景颜色(&B)...", // LOCALIZATION_ID_WINDOWED_BACKGROUND_COLOR_MENU' in zh)

def t_field_fixes_round49():
    """Guards for the font unification round (1.1.11).

    The 1.1.09 unification hard coded every dialog template to Segoe UI
    9pt, but Segoe UI holds no CJK glyphs: on a Chinese system every
    dialog label rendered through the GDI font-linking fallback with the
    Latin line metrics, while the menu bar (lfMenuFont) and the status
    bar (lfStatusFont) draw the locale face - two type systems in one
    window. This round gives every dialog the system message font at
    the dialog window own DPI, from the shared dark proc's
    WM_INITDIALOG case, before each dialog's own initialization runs.
    """
    viv = read("src/viv.c").decode("latin-1")
    osc = read("src/os.c").decode("latin-1")
    osh = read("src/os.h").decode("latin-1")
    rc = read("res/voidImageViewer.rc").decode("utf-8", errors="replace")

    # --- the os layer: the message font at the window dpi ---
    check("os.h declares the window dpi and the dialog font",
          "int os_window_dpi(HWND hwnd);" in osh and
          "int os_dialog_font(LOGFONTW *lf,HWND hwnd);" in osh)
    check("os.c fills lf from lfMessageFont on both query paths",
          osc.count("*lf = ncm.lfMessageFont;") == 2)
    check("the dialog font queries at the dialog window own dpi",
          "(UINT)os_window_dpi(hwnd)" in osc)
    check("os_window_dpi falls back to the tracked dpi pre-1607",
          "return os_logical_wide;" in osc and
          "return (int)_os_GetDpiForWindow(hwnd);" in osc)

    # --- the viv layer: the ownership moved to the dialogs ---
    # the review fix (round 50) replaced the shared cache with the per
    # dialog ownership: a broadcast cannot delete a face an open dialog
    # is still drawing with. the guards live in t_field_fixes_round50.

    # --- the apply: the dialog itself, then every child ---
    check("the apply sets the font on the dialog window first",
          "SendMessage(hwnd,WM_SETFONT,(WPARAM)font,MAKELPARAM(TRUE,0));" in viv and
          "EnumChildWindows(hwnd,_viv_dialog_font_child,(LPARAM)font);" in viv)
    check("the child callback forwards the setfont broadcast",
          "static BOOL CALLBACK _viv_dialog_font_child(HWND hwnd,LPARAM lParam)" in viv)

    # --- the shared proc wiring: the order is the fix ---
    dark_proc = viv[viv.find("INT_PTR _viv_dialog_dark_proc(HWND hwnd,UINT msg,WPARAM wParam,LPARAM lParam)"):]
    dark_proc = dark_proc[:dark_proc.find("\nstatic void _viv_set_custom_rate")]
    check("the shared proc owns the wm_initdialog case",
          "case WM_INITDIALOG:" in dark_proc and
          "_viv_dialog_apply_font(hwnd);" in dark_proc)
    check("the initdialog case breaks (the -1 pass-through survives)",
          "_viv_dialog_apply_font(hwnd);\r\n\t\t\t\r\n\t\t\tbreak;" in viv)
    check("a dialog dragged across monitors re-reads the font",
          "case WM_DPICHANGED:" in dark_proc and
          dark_proc.count("_viv_dialog_apply_font(hwnd);") == 2)
    # r114: the five classic options dialogs retired (10 -> 5: custom
    # rate, about, rename, jumpto, everything).
    check("the shared dark proc fronts all five dialogs (rc.79 + r114)",
          viv.count("_viv_dialog_dark_proc(hwnd,msg,wParam,lParam);") == 5)

    # --- the template keeps its job: the dlu skeleton ---
    check("five segoe template statements stay (the dlu grid, not the face; rc.79 + r114)",
          rc.count('FONT 9, "Segoe UI"') == 5 and "MS Shell Dlg" not in rc)

def t_field_fixes_round50():
    """Guards for the font lifetime review fix (1.1.11, pre-release).

    The re-review of the font unification caught the shared handle
    dying under live dialogs: the cache deleted the font object on
    every settings broadcast and on every dpi rebuild, but the options
    container and its create dialog pages coexist (and jumpto is a
    create dialog too) - a broadcast arriving while a dialog stood
    open deleted the handle its controls still held, so the next paint
    selected a dead font and the labels fell to the default system
    face. The fix moves the ownership to the dialog: every dialog
    creates its own message font, keeps it in a window prop and
    releases it at ncdestroy, after its children are gone.
    """
    viv = read("src/viv.c").decode("latin-1")

    proc_start = viv.find("INT_PTR _viv_dialog_dark_proc(HWND hwnd,UINT msg,WPARAM wParam,LPARAM lParam)")
    # R70: skip the forward declaration (the first hit is the proto).
    proc_start = viv.find("INT_PTR _viv_dialog_dark_proc(HWND hwnd,UINT msg,WPARAM wParam,LPARAM lParam)", proc_start + 60)
    after = viv.find("\nstatic ", proc_start + 60)
    dark_proc = viv[proc_start:after] if after != -1 else viv[proc_start:proc_start + 20000]

    check("no process global font handle survives (the lifetime follows the dialogs)",
          "_viv_dialog_font_handle" not in viv and
          "_viv_dialog_font_dpi" not in viv and
          "_viv_dialog_font_drop" not in viv)
    check("the apply creates the font per dialog",
          "font = CreateFontIndirectW(&lf);" in viv)
    check("the dialog holds its font in a window prop",
          "SetPropW(hwnd,_VIV_DIALOG_FONT_PROP,(HANDLE)font);" in viv)
    check("a re-apply retires the old face only after the broadcast",
          "old_font = (HFONT)GetPropW(hwnd,_VIV_DIALOG_FONT_PROP);" in viv and
          viv.count("DeleteObject(old_font);") == 1)
    check("the release lands at the dialog ncdestroy (after the children)",
          "case WM_NCDESTROY:" in dark_proc and
          "RemovePropW(hwnd,_VIV_DIALOG_FONT_PROP);" in dark_proc and
          "DeleteObject(font);" in dark_proc)
    check("the settings broadcast touches no font",
          "_viv_dialog_font_drop" not in viv)


def t_field_fixes_round51():
    """Guards for the about title hard code fix (1.1.12, the first round
    after the rollback).

    The 1.1.11 font round recorded the design ("the about title derives
    its larger face from it") but the derivation covered only the
    family: the height stayed the literal 32 - a 96 dpi design point, so
    the 8/3 proportion collapsed to 2/3 at 150% and 1/2 at 200%; the
    site was the only one left on the unsuffixed forms (getobject,
    logfont, createfontindirect - the wide pipeline borrowed from the
    project-level unicode define, while every other font site spells
    the w forms); and the handle was created once and cached for the
    process lifetime, so a dpi change between two about opens kept the
    stale face. The fix derives the title end to end from the live
    dialog font: wm_getfont -> getobjectw into a logfontw -> the height
    scaled by 8/3 -> createfontindirectw, rebuilt at every open, the
    previous handle dying only after the control took the new face, a
    creation failure keeping the previous face drawing.
    """
    viv = read("src/viv.c").decode("latin-1")

    about_start = viv.find(
        "_viv_about_proc(HWND hwnd,UINT msg,WPARAM wParam,LPARAM lParam)\r\n{\r\n")
    about_end = viv.find("\nstatic ", about_start)
    about = viv[about_start:about_end] if about_start != -1 else ""
    check("the about proc block was found for the round51 guards",
          about != "", "")

    check("the literal 32 is gone and the title reads the live dialog font",
          "lf.lfHeight = 32;" not in viv and
          "SendMessage(GetDlgItem(hwnd,IDC_ABOUTTITLE),WM_GETFONT,0,0);" in about and
          "GetObjectW(hfont,sizeof(LOGFONTW),&lf)" in about)
    check("the title height is the 8/3 ratio of the live message font",
          "lf.lfHeight = (lf.lfHeight * 8) / 3;" in about)
    check("the title face is created through the wide pipeline",
          "_viv_about_hfont = CreateFontIndirectW(&lf);" in about and
          "LOGFONT lf;" not in viv and
          "CreateFontIndirect(&lf)" not in viv)
    check("the face is rebuilt at every open (the create-once cache is gone)",
          "if (!_viv_about_hfont)" not in viv)
    check("the previous handle dies only after the new face took the control",
          "old_hfont = _viv_about_hfont;" in about and
          "DeleteObject(old_hfont);" in about)
    check("a creation failure keeps the previous face drawing",
          "_viv_about_hfont = old_hfont;" in about)
    check("the control is never handed a null font",
          "if (_viv_about_hfont)\r\n\t\t\t{\r\n\t\t\t\tSendMessage(GetDlgItem(hwnd,IDC_ABOUTTITLE),WM_SETFONT,(WPARAM)_viv_about_hfont,0);" in about)

    changes = read("Changes.txt").decode("utf-8", errors="replace")
    check("the changelog states the collapse numbers and the derivation",
          "collapsed to 2/3 at 150%" in changes and
          "-12 * 8 / 3 = -32" in changes and
          "the tag and the release stay user-gated" in changes)


def t_about_band_round64():
    """Guards for the about band template round (1.1.12 b42, the dark
    mode temporary-draw report).

    The about dialog painted its band chrome at runtime in WM_PAINT:
    three FillRect passes whose band edges were pixel literals (48 and
    46 at the 96 dpi design point, scaled by the system dpi fraction)
    while the dialog template positions the buttons in dialog units -
    two coordinate systems that only met at 96 dpi. At any other scale
    (and after the b41 round made the dialog font per-dpi) the separator
    lines drifted off the button strip and the band boundaries crossed
    the controls: the rendering the field report called temporary
    drawing that never reached the resource template. The fix moves the
    band chrome into the template: two 1-du line controls and one 24-du
    strip control declared before the buttons (so they sit under them
    in the z order) carry the geometry, the control color replies carry
    the same palettes the wm_paint passes used, WM_CTLCOLORDLG answers
    the dialog face, and the WM_PAINT case is gone - the band follows
    the template grid at every dpi, in both themes, and the banner
    keeps its 1.1.11 face (the black band with the white title in the
    light ui, the shared dark canvas in the dark).
    """
    viv = read("src/viv.c").decode("latin-1")

    about_start = viv.find(
        "_viv_about_proc(HWND hwnd,UINT msg,WPARAM wParam,LPARAM lParam)\r\n{\r\n")
    about_end = viv.find("\nstatic ", about_start)
    about = viv[about_start:about_end] if about_start != -1 else ""
    check("the about proc block was found for the round64 guards",
          about != "", "")

    check("the 96-dpi band literals are gone from the whole code base",
          "(48 * os_logical_high)" not in viv and
          "(46 * os_logical_high)" not in viv)
    check("the about proc no longer repaints the bands in WM_PAINT",
          "case WM_PAINT:" not in about)
    check("the own color replies run before the shared dark handler",
          "own_reply = _viv_about_colors(hwnd,msg,wParam,lParam);" in about)

    colors_start = viv.find(
        "static INT_PTR _viv_about_colors(HWND hwnd,UINT msg,WPARAM wParam,LPARAM lParam)\r\n{\r\n")
    colors_end = viv.find("\r\nstatic ", colors_start)
    colors = viv[colors_start:colors_end] if colors_start != -1 else ""
    check("the about color handler block was found for the round64 guards",
          colors != "", "")
    check("the dialog face answers through the WM_CTLCOLORDLG pipeline",
          "if (msg == WM_CTLCOLORDLG)" in colors)
    check("the banner keeps the 1.1.11 light face (black band, white title)",
          "GetStockObject(BLACK_BRUSH)" in colors and
          "SetTextColor(hdc,RGB(255,255,255));" in colors)
    check("the band controls draw through the color palettes, not the paint passes",
          "IDC_ABOUTLINE1" in colors and
          "IDC_ABOUTLINE2" in colors and
          "IDC_ABOUTBAND" in colors and
          "_viv_dark_chrome_brush(1)" in colors and
          "_viv_dark_chrome_brush(2)" in colors and
          "_viv_dark_chrome_brush(0)" in colors)

    rc = read("res/voidImageViewer.rc").decode("latin-1")
    about_rc_start = rc.find("IDD_ABOUT DIALOGEX")
    about_rc_end = rc.find("\nIDD_RENAME", about_rc_start)
    check("the round64 block ends at the rename template, not the file's end",
          about_rc_end != -1 and (about_rc_end - about_rc_start) < 4000, str(about_rc_end - about_rc_start))
    about_rc = rc[about_rc_start:about_rc_end] if about_rc_start != -1 else ""
    check("the IDD_ABOUT template block was found for the round64 guards",
          about_rc != "", "")
    ok_index = about_rc.find('DEFPUSHBUTTON   "OK",IDOK')
    check("the band geometry lives in the resource template",
          "LTEXT           \"\",IDC_ABOUTLINE1,0,163,228,1" in about_rc and
          "LTEXT           \"\",IDC_ABOUTLINE2,0,164,228,1" in about_rc and
          "LTEXT           \"\",IDC_ABOUTBAND,0,165,228,24" in about_rc)
    check("the band controls are declared before the buttons (z order under them)",
          (about_rc.find("IDC_ABOUTLINE1") != -1) and (ok_index != -1) and
          (about_rc.find("IDC_ABOUTLINE1") < ok_index) and
          (about_rc.find("IDC_ABOUTBAND") < ok_index))

    ids = read("res/resource.h").decode("latin-1")
    check("the band control ids are defined with the next value moved",
          "#define IDC_ABOUTBAND                    1073" in ids and
          "#define IDC_ABOUTLINE1                   1074" in ids and
          "#define IDC_ABOUTLINE2                   1075" in ids and
          "_APS_NEXT_CONTROL_VALUE         1076" in ids)

    version = read("src/version.h").decode("latin-1")
    check("the release candidate line rides the current build (the reentry state round sweeps the pin)",
          "#define VERSION_BUILD 95" in version)

    changes = read("Changes.txt").decode("utf-8", errors="replace")
    check("the changelog states the two coordinate systems and the template move",
          "two coordinate systems" in changes and
          "the resource template" in changes and
          "WM_CTLCOLORDLG" in changes)


def t_white_band_round67():
    """Guards for the white band fix round (1.1.12-rc.2: the theme flip
    repaint sweep, the system menu pad, the toolbar window width and the
    class brushes)."""
    print("the white band fix round (1.1.12-rc.2)")

    viv = read("src/viv.c").decode()
    osc = read("src/os.c").decode()

    # 1. the flip's immediate sweep carries the frame: the official
    #    semantics keep the non client area out of a frameless sweep, so
    #    the wm_ncpaint gap fill never ran and the system light strip
    #    survived the flip (the white band the field caught).
    check("the flip repaint sweep carries rdw_frame",
          "RedrawWindow(_viv_hwnd,0,0,RDW_INVALIDATE | RDW_UPDATENOW | RDW_ALLCHILDREN | RDW_FRAME);" in viv)
    check("the frameless sweep form is gone",
          "RedrawWindow(_viv_hwnd,0,0,RDW_INVALIDATE | RDW_UPDATENOW | RDW_ALLCHILDREN);" not in viv)

    # 2. the flip relayouts the strip windows after the image list rebuild
    #    (the comctl re-metrics on the theme switch): the dpi change and
    #    the language switch run the layout, the flip now does too.
    apply_start = viv.find("void _viv_apply_dark_mode(int repaint)")
    apply_start = viv.find("void _viv_apply_dark_mode(int repaint)", apply_start + 10)
    apply_body = viv[apply_start:viv.find("\nstatic ", apply_start + 10)]
    image_list_at = apply_body.find("_viv_toolbar_build_image_list();")
    on_size_at = apply_body.find("_viv_on_size();")
    check("the flip relayouts the strip through the size sweep",
      viv.find("_viv_toolbar_set_dark(dark);") != -1 and
      0 <= viv.find("_viv_toolbar_set_dark(dark);") < viv.find("InvalidateRect(_viv_hwnd,0,FALSE);", viv.find("_viv_toolbar_set_dark(dark);")))
    # 3. the menu pad: the top bar remake (rc.6) killed the capture - the
    #    field measured a 17px label gap in the light ui and a 94px gap in
    #    the dark ui, the captured pad compounding through the two
    #    measurement systems. one fixed air serves both themes now.
    check("the system menu pad probe is gone with the owner draw",
          "_viv_menu_bar_capture_pad" not in viv and
          "_viv_menu_bar_pad" not in viv)
    check("the fixed air serves both themes",
          "pad = (4 * os_logical_wide) / 96;" in viv)

    # 4. the toolbar window width: tb_getmaxsize (the official total size
    #    of all the visible buttons and separators) leads, the content
    #    scan stays as the pre 5.80 fallback.
    tb_src_w = open("src/viv_toolbar.c", "rb").read().decode("utf-8", errors="replace")
    check("the strip measures its own labels",
          "GetTextExtentPoint32" in tb_src_w and "TB_GETMAXSIZE" not in viv)

    # 5. the class brushes: the register wrapper ignored the brush and the
    #    cursor it was given (every class registered white); the rebar
    #    passes no brush so a bypassing erase never flashes white.
    check("the register wrapper honors the brush and cursor it is given",
          "wcex.hCursor = hCursor;" in osc and
          "wcex.hbrBackground = hbrBackground;" in osc and
          "wcex.hbrBackground = (HBRUSH)(COLOR_WINDOW+1);" not in osc)
    tb_src_r = open("src/viv_toolbar.c", "rb").read().decode("utf-8", errors="replace")
    check("the toolbar registers without a class brush",
          "LoadCursor(NULL,IDC_ARROW)" in tb_src_r and "\"_VIV_TOOLBAR\"" in tb_src_r and
          "(HBRUSH)(COLOR_WINDOW+1)" not in tb_src_r)
    check("the rebar class is retired",
          "_VIV_REBAR" not in viv)

    version = read("src/version.h").decode("latin-1")
    check("the version pins ride the current release (the reentry state round sweeps them)",
          "#define VERSION_BUILD 95" in version and
          '#define VERSION_STRING "1.1.15-rc.16"' in version)

    changes = read("Changes.txt").decode("utf-8", errors="replace")
    check("the changelog states the flip sweep gap and the frame fix",
          "rdw_frame" in changes and
          "a frameless sweep" in changes and
          "wm_ncpaint" in changes)
    check("the changelog states the pad probe and the toolbar window query",
          "the system's own layout" in changes and
          "tb_getmaxsize" in changes)



def t_split_architecture_round69():
    """Guards for the viv.c split architecture decision (the spec carries
    the argument, the discipline is stated, the monolith baseline is
    pinned so the split can only shrink it; the construction plan retired
    with the split it scheduled - git history keeps it)."""
    print("the viv split architecture round (r69)")

    spec = read("docs/architecture/viv-split-spec.md").decode()
    viv = read("src/viv.c").decode("utf-8", errors="replace")

    # 1. the decision is on file: the dialectic, the slice table and the
    #    pure-move discipline exist in the spec.
    check("the split spec exists with the dialectic",
          "## 2. 辩证讨论" in spec and "拆还是不拆" in spec)
    check("the spec pins the pure-move discipline",
          "纯移动纪律" in spec and "禁止" in spec)
    check("the spec carries the 12-slice table",
          spec.count("| 0 |") >= 1 and "viv_state.h" in spec and
          "viv_recent.c" in spec and "viv_view.c" in spec)
    check("the spec pins the monolith baseline",
          "21,129" in spec and "536" in spec and "159" in spec)

    # 2. the monolith baseline, recalibrated in R70 when the one-shot split
    #    landed: the guards read viv.c spliced with the domain modules, so
    #    this now pins the TOTAL code size (the pure move may only add
    #    declarations, never code).
    viv_lines = viv.count("\n") + 1
    check("the spliced code stays inside the growth window",
          viv_lines >= 21130 and viv_lines <= 32800)  # round-102 recalibration: the export module joins the splice (measured 30548); round-108: the cache-set ceiling joins the load domain (measured 31129); round-109: the slot architecture (measured 31138); round-110: the audit response (measured 31170); round-112: the command picker cascade joins the settings domain (measured 31410); round-113: the audit response - the init checks, the ime dissociation, the reply wakeup duty and the offset validator (measured 31613); round-114: the classic options dialogs retire (measured 30523); round-118: the seventh audit response - the clipboard gates, the identity binding, the job snapshot and the interlocked stage (measured 30741); round-120: the memory and cache round (measured 30961); round-121: the gui limits round (measured 31081); round-122: the resume and chain round (measured 31118); round-123: the report fusion round (measured 31221 after the cross-check patch); round-125: the field response round (measured 31647), then the settings remake (measured 31990); round-126: the settings footer round (measured 32740)

    # 3. recalibrated in R70: the state layer and the domain modules now
    #    exist (see t_split_architecture_round70 for the landing guards).
    import os
    check("the state layer landed",
          os.path.exists("src/viv_state.h"))
    check("the recent domain module landed",
          os.path.exists("src/viv_recent.c"))


def t_split_architecture_round70():
    """Guards for the one-shot viv.c split landing (R70): the monolith is
    gone, every domain module exists under its size cap, the state layer
    carries the transition externs, the props register every new compile
    unit exactly once."""
    print("the viv one-shot split landing round (r70)")
    import os
    raw_viv = open("src/viv.c", "rb").read().decode("utf-8", errors="replace")
    viv = read("src/viv.c").decode("utf-8", errors="replace")

    # 1. the residual monolith is under the 5,000 line target
    check("the viv.c residual is under 5,000 lines",
          raw_viv.count("\n") + 1 < 5000)

    # 2. every domain module exists and is under the 3,000 line cap
    domains = ["recent", "playlist", "load", "anim", "render", "chrome",
               "dark", "dialogs", "view", "install", "menu"]
    for d in domains:
        p = f"src/viv_{d}.c"
        ok = os.path.exists(p)
        check(f"the {d} domain module exists", ok)
        if ok:
            n = open(p, "rb").read().decode("utf-8", errors="replace").count("\n") + 1
            cap = 3500 if d == "view" else (3400 if d == "wndproc" else 3000)  # round-128: the rotate ask and the delete retire take the view to 3450 (measured 3450)  # round-125: the successor window machine takes the view home (measured 3380)  # r114: the paste text fallback joins the wndproc domain (measured 3322)  # rc.5: view takes the gesture cluster home (+243); rc.4: wndproc takes the halftone palette and the dpi icons home (+232)
            check(f"viv_{d}.c is under the {cap}-line cap", n < cap, f"({n})")

    # 3. the state layer exists and carries the transition externs
    check("the state layer exists", os.path.exists("src/viv_state.h"))
    if os.path.exists("src/viv_state.h"):
        state = open("src/viv_state.h", "rb").read().decode("utf-8", errors="replace")
        check("the state layer declares the shared state",
              state.count("extern ") >= 100)
        state_lines = state.split("\n")
        inc = next(k for k, l in enumerate(state_lines) if l.startswith('#include "viv.h"'))
        ext = next(k for k, l in enumerate(state_lines) if l.startswith("extern "))
        check("the state layer includes viv.h first", inc < ext)

    # 4. every new module is registered exactly once in the shared props
    props = read("voidImageViewer.files.props").decode("utf-8-sig")
    for d in domains:
        check(f"{d} is registered in the props",
              props.count(f'src\\viv_{d}.c" />') == 1 and
              props.count(f'src\\viv_{d}.h" />') == 1)
    check("the state header is registered in the props",
          props.count('src\\viv_state.h" />') == 1)

    # 5. the pure-move discipline: the spliced view of the code is only
    #    ~200 declaration lines larger than the 21,130 line baseline.
    total = viv.count("\n") + 1
    check("the spliced total stays in the growth window",
          21130 <= total <= 32800, f"({total})")  # round-102 recalibration (the export module measured 30548); round-108: the cache-set block (measured 31129); round-109: the slot architecture (measured 31138); round-110: the audit response (measured 31170); round-112: the command picker cascade joins the settings domain (measured 31410); round-113: the audit response - the init checks, the ime dissociation, the reply wakeup duty and the offset validator (measured 31613); round-114: the classic options dialogs retire (measured 30523); round-118: the seventh audit response (measured 30741); round-120: the memory and cache round (measured 30961); round-121: the gui limits round (measured 31081); round-122: the resume and chain round (measured 31118); round-123: the report fusion round (measured 31221 after the cross-check patch); round-125: the field response round (measured 31647), then the settings remake (measured 31990); round-126: the settings footer round (measured 32740)

    # 6. the fourth CI catch stays guarded: a measurement macro expanded
    #    inside a struct body must see both its #define and the extern it
    #    measures earlier in the header, and the three late exports stay
    #    exported (the one-shot split initially left them static in the
    #    core while chrome and dialogs referenced them).
    state = open("src/viv_state.h", "rb").read().decode("utf-8", errors="replace")
    i_struct = state.find("config_key_t *start[_VIV_COMMAND_COUNT];")
    i_macro = state.find("#define _VIV_COMMAND_COUNT")
    i_extern = state.find("extern _viv_command_t _viv_commands[];")
    check("the command-count macro is defined before the key-list type",
          -1 < i_macro < i_struct)
    check("the command table extern precedes the key-list type",
          -1 < i_extern < i_struct)
    vivc = open("src/viv.c", "rb").read().decode("utf-8", errors="replace")
    for decl, core_def in (
        ("extern wchar_t _viv_status_part_text[_VIV_STATUS_PART_MAX][STRING_SIZE];",
         "wchar_t _viv_status_part_text[_VIV_STATUS_PART_MAX][STRING_SIZE];"),
        ("extern BYTE _viv_is_cursor_shown;",
         "BYTE _viv_is_cursor_shown = 1;"),
        # r114: the _viv_options_page_ids pair retired with the classic
        # dialogs (the page-count define stays - the settings window rides it).
    ):
        name = core_def.split("[")[0].split("=")[0].split(";")[0].strip().split()[-1]
        check("the state layer exports %s" % name, decl in state)
        check("the core defines %s without static" % name,
              core_def in vivc and ("static " + core_def) not in vivc)

    # 8. the fifth CI catch stays guarded: msvc c resolves sizeof on an
    #    unsized extern to zero (warning c4034 - the loops silently stop),
    #    so the five count macros carry literals, each pinned to its real
    #    table by a c_assert in the defining unit; and the bare-cr join
    #    that swallowed the light window theme declaration is gone.
    for macro in ("_VIV_COMMAND_COUNT", "_VIV_ANIMATION_RATE_MAX",
                  "_VIV_SLIDESHOW_RATE_PRESET_COUNT", "_VIV_OPTIONS_PAGE_COUNT",
                  "_VIV_ASSOCIATION_COUNT"):
        line = next((l for l in state.split("\n") if l.startswith("#define " + macro)), "")
        check("the %s macro is a literal, not a sizeof measurement" % macro,
              line != "" and "sizeof" not in line)
    vivview = open("src/viv_view.c", "rb").read().decode("utf-8", errors="replace")
    vivdlg = open("src/viv_dialogs.c", "rb").read().decode("utf-8", errors="replace")
    for unit, assertion in (
        ("viv.c", "typedef char _viv_commands_count_assert[(sizeof(_viv_commands) / sizeof(_viv_command_t) == _VIV_COMMAND_COUNT) ? 1 : -1];"),
        ("viv.c", "typedef char _viv_animation_rates_count_assert[(sizeof(_viv_animation_rates) / sizeof(float) == _VIV_ANIMATION_RATE_MAX) ? 1 : -1];"),
        ("viv.c", "typedef char _viv_association_extensions_count_assert[(sizeof(_viv_association_extensions) / sizeof(_viv_association_extensions[0]) == _VIV_ASSOCIATION_COUNT) ? 1 : -1];"),
        ("viv_view.c", "typedef char _viv_slideshow_rate_presets_count_assert[(sizeof(_viv_slideshow_rate_presets) / sizeof(WORD) == _VIV_SLIDESHOW_RATE_PRESET_COUNT) ? 1 : -1];"),
        # r114: the viv_dialogs.c page-ids assert retired with the classic
        # dialogs (the page-count macro stays - the settings window rides it).
    ):
        text = {"viv.c": vivc, "viv_view.c": vivview, "viv_dialogs.c": vivdlg}[unit]
        check("the %s count is pinned by a negative-subscript typedef assert" % unit, assertion in text)
    osh = open("src/os.h", "rb").read().decode("utf-8", errors="replace")
    check("os.h declares the light window theme on its own line",
          "extern int os_light_window_theme(HWND hwnd);" in osh)
    for f in ("src/os.h", "src/os.c"):
        data = open(f, "rb").read()
        check("no bare-cr line joins left in %s" % f,
              len(re.findall(rb"\r(?!\n)", data)) == 0)



def t_structure_round76():
    """Guards for the R76 structure round: the splice guard finally carries
    the file manifest (the glob could silently absorb a new domain and
    silently drop a renamed one - the growth window was measured against
    an invisible list), the window procedure is a domain module with the
    case bodies as handlers, and the gesture cluster is home in view."""
    print("the structure round (1.1.12-rc.5)")
    import glob

    # 1. THE FILE MANIFEST: the splice read("src/viv.c") is viv.c + every
    #    src/viv_*.c in dictionary order + viv_state.h. pinned exactly, so
    #    a new domain must be added here (and to the props) on purpose.
    actual = sorted(os.path.basename(p) for p in glob.glob("src/viv_*.c"))
    expected = ["viv_anim.c", "viv_chrome.c", "viv_dark.c", "viv_dialogs.c",
                "viv_export.c", "viv_install.c", "viv_load.c", "viv_menu.c",
                "viv_menubar.c", "viv_msgbox.c", "viv_playlist.c",
                "viv_recent.c", "viv_render.c", "viv_selfshot.c",
                "viv_settings.c", "viv_theme.c", "viv_toolbar.c",
                "viv_view.c", "viv_wndproc.c"]
    check("the splice manifest is the pinned 19-domain list",
          actual == expected, f"({actual})")  # round-102: the export module joins
    check("the state layer is the splice tail",
          os.path.exists("src/viv_state.h"))

    # 2. the wndproc domain: exists, sized, registered exactly once.
    wnd = open("src/viv_wndproc.c", "rb").read().decode("utf-8", errors="replace")
    check("the wndproc domain exists", "static LRESULT _viv_on_wm_nchittest(" in wnd)
    check("the wndproc domain is under the 3,400 line cap",
          wnd.count("\n") + 1 < 3400, f"({wnd.count(chr(10)) + 1})")  # rc.4: the halftone palette and the dpi icons; rc.10: the renderer-fallback notice joins the paint path; round-113: the reply wakeup duty's drain-side repost and the copydata null-buffer belt (measured 3217); round-114: the paste text fallback (measured 3322)
    props = read("voidImageViewer.files.props").decode("utf-8-sig")
    check("viv_wndproc.c is registered in the props",
          props.count('src\\viv_wndproc.c" />') == 1)
    check("viv_wndproc.h is registered in the props",
          props.count('src\\viv_wndproc.h" />') == 1)
    check("the rc.6 menubar domain is registered in the props",
          props.count('src\\viv_menubar.c" />') == 1 and
          props.count('src\\viv_menubar.h" />') == 1)

    # 3. the dispatch: _viv_proc is a slim switch again (the 2,208-line
    #    monolith case bodies are now per-message handlers).
    i = wnd.find("LRESULT CALLBACK _viv_proc(")
    disp = wnd[i:]
    check("the dispatch is under 130 lines",
          disp.count("\n") < 130, f"({disp.count(chr(10))})")
    handlers = wnd.count("\nstatic LRESULT _viv_on_")
    # rc.6: wm_syschar, wm_syskeydown, wm_syskeyup and wm_initmenupopup
    # joined the dispatch, wm_measureitem and wm_ncpaint left with the
    # owner draw menu machinery they existed for.
    check("the 52 per-message handlers exist", handlers == 52, f"({handlers})")  # halftone round: wm_querynewpalette, wm_palettechanged and wm_displaychange join the dispatch
    check("every dispatch case returns its handler",
          disp.count("\n\t\t\treturn _viv_on_") == 52)

    # 4. the pure-move discipline held: the moved case bodies kept their
    #    bytes, only the case-exit breaks became DefWindowProc returns.
    check("the case exits are explicit DefWindowProc returns",
          wnd.count("return DefWindowProc(hwnd,msg,wParam,lParam);") == 44)  # halftone round: the no-palette querynewpalette exit joins

    # 5. the gesture cluster is home in the view domain (chrome no longer
    #    owns it): the three touch entry points + the engine state.
    view = open("src/viv_view.c", "rb").read().decode("utf-8", errors="replace")
    chrome = open("src/viv_chrome.c", "rb").read().decode("utf-8", errors="replace")
    check("the gesture engine lives in view",
          "int _viv_on_gesture(HWND hwnd,void *gesture_info_handle)\r\n{" in view)
    check("the touch click probe lives in view",
          "int _viv_is_touch_click(void)\r\n{" in view)
    check("the touch double click lives in view",
          "void _viv_touch_double_click(void)\r\n{" in view)
    check("the gesture state moved with the engine",
          "// touch gesture state." in view)
    check("chrome no longer references the gesture cluster",
          "_viv_on_gesture" not in chrome and "_viv_touch_double_click" not in chrome)
    chh = open("src/viv_chrome.h", "rb").read().decode("utf-8", errors="replace")
    check("the chrome header no longer claims gestures",
          "gestures" not in chh.split("\n")[22])
    vh = open("src/viv_view.h", "rb").read().decode("utf-8", errors="replace")
    check("the view header exports the touch entry points",
          "int _viv_on_gesture(HWND hwnd,void *gesture_info_handle);" in vh)

    # 6. the two shared symbols the split promoted to the state layer.
    state = open("src/viv_state.h", "rb").read().decode("utf-8", errors="replace")
    vivc = open("src/viv.c", "rb").read().decode("utf-8", errors="replace")
    check("the background brush is externed and released by kill",
          "extern HBRUSH _viv_background_hbrush;" in state and
          vivc.count("_viv_background_hbrush") == 3)
    check("the command line processor is declared in the state layer",
          "void _viv_process_command_line(wchar_t *cl);" in state and
          "\nvoid _viv_process_command_line(wchar_t *cl)" in vivc)
    check("the moved statics left the core",
          "_viv_mdoing_x" not in vivc and "_viv_is_animation_paint" not in vivc and
          "_viv_drop_files" not in vivc)
    check("the core registers the wndproc through its header",
          '#include "viv_wndproc.h"' in vivc and
          "os_RegisterClassEx(" in vivc)

    # 7. the version moved to rc.5 / build 47.
    version = read("src/version.h").decode()
    check("the version is 1.1.15-rc.16 build 95",
          '#define VERSION_BUILD 95' in version and
          '#define VERSION_STRING "1.1.15-rc.16"' in version)
    changes = read("Changes.txt").decode("utf-8", errors="replace")
    check("the changelog states the structure round",
          "the structure round" in changes and
          "42 static per-message handlers" in changes and
          "the manifest is pinned now" in changes)

def t_theme_race_round72():
    """Guards for the theme race self-heal round (1.1.12-rc.4: the white
    band after the options dialog and the theme switch, the options tree
    contrast, the dark erase)."""
    print("the theme race self-heal round (1.1.12-rc.4)")

    viv = read("src/viv.c").decode()
    dialogs = read("src/viv_dialogs.c").decode()
    dark = read("src/viv_dark.c").decode()

    # 1. the recheck timer re-applies unconditionally: the flip gate let the
    #    system asynchronous light repaint survive while the app level dark
    #    answer never flipped.
    recheck_at = viv.find("case VIV_ID_DARK_RECHECK_TIMER:")
    recheck_body = viv[recheck_at:viv.find("break;", recheck_at)]
    check("the recheck timer applies without the flip gate",
          "_viv_apply_dark_mode(1);" in recheck_body and
          "was_dark != is_dark" not in recheck_body)

    # 2. wm_themechanged re-applies unconditionally (the system re-themed
    #    the comctl classes and the frame: the per window dark state must be
    #    re-asserted even when the answer did not flip).
    theme_at = viv.find("static LRESULT _viv_on_wm_themechanged(")
    theme_body = viv[theme_at:viv.find("static LRESULT _viv_on_wm_setcursor(", theme_at)]
    check("wm_themechanged applies without the flip gate",
          "_viv_apply_dark_mode(1);" in theme_body and
          "was_dark != is_dark" not in theme_body)

    # 3. the main window erases with the dark chrome face in the dark ui (a
    #    bypassing paint must not flash the light class brush).
    check("the dark ui erases with the chrome face",
          "// the dark ui erases with the chrome face" in viv and
          "FillRect((HDC)wParam,&rect,_viv_dark_chrome_brush(0));" in viv)

    # r114: the options combo assert pin retired with the classic dialogs.

    # r114: the options dialog creation-race self heal retired with the
    # dialog (the settings window runs its own dark pass).

    # 6. the tree pins its face and label colors in the dark dialog children
    #    walk (the theme gray read as low contrast), with the system default
    #    on the light flip.
    walk_at = dark.find("static BOOL CALLBACK _viv_dark_dialog_children(HWND hwnd,LPARAM lParam)\r\n{")
    walk = dark[walk_at:dark.find("\nstatic ", walk_at + 10)]
    check("the children walk pins the tree colors",
          'string_compare(class_name,L"SysTreeView32") == 0' in walk and
          "TVM_SETBKCOLOR,0,dark ? viv_theme_color(VIV_TK_FACE) : (COLORREF)0xFFFFFFFF" in walk and
          "TVM_SETTEXTCOLOR,0,dark ? viv_theme_color(VIV_TK_TEXT) : (COLORREF)0xFFFFFFFF" in walk)

    version = read("src/version.h").decode("latin-1")
    check("the version pins ride the current release (the reentry state round sweeps them)",
          "#define VERSION_BUILD 95" in version and
          '#define VERSION_STRING "1.1.15-rc.16"' in version)

    changes = read("Changes.txt").decode("utf-8", errors="replace")
    check("the changelog states the race and the self heal",
          "the theme race self-heal round" in changes and
          "was_dark != is_dark" in changes)



# ---------------------------------------------------------------------------
# the carpet repair round (R76): the seven-subagent adversarial review of the
# user's rc.8-rc.10 remake, followed by the fix round. every pin here anchors
# a fix that shipped through a green suite (the review found them because the
# guards did not reach the new code).
# ---------------------------------------------------------------------------
def t_carpet_repair_round76():
    chrome = open("src/viv_chrome.c", "rb").read().decode("utf-8", errors="replace")
    menubar = open("src/viv_menubar.c", "rb").read().decode("utf-8", errors="replace")
    menu = open("src/viv_menu.c", "rb").read().decode("utf-8", errors="replace")
    toolbar = open("src/viv_toolbar.c", "rb").read().decode("utf-8", errors="replace")
    zoomui = open("src/zoomui.c", "rb").read().decode("utf-8", errors="replace")
    render = open("src/viv_render.c", "rb").read().decode("utf-8", errors="replace")
    view = open("src/viv_view.c", "rb").read().decode("utf-8", errors="replace")
    wnd = open("src/viv_wndproc.c", "rb").read().decode("utf-8", errors="replace")
    osc = open("src/os.c", "rb").read().decode("utf-8", errors="replace")
    msgbox = open("src/viv_msgbox.c", "rb").read().decode("utf-8", errors="replace")
    settings = open("src/viv_settings.c", "rb").read().decode("utf-8", errors="replace")
    recent = open("src/viv_recent.c", "rb").read().decode("utf-8", errors="replace")
    glyphs = open("src/glyphs.c", "rb").read().decode("utf-8", errors="replace")
    mf = read("res/voidImageViewer.Manifest").decode("utf-8", errors="replace")

    check("the menu domain exports the row label resolver",
          "void _viv_menu_row_item_text(void *row,wchar_t *wbuf);" in read("src/viv_menu.h").decode("utf-8", errors="replace"))

    check("the status date pane uses the localized system formatters",
          "GetDateFormatW(LOCALE_USER_DEFAULT,DATE_SHORTDATE" in chrome
          and "GetTimeFormatW(LOCALE_USER_DEFAULT,TIME_NOSECONDS" in chrome)
    check("the date pane never feeds a wide literal to the narrow parser",
          'string_printf(date_buf,L' not in chrome)

    check("the view top helper exists and the header exports it",
          "int _viv_get_view_top(void)" in chrome
          and "int _viv_get_view_top(void);" in read("src/viv_chrome.h").decode("utf-8", errors="replace"))
    check("the size math reserves the top stack, not a bottom toolbar",
          render.count("- _viv_get_status_high() - _viv_get_view_top()") >= 4
          and view.count("- _viv_get_status_high() - _viv_get_view_top()") >= 5
          and "- _viv_get_status_high() - _viv_get_view_top();" in wnd)
    check("the paint blits land in client coordinates (the origin rides every dst y)",
          wnd.count("ry + view_top") >= 5)
    check("the mouse anchors turn viewport relative",
          "pt.y -= _viv_get_view_top();" in view
          and "cursor_y = pt.y - _viv_get_view_top();" in view)
    check("the pill lands at the viewport bottom",
          "y = _viv_get_view_top() + high - container_high - _zoomui_margin;" in zoomui)

    check("the toolbar play slot never forces the fullscreen jump",
          "VIV_ID_SLIDESHOW_PLAY_ONLY" in toolbar
          and "VIV_ID_SLIDESHOW_PAUSE_ONLY" in toolbar
          and "VIV_ID_ANIMATION_PLAY_PAUSE" in toolbar)

    check("the apply path flushes the token cache",
          chrome.count("viv_theme_refresh();") >= 1)
    check("the classic palette change repaints (wm_syscolorchange)",
          "_viv_on_wm_syscolorchange" in wnd)

    check("os.c resolves the vista+ system aware tier",
          'GetProcAddress(_os_user32_hmodule,"SetProcessDPIAware")' in osc)
    check("the manifest declares dpi awareness twice (2005 + 2016)",
          "<dpiAware>true</dpiAware>" in mf
          and "<dpiAwareness>PerMonitorV2, PerMonitor</dpiAwareness>" in mf)

    check("the themed box maps the close button and propagates quit",
          "IDOK : IDCANCEL" in msgbox
          and "PostQuitMessage((int)msg.wParam);" in msgbox)

    check("the capture commit keeps the unchanged edit binding",
          "(!_viv_settings_capture_edit) || (old_key != (int)_viv_settings_capture_key)" in settings)
    check("modifier combos stay capturable (ctrl+return rebindable)",
          "GetKeyState(VK_CONTROL) & 0x8000" in settings)

    check("the mru swap defers while a menu is up and flushes on close",
          "_viv_recent_menu_pending" in recent
          and "_viv_recent_menu_flush();" in menubar
          and "_viv_in_popup_menu = 1;" in menubar)

    check("the pill percent reads the render percent",
          "return _viv_zoom_percent();" in zoomui)

    check("the menubar pump breaks on the error return",
          "GetMessage(&msg,0,WM_MOUSEFIRST,WM_MOUSELAST) <= 0" in menubar)

    check("the radio dot painter exists",
          "_viv_menu_draw_dot" in menu)
    check("the mnemonic scan skips grayed rows",
          "MIIM_DATA | MIIM_FTYPE | MIIM_STATE" in menu)

    check("the owner draw delete notification has a handler",
          "_viv_on_wm_deleteitem" in wnd)

    # the comctl status bar sends its pane draws with CtlType == ODT_MENU:
    # the field is uninitialized garbage on that path, so the pane branch
    # must route by the control id BEFORE the menu branch, or the pane
    # index gets dereferenced as a menu row.
    check("the status pane draws route before the menu branch (the comctl ctltype garbage)",
          0 <= 0 <= wnd.find("wParam == VIV_ID_STATUS") < wnd.find("_viv_menu_draw_item((DRAWITEMSTRUCT *)lParam)"))

    check("a failed glyph build never poisons the cache",
          "if (!icon)" in glyphs
          and "return 0;" in glyphs.split("HICON glyphs_icon")[1][:1200])

    check("the toolbar rides the theme cache (no private palette)",
          "_viv_toolbar_dark_face" not in toolbar
          and "return viv_theme_brush(VIV_TK_CHROME);" in toolbar)
    check("the strip background drag still moves the window",
          "_viv_start_move_window();" in toolbar)

def t_platform_guardrails():
    """Keep loader/build compatibility fixes from being regressed."""
    manifest = read("res/voidImageViewer.Manifest")
    wndproc = read("src/viv_wndproc.c")
    vc2019 = read("vs2019/voidImageViewer.vcxproj")
    vc2026 = read("vs2026/voidImageViewer.vcxproj")
    check("manifest uses the official Windows 8.1 compatibility GUID",
          b"{1f676c76-80e1-4239-95bb-83d0f6d0da78}" in manifest
          and b"{1f676c76-3e4d-4f03-ac22-34155b000000}" not in manifest)
    check("DPI change tolerates a missing suggested rectangle",
          b"if (suggested_rect)" in wndproc)
    check("Win32 projects do not depend on the absent UnicoWS library",
          b"UnicoWS.lib" not in vc2019 and b"UnicoWS.lib" not in vc2026)



def t_field_repair_round78():
    """The non-win11 field round: the play face, the first-click zoom and
    the non-win11 visual fallbacks (the windows 10 field report)."""
    toolbar = read("src/viv_toolbar.c")
    anim = read("src/viv_anim.c")
    view = read("src/viv_view.c")
    theme = read("src/viv_theme.c")
    menu = read("src/viv_menu.c")
    menuh = read("src/viv_menu.h")
    settings = read("src/viv_settings.c")
    msgbox = read("src/viv_msgbox.c")
    zoomui = read("src/zoomui.c")
    osc = read("src/os.c")
    osh = read("src/os.h")
    wndproc = read("src/viv_wndproc.c")
    changes = read("Changes.txt").decode("utf-8", errors="replace")

    # the play face: the animation clock only counts when frames exist.
    check("the play face gates the animation clock on the frame count",
          b"((_viv_slot_current.frame_count > 1) && (_viv_animation_play))" in toolbar)
    check("the animation clock changes notify the toolbar and the ontop",
          anim.count(b"_viv_toolbar_update_buttons();") >= 3 and
          anim.count(b"_viv_update_ontop();") >= 3)
    check("the frame home and end jumps notify too",
          view.count(b"_viv_toolbar_update_buttons();") >= 2)
    check("the destroy resets the play latch",
          b"_viv_toolbar_playing = 0;" in toolbar)

    # the first-click zoom: direction-strict snap + the true floor.
    check("the zoom out snap rounds strictly down",
          b"target = ((percent - 1) / 10) * 10;" in view)
    check("the zoom in snap rounds strictly up",
          b"target = ((percent / 10) * 10) + 10;" in view)
    check("the zoom out floor gate reads the true ladder floor",
          b"_viv_zoom_pos <= _viv_zoom_pos_floor()" in view)
    check("the old dead pos==0 gate is gone",
          b"(_viv_zoom_pos == 0))" not in view)
    check("the sparse-zone single-position fallback exists",
          b"step one position" in view and
          b"old_zoom_pos + ((force > 0) ? 1 : -1)" in view)

    # the non-win11 visuals: the platform probe and the fallbacks.
    check("the dwm chrome calls are the platform probe",
          b"int os_window_modern_chrome" in osh and
          b"int os_menu_modern_chrome" in osh and
          b"int os_is_win11(void);" in osh)
    check("the probe latches the answer",
          b"_os_win11_chrome = 2;" in osc and
          b"int os_is_win11(void)" in osc)
    check("the settings window drops a shadow off win11",
          b"CS_DROPSHADOW" in settings and b"os_is_win11()" in settings)
    check("the settings dropdowns ride the owner drawn menu rows",
          b"_VIV_MENU_POOL_SETTINGS" in settings and
          b"MFT_OWNERDRAW | MFT_RADIOCHECK" in settings and
          b"_viv_menu_row_set_text(" in settings)
    check("the settings dropdowns dropped tpm_nonotify",
          b"TPM_NONOTIFY" not in settings)
    check("the settings owner forwards the menu messages",
          b"case WM_INITMENUPOPUP:" in settings and
          b"case WM_DRAWITEM:" in settings and
          b"case WM_MEASUREITEM:" in settings and
          b"case WM_DELETEITEM:" in settings)
    check("the popup theming is shared with the main window",
          b"_viv_menu_popup_theme" in menuh and
          b"void _viv_menu_popup_theme(void)" in menu and
          b"_viv_menu_popup_theme();" in wndproc and
          b"_viv_menu_popup_theme();" in settings)
    check("the menu rows can carry an ad hoc label",
          b"#define _VIV_MENU_DRAW_TEXT" in menuh and
          b"case _VIV_MENU_DRAW_TEXT:" in menu and
          b"_viv_settings_draw_pool" in menu)
    check("the msgbox buttons take the pill radius",
          b"RoundRect" in msgbox and b"pill geometry" in msgbox)
    check("the light hover token has real contrast",
          b"RGB(0xDC,0xDC,0xDC)" in theme)
    check("the light nav face layers against the content face",
          b"RGB(0xE4,0xE4,0xE4)" in theme)
    check("the light strip ties to the system menu color",
          b"case VIV_TK_CHROME: return GetSysColor(COLOR_MENU);" in theme)
    check("the zoom pill keeps its row still under a press",
          b"_zoomui_pct_recenter" in zoomui)

    # the changelog states the round (phrases can wrap across the 70
    # column discipline - normalize the whitespace before matching).
    flat = " ".join(changes.split())
    check("the changelog states the non-win11 field round",
          "the non-win11 field round" in flat and
          "gates the clock on the frame count" in flat and
          "the snap is direction strict" in flat and
          "the drop shadow style off win11" in flat and
          "the same painter, radio dots" in flat)

# ---------------------------------------------------------------------------
# 1.1.12-rc.79: the zoom pane editor round. the field report: "the zoom
# percent in the corner needs two clicks to open, and the click shows a
# select box". the pane's drag anchor (inherited from upstream) ate the
# button down, the move loop ate the up, and the NM_CLICK that opens the
# editor only fired from an inactive window's orphan up; the editor was
# a 1998 centered dialog. the editor is in place now.
# ---------------------------------------------------------------------------
def t_zoom_pane_editor_round79():
    viv = read("src/viv.c").decode()
    chrome = read("src/viv_chrome.c").decode()
    dialogs = read("src/viv_dialogs.c").decode()
    dialogsh = read("src/viv_dialogs.h").decode()
    osc = read("src/os.c").decode()
    osh = read("src/os.h").decode()
    rct = read("res/voidImageViewer.rc").decode()
    rh = read("res/resource.h").decode()
    lh = read("src/localization.h").decode()
    le = read("src/localization_en_us.h").decode()
    lz = read("src/localization_zh_cn.h").decode()

    # the drag anchor retirement: the pane's down reaches the bar.
    check("the pane drag helper is gone everywhere (the down reaches the pane)",
          "os_statusbar_index_from_x" not in viv and
          "os_statusbar_index_from_x" not in chrome and
          "os_statusbar_index_from_x" not in osc and
          "os_statusbar_index_from_x" not in osh)
    check("the status subclass answers the editor colors (the chrome tokens)",
          "case WM_CTLCOLOREDIT:" in chrome and
          "SetBkColor((HDC)wParam,viv_theme_color(VIV_TK_CHROME));" in chrome and
          "SetTextColor((HDC)wParam,viv_theme_color(VIV_TK_TEXT));" in chrome)

    # the field: created on the pane, themed by the strip.
    check("the editor is a borderless number field on the pane",
          re.search(r'os_CreateWindowEx\(\s*\r\n\s*0,\s*\r\n\s*"EDIT",', dialogs) is not None and
          "WS_CHILD|WS_VISIBLE|ES_NUMBER|ES_AUTOHSCROLL" in dialogs)
    check("the field is placed and sized from the pane rect",
          "SB_GETRECT,0,(LPARAM)&pane_rect" in dialogs and
          "wide < 32" in dialogs)
    check("the old proc lives in the userdata (the edit key idiom, two steps)",
          "old_proc = (WNDPROC)SetWindowLongPtr(hwnd,GWLP_WNDPROC,(LONG_PTR)_viv_zoom_edit_proc);" in dialogs and
          "SetWindowLongPtr(hwnd,GWLP_USERDATA,(LONG_PTR)old_proc);" in dialogs and
          "old_proc = (WNDPROC)GetWindowLongPtr(hwnd,GWLP_USERDATA);" in dialogs)
    check("the digits ride the strip font",
          "WM_GETFONT,0,0" in dialogs and "WM_SETFONT,(WPARAM)hfont" in dialogs)
    check("typing replaces the selected percent",
          "EM_SETSEL,0,-1" in dialogs and
          'string_printf(wbuf,"%d",_viv_zoom_percent());' in dialogs)

    # the three exits and the re-entry guard.
    check("enter commits, escape cancels",
          "if (wParam == VK_RETURN)" in dialogs and
          "if (wParam == VK_ESCAPE)" in dialogs)
    check("focus lost commits",
          "case WM_KILLFOCUS:" in dialogs)
    check("the end guards against the re-entries",
          "if (hwnd != _viv_zoom_edit_hwnd)" in dialogs)
    check("the destroy clears the static (the teardown cascade)",
          "_viv_zoom_edit_hwnd = 0;" in dialogs)
    check("the keyboard goes home after the editor",
          "SetFocus(_viv_hwnd);" in dialogs)
    check("the commit contract survives the rewrite",
          "percent = string_to_int(wbuf);" in dialogs and
          "if (percent > 1600)" in dialogs and
          "if (percent >= 1)" in dialogs and
          "_viv_zoom_set_percent(percent,pt.x,pt.y,0);" in dialogs)
    check("one editor at a time (the re-open commits the first)",
          "_viv_zoom_edit_end(_viv_zoom_edit_hwnd,1);" in dialogs)
    check("the entry function body carries no dialog machinery",
          "DialogBox" not in dialogs.split("void _viv_set_zoom_dialog(void)\r\n{")[1].split("static void _viv_zoom_edit_end")[0])
    check("the header keeps the call sites stable",
          "keeps the old dialog's call sites stable" in dialogsh)

    # the retirement: template, ids, strings.
    check("the template is gone",
          "IDD_SET_ZOOM" not in rct)
    check("the control ids are gone",
          "IDC_SET_ZOOM" not in rh and "IDD_SET_ZOOM" not in rh)
    check("the localization ids and strings are gone",
          "SET_ZOOM" not in lh and "SET_ZOOM" not in le and "SET_ZOOM" not in lz)

    # the P1 audit fix: a canvas click takes the keyboard home (an open
    # editor commits through its kill focus path, no dead navigation zone).
    wndproc = read("src/viv_wndproc.c").decode()
    lb = wndproc.split("static LRESULT _viv_on_wm_lbuttondown")[1].split("static LRESULT")[0]
    check("a canvas click takes the keyboard home",
          lb.index("SetFocus(hwnd);") < lb.index("_viv_do_left_click_action"))

    changes = read("Changes.txt").decode("utf-8", errors="replace")
    check("the changelog states the zoom pane editor round",
          "the zoom pane editor round" in changes and
          "the anchor is retired" in changes and
          "enter commits, escape cancels" in changes)


def t_field_report_round80():
    load = read("src/viv_load.c").decode()
    view = read("src/viv_view.c").decode()
    viv = read("src/viv.c").decode()
    recent = read("src/viv_recent.c").decode()
    recenth = read("src/viv_recent.h").decode()
    dialogs = read("src/viv_dialogs.c").decode()
    settings = read("src/viv_settings.c").decode()
    chrome = read("src/viv_chrome.c").decode()
    menu = read("src/viv_menu.c").decode()
    wndproc = read("src/viv_wndproc.c").decode()
    changes = read("Changes.txt").decode("utf-8", errors="replace")

    # 1. the mru re-entry guard: a reload of the on-screen file is not an open.
    check("the mru push site is single and guarded",
          load.count("_viv_recent_file_push(full_path_and_filename);") == 1 and
          "_viv_icompare_filename(full_path_and_filename,_viv_current_fd->cFileName) != 0" in load)
    load_flat = " ".join(load.split())
    check("the guard states the reload contract",
          "masquerade as a new open" in load_flat and
          "a reload, not a recent open" in load_flat)
    check("the dialog open still feeds the mru (rc.16: declared a user open)",
          "_viv_open_from_filename(ofn.lpstrFile,VIV_OPEN_RECENT);" in view)
    wnd = read("src/viv_wndproc.c").decode()
    check("the single-file drop still feeds the mru (rc.16: declared a user open)",
          "_viv_open_from_filename(filename,VIV_OPEN_RECENT);" in wnd)
    check("the mru click still opens by name (rc.16: as a user open)",
          "_viv_open_from_filename(config_recent_files[recent_index],VIV_OPEN_RECENT)" in view)
    check("the command line still opens the single file (rc.16: as forwarded)",
          "_viv_open_from_filename(open_filename,VIV_OPEN_FORWARDED)" in viv)
    rotate = " ".join(view.split("static void _viv_edit_rotate(int counterclockwise)\r\n{")[1].split("static void _viv_file_edit(void)\r\n{")[0].split())
    check("rotate never touches the recent list",
          "_viv_recent" not in rotate and
          'counterclockwise ? "rotate270" : "rotate90"' in rotate)
    check("the single-instance forward survives (the fix is at the push site)",
          "cds.dwData = _VIV_COPYDATA_COMMAND_LINE;" in viv)

    # 2. the mru hygiene: delete drops the entry, rename swaps it in place.
    check("the remove-by-name helper exists and walks the live list",
          "void _viv_recent_file_remove_filename(const wchar_t *filename)" in recent and
          "_viv_recent_file_remove_filename(const wchar_t *filename);" in recenth)
    check("the rename helper swaps the entry in place (no push)",
          "void _viv_recent_file_rename(const wchar_t *old_filename,const wchar_t *new_filename)" in recent and
          "config_recent_files[i] = entry;" in recent and
          "_viv_recent_file_rename(const wchar_t *old_filename,const wchar_t *new_filename);" in recenth)
    check("the delete maintains its recent entry",
          "_viv_recent_file_remove_filename(fd.cFileName);" in view)
    check("the rename maintains its recent entry",
          "_viv_recent_file_rename(old_filename,file_op_new_name);" in dialogs and
          '#include "viv_recent.h"' in dialogs)
    check("the rename swap keeps its position (no reorder)",
          "string_alloc(new_filename)" in recent and
          "mem_free(config_recent_files[i]);" in recent)

    # 3. the font harmony: the settings rows ride the system basis.
    check("the settings row font is 12 dip (the system message basis)",
          "lf.lfHeight = -_viv_settings_dip(12);" in settings)
    check("the 13 dip row font is retired",
          "lf.lfHeight = -_viv_settings_dip(13);" not in settings)
    check("the description font keeps the one-dip hierarchy",
          "lf.lfHeight = -_viv_settings_dip(11);" in settings)
    check("the design note carries the new contract",
          "12 dip rows, 11 dip descriptions" in settings)
    check("the row bands did not shrink with the font",
          "#define _VIV_SETTINGS_ROW_HIGH		34" in settings and
          "#define _VIV_SETTINGS_DESC_HIGH		18" in settings)

    # 4. the capture-hint mojibake: utf-8 strings ride the utf-8 copier.
    check("the capture hint copies through the utf-8 bridge",
          "string_copy_utf8_string(wbuf,localization_get_string(_viv_settings_capture_edit" in settings)
    flat_settings = " ".join(settings.split())
    import glob as _glob
    bad = []
    for path in _glob.glob("src/*.c"):
        src = " ".join(read(path).decode().split())
        for fn in ("string_copy", "string_cat"):
            if re.search(fn + r"\(\s*\w+\s*,\s*localization_get_string\(", src):
                bad.append(path)
    check("no wide copier ever consumes a localization string (tree-wide)",
          bad == [], str(bad))

    # 5. the status strip font: pinned, and re-pinned on dpi change.
    check("the status bar creation pins the menu font",
          "SendMessage(_viv_status_hwnd,WM_SETFONT,(WPARAM)_viv_menu_font(),MAKELPARAM(TRUE,0));" in chrome)
    dpi = wndproc.split("static LRESULT _viv_on_wm_dpichanged")[1].split("static LRESULT")[0]
    check("a dpi change re-pins the strip font",
          "SendMessage(_viv_status_hwnd,WM_SETFONT,(WPARAM)_viv_menu_font(),MAKELPARAM(TRUE,0));" in dpi and
          dpi.index("_viv_menu_font_drop();") < dpi.index("WM_SETFONT"))

    # 6. the menu window theming owns its process.
    check("the popup theme validates the owning process",
          "GetWindowThreadProcessId(menu_hwnd,&menu_pid)" in menu and
          "(menu_pid == GetCurrentProcessId())" in menu)
    check("the dark re-apply validates the owning process too",
          "GetWindowThreadProcessId(menu_hwnd,&menu_pid)" in chrome and
          "(menu_pid == GetCurrentProcessId())" in chrome)

    # 7. the changelog and the release identity.
    flat = " ".join(changes.split())
    check("the changelog states the recent reentry guard",
          "the push site now asks one question" in flat and
          "a reload is not a recent open" in flat)
    check("the changelog states the mru hygiene",
          "the delete drops it, the rename" in flat)
    check("the changelog states the font proportion round",
          "the settings rows move to" in flat and "12 dip" in flat)
    check("the changelog states the mojibake root",
          "the localization returns utf-8" in flat)
    check("the changelog states the menu window hardening",
          "the owning process" in flat)


def t_open_intent_round81():
    load = read("src/viv_load.c").decode()
    loadh = read("src/viv_load.h").decode()
    view = read("src/viv_view.c").decode()
    viv = read("src/viv.c").decode()
    wnd = read("src/viv_wndproc.c").decode()
    changes = read("Changes.txt").decode("utf-8", errors="replace")

    # 1. the policy is a declared parameter, not a guess at the push site.
    check("the open-by-name carries a recent-list policy",
          "BOOL _viv_open_from_filename(const wchar_t *filename,int recent_policy);" in loadh and
          load.count("BOOL _viv_open_from_filename(const wchar_t *filename,int recent_policy);") == 1)
    check("the two policies are named constants",
          "#define VIV_OPEN_RECENT     1" in loadh and
          "#define VIV_OPEN_FORWARDED  0" in loadh)
    check("the one-argument signature is gone from the tree",
          all("_viv_open_from_filename(const wchar_t *filename)" not in src
              for src in (load, loadh, view, viv, wnd)))
    loadh_flat = " ".join(loadh.split())
    check("the declaration states the standard mru contract",
          "the standard mru contract" in loadh_flat and
          "a re-open of the displayed file re-tops it" in loadh_flat)

    # 2. the push condition: the declared policy first, the file identity second.
    check("the push asks the policy first, the file identity second",
          "if ((recent_policy) || ((_viv_icompare_filename(full_path_and_filename,_viv_current_fd->cFileName) != 0) && (_viv_icompare_filename(full_path_and_filename,_viv_slot_current.fd.cFileName) != 0)))" in load)
    load_flat = " ".join(load.split())
    check("the guard states the declared-intent contract",
          "declared by the caller" in load_flat and
          "masquerade as a new open" in load_flat and
          "a reload, not a recent open" in load_flat)

    # 3. the three user commands declare a user open - the trade refunded.
    check("the dialog open feeds the mru unconditionally",
          "_viv_open_from_filename(ofn.lpstrFile,VIV_OPEN_RECENT);" in view)
    check("the single-file drop feeds the mru unconditionally",
          "_viv_open_from_filename(filename,VIV_OPEN_RECENT);" in wnd)
    check("the mru click re-tops the displayed file again (the refund)",
          "_viv_open_from_filename(config_recent_files[recent_index],VIV_OPEN_RECENT)" in view)

    # 4. the one unknowable path declares itself forwarded.
    check("the forwarded command line keeps the same-file question",
          "_viv_open_from_filename(open_filename,VIV_OPEN_FORWARDED)" in viv)
    check("the tree counts the open-by-name exactly (2 declarations + 6 sites)",
          viv.count("_viv_open_from_filename(") == 8 and
          view.count("_viv_open_from_filename(") == 2 and
          wnd.count("_viv_open_from_filename(") == 2)  # r114: the paste text fallback opens a copied path

    # 5. the changelog refunds the trade.
    flat = " ".join(changes.split())
    check("the changelog states the refund",
          "refunded here" in flat and
          "the honest trade of the" in flat)
    check("the changelog states the declared intent",
          "declared where it is knowable" in flat and
          "a same-file forward is a reload, not a recent open" in flat)



def t_review_absorption_round82():
    """Guards for the review absorption round (1.1.12-rc.17: the external
    carpet review v2 - its confirmed fixes absorbed, its refutations
    recorded)."""
    chrome = read("src/viv_chrome.c").decode()
    settings = read("src/viv_settings.c").decode()
    wnd = read("src/viv_wndproc.c").decode()
    vivc = read("src/viv.c").decode()
    changes = read("Changes.txt").decode("utf-8", errors="replace")

    # 1. the pane index rides the official carrier, with a fallback draw.
    check("the pane draw reads itemID first with itemData as the fallback",
          "part = (int)draw_item->itemID;" in chrome and
          "part = (int)draw_item->itemData;" in chrome and
          chrome.find("part = (int)draw_item->itemID;") <
          chrome.find("part = (int)draw_item->itemData;"))
    check("an unresolvable pane paints the face instead of returning unhandled",
          chrome.count("FillRect(hdc,&draw_item->rcItem,viv_theme_brush(VIV_TK_CHROME));") == 2 and
          "the rc.11 bottom white bar was exactly a" in chrome)
    check("the pane text rides the text token",
          "text_color = viv_theme_color(VIV_TK_TEXT);" in chrome)

    # 2. the status strip is on the tokens, no hardcoded palette of its own.
    check("the erase rides the chrome brush",
          "FillRect((HDC)wParam,&rect,viv_theme_brush(VIV_TK_CHROME));" in chrome)
    check("the grip fill and dots ride the tokens",
          "FillRect(hdc,&grip_rect,viv_theme_brush(VIV_TK_CHROME));" in chrome and
          "grip_rect.bottom - 2 - (dot_y * step),viv_theme_color(VIV_TK_TEXT2));" in chrome)
    check("the zoom editor field rides the tokens",
          "SetBkColor((HDC)wParam,viv_theme_color(VIV_TK_CHROME));" in chrome and
          "SetTextColor((HDC)wParam,viv_theme_color(VIV_TK_TEXT));" in chrome and
          "return (LRESULT)viv_theme_brush(VIV_TK_CHROME);" in chrome)
    check("the strip carries no hardcoded palette of its own",
          "RGB(0xE8,0xE8,0xE8)" not in chrome and
          "RGB(0x20,0x20,0x20)" not in chrome and
          "RGB(0x9A,0x9A,0x9A)" not in chrome and
          "_viv_dialog_dark_brush() : GetSysColorBrush(COLOR_BTNFACE" not in chrome)

    # 3. the crash-guard comment tells the true story.
    check("the drawitem comment calls the ctltype what it is (uninitialized, not a quirk)",
          "leaves the field uninitialized" in wnd and
          "historical quirk" not in wnd)

    # 4. the settings window: the dpi correction and the escape hatch.
    check("the show path re-measures the frame at the window's own dpi",
          "_viv_settings_window_size_px(&wide,&high);" in settings and
          "SetWindowPos(_viv_settings_hwnd,0,0,0,wide,high,SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE);" in settings and
          settings.find("vivp_dpi_probe(\"show-after-create\")") <
          settings.find("SetWindowPos(_viv_settings_hwnd,0,0,0,wide,high"))
    check("the window grows: the edges hit the sizing codes",
          "static int _viv_settings_edge_hit(HWND hwnd,POINT *pt)" in settings and
          "return HTBOTTOMLEFT;" in settings and
          "return HTLEFT;" in settings and
          "return HTRIGHT;" in settings)
    check("the resize band handles the older sdk headers",
          "#define SM_CXPADDEDBORDER 92" in settings)
    check("the edge hit runs before the title row drag",
          settings.find("_viv_settings_edge_hit(hwnd,&pt);") <
          settings.find("if (pt.y < _viv_settings_dip(_VIV_SETTINGS_TITLE_HIGH))"))
    check("the minimum track size is the design size",
          "case WM_GETMINMAXINFO:" in settings and
          "_viv_settings_window_size_px(&mmi->ptMinTrackSize.x,&mmi->ptMinTrackSize.y);" in settings)

    # 5. the startup dpi sync.
    check("the init path syncs the window dpi before the first strip",
          "os_window_update_dpi(_viv_hwnd);" in vivc and
          vivc.find("os_CreateWindowEx(") <
          vivc.find("os_window_update_dpi(_viv_hwnd);") <
          vivc.find("_viv_menubar_show(config_show_menu);"))

    # 6. the version and the changelog.
    version = read("src/version.h").decode()
    check("the version is 1.1.15-rc.16 build 95",
          '#define VERSION_BUILD 95' in version and
          '#define VERSION_STRING "1.1.15-rc.16"' in version)
    flat = " ".join(changes.split())
    check("the changelog states the review absorption",
          "the review absorption round" in flat and
          "records its refutations" in flat)
    check("the changelog states the gesture refutation",
          "the gesture leak it asked to self-check is not there" in flat)
    check("the changelog states the deferrals",
          "deferred on purpose" in flat and
          "the debt list" in flat)



def t_stable_promotion_round83():
    """Guards for the stable promotion round (1.1.12: the rc arc promoted -
    no code rides the round, only the version identity, the changelog
    and the readme)."""
    version = read("src/version.h").decode()
    changes = read("Changes.txt").decode("utf-8", errors="replace")
    readme = read("README.md").decode("utf-8", errors="replace")
    experience = read("experience.md").decode("utf-8", errors="replace")

    # the tree has moved on to the 1.1.13 rc train: the promotion
    # identity now rides the central version guard (t_version), and
    # this section pins the promotion's permanent record.
    check("the changelog keeps the stable promotion entry",
          "Stable: Version 1.1.12 (the remake stable)" in changes)
    flat = " ".join(changes.split())
    check("the changelog states the no-code promotion",
          "carries no code" in flat and
          "the verdict on the whole rc arc" in flat)
    check("the readme current line is the stable (the 1.1.14 promotion moved it)",
          "**1.1.14 \u2014 the current stable**" in readme)
    check("the rc.17 entry rides the one-line list (the 1.1.13 candidate took the slot)",
          "**1.1.12-rc.17** \u2014" in experience and
          "(the previous release candidate):**" not in readme)


def t_readme_diet_round85():
    """Guards for the readme diet round (1.1.13-rc.1: the news section
    demoted to the one-line list - full treatment only for the current
    stable and the current candidate; the changelog is the archive of
    record)."""
    readme = read("README.md").decode("utf-8", errors="replace")
    experience = read("experience.md").decode("utf-8", errors="replace")
    changes = read("Changes.txt").decode("utf-8", errors="replace")

    check("the rc.1 entry rides the one-line list (the rc.2 candidate took the slot)",
          "**1.1.13-rc.1** \u2014" in experience and
          "**1.1.13-rc.1 \u2014" not in readme)
    check("the stable line keeps its house form (the 1.1.14 stable now)",
          "**1.1.14 \u2014 the current stable**" in readme)
    # the diet itself: the retired rounds carry no full sections. the
    # full form is "**<tag> \u2014 ...**" (bold spans the dash); the
    # one-line form is "**<tag>** \u2014 ..." (bold closes before the dash).
    check("the rc.17 entry rides the one-line list",
          "**1.1.12-rc.17** \u2014" in experience and
          "**1.1.12-rc.17 \u2014" not in readme)
    check("the retired rounds carry no full sections",
          all(f"**{tag} \u2014" not in readme for tag in (
              "1.1.12-rc.16", "1.1.12-rc.15", "1.1.12-rc.14", "1.1.12-rc.13",
              "1.1.12-rc.12", "1.1.12-rc.11", "1.1.12-rc.10", "1.1.12-rc.9",
              "1.1.12-rc.7")))
    check("the one-line list reaches back through the early arc",
          "**1.1.11** \u2014" in experience and "**1.1.02\u20131.1.10**" in experience)
    check("the archive pointer stays",
          "lives in [experience.md](experience.md)" in readme)
    flat = " ".join(changes.split())
    check("the changelog states the diet rule",
          "full treatment goes to the" in flat and
          "archive of record" in flat)
    check("the changelog opens the 1.1.13 arc (the todo adoption)",
          "the upstream todo the fork now adopts" in flat and
          "the association guard" in flat and
          "the renderers" in flat)


def t_association_guard_round86():
    """Guards for the association guard round (1.1.13-rc.2: the first
    upstream todo item - install bmp/jpg only when the extension's
    effective default is the windows canonical class or our own)."""
    install = read("src/viv_install.c").decode("latin-1")
    changes = read("Changes.txt").decode("utf-8", errors="replace")
    readme = read("README.md").decode("utf-8", errors="replace")
    experience = read("experience.md").decode("utf-8", errors="replace")

    at = install.index("static int _viv_is_foreign_association(const char *association,const wchar_t *class_name)\r\n{")
    body = install[at:install.index("void _viv_install_association_by_extension(", at)]
    assoc_at = install.index("void _viv_install_association_by_extension(const char *association,const char *description,const char *icon_location)\r\n{")
    assoc = install[assoc_at:install.index("void _viv_uninstall_association_by_extension(", assoc_at)]

    # 1. the canonical table: the todo's two names across the
    #    association table's spellings.
    check("the canonical table covers bmp and the jpg family",
          '"bmp","jpg","jpeg"' in body and
          '"bmpfile","jpgfile","jpgfile"' in body)
    # 2. the effective owner is read from the merged view the shell
    #    resolves (the per-user classes over the machine ones).
    check("the effective owner is read from the merged view",
          "RegOpenKeyExW(HKEY_CLASSES_ROOT,key,0,KEY_QUERY_VALUE,&hkey)" in body)
    # 3. the tri-state: an empty default, the canonical class, or
    #    our own class passes; anything else is foreign.
    check("the gate reads the extension's default value",
          "_viv_get_registry_string(hkey,0,wbuf,STRING_SIZE)" in body and
          "(*wbuf)" in body)
    check("only the canonical class or our own class passes",
          "string_icompare_lowercase_ascii(wbuf,canonical_class) != 0" in body and
          "string_compare(wbuf,class_name) != 0" in body)
    # 4. the gate position: after the uninstall-restore (an upgrade
    #    heals to the pre-fork owner first), before the backup write.
    check("the gate sits after the uninstall-restore (upgrade heal)",
          assoc.index("_viv_is_foreign_association(association,class_name)") >
          assoc.index("_viv_uninstall_association_by_extension(association);"))
    check("the gate sits before the backup write",
          assoc.index("_viv_is_foreign_association(association,class_name)") <
          assoc.index("voidImageViewer.Backup"))
    check("a foreign owner returns before any write of ours",
          'debug_printf("association .%s left alone (a foreign viewer owns it)' in assoc)
    # 5. the readme candidate slot hands over.
    check("the rc.2 entry rides the one-line list (the rc.3 candidate took the slot)",
          "**1.1.13-rc.2** \u2014" in experience and
          "**1.1.13-rc.2 \u2014" not in readme)
    # 6. the changelog carries the round.
    flat = " ".join(changes.split())
    check("the changelog states the todo landing",
          "the first todo item lands" in flat and
          "foreign viewer" in flat)
    check("the changelog states the gate position",
          "after the uninstall-restore" in flat)


def t_halftone_palette_round89():
    """Guards for the halftone palette round (1.1.13-rc.3: the second
    upstream todo item - graphics::GetHalftonePalette for 256 color
    mode)."""
    wndproc = read("src/viv_wndproc.c").decode("latin-1")
    osh = read("src/os.h").decode("latin-1")
    osc = read("src/os.c").decode("latin-1")
    changes = read("Changes.txt").decode("utf-8", errors="replace")
    readme = read("README.md").decode("utf-8", errors="replace")
    experience = read("experience.md").decode("utf-8", errors="replace")
    version = read("src/version.h").decode("latin-1")

    # 1. the os layer: the gdiplus flat api behind the todo's call.
    check("os.h declares the halftone palette flat api",
          "extern HPALETTE (__stdcall *os_GdipCreateHalftonePalette)(void);" in osh)
    check("os.c loads the flat api beside the other gdi+ entries",
          '_os_get_proc_address(_os_gdiplus_hmodule,"GdipCreateHalftonePalette")' in osc)
    # 2. the need gate: the palette exists only on a palettized display.
    check("the sync reads the color depth from the window's own dc",
          "GetDeviceCaps(hdc,BITSPIXEL) * GetDeviceCaps(hdc,PLANES)" in wndproc)
    check("the palette is created only for the 256 color mode",
          "bpp == 8" in wndproc)
    # 3. the classic contract: foreground on activation, background on
    #    another window's change, never answering our own.
    check("wm_querynewpalette realizes the foreground palette",
          "case WM_QUERYNEWPALETTE:" in wndproc and
          "_viv_halftone_palette_realize(hwnd,1)" in wndproc)
    check("wm_palettechanged never answers its own change (the loop guard)",
          "if ((HWND)wParam == hwnd)" in wndproc and
          "_viv_halftone_palette_realize(hwnd,0)" in wndproc)
    check("the paint dc carries the palette selection",
          "SelectPalette(ps.hdc,_viv_halftone_palette" in wndproc)
    check("wm_displaychange re-reads the need",
          "case WM_DISPLAYCHANGE:" in wndproc)
    at = wndproc.index("static LRESULT _viv_on_wm_destroy(")
    end = wndproc.index("static LRESULT _viv_on_wm_queryendsession(", at)
    destroy = wndproc[at:end]
    check("the destroy path releases the palette",
          "DeleteObject(_viv_halftone_palette)" in destroy)
    # 4. the version and the readme candidate slot.
    check("the version is 1.1.15-rc.16 build 95",
          '#define VERSION_BUILD 95' in version and
          '#define VERSION_STRING "1.1.15-rc.16"' in version)
    check("the rc.3 entry rides the one-line list (the rc.4 candidate took the slot)",
          "**1.1.13-rc.3** \u2014" in experience and
          "**1.1.13-rc.3 \u2014" not in readme)
    # 5. the changelog carries the round.
    flat = " ".join(changes.split())
    check("the changelog states the todo landing",
          "the second todo item lands" in flat and
          "256 color" in flat)
    check("the changelog states the realization contract",
          "foreground" in flat and
          "background" in flat and
          "wm_displaychange" in flat)


def t_release_title_crlf_fix():
    """Guard for the pipeline title derivation fix (caught live on the
    rc.3 release): the changelog is crlf, head -n 1 carries the \r, and
    the sed chain's trailing-paren strip never fired - the derived title
    shipped a stray ")". the notes h1 derivation was immune (python
    splitlines + rstrip never see the \r). this guard runs the
    workflow's own pipeline against the live changelog head so the two
    derivations must agree; a regression cannot ship green."""
    import os
    import subprocess
    ry = read(".github/workflows/release.yml").decode()

    # 1. the fix rides the sed chain: the \r strip comes first.
    check("the title derivation strips the crlf tail first",
          "sed -e 's/\\r$//' -e 's/^[^:]*: *Version *[^ ]* *//' -e 's/^(//' -e 's/)$//'" in ry)

    # 2. the live proof: run the workflow's own pipeline (bash with
    #    GITHUB_WORKSPACE pointed at the tree) on the real head line.
    m = re.search(r'round="\$\((.*?)\)"', ry, re.S)
    check("the workflow still derives the title from the changelog head",
          m is not None)
    if m:
        env = dict(os.environ, GITHUB_WORKSPACE=".")
        out = subprocess.run(["bash", "-c", m.group(1)], capture_output=True,
                             env=env, cwd=".").stdout.decode("utf-8", errors="replace").strip()
        head = read("Changes.txt").decode("utf-8", errors="replace").split("\n")[0]
        hm = re.match(r"^[^:]*: *Version *[^ ]* \((.*)\)\r?$", head)
        expected = hm.group(1) if hm else ""
        check("the derived round name equals the changelog parenthetical",
              out == expected, f"(derived {out!r} vs parsed {expected!r})")


def t_high_dpi_icons_round90():
    """Guards for the high dpi icons round (1.1.13-rc.4: the third
    upstream todo item - the ico grows the dpi ladder and the frame
    icons ride the window's dpi)."""
    import struct
    wnd = read("src/viv_wndproc.c").decode("latin-1")
    osh = read("src/os.h").decode("latin-1")
    osc = read("src/os.c").decode("latin-1")
    ico = read("res/voidImageViewer.ico")
    changes = read("Changes.txt").decode("utf-8", errors="replace")
    readme = read("README.md").decode("utf-8", errors="replace")
    experience = read("experience.md").decode("utf-8", errors="replace")
    version = read("src/version.h").decode("latin-1")

    # 1. the resource ladder: ten frames, the dpi sizes, the legacy
    #    8bpp pair still riding along.
    n = struct.unpack("<H", ico[4:6])[0]
    sizes = sorted((struct.unpack("<B", ico[6 + i * 16:6 + i * 16 + 1])[0] or 256) for i in range(n))
    bpps = [struct.unpack("<H", ico[6 + i * 16 + 6:6 + i * 16 + 8])[0] for i in range(n)]
    check("the ico carries the high dpi ladder",
          n == 10 and sizes == [16, 16, 20, 24, 32, 32, 48, 64, 128, 256])
    check("the legacy 8bpp frames still ride along",
          bpps.count(8) == 2 and bpps.count(32) == 8)
    # 2. the os ladder: the metric at an explicit dpi.
    check("os.h exports the dpi-aware metric helper",
          "int os_GetSystemMetricsForDpi(int index,UINT dpi);" in osh)
    check("os.c lazy-loads GetSystemMetricsForDpi beside the other dpi entries",
          'GetProcAddress(_os_user32_hmodule,"GetSystemMetricsForDpi")' in osc)
    check("the fallback rides the raw metric (the pre-1607 ladder)",
          "return GetSystemMetrics(index);" in osc)
    # 3. the per-window icons.
    check("the icons load at the window's dpi",
          "os_GetSystemMetricsForDpi(SM_CXICON,dpi)" in wnd and
          "os_GetSystemMetricsForDpi(SM_CXSMICON,dpi)" in wnd)
    check("wm_seticon pins the big and small pair",
          "SendMessage(hwnd,WM_SETICON,ICON_BIG,(LPARAM)big_icon)" in wnd and
          "SendMessage(hwnd,WM_SETICON,ICON_SMALL,(LPARAM)small_icon)" in wnd)
    check("the rpcndr small-macro trap stays documented",
          "`small` is an rpcndr.h macro" in wnd)
    check("the new pair lands before the old one retires",
          wnd.index("SendMessage(hwnd,WM_SETICON,ICON_BIG,(LPARAM)big_icon)") <
          wnd.index("DestroyIcon(_viv_icon_big);"))
    check("wm_dpichanged re-pins the pair at the new dpi",
          "_viv_icons_apply(hwnd);" in wnd)
    at = wnd.index("static LRESULT _viv_on_wm_destroy(")
    end = wnd.index("static LRESULT _viv_on_wm_queryendsession(", at)
    destroy = wnd[at:end]
    check("the destroy path retires both icons",
          "DestroyIcon(_viv_icon_big)" in destroy and
          "DestroyIcon(_viv_icon_small)" in destroy)
    # 4. the init call and the version.
    vivc = read("src/viv.c").decode("latin-1")
    check("the init path pins the icons after the dpi sync",
          vivc.index("_viv_icons_apply(_viv_hwnd);") >
          vivc.index("os_window_update_dpi(_viv_hwnd);"))
    check("the version is 1.1.15-rc.16 build 95",
          '#define VERSION_BUILD 95' in version and
          '#define VERSION_STRING "1.1.15-rc.16"' in version)
    check("the rc.4 entry rides the one-line list (the rc.5 candidate took the slot)",
          "**1.1.13-rc.4** \u2014" in experience and
          "**1.1.13-rc.4 \u2014" not in readme)
    # 5. the changelog carries the round.
    flat = " ".join(changes.split())
    check("the changelog states the todo landing",
          "the third todo item lands" in flat and
          "high dpi" in flat)
    check("the changelog states the ladder and the seticon pair",
          "256, 128, 64," in flat and
          "wm_seticon pins the big and small pair" in flat)


def t_dead_residue_round91():
    """Guards for the dead residue round (1.1.13-rc.5: the carpet sweep -
    the zero-reference residue the static scan surfaced retires, and the
    deliberately-frozen corners stay pinned as kept)."""
    memc = read("src/mem.c").decode("latin-1")
    memh = read("src/mem.h").decode("latin-1")
    render = read("src/viv_render.c").decode("latin-1")
    view = read("src/viv_view.c").decode("latin-1")
    stateh = read("src/viv_state.h").decode("latin-1")
    osh = read("src/os.h").decode("latin-1")
    selfc = read("src/viv_selfshot.c").decode("latin-1")
    settingsc = read("src/viv_settings.c").decode("latin-1")
    instc = read("src/viv_install.c").decode("latin-1")
    resh = read("res/resource.h").decode("latin-1")
    changes = read("Changes.txt").decode("utf-8", errors="replace")
    readme = read("README.md").decode("utf-8", errors="replace")
    version = read("src/version.h").decode("latin-1")

    # 1. the three zero-reference functions retire (their live
    #    neighbors survive - the sweep did not overreach).
    check("the dead mem getter is gone",
          "get_mem_usage" not in memc and "get_mem_usage" not in memh)
    check("the mem accounting global survives (the debug paths read it)",
          "mem_usage" in memc)
    check("the dead ceil helper is gone",
          "_viv_ceil" not in render)
    check("the stretch path it sat beside survives",
          "_viv_stretch_blt" in render)
    check("the dead key-state probe is gone",
          "_viv_is_key_state" not in view)
    check("the pause path it sat above survives",
          "void _viv_pause(void)" in view)
    # 2. the dead macro families.
    check("the stale association bit macros are gone (the positional scheme is the carrier)",
          "_VIV_ASSOCIATION_BMP" not in stateh and
          "1 << exti" in instc and
          "_VIV_ASSOCIATION_COUNT" in stateh)
    check("the selfshot harness keeps only the timer id",
          "_VIV_SELF_WM_STEP" not in selfc and "_VIV_SELF_TIMER_ID" in selfc)
    check("the unused settings color sentinel is gone",
          "_VIV_SETTINGS_COUNT" not in settingsc)
    # 3. the resource.h residue (the toolbar-icon-drop precedent).
    for dead in ("IDD_FORMVIEW", "IDD_FORMVIEW1", "IDD_DIALOG1", "IDD_FORMVIEW2",
                 "IDB_BITMAP1", "IDC_COMBO3", "IDC_CHECK1", "IDC_BUTTON1",
                 "IDC_BUTTON2", "IDC_BUTTON3", "IDC_LIST1", "IDC_LIST2",
                 "IDC_OLD_EDIT", "IDC_EDIT1"):
        check("resource.h drops %s" % dead,
              ("#define " + dead + " ") not in resh)
    # r114: the classic options ids left with the dialogs they named
    # (r91 keeps them was the recorded rollback anchor; the rc.4 cascade
    # picker is that list browsing, so the condition is met).
    # 4. the deliberate keeps (recorded decisions, not residue).
    # r114: the frozen options family left (the rc.4 cascade picker is
    # the list browsing the round-91 anchor was waiting for).
    check("the unicows bootstrap stays (version_x86 builds link it)",
          "LoadUnicowsProc" in read("src/viv.c").decode("latin-1"))
    check("the crt assert override stays",
          "_wassert" in read("src/webp.c").decode("latin-1"))
    check("the full cbs/rbs state ladder stays (guard-pinned table)",
          "#define OS_BS_CHECKEDDISABLED 8" in osh)
    # 5. the version and the readme slot.
    check("the version is 1.1.15-rc.16 build 95",
          '#define VERSION_BUILD 95' in version and
          '#define VERSION_STRING "1.1.15-rc.16"' in version)
    check("the current line is the stable promotion stable",
          "**1.1.14 \u2014 the current stable**" in readme)
    # 6. the changelog carries the round.
    flat = " ".join(changes.split())
    check("the changelog states the sweep",
          "the dead residue round" in flat)
    check("the changelog states the deliberate keeps",
          "stays frozen" in flat and "unicows" in flat)


def t_peripheral_residue_round92():
    """Guards for the peripheral residue round (1.1.13-rc.6: the closing
    sweep - the never-built wine probe, four zero-reference api functions,
    the year stringize pair and three never-requested localization strings
    retire; the recorded keeps stay pinned)."""
    osc = read("src/os.c").decode("latin-1")
    osh = read("src/os.h").decode("latin-1")
    safec = read("src/safe_size.c").decode("latin-1")
    safeh = read("src/safe_size.h").decode("latin-1")
    poolc = read("src/small_pool.c").decode("latin-1")
    poolh = read("src/small_pool.h").decode("latin-1")
    u8c = read("src/utf8.c").decode("latin-1")
    u8h = read("src/utf8.h").decode("latin-1")
    stateh = read("src/viv_state.h").decode("latin-1")
    loch = read("src/localization.h").decode("latin-1")
    locen = read("src/localization_en_us.h").decode("utf-8", errors="replace")
    loczh = read("src/localization_zh_cn.h").decode("utf-8", errors="replace")
    changes = read("Changes.txt").decode("utf-8", errors="replace")
    readme = read("README.md").decode("utf-8", errors="replace")
    version = read("src/version.h").decode("latin-1")

    # 1. the never-built wine dpi probe retires (it was never in any build).
    check("the wine dpi probe is gone",
          not os.path.exists("build-zig/dpiprobe.c"))
    check("the zig build list never knew it",
          "dpiprobe" not in read("build-zig/files.txt").decode("latin-1"))
    # 2. the four zero-reference api functions retire (their live
    #    neighbors survive - the sweep did not overreach).
    check("the manual frame calculator is gone",
          "os_adjust_window_rect" not in osc and
          "os_adjust_window_rect" not in osh)
    check("the window style getters it sat between survive",
          "os_get_window_style" in osh and "os_get_window_ex_style" in osh)
    check("the duplicate x2 helper is gone",
          "safe_size_mul_2" not in safec and "safe_size_mul_2" not in safeh)
    check("the rest of the safe size family survives",
          "safe_size_mul(" in safec and "safe_size_mul_sizeof_wchar" in safeh)
    check("the never-called pool reset is gone",
          "small_pool_empty" not in poolc and "small_pool_empty" not in poolh)
    check("the live pool api survives",
          "small_pool_init(" in poolc and "small_pool_kill(" in poolc and
          "small_pool_alloc" in poolh)
    check("the double-null walker is gone",
          "utf8_length_double_null" not in u8c and
          "utf8_length_double_null" not in u8h)
    check("the live utf8 api survives",
          "utf8_length(" in u8c and "utf8_to_int" in u8h)
    # 3. the year stringize pair retires.
    check("the year stringize pair is gone",
          "VIV_YEAR_STRING" not in stateh)
    check("the state layer neighbors survive",
          "_VIV_STRETCH_BLT_STITCH_SIZE" in stateh and
          "_VIV_DEFAULT_SHUFFLE_ALLOCATED" in stateh)
    # 4. the never-requested localization ids retire (word-boundary
    #    checks: REMOVE is a prefix of REMOVE_KEY_BUTTON, which stays).
    for dead in ("LOCALIZATION_ID_REMOVE", "LOCALIZATION_ID_SEARCH_EVERYTHING",
                 "LOCALIZATION_ID_STATUS_BAR_DIMENSIONS_FORMAT"):
        pat = re.compile(dead + r"(?![A-Z0-9_])")
        check("localization drops %s" % dead,
              pat.search(loch) is None and pat.search(locen) is None and
              pat.search(loczh) is None)
    check("the remove key button id the dialogs request stays",
          "LOCALIZATION_ID_REMOVE_KEY_BUTTON" in loch and
          "LOCALIZATION_ID_REMOVE_KEY_BUTTON" in locen and
          "LOCALIZATION_ID_REMOVE_KEY_BUTTON" in loczh)
    check("the open everything search id the menu requests stays",
          "LOCALIZATION_ID_OPEN_EVERYTHING_SEARCH" in loch and
          "LOCALIZATION_ID_OPEN_EVERYTHING_SEARCH" in locen)
    check("the live status format ids stay",
          "LOCALIZATION_ID_STATUS_BAR_POSITION_FORMAT" in loch and
          "LOCALIZATION_ID_STATUS_BAR_RGB_FORMAT" in loch and
          "LOCALIZATION_ID_STATUS_BAR_RGB_FORMAT" in locen)
    # 5. the deliberate keeps (recorded decisions, not residue).
    check("the libcmt debugger-present override stays (the linker consumes it)",
          "_imp__IsDebuggerPresent" in osc)
    check("the dark controls capability api stays (pinned since the dead gate)",
          "int os_dark_controls_supported(void)" in osc and
          "int os_dark_controls_supported(void);" in osh)
    check("the vendored everything sdk header stays intact",
          "EVERYTHING_IPC_QUERY2_REQUEST_PATH" in
          read("src/everything_ipc.h").decode("latin-1"))
    check("the theme transcription pair stays (the rc.8 mockup sync decision)",
          os.path.exists("scripts/extract-theme.mjs") and
          os.path.exists("sim/theme-tokens.ts"))
    # 6. the version and the readme slot.
    check("the version is 1.1.15-rc.16 build 95",
          '#define VERSION_BUILD 95' in version and
          '#define VERSION_STRING "1.1.15-rc.16"' in version)
    check("the current line is the stable promotion stable",
          "**1.1.14 \u2014 the current stable**" in readme)
    # 7. the changelog carries the round.
    flat = " ".join(changes.split())
    check("the changelog states the closing sweep",
          "the peripheral residue round" in flat)
    check("the changelog states the deliberate keeps",
          "gs_report" in flat and "mockup cannot drift" in flat)




def t_format_horizons_round94():
    """Guards for the format horizons round (1.1.13 stable: the wic fallback
    layer, the qoi decoder, the widened format gates and the stable promotion
    - the fork's first stable to carry code)."""
    vivh = read("src/viv.h").decode("latin-1")
    load = read("src/viv_load.c").decode("latin-1")
    view = read("src/viv_view.c").decode("latin-1")
    playlist = read("src/viv_playlist.c").decode("latin-1")
    wic = read("src/wic.c").decode("latin-1")
    qoi = read("src/qoi.c").decode("latin-1")
    wih = read("src/wic.h").decode("latin-1")
    qoh = read("src/qoi.h").decode("latin-1")
    files_txt = read("build-zig/files.txt").decode("latin-1")
    props = read("voidImageViewer.files.props").decode("latin-1")
    stateh = read("src/viv_state.h").decode("latin-1")
    changes = read("Changes.txt").decode("utf-8", errors="replace")
    readme = read("README.md").decode("utf-8", errors="replace")
    experience = read("experience.md").decode("utf-8", errors="replace")
    version = read("src/version.h").decode("latin-1")

    # 1. the wic layer.
    check("wic.c self-defines the imaging factory clsid (the os.c precedent)",
          "0xcacaf262,0x9370,0x4615" in wic and "_wic_clsid_imaging_factory" in wic)
    check("wic.c converts through the one universal pixel format",
          "_wic_pixel_format_32bpp_bgra" in wic and "0x6fddc324,0x4e03,0x4bfe" in wic)
    check("the wic canvas passes the pixel budget before the copy buffer",
          "VIV_MAX_IMAGE_PIXELS" in wic)
    check("the wic read head resets before the decoder sniffs (the gdi+ failure path)",
          "STREAM_SEEK_SET" in wic)
    check("wic.h answers the generic frame contract",
          "int wic_load(IStream *stream,void *user_data" in wih)
    check("the wic layer serves a single frame (the stills contract)",
          "user_data,1,wide,high,has_alpha" in wic)
    check("the wic file rides the crlf discipline",
          "\r\n" in wic)

    # 2. the qoi decoder.
    check("qoi.c checks the qoi magic",
          "0x716f6966" in qoi and "0x66696f71" not in qoi)
    check("qoi.c verifies the reference end marker",
          "{0,0,0,0,0,0,0,1}" in qoi)
    check("qoi.c ports the full reference opcode set",
          "0xfe" in qoi and "0xff" in qoi and "(b1 & 0xc0)" in qoi)
    check("qoi.c pins its port source (the mit reference decoder)",
          "reference decoder" in qoi)
    check("the qoi canvas passes the pixel budget",
          "VIV_MAX_IMAGE_PIXELS" in qoi)
    check("qoi.h answers the generic frame contract",
          "int qoi_load(IStream *stream,void *user_data" in qoh)
    check("the qoi file rides the crlf discipline",
          "\r\n" in qoi)

    # 3. the load dispatch chain: webp, then qoi, then wic.
    check("the fallback chain runs webp, then qoi, then wic",
          load.index("if (webp_load(stream,&viv_webp,") <
          load.index("if (qoi_load(stream,&viv_webp,") <
          load.index("else if (wic_load(stream,&viv_webp,"))
    check("the new layers ride the generic webp frame delivery",
          load.count("(int (*)(void *,DWORD,DWORD,DWORD,int))_viv_webp_info_proc") == 3 and
          load.count("(int (*)(void *,BYTE *,int))_viv_webp_frame_proc") == 3)
    check("viv.h carries the new decoder headers",
          '#include "qoi.h"' in vivh and '#include "wic.h"' in vivh)

    # 4. the format gates.
    check("the open filter carries the eight new extensions (both halves)",
          view.count("*.avif;*.bmp;*.dds;*.gif;*.hdp;*.heic;*.heif;*.ico;*.jpeg;*.jpg;*.jxr;*.png;*.qoi;*.tif;*.tiff;*.wdp;*.webp;*.emf;*.wmf") == 2)
    check("the everything search prefixes widen (both sites)",
          playlist.count("ext:avif;bmp;dds;gif;hdp;heic;heif;ico;jpeg;jpg;jxr;png;qoi;tif;tiff;wdp;webp;emf;wmf <") == 2)

    # 5. the association deferral stays pinned.
    check("the association table deliberately stays eleven",
          "#define _VIV_ASSOCIATION_COUNT\t11" in stateh)
    flat = " ".join(changes.split())
    check("the changelog states the association deferral",
          "association table deliberately stays" in flat)

    # 6. the build wiring.
    check("the zig file list carries both new units",
          "src/qoi.c" in files_txt and "src/wic.c" in files_txt)
    check("the shared props list carries all four new files",
          props.count('Include="..\\src\\qoi.c"') == 1 and
          props.count('Include="..\\src\\wic.c"') == 1 and
          props.count('Include="..\\src\\qoi.h"') == 1 and
          props.count('Include="..\\src\\wic.h"') == 1)

    # 7. the readme slot handover.
    check("the stable slot is the stable promotion round",
          "**1.1.14 \u2014 the current stable**" in readme)
    check("the rc.7 entry rides the one-line list",
          "**1.1.13-rc.7** \u2014" in experience and
          "**1.1.13-rc.7 \u2014" not in readme)
    check("the 1.1.12 full block demotes to the one-line list",
          "**1.1.12** \u2014" in experience and
          "**1.1.12 \u2014" not in readme)
    check("the rc.6 orphaned bullets retire (the r93 handover leftovers)",
          "- **The closing sweep** \u2014 the round-91 static scan rebuilt" not in readme)
    check("the readme headline carries the new formats",
          "JPEG-XR" in readme and "QOI" in readme)

    # 8. the changelog and the version.
    check("the changelog top entry is the stable promotion",
          "Stable: Version 1.1.13 (the format horizons round)" in changes)
    check("the version is 1.1.15-rc.16 build 95",
          '#define VERSION_BUILD 95' in version and
          '#define VERSION_STRING "1.1.15-rc.16"' in version)

    # 9. the closing scan's slam-dunks: two dead prototypes retire
    #    (the dispatch-wired families stay - macro token pasting is
    #    beyond a text-level scan's sight, the round-91 lesson).
    check("the dead prototypes retire (the r94 scan)",
          "__os_get_proc_address" not in read("src/os.c").decode("latin-1") and
          "_viv_queue_clear" not in read("src/viv.c").decode("latin-1"))

def t_todo_closure_round96():
    """Guards for the todo closure round (1.1.14-rc.1: the upstream TODO list
    retires - the opengl and direct3d renderers, the toolbar customization
    and the shell context menu land as the last four open items)."""
    vivc = read("src/viv.c").decode("latin-1")
    vivh = read("src/viv.h").decode("latin-1")
    stateh = read("src/viv_state.h").decode("latin-1")
    wnd = read("src/viv_wndproc.c").decode("latin-1")
    view = read("src/viv_view.c").decode("latin-1")
    menu = read("src/viv_menu.c").decode("latin-1")
    toolbar = read("src/viv_toolbar.c").decode("latin-1")
    toolbarh = read("src/viv_toolbar.h").decode("latin-1")
    configc = read("src/config.c").decode("latin-1")
    configh = read("src/config.h").decode("latin-1")
    shellh = read("src/shellmenu.h").decode("latin-1")
    shellc = read("src/shellmenu.c").decode("latin-1")
    glh = read("src/hwgl.h").decode("latin-1")
    glc = read("src/hwgl.c").decode("latin-1")
    d3dh = read("src/hwd3d.h").decode("latin-1")
    d3dc = read("src/hwd3d.c").decode("latin-1")
    files_txt = read("build-zig/files.txt").decode("latin-1")
    props = read("voidImageViewer.files.props").decode("latin-1")
    version = read("src/version.h").decode("latin-1")
    changes = read("Changes.txt").decode("utf-8", errors="replace")
    readme = read("README.md").decode("utf-8", errors="replace")
    experience = read("experience.md").decode("utf-8", errors="replace")

    # 1. the todo pile is closed.
    check("viv.c carries the closure marker",
          "the upstream list is closed" in vivc)
    check("the four open todo lines are gone",
          vivc.count("// - OpenGL renderer.") == 0 and
          vivc.count("// - Direct3D renderer.") == 0 and
          vivc.count("// - control toolbar customization.") == 0 and
          vivc.count("// - shell context menu") == 0)

    # 2. the shell context menu module.
    check("shellmenu.h pins the private id range",
          "#define _VIV_SHELL_MENU_ID_FIRST 0x7000" in shellh and
          "#define _VIV_SHELL_MENU_ID_LAST 0x7fff" in shellh)
    check("shellmenu.c resolves the shell32 5.0+ export set dynamically",
          'LoadLibraryA("shell32.dll")' in shellc and
          shellc.count("GetProcAddress") >= 5 and
          '"CDefFolderMenu_Create2"' in shellc and
          '"ILCreateFromPathW"' in shellc and
          '"ILClone"' in shellc and
          '"ILFindLastID"' in shellc and
          '"ILRemoveLastID"' in shellc)
    check("shellmenu.c self-spells the icontextmenu iids (the os.c precedent)",
          "_viv_shell_iid_icontextmenu" in shellc and
          "0x000214e4" in shellc and
          "0x000214f4" in shellc and
          "0xbcfce0a0" in shellc)
    check("shellmenu.c runs the documented invoke sequence",
          "QueryContextMenu" in shellc and
          "CMF_NORMAL" in shellc and
          "InvokeCommand" in shellc and
          "HandleMenuMsg2" in shellc)
    check("the shellmenu file rides the crlf discipline",
          "\r\n" in shellc)
    check("the context menu appends the shell section after the app items",
          wnd.index("_viv_shell_context_menu_append") >
          wnd.index("_viv_context_menu_items[i]") and
          wnd.index("_viv_shell_context_menu_append") <
          wnd.index("_viv_check_menus(hmenu)"))
    check("the shell menu finishes after the popup tracks",
          wnd.index("_viv_shell_context_menu_finish") >
          wnd.index("TrackPopupMenu(hmenu,tpm_flags") and
          wnd.index("_viv_shell_context_menu_finish") <
          wnd.index("DestroyMenu(hmenu)"))
    check("wm_command routes the shell range before the command table",
          wnd.index("_viv_shell_context_menu_invoke(hwnd,command_id)") <
          wnd.index("_viv_command(command_id)"))
    check("the shell owner-draw rows forward before the app painter",
          wnd.index("_viv_shell_context_menu_handle_menu_msg(hwnd,msg,wParam,lParam)") <
          wnd.index("_viv_menu_measure_item") and
          wnd.count("_viv_shell_context_menu_handle_menu_msg(hwnd,msg,wParam,lParam)") == 3)

    # 3. the opengl renderer module.
    check("hwgl.c loads opengl32 dynamically (the gdi+ table precedent)",
          'LoadLibraryA("opengl32.dll")' in glc and
          "wglCreateContext" in glc and
          "ChoosePixelFormat" in glc)
    check("hwgl.c probes GL_EXT_bgra before the bgr direct upload",
          "GL_EXT_bgra" in glc and "GL_BGR_EXT" in glc)
    check("hwgl.c pads to power-of-two textures (the GL 1.1 contract)",
          "power of two" in glc and glc.count("<<= 1") == 2)
    check("hwgl.c rides the linear filter without the glu dependency",
          "GL_LINEAR" in glc and "glu" not in glc)
    check("hwgl.c swaps buffers and shuts down its context",
          "SwapBuffers" in glc and
          "wglDeleteContext" in glc and
          "wglMakeCurrent(NULL,NULL)" in glc)
    check("hwgl.c caps the texture by the max size query",
          "GL_MAX_TEXTURE_SIZE" in glc)
    check("the hwgl file rides the crlf discipline",
          "\r\n" in glc)

    # 4. the direct3d renderer module.
    check("hwd3d.c loads d3d9.dll dynamically",
          'LoadLibraryA("d3d9.dll")' in d3dc and
          "Direct3DCreate9" in d3dc)
    check("hwd3d.c preserves the fpu (the gdi+ contract)",
          "D3DCREATE_FPU_PRESERVE" in d3dc)
    check("hwd3d.c draws pretransformed quads",
          "D3DFVF_XYZRHW" in d3dc and
          "D3DPT_TRIANGLESTRIP" in d3dc and
          "DrawPrimitiveUP" in d3dc)
    check("hwd3d.c rides managed textures and clamp addressing",
          "D3DPOOL_MANAGED" in d3dc and
          "D3DTADDRESS_CLAMP" in d3dc)
    check("hwd3d.c answers the caps before the npot decision",
          "D3DPTEXTURECAPS_POW2" in d3dc and
          "D3DPTEXTURECAPS_NONPOW2CONDITIONAL" in d3dc)
    check("hwd3d.c rides the device-lost discipline",
          "D3DERR_DEVICELOST" in d3dc and
          "TestCooperativeLevel" in d3dc and
          "D3DERR_DEVICENOTRESET" in d3dc)
    check("the hwd3d file rides the crlf discipline",
          "\r\n" in d3dc)

    # 5. the renderer radio trio.
    check("the renderer submenu joins the view menu",
          "_VIV_MENU_VIEW_RENDERER" in stateh and
          "LOCALIZATION_ID_RENDERER,MF_POPUP" in vivc)
    check("the renderer items ride radio checks in the command table",
          vivc.count("|MFT_RADIOCHECK,_VIV_MENU_VIEW_RENDERER,VIV_ID_VIEW_RENDERER_") == 3)
    check("the radios check against the config in _viv_check_menus",
          menu.count("VIV_ID_VIEW_RENDERER_") >= 3 and
          "config_renderer == CONFIG_RENDERER_GDI" in menu)
    check("the command dispatch lands the trio and resets both modules",
          "VIV_ID_VIEW_RENDERER_GDI:" in view and
          "_viv_hwgl_shutdown();" in view and
          "_viv_hwd3d_shutdown();" in view)
    check("the kill path and the dispatch release both renderers (the r70 splice counts viv.c plus every viv_*.c)",
          vivc.count("_viv_hwgl_shutdown();") == 2 and
          vivc.count("_viv_hwd3d_shutdown();") == 2)

    # 6. the config wiring.
    check("config.h defines the renderer ladder and the toolbar mask",
          "#define CONFIG_RENDERER_GDI " in configh and
          "#define CONFIG_RENDERER_OPENGL " in configh and
          "#define CONFIG_RENDERER_DIRECT3D " in configh and
          "extern int config_renderer;" in configh and
          "extern int config_toolbar_groups;" in configh)
    check("config.c defaults gdi and the full toolbar",
          "config_renderer = CONFIG_RENDERER_GDI;" in configc and
          "config_toolbar_groups = 0x3f;" in configc)
    check("the ini carries both new keys read and write",
          configc.count('"renderer"') == 2 and
          configc.count('"toolbar_groups"') == 2)

    # 7. the toolbar customization.
    check("the toolbar measure honors the group mask before the overflow",
          toolbar.index("config_toolbar_groups & (1 <<") <
          toolbar.index("the overflow: whole groups hide from the right"))
    check("the toolbar owns a context menu",
          "case WM_CONTEXTMENU:" in toolbar and
          "_viv_toolbar_context_menu" in toolbar)
    check("the toolbar popup ids ride their own range",
          "#define _VIV_TOOLBAR_CONTEXT_ID_FIRST 0x6f00" in toolbarh)
    check("the toolbar context command exports to the wndproc interceptor",
          "void _viv_toolbar_context_command(int command_id);" in toolbarh and
          "_viv_toolbar_context_command(command_id)" in wnd)
    check("the mask starts at every group visible",
          "0x3f" in toolbar)

    # 8. the paint path takes the hardware branch before the backbuffer.
    check("wm_paint tries the hardware renderers before the gdi backbuffer",
          wnd.index("_viv_hw_render_frame(hwnd,ps.hdc") <
          wnd.index("_viv_paint_begin(ps.hdc"))
    check("the hardware branch reads the view math the gdi path uses",
          "_viv_get_render_size(&rw,&rh);" in wnd and
          "config_renderer != CONFIG_RENDERER_GDI" in wnd)
    check("the brush color hoists above both paths",
          wnd.count("COLORREF brush_color;") == 1)

    # 9. the build wiring.
    check("the zig file list carries the four new units",
          "src/hwgl.c" in files_txt and
          "src/hwd3d.c" in files_txt and
          "src/shellmenu.c" in files_txt and
          "src/viv_export.c" in files_txt and
          files_txt.count("\n") == 104)
    check("the shared props list carries all six new files",
          props.count('Include="..\\src\\hwgl.c"') == 1 and
          props.count('Include="..\\src\\hwd3d.c"') == 1 and
          props.count('Include="..\\src\\shellmenu.c"') == 1 and
          props.count('Include="..\\src\\hwgl.h"') == 1 and
          props.count('Include="..\\src\\hwd3d.h"') == 1 and
          props.count('Include="..\\src\\shellmenu.h"') == 1)
    check("viv.h carries the three new module headers",
          '#include "hwgl.h"' in vivh and
          '#include "hwd3d.h"' in vivh and
          '#include "shellmenu.h"' in vivh)

    # 10. the changelog, the readme and the version (the fixture round
    #     took the candidate slot and the top changelog entry; the
    #     closure round rides one line in the readme list below).
    check("the changelog carries the todo closure pre-release (below the fixture round)",
          "Pre-release: Version 1.1.14-rc.1 (the todo closure round)" in changes)
    check("the readme carries the closure round as a one-liner (demoted from the candidate slot)",
          "**1.1.14-rc.1** \u2014" in experience)
    check("the version is 1.1.15-rc.16 build 95 (the navigation visibility round pins ride it)",
          '#define VERSION_BUILD 95' in version and
          '#define VERSION_STRING "1.1.15-rc.16"' in version)

def t_field_sweep_round93():
    """Guards for the field sweep round (1.1.13-rc.7: the cold-start
    slideshow dispatch fix, the startup shortcut retirement, the widened
    no-image menu gate, the single keyboard-only focus ring and the legacy
    pile sweep)."""
    settings = read("src/viv_settings.c").decode("latin-1")
    toolbar = read("src/viv_toolbar.c").decode("latin-1")
    menu = read("src/viv_menu.c").decode("latin-1")
    loch = read("src/localization.h").decode("latin-1")
    locen = read("src/localization_en_us.h").decode("utf-8", errors="replace")
    loczh = read("src/localization_zh_cn.h").decode("utf-8", errors="replace")
    vivc = read("src/viv.c").decode("latin-1")
    view = read("src/viv_view.c").decode("latin-1")
    osc = read("src/os.c").decode("latin-1")
    dialogs = read("src/viv_dialogs.c").decode("latin-1")
    changes = read("Changes.txt").decode("utf-8", errors="replace")
    readme = read("README.md").decode("utf-8", errors="replace")
    experience = read("experience.md").decode("utf-8", errors="replace")
    version = read("src/version.h").decode("latin-1")

    # 1. the startup shortcut retires everywhere it lived.
    for dead in ("LOCALIZATION_ID_SETTINGS_STARTUP_SHORTCUT",):
        pat = re.compile(dead + r"(?![A-Z0-9_])")
        check("localization drops %s" % dead,
              pat.search(loch) is None and pat.search(locen) is None and
              pat.search(loczh) is None)
    check("the settings row id is gone", "_VIV_SETTINGS_ID_RUNKEY" not in settings)
    check("the run key writer and reader are gone",
          "_viv_settings_run_key_set" not in settings and
          "_viv_settings_run_key_present" not in settings)
    check("the retire helper replaces them",
          settings.count("static void _viv_settings_run_key_retire(void)") == 2)
    check("the open pass calls the retire once",
          settings.count("_viv_settings_run_key_retire();") == 1)
    check("the run key constants stay (the retire reads them)",
          "_VIV_SETTINGS_RUN_KEY_PATH" in settings and
          "_VIV_SETTINGS_RUN_KEY_VALUE" in settings)
    check("the section survives with its new name",
          "LOCALIZATION_ID_SETTINGS_SECTION_STARTUP" in loch and
          '"System integration"' in locen and
          "\u7cfb\u7edf\u96c6\u6210" in loczh)
    check("the allow multiple row survives the surgery",
          "_viv_settings_ctl_add(_VIV_SETTINGS_CT_SWITCH,_VIV_SETTINGS_ID_MULTIPLE" in settings)

    # 2. the cold-start fix: the play slot gates its animation branch on
    #    frames (the rc.13 face rule reaches the dispatch). round-125:
    #    the paused state answers the resume click now - the dispatch
    #    reads the frame count alone and the face rule (which keeps
    #    the play term to show the play face) stays a face rule.
    check("the toolbar play dispatch carries the frame gate",
          toolbar.count("else if (_viv_slot_current.frame_count > 1)") == 1 and
          toolbar.count("playing = ((_viv_is_slideshow) || ((_viv_slot_current.frame_count > 1) && (_viv_animation_play)))") == 1)

    # 3. the widened no-image gate.
    check("the animation gate variable exists",
          "UINT is_animation_enabled" in menu)
    check("the pause item rides the live gate",
          menu.count("\tEnableMenuItem(hmenu,VIV_ID_SLIDESHOW_PAUSE,is_image_enabled);") == 1)
    check("the navigation pair gates on the neighbor rule (the fourth audit moved it off the bare image gate)",
          "EnableMenuItem(hmenu,VIV_ID_NAV_PREV,is_nav_enabled);" in menu and
          "EnableMenuItem(hmenu,VIV_ID_NAV_NEXT,is_nav_enabled);" in menu)
    check("the window sizing and zoom family gate on the image",
          "EnableMenuItem(hmenu,VIV_ID_VIEW_WINDOW_SIZE_AUTO_FIT,is_image_enabled);" in menu and
          "EnableMenuItem(hmenu,VIV_ID_VIEW_ZOOM_IN,is_image_enabled);" in menu and
          "EnableMenuItem(hmenu,VIV_ID_VIEW_BESTFIT,is_image_enabled);" in menu)
    check("the animation family gates on frames",
          "EnableMenuItem(hmenu,VIV_ID_ANIMATION_PLAY_PAUSE,is_animation_enabled);" in menu and
          "EnableMenuItem(hmenu,VIV_ID_ANIMATION_FRAME_STEP,is_animation_enabled);" in menu)
    check("the duplicate 1to1 check retires",
          menu.count("CheckMenuItem(hmenu,VIV_ID_VIEW_1TO1,") == 1)

    # 4. the focus ring: one ring, keyboard only.
    check("the keyboard flag exists and the paint gates on it",
          "static BYTE _viv_settings_focus_keyboard = 0;" in settings and
          "focus = ((_viv_settings_focus == i) && (_viv_settings_focus_keyboard)) ? 1 : 0;" in settings)
    check("the row ring is gone (the paint loop no longer rings ctl->rect)",
          "the keyboard focus ring (2 dip accent stroke)" not in settings and
          "_viv_settings_draw_focus_ring(mem,&ctl->rect)" not in settings)
    check("the control ring survives (the switch still rings its capsule)",
          "_viv_settings_draw_focus_ring(hdc,&ctl->value);" in settings)

    # 5. the legacy sweep.
    for gone in ("_viv_is_alt", "_viv_tooltip", "_viv_low_priority_paint",
                 "_viv_fd_t"):
        check("%s is gone from the tree" % gone,
              gone not in vivc and gone not in view and
              gone not in read("src/viv_render.c").decode("latin-1") and
              gone not in read("src/viv_chrome.c").decode("latin-1") and
              gone not in read("src/viv_wndproc.c").decode("latin-1") and
              gone not in read("src/viv_state.h").decode("latin-1"))
    check("the upstream todo pile is closed (the todo closure round retired the last four items)",
          "the upstream list is closed" in vivc and
          vivc.count("// - OpenGL renderer.") == 0 and
          vivc.count("// - Direct3D renderer.") == 0 and
          "CDefFolderMenu_Create2" in vivc)
    check("the dead zoom-clamp copies are gone",
          view.count("_viv_zoom_pos == 1") == 0 and
          read("src/viv_wndproc.c").decode("latin-1").count("_viv_zoom_pos == 1") == 0)
    check("the webp rgba corpse is gone",
          "convert RGBA to BGRA" not in read("src/webp.c").decode("latin-1"))
    check("the dead process-name fixme retires",
          "we should check for the process name" not in
          read("src/viv_install.c").decode("latin-1"))
    check("the dead wait fixme retires",
          "FIXME: we need to wait for image to load." not in view)

    # 6. the deliberate keeps.
    check("the real shortcut-resolver todo stays",
          "TODO: resolve shortcuts" in osc)
    check("the real preload review todo stays",
          "//TODO: review -when enabled, viv fills unresponsive/sluggish." in view)
    # r114: the frozen options family left (see the dead residue round).
    check("the brace flattening stays deferred (the bare braces keep their lines)",
          read("src/viv_load.c").decode("latin-1").count(
              "if (!_viv_load_image_terminate)") == 2)

    # 6.5 the icon payload diet (the size complaint's top lever): the four
    #     large frames ride png payloads (vista+), the six small frames
    #     stay raw and byte-identical (the pre-vista fallback ladder).
    import struct
    ico = read("res/voidImageViewer.ico")
    n_ico = struct.unpack("<H", ico[4:6])[0]
    png_frames = 0
    raw_frames = 0
    for i_ico in range(n_ico):
        d_ico = ico[6 + i_ico * 16:6 + (i_ico + 1) * 16]
        w_ico = d_ico[0] or 256
        sz_ico = struct.unpack("<I", d_ico[8:12])[0]
        off_ico = struct.unpack("<I", d_ico[12:16])[0]
        pl_ico = ico[off_ico:off_ico + sz_ico]
        if w_ico >= 48:
            if pl_ico[:8] == b"\x89PNG\r\n\x1a\n":
                png_frames += 1
        elif pl_ico[:4] == b"\x28\x00\x00\x00":
            raw_frames += 1
    check("the four large frames ride png payloads",
          n_ico == 10 and png_frames == 4 and raw_frames == 6 and
          len(ico) < 90000)

    # 7. the version and the readme slot.
    check("the version is 1.1.15-rc.16 build 95",
          '#define VERSION_BUILD 95' in version and
          '#define VERSION_STRING "1.1.15-rc.16"' in version)
    check("the current line is the stable promotion stable",
          "**1.1.14 \u2014 the current stable**" in readme)
    check("the rc.6 entry rides the one-line list",
          "**1.1.13-rc.6** \u2014" in experience and
          "**1.1.13-rc.6 \u2014" not in readme)

    # 8. the changelog carries the round.
    flat = " ".join(changes.split())
    check("the changelog states the field sweep round",
          "the field sweep round" in flat)
    check("the changelog states the retirement and the reason",
          "the startup shortcut retires" in flat and
          "no business in the boot path" in flat)


def t_fixture_round98():
    """Guards for the fixture round (1.1.14-rc.2: the test sample set
    commits - the anomaly fixtures and the real imagery fixtures ride
    the tree so the smoke sweep's coverage only grows, the generation
    path the ci owned all along becomes the regen oracle)."""
    import shutil
    import struct
    import subprocess
    import tempfile

    root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    samples = os.path.join(root, "tests", "samples")

    version = read("src/version.h").decode("latin-1")
    changes = read("Changes.txt").decode("utf-8", errors="replace")
    readme = read("README.md").decode("utf-8", errors="replace")
    experience = read("experience.md").decode("utf-8", errors="replace")
    gitignore = read(".gitignore").decode("latin-1")
    files_txt = read("build-zig/files.txt").decode("latin-1")
    props = read("voidImageViewer.files.props").decode("latin-1")
    tests_yml = read(".github/workflows/tests.yml").decode("latin-1")
    release_yml = read(".github/workflows/release.yml").decode("latin-1")
    smoke = read("tests/smoke_test.ps1").decode("latin-1")
    fixture_gen = read("tests/make_fixture_samples.py").decode("latin-1")
    media_gen = read("tools/gen_media_samples.py").decode("latin-1")
    qoi_src = read("src/qoi.c").decode("latin-1")

    # 1. the sample set commits: 46 files (38 anomalies + 8 fixtures).
    fx_names = [
        "fx_anim_bounce.gif", "fx_anim_fade.gif", "fx_still_rgba.png",
        "fx_still_24bpp.bmp", "fx_still_qoi_rgb.qoi",
        "fx_still_qoi_rgba.qoi", "fx_still_photo.jpg",
        "fx_anim_pulse.webp", "fx_still_webp_odd.webp",
        "fx_still_qoi_sliver.qoi", "fx_still_textured.png",
    ]
    present = sorted(n for n in os.listdir(samples)
                     if os.path.isfile(os.path.join(samples, n))) \
        if os.path.isdir(samples) else []
    check("the sample directory carries exactly 51 files (40 anomalies + 11 fixtures)",
          len(present) == 51, "%d files" % len(present))
    check("the eleven fixtures are all present by name",
          all(n in present for n in fx_names),
          ", ".join(n for n in fx_names if n not in present))
    check("the over budget canvas still rides the sweep (the stage 3 leg)",
          "35_png_over_budget_110mp.png" in present)
    total = sum(os.path.getsize(os.path.join(samples, n)) for n in present)
    check("the sample set stays under 2.5 mb (the budget canvas and the frame-count sample are the heavyweights)",
          total < 2500000, "%d bytes" % total)

    # 2. the gitignore flip: the samples used to be ci-time generated,
    #    the ignore line kept them out of the tree - it retires now.
    check("the gitignore no longer ignores tests/samples",
          "tests/samples/" not in gitignore)
    check("the gitignore keeps the one-shot patch pattern (the media generator must not match it)",
          "tools/r*_*.py" in gitignore and
          not re.match(r"r\d", "gen_media_samples.py"))

    # 3. the anomaly set regenerates byte-identical (the stdlib
    #    determinism the ci generation path owned all along).
    tmp = tempfile.mkdtemp(prefix="viv_anom_")
    try:
        r = subprocess.run(
            [sys.executable,
             os.path.join(root, "tests", "make_anomaly_samples.py"), tmp],
            capture_output=True, text=True)
        check("the anomaly generator self check passes on regeneration",
              r.returncode == 0,
              (r.stdout.strip().splitlines() or ["no output"])[-1])
        mismatch = []
        for n in sorted(os.listdir(tmp)):
            want = open(os.path.join(tmp, n), "rb").read()
            have_path = os.path.join(samples, n)
            have = open(have_path, "rb").read() \
                if os.path.isfile(have_path) else None
            if have != want:
                mismatch.append(n)
        check("all 40 anomaly samples regenerate byte-identical",
              not mismatch, ", ".join(mismatch))
    finally:
        shutil.rmtree(tmp, ignore_errors=True)

    # 4. the fixture generator: stdlib discipline (the ci path may run
    #    it), self checking, and its six regenerate byte-identical.
    check("the fixture generator keeps the stdlib discipline (no pillow on the ci path)",
          "from PIL" not in fixture_gen and
          "import zlib" in fixture_gen and
          "import struct" in fixture_gen)
    check("the fixture generator carries its own oracle (lzw mirror + qoi port)",
          "def lzw_decode(" in fixture_gen and
          "def qoi_decode(" in fixture_gen and
          "def self_check(" in fixture_gen)
    tmp2 = tempfile.mkdtemp(prefix="viv_fx_")
    try:
        r = subprocess.run(
            [sys.executable,
             os.path.join(root, "tests", "make_fixture_samples.py"), tmp2],
            capture_output=True, text=True)
        check("the fixture generator self check passes on regeneration",
              r.returncode == 0,
              (r.stdout.strip().splitlines() or ["no output"])[-1])
        hand_encoded = [n for n in fx_names
                        if n not in ("fx_still_photo.jpg",
                                     "fx_anim_pulse.webp")]
        mismatch = [n for n in hand_encoded
                    if open(os.path.join(samples, n), "rb").read() !=
                    open(os.path.join(tmp2, n), "rb").read()]
        check("all nine hand-encoded fixtures regenerate byte-identical",
              not mismatch and len(hand_encoded) == 9, ", ".join(mismatch))
    finally:
        shutil.rmtree(tmp2, ignore_errors=True)

    # 5. the media fixtures pin by structure (the pil generator is an
    #    authoring tool; the ci path never runs it).
    check("the media generator declares itself the pillow authoring tool",
          "from PIL import Image" in media_gen and
          "authoring" in media_gen)
    jpg = open(os.path.join(samples, "fx_still_photo.jpg"), "rb").read()
    check("the jpeg fixture carries the jfif contract",
          jpg[:2] == b"\xff\xd8" and b"JFIF" in jpg[:32] and
          jpg[-2:] == b"\xff\xd9")

    def jpeg_dims(j):
        p = 2
        while p < len(j) - 9:
            if j[p] != 0xFF:
                p += 1
                continue
            m = j[p + 1]
            if m in (0xC0, 0xC1, 0xC2):
                h = struct.unpack(">H", j[p + 5:p + 7])[0]
                w = struct.unpack(">H", j[p + 7:p + 9])[0]
                return w, h
            if m == 0xD9 or 0xD0 <= m <= 0xD7 or m == 0x01:
                p += 2
                continue
            p += 2 + struct.unpack(">H", j[p + 2:p + 4])[0]
        return None

    check("the jpeg fixture parses to 96x64 through its own marker walk",
          jpeg_dims(jpg) == (96, 64), repr(jpeg_dims(jpg)))
    webp = open(os.path.join(samples, "fx_anim_pulse.webp"), "rb").read()
    riff_size = struct.unpack("<I", webp[4:8])[0]
    check("the webp fixture is the riff container with the extended format",
          webp[:4] == b"RIFF" and webp[8:12] == b"WEBP" and
          b"VP8X" in webp and b"ANIM" in webp and
          riff_size == len(webp) - 8)
    check("the webp fixture carries six animation frames",
          webp.count(b"ANMF") == 6)

    # 5c. the shape fixtures (the renderer parity round): the odd webp
    #     is the hand-encoded simple vp8l container - no vp8x wrapper,
    #     a non power of two 101x101 canvas - and the sliver is the
    #     extreme aspect through the long-proven qoi decoder.
    odd = open(os.path.join(samples, "fx_still_webp_odd.webp"), "rb").read()
    odd_riff = struct.unpack("<I", odd[4:8])[0]
    check("the odd webp fixture is the simple vp8l container (no vp8x)",
          odd[:4] == b"RIFF" and odd[8:12] == b"WEBP" and
          odd[12:16] == b"VP8L" and b"VP8X" not in odd and
          odd_riff == len(odd) - 8 and
          struct.unpack("<I", odd[16:20])[0] == len(odd) - 20)
    check("the odd webp header claims 101 wide (the non power of two shape)",
          odd[20] == 0x2f and
          ((struct.unpack("<I", odd[20:24])[0] >> 8) & 0x3fff) + 1 == 101)
    sliver = open(os.path.join(samples, "fx_still_qoi_sliver.qoi"), "rb").read()
    check("the sliver fixture claims the 1000x37 extreme aspect",
          sliver[:4] == b"qoif" and
          struct.unpack(">II", sliver[4:12]) == (1000, 37))

    # 5b. the qoi magic fix - the round's real catch: the format
    #     horizons round shipped the constant in little-endian spelling,
    #     so the big-endian reader refused every valid qoi file at the
    #     gate; the guard of that round pinned the wrong bytes and the
    #     suites stayed green over a decoder that never accepted a real
    #     file. the fixture round's real-byte host harness (the
    #     committed fixtures fed through a byte-identical copy of
    #     src/qoi.c compiled on the host) caught it; the fix rides this
    #     round and the harness now proves the pixels come back exact.
    check("the qoi magic reads big endian (the reference's word)",
          "0x716f6966" in qoi_src and
          "0x66696f71" not in qoi_src)
    check("the qoi source records the catch (the host harness story)",
          "real-byte host harness caught it" in qoi_src)
    check("the changelog records the catch",
          "the qoi magic constant" in changes)

    # 6. the sample files ride no build list (they are not translation
    #    units - the sweep opens them as data, the compilers never see
    #    them).
    check("the sample files ride no build list",
          "tests/samples" not in files_txt and
          "samples" not in props)

    # 7. the smoke sweep rides the committed set on both pipelines (the
    #    generation fallback goes dormant, the coverage only grows).
    check("the tests pipeline sweeps the committed samples",
          "smoke_test.ps1" in tests_yml and "tests\\samples" in tests_yml)
    check("the release pipeline sweeps the committed samples",
          "smoke_test.ps1" in release_yml and "tests\\samples" in release_yml)
    check("the smoke script keeps its samples dir parameter and its over_budget leg",
          "-SamplesDir" in smoke and "over_budget" in smoke)

    # 8. the changelog, the readme and the version (the navigation
    #     visibility round took the candidate slot and the top changelog
    #     entry; the fixture round rides one line in the readme list below).
    check("the changelog carries the fixture round pre-release (below the navigation visibility round)",
          "Pre-release: Version 1.1.14-rc.2 (the fixture round)" in changes)
    check("the readme carries the fixture round as a one-liner (demoted from the candidate slot)",
          "**1.1.14-rc.2** \u2014" in experience)
    check("the readme one-liner carries the todo closure round (demoted)",
          "**1.1.14-rc.1** \u2014" in experience)
    check("the version is 1.1.15-rc.16 build 95 (the navigation visibility round pins ride it)",
          '#define VERSION_BUILD 95' in version and
          '#define VERSION_STRING "1.1.15-rc.16"' in version)


def t_navigation_visibility_round99():
    """Guards for the navigation visibility round (1.1.14-rc.3: the field
    report - next/previous stopped answering for the formats the format
    horizons round opened. the folder scan, the playlist build and the
    drop enumeration all filtered candidates against the eleven-extension
    association table while the open filter and the everything prefixes
    had been widened to nineteen - the three format lists must widen as
    one set, and this guard is the cross-list oracle that keeps them so:
    the navigation visibility set must equal the open filter set must
    equal the search prefix set, forever)."""
    viv = read("src/viv.c").decode("latin-1")
    stateh = read("src/viv_state.h").decode("latin-1")
    view = read("src/viv_view.c").decode("latin-1")
    playlist = read("src/viv_playlist.c").decode("latin-1")
    version = read("src/version.h").decode("latin-1")
    changes = read("Changes.txt").decode("utf-8", errors="replace")
    readme = read("README.md").decode("utf-8", errors="replace")
    experience = read("experience.md").decode("utf-8", errors="replace")

    # 1. the supported-extension table exists beside the association table
    #    (the association set stays eleven - it is the installer contract;
    #    the navigation set is the viewer's own open universe).
    m = re.search(r"const char \*_viv_supported_extensions\[\]\s*=\s*\{(.*?)\};",
                  viv, re.S)
    supported = re.findall(r'"([a-z0-9]+)"', m.group(1)) if m else []
    check("the supported-extension table exists with all nineteen entries",
          len(supported) == 19, "%d entries: %s" % (len(supported), supported))
    check("the supported table carries the count assert",
          "_viv_supported_extensions_count_assert" in viv and
          "_VIV_SUPPORTED_EXTENSION_COUNT\t19" in stateh)
    check("the state layer exports the navigation set",
          "extern const char *_viv_supported_extensions[];" in stateh)

    # 2. the navigation filter answers the supported table, not the
    #    association table (the single-point fix: every consumer - the
    #    folder scan in _viv_next/_viv_home, the playlist build, the drop
    #    enumeration - heals through this one function).
    seg_at = playlist.rindex("int _viv_is_valid_filename")
    seg = playlist[seg_at:seg_at + 1600]
    check("_viv_is_valid_filename scans the supported-extension table",
          "_viv_supported_extensions" in seg and
          "_viv_association_extensions" not in seg)
    check("the navigation filter keeps the directory exclusion",
          "FILE_ATTRIBUTE_DIRECTORY" in seg)

    # 3. the cross-list oracle: the three format lists are one set. the
    #    open filter string and both everything prefixes are parsed and
    #    compared as sets against the supported table - a future round
    #    that widens one list and forgets another goes red right here.
    filt = re.search(r"\(\*\.(.*?)\)%c", view)
    open_set = set(x[2:] if x.startswith("*.") else x
                   for x in filt.group(1).split(";")) if filt else set()
    pre = re.findall(r"ext:([a-z0-9;]+?) <", playlist)
    prefix_set = set(pre[0].split(";")) if pre else set()
    check("the open filter and the everything prefix agree as sets",
          open_set == prefix_set and len(open_set) == 19,
          "open=%d prefix=%d" % (len(open_set), len(prefix_set)))
    check("the navigation visibility set equals the open filter set",
          set(supported) == open_set,
          "nav-only=%s open-only=%s" %
          (sorted(set(supported) - open_set), sorted(open_set - set(supported))))
    check("the eight format-horizons extensions are navigation-visible",
          {"avif", "dds", "hdp", "heic", "heif", "jxr", "qoi", "wdp"} <= set(supported))

    # 4. the version and the changelog (the corner and audit response
    #     round took the candidate slot and the top changelog entry; the
    #     navigation visibility round rides one line in the readme list
    #     below).
    check("the changelog carries the navigation visibility round pre-release (below the corner and audit response round)",
          "Pre-release: Version 1.1.14-rc.3 (the navigation visibility round)" in changes)
    check("the readme carries the navigation visibility round as a one-liner (demoted from the candidate slot)",
          "**1.1.14-rc.3** \u2014" in experience)
    check("the version is 1.1.15-rc.16 build 95 (the corner and audit response round pins ride it)",
          '#define VERSION_BUILD 95' in version and
          '#define VERSION_STRING "1.1.15-rc.16"' in version)


def t_audit_response_round101():
    """Guards for the corner and audit response round (1.1.14-rc.4: the
    fullscreen window answers the sharp corners and the custom caption
    pins its text color; the external audit of the tree lands with
    evidence first - its alpha and square-padding renderer claims retire
    against the load path's pre-flattening and the d3d caps branch, while
    its surviving findings land as code: the gl padding zero, the gl
    pixel format re-entry, the d3d same-dimension texture reuse, the
    experimental renderer labels, the long-path manifest claim, the
    installer's user-key probe and the fork's publisher line, and the
    msvc hardening line)."""
    osc = read("src/os.c").decode("latin-1")
    osh = read("src/os.h").decode("latin-1")
    chrome = read("src/viv_chrome.c").decode("latin-1")
    hwgl = read("src/hwgl.c").decode("latin-1")
    hwd3d = read("src/hwd3d.c").decode("latin-1")
    loc_en = read("src/localization_en_us.h").decode("latin-1")
    loc_zh = read("src/localization_zh_cn.h").decode("utf-8", errors="replace")
    manifest = read("res/voidImageViewer.Manifest").decode("latin-1")
    nsi = read("nsis/installer.nsi").decode("latin-1")
    install = read("src/viv_install.c").decode("latin-1")
    vcx = read("vs2019/voidImageViewer.vcxproj").decode("latin-1")
    version = read("src/version.h").decode("latin-1")
    changes = read("Changes.txt").decode("utf-8", errors="replace")
    readme = read("README.md").decode("utf-8", errors="replace")
    experience = read("experience.md").decode("utf-8", errors="replace")

    # 1. the corner optimization: the custom chrome set completes (the
    #    caption text follows the palette's own token) and the fullscreen
    #    transitions own the corner policy (a monitor-covering window
    #    answers the sharp corners - the round preference would clip the
    #    image at the four corners).
    check("the modern chrome signature carries the caption text color",
          "int os_window_modern_chrome(HWND hwnd,COLORREF caption_color,COLORREF text_color);" in osh)
    check("the modern chrome sets DWMWA_TEXT_COLOR (attribute 36)",
          "_os_DwmSetWindowAttribute(hwnd,36,&color,sizeof(color));" in osc)
    check("the corner helper exists for the fullscreen transitions",
          "void os_window_corner_round(HWND hwnd,int round);" in osh and
          "void os_window_corner_round(HWND hwnd,int round)" in osc and
          "corner = round ? 2 : 1;" in osc)
    enter = chrome.find("_viv_is_fullscreen = 1;")
    check("the fullscreen enter pins the sharp corners",
          enter != -1 and "os_window_corner_round(_viv_hwnd,0);" in chrome[enter:enter + 2500])
    leave = chrome.find("_viv_is_fullscreen = 0;")
    check("the windowed restore takes the round corners back",
          leave != -1 and "os_window_corner_round(_viv_hwnd,1);" in chrome[leave:leave + 2500])
    dark = chrome.find("void _viv_apply_dark_mode(int repaint)\r\n{")
    check("the dark re-apply carries the text color and the fullscreen corner guard",
          dark != -1 and
          "os_window_modern_chrome(_viv_hwnd,_viv_windowed_background(),viv_theme_color(VIV_TK_TEXT));" in chrome[dark:dark + 3000] and
          "os_window_corner_round(_viv_hwnd,0);" in chrome[dark:dark + 3000])

    # 2. the audit's surviving renderer findings land as code (and its
    #    two wrong claims retire on evidence: the load path pre-flattens
    #    the alpha onto the backdrop, so the hardware paths' opaque
    #    writes match the gdi semantics - the comment pins it).
    fn = hwgl[hwgl.find("static int _viv_gl_context_create"):hwgl.find("int _viv_hwgl_render")]
    check("the gl pixel format re-runs for a window change (outside the context guard)",
          "if (hwnd != _viv_gl_pixel_format_hwnd)" in fn and
          0 <= fn.find("if (hwnd != _viv_gl_pixel_format_hwnd)") < fn.find("if (!_viv_gl_context)"))
    check("the gl padding replicates the image edges (the round-101 zero retired)",
          "ZeroMemory(buf,size);" not in hwgl and
          "the pad must answer as if the texture border sat at the image" in hwgl and
          "for(x=wide;x<pot_wide;x++)" in hwgl)
    check("the gl upload records the pre-flattened alpha evidence",
          "pre-flattened" in hwgl)
    check("the d3d texture refills in place for same padded dimensions",
          "_viv_d3d_last_pot_wide" in hwd3d and
          "pot_wide != _viv_d3d_last_pot_wide" in hwd3d and
          hwd3d.count("_viv_d3d_last_pot_wide") >= 3)
    check("the d3d shutdown resets the reuse gate",
          "_viv_d3d_last_pot_wide = 0;" in hwd3d)

    # 3. the experimental labels, the long-path claim, the installer's
    #    user-key probe, the fork's publisher line.
    check("the renderer labels carry the experimental mark (en)",
          '"OpenGL (experimental)", // LOCALIZATION_ID_RENDERER_OPENGL' in loc_en and
          '"Direct3D (experimental)", // LOCALIZATION_ID_RENDERER_DIRECT3D' in loc_en)
    check("the renderer labels carry the experimental mark (zh)",
          '"OpenGL\uff08\u5b9e\u9a8c\u6027\uff09"' in loc_zh and
          '"Direct3D\uff08\u5b9e\u9a8c\u6027\uff09"' in loc_zh)
    check("the manifest claims long-path awareness",
          "<longPathAware>true</longPathAware>" in manifest)
    check("the installer probes the user uninstall key before the machine one",
          'ReadRegStr $R2 HKCU "SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\voidImageViewer"' in nsi and
          "IfErrors probe_hklm probe_done" in nsi and
          "probe_hklm:" in nsi)
    check("the publisher line carries the fork attribution",
          'L"voidImageViewer_PLUS (voidtools fork)"' in install)

    # 4. the msvc hardening line: sdl and control flow guard on every
    #    configuration, the safeseh opt-out retired, the buffer check
    #    back on everywhere.
    check("the msvc line compiles with sdl and control flow guard (all eight configs)",
          vcx.count("/sdl /guard:cf") == 8)
    check("the msvc line links the guard on every configuration",
          vcx.count("/GUARD:CF") == 8)
    check("the safeseh opt-out retires from the project file",
          "safeseh:no" not in vcx)
    check("the buffer security check rides every configuration",
          "<BufferSecurityCheck>false</BufferSecurityCheck>" not in vcx)

    # 4b. the sdl elevation catch: the older v143 analyzer flags two
    #     potentially-uninitialized pointer reads the newer v145 one
    #     accepts - both retire with null initializers (zero behavior
    #     change: the uses are guarded or debug-only).
    load_src = read("src/viv_load.c").decode("latin-1")
    check("the sdl elevation catches retire with null initializers",
          "void *bits = NULL;" in load_src and
          "void *image = NULL;" in load_src)

    # 5. the version, the changelog, the readme.
    top = changes.lstrip("\ufeff").split("\r\n")[0]
    check("the changelog carries the corner and audit response round pre-release (below the pixel oracle round)",
          "Pre-release: Version 1.1.14-rc.4 (the corner and audit response round)" in changes)
    check("the version is 1.1.15-rc.16 build 95",
          '#define VERSION_BUILD 95' in version and
          '#define VERSION_STRING "1.1.15-rc.16"' in version)
    check("the readme carries the corner and audit response round as a one-liner (demoted from the candidate slot)",
          "**1.1.14-rc.4** \u2014" in experience and
          "**1.1.14-rc.3** \u2014" in experience)
    check("the readme discloses the unsigned binaries",
          "binaries are unsigned" in readme)
    check("the readme's format line conditions the store-backed codecs",
          "HEIF/AVIF ride the store's image extensions" in readme)


def t_pixel_oracle_round102():
    """Guards for the pixel oracle round (1.1.14-rc.5: the deeper renderer
    fixes the rc.4 round carried in - the gl context rebuild on a window
    change and the pad edge replication both renderers - and the hidden
    render export, the pixel regression harness the external audit asked
    for: the real pipeline renders a sample, the exact pixels read back,
    and the ci hashes them against committed goldens)."""
    hwgl = read("src/hwgl.c").decode("latin-1")
    hwd3d = read("src/hwd3d.c").decode("latin-1")
    exp = read("src/viv_export.c").decode("latin-1")
    exph = read("src/viv_export.h").decode("latin-1")
    viv = read("src/viv.c").decode("utf-8", errors="replace")
    wnd = read("src/viv_wndproc.c").decode("latin-1")
    chrome = read("src/viv_chrome.c").decode("latin-1")
    version = read("src/version.h").decode()
    changes = read("Changes.txt").decode("utf-8", errors="replace")
    readme = read("README.md").decode("utf-8", errors="replace")
    experience = read("experience.md").decode("utf-8", errors="replace")

    # 1. the gl context rebuild: the wglMakeCurrent contract (the dc must
    #    answer the same device and the same pixel format the rc was
    #    created against - the msdn wording, second-verified) leaves the
    #    old rc unusable on a differently formatted dc; keeping it was the
    #    original refusal wearing a more hidden shell.
    fn = hwgl[hwgl.find("static int _viv_gl_context_create"):hwgl.find("int _viv_hwgl_render")]
    check("the gl context rebuilds when the window changes",
          "if (_viv_gl_context)" in fn and
          "_viv_gl_wglDeleteContext(_viv_gl_context);" in fn and
          "_viv_gl_texture = 0;" in fn and
          "_viv_gl_last_hbitmap = 0;" in fn)
    check("the gl rebuild lives inside the pixel format branch",
          fn.find("if (_viv_gl_context)") > fn.find("if (hwnd != _viv_gl_pixel_format_hwnd)") >= 0)
    # 2. the pad edge replication (both renderers): the linear sampler's
    #    half texel at the sub-rectangle edge reads the pad; clamp only
    #    answers at the texture's own border. the round-101 gl zero
    #    retired (a zeroed pad still averages black into the outermost
    #    destination pixels on a heavy upscale) and the d3d same-size
    #    reuse cannot bleed the previous frame back in.
    check("the gl pad replicates the last column and the last row",
          "for(x=wide;x<pot_wide;x++)" in hwgl and
          "for(y=high;y<pot_high;y++)" in hwgl and
          "ZeroMemory(buf,size);" not in hwgl)
    check("the d3d pad replicates the last column and the last row",
          "for(x=wide;x<pot_wide;x++)" in hwd3d and
          "for(y=high;y<pot_high;y++)" in hwd3d and
          "memcpy(base + (uintptr_t)y * (uintptr_t)locked.Pitch,last_row" in hwd3d)
    check("the d3d reuse gate comment tells the copy truth",
          "copy loop only rewrites the image's own texels" in hwd3d)

    # 3. the export module: the hidden switches, the deterministic pins,
    #    the readbacks, the exit codes.
    check("the export module exists and is registered",
          os.path.exists("src/viv_export.c") and os.path.exists("src/viv_export.h")
          and "viv_export.c" in read("build-zig/files.txt").decode()
          and "viv_export.c" in read("voidImageViewer.files.props").decode("utf-8-sig"))
    check("the export header carries the harness contract",
          "_VIV_EXPORT_DEFAULT_WIDE 640" in exph and
          "_VIV_EXPORT_TIMEOUT_MS 60000" in exph)
    check("the export probe answers before the install options and the mutex",
          "_viv_export_probe_command_line();" in viv and
          0 <= viv.find("_viv_export_probe_command_line();") < viv.find("_viv_process_install_command_line_options(GetCommandLineW())")
          and "if (!_viv_export_mode)" in viv)
    check("the export never redirects to a live single instance",
          "((!_viv_export_mode) && (!config_multiple_instances))" in viv)
    check("the export window stays hidden and forces its canvas",
          "(! _viv_export_mode)" in viv.replace(" ", "") or
          viv.count("(!_viv_export_mode)") >= 2)
    check("the export config pins answer before the first frame",
          "_viv_export_apply_config_pins();" in viv and
          "config_dark_mode = 0;" in exp and
          "config_icm = 0;" in exp and
          "config_orientation = 0;" in exp and
          "_viv_animation_play = 0;" in exp)
    check("the export switches stay out of the user help face",
          "render-export" in viv and
          "the output path never" in viv)
    check("the export run owns the main loop exit",
          "export_ret = _viv_export_run();" in viv and
          "return export_ret;" in viv)
    check("the exit codes are the harness contract",
          "return 2;" in exp and "return 3;" in exp and "return 4;" in exp and "return 5;" in exp)
    check("the export never rewrites the user's ini",
          "if ((!_viv_export_mode) && (_viv_recent_save_dirty))" in wnd)
    check("the gdi readback rides the paint backbuffer",
          "int _viv_paint_readback(BYTE *bits,int wide,int high)" in chrome and
          "GetDIBits(screen_hdc,_viv_paint_hbitmap" in chrome and
          "bmi.bmiHeader.biHeight = -high;" in chrome)
    check("the gl readback rides glreadpixels with the reserved byte pinned",
          "_viv_gl_readpixels" in hwgl and
          "d[x * 4 + 3] = 0;" in hwgl)
    check("the d3d readback rides getrendertargetdata through a system memory surface",
          "GetRenderTargetData" in hwd3d and
          "D3DPOOL_SYSTEMMEM" in hwd3d and
          "d[3] = 0;" in hwd3d)
    check("the export writes the classic bottom-up 32bpp bitmap",
          "file_header.bfType = 0x4d42;" in exp and
          "info_header.biBitCount = 32;" in exp and
          "for(y=high-1;y>=0;y--)" in exp)

    # 4. the version, the changelog, the readme.
    top = changes.lstrip("\ufeff").split("\r\n")[0]
    check("the changelog top entry is the field response round pre-release",
          top == "Pre-release: Version 1.1.15-rc.16 (the parallel evaluation round)", top)
    check("the version is 1.1.15-rc.16 build 95",
          '#define VERSION_BUILD 95' in version and
          '#define VERSION_STRING "1.1.15-rc.16"' in version)
    check("the readme candidate slot holds the navigation faces round",
          "**1.1.14 \u2014 the current stable**" in readme and
          "**1.1.14-rc.4** \u2014" in experience)
    flat_changes = " ".join(changes.split())
    check("the changelog states the wglmakecurrent contract wording",
          "the same device and the same pixel format" in flat_changes)
    check("the changelog states the texel row mapping",
          "[-0.5, n-0.5]" in changes)

    # 5. the carpet audit's fixes (the parallel round-102 sweep): the max
    #    clauses answer the non power of two overshoot and the square
    #    promote, the scene gate keeps a refused beginscene from hashing a
    #    blank frame, the format guard keeps a 16bpp desktop from walking
    #    garbage, the readbacks disarm, the background color pins (a
    #    developer machine's custom mat must not leak into the goldens)
    #    and the harness never leaks a wedged export.
    check("the gl max clause answers the pot overshoot",
          "(pot_wide > _viv_gl_max_texture)" in hwgl and
          "(pot_high > _viv_gl_max_texture)" in hwgl)
    check("the d3d max clauses answer the pot and square overshoots",
          "(pot_wide > _viv_d3d_max_wide)" in hwd3d and
          "(pot_high > _viv_d3d_max_high)" in hwd3d)
    check("the d3d readback gates on the scene and the format",
          "scene_ok" in hwd3d and
          "desc.Format == D3DFMT_X8R8G8B8" in hwd3d)
    check("the readbacks disarm after the fill",
          "_viv_gl_export_bits = 0;" in hwgl and
          "_viv_d3d_export_bits = 0;" in hwd3d)
    check("the export pins the windowed background",
          "config_windowed_background_color_r = 255;" in exp and
          "config_windowed_background_color_g = 255;" in exp and
          "config_windowed_background_color_b = 255;" in exp)
    golden_ps1 = read("tests/render_golden.ps1").decode("utf-8", errors="replace")
    check("the harness canvas carries the min-width headroom",
          '$size = "640x480"' in golden_ps1)
    check("the harness kills a wedged export and sweeps the tail",
          "the export hung (killed)" in golden_ps1 and
          "never leave a viewer behind" in golden_ps1)

    # 6. the export re-audit's catches: the renderer words must never reach
    #    the usage box (a modal message the hidden harness window could
    #    never dismiss), the fit gates pin, the animation pause re-asserts
    #    after the first-frame reply resets it, and the shutdowns clear
    #    the export pointers.
    check("the renderer words carry consumption cases (no usage box)",
          'string_icompare_lowercase_ascii(bufstart,"render-gdi")' in viv and
          'string_icompare_lowercase_ascii(bufstart,"render-gl")' in viv and
          'string_icompare_lowercase_ascii(bufstart,"render-d3d")' in viv and
          "a click that never comes" in viv)
    check("the export pins the fit gates",
          "config_allow_shrinking = 1;" in exp and
          "config_auto_zoom = 0;" in exp)
    check("the export re-asserts the animation pause before the paint",
          exp.count("_viv_animation_play = 0;") >= 2)
    check("the shutdowns clear the export pointers",
          "_viv_gl_export_bits = 0;" in hwgl and
          "_viv_d3d_export_bits = 0;" in hwd3d)


def t_audit_hardening_round103():
    """Guards for the budget and baseline round (1.1.14-rc.6: the working
    set and animation budgets with their own status-line reason, the loader
    stage telemetry ahead of the hard kill, the machine-verified toolchain
    security baseline, and the release trust chain - attestation, codeql,
    the libwebp verifier, the collaboration pack, archive-safe goldens)."""
    viv = read("src/viv.c").decode("latin-1")
    vivh = read("src/viv.h").decode()
    state = read("src/viv_state.h").decode()
    vivload = read("src/viv_load.c").decode("latin-1")
    webp = read("src/webp.c").decode("latin-1")
    wic = read("src/wic.c").decode("latin-1")
    qoi = read("src/qoi.c").decode("latin-1")
    chrome = read("src/viv_chrome.c").decode("latin-1")
    loc_h = read("src/localization.h").decode()
    loc_e = read("src/localization_en_us.h").decode("utf-8", errors="replace")
    loc_z = read("src/localization_zh_cn.h").decode("utf-8", errors="replace")
    wndproc = read("src/viv_wndproc.c").decode("latin-1")
    vcx26 = read("vs2026/voidImageViewer.vcxproj").decode("latin-1")
    tests_yml = read(".github/workflows/tests.yml").decode()
    release_yml = read(".github/workflows/release.yml").decode()
    golden_pg = read("tests/pixel_golden_test.py").decode()

    # 1. the budget regime: the byte ceilings ride the pixel ceilings, the
    #    animation axes are all bounded, and every decoder prices the same
    #    working-set estimate.
    check("the working set ceilings ride the pixel ceilings",
          "#define VIV_IMAGE_WORKING_SET_BYTES_PER_PIXEL\t12" in vivh and
          "#define VIV_MAX_IMAGE_BYTES\t2400000000" in vivh and
          "#define VIV_MAX_IMAGE_BYTES\t1200000000" in vivh)
    check("the animation ceilings carry frames and total bytes",
          "#define VIV_MAX_ANIMATION_FRAMES\t10000" in vivh and
          "#define VIV_MAX_ANIMATION_TOTAL_BYTES\t2000000000" in vivh and
          "#define VIV_MAX_ANIMATION_TOTAL_BYTES\t400000000" in vivh and
          "#define VIV_MAX_ANIMATION_PIXELS\t150000000" in vivh)
    check("every loader prices the working set (the 12 bytes per pixel estimate)",
          "(VIV_UINT64)pixels * VIV_IMAGE_WORKING_SET_BYTES_PER_PIXEL > VIV_MAX_IMAGE_BYTES" in vivload and
          "(VIV_UINT64)pixels * VIV_IMAGE_WORKING_SET_BYTES_PER_PIXEL > VIV_MAX_IMAGE_BYTES" in webp and
          "(VIV_UINT64)pixels * VIV_IMAGE_WORKING_SET_BYTES_PER_PIXEL > VIV_MAX_IMAGE_BYTES" in wic and
          "(VIV_UINT64)pixels * VIV_IMAGE_WORKING_SET_BYTES_PER_PIXEL > VIV_MAX_IMAGE_BYTES" in qoi)
    check("the working set refusal is diagnosable in every decoder",
          "working set budget: refusing a %u mp canvas" in vivload and
          "working set budget: refusing a %u mp canvas" in webp and
          "working set budget: refusing a %u mp canvas" in wic and
          "working set budget: refusing a %u mp canvas" in qoi)
    check("the gdi+ frame array is gated before the frame loop",
          "if (_viv_animation_budget_refused(first_frame.frame_count,safe_size_mul((SIZE_T)first_frame.wide,(SIZE_T)first_frame.high)))" in vivload and
          "fails like any other unloadable file" in vivload)
    check("the webp frame array is gated with the canvas",
          "(!_animation_budget_refused(anim_info.frame_count,safe_size_mul((SIZE_T)anim_info.canvas_width,(SIZE_T)anim_info.canvas_height)))" in webp)
    check("the animation gates price frames and total bytes",
          "(VIV_UINT64)frame_count * (VIV_UINT64)canvas_pixels * 16 / 3 > VIV_MAX_ANIMATION_TOTAL_BYTES" in vivload and
          "(VIV_UINT64)frame_count * (VIV_UINT64)canvas_pixels * 16 / 3 > VIV_MAX_ANIMATION_TOTAL_BYTES" in webp and
          "frame_count > VIV_MAX_ANIMATION_FRAMES" in vivload and
          "frame_count > VIV_MAX_ANIMATION_FRAMES" in webp)

    # 2. the refusal reason reaches the user: the flag is shared state with
    #    a cleared dispatch, every refusal marks it, and the status line has
    #    its own localized string for it.
    # r114: the refusal flags ride the interlocked forms (the same rule
    # the rc.5 cancel flag took - the arm legs carry no barrier on plain
    # volatile reads).
    check("the refusal flag is shared state with a cleared dispatch",
          "extern volatile LONG _viv_load_refused_budget;" in state and
          "volatile LONG _viv_load_refused_budget = 0;" in viv and
          "_VIV_LOAD_REFUSED_CLEAR(_viv_load_refused_budget);" in vivload and
          "#define _VIV_LOAD_REFUSED_CLEAR(flag) InterlockedExchange(&(flag),0)" in state)
    check("every budget refusal marks the flag",
          vivload.count("_VIV_LOAD_REFUSED_SET(_viv_load_refused_budget);") == 4 and
          webp.count("_VIV_LOAD_REFUSED_SET(_viv_load_refused_budget);") == 4 and
          wic.count("_VIV_LOAD_REFUSED_SET(_viv_load_refused_budget);") == 2 and
          qoi.count("_VIV_LOAD_REFUSED_SET(_viv_load_refused_budget);") == 2)
    check("the status line carries the budget reason",
          "if (_VIV_LOAD_REFUSED_READ(_viv_load_refused_budget))" in chrome and
          "LOCALIZATION_ID_STATUS_BAR_IMAGE_OVER_BUDGET" in chrome and
          "LOCALIZATION_ID_STATUS_BAR_IMAGE_OVER_BUDGET" in loc_h and
          "LOCALIZATION_ID_STATUS_BAR_IMAGE_OVER_BUDGET" in loc_e and
          "LOCALIZATION_ID_STATUS_BAR_IMAGE_OVER_BUDGET" in loc_z)

    # 3. the hard exit is a recorded event: the stage ladder publishes
    #    through shared state, the timeout reports before exiting, and the
    #    one decoder with a hand-rolled pixel loop honors the cancel.
    check("the loader publishes its stage through shared state",
          "extern PVOID volatile _viv_load_stage;" in state and
          'PVOID volatile _viv_load_stage = (PVOID)"";' in viv)
    check("the thread proc walks the stage ladder",
          vivload.count('InterlockedExchangePointer(&_viv_load_stage,(PVOID)"') == 4 and
          webp.count('InterlockedExchangePointer(&_viv_load_stage,(PVOID)"webp");') == 1 and
          qoi.count('InterlockedExchangePointer(&_viv_load_stage,(PVOID)"qoi");') == 1 and
          wic.count('InterlockedExchangePointer(&_viv_load_stage,(PVOID)"wic");') == 1)
    check("the exit timeout reports the stage before the hard exit",
          'debug_printf("load thread timeout: exiting at stage %s (%S)\\n"' in viv and
          "InterlockedCompareExchangePointer(&_viv_load_stage,NULL,NULL)" in viv)
    check("the qoi decode honors the cooperative cancel",
          "if (_VIV_LOAD_TERMINATED())" in qoi and
          "the torn buffer never ships" in qoi)

    # 4. the toolchain baseline: the v145 project matches the v143 hardening
    #    line, the opt-outs retire, and the pe header check rides both
    #    pipelines so a dropped flag fails the build, not a user's machine.
    check("the v145 project compiles with the same hardening line (all eight configs)",
          vcx26.count("/sdl /guard:cf") == 8)
    check("the v145 project links the guard on every configuration",
          vcx26.count("/GUARD:CF") == 8)
    check("the v145 opt-outs retire",
          "safeseh:no" not in vcx26 and
          "<BufferSecurityCheck>false</BufferSecurityCheck>" not in vcx26)
    check("the window procedure includes viv_menu.h once",
          wndproc.count('#include "viv_menu.h"') == 1)
    pe_ps1 = read("tools/pe_security_check.ps1").decode("utf-8", errors="replace")
    check("the pe checker asserts the mitigation bits",
          "GUARD_CF" in pe_ps1 and "0x4000" in pe_ps1 and
          "DllCharacteristics" in pe_ps1 and "HIGH_ENTROPY_VA" in pe_ps1)
    check("both pipelines run the pe check after the build",
          "pe_security_check.ps1" in tests_yml and
          "pe_security_check.ps1" in release_yml and
          0 <= release_yml.find("Verify build output") < release_yml.find("pe_security_check.ps1"))
    check("the pe check passes its file list as a real array",
          '-Command "& tools\\pe_security_check.ps1 -ExePath' in tests_yml and
          '-Command "& tools\\pe_security_check.ps1 -ExePath' in release_yml and
          '-File tools\\pe_security_check.ps1 -ExePath "build\\x64' not in tests_yml and
          '-File tools\\pe_security_check.ps1 -ExePath "build\\x64' not in release_yml)

    # 5. the release trust chain: provenance attestation on every asset,
    #    codeql on the fork's own sources, the vendored verifier, and the
    #    archive-safe golden structure test.
    check("the publish job attests the build provenance",
          "actions/attest-build-provenance@4d101475d8b20a2381f78447822ac1eab6504dd8" in release_yml and
          "id-token: write" in release_yml and
          "attestations: write" in release_yml and
          "subject-path:" in release_yml)
    codeql_yml = read(".github/workflows/codeql.yml").decode()
    codeql_cfg = read(".github/codeql/config.yml").decode()
    check("the codeql workflow analyzes the fork's own sources",
          "github/codeql-action/init@4bd7200e1f146b1c937cae12d258b50f41a53cf8" in codeql_yml and
          "github/codeql-action/analyze@4bd7200e1f146b1c937cae12d258b50f41a53cf8" in codeql_yml and
          "config-file: ./.github/codeql/config.yml" in codeql_yml and
          "paths:" in codeql_cfg and "- src" in codeql_cfg)
    check("the golden structure test is archive-safe",
          'in_git_checkout = os.path.isdir(".git")' in golden_pg and
          "source archive, not a git checkout" in golden_pg)
    check("the vendored tree carries its offline verifier",
          os.path.exists("tools/update_libwebp.py") and
          "--check" in read("tools/update_libwebp.py").decode("utf-8", errors="replace"))
    check("the collaboration pack is in place",
          os.path.exists("SECURITY.md") and
          os.path.exists("CONTRIBUTING.md") and
          os.path.exists("CODEOWNERS") and
          os.path.exists(".github/dependabot.yml") and
          os.path.exists("THIRD_PARTY_NOTICES.md"))

    # 6. the budget anomaly samples ride the sweep (the generator self
    #    check pins their math; this pins their presence and the refusal
    #    naming that the smoke stage 3 glob expects).
    samples_dir = "tests/samples"
    check("the animation budget samples ride the sweep",
          os.path.isfile(os.path.join(samples_dir, "39_gif_anim_over_budget_frames.gif")) and
          os.path.isfile(os.path.join(samples_dir, "40_gif_anim_over_budget_bytes.gif")))
    gen = read("tests/make_anomaly_samples.py").decode()
    check("the generator prices the budget samples on both axes",
          "emit('39_gif_anim_over_budget_frames.gif', make_gif(8, 8, [1] * 12000))" in gen and
          "emit('40_gif_anim_over_budget_bytes.gif', make_gif(1200, 1200, [1] * 600))" in gen and
          "600 * 1200 * 1200 * 4 > 2000000000" in gen and
          "12000 > 10000" in gen)


def t_input_ceiling_round104():
    """Guards for the input ceiling round (1.1.14-rc.7: the second external
    audit's pre-release list - the whole-file read gains a byte ceiling
    before any allocation (GetFileSizeEx sees past 4 gb where the 32-bit
    form answered the low dword), the exit timeout takes the hard exit
    instead of the hard kill, the thread-creation failures unwind their
    own state, codeql grows the full attack-surface leg and the vendored
    verifier rides both pipelines for real)."""
    viv = read("src/viv.c").decode("utf-8", errors="replace")
    vivh = read("src/viv.h").decode()
    vivload = read("src/viv_load.c").decode("latin-1")
    state = read("src/viv_state.h").decode()
    chrome = read("src/viv_chrome.c").decode("latin-1")
    ini = read("src/ini.c").decode("latin-1")
    selfshot = read("src/viv_selfshot.c").decode("latin-1")
    loc = read("src/localization.h").decode()
    loc_en = read("src/localization_en_us.h").decode()
    loc_zh = read("src/localization_zh_cn.h").decode("utf-8")
    version = read("src/version.h").decode()
    changes = read("Changes.txt").decode("utf-8", errors="replace")
    readme = read("README.md").decode("utf-8", errors="replace")
    experience = read("experience.md").decode("utf-8", errors="replace")
    codeql_yml = read(".github/workflows/codeql.yml").decode()
    codeql_full = read(".github/codeql/config-full.yml").decode()
    tests_yml = read(".github/workflows/tests.yml").decode()
    release_yml = read(".github/workflows/release.yml").decode()
    smoke = read("tests/smoke_test.ps1").decode("latin-1")

    # 1. the input ceiling: the constants, the 64-bit size read, the
    #    refusal before the allocation, the status line reason.
    check("the input ceiling is defined for both pointer widths",
          "#define VIV_MAX_INPUT_FILE_BYTES\t1000000000" in vivh and
          "#define VIV_MAX_INPUT_FILE_BYTES\t512000000" in vivh and
          vivh.count("#define VIV_MAX_INPUT_FILE_BYTES") == 2)
    check("the loader reads its size through GetFileSizeEx",
          "static int _viv_input_size_refused(HANDLE h,LARGE_INTEGER *file_size)" in vivload and
          "GetFileSize(h,0);" not in vivload)
    check("the open gate refuses the size before the whole-file allocation",
          "if ((h != INVALID_HANDLE_VALUE) && (!_viv_input_size_refused(h,&file_size)))" in vivload and
          "size = (DWORD)file_size.QuadPart;" in vivload and
          0 <= vivload.find("_viv_input_size_refused") < vivload.find("GlobalAlloc(GMEM_MOVEABLE,size);"))
    check("the empty file and the short read both refuse with a record",
          'debug_printf("empty file\\n");' in vivload and
          'debug_printf("short read: %u of %u bytes (the file changed size or the media failed)\\n",size - totreadsize,size);' in vivload)
    check("the over-ceiling refusal marks the shared flag",
          "_VIV_LOAD_REFUSED_SET(_viv_load_refused_input_size);" in vivload and
          "extern volatile LONG _viv_load_refused_input_size;" in state and
          "volatile LONG _viv_load_refused_input_size = 0;" in viv)
    check("the input refusal flag clears wherever the budget flag clears",
          vivload.count("_VIV_LOAD_REFUSED_CLEAR(_viv_load_refused_input_size);") == 4 and
          vivload.count("_VIV_LOAD_REFUSED_CLEAR(_viv_load_refused_budget);") == 4)
    check("the status line names the input ceiling in both languages",
          "LOCALIZATION_ID_STATUS_BAR_INPUT_OVER_LIMIT," in loc and
          '"The file exceeds the input size limit.", // LOCALIZATION_ID_STATUS_BAR_INPUT_OVER_LIMIT,' in loc_en and
          loc_zh.count("LOCALIZATION_ID_STATUS_BAR_INPUT_OVER_LIMIT") == 1 and
          "if (_VIV_LOAD_REFUSED_READ(_viv_load_refused_input_size))" in chrome)
    check("the ini reader takes the same 64-bit form",
          "if ((GetFileSizeEx(h,&file_size)) && (file_size.QuadPart > 0) && (file_size.QuadPart <= 0x1000000))" in ini and
          "GetFileSize(h,0);" not in ini)

    # 2. the hard exit replaces the hard kill, and the thread creations
    #    answer their failures.
    check("TerminateThread is gone from the loader's exit path",
          "TerminateThread(_viv_load_image_thread" not in viv and
          "TerminateThread(" not in vivload)
    check("the exit timeout exits the process after the record",
          'debug_printf("load thread timeout: exiting at stage %s (%S)\\n"' in viv and
          0 <= viv.find("load thread timeout: exiting at stage") < viv.find("ExitProcess(1);"))
    check("the loader dispatch unwinds a failed thread creation",
          'debug_printf("CreateThread failed %x\\n",GetLastError());' in vivload and
          "mem_free(_viv_load_image_filename);" in vivload and
          "_viv_load_failed = 1;" in vivload)
    check("the self-shot harness shuts its gdi+ session on a failed thread creation",
          '_viv_self_log("thread_create_failed",0);' in selfshot and
          "if ((gdi_started) && (os_GdiplusShutdown))" in selfshot)

    # 3. the static surface: the full attack-surface leg, the repaired
    #    push trigger, the vendored verifier in both pipelines, the smoke
    #    stage 4 sparse file.
    check("the codeql push trigger targets main",
          "branches: [main]" in codeql_yml)
    check("the full attack surface leg watches src and libwebp",
          "analyze-full:" in codeql_yml and
          "config-file: ./.github/codeql/config-full.yml" in codeql_yml and
          "if: github.event_name != 'push'" in codeql_yml and
          'category: "/language:c-cpp:full"' in codeql_yml and
          "paths:" in codeql_full and "- src" in codeql_full and "- libwebp" in codeql_full)
    check("the vendored verifier rides both pipelines",
          "tools/update_libwebp.py --check" in tests_yml and
          "tools/update_libwebp.py --check" in release_yml)
    check("the smoke test opens a sparse over-ceiling file at run time",
          "stage 4: input ceiling" in smoke and
          "fsutil sparse setflag" in smoke and
          "SetLength($bigSize)" in smoke and
          "$bigSize = 4294967297" in smoke)

    # 4. the version, the changelog, the readme.
    top = changes.lstrip("\ufeff").split("\r\n")[0]
    check("the changelog top entry is the field response round pre-release",
          top == "Pre-release: Version 1.1.15-rc.16 (the parallel evaluation round)", top)
    check("the version is 1.1.15-rc.16 build 95",
          '#define VERSION_BUILD 95' in version and
          '#define VERSION_STRING "1.1.15-rc.16"' in version)
    check("the readme candidate slot holds the navigation faces round",
          "**1.1.14 \u2014 the current stable**" in readme and
          "**1.1.14-rc.7** \u2014" in experience)
    flat_changes = " ".join(changes.split())
    check("the changelog states the 32-bit size read's blind spot",
          "answers the low dword there" in flat_changes)
    check("the changelog states the hard exit reasoning",
          "no further teardown shares memory with a stuck thread" in flat_changes)


def t_renderer_parity_round105():
    """Guards for the renderer parity round (1.1.14-rc.8: the third
    external audit arrived with a heap-overflow claim in the gl upload
    loop; the mechanical brace-stack parse answered it - the increment
    sits inside the rgba branch, the claim does not survive, and the
    misindented closing braces that baited it retired - while the
    finding that did hold landed as code: the gdi+ frames answer as dib
    sections so the hardware renderers actually render every decoder
    family, the golden set gains the shape dimension, and the ceiling
    smoke stage proves the refusal through the export oracle)."""
    hwgl = read("src/hwgl.c").decode("latin-1")
    hwd3d = read("src/hwd3d.c").decode("latin-1")
    vivload = read("src/viv_load.c").decode("latin-1")
    vivrender = read("src/viv_render.c").decode("latin-1")
    version = read("src/version.h").decode()
    changes = read("Changes.txt").decode("utf-8", errors="replace")
    readme = read("README.md").decode("utf-8", errors="replace")
    experience = read("experience.md").decode("utf-8", errors="replace")
    smoke = read("tests/smoke_test.ps1").decode("latin-1")
    golden_ps1 = read("tests/render_golden.ps1").decode("latin-1")
    golden_pg = read("tests/pixel_golden_test.py").decode()
    fixture_gen = read("tests/make_fixture_samples.py").decode("latin-1")

    # 1. the audit's headline claim answered by structure, not eyeball:
    #    a real brace parser walks the upload loop and pins where the
    #    gutter increment sits (inside the rgba else, never the bgr
    #    if), and every closing brace in both hardware renderers sits
    #    at its opening's indentation depth - the misleading-indent
    #    bait that fooled a careful audit retires for good.
    def _c_strip(line):
        out = []
        i = 0
        instr = None
        while i < len(line):
            c = line[i]
            if instr:
                if c == instr:
                    instr = None
                i += 1
                continue
            if c in "\"'":
                instr = c
                i += 1
                continue
            if c == "/" and i + 1 < len(line) and line[i + 1] == "/":
                break
            out.append(c)
            i += 1
        return "".join(out)

    def _c_enclosing(src, needle):
        """the construct stack enclosing the first line carrying needle:
        a list of (opening line text, preceding non-blank text) pairs,
        outermost first."""
        lines = src.split("\n")
        idx = next((i for i, l in enumerate(lines) if needle in l), None)
        if idx is None:
            return None
        stack = []
        prev_text = ""
        for l in lines[:idx + 1]:
            clean = _c_strip(l)
            for ch in clean:
                if ch == "{":
                    stack.append((l.strip(), prev_text))
                elif ch == "}" and stack:
                    stack.pop()
            if l.strip():
                prev_text = l.strip()
        return stack

    gutter = _c_enclosing(hwgl, "d += (pot_wide - wide) * 4;")
    check("the gl upload's gutter increment sits inside the rgba else (the brace-stack answer to the overflow claim)",
          gutter is not None and
          len(gutter) == 4 and
          gutter[-1][1] == "else" and
          gutter[-2][1] == "for(y=0;y<high;y++)" and
          all("if (format == GL_BGR_EXT)" not in g[0] for g in gutter),
          repr(gutter[-1]) if gutter else None)
    bgr = _c_enclosing(hwgl, "d += pot_wide * 3;")
    check("the bgr row advance stays inside its own branch (the whole-pot stride)",
          bgr is not None and
          any(g[1] == "if (format == GL_BGR_EXT)" for g in bgr) and
          bgr[-1][1] == "if (format == GL_BGR_EXT)")
    stride = _c_enclosing(hwgl, "s += stride;")
    check("the source stride advance answers to the row loop, not a branch",
          stride is not None and
          any(g[1] == "for(y=0;y<high;y++)" for g in stride) and
          all(g[1] != "else" for g in stride))

    def _c_indent_clean(src):
        lines = src.split("\n")
        stack = []
        for l in lines:
            clean = _c_strip(l)
            tabs = len(l) - len(l.lstrip("\t"))
            for ch in clean:
                if ch == "{":
                    stack.append(tabs)
                elif ch == "}":
                    if stack and stack.pop() != tabs:
                        return False
        return True

    check("every closing brace in the hardware renderers sits at its opening's depth (the indent bait retired)",
          _c_indent_clean(hwgl) and _c_indent_clean(hwd3d))

    # 2. the renderer parity: the gdi+ frames, their thumbnails and the
    #    orientation copies answer the dib contract the hardware gates
    #    read; the gdi-only surfaces (clipboard copy, mipmaps, the gdi
    #    stretch temp) keep their own.
    check("the frame dib helper is the webp shape (24bpp top-down)",
          "static HBITMAP _viv_load_create_frame_dib(HDC dc,int wide,int high)" in vivload and
          "bmi.bmiHeader.biHeight = -high; // top-down, the webp frames' own shape" in vivload and
          "bmi.bmiHeader.biBitCount = 24;" in vivload and
          "return CreateDIBSection(dc,&bmi,DIB_RGB_COLORS,&bits,NULL,0);" in vivload)
    check("the gdi+ frame loop and the thumbnail both build dib sections",
          "hbitmap = _viv_load_create_frame_dib(screen_hdc,load_wide,load_high);" in vivload and
          "hbitmap = _viv_load_create_frame_dib(screen_hdc,(int)thumb_wide,(int)thumb_high);" in vivload and
          vivload.count("CreateCompatibleBitmap(") == 1)
    check("the orientation copies keep the dib contract (32bpp top-down, the native section write)",
          "ret_hbitmap = CreateDIBSection(screen_hdc,&bmi,DIB_RGB_COLORS,&ret_bits,NULL,0);" in vivrender and
          "os_copy_memory(ret_bits,new_pixels,safe_size_mul(safe_size_mul((SIZE_T)ret_wide,(SIZE_T)ret_high),sizeof(DWORD)));" in vivrender and
          "SetDIBits(" not in vivrender and
          vivrender.count("CreateCompatibleBitmap(") == 2)
    check("the renderer refusals name their reason (the silent zero retired)",
          'debug_printf("opengl: the frame is not a dib section (no bits answered) - the gdi path paints it\\r\\n");' in hwgl and
          'debug_printf("direct3d: the frame is not a dib section (no bits answered) - the gdi path paints it\\r\\n");' in hwd3d and
          'debug_printf("opengl: the frame is %d bpp (24 or 32 answer) - the gdi path paints it\\r\\n",ds.dsBm.bmBitsPixel);' in hwgl and
          'debug_printf("direct3d: the frame is %d bpp (24 or 32 answer) - the gdi path paints it\\r\\n",ds.dsBm.bmBitsPixel);' in hwd3d)

    # 3. the shape dimension: the fixtures, the golden wiring, and the
    #    vp8l writer's own rules.
    check("the fixture generator carries the hand-rolled vp8l writer and its mirror",
          "def make_still_webp(width, height, pixel_fn):" in fixture_gen and
          "def vp8l_decode_still(data):" in fixture_gen and
          "FIXTURE_COUNT = 9" in fixture_gen)
    check("the vp8l writer pins the vendored decoder's own rules (canonical order, zero-bit single codes)",
          "sorted((VP8L_WEAVE_A[1], VP8L_WEAVE_B[1]))" in fixture_gen and
          "value takes code 0" in fixture_gen and
          "transform: absent" in fixture_gen)
    check("the shape fixtures are generated at their shapes",
          "make_still_webp(101, 101, webp_weave_pixel)" in fixture_gen and
          "make_qoi(1000, 37, 3, sliver_pixel)" in fixture_gen)
    check("the golden set rides the shape dimension",
          '"fx_still_webp_odd.webp"' in golden_ps1 and
          '"fx_still_qoi_sliver.qoi"' in golden_ps1 and
          "two defects can" in golden_ps1)
    check("the pixel golden suite pins the no-null manifest (the parity round's own oracle)",
          "whole decoder families were being refused" in golden_pg)

    # 4. the ceiling smoke stage proves the refusal through the export.
    check("the ceiling stage runs the export oracle and demands the refusal",
          '"(input ceiling export)"' in smoke and
          '($probe.ExitCode -eq 2) -and (-not (Test-Path $probeBmp))' in smoke and
          '"-render-gdi", "-render-size", "640x480", "-render-export",' in smoke and
          "the export hung on the over-ceiling file" in smoke)

    # 5. the version, the changelog, the readme.
    top = changes.lstrip("\ufeff").split("\r\n")[0]
    check("the changelog top entry is the field response round pre-release",
          top == "Pre-release: Version 1.1.15-rc.16 (the parallel evaluation round)", top)
    check("the version is 1.1.15-rc.16 build 95",
          '#define VERSION_BUILD 95' in version and
          '#define VERSION_STRING "1.1.15-rc.16"' in version)
    check("the readme candidate slot holds the navigation faces round",
          "**1.1.14 \u2014 the current stable**" in readme and
          "**1.1.14-rc.7** \u2014" in experience)
    flat_changes = " ".join(changes.split())
    check("the changelog states the brace-stack verdict",
          "the claim does not survive verification" in flat_changes and
          "what was real was the bait" in flat_changes)
    check("the changelog discloses the exit timeout's ini cost",
          "costs the unsaved settings of that session" in flat_changes)
    check("the readme carries the attestation verification line",
          "gh attestation verify" in readme)


def t_navigation_faces_round106():
    """Guards for the navigation faces round (1.1.14-rc.9: the field
    report - the toolbar's prev/next faces sat permanently gray on a
    plain open. the enable rule read _viv_nav_item_count, a cache only
    the jump-to dialog ever fills (its WM_INITDIALOG folder scan), while
    the navigation itself walks two other paths entirely - the playlist
    when one is loaded, and a live FindFirstFile folder scan in single
    file mode. the faces now answer the same question the navigation
    walks: the playlist branch reads the playlist's own items, the
    single file branch reads a folder fact the _viv_next scan records
    (the preload runs that scan after every load), and the jump-to
    cache goes back to being the dialog's own listing)."""
    toolbar = read("src/viv_toolbar.c").decode("latin-1")
    view = read("src/viv_view.c").decode("latin-1")
    vivload = read("src/viv_load.c").decode("latin-1")
    stateh = read("src/viv_state.h").decode("latin-1")
    viv = read("src/viv.c").decode("latin-1")
    menu = read("src/viv_menu.c").decode("latin-1")
    dialogs = read("src/viv_dialogs.c").decode("latin-1")
    version = read("src/version.h").decode()
    changes = read("Changes.txt").decode("utf-8", errors="replace")
    readme = read("README.md").decode("utf-8", errors="replace")
    experience = read("experience.md").decode("utf-8", errors="replace")

    # 1. the root cause retires: the toolbar must not read the jump-to
    #    dialog's cache - a variable only _viv_jumpto_proc's
    #    WM_INITDIALOG scan ever fills. on a plain open (single file,
    #    empty playlist) it sits at its viv.c zero forever and the faces
    #    stay gray while the keys, the menu and the pill keep stepping.
    check("the toolbar no longer gates the step faces on the jump-to dialog's cache",
          "_viv_nav_item_count" not in toolbar)
    # the fourth audit response round moved the predicate into the
    # navigation domain (viv_view.c) so the menu and the toolbar ask it
    # through one helper - the pins follow the predicate.
    check("the neighbor predicate's playlist branch reads the playlist's own items (the navigation domain carries it now)",
          "_viv_playlist_count > 1" in view and
          "_viv_fd_compare(&_viv_playlist_start->fd,_viv_current_fd) != 0" in view)
    check("the neighbor predicate's single file branch reads the folder fact and keeps the random mode live",
          "_viv_nav_folder_neighbor != 0" in view and
          "_viv_random" in view)
    check("the toolbar reaches the predicate through the navigation domain's own header (the playlist include left with it)",
          '#include "viv_view.h"' in toolbar and
          '#include "viv_playlist.h"' not in toolbar)

    # 2. the tri-state fact: -1 unknown, 0 the folder holds nothing
    #    else, 1 it does. defined beside the dialog cache it replaces
    #    in the toolbar's reading, exported through the state layer.
    check("the folder fact is defined unknown beside the dialog cache",
          "int _viv_nav_folder_neighbor = -1;" in viv)
    check("the state layer exports the folder fact beside the dialog cache",
          stateh.count("extern int _viv_nav_folder_neighbor;") == 1 and
          "extern int _viv_nav_item_count;" in stateh)

    # 3. the producer: the _viv_next folder scan resolves best / wrap /
    #    none for the current position, and that resolution IS the
    #    answer to "is there anywhere to step to". the wrap target
    #    being the current file itself means the folder holds nothing
    #    else. a fact change refreshes the faces on the spot - a failed
    #    step must gray the very face that was clicked.
    check("the _viv_next scan resolution records the folder fact",
          "_viv_nav_folder_neighbor = folder_neighbor;" in view)
    check("the wrap target compare feeds the fact (the open guard and the fact read share it)",
          view.count("string_compare(start_fd.cFileName,_viv_current_fd->cFileName) != 0") == 2)
    fact_at = view.find("_viv_nav_folder_neighbor = folder_neighbor;")
    check("the fact change refreshes the faces immediately",
          fact_at != -1 and
          "_viv_toolbar_update_buttons();" in view[fact_at:fact_at + 400])

    # 4. the invalidation: every site that swaps the current file
    #    resets the fact to unknown - the plain open, the last-cache
    #    activation and the preload activation (the three current-fd
    #    writes in the load domain). the preload's own _viv_open
    #    (is_preload) never swaps the current fd, so the fact the scan
    #    just recorded survives until the load it triggered lands.
    def _reset_near(anchor):
        i = vivload.find(anchor)
        return i != -1 and "_viv_nav_folder_neighbor = -1;" in vivload[i:i + 320]

    check("the three current-fd swaps each reset the fact to unknown",
          vivload.count("_viv_nav_folder_neighbor = -1;") == 3)
    check("the plain open resets the fact when it swaps the current fd",
          _reset_near("os_copy_memory(_viv_current_fd,fd,sizeof(WIN32_FIND_DATA));"))
    check("the last-cache activation resets the fact",
          _reset_near("os_copy_memory(_viv_current_fd,&_viv_slot_cache[index].fd,sizeof(WIN32_FIND_DATA));"))
    check("the preload activation resets the fact",
          _reset_near("os_copy_memory(_viv_current_fd,&_viv_slot_preload.fd,sizeof(WIN32_FIND_DATA));"))

    # 5. the dialog cache stays the dialog's own: the jump-to listing
    #    still populates from its WM_INITDIALOG scan and the shutdown
    #    still frees it - the fix must not reach into the dialog's
    #    world. the menu's permissive image gate held until the fourth
    #    audit's consistency finding retired it (see round 110).
    check("the jump-to dialog still builds its own listing",
          "_viv_nav_item_add(&d->fd);" in dialogs and
          "_viv_nav_item_add(&fd);" in dialogs)
    check("the menu gates navigation on the neighbor rule (the fourth audit's consistency fix)",
          "EnableMenuItem(hmenu,VIV_ID_NAV_PREV,is_nav_enabled);" in menu and
          "EnableMenuItem(hmenu,VIV_ID_NAV_NEXT,is_nav_enabled);" in menu)

    # 6. the version, the changelog, the readme.
    top = changes.lstrip("\ufeff").split("\r\n")[0]
    check("the changelog top entry is the field response round pre-release",
          top == "Pre-release: Version 1.1.15-rc.16 (the parallel evaluation round)", top)
    check("the version is 1.1.15-rc.16 build 95",
          '#define VERSION_BUILD 95' in version and
          '#define VERSION_STRING "1.1.15-rc.16"' in version)
    check("the readme candidate slot holds the navigation faces round",
          "**1.1.14 \u2014 the current stable**" in readme and
          "**1.1.14-rc.8** \u2014" in experience)
    flat_changes = " ".join(changes.split())
    check("the changelog tells the cache-versus-paths story",
          "only the jump-to dialog ever fills" in flat_changes and
          "two other paths" in flat_changes)
    check("the changelog states the unknown-counts-as-yes default",
          "unknown counts as a yes" in flat_changes)



def t_corrections_round107():
    """Guards for the corrections round (1.1.14-rc.10: the external
    corrections review - a fact-check pass over the first review's
    claims - with its two must-fix gates and the honest closures.
    the byte-mangling class gets its own gate (editorconfig + the
    byte invariant suite + git diff --check), the split's pure-move
    claim gets its re-runnable proof (the conservation tool, 290/290
    over the actual split pair), the built-in qoi decoder gets its
    deterministic mutant corpus, the arm64 machine chain gets its
    compile gate, and the renderer fallback names itself on the
    status line instead of falling back in silence)."""
    ec = read(".editorconfig").decode()
    byte_test = read("tests/byte_invariant_test.py").decode()
    cons = read("tools/split_conservation.py").decode()
    fuzz = read("tests/qoi_fuzz_smoke.ps1").decode()
    arm = read("build-zig/build-arm64.sh").decode()
    tests_yml = read(".github/workflows/tests.yml").decode()
    release_yml = read(".github/workflows/release.yml").decode()
    gitignore = read(".gitignore").decode()
    stateh = read("src/viv_state.h").decode("latin-1")
    viv = read("src/viv.c").decode("latin-1")
    vivload = read("src/viv_load.c").decode("latin-1")
    wnd = read("src/viv_wndproc.c").decode("latin-1")
    chrome = read("src/viv_chrome.c").decode("latin-1")
    loch = read("src/localization.h").decode("latin-1")
    loce = read("src/localization_en_us.h").decode("latin-1")
    locz = read("src/localization_zh_cn.h").decode("utf-8", errors="replace")
    safe = read("src/safe_size.c").decode("latin-1")
    readme = read("README.md").decode("utf-8", errors="replace")
    contributing = read("CONTRIBUTING.md").decode("utf-8", errors="replace")
    security = read("SECURITY.md").decode("utf-8", errors="replace")
    changes = read("Changes.txt").decode("utf-8", errors="replace")

    # 1. the editorconfig records the tree's real conventions and
    #    stays out of the mixed corners (the two projects, the nsis
    #    tree) - the file describes what is.
    check("the editorconfig exists and roots itself",
          "root = true" in ec)
    check("the editorconfig states the C layer as crlf + tab",
          "end_of_line = crlf" in ec and "indent_style = tab" in ec)
    check("the editorconfig states the unix layers as lf + space",
          "end_of_line = lf" in ec and "indent_style = space" in ec)
    check("the editorconfig states the changelog bom and keeps its section after the txt glob",
          "charset = utf-8-bom" in ec and
          ec.find("\n[Changes.txt]") > ec.find("\n[*.{py,ps1,md,yml,yaml,sh,txt}]") >= 0)
    # 2. the byte invariant suite: the class gate the review asked
    #    for, with its population pins so a renamed class cannot
    #    pass by vacuity.
    check("the byte invariant suite exists and pins the endings per class",
          "want_crlf=True" in byte_test and "want_crlf=False" in byte_test and
          "bare_lf" in byte_test and "bare_cr" in byte_test)
    check("the byte suite pins the changelog bom and the final newlines",
          "has_bom" in byte_test and "ends_with_newline" in byte_test and
          "Changes.txt carries its UTF-8 BOM" in byte_test)
    check("the byte suite was born from real stragglers (the population floor)",
          "len(c_files) > 80" in byte_test and "len(md_files) >= 6" in byte_test)
    check("the byte suite cross-checks the editorconfig it audits beside",
          "the editorconfig exists beside the invariants it describes" in byte_test)
    check("the workflows run the byte suite on both pipelines",
          "tests/byte_invariant_test.py" in tests_yml and
          "tests/byte_invariant_test.py" in release_yml)
    check("the whitespace gate rides the pushed ranges (git diff --check)",
          "git diff --check" in tests_yml and "github.event_before" in tests_yml)

    # 3. the conservation tool: the split's pure-move claim,
    #    re-runnable. the lexer masks literals before counting
    #    braces (the guard suites pin brace-bearing text in string
    #    constants), and the default file set is the union of both
    #    sides (the domains did not exist before the split).
    check("the conservation tool exists with its literal mask",
          "def mask_literals" in cons and "def extract_functions" in cons)
    check("the conservation tool unions the file lists of both revisions",
          'git_ls(old_rev, r"src/viv_.*\\.c$")' in cons and
          'git_ls(new_rev, r"src/viv_.*\\.c$")' in cons)
    check("the conservation tool normalizes exactly the documented move edits",
          're.sub(r"^static\\s+", "", text)' in cons)
    check("the changelog records the conservation answer over the split pair",
          "290 old, 290 new, 290" in changes and
          "tools/split_conservation.py" in changes)
    check("contributing carries the structural-refactor policy",
          "## Structural refactors and the conservation gate" in contributing and
          "split_conservation.py" in contributing)

    # 4. the qoi fuzz smoke: decode-or-refuse over a deterministic
    #    corpus, on the gdi leg where the renderer question cannot
    #    contaminate the exit contract.
    check("the qoi fuzz harness exists with its deterministic seeds",
          "fx_still_qoi_rgb.qoi" in fuzz and "fx_still_qoi_sliver.qoi" in fuzz and
          "0x514F49" in fuzz)
    check("the fuzz contract is decode-or-refuse (0 or 2, bitmap required on 0)",
          "$code -eq 0" in fuzz and "$code -eq 2" in fuzz and
          "exit 0 but no bitmap answered" in fuzz and
          "outside the decode-or-refuse contract" in fuzz)
    check("the fuzz stage wedges get killed, not waited out",
          "Stop-Process" in fuzz and "WaitForExit(45000)" in fuzz)
    check("the fuzz stage rides the windows leg beside the smoke sweep",
          "qoi_fuzz_smoke.ps1" in tests_yml)

    # 5. the arm64 compile gate: the machine chain the tree carries
    #    finally compiles somewhere, every push.
    check("the arm64 zig build targets aarch64 windows with the VERSION_ARM64 chain",
          "aarch64-windows-gnu" in arm and "-DVERSION_ARM64" in arm and
          "viv-arm64.exe" in arm)
    check("the arm64 gate rides the ubuntu job",
          "build-arm64.sh" in tests_yml and "ziglang==0.16.0" in tests_yml)
    check("the arm64 build artifacts stay untracked",
          "build-zig/obj-arm64/" in gitignore and
          "build-zig/*.manifest" in gitignore)

    # 6. the renderer honesty line: the refusal point sets a per-image
    #    flag once, the status line names the fallback beside the
    #    filename, and the next load dispatch clears it where the
    #    budget refusal flags clear.
    check("the fallback flag is shared state with its own comment",
          "extern BYTE _viv_hw_render_fallback;" in stateh and
          "BYTE _viv_hw_render_fallback = 0;" in viv)
    check("the paint path sets the flag once and pays for one status refresh",
          "_viv_hw_render_fallback = 1;" in wnd and
          wnd.count("_viv_status_update();") >= 1 and
          "if (!_viv_hw_render_fallback)" in wnd)
    check("every load dispatch reset clears the fallback flag (four sites)",
          vivload.count("_viv_hw_render_fallback = 0;") == 4)
    check("the status line appends the fallback notice beside the filename",
          "LOCALIZATION_ID_STATUS_BAR_RENDERER_FALLBACK" in chrome and
          'string_cat_utf8(text_buf,(const utf8_t *)"  ");' in chrome)
    check("the fallback string answers in both languages",
          'LOCALIZATION_ID_STATUS_BAR_RENDERER_FALLBACK' in loch and
          'The selected renderer cannot display this image - painting with GDI.' in loce and
          "所选渲染器无法显示此图像" in locz)
    check("the debug channel still names the refusal it always named",
          "the frame is not a dib section" in read("src/hwgl.c").decode("latin-1"))

    # 7. the documentation honesty: credits, dco, the language guide,
    #    the sole-maintainer expectation, and the one style nit.
    check("the readme credits section names upstream beside the fork author",
          "## Credits" not in readme and "\nCredits\n" in readme and
          "hesphoros" in readme and "David Carpenter" in readme)
    check("contributing carries the dco sign-off flow",
          "## Contributor sign-off (DCO)" in contributing and
          "developercertificate.org" in contributing)
    check("contributing carries the adding-a-language guide",
          "## Adding a language" in contributing and
          "localization_en_us.h" in contributing)
    check("the code style section points at the byte gates",
          "byte_invariant_test.py" in contributing and ".editorconfig" in contributing)
    check("security states the sole-maintainer best-effort reality",
          "one active maintainer" in security and "best-effort" in security)
    check("the safe_size copyright header matches the dominant spelling",
          "// Copyright 2025 voidtools / David Carpenter" in safe and
          "// Copyright (C) 2025" not in safe)

    # 8. the changelog's own record: the encounters ledger the review
    #    asked for (the author claim becomes the auditable list) and
    #    the round's version identity.
    check("the changelog carries the byte-mangling encounters ledger",
          "byte-mangling encounters ledger" in changes and
          changes.count("doubled carriage returns") >= 1)
    check("the changelog top entry is the corrections round",
          "Pre-release: Version 1.1.14-rc.10 (the corrections round)" in changes and
          changes.find("1.1.14-rc.10 (the corrections round)") <
          changes.find("1.1.14-rc.9 (the navigation faces round)"))
    check("the readme zig line states the measured translation unit count",
          "104 translation units" in readme and "480 KB" in readme)


def t_stable_promotion_round108():
    """Guards for the stable promotion round (1.1.14: the arc's ten
    candidates converge on the stable mark, and the promotion asks the
    memory question the rc arc never had to answer in writing - the
    budget ceilings priced one image at a time while the viewer holds
    three: the current image, the last-image cache and the preload
    slot. the cache-set ceiling closes the gap at every fill point:
    the preload is the one load that refuses first (it is the only
    load nobody asked for), and the settle-point trim drops the cache,
    never the image the user is looking at. the round-93 startup-row
    removal left its y-advance behind - the orphan drew a blank band
    inside the general page - and the walk now carries no advance
    without its control row. the defaults the promotion was asked to
    turn on get pinned as the facts they already are: preload-next and
    cache-last have shipped on by default since the upstream
    introduction; the promise becomes a guard instead of an
    accident)."""
    vivload = read("src/viv_load.c").decode("latin-1")
    wnd = read("src/viv_wndproc.c").decode("latin-1")
    loadh = read("src/viv_load.h").decode("latin-1")
    settings = read("src/viv_settings.c").decode("latin-1")
    config = read("src/config.c").decode("latin-1")
    version = read("src/version.h").decode()
    changes = read("Changes.txt").decode("utf-8", errors="replace")
    readme = read("README.md").decode("utf-8", errors="replace")
    experience = read("experience.md").decode("utf-8", errors="replace")

    # 1. the version marks the stable promotion (the rc phase suffix
    #    is gone; the plain tag form is the stable release form).
    check("version.h = 1.1.15-rc.11.90 (the stable promotion round pins ride the slot architecture round)",
          "#define VERSION_BUILD 95" in version and
          '#define VERSION_STRING "1.1.15-rc.16"' in version and
          '#define VERSION_TYPE ""' in version)

    # 2. the defaults the promotion promises, pinned as facts: both
    #    caches ship on by default and an absent ini key keeps the
    #    in-memory default (a user's explicit off stays off - the
    #    defaults speak for the silent majority, not over them).
    check("preload-next ships on by default",
          "BYTE config_preload_next = 1;" in config)
    check("cache-last ships on by default",
          "BYTE config_cache_last = 1;" in config)
    check("the ini read keeps the in-memory default for absent keys",
          'ini_get_int(ini,(const utf8_t *)"preload_next",config_preload_next);' in config and
          'ini_get_int(ini,(const utf8_t *)"cache_last",config_cache_last);' in config)

    # 3. the cache-set arithmetic: one slot's held bytes priced as the
    #    worst-case 32bpp frame set with the mipmap chain's extra
    #    third, every multiplication through the safe helpers so a
    #    hostile dimension pair cannot wrap into an "it fits" answer.
    check("the frame-set estimator multiplies through safe_size_mul",
          "pixels = safe_size_mul((SIZE_T)(unsigned int)wide,(SIZE_T)(unsigned int)high);" in vivload and
          "bytes = safe_size_mul(pixels,4);" in vivload and
          "bytes = safe_size_mul(bytes,(SIZE_T)(unsigned int)frame_count);" in vivload)
    check("the estimator prices the mipmap third and keeps the sentinel",
          "if (bytes == SIZE_MAX)" in vivload and
          "return bytes / 3;" in vivload)
    check("the ceiling prices current, last and preload together",
          "_viv_slot_bytes(&_viv_slot_current)" in vivload and
          "total = safe_size_add(total,_viv_slot_bytes(&_viv_slot_cache[i]));" in vivload and
          "safe_size_add(total,_viv_slot_bytes(&_viv_slot_preload))" in vivload)
    check("the ceiling answers against its own cache-set line",
          "return total > VIV_CACHE_SET_MAX_BYTES;" in vivload)
    check("the pending-clear slot stays out of the sum by construction",
          "so it is empty wherever these gates" in vivload)

    # 4. the preload fill gate: the one load nobody asked for refuses
    #    first and silently, exactly like every other background
    #    preload failure.
    check("the preload fill gate is declared and defined",
          "int _viv_preload_set_refused(int wide,int high,int frame_count);" in loadh and
          "int _viv_preload_set_refused(int wide,int high,int frame_count)" in vivload)
    gate = wnd.find("_viv_preload_set_refused(first_frame->wide,first_frame->high,first_frame->frame_count)")
    check("the first-frame reply prices the incoming preload against the set",
          gate != -1)
    check("the abandoned preload unwinds the slot and routes the decode to the discard path",
          gate != -1 and
          "_viv_slot_preload.state = 2;" in wnd[gate:gate + 2500] and
          "_viv_slot_preload.fd.cFileName[0] = 0;" in wnd[gate:gate + 2500] and
          "InterlockedExchange(&_viv_load_image_terminate,1);" in wnd[gate:gate + 2500] and
          "DeleteObject(first_frame->frame.hbitmap);" in wnd[gate:gate + 2500])
    check("the frame array allocation still goes through safe_size_mul",
          "mem_alloc(safe_size_mul(sizeof(_viv_frame_t),(SIZE_T)_viv_slot_preload.frame_count));" in wnd)

    # 5. the settle-point trim: over the ceiling the cache goes and
    #    the current image stays.
    check("the trim drops the last cache over the ceiling",
          "static void _viv_cache_set_trim(void)" in vivload and
          "_viv_clear_last();" in vivload)
    pn = vivload.find("void _viv_preload_next(void)")
    trim_at = vivload.find("_viv_cache_set_trim();", pn)
    gate_at = vivload.find("if (config_preload_count > 0)", pn)
    check("the trim runs before the config gate (a memory promise, not a convenience)",
          pn != -1 and trim_at != -1 and gate_at != -1 and trim_at < gate_at)
    paste_note = vivload.find("the paste path never reaches a load-settle hook")
    check("the paste path trims too (it never reaches a load-settle hook)",
          paste_note != -1 and
          vivload.find("_viv_cache_set_trim();", paste_note) < paste_note + 400)

    # 6. the already-loading answer only speaks for loads headed to
    #    the screen: an in-flight preload whose fd was cleared (the
    #    cache-set abandonment, or a displaced decode) must not
    #    swallow the request - it queues as the normal load it is.
    check("already-loading requires the in-flight load to be a normal load",
          "if ((!is_preload) && (!_viv_load_is_preload) && (_viv_load_image_thread) && (_viv_load_image_filename) && (string_compare(_viv_load_image_filename,fd->cFileName) == 0))" in vivload)

    # 7. the settings blank: the round-93 startup-row removal left its
    #    y-advance behind (a 52-dip band inside the general page). the
    #    walk now carries no advance without its control row: the span
    #    between two consecutive advances must hold the row it paid
    #    for (a control add or the row_high assignment itself).
    # round-125: the settings remake - the view page re-cuts its eleven
    # advances into ten (the toolbar icon row moved), the general page
    # gains four and the controls page one: 22 + 4.
    check("the orphan advance is gone (one advance per row)",
          settings.count("y += row_high;") == 26)
    orphans = []
    lines = settings.split("\n")
    prev = 0
    for i, line in enumerate(lines):
        if "y += row_high;" in line:
            span = "\n".join(lines[prev:i])
            if "_viv_settings_ctl_add(" not in span and "row_high = " not in span:
                orphans.append(i + 1)
            prev = i
    check("no y-advance rides without its control row (the round-93 lesson)",
          not orphans, f"orphan advances at lines {orphans}")
    check("the section comment counts its three switch rows",
          "three switch rows" in settings)

    # 8. the changelog, the readme and the demotion ride the promotion
    check("the changelog tops with the slot architecture round",
          changes.lstrip("\ufeff").startswith("Pre-release: Version 1.1.15-rc.16 (the parallel evaluation round)"))
    check("the readme current-stable line says 1.1.14",
          "**1.1.14 \u2014 the current stable**" in readme)
    check("the 1.1.13 full section demotes to the one-line list",
          "**1.1.13 \u2014" not in readme and "**1.1.13** \u2014" in experience)
    check("the rc.10 candidate section demotes to the one-line list (rc.3 is the candidate now)",
          "**1.1.14-rc.10** \u2014" in experience and
          "**1.1.15-rc.6 \u2014" in readme and
          readme.count("(the current release candidate):**") == 1)


def t_slot_architecture_round109():
    """Guards for the image slot architecture round (1.1.15-rc.2: the
    physical separation becomes an architecture. the three parallel
    frame-set families - the current image, the last-image cache and
    the preload slot, each a set of loose globals with its own
    hand-written field-by-field moves - collapse into one typed slot
    and three instances. the lifecycle is two primitives now
    (_viv_slot_take moves a whole slot, _viv_slot_clear_frames empties
    one), the cache-set ceiling prices the three slots through one
    _viv_slot_bytes helper, and the five-local ping-pong behind the
    back-navigation is one local slot plus two takes. the walk past
    the status code also catches the nav-index cache: its key compared
    the frame fd's address, but the fd has lived at one stable address
    since the split era - the index pane froze on the first file the
    walk ever saw. the file name is the identity now)."""
    viv = read("src/viv.c").decode("latin-1")
    state = read("src/viv_state.h").decode("latin-1")
    vivload = read("src/viv_load.c").decode("latin-1")
    wnd = read("src/viv_wndproc.c").decode("latin-1")
    chrome = read("src/viv_chrome.c").decode("latin-1")
    version = read("src/version.h").decode()
    changes = read("Changes.txt").decode("utf-8", errors="replace")
    readme = read("README.md").decode("utf-8", errors="replace")

    # 1. the version mark
    check("version.h = 1.1.15-rc.11.90 (the slot architecture round's pins ride the audit response round)",
          "#define VERSION_BUILD 95" in version and
          '#define VERSION_STRING "1.1.15-rc.16"' in version)

    # 2. the type: one held image - the file identity, the frames,
    #    both counts, the dimensions and the preload role's state -
    #    declared once in the state layer.
    check("the slot type carries the seven members",
          "typedef struct _viv_image_slot_s" in state and
          "WIN32_FIND_DATA fd;" in state and
          "_viv_frame_t *frames;" in state and
          "int frame_count;" in state and
          "int frame_loaded_count;" in state and
          "int image_wide;" in state and
          "int image_high;" in state and
          "BYTE state;" in state)
    check("the slots are declared (current, the ring, preload)",
          "extern _viv_image_slot_t _viv_slot_current;" in state and
          "extern _viv_image_slot_t _viv_slot_cache[VIV_CACHE_SLOTS];" in state and
          "extern _viv_image_slot_t _viv_slot_preload;" in state)

    # 3. the retirement: the nineteen loose globals of the physical
    #    separation are gone from the spliced code (the splice covers
    #    every viv_*.c and appends the state layer, so this is the
    #    whole tree).
    retired = ["_viv_frames", "_viv_frame_count", "_viv_frame_loaded_count",
               "_viv_image_wide", "_viv_image_high", "_viv_frame_fd",
               "_viv_last_frames", "_viv_last_frame_count", "_viv_last_fd",
               "_viv_last_image_wide", "_viv_last_image_high",
               "_viv_preload_frames", "_viv_preload_frame_count",
               "_viv_preload_frame_loaded_count", "_viv_preload_image_wide",
               "_viv_preload_image_high", "_viv_preload_fd",
               "_viv_preload_state", "_viv_preload_is_prev"]
    leaked = [n for n in retired if re.search(r"\b" + re.escape(n) + r"\b", viv)]
    check("no loose slot global survives anywhere in the spliced code",
          not leaked, f"leaked: {leaked}")

    # 4. the take: one primitive moves the whole slot - the fd, the
    #    frames, both counts and the dimensions - and empties the
    #    source. the state member is role metadata, not content: the
    #    take must never move it.
    check("the take primitive moves the whole slot and empties the source",
          "static void _viv_slot_take(_viv_image_slot_t *dst,_viv_image_slot_t *src)" in vivload and
          "os_copy_memory(&dst->fd,&src->fd,sizeof(WIN32_FIND_DATA));" in vivload and
          "src->fd.cFileName[0] = 0;" in vivload and
          "src->frames = NULL;" in vivload and
          "src->image_high = 0;" in vivload)
    check("the take never touches the state member",
          "src->state" not in vivload and "dst->state" not in vivload)

    # 5. the clear: frees by the loaded count (a partial animation only
    #    allocated the frames that arrived) and zeroes the counts and
    #    dimensions unconditionally - the preload family's old shape,
    #    now the one clear for every slot.
    check("the clear primitive frees by the loaded count",
          "static void _viv_slot_clear_frames(_viv_image_slot_t *slot)" in vivload and
          "_viv_clear_frames(slot->frames,slot->frame_loaded_count);" in vivload)

    # 6. the three hand-written family clears route through the one
    #    primitive; the extern surface keeps its names.
    check("the family clears route through the primitive",
          vivload.count("_viv_slot_clear_frames") == 5 and
          "void _viv_clear_last(void)\r\n{\r\n\tint i;\r\n\t\r\n\tfor(i=0;i<VIV_CACHE_SLOTS;i++)\r\n\t{\r\n\t\t_viv_slot_clear_frames(&_viv_slot_cache[i]);\r\n\t}\r\n}\r\n" in vivload and
          "void _viv_clear_preload_frames(void)\r\n{\r\n\t_viv_slot_clear_frames(&_viv_slot_preload);\r\n}\r\n" in vivload)

    # 7. the moves: every slot-to-slot transition is a take - the
    #    preload activation, the last-cache fill and the back-navigation
    #    ping-pong. three takes, no hand-rolled field moves left.
    check("every slot transition is one take (three in the file)",
          vivload.count("_viv_slot_take(&_viv_slot") == 3 and
          "_viv_slot_take(&_viv_slot_current,&_viv_slot_preload);" in vivload and
          "_viv_slot_take(&_viv_slot_cache[0],src);" in vivload and
          "_viv_slot_take(&_viv_slot_current,&_viv_slot_cache[index]);" in vivload)
    check("the five-local ping-pong is one local slot plus a ring insert",
          "_viv_image_slot_t old_slot;" in vivload and
          "_viv_cache_insert(&old_slot);" in vivload and
          "WIN32_FIND_DATA old_fd;" not in vivload and
          "_viv_frame_t *old_frames;" not in vivload)

    # 8. the ceiling prices the slots through the one helper (the
    #    round-108 pins above carry the three-line sum; this pins the
    #    wrapper that makes a fourth slot a one-line change).
    check("the slot-bytes helper prices one slot",
          "static SIZE_T _viv_slot_bytes(const _viv_image_slot_t *slot)" in vivload and
          "return _viv_frame_set_bytes(slot->image_wide,slot->image_high,slot->frame_count);" in vivload)

    # 9. the nav-index cache: the pointer key could never invalidate
    #    (the frame fd has one stable address), so the index pane froze
    #    on the first file the walk ever saw. the name is the identity.
    check("the nav-index cache keys on the file name, not the address",
          "static wchar_t last_filename[STRING_SIZE];" in chrome and
          "string_compare(last_filename,_viv_slot_current.fd.cFileName) == 0" in chrome and
          "last_fd ==" not in chrome and
          "string_copy(last_filename,_viv_slot_current.fd.cFileName);" in chrome)

    # 10. the fd heap blocks: the three slot fds are embedded members
    #     now; only the navigation fd and the load fd stay allocated.
    check("only the navigation, load and dispatch-queue fds allocate",
          viv.count("mem_alloc(sizeof(WIN32_FIND_DATA))") == 3 and
          "mem_free(_viv_frame_fd);" not in viv and
          "mem_free(_viv_last_fd);" not in viv and
          "mem_free(_viv_preload_fd);" not in viv)
    check("the kill path releases the ring seats through the slot",
          "_viv_clear_frames(_viv_slot_cache[i].frames,_viv_slot_cache[i].frame_loaded_count);" in viv)

    # 11. the reply handlers fill the slots
    check("the first-frame reply fills the preload slot",
          "_viv_slot_preload.frames[0].hbitmap = first_frame->frame.hbitmap;" in wnd)
    check("the first-frame reply fills the current slot",
          "_viv_slot_current.frames[0].hbitmap = first_frame->frame.hbitmap;" in wnd and
          "os_copy_memory(&_viv_slot_current.fd,_viv_load_fd,sizeof(WIN32_FIND_DATA));" in wnd)

    # 12. the changelog, the readme and the candidate block
    check("the changelog tops with the slot architecture round",
          changes.lstrip("\ufeff").startswith("Pre-release: Version 1.1.15-rc.16 (the parallel evaluation round)"))
    check("the readme carries the new candidate block",
          "**1.1.15-rc.6 \u2014" in readme and
          readme.count("(the current release candidate):**") == 1)
    check("the readme stable block survives the candidate",
          "**1.1.14 \u2014 the current stable**" in readme)


def t_audit_response_round110():
    """Guards for the fourth audit response round (1.1.15-rc.2: the
    external audit's fourth report lands against the stable - its own
    core finding retracted by its own bracket-stack reparse, its three
    live findings answered here. the golden set learns to discriminate:
    its two controls are single solid fills, and one of them shares a
    bit-identical d3d hash with its gdi hash - the audit could not
    rule out, from linux, whether that was the integer-aligned solid
    coincidence or a d3d leg that never drew reading the gdi result
    back. the textured png adjudicates: real structure at several
    scales that no filter pair answers identically on, held to the
    three-way distinct contract the moment its hashes land. the menu's
    navigation pair gates on the neighbor rule the toolbar's faces
    already carried. the cache-set ceiling's comment tells the
    unit-price truth)."""
    view = read("src/viv_view.c").decode("latin-1")
    viewh = read("src/viv_view.h").decode("latin-1")
    menu = read("src/viv_menu.c").decode("latin-1")
    toolbar = read("src/viv_toolbar.c").decode("latin-1")
    vivload = read("src/viv_load.c").decode("latin-1")
    fixture_gen = read("tests/make_fixture_samples.py").decode("latin-1")
    golden_ps1 = read("tests/render_golden.ps1").decode("utf-8", errors="replace")
    golden_pg = read("tests/pixel_golden_test.py").decode()
    version = read("src/version.h").decode()
    changes = read("Changes.txt").decode("utf-8", errors="replace")
    readme = read("README.md").decode("utf-8", errors="replace")
    experience = read("experience.md").decode("utf-8", errors="replace")

    # 1. the version mark
    check("version.h = 1.1.15-rc.11.90 (the fourth audit response round)",
          "#define VERSION_BUILD 95" in version and
          '#define VERSION_STRING "1.1.15-rc.16"' in version)

    # 2. the neighbor predicate: one helper in the navigation domain,
    #    declared on the navigation's own export face, carrying the
    #    exact rule the toolbar's step faces shipped in rc.9.
    check("the neighbor predicate lives in the navigation domain",
          "int _viv_nav_neighbor_available(void)" in viewh and
          "int _viv_nav_neighbor_available(void)\r\n{" in view)
    check("the predicate keeps the rc.9 rule whole",
          "_viv_fd_compare(&_viv_playlist_start->fd,_viv_current_fd) != 0" in view and
          "(_viv_nav_folder_neighbor != 0) ? 1 : 0" in view)
    check("the toolbar routes the step faces through the helper",
          "enable = _viv_nav_neighbor_available();" in toolbar and
          "_viv_nav_item_count" not in toolbar)
    check("the menu pair rides the same predicate",
          "is_nav_enabled = _viv_nav_neighbor_available() ? MF_ENABLED : MF_DISABLED;" in menu and
          "EnableMenuItem(hmenu,VIV_ID_NAV_PREV,is_nav_enabled);" in menu and
          "EnableMenuItem(hmenu,VIV_ID_NAV_NEXT,is_nav_enabled);" in menu)
    check("home and end keep the image rule (they re-scan and re-open unconditionally)",
          "EnableMenuItem(hmenu,VIV_ID_NAV_HOME,is_image_enabled);" in menu and
          "EnableMenuItem(hmenu,VIV_ID_NAV_END,is_image_enabled);" in menu)

    # 3. the cache-set ceiling comment tells the unit-price truth the
    #    audit asked for: 12 bytes per pixel of working-set residency
    #    against 16/3 bytes per pixel of held frames, one shared byte
    #    ceiling, and the conservative direction named.
    check("the cache-set comment prices the two unit prices honestly",
          "12 bytes per pixel" in vivload and
          "16/3 bytes per pixel" in vivload and
          "the old comment\r\n// papered over the difference" in vivload and
          "the same working-set number the per-image gates use" not in vivload)
    check("the pending-clear sentence survives the rewrite (its pin lives in round 108)",
          "so it is empty wherever these gates" in vivload)

    # 4. the discrimination fixture: generated at 130x97 (both axes non
    #    power of two, the padded 256x128 inside the gl texture
    #    ceiling), hand-encoded with the standard library only, and
    #    self-checked to carry thousands of distinct pixel values - a
    #    fixture that regresses into a solid fill fails its own
    #    generator, not a golden run.
    check("the discrimination fixture rides the generator",
          "def make_textured_png(width, height):" in fixture_gen and
          "emit('fx_still_textured.png', make_textured_png(130, 97))" in fixture_gen)
    check("the fixture self-check pins the discrimination floor",
          "len(distinct) >= 5000" in fixture_gen and
          "the discrimination contract died" in fixture_gen)
    check("the golden set carries the texture (thirteen samples)",
          '"fx_still_textured.png"' in golden_ps1 and
          "thirteen below cover every decoder family" in golden_ps1)
    # round-125: the distinctness contract moved to the scaled
    # samples (the 1:1 samples legitimately agree under the
    # upright mapping - the flip era faked their difference).
    check("the pixel suite holds scaled textured samples to three-way distinct",
          "solid_exempt" in golden_pg and
          "one_to_one_exempt" in golden_pg and
          "the fake green the fourth audit flagged" in golden_pg)

    # 5. the one-way ratchet closed: a leg whose hash the manifest
    #    pinned must not quietly regress to a renderer refusal.
    check("the ratchet: a pinned hash regressing to a refusal goes red",
          "the pinned hash regressed to a renderer refusal" in golden_ps1 and
          "$mismatched++" in golden_ps1)

    # 6. the changelog, the readme and the candidate rotation
    check("the changelog tops with the fourth audit response round",
          changes.lstrip("\ufeff").startswith("Pre-release: Version 1.1.15-rc.16 (the parallel evaluation round)"))
    check("the readme carries the new candidate block and the rc.1 one-liner",
          "**1.1.15-rc.6 \u2014" in readme and
          "### 1.1.15-rc.1 \u2014" in experience and
          "### 1.1.15-rc.2 —" in experience and
          readme.count("(the current release candidate):**") == 1)




def t_audit_response_round111():
    """Guards for the fifth audit response round (1.1.15-rc.3: the fusion
    report - a first pass whose nav-and-golden fix package never reached
    a tree, and eight parallel sweeps. every claim was re-verified against
    the tree itself before landing, per the house rule for external
    reports. the navigation trio: the full-path sort's directory scans
    compared bare names against full paths - the path completes before
    the first compare now, at all three scan sites; the neighbor
    predicate asks random in the walk's own order; the playlist matches
    by file name, not the reserved id pair an external fd shares with
    the first entry. batch a lands in full - the mipmap node born
    complete, the blank state refreshing the toolbar faces, the rename
    mirrored into both name sources, the theme re-pin beside the dpi
    one, the attestation typo, the utf-8 termination, the copydata
    null guard, the glyphs token pairing, the probe zero, the test-side
    litter. the guard blind spots close: the ladder step extracted and
    relation-checked, the deleted-manifest red gate, and the bootstrap
    refusing the fake green at the source)."""
    import os.path
    view = read("src/viv_view.c").decode("latin-1")
    playlist = read("src/viv_playlist.c").decode("latin-1")
    render = read("src/viv_render.c").decode("latin-1")
    load = read("src/viv_load.c").decode("latin-1")
    dialogs = read("src/viv_dialogs.c").decode("latin-1")
    dialogsh = read("src/viv_dialogs.h").decode("latin-1")
    wndproc = read("src/viv_wndproc.c").decode("latin-1")
    glyphs = read("src/glyphs.c").decode("latin-1")
    glyphsh = read("src/glyphs.h").decode("latin-1")
    stringc = read("src/string.c").decode("latin-1")
    viv = read("src/viv.c").decode("latin-1")
    readme = read("README.md").decode("utf-8", errors="replace")
    experience = read("experience.md").decode("utf-8", errors="replace")
    changes = read("Changes.txt").decode("utf-8", errors="replace")
    version = read("src/version.h").decode()
    tests_yml = read(".github/workflows/tests.yml").decode()
    release_yml = read(".github/workflows/release.yml").decode()
    golden_ps1 = read("tests/render_golden.ps1").decode("utf-8", errors="replace")
    qoi_ps1 = read("tests/qoi_fuzz_smoke.ps1").decode("utf-8", errors="replace")
    sim = read("tests/simulation_test.py").decode()
    zoommath = read("tests/zoom_math_test.py").decode()

    # 1. the version mark
    check("version.h = 1.1.15-rc.11.90 (the fifth audit response round)",
          "#define VERSION_BUILD 95" in version and
          '#define VERSION_STRING "1.1.15-rc.16"' in version)

    # 2. the navigation trio - the sort pollution: the path completes
    #    before the first compare, at all three scan sites, and the six
    #    late builds it used to ride inside the selection blocks retired.
    check("the three scan sites complete the path before comparing",
          view.count("string_path_combine(search_wbuf,path_wbuf,fd.cFileName);") == 3 and
          "the path before the first compare or the sort orders against" in view)
    check("the wrap rescan and the home scan hoist the same pair",
          "the wrap rescan and the home scan below hoist" in view and
          "same hoist as the step scan above: the wrap candidates" in view and
          "same hoist as the step scan: home and end order against" in view)

    # 3. the navigation trio - the gate order: random answers before the
    #    file gate, in the walk's own order, and the old unreachable
    #    fallback at the tail retired.
    check("the neighbor predicate asks random first, in the walk's order",
          "_viv_next answers random before it ever looks at the" in view and
          "((_viv_nav_folder_neighbor != 0) || (_viv_random))" not in view)

    # 4. the navigation trio - the playlist identity: the file name, the
    #    same case-folded compare the recent list uses; the id pair's
    #    equality match retired from both lookups.
    check("the playlist identity is the file name in both lookups",
          playlist.count("_viv_icompare_filename(") >= 3 and
          "the same case-folded compare the recent list has always" in playlist and
          "the identity is the file name: the reserved pair is a" in playlist)
    check("the id-pair false match retired from the lookups",
          "->fd.dwReserved0 == fd->dwReserved0" not in playlist and
          "d->fd.dwReserved1 == fd->dwReserved1" not in playlist)

    # 5. the mipmap node is born complete before the dc dance
    check("the mipmap node is born complete",
          "the node is born complete before the dc dance" in render and
          "(*pmip)->hbitmap = 0;" in render and
          render.count("(*pmip)->mipmap = NULL;") == 1)

    # 6. the blank state refreshes the toolbar faces
    check("the blank state updates the toolbar faces",
          "the toolbar's faces answer the blank state too" in load and
          "_viv_toolbar_update_buttons();" in load)

    # 7. the rename mirrors into both name sources
    check("the rename mirrors into the slot fd beside the current fd",
          "string_copy_with_bufsize(_viv_slot_current.fd.cFileName,MAX_PATH,file_op_new_name);" in dialogs and
          "the title bar reads the current fd while the status strip" in dialogs)

    # 8. the theme change re-pins beside the dpi path, and the zoom
    #    editor follows both
    check("the theme change re-pins the status font",
          "the theme flip is the same lifecycle" in wndproc and
          wndproc.count("SendMessage(_viv_status_hwnd,WM_SETFONT,(WPARAM)_viv_menu_font(),MAKELPARAM(TRUE,0));") == 2)
    check("the zoom editor re-pins on both paths",
          "void _viv_zoom_edit_refont(void)" in dialogs and
          "void _viv_zoom_edit_refont(void);" in dialogsh and
          wndproc.count("_viv_zoom_edit_refont();") == 2)

    # 9. the small guards
    check("the utf-8 allocation terminates before the conversion",
          "an unallocated block scanned as text is worse than an" in stringc and
          "p[0] = 0;" in stringc)
    check("the copydata handler declines a null pointer",
          "same message from an in-process sender with a null lParam" in wndproc)
    check("the glyphs gdi+ token pairs its shutdown",
          "void glyphs_shutdown(void)" in glyphs and
          "void glyphs_shutdown(void);" in glyphsh and
          "glyphs_shutdown();" in viv)
    check("the pixel probe initializes its color",
          "COLORREF src_pixel_rgb = 0;" in render)
    check("the attestation command spells the repo again",
          "purfecto114" not in readme)

    # 10. the guard blind spots
    check("the ladder step is extracted, not self-written (the sim relation)",
          'r"f \\*= 1\.(\d+);"' in sim and
          "the shrink ladder spans the ~16x cap the header documents" in sim)
    check("the ladder step literal is pinned in the zoom math",
          "the ladder step literal is extractable from the init walk" in zoommath and
          "if False else" not in zoommath)
    check("the deleted manifest fails the push gate",
          "a deletion is a downgrade, not a bootstrap window" in tests_yml and
          "not bootstrapped yet - the strict pixel comparison waits" not in tests_yml)
    check("the pipeline token is least-privilege",
          "permissions:" in tests_yml and
          "actions: write" not in tests_yml and
          tests_yml.count("timeout-minutes:") == 2)
    check("the bootstrap refuses the fake green at the source",
          "the fake-green signature the discrimination gate exists to refuse" in golden_ps1 and
          "$solidControls" in golden_ps1)
    check("the fuzz smoke's setup failures answer the setup code",
          qoi_ps1.count("exit 2") >= 2 and
          "exit codes: 0 pass, 1 fail, 2 setup error" in qoi_ps1)

    # 11. the test-side litter retired
    check("the dead zoom icon generator retired (git history is the archive)",
          not os.path.exists("tools/gen_zoom_icons.py"))
    check("the release pipeline carries no dead artifact env",
          "ARTIFACT:" not in release_yml)

    # 12. the changelog, the readme and the candidate rotation
    check("the changelog tops with the fifth audit response round",
          changes.lstrip("\ufeff").startswith("Pre-release: Version 1.1.15-rc.16 (the parallel evaluation round)"))
    check("the changelog carries the round's own ledger",
          "the navigation trio first, because two of the three are ordering defects" in changes and
          "the ladder step is extracted, not self-written" in changes and
          "a deleted manifest fails the push gate" in changes)
    check("the readme candidate block and the rc.2 one-liner",
          "**1.1.15-rc.6 \u2014" in readme and
          "### 1.1.15-rc.2 \u2014" in experience and
          readme.count("(the current release candidate):**") == 1)




def t_judged_fixes_round114():
    """Guards for the judged-fixes round (1.1.15-rc.6: the user's own fix
    list - the msgbox button bridge, the fullscreen binary searches, the
    refusal flags' interlocked forms, the classic options retirement,
    the borderless corner resize, the icon-only toolbar and the paste
    text fallback)."""
    viv = read("src/viv.c").decode("utf-8", errors="replace")
    vivload = read("src/viv_load.c").decode("utf-8", errors="replace")
    wnd = read("src/viv_wndproc.c").decode("utf-8", errors="replace")
    settings = read("src/viv_settings.c").decode("utf-8", errors="replace")
    dialogs = read("src/viv_dialogs.c").decode("utf-8", errors="replace")
    chrome = read("src/viv_chrome.c").decode("utf-8", errors="replace")
    toolbar = read("src/viv_toolbar.c").decode("utf-8", errors="replace")
    msgbox = read("src/viv_msgbox.c").decode("utf-8", errors="replace")
    configc = read("src/config.c").decode("utf-8", errors="replace")
    state = read("src/viv_state.h").decode("utf-8", errors="replace")
    loc_h = read("src/localization.h").decode("utf-8", errors="replace")
    loc_e = read("src/localization_en_us.h").decode("utf-8", errors="replace")
    loc_z = read("src/localization_zh_cn.h").decode("utf-8", errors="replace")
    rc = read("res/voidImageViewer.rc").decode("utf-8", errors="replace")
    resh = read("res/resource.h").decode("utf-8", errors="replace")

    # 1. the msgbox button bridge: the last raw utf-8-to-wide-api point.
    check("the msgbox button text bridges through the utf8 converter",
          "static wchar_t text[STRING_SIZE];" in msgbox and
          msgbox.count("string_copy_utf8_string(text,localization_get_string(") == 3)
    check("the msgbox button path never returns the raw table bytes",
          "return localization_get_string(LOCALIZATION_ID_OK_BUTTON);" not in msgbox and
          "return localization_get_string(" not in msgbox)

    # 2. the fullscreen toggle: two binary searches, no precompute table.
    check("the fullscreen-fill offset binary-searches the windowed geometry",
          "if ((!_viv_is_fullscreen) && (config_fullscreen_fill_window))" in chrome and
          chrome.count("os_MonitorRectFromWindow(_viv_hwnd,1,&monitor_rect);") == 2 and
          "(rw > mon_wide) || (rh > mon_high)" in chrome)
    check("the fill-window offset binary-searches the fullscreen geometry",
          "if ((!config_fullscreen_fill_window) && (config_fill_window))" in chrome and
          "(rw > old_rw) || (rh > old_rh)" in chrome)
    check("the two fill modes keep their else-if relation",
          chrome.find("if ((!config_fullscreen_fill_window) && (config_fill_window))") > chrome.find("if ((!_viv_is_fullscreen) && (config_fullscreen_fill_window))"))
    check("the boundary arithmetic keeps the old scans' edge semantics",
          chrome.count("(lo > 0) ? (lo - 1) : 0") == 1 and
          chrome.count("(lo > 0) ? -(lo - 1) : 0") == 1)
    check("the precompute arrays are gone",
          "zoom_wide_array" not in chrome and "zoom_high_array" not in chrome)
    check("the restore path never measures",
          0 <= chrome.find("_viv_get_render_size(&old_rw,&old_rh);") < chrome.find("if (_viv_is_fullscreen)"))

    # 3. the refusal flags: the interlocked macro trio.
    check("the refusal macros mirror the cancel flag's forms",
          "#define _VIV_LOAD_REFUSED_READ(flag) (InterlockedCompareExchange(&(flag),0,0) != 0)" in state and
          "#define _VIV_LOAD_REFUSED_SET(flag) InterlockedExchange(&(flag),1)" in state and
          "#define _VIV_LOAD_REFUSED_CLEAR(flag) InterlockedExchange(&(flag),0)" in state)
    check("no bare refusal write survives anywhere",
          "= _viv_load_refused_budget" not in viv + vivload + chrome and
          "= _viv_load_refused_input_size" not in viv + vivload + chrome)

    # 4. the classic options retirement: nothing opens, nothing compiles.
    for dead in ("_viv_options_proc", "_viv_options_general_proc",
                 "_viv_options_view_proc", "_viv_options_controls_proc",
                 "_viv_options_tab_proc", "_viv_edit_key_proc",
                 "_viv_edit_key_edit_proc", "_viv_edit_key_set_key",
                 "_viv_options_edit_key", "_viv_options_key_list_sel_change",
                 "_viv_options_remove_key", "_viv_options_treeview_changed",
                 "_viv_options_update_sheild", "_viv_edit_key_remove_currently_used_by",
                 "_viv_edit_key_changed"):
        check("the dialogs domain drops %s" % dead, dead not in dialogs)
    check("the dialogs header drops the redirect prototype",
          "void _viv_options(void);" not in read("src/viv_dialogs.h").decode())
    check("the state layer drops the page-id export",
          "extern int _viv_options_page_ids[];" not in state and
          "int _viv_options_page_ids[]" not in viv)
    check("the page-count define stays for the settings window",
          "#define _VIV_OPTIONS_PAGE_COUNT" in state and
          state.count("_VIV_OPTIONS_PAGE_COUNT") >= 1)
    for dead_id in ("IDD_GENERAL", "IDD_OPTIONS", "IDD_VIEW", "IDD_CONTROLS", "IDD_EDIT_KEY"):
        check("the rc drops the %s template" % dead_id,
              (dead_id + " DIALOGEX") not in rc and ("#define " + dead_id + " ") not in resh)
    for dead_ctl in ("IDC_TAB1", "IDC_BMP", "IDC_WEBP", "IDC_COMMANDS_LIST",
                     "IDC_KEYS_LIST", "IDC_EDIT_KEY_EDIT", "IDC_DARKMODE",
                     "IDC_LANGUAGE", "IDC_ASSOCIATIONS_GROUPBOX"):
        check("the resource header drops %s" % dead_ctl,
              ("#define " + dead_ctl + " ") not in resh)
    check("the settings ids keep their numbers (no renumber)",
          "#define _VIV_SETTINGS_ID_CANCEL\t\t40" in settings)

    # 5. the borderless corner: the strip answers the grip box.
    check("the status bar answers the grip box in the manual layout",
          "case WM_NCHITTEST:" in chrome and
          "return HTBOTTOMRIGHT;" in chrome and
          "(!config_show_thickframe) && (!_viv_is_fullscreen) && (!IsZoomed(_viv_hwnd))" in chrome)
    check("the strip forwards the press to the parent size loop",
          "PostMessage(GetParent(hwnd),WM_SYSCOMMAND,(SC_SIZE | WMSZ_BOTTOMRIGHT),lParam);" in chrome)
    check("the dark grip repaint joins the manual mode",
          "((GetWindowLong(_viv_hwnd,GWL_STYLE)) & WS_THICKFRAME) || (!config_show_thickframe)" in chrome)
    check("the manual edge band widens to the padded border",
          "band_x = GetSystemMetrics(SM_CXSIZEFRAME) + GetSystemMetrics(SM_CXPADDEDBORDER);" in wnd and
          "#define SM_CXPADDEDBORDER 92" in wnd and
          "GetSystemMetrics(SM_CXSIZEFRAME))" not in wnd[wnd.index("static LRESULT _viv_on_wm_nchittest"):wnd.index("static LRESULT _viv_on_wm_nclbuttondown")])

    # 6. the icon-only toolbar: config, measure, paint, menu, settings.
    check("the config declares the icon-only default off",
          "BYTE config_toolbar_icon_only = 0;" in configc and
          'config_toolbar_icon_only = ini_get_int(ini,(const utf8_t *)"toolbar_icon_only",config_toolbar_icon_only);' in configc and
          '_config_write_int(h,"toolbar_icon_only",config_toolbar_icon_only);' in configc)
    check("the measure shrinks the button to the glyph",
          "_viv_toolbar_item_wide[itemi] = (_viv_toolbar_button_pad * 2) + _viv_toolbar_icon_size;" in toolbar and
          "GetTextExtentPoint32W" in toolbar)
    check("the paint centers the glyph and skips the label",
          "config_toolbar_icon_only ? (_viv_toolbar_item_x[itemi] + ((_viv_toolbar_item_wide[itemi] - _viv_toolbar_icon_size) / 2) + offset)" in toolbar and
          "if (!config_toolbar_icon_only)" in toolbar)
    check("the context menu carries the row and the check",
          "LOCALIZATION_ID_TOOLBAR_ICON_ONLY" in toolbar and
          "_VIV_TOOLBAR_CONTEXT_ID_FIRST + 8" in toolbar and
          "CheckMenuItem(hmenu,command_id,config_toolbar_icon_only ? MF_CHECKED : MF_UNCHECKED);" in toolbar)
    check("the command flips the flag and re-measures",
          "config_toolbar_icon_only = config_toolbar_icon_only ? 0 : 1;" in toolbar)
    check("the interceptor range covers the ninth row",
          "_VIV_TOOLBAR_CONTEXT_ID_FIRST + 9)" in wnd)
    check("the settings row wires all four points",
          "_VIV_SETTINGS_ID_TOOLBARICON" in settings and
          "LOCALIZATION_ID_SETTINGS_TOOLBAR_ICON_ONLY" in settings and
          "config_toolbar_icon_only ? 1 : 0" in settings and
          "_viv_settings_snap_toolbar_icon_only" in settings)
    check("the toggle runs the size sweep and the restore follows",
          settings.count("_viv_on_size();") >= 2)
    check("both languages carry the icon-only pair",
          "LOCALIZATION_ID_TOOLBAR_ICON_ONLY," in loc_h and
          "LOCALIZATION_ID_SETTINGS_TOOLBAR_ICON_ONLY," in loc_h and
          '"Icons only", // LOCALIZATION_ID_TOOLBAR_ICON_ONLY' in loc_e and
          '"Toolbar icons only", // LOCALIZATION_ID_SETTINGS_TOOLBAR_ICON_ONLY' in loc_e and
          '"\u4ec5\u56fe\u6807", // LOCALIZATION_ID_TOOLBAR_ICON_ONLY' in loc_z and
          '"\u5de5\u5177\u680f\u4ec5\u663e\u793a\u56fe\u6807", // LOCALIZATION_ID_SETTINGS_TOOLBAR_ICON_ONLY' in loc_z)

    # 7. the paste text fallback: a copied path opens.
    check("the image reader answers a verdict",
          "BOOL _viv_paste_clipboard_image(void)" in vivload and
          "void _viv_paste_clipboard_image(void)" not in vivload)
    check("the paste takes the text when the image readers miss",
          "if (!_viv_paste_clipboard_image())" in wnd and
          "GetClipboardData(CF_UNICODETEXT);" in wnd)
    check("the text copy is bounded by the global's own size",
          "text_count = GlobalSize(hglobal) / sizeof(wchar_t);" in wnd and
          "wbuf[text_count] = 0;" in wnd)
    q = chr(39) + chr(34) + chr(39)  # the C literal for a double-quote char
    check('the trim strips whitespace and copy-as-path quotes',
          ('wchar_is_ws(*path_start)) || (*path_start == ' + q + ')') in wnd)
    check("the extension gate and the existence check precede the open",
          "string_icompare_lowercase_ascii(extension,_viv_supported_extensions[exti]) == 0" in wnd and
          "os_GetFileAttributesExW(path_start,GetFileExInfoStandard,&find_data)" in wnd and
          "_viv_open_from_filename(path_start,VIV_OPEN_RECENT);" in wnd)

def t_audit_response_round113():
    """Guards for the sixth audit response round (1.1.15-rc.6: the ime
    dissociation, the init answers, the reply wakeup duty, the offset
    validators, the uninstaller staging and the license set - every
    claim re-verified against the tree before anything landed, two p0s
    real, seven green-light claims refuted with the evidence recorded)."""
    viv = read("src/viv.c").decode("utf-8", errors="replace")
    vivload = read("src/viv_load.c").decode("utf-8", errors="replace")
    wnd = read("src/viv_wndproc.c").decode("utf-8", errors="replace")
    settings = read("src/viv_settings.c").decode("utf-8", errors="replace")
    dialogs = read("src/viv_dialogs.c").decode("utf-8", errors="replace")
    zoomui = read("src/zoomui.c").decode("utf-8", errors="replace")
    osc = read("src/os.c").decode("utf-8", errors="replace")
    osh = read("src/os.h").decode("utf-8", errors="replace")
    state = read("src/viv_state.h").decode("utf-8", errors="replace")
    webp = read("src/webp.c").decode("utf-8", errors="replace")
    glyphs = read("src/glyphs.c").decode("utf-8", errors="replace")
    qoi = read("src/qoi.c").decode("utf-8", errors="replace")
    install = read("src/viv_install.c").decode("utf-8", errors="replace")
    nsi = read("nsis/installer.nsi").decode("utf-8-sig")
    notices = read("THIRD_PARTY_NOTICES.md").decode("utf-8")
    security = read("SECURITY.md").decode("utf-8")
    buildsh = read("build-zig/build.sh").decode("utf-8", errors="replace")
    buildarm = read("build-zig/build-arm64.sh").decode("utf-8", errors="replace")
    zoommath = read("tests/zoom_math_test.py").decode("utf-8")
    version = read("src/version.h").decode("utf-8", errors="replace")

    # 1. the ime dissociation: the wrapper, the four window families, the
    #    link line on both toolchains, and the fixme that asked the same
    #    question one step later.
    check("the ime dissociation wrapper exists",
          "void os_imm_associate_disable(HWND hwnd);" in osh and
          "void os_imm_associate_disable(HWND hwnd)" in osc and
          "ImmAssociateContext(hwnd,NULL);" in osc)
    check("the wrapper links imm32 on both toolchains",
          "#pragma comment(lib,\"imm32.lib\")" in osc and
          "#include <imm.h>" in osc and
          "-limm32" in buildsh and
          "-limm32" in buildarm)
    # r114: the edit-key capture dissociation left with the classic
    # dialogs it lived in (four live surfaces remain).
    check("the canvas, the pill, the zoom editor and the settings dissociate",
          "os_imm_associate_disable(_viv_hwnd);" in viv and
          "os_imm_associate_disable(_zoomui_hwnd);" in zoomui and
          "os_imm_associate_disable(hwnd);" in dialogs and
          "os_imm_associate_disable(GetDlgItem(hwnd,IDC_EDIT_KEY_EDIT));" not in dialogs and
          "os_imm_associate_disable(_viv_settings_hwnd);" in settings)
    check("the processkey fixme retires (the dissociation answers it earlier)",
          "case VK_PROCESSKEY" not in dialogs and
          "ImmGetVirtualKey" not in dialogs and
          "dissociation answers the same question one step earlier" not in dialogs)  # r114: the comment left with the classic edit-key dialog

    # 2. the init answers: the atom escapes the wrapper, the three steps
    #    fail out loud, and the failure box localizes through the utf-8 bridge.
    check("the class registration returns its atom",
          "int os_RegisterClassEx(UINT style,WNDPROC lpfnWndProc,HICON hIcon,HCURSOR hCursor,HBRUSH hbrBackground,const utf8_t *name,HICON hIconSm);" in osh and
          "return RegisterClassExW(&wcex) ? 1 : 0;" in osc)
    check("the three init steps fail out loud",
          viv.count("_viv_init_failed((int)GetLastError());") == 3 and
          viv.count("_viv_kill();\r\n\t\t\r\n\t\treturn -1;") == 3)
    check("the init-failure string rides the localization tables",
          "LOCALIZATION_ID_INIT_FAILED" in read("src/localization.h").decode() and
          "failed to initialize" in read("src/localization_en_us.h").decode() and
          "初始化失败" in read("src/localization_zh_cn.h").decode())
    check("the failure box crosses the utf-8 bridge (the mojibake rule)",
          "string_copy_utf8_string(text_wbuf,localization_get_string(LOCALIZATION_ID_INIT_FAILED));" in viv and
          "string_copy(title_wbuf,localization_get_string(" not in viv)

    # 3. the reply wakeup duty: the flag, the refused-post hand-back, and
    #    the drain's tail repost.
    check("the reply posted flag exists in the state layer",
          "extern int _viv_reply_posted;" in state)
    check("a refused post hands the duty back",
          "if ((is_first) && (!PostMessage(_viv_hwnd,_VIV_WM_REPLY,0,0)))" in vivload and
          "_viv_reply_posted = 0;" in vivload)
    check("the drain clears the duty and re-posts for stragglers",
          "_viv_reply_posted = 0;" in wnd and
          "need_post = ((_viv_reply_start) && (!_viv_reply_posted));" in wnd)

    # 4. the interlocked cancel: the type, the macro, and the retirement of
    #    the plain writes (comments excepted).
    check("the cancel flag rides volatile LONG with the interlocked read macro",
          "extern volatile LONG _viv_load_image_terminate;" in state and
          "#define _VIV_LOAD_TERMINATED() (InterlockedCompareExchange(&_viv_load_image_terminate,0,0) != 0)" in state)
    flat = " ".join(vivload.split())
    check("no plain cancel write survives in the load domain",
          "_viv_load_image_terminate = 1" not in " ".join(l for l in vivload.splitlines() if not l.strip().startswith("//")) and
          "InterlockedExchange(&_viv_load_image_terminate,1);" in vivload and
          "InterlockedExchange(&_viv_load_image_terminate,0);" in vivload)
    check("the loader checkpoints read through the macro",
          "if ((i) && (_VIV_LOAD_TERMINATED()))" in vivload and
          "if (_VIV_LOAD_TERMINATED())" in qoi)

    # 5. the pairing flags: com on both threads, gdi+ on the viewer and the
    #    glyphs, and the webp null guard.
    check("the ui thread pairs its com and gdi+ teardown",
          "_viv_com_initialized = SUCCEEDED(CoInitializeEx(NULL, COINIT_APARTMENTTHREADED|COINIT_DISABLE_OLE1DDE));" in viv and
          "if (_viv_com_initialized)" in viv and
          "_viv_gdiplus_started = (gdiplus_ret == 0) ? 1 : 0;" in viv and
          "if ((os_GdiplusShutdown) && (_viv_gdiplus_started))" in viv)
    check("the loader thread pairs its own com",
          "com_initialized = SUCCEEDED(CoInitializeEx(NULL, COINIT_APARTMENTTHREADED|COINIT_DISABLE_OLE1DDE));" in vivload and
          "if (com_initialized)" in vivload)
    check("the glyphs startup gates its state",
          "if (os_GdiplusStartup(&_glyphs_gdiplus_token,&input,0) == 0)" in glyphs)
    check("the webp delay write matches the read side's null guard",
          "if ((frame_delays) && (WebPDemuxGetFrame(demux,1,&iter)))" in webp)

    # 6. the pending-clear belt and the offset validators.
    check("the pending mailbox drains before it takes (the single-flight belt)",
          "if (_viv_pending_clear_frames)\r\n\t{" in vivload and
          "the mailbox is single-slot by the single-flight invariant" in vivload)
    check("the validator takes the offset, never a pre-built pointer",
          "int _viv_safe_copy_data(const void *base,SIZE_T src_size,SIZE_T offset,void *dst,SIZE_T dst_size)" in vivload and
          "cds->lpData,cds->cbData,((char *)cds->lpData)" not in wnd and
          "((const char *)cds->lpData) + item->data_offset" not in read("src/viv_playlist.c").decode("utf-8", errors="replace"))
    check("the item walk carries distances end to end",
          "static SIZE_T _viv_copydata_read(const COPYDATASTRUCT *cds,SIZE_T offset,void *dst,DWORD size)" in read("src/viv_playlist.c").decode("utf-8", errors="replace"))
    check("the copydata handler refuses a length with no buffer",
          "if ((!cds->lpData) && (cds->cbData))" in wnd)

    # 7. the uninstaller staging and the license set.
    check("the second stage runs from an unpredictable fresh directory",
          "GetTempFileName $0 $Temp" in nsi and
          "CreateDirectory $0" in nsi and
          'IfFileExists "$0\\voidImageViewer.exe" run_second_stage' in nsi and
          "RMDir /REBOOTOK $0" in nsi)
    check("the fixed temp name is gone",
          "$Temp\\voidImageViewer.exe" not in nsi)
    check("the license pair rides the payload, the install and the uninstall",
          'File "..\\LICENSE"' in nsi and
          'File "..\\THIRD_PARTY_NOTICES.md"' in nsi and
          '(const utf8_t *)"LICENSE",0);' in install and
          '(const utf8_t *)"THIRD_PARTY_NOTICES.md",0);' in install and
          install.count('(const utf8_t *)"THIRD_PARTY_NOTICES.md")') == 1)
    check("the qoi attribution is complete",
          "Dominic Szablewski" in notices and
          "Copyright (c) 2021, Dominic Szablewski" in notices and
          "phoboslab.org" in notices)
    check("the security table rides the shipped lines",
          "| 1.1.14 | latest stable | yes |" in security and
          "1.1.15-rc.16 at the time of writing" in security)

    # 8. the pill's keyboard exit and the suite's own two defects.
    check("the pill answers escape with focus back to the viewer",
          "case VK_ESCAPE:" in zoomui and
          "SetFocus(_zoomui_parent_hwnd);" in zoomui and
          "is the keyboard way back to the viewer" in zoomui)
    check("zoom math carries its conditions inside the checks (no -O blindness)",
          'check("geometric ladder = fit * 1.01^pos (+/-2px)", ok)' in zoommath and
          zoommath.count(", True)") == 3)  # the pinch group's guarded aggregate + two 1:1 data tuples
    check("the born-dead tautology is gone",
          ("assert m " + "or True") not in read("tests/menu_structure_test.py").decode("latin-1"))

    # 9. the version mark.
    check("version.h = 1.1.15-rc.11.90 (the sixth audit response round)",
          '#define VERSION_BUILD 95' in version and
          '#define VERSION_STRING "1.1.15-rc.16"' in version)


def t_command_picker_round112():
    """Guards for round 112 (1.1.15-rc.4: the command picker round. the
    user report: keyboard shortcuts could not be added - because the
    settings window's command dropdown capped at the popup label
    store's thirty-two rows while the command table holds one hundred
    and eighteen pickable commands, leaving eighty-six unreachable
    from the only shortcut editor the app ships. the fifth audit had
    this on its deferred ledger as b1 and called the unlock "not a
    one-liner's blast radius" - right, because a flat list of 118 rows
    is taller than any screen, so the picker became the real menu
    tree: the same walk the frame menu runs, every leaf owner drawn
    with its live shortcut, the current command radio checked. the
    round also ran the entry-point census the report asked for: every
    command row dispatches, every toolbar and context row routes
    through the same wm_command, every settings control id answers,
    and the ini round-trip covers all one hundred and eighteen
    names)."""
    settings = read("src/viv_settings.c").decode("latin-1")
    menu = read("src/viv_menu.c").decode("latin-1")
    viv = read("src/viv.c").decode("latin-1")
    version = read("src/version.h").decode()
    readme = read("README.md").decode("utf-8", errors="replace")
    experience = read("experience.md").decode("utf-8", errors="replace")
    changes = read("Changes.txt").decode("utf-8", errors="replace")

    # 1. the version mark
    check("version.h = 1.1.15-rc.11.90 (the command picker round)",
          "#define VERSION_BUILD 95" in version and
          '#define VERSION_STRING "1.1.15-rc.16"' in version)

    # 2. the command dropdown answers through the cascade picker, and
    #    the flat call that used to feed it retired.
    check("the command dropdown answers through the cascade picker",
          "static int _viv_settings_command_picker(HWND hwnd,const RECT *anchor)" in settings and
          "selected = _viv_settings_command_picker(hwnd,&drop_ctl->value);" in settings and
          "_viv_settings_command_at(selected);" not in settings)

    # 3. the flat path retired: no commands kind, no item count.
    check("the flat commands kind retired",
          "_VIV_SETTINGS_POPUP_COMMANDS" not in settings and
          "_viv_settings_command_item_count" not in settings)

    # 4. the cascade walks the table like the frame menu does
    check("the cascade walks every table row",
          "menus[_VIV_MENU_ROOT] = menu;" in settings and
          settings.count("_viv_menu_row_alloc(_VIV_MENU_POOL_SETTINGS,") >= 4 and
          "_VIV_MENU_DRAW_POPUP,i,0,0" in settings and
          "_VIV_MENU_DRAW_COMMAND,i,0,0" in settings and
          "_VIV_MENU_DRAW_SEPARATOR,0,0,0" in settings)

    # 5. the hidden delete is not a pickable command
    check("the hidden delete is skipped",
          "the hidden plain delete is not a pickable command" in settings)

    # 6. the id contract: table index plus one, zero stays the cancel answer
    check("the leaf id carries the table index (offset one, zero is cancel)",
          "mii.wID = (UINT)(i + 1);" in settings and
          "if ((ret <= 0) || (ret > _VIV_COMMAND_COUNT))" in settings and
          "return ret - 1;" in settings)

    # 7. the current command radios in the cascade
    check("the current command is radio checked",
          "mii.fState = (i == _viv_settings_command_index) ? MFS_CHECKED : 0;" in settings and
          "mii.fType = MFT_RADIOCHECK;" in settings)

    # 8. the settings row pool sizes to the command count (the 32 cap is gone)
    check("the settings row pool sizes to the command count",
          "static _viv_menu_draw_t _viv_settings_draw_pool[_VIV_COMMAND_COUNT];" in menu and
          "cap = _VIV_COMMAND_COUNT;" in menu and
          "_viv_settings_draw_pool[32]" not in menu)

    # 9. the flat store stays for the short lists, with its narrowed role
    check("the flat label store keeps the short lists",
          "#define _VIV_SETTINGS_POPUP_MAX		32" in settings and
          "this store serves the flat kinds only" in settings)

    # 10. the opening default still filters (first listable command)
    check("the opening default keeps the filter",
          "_viv_settings_command_index = _viv_settings_command_at(0);" in settings)

    # 11. the default keys all install onto real table commands (the
    #     entry-point census's keyboard leg).
    check("the default keys all install onto real commands",
          viv.count("{VIV_ID_") == 62 and  # r114: the options page-id array left with the classic dialogs
          "_viv_command_index_from_command_id(_viv_default_keys[i].command_id)" in viv)

    # 12. the changelog, the readme and the candidate rotation
    check("the changelog tops with the command picker round",
          changes.lstrip("\ufeff").startswith("Pre-release: Version 1.1.15-rc.16 (the parallel evaluation round)"))
    check("the changelog carries the round's own ledger",
          "eighty-six commands\r\n\twere unreachable" in changes and
          "the cascade is the real menu tree" in changes and
          "walked, every surface verified against the dispatcher" in changes)
    check("the readme candidate block and the rc.3 one-liner",
          "**1.1.15-rc.6 \u2014" in readme and
          "### 1.1.15-rc.3 \u2014" in experience and
          readme.count("(the current release candidate):**") == 1)



# ---------------------------------------------------------------------------
# 1.1.15-rc.11: the seventh audit response round. the full-project review
# with line numbers: the clipboard validation order, the CF_BITMAP budget,
# the displayed identity binding, the immutable preload snapshot, the
# reply tail's duty hand-back, the config write latch, the clean zig
# object directories and the release-chain trust hardening.
# ---------------------------------------------------------------------------
def t_audit_response_round118():
    """Guards for the seventh audit response round (1.1.15-rc.8). every
    claim from the report was verified against the tree before it moved;
    these pins hold the shapes the fixes landed in."""
    print("the seventh audit response round (1.1.15-rc.11)")
    vivload = read("src/viv_load.c").decode()
    view = read("src/viv_view.c").decode()
    wndproc = read("src/viv_wndproc.c").decode()
    config = read("src/config.c").decode()
    toolbar = read("src/viv_toolbar.c").decode()

    # 1. the CF_DIB order: size before shape. the length gate runs
    #    before the lock; the whole-dib total runs before the section is
    #    created; the hostile corners fall through with the malformed
    #    headers; the stride and the copy length ride the safe math.
    check("the dib paste measures the global before it locks it",
          "dib_size = (SIZE_T)GlobalSize(hglobal);" in vivload and
          "if (dib_size >= sizeof(BITMAPINFOHEADER))" in vivload)
    check("the dib paste proves the whole dib before the section is created",
          "if ((total_needed != SIZE_MAX) && (dib_size >= total_needed))" in vivload and
          vivload.find("total_needed != SIZE_MAX") <
          vivload.find("CreateDIBSection(screen_hdc,(BITMAPINFO *)bih"))
    check("the hostile dib corners fall through",
          "bih->biHeight != (-2147483647 - 1)" in vivload and
          "bih->biClrUsed <= (DWORD)(1 << bih->biBitCount)" in vivload)
    check("the dib stride and copy lengths ride the safe math",
          "stride = safe_size_mul(safe_size_add(safe_size_mul((SIZE_T)bih->biWidth,(SIZE_T)bih->biBitCount),31) / 32,4);" in vivload and
          "os_copy_memory(bits,src,pixels_size);" in vivload)

    # 2. the CF_BITMAP budget prices the copy before CopyImage allocates.
    check("the bitmap paste prices the copy before it runs",
          "(!_viv_pixel_budget_refused(safe_size_mul((SIZE_T)bm.bmWidth,(SIZE_T)bm.bmHeight)))" in vivload and
          vivload.find("GetObject(hbitmap,sizeof(BITMAP),&bm)") <
          vivload.find("CopyImage(hbitmap,IMAGE_BITMAP"))

    # 3. the displayed identity: every file-acting command reads the
    #    slot's fd; the navigation family keeps the requested file.
    check("delete binds to the displayed slot's fd",
          "string_copy_double_null(filename_list,_viv_slot_current.fd.cFileName);" in view and
          "os_copy_memory(&fd,&_viv_slot_current.fd,sizeof(WIN32_FIND_DATA));" in view)
    check("the shell verb family binds to the displayed slot's fd",
          'os_shell_execute(_viv_hwnd,_viv_slot_current.fd.cFileName,0,"properties",0);' in view and
          'counterclockwise ? "rotate270" : "rotate90"' in view and
          '_viv_slot_current.fd.cFileName,0,counterclockwise' in view)  # round-125: the ui-thread wait is gone
    # round-125: the successor window adds two readers (the serves()
    # comparison and the begin() anchor) - both hold the fd, never
    # write it.
    check("the navigation family keeps the requested fd (17 readers - the delete's re-anchor, the window's two)",
          view.count("_viv_current_fd") == 17)
    check("the toolbar's has-image gate answers the image on screen",
          "(*_viv_slot_current.fd.cFileName)" in toolbar)

    # 4. the immutable job snapshot: the thread reads the flag exactly
    #    once, at entry, and its two uses ride the local.
    check("the loader thread captures the preload flag once at entry",
          "is_preload_job = _viv_load_is_preload;" in vivload and
          vivload.count("is_preload_job ?") == 1 and
          vivload.count("(!is_preload_job)") == 1)

    # 5. the reply tail hands a refused post's duty back (rc.5 closed
    #    the enqueue side; the tail post now checks its return value).
    check("the reply tail repost checks its post's return value",
          "if (!PostMessage(hwnd,_VIV_WM_REPLY,0,0))" in wndproc and
          "the tail post answers the same duty" in wndproc)

    # 6. the config save latches its write failures and replaces
    #    write-through behind a flush.
    check("the config save latches its write failures",
          "static int _config_write_failed = 0;" in config and
          "_config_write_failed = 1;" in config and
          "FlushFileBuffers(h);" in config)
    check("the config replace runs write-through",
          "MoveFileExW(tempname,filename,MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)" in config)

    # 7. the zig builds start from a wiped object directory - a deleted
    #    source must not leave its .o behind for the link glob to
    #    resurrect.
    build_sh = read("build-zig/build.sh").decode()
    build_arm = read("build-zig/build-arm64.sh").decode()
    build_self = read("build-zig/build-selfshot.sh").decode()
    check("every zig build starts from a clean object directory",
          'rm -rf "$OBJDIR"' in build_sh and
          'rm -rf "$OBJDIR"' in build_arm and
          'rm -rf "$OBJDIR"' in build_self)

    # 8. the release chain: the job-scoped actions:write, the per-tag
    #    concurrency lock, the pinned NSIS, the check-only encodings,
    #    the attested checksum file and the whitelisted installer
    #    parameters.
    tests_yml = read(".github/workflows/tests.yml").decode()
    release_yml = read(".github/workflows/release.yml").decode()
    installer = read("nsis/build_installer.ps1").decode()
    enc = read("nsis/ensure_encodings.ps1").decode()
    check("ci's actions:write is gone entirely (the upload runs on the default token)",
          "actions: write" not in tests_yml)
    check("releases serialize per tag",
          "group: release-${{ inputs.tag || github.ref_name }}" in release_yml and
          "cancel-in-progress: false" in release_yml)
    check("NSIS is pinned to the 3.12 family before any installer is built (both copies)",
          release_yml.count("-notmatch \'^v?3\\.12(\\.\\d+)?$\'") == 2 and
          "--allow-downgrade" in release_yml)
    check("the encoding check runs check-only in ci",
          "ensure_encodings.ps1 -CheckOnly" in release_yml and
          "param([switch]$CheckOnly)" in enc)
    check("sha256.txt joins the attested subjects",
          "dist/sha256.txt" in release_yml)
    check("the installer parameters are whitelisted",
          '[ValidateSet("x86","x64")]' in installer and
          '[ValidateSet("vs2019","vs2026")]' in installer)

    # 9. the readme twins: the language links point both ways and the
    #    floating-controls row describes the row that exists.
    readme = read("README.md").decode("utf-8", errors="replace")
    readme_cn = read("README_CN.md").decode("utf-8", errors="replace")
    check("the readme language links switch both ways",
          "[\u7b80\u4f53\u4e2d\u6587](README_CN.md)" in readme and
          "[English](README.md)" in readme_cn)
    check("the readme twins carry the same candidate and stable",
          "1.1.15-rc.14" in readme and "1.1.15-rc.14" in readme_cn and
          "1.1.14 \u2014 the current stable" in readme and
          "1.1.14 \u2014 \u5f53\u524d\u7a33\u5b9a\u7248" in readme_cn)
    check("the floating-controls row tells the one-row truth",
          "One seven-cell row in both modes" in readme and
          "\u4e03\u683c\u63a7\u4ef6\u884c" in readme_cn)


def t_memory_and_cache_round120():
    """Guards for the memory and cache round (1.1.15-rc.9). the single
    last-cache slot becomes an eight-seat LRU ring the settings size,
    the preload walks a chain of finished loads into that ring, the
    recent guard reads both known identities, the resume switch captures
    the session's last file, the animation frames stop building their
    mipmaps eagerly, and the animation gates price the mipmap's third."""
    print("the memory and cache round (1.1.15-rc.11)")
    vivload = read("src/viv_load.c").decode()
    viv = read("src/viv.c").decode()
    state = read("src/viv_state.h").decode()
    wndproc = read("src/viv_wndproc.c").decode()
    view = read("src/viv_view.c").decode()
    dialogs = read("src/viv_dialogs.c").decode()
    config = read("src/config.c").decode()
    header = read("src/config.h").decode()
    settings = read("src/viv_settings.c").decode()
    anim = read("src/viv_anim.c").decode()
    webp = read("src/webp.c").decode()
    vheader = read("src/viv.h").decode()
    loc = read("src/localization.h").decode()
    vh = read("src/version.h").decode()

    # 1. the cache ring: eight seats, the LRU head at [0].
    check("the cache ring declares eight seats in the state layer",
          "#define VIV_CACHE_SLOTS\t8" in state and
          "extern _viv_image_slot_t _viv_slot_cache[VIV_CACHE_SLOTS];" in state)
    check("the ring's chain counter is exported",
          "extern int _viv_preload_chain_count;" in state)
    check("the ring definition and counter live in viv.c",
          "_viv_image_slot_t _viv_slot_cache[VIV_CACHE_SLOTS];" in viv and
          "int _viv_preload_chain_count = 0;" in viv)
    check("the kill path releases every ring seat",
          "for(i=0;i<VIV_CACHE_SLOTS;i++)" in viv and
          "_viv_clear_frames(_viv_slot_cache[i].frames,_viv_slot_cache[i].frame_loaded_count);" in viv)

    # 2. the ring primitives: find, insert, activate.
    check("the ring find scans the seats",
          "static int _viv_cache_find(const wchar_t *filename)" in vivload and
          "string_compare(_viv_slot_cache[i].fd.cFileName,filename)" in vivload)
    check("the ring insert drops the oldest when full then shifts down",
          "static void _viv_cache_insert(_viv_image_slot_t *src)" in vivload and
          "_viv_slot_clear_frames(&_viv_slot_cache[config_cache_count-1]);" in vivload and
          "_viv_slot_take(&_viv_slot_cache[0],src);" in vivload)
    check("the push rides the insert primitive under the count gate",
          "_viv_cache_insert(&_viv_slot_current);" in vivload and
          vivload.find("if (config_cache_count > 0)") <
          vivload.find("_viv_cache_insert(&_viv_slot_current);"))
    check("the activation closes the hole and detaches the vacated tail",
          "static void _viv_cache_activate(int index)" in vivload and
          "for(i=index;i<active-1;i++)" in vivload and
          "_viv_slot_cache[active-1].frames = NULL;" in vivload)
    check("the activation restores the saved image through the insert",
          "_viv_cache_insert(&old_slot);" in vivload)
    check("the ring clear wipes every seat",
          "void _viv_clear_last(void)\r\n{\r\n\tint i;\r\n\t\r\n\tfor(i=0;i<VIV_CACHE_SLOTS;i++)\r\n\t{\r\n\t\t_viv_slot_clear_frames(&_viv_slot_cache[i]);\r\n\t}\r\n}" in vivload)

    # 3. the cache-set ceiling sums the ring and holds its own line.
    check("the ceiling sums every ring seat",
          "total = safe_size_add(total,_viv_slot_bytes(&_viv_slot_cache[i]));" in vivload)
    check("the cache set carries its own ceiling",
          "return total > VIV_CACHE_SET_MAX_BYTES;" in vivload and
          "#define VIV_CACHE_SET_MAX_BYTES\t1200000000" in vheader)
    check("the trim walks the ring oldest-first and only touches a finished preload",
          "for(i=VIV_CACHE_SLOTS-1;i>=0;i--)" in vivload and
          "if (_viv_slot_preload.state == 1)" in vivload)

    # 4. the hit branches read the ring.
    check("the open path consults the ring before loading",
          "cache_hit = _viv_cache_find(fd->cFileName);" in vivload)
    check("the ring activation keeps the request identity in step",
          "os_copy_memory(_viv_current_fd,&_viv_slot_cache[index].fd,sizeof(WIN32_FIND_DATA));" in vivload)

    # 5. the preload chain.
    check("the chain counter resets on the hit paths and the settle",
          vivload.count("_viv_preload_chain_count = 0;") >= 2 and
          wndproc.count("_viv_preload_chain_count = 0;") >= 1 and
          "_viv_preload_chain_walk();" in wndproc)
    check("the chain walk promotes the finished preload into the ring",
          "(_viv_preload_chain_count + 1 < config_preload_count)" in vivload and
          "_viv_cache_insert(&_viv_slot_preload);" in vivload)

    # 6. the recent guard reads both identities.
    check("the recent guard skips a reload of either known identity",
          "_viv_icompare_filename(full_path_and_filename,_viv_slot_current.fd.cFileName) != 0" in vivload and
          "_viv_icompare_filename(full_path_and_filename,_viv_current_fd->cFileName) != 0" in vivload)
    check("the rename family binds to the displayed slot both ways",
          "string_compare(old_filename,_viv_slot_current.fd.cFileName) == 0" in dialogs and
          "string_copy_with_bufsize(_viv_slot_current.fd.cFileName,MAX_PATH,file_op_new_name);" in dialogs and
          "string_copy_with_bufsize(_viv_current_fd->cFileName,MAX_PATH,file_op_new_name);" in dialogs and
          "DialogBoxParam(os_hinstance,MAKEINTRESOURCE(IDD_RENAME),_viv_hwnd,_viv_rename_proc,(LPARAM)_viv_slot_current.fd.cFileName);" in dialogs)

    # 7. the resume switch.
    check("the resume switch and the last-file record are declared",
          "extern BYTE config_resume_last_file;" in header and
          "extern wchar_t config_last_file[MAX_PATH];" in header)
    check("the exit path captures the displayed file after the fold",
          viv.find("_viv_recent_save_fold();") <
          viv.find("string_copy_with_bufsize(config_last_file,MAX_PATH,_viv_slot_current.fd.cFileName);"))
    check("the endsession path captures too",
          "string_copy_with_bufsize(config_last_file,MAX_PATH,_viv_slot_current.fd.cFileName);" in wndproc)
    check("the startup resume rides the blank-open else",
          "if ((!_viv_export_mode) && (config_resume_last_file) && (config_last_file[0]))" in viv and
          "_viv_open_from_filename(config_last_file,VIV_OPEN_RECENT)" in viv)

    # 8. the count settings and their migration.
    check("the counts are int config with migration fallbacks",
          'ini_get_int(ini,(const utf8_t *)"preload_count",-1)' in config and
          'ini_get_int(ini,(const utf8_t *)"cache_count",-1)' in config and
          "config_preload_next ? 1 : 0" in config and
          "config_cache_last ? 1 : 0" in config)
    check("the counts clamp to their ranges",
          "config_preload_count = 5;" in config and
          "config_cache_count = 8;" in config)
    check("the save writes both keys and keeps the legacy pair in step",
          '_config_write_int(h,"preload_count",config_preload_count);' in config and
          '_config_write_int(h,"cache_count",config_cache_count);' in config and
          '_config_write_int(h,"preload_next",config_preload_count >= 1);' in config and
          '_config_write_int(h,"cache_last",config_cache_count >= 1);' in config)

    # 9. the settings rows.
    check("the two count rows are dropdowns on the view page",
          "_viv_settings_ctl_add(_VIV_SETTINGS_CT_DROPDOWN,_VIV_SETTINGS_ID_PRELOAD,0," in settings and
          "_viv_settings_ctl_add(_VIV_SETTINGS_CT_DROPDOWN,_VIV_SETTINGS_ID_CACHE,0," in settings)
    check("the resume switch rides the startup section",
          "_viv_settings_ctl_add(_VIV_SETTINGS_CT_SWITCH,_VIV_SETTINGS_ID_RESUME,0," in settings and
          "#define _VIV_SETTINGS_ID_RESUME\t13" in settings)

    # 10. the lazy mipmap and the honest animation gate.
    check("the additional frames stop building mipmaps eagerly",
          vivload.count("_viv_get_mipmap(hbitmap") == 1 and
          anim.count("_viv_get_mipmap(hbitmap") == 1)
    check("the animation gates price the mipmap's third",
          "(VIV_UINT64)frame_count * (VIV_UINT64)canvas_pixels * 16 / 3 > VIV_MAX_ANIMATION_TOTAL_BYTES" in vivload and
          "(VIV_UINT64)frame_count * (VIV_UINT64)canvas_pixels * 16 / 3 > VIV_MAX_ANIMATION_TOTAL_BYTES" in webp)

    # 11. the localization set grows the three settings strings.
    check("the three new settings strings are declared",
          "LOCALIZATION_ID_SETTINGS_RESUME_LAST" in loc and
          "LOCALIZATION_ID_SETTINGS_PRELOAD_COUNT" in loc and
          "LOCALIZATION_ID_SETTINGS_CACHE_COUNT" in loc)

    # 12. the version moved to rc.11 build 90 (the sweep that renamed this round's pins).
    check("the version moved to 1.1.15-rc.11 build 90",
          "#define VERSION_BUILD 95" in vh and
          '#define VERSION_STRING "1.1.15-rc.16"' in vh)


def t_gui_limits_round121():
    """Guards for the gui limits round (1.1.15-rc.10). the bottom-right
    pair gets its breathing room (the file stamp pane carries twelve
    logical pixels of right padding so the time and the size never read
    as one glued line), the give-way order learns priorities (the pos
    and rgb readouts die first, the frame counter next, the file stamp
    holds out to the resolution pane's own last-resort clip), the stamp
    and the size decimal learn explicit buffer budgets, the parts array
    learns a pane budget (the layout and the texts gate on the same
    one), the rc.9 count dropdowns come alive (the popup, the label, the
    value and the apply - three machines the lost surgery left dead),
    the zoom editor takes four digits, the message box owns one heap
    copy per open (the usage page is 1179 characters against the
    1023-cell store), and four stale comments tell the truth."""
    print("the gui limits round (1.1.15-rc.11)")
    chrome = read("src/viv_chrome.c").decode()
    settings = read("src/viv_settings.c").decode()
    msgbox = read("src/viv_msgbox.c").decode()
    dialogs = read("src/viv_dialogs.c").decode()
    menu = read("src/viv_menu.c").decode()
    menubar = read("src/viv_menubar.c").decode()
    version = read("src/version.h").decode()
    changes = read("Changes.txt").decode("utf-8", errors="replace")

    # 1. the breathing gap between the stamp and the size pane.
    check("the stamp pane carries the breathing gap",
          "date_wide = size.cx + GetSystemMetrics(SM_CXEDGE) * 5 + ((12 * os_logical_wide) / 96);" in chrome)
    check("the gap comment names the pair",
          "the stamp carries twelve extra logical" in chrome and
          "so the time and the size never touch" in chrome)

    # 2. the give-way order: pos, rgb, frame, date (the stamp survives
    #    longest as half of the bottom-right pair).
    i = chrome.find("while ((zoom_wide + preload_wide + dimension_wide")
    body = chrome[i:chrome.find("if (zoom_wide + preload_wide + dimension_wide > avail_wide)", i)]
    check("the give-way body orders pos before rgb",
          0 <= body.find("pixel_pos_wide = 0;") < body.find("pixel_rgb_wide = 0;"))
    check("the give-way body orders rgb before frame",
          0 <= body.find("pixel_rgb_wide = 0;") < body.find("frame_wide = 0;"))
    check("the give-way body orders frame before date (the stamp holds out longest)",
          0 <= body.find("frame_wide = 0;") < body.find("date_wide = 0;"))
    check("the give-way comment names the priority",
          "give way by" in chrome and "the stamp is half of the bottom-" in chrome)

    # 3. the explicit buffer budgets.
    check("the date concat reserves the full time tail",
          "if ((date_len) && (date_len < (STRING_SIZE - 64)))" in chrome)
    check("the decimal tail bounds its two hand-written cells",
          "if (string_get_length(widebuf) < (STRING_SIZE - 2))" in chrome)

    # 4. the pane budget: both sections gate the same way.
    check("the layout gates every optional right-cluster pane",
          chrome.count("&& (parti < (_VIV_STATUS_PART_MAX - 1))") == 8)
    check("the final pane writes are budget gated",
          chrome.count("if (parti < _VIV_STATUS_PART_MAX)") == 2)
    check("the budget comment names the lockstep rule",
          "the text section below gates on the same budget" in chrome)

    # 5. the stale comments tell the truth.
    check("the last-pane comment is single and names the resolution",
          chrome.count("// the last pane: the file date") == 0 and
          chrome.count("// the last pane: the resolution") == 1)
    check("the zoomui fullscreen comment matches the seven-cell row",
          "one seven cell row serves both modes (the zoomui header owns the" in chrome and
          "six button overlay bar" not in chrome)
    check("the menubar strip-height comment is single",
          menubar.count("// the strip height:") == 1)

    # 6. the rc.9 count dropdowns come alive (the three dead machines).
    check("the count popup kind is declared",
          "#define _VIV_SETTINGS_POPUP_COUNT\t\t3" in settings)
    check("the count ladder helper exists",
          "static void _viv_settings_count_text(int count,wchar_t *wbuf)" in settings and
          "LOCALIZATION_ID_SETTINGS_COUNT_OFF" in settings and
          "LOCALIZATION_ID_SETTINGS_COUNT_ONE" in settings and
          "LOCALIZATION_ID_SETTINGS_COUNT_MANY" in settings)
    check("the popup text answers the count kind",
          "case _VIV_SETTINGS_POPUP_COUNT:\n\t\t\t_viv_settings_count_text(index,wbuf);" in settings.replace("\r\n", "\n"))
    check("the preload dropdown runs (off / 1..5, six entries)",
          "_viv_settings_popup(hwnd,&drop_ctl->value,_VIV_SETTINGS_POPUP_COUNT,0,6,config_preload_count);" in settings)
    check("the cache dropdown runs (off / 1..8, nine entries)",
          "_viv_settings_popup(hwnd,&drop_ctl->value,_VIV_SETTINGS_POPUP_COUNT,0,9,config_cache_count);" in settings)
    settings_lf = settings.replace("\r\n", "\n")
    j = settings_lf.find("case _VIV_SETTINGS_ID_CACHE:\n\t\t{", settings_lf.find("static void _viv_settings_run_dropdown"))
    cache_case = settings_lf[j:settings_lf.find("break;", settings_lf.find("_viv_clear_last();", j))]
    check("the cache apply clears the ring",
          "_viv_clear_last();" in cache_case)
    check("the dropdown labels draw for both count rows",
          "_viv_settings_draw_label(mem,&label_rect,LOCALIZATION_ID_SETTINGS_PRELOAD_COUNT," in settings and
          "_viv_settings_draw_label(mem,&label_rect,LOCALIZATION_ID_SETTINGS_CACHE_COUNT," in settings)
    check("the dead switch-branch label cases are gone",
          "label_id = LOCALIZATION_ID_SETTINGS_PRELOAD_COUNT;" not in settings and
          "label_id = LOCALIZATION_ID_SETTINGS_CACHE_COUNT;" not in settings)
    check("the value faces read the count ladder",
          "_viv_settings_count_text(config_preload_count,wbuf);" in settings and
          "_viv_settings_count_text(config_cache_count,wbuf);" in settings)

    # 7. the zoom editor takes four digits.
    check("the zoom editor limits its input to four digits",
          "SendMessage(hwnd,EM_SETLIMITTEXT,4,0);" in dialogs)

    # 8. the message box owns one heap copy per open.
    check("the message box text store is a heap pointer",
          "static wchar_t *_viv_msgbox_text = 0;" in msgbox)
    check("the open path frees the previous copy and allocates the new one",
          "mem_free(_viv_msgbox_text);" in msgbox and
          "_viv_msgbox_text = string_alloc(text);" in msgbox and
          "string_copy(_viv_msgbox_text,text);" not in msgbox)

    # 9. the menu row painter stops falling through.
    i = menu.find("case _VIV_MENU_DRAW_LOCALIZED:")
    seg = menu[i:menu.find("case _VIV_MENU_DRAW_TEXT:", i)]
    check("the localized row does not fall through into the text row",
          "break;" in seg)

    # 10. the version and the changelog.
    top = changes.lstrip("\ufeff").split("\r\n")[0]
    check("the changelog top entry is the field response round pre-release",
          top == "Pre-release: Version 1.1.15-rc.16 (the parallel evaluation round)", top)
    check("the version is 1.1.15-rc.16 build 95",
          "#define VERSION_BUILD 95" in version and
          '#define VERSION_STRING "1.1.15-rc.16"' in version)



def t_resume_and_chain_round122():
    """Guards for the resume and chain round (1.1.15-rc.11). the field
    reported two entries the memory round shipped, both broken: the
    resume switch drew its label and toggled its config but the pill
    never rendered (the state-draw machine never got its case - the
    resurrection rounds audited the dropdown machines, the switch
    machines rode unaudited), and the preload count that promised n
    images ahead delivered disk loads (the chain promotes into a ring
    with as many seats as the cache count - a chain longer than the
    ring evicts its own head promoting its tail; three ahead on the
    default one-seat cache reloaded every image from disk). the round
    lands the missing case, the walk's seat gate, the apply coupling
    and the parked-hit early return - and the suite gains the control
    matrix walk: every switch against its three machines (activate,
    label, state draw), every dropdown against its own three (run,
    label, value face), so a control that exists without its machines
    can never pass again."""
    print("the resume and chain round (1.1.15-rc.11)")
    settings = read("src/viv_settings.c").decode()
    load = read("src/viv_load.c").decode()
    version = read("src/version.h").decode()
    changes = read("Changes.txt").decode("utf-8", errors="replace")
    readme = read("README.md").decode("utf-8", errors="replace")
    readme_cn = read("README_CN.md").decode("utf-8", errors="replace")

    s = settings.replace("\r\n", "\n")
    l = load.replace("\r\n", "\n")

    # 1. THE CONTROL MATRIX WALK - the permanent same-class guard. a
    #    control row is furniture; its machines are the lights, the
    #    labels and the switchgear. the rc.9 dropdowns and the rc.9
    #    resume pill both shipped as furniture without machines, and
    #    the row-level pins stayed green over both. this walk reads
    #    the census from the ctl_add calls and demands every machine.
    def body_of(sig):
        i = s.find(sig + "\n")
        if i < 0:
            return ""
        b = s.find("{", i)
        depth, j = 0, b
        while j < len(s):
            if s[j] == "{":
                depth += 1
            elif s[j] == "}":
                depth -= 1
                if depth == 0:
                    break
            j += 1
        return s[b:j]

    rows = re.findall(r"_viv_settings_ctl_add\(_VIV_SETTINGS_(CT_\w+),_VIV_SETTINGS_(ID_\w+)", s)
    switch_rows = [cid for ct, cid in rows if ct == "CT_SWITCH"]
    drop_rows = [cid for ct, cid in rows if ct == "CT_DROPDOWN"]
    # round-125: the settings remake - the three interface switch rows join
    # (the toolbar icon row moved pages, the census is type wide).
    check("the census knows the ten switch rows", len(switch_rows) == 10, str(switch_rows))
    check("the census knows the fourteen dropdown rows", len(drop_rows) == 14, str(len(drop_rows)))

    act = body_of("static void _viv_settings_activate(int index,int x,int y)")
    i = act.find("case _VIV_SETTINGS_CT_SWITCH:")
    k = act.find("case _VIV_SETTINGS_CT_CHECK:", i)
    act_set = set(re.findall(r"case (_VIV_SETTINGS_ID_\w+):\n", act[i:k]))

    paint = body_of("static void _viv_settings_paint(HWND hwnd)")
    i = paint.find("case _VIV_SETTINGS_CT_DROPDOWN:")
    w = paint.find("case _VIV_SETTINGS_CT_SWITCH:")
    c = paint.find("case _VIV_SETTINGS_CT_CHECK:", w)
    drop_label_set = set(re.findall(r"case (_VIV_SETTINGS_ID_\w+):\n", paint[i:w]))
    sw_region = paint[w:c]
    sw_label_set = set(re.findall(r"case (_VIV_SETTINGS_ID_\w+):\n\s*label_id", sw_region))
    sw_draw_set = set(re.findall(r"case (_VIV_SETTINGS_ID_\w+):\n\s*_viv_settings_draw_switch", sw_region))

    dd = body_of("static void _viv_settings_draw_dropdown(HDC hdc,const _viv_settings_ctl_t *ctl,int hot,int focus)")
    value_set = set(re.findall(r"case (_VIV_SETTINGS_ID_\w+):\n", dd))

    run = body_of("static void _viv_settings_run_dropdown(HWND hwnd,const _viv_settings_ctl_t *ctl)")
    run_set = set(re.findall(r"case (_VIV_SETTINGS_ID_\w+):\n", run))

    switch_ids = {"_VIV_SETTINGS_" + cid for cid in switch_rows}
    drop_ids = {"_VIV_SETTINGS_" + cid for cid in drop_rows}
    for cid in sorted(switch_rows):
        full = "_VIV_SETTINGS_" + cid
        check(f"the switch {cid[3:].lower()} answers the activate machine",
              full in act_set)
        check(f"the switch {cid[3:].lower()} answers the label machine",
              full in sw_label_set)
        check(f"the switch {cid[3:].lower()} answers the state-draw machine",
              full in sw_draw_set)
    check("the switch machines carry no dead cases",
          act_set <= switch_ids and sw_label_set <= switch_ids and sw_draw_set <= switch_ids)
    for cid in sorted(drop_rows):
        full = "_VIV_SETTINGS_" + cid
        check(f"the dropdown {cid[3:].lower()} answers the run machine",
              full in run_set)
        check(f"the dropdown {cid[3:].lower()} answers the label machine",
              full in drop_label_set)
        check(f"the dropdown {cid[3:].lower()} answers the value machine",
              full in value_set)
    check("the dropdown machines carry no dead cases",
          run_set <= drop_ids and value_set <= drop_ids and drop_label_set <= drop_ids)

    # 2. the resume pill (the field report's first entry).
    check("the resume switch draws its pill",
          "case _VIV_SETTINGS_ID_RESUME:\n\t\t\t\t\t\t_viv_settings_draw_switch(mem,ctl,config_resume_last_file ? 1 : 0,hot,focus);" in s)

    # 3. the seat gate (the field report's second entry).
    # round-125: the seat term reserves the back-navigation's seat.
    check("the chain walk stops at the ring's capacity too (and refuses an empty slot)",
          "((_viv_preload_chain_count + 1 < config_preload_count) && (_viv_preload_chain_count + 1 < config_cache_count) && (_viv_slot_preload.frames))" in l)
    check("the seat-gate comment names the upgrade field report (round-125)",
          "the upgrade field report: preload" in l and "min(preload, cache)" in l)

    # 4. the apply coupling: the promise raises the cache it needs.
    j = s.find("case _VIV_SETTINGS_ID_PRELOAD:\n\t\t{", s.find("static void _viv_settings_run_dropdown"))
    preload_case = s[j:s.find("break;\n\t\t}", j)]
    # round-125: the off-by-one hole (preload two on a one-seat cache
    # passed the old raise) - the promise needs the full cache now.
    check("the preload apply raises the cache to hold the promise",
          "if ((config_preload_count > 1) && (config_cache_count < config_preload_count))" in preload_case and
          "config_cache_count = config_preload_count;" in preload_case)
    check("the raised cache clears the ring (the count change rule)",
          "_viv_clear_last();" in preload_case)

    # 5. the parked-hit early return: no decode paid twice.
    check("a walk onto the parked file answers as-is",
          "if ((is_preload) && (_viv_slot_preload.state != 2) && (*_viv_slot_preload.fd.cFileName) && (string_compare(_viv_slot_preload.fd.cFileName,fd->cFileName) == 0))" in l)
    check("the parked-hit branch sits before the fresh dispatch",
          0 <= l.find("_viv_slot_preload.state != 2") < l.find("// clear any existing preload and start a fresh one."))
    check("a failed preload still retries (the state-2 fall-through)",
          "a failed preload\n\t\t// falls through - the retry may answer a transient lock." in l)

    # 6. the version, the changelog and the readme twins.
    top = changes.lstrip("\ufeff").split("\r\n")[0]
    check("the changelog top entry is the field response round pre-release",
          top == "Pre-release: Version 1.1.15-rc.16 (the parallel evaluation round)", top)
    check("the version is 1.1.15-rc.16 build 95",
          "#define VERSION_BUILD 95" in version and
          '#define VERSION_STRING "1.1.15-rc.16"' in version)
    check("the readme twins carry the round as the candidate",
          "**1.1.15-rc.16 \u2014 the parallel evaluation round (the current release candidate):**" in readme and
          "**1.1.15-rc.16 \u2014 \u5e76\u884c\u8bc4\u4f30\u8f6e\uff08\u5f53\u524d\u5019\u9009\u7248\uff09\uff1a**" in readme_cn)


def t_field_response_round124():
    """Guards for the field response round (1.1.15-rc.13: the nine-item
    field report - the upgrade's lost cache-last seat, the per-step
    folder rescans, the dead animation toolbar, the stale menu rows,
    the hardware legs' flipped launch, the pill's black corners, the
    weird settings face, the sub-second slideshow entries. every pin
    below bites a machine or an absence, never a mention."""
    print("the field response round (1.1.15-rc.14)")
    viv = read("src/viv.c").decode("utf-8", errors="replace")
    vivh = read("src/viv.h").decode("utf-8", errors="replace")
    menu = read("src/viv_menu.c").decode("utf-8", errors="replace")
    view = read("src/viv_view.c").decode("utf-8", errors="replace")
    load = read("src/viv_load.c").decode("utf-8", errors="replace")
    state = read("src/viv_state.h").decode("utf-8", errors="replace")
    anim = read("src/viv_anim.c").decode("utf-8", errors="replace")
    d3d = read("src/hwd3d.c").decode("utf-8", errors="replace")
    gl = read("src/hwgl.c").decode("utf-8", errors="replace")
    render = read("src/viv_render.c").decode("utf-8", errors="replace")
    loc = read("src/localization.h").decode("utf-8", errors="replace")
    en = read("src/localization_en_us.h").decode("utf-8", errors="replace")
    zh = read("src/localization_zh_cn.h").decode("utf-8", errors="replace")
    toolbar = read("src/viv_toolbar.c").decode("utf-8", errors="replace")

    # --- the retired dead rows (the keyboard persists by name, so the
    #     enum renumber is inert to saved bindings) ---
    check("the slideshow stop id retired everywhere (three-site dead code)",
          all("VIV_ID_SLIDESHOW_STOP" not in s for s in (vivh, menu, view, viv)))
    # word-boundary matters: RATE_5000 and RATE_50000 contain the
    # retired spellings as substrings.
    check("the sub-second rate ids retired everywhere",
          all(re.search(r"VIV_ID_SLIDESHOW_RATE_(250|500)\b", s) is None
              for s in (viv, vivh, menu, view)))
    check("the sub-second rate strings retired from both languages",
          ("RATE_250_MILLISECONDS" not in en) and ("RATE_500_MILLISECONDS" not in en) and
          ("RATE_250_MILLISECONDS" not in zh) and ("RATE_500_MILLISECONDS" not in zh) and
          ("RATE_250_MILLISECONDS" not in loc) and ("RATE_500_MILLISECONDS" not in loc))
    check("the preset ladder starts at one second",
          "{250,500" not in view and "{1000,2000" in view)
    check("the preset count literal followed the ladder",
          re.search(r"_VIV_SLIDESHOW_RATE_PRESET_COUNT\s+15\b", state) is not None)

    # --- the preview gate: the menu bar wears the context menu's rule ---
    check("the menu bar builder gates the dead preview verb",
          "VIV_ID_FILE_PREVIEW" in menu and "os_is_windows_8_or_later()" in menu)
    check("the context menu keeps its own gate",
          "os_is_windows_8_or_later()" in read("src/viv_wndproc.c").decode("utf-8", errors="replace"))

    # --- the floating control bar rename (the row names what it toggles:
    #     navigation, playback and zoom in one capsule) ---
    check("the english row names the floating control bar",
          "Floating &Control Bar" in en)
    check("the chinese row names the floating control bar",
          "显示悬浮控制条" in zh)

    # --- the renderer orientation contract (the sign is unrecoverable
    #     through getobject - it answers the absolute height) ---
    check("the d3d leg no longer reads the sign it can never see",
          "dsBmih.biHeight > 0" not in d3d and "_viv_d3d_bottom_up" not in d3d)
    check("the gl leg no longer reads the sign it can never see",
          "dsBmih.biHeight > 0" not in gl and "_viv_gl_bottom_up" not in gl)
    check("the clipboard normalizes to the top-down frame contract",
          "_viv_clipboard_top_down" in load)

    # --- the backdrop alpha gate ---
    check("the slot carries the baked-backdrop fact",
          "alpha_baked" in state and "_viv_slot_current.alpha_baked" in render)
    check("the loaders stamp the fact where the bake happens",
          "first_frame.alpha_baked = (image_flags & 2) ? 1 : 0;" in load and
          "first_frame.alpha_baked = viv_webp->has_alpha ? 1 : 0;" in anim)

    # --- the unblocked rotate ---
    check("the rotate verb launches without the ui-thread wait",
          'os_shell_execute(_viv_hwnd,_viv_slot_current.fd.cFileName,0,counterclockwise ? "rotate270" : "rotate90",0)' in view)

    # --- the timer fallback pair ---
    check("the animation clock falls back on a queue-timer refusal",
          "if (!_viv_is_timer_queue_timer)" in anim)


    # --- the pill's per-pixel alpha: the layered rework (round-125's
    #     field item: black corners on the stadium's four crescents and
    #     jaggies on every arc - lwa_alpha ignores per-pixel alpha and
    #     gdi has no antialiasing) ---
    zoomui = read("src/zoomui.c").decode("utf-8", errors="replace")
    osc = read("src/os.c").decode("utf-8", errors="replace")
    check("the pill renders through UpdateLayeredWindow with per-pixel alpha",
          "UpdateLayeredWindow(_zoomui_hwnd,0,0,&size,_zoomui_mem_hdc,&pt,0,&blend,ULW_ALPHA)" in zoomui and
          "blend.AlphaFormat = AC_SRC_ALPHA;" in zoomui)
    check("the layered path is gated on both probes (layered children and gdiplus)",
          "static int _zoomui_use_ulw(void)" in zoomui)
    check("the direct-alpha to premultiplied conversion runs before the upload",
          "static void _zoomui_premultiply(unsigned char *bits,int wide,int high)" in zoomui and
          "_zoomui_premultiply(" in zoomui)
    check("the gdi+ antialiased rasterization carries the path primitives",
          all(s in zoomui for s in ("os_GdipCreatePath", "os_GdipAddPathArc", "os_GdipAddPathLine",
                                    "os_GdipClosePathFigure", "os_GdipFillPath", "os_GdipDrawPath",
                                    "os_GdipSetSmoothingMode")))
    check("the exports live in the os table (no new dll - gdiplus was already loaded)",
          all(s in osc for s in ("os_GdipCreatePath", "os_GdipAddPathArc", "os_GdipFillPath")))
    check("the fade re-blits the cached bitmap instead of re-rasterizing",
          "blend.SourceConstantAlpha = (BYTE)alpha;" in zoomui)
    check("the win7 fallback leg keeps its gdi roundrect (no layered children there)",
          "RoundRect(" in zoomui)

    # --- the toolbar's play resolution: a paused animation answers the
    #     resume click, not a slideshow (the face rule keeps the play
    #     term - it is a face, the dispatch is a dispatch) ---
    fire_start = toolbar.find("_viv_toolbar_fire")
    fire_end = toolbar.find("SendMessage", fire_start)
    fire = toolbar[fire_start:fire_end]
    check("the play slot resolves on the frame count alone",
          "(_viv_slot_current.frame_count > 1) && (_viv_animation_play)" not in fire and
          "else if (_viv_slot_current.frame_count > 1)" in fire)
    check("the resolution keeps its three-way shape",
          toolbar.count("VIV_ID_SLIDESHOW_PAUSE_ONLY") >= 1 and
          toolbar.count("VIV_ID_ANIMATION_PLAY_PAUSE") >= 1 and
          toolbar.count("VIV_ID_SLIDESHOW_PLAY_ONLY") >= 1)


def t_report_fusion_round123():
    """Guards for the report fusion round (1.1.15-rc.12: the third fusion
    report's ledger - fourteen sweeps over the rc.10 tree, the main thread
    re-verifying eight of eight headline claims, and the verdict that the
    rc.10 lesson was never systematized: a fourth dead machine, two string
    pins the code's own comments could satisfy, and half the fresh guard
    teeth decorative. this round lands the whole ledger - the two P1
    teeth, the sixteen P2 line fixes, the P3/P4 debts - and every pin
    below bites a machine or an arithmetic, never a mention."""
    viv = read("src/viv.c").decode()
    settings = read("src/viv_settings.c").decode()
    load = read("src/viv_load.c").decode()
    version = read("src/version.h").decode()
    changes = read("Changes.txt").decode("utf-8", errors="replace")
    readme = read("README.md").decode("utf-8", errors="replace")
    readme_cn = read("README_CN.md").decode("utf-8", errors="replace")
    wndproc = read("src/viv_wndproc.c").decode()
    msgbox = read("src/viv_msgbox.c").decode()
    anim = read("src/viv_anim.c").decode()
    menu_c = read("src/viv_menu.c").decode()
    chrome = read("src/viv_chrome.c").decode()
    dialogs = read("src/viv_dialogs.c").decode()
    installer_c = read("src/viv_install.c").decode()
    debug_c = read("src/debug.c").decode()
    glyphs = read("src/glyphs.c").decode()
    view = read("src/viv_view.c").decode()
    load_h = read("src/viv_load.h").decode()
    release_yml = read(".github/workflows/release.yml").decode()
    tests_yml = read(".github/workflows/tests.yml").decode()
    codeql_yml = read(".github/workflows/codeql.yml").decode()
    installer_nsi = read("nsis/installer.nsi").decode("latin-1")
    golden_ps1 = read("tests/render_golden.ps1").decode("latin-1")

    s = settings.replace("\r\n", "\n")
    l = load.replace("\r\n", "\n")
    v = viv.replace("\r\n", "\n")
    w = wndproc.replace("\r\n", "\n")
    mb = msgbox.replace("\r\n", "\n")
    an = anim.replace("\r\n", "\n")
    me = menu_c.replace("\r\n", "\n")
    ch = chrome.replace("\r\n", "\n")
    dg = dialogs.replace("\r\n", "\n")
    ic = installer_c.replace("\r\n", "\n")
    db = debug_c.replace("\r\n", "\n")
    gl = glyphs.replace("\r\n", "\n")
    vw = view.replace("\r\n", "\n")
    lh = load_h.replace("\r\n", "\n")
    ry = release_yml.replace("\r\n", "\n")
    ty = tests_yml.replace("\r\n", "\n")
    cy = codeql_yml.replace("\r\n", "\n")
    ns = installer_nsi.replace("\r\n", "\n")
    gp = golden_ps1.replace("\r\n", "\n")

    def body(src, sig):
        i = 0
        while True:
            i = src.find(sig, i)
            if i < 0:
                return ""
            b = src.find("{", i)
            semi = src.find(";", i)
            if 0 <= semi < b:
                i += 1
                continue  # a forward declaration, not the body
            break
        depth, j = 0, b
        while j < len(src):
            if src[j] == "{":
                depth += 1
            elif src[j] == "}":
                depth -= 1
                if depth == 0:
                    break
            j += 1
        return src[b:j]

    def case_body(src, sig, cid):
        f = body(src, sig)
        if not f:
            return ""
        i = f.find("case _VIV_SETTINGS_ID_" + cid + ":")
        if i < 0:
            return ""
        j = f.find("case _VIV_SETTINGS_ID_", i + 10)
        k = f.find("default:", i + 10)
        ends = [x for x in (j, k) if x > 0] + [len(f)]
        return f[i:min(ends)]

    # 1. THE WALK'S EMPTY-SEAT REFUSAL (the fusion report's P2-1 first
    #    half: the counter gate never looked inside the slot - the
    #    activation had just taken the preload out, and the walk
    #    inserted the empty seat anyway: a ghost seat that evicted a
    #    real neighbour and counted itself forever).
    # round-125: the seat term reserves the history seat.
    check("the chain walk refuses an empty slot",
          "((_viv_preload_chain_count + 1 < config_preload_count) && (_viv_preload_chain_count + 1 < config_cache_count) && (_viv_slot_preload.frames))" in l)

    # 2. THE EXIT FREE'S LOADED COUNT (P2-1's second half: a partial
    #    frame set entered the ring when frames failed mid-decode, and
    #    the exit path freed by the declared count over garbage tail
    #    entries - the ring's own eviction never made that mismatch,
    #    it always frees by loaded).
    check("the exit free answers the loaded count",
          "_viv_clear_frames(_viv_slot_cache[i].frames,_viv_slot_cache[i].frame_loaded_count);" in v)

    # 3. THE ICONIC APPLY (P2-2: an explicit geometry landed on the
    #    virtual corner of a minimized window and the restore replayed
    #    the old placement - the launcher's rectangle silently died).
    check("an explicit geometry restores the minimized window first",
          "if (IsIconic(_viv_hwnd))\n\t\t\t\t{\n\t\t\t\t\tShowWindow(_viv_hwnd,SW_RESTORE);\n\t\t\t\t}" in v and
          0 <= v.find("if (IsIconic(_viv_hwnd))\n\t\t\t\t{") < v.find("SetWindowPos(_viv_hwnd,0,window_x"))

    # 4. THE LIVE-HIDDEN REVEAL (P2-3: a hidden live window stayed
    #    unreachable forever - the explorer forward arrived as
    #    SW_SHOWNORMAL and the old branch answered only the maximized
    #    word. SW_SHOW is the no-op on a visible window and the reveal
    #    a hidden one waits for).
    check("a live window answers every non-maximized word with a show",
          "ShowWindow(hwnd,(showcmd == SW_SHOWMAXIMIZED) ? SW_SHOWMAXIMIZED : SW_SHOW);" in w)
    #    and the one word that must not reveal: an explicit hide stays a
    #    hide (the bare STARTF-less zero rides the same exclusion).
    check("the hide word stays a hide",
          "if (showcmd != SW_HIDE)" in w)
    #    and the rc.7 shape itself, pinned as code rather than comment
    #    (the fusion report's E4: the old pin was satisfied by the
    #    comment that explains the line; reverting the code alone
    #    passed every suite).
    check("the iconic branch answers restore, as code rather than comment",
          "ShowWindow(hwnd,(showcmd == SW_SHOWMAXIMIZED) ? SW_SHOWMAXIMIZED : SW_RESTORE);" in w)

    # 5. THE MSGBOX CLAMP (P2-4: the box centered on an iconic
    #    parent's virtual rect parked off-screen, and the modal pump
    #    behind it stalled a second instance forever - the dialog
    #    family clamps through os.c; the box joins them).
    check("the msgbox clamps its centered rect on screen",
          "os_make_rect_completely_visible(_viv_msgbox_hwnd,&box_rect);" in mb and
          0 <= mb.find("os_make_rect_completely_visible") < mb.find("SetWindowPos(_viv_msgbox_hwnd,HWND_TOP"))

    # 6. THE TIMER'S DUTY HAND-BACK (P2-13: the sticky flag went up
    #    before an unchecked PostMessage - one refused post and the
    #    animation froze forever; the reply queue's own duty fix from
    #    rc.5, on the primitive next door).
    check("a refused timer post hands the duty back",
          "if (!PostMessage(_viv_hwnd,WM_TIMER,VIV_ID_ANIMATION_TIMER,0))" in an and
          "_viv_is_animation_timer_event = 0;" in body(an, "_viv_timer_queue_timer_callback"))

    # 7. THE PASTE'S QUEUED-NEXT REFUSAL (P2-14: the paste stopped the
    #    in-flight leg but left the queued next - its COMPLETE
    #    dispatched the stranger over the pasted image).
    pb = body(l, "static void _viv_show_clipboard_image")
    check("the paste clears the queued next",
          "mem_free(_viv_load_image_next_fd);" in pb and "_viv_load_image_next_fd = NULL;" in pb)
    check("the paste retires the activation ask with the pair",
          "_viv_should_activate_preload_on_load = 0;" in pb and
          "_viv_slot_preload.fd.cFileName[0] = 0;" in pb)

    # 8. THE FOUR FACES' ONE ANSWER (P2-15 + P3-6 + P3-21: the menu
    #    face, the title bar, the cursor gate and the save-as seed all
    #    read the request fd while the toolbar and the command bodies
    #    answered the slot - during a first load the request names a
    #    file nothing shows yet).
    check("the menu's image face answers the slot",
          "is_image_enabled = ((*_viv_slot_current.fd.cFileName) && (!_viv_file_not_found) && (!_viv_load_failed)) ? MF_ENABLED : MF_DISABLED;" in me)
    check("the title bar answers the slot",
          "filename = _viv_slot_current.fd.cFileName;" in ch and
          "filename = string_get_filename_part(_viv_slot_current.fd.cFileName);" in ch)
    check("the cursor gate answers the slot",
          "if ((*_viv_slot_current.fd.cFileName) && (!_viv_file_not_found) && (!_viv_load_failed))" in ch)
    sa = body(l, "void _viv_save_image_as(void)")
    check("the save-as gate and seed answer the slot (the pixels already do)",
          "if (*_viv_slot_current.fd.cFileName)" in sa and
          "string_copy(tobuf,_viv_slot_current.fd.cFileName);" in sa)

    # 9. THE DELETE'S RE-ANCHORED SCAN (P3-7: the post-delete scan
    #    anchored on the request fd - delete A during B's first load
    #    and the next answered C past A's side instead of A's
    #    neighbour).
    de = body(vw, "static void _viv_delete(int permanently)")
    check("the post-delete scan anchors on the deleted file's seat",
          "os_copy_memory(_viv_current_fd,&fd,sizeof(WIN32_FIND_DATA));" in de and
          0 <= de.find("os_copy_memory(_viv_current_fd") < de.find("_viv_next(0,1,0,0)"))

    # 10. THE BLANK'S IN-FLIGHT REFUSAL (P3-8: close never stopped the
    #     load it closed on - the FIRST_FRAME landed on the blank
    #     screen and the queued next followed it).
    bl = body(l, "void _viv_blank(void)")
    check("close stops the in-flight load and its queue",
          "_viv_load_image_allow_draw = 0;" in bl and
          "InterlockedExchange(&_viv_load_image_terminate,1);" in bl and
          "_viv_load_image_next_fd = NULL;" in bl)
    check("close retires the activation ask too",
          "_viv_should_activate_preload_on_load = 0;" in bl)

    # 11. THE RING'S INERT GUARDS (P3-9/P3-30: the insert could push
    #     past the ceiling between the two settle points, and the
    #     activate's zero-count tail wrote at [-1] - unreachable
    #     today, unguarded tomorrow).
    ins = body(l, "static void _viv_cache_insert")
    check("the insert trims at every seat change",
          "_viv_cache_set_trim();" in ins and
          ins.find("_viv_cache_set_trim();") > ins.find("_viv_slot_take(&_viv_slot_cache[0],src)") >= 0)
    ca = body(l, "static void _viv_cache_activate")
    check("the activate refuses a zero-count ring",
          "if (config_cache_count <= 0)" in ca and
          0 <= ca.find("if (config_cache_count <= 0)") < ca.find("_viv_clear();"))

    # 12. THE GLYPHS' PERMANENT NO (P3-4: a refused GdiplusStartup left
    #     the state at zero with a success return - every paint re-ran
    #     the LoadLibrary table for a gdi+ that never comes).
    check("a refused startup is a permanent no",
          "else\n\t\t{\n\t\t\t// a refused startup is a permanent state too" in gl and
          "_glyphs_state = 2;" in gl and
          gl.count("return (_glyphs_state == 1) ? 1 : 0;") == 2)

    # 13. THE EXIT CODES (P3-5 + the ledger's debug fatal: three
    #     fail-loud inits answered the process exit code with success,
    #     and debug_fatal's ExitProcess(0) read as success to a setup
    #     that waited on it).
    main_body = v[v.find("static int _viv_main"):v.find("int APIENTRY WinMain")]
    check("the exit-code contract is three-way (a completed one-shot and a quiet gui close answer zero; a failed init answers one)",
          "return (init_ret == 0) ? 0 : 1;" in main_body and
          main_body.count("_viv_kill();\n\t\t\n\t\treturn -1;") == 3)
    check("the quiet gui close answers zero at the exit label",
          "\texit:\n\t\t\n\t\t_viv_kill();\n\t\t\n\t\t// the quiet gui close answers zero.\n\t\treturn 0;\n\t}" in main_body)
    check("debug_fatal exits with a failure code",
          "ExitProcess(1);" in db)

    # 14. THE DEAD WEIGHT (P2-9 + the ledger's duplicate prototype: two
    #     never-called color button helpers survived the rc.6
    #     retirement, and a duplicated chain walk prototype outlived
    #     the round that lost it).
    check("the color button helpers are gone",
          "_viv_update_color_button_bitmap" not in dg and
          "_viv_delete_color_button_bitmap" not in dg)
    check("the chain walk declares itself exactly once",
          lh.count("void _viv_preload_chain_walk(void);") == 1)

    # 15. THE INSTALLER'S QUOTE REFUSAL (P2-16: the runtime options word
    #     travelled inside a quoted argument of an elevated command
    #     line - a quote in it would close that quote and restructure
    #     the elevated call into commands of the caller's choosing.
    #     the build-time whitelist pinned the builder's own
    #     parameters; the runtime side refused nothing. both sides
    #     refuse the quote now).
    check("the nsis side scans the options word for quotes",
          "install_options_quote_scan:" in ns and "install_options_refused:" in ns)
    check("the c side refuses the quote before it restructures the runas line",
          "if (*q == '\"')" in ic and
          0 <= 0 <= ic.find("install_options[0] = 0;") < ic.find("os_shell_execute(0,new_exe_filename_wbuf,1,NULL,install_options)"))

    # 16. THE LEAF-ID ARITHMETIC (P1-3: the fusion report's E1 - a
    #     single ret-1 could vanish into the flat popup's copy and
    #     every suite stayed green while the cascade bound every
    #     command to its successor).
    check("the picker's leaf-id contract lives in exactly two bodies",
          s.count("return ret - 1;") == 2)

    # 17. THE GOLDEN META PINS (P2-8: the ratchet and the fail-closed
    #     exit had no pins of their own - the fusion report's E5a/E5c
    #     mutated both away to green).
    check("the golden ratchet counts in both branches",
          gp.count("$mismatched++") == 2)
    check("the golden fail-closed exit is pinned",
          "if (($failures + $mismatched) -gt 0) {" in gp)

    # 18. THE CENSUS EXTENSION (P2-5's other half: a case is not a
    #     binding - the rc.9 pill answered its click with the right
    #     key while the pill itself sat unpainted; the matrix walk
    #     catches a missing machine but not a case wired to the wrong
    #     key. every switch's activate and state-draw bodies carry
    #     their own config variable).
    switch_binding = {
        "MULTIPLE": "config_multiple_instances",
        "STARTMENU": "_viv_settings_startmenu",
        "APPDATA": "_viv_settings_appdata",
        "AUTOZOOM": "config_auto_zoom",
        "LOOP": "config_loop_animations_once",
        "RESUME": "config_resume_last_file",
        "TOOLBARICON": "config_toolbar_icon_only",
        # round-125: the settings remake - the interface chrome rows.
        "FLOATBAR": "config_show_zoom_controls",
        "AUTOHIDE": "config_zoom_auto_hide",
        "PIXELINFO": "config_pixel_info",
    }
    switch_draw = {
        "MULTIPLE": "config_multiple_instances ? 1 : 0",
        "STARTMENU": "_viv_settings_startmenu",
        "APPDATA": "_viv_settings_appdata",
        "AUTOZOOM": "config_auto_zoom ? 1 : 0",
        "LOOP": "config_loop_animations_once ? 1 : 0",
        "RESUME": "config_resume_last_file ? 1 : 0",
        "TOOLBARICON": "config_toolbar_icon_only ? 1 : 0",
        "FLOATBAR": "config_show_zoom_controls ? 1 : 0",
        "AUTOHIDE": "config_zoom_auto_hide ? 1 : 0",
        "PIXELINFO": "config_pixel_info ? 1 : 0",
    }
    act_sig = "static void _viv_settings_activate(int index,int x,int y)"
    for cid in sorted(switch_binding):
        var = switch_binding[cid]
        check(f"the {cid.lower()} activate case binds its own config key",
              (var + " = " + var + " ? 0 : 1;") in case_body(s, act_sig, cid))
    for cid in sorted(switch_draw):
        expr = switch_draw[cid]
        check(f"the {cid.lower()} pill draws its own config key",
              f"case _VIV_SETTINGS_ID_{cid}:\n\t\t\t\t\t\t_viv_settings_draw_switch(mem,ctl,{expr},hot,focus);" in s)
    switch_label = {
        "MULTIPLE": "LOCALIZATION_ID_SETTINGS_ALLOW_MULTIPLE",
        "STARTMENU": "LOCALIZATION_ID_STARTMENU_SHORTCUTS",
        "APPDATA": "LOCALIZATION_ID_STORE_SETTINGS_APPDATA",
        "AUTOZOOM": "LOCALIZATION_ID_OPTIONS_VIEW_AUTO_SIZE_WINDOW_STATIC",
        "LOOP": "LOCALIZATION_ID_PLAY_ANIMATIONS_ONCE_STATIC",
        "RESUME": "LOCALIZATION_ID_SETTINGS_RESUME_LAST",
        "TOOLBARICON": "LOCALIZATION_ID_SETTINGS_TOOLBAR_ICON_ONLY",
        "FLOATBAR": "LOCALIZATION_ID_SETTINGS_FLOATING_BAR",
        "AUTOHIDE": "LOCALIZATION_ID_SETTINGS_AUTO_HIDE_BAR",
        "PIXELINFO": "LOCALIZATION_ID_SETTINGS_PIXEL_INFO",
    }
    paint_sig = "static void _viv_settings_paint(HWND hwnd)"
    for cid in sorted(switch_label):
        lid = switch_label[cid]
        check(f"the {cid.lower()} label case binds its own string",
              f"label_id = {lid};" in case_body(s, paint_sig, cid))

    # 19. THE LADDER'S OWN BOUNDARIES (P2-6: the pins named the helper
    #     while a test-side model held the boundaries - mutating the
    #     real count == 1 to count == 2 passed every suite green).
    i = s.find("static void _viv_settings_count_text(")
    ladder_body = s[i:s.find("\n}", i)] if i >= 0 else ""
    m0 = re.search(r"count <= (\d+)", ladder_body)
    m1 = re.search(r"count == (\d+)", ladder_body)
    check("the ladder's boundaries extract from the source",
          m0 is not None and m1 is not None)
    if m0 and m1:
        lo = int(m0.group(1))
        one = int(m1.group(1))
        def ladder_text(count, lo=lo, one=one):
            if count <= lo:
                return "off"
            if count == one:
                return "one"
            return "many"
        check("the extracted ladder answers 0/1/2 as off/one/many",
              ladder_text(0) == "off" and ladder_text(1) == "one" and ladder_text(2) == "many",
              f"lo={lo} one={one}")

    # 20. THE CI LEDGER (the fusion report's P3-14/15/16/23: six jobs
    #     without a timeout, a write token no step on the v145 leg
    #     ever spent, a CRLF sha256 that older coreutils refused, and
    #     a zip channel the qoi notice never travelled with).
    check("every release job carries a timeout",
          ry.count("timeout-minutes:") == 4)
    check("every codeql job carries a timeout",
          cy.count("timeout-minutes:") == 2)
    check("the tests workflow grants no write token the bootstrap alone spends",
          "actions: write" not in ty)
    check("the sha256 manifest travels with LF endings",
          "[IO.File]::WriteAllText" in ry and "-Encoding ascii" not in ry)
    check("the zip channel carries the third-party notices",
          ry.count("'THIRD_PARTY_NOTICES.md'") == 2)

    # 21. THE UNINSTALLER'S SECOND STAGE (P3-22 + P4-7 + P3-24: the
    #     %TEMP% copy never deleted itself - RMDir cannot remove a
    #     directory the exe still sits in - the second ExecWait
    #     answered no error check, and a comment claimed the stage
    #     proves provenance it cannot).
    check("the second stage deletes its exe before the rmdir",
          'Delete /REBOOTOK "$0\\voidImageViewer.exe"' in ns and
          0 <= ns.find('Delete /REBOOTOK "$0\\voidImageViewer.exe"') < ns.find("RMDir /REBOOTOK $0"))
    check("the second stage's exec answers an error check",
          "uninstall_stage_error:" in ns)
    check("the stage-failure message is localized, not hardcoded",
          "LangString MsgUninstallStageFailed ${LANG_ENGLISH}" in ns and
          "LangString MsgUninstallStageFailed ${LANG_SIMPCHINESE}" in ns and
          "$(MsgUninstallStageFailed)" in ns)
    check("the second stage comment claims only existence",
          "whose existence this section verified" in ns)

    # 22. the version, the changelog and the readme twins.
    top = changes.lstrip("\ufeff").split("\r\n")[0]
    check("the changelog top entry is the field response round pre-release",
          top == "Pre-release: Version 1.1.15-rc.16 (the parallel evaluation round)", top)
    check("the version is 1.1.15-rc.16 build 95",
          "#define VERSION_BUILD 95" in version and
          '#define VERSION_STRING "1.1.15-rc.16"' in version)
    check("the readme twins carry the round as the candidate",
          "**1.1.15-rc.16 \u2014 the parallel evaluation round (the current release candidate):**" in readme and
          "**1.1.15-rc.16 \u2014 \u5e76\u884c\u8bc4\u4f30\u8f6e\uff08\u5f53\u524d\u5019\u9009\u7248\uff09\uff1a**" in readme_cn)




# ---------------------------------------------------------------------------
# the settings footer round (1.1.15-rc.14): the 4k 300 percent field report.

def t_settings_capacity_round126():
    """Guards for the settings footer round (1.1.15-rc.14). the 300
    percent report showed the general page with no footer at all: the
    remake pushed the page to thirty-two live controls against a thirty
    seat array and the capacity guard answered by silently dropping the
    last two registrations - the cancel and ok buttons, on every dpi, on
    the page that carries most of the settings. the same report's "the
    floating bar does not hot switch" was the cascade: the pill did
    follow the toggle live, but the only exits left on that page were
    esc, the close button and the title x - all three cancel paths, so
    the toggle rode back with the rewind. the round answers with the
    capacity lift, the apply button (commit, re-baseline, stay open),
    the content scroll for the clamped work areas, the monitor aware
    height clamp and three keyboard repairs the review round surfaced
    (the arrow wrap page jump, enter swallowing the focused button, the
    focus ring skipping the navigation column)."""
    print("the settings footer round (1.1.15-rc.14)")
    settings = read("src/viv_settings.c").decode()
    version = read("src/version.h").decode()
    changes = read("Changes.txt").decode("utf-8", errors="replace")
    readme = read("README.md").decode("utf-8", errors="replace")
    readme_cn = read("README_CN.md").decode("utf-8", errors="replace")
    local_h = read("src/localization.h").decode()
    local_en = read("src/localization_en_us.h").decode()
    local_zh = read("src/localization_zh_cn.h").decode()

    s = settings.replace("\r\n", "\n")

    def body_of(sig):
        i = s.find(sig + "\n")
        if i < 0:
            return ""
        b = s.find("{", i)
        depth, j = 0, b
        while j < len(s):
            if s[j] == "{":
                depth += 1
            elif s[j] == "}":
                depth -= 1
                if depth == 0:
                    break
            j += 1
        return s[b:j]

    # 1. THE CAPACITY CENSUS - the round's root cause. the pages demand
    #    live control seats (nav three + content + footer three); the
    #    array answers a full house by silently dropping the tail, and
    #    the tail is the footer because the footer registers last.
    m = re.search(r"#define _VIV_SETTINGS_CTL_MAX\s+(\d+)", s)
    check("the ctl array declares its capacity", m is not None)
    ctl_max = int(m.group(1)) if m else 0

    i = s.find("case _VIV_SETTINGS_PAGE_GENERAL:")
    j = s.find("case _VIV_SETTINGS_PAGE_VIEW:")
    gen_block = s[i:j]
    j2 = s.find("case _VIV_SETTINGS_PAGE_CONTROLS:")
    view_block = s[j:j2]
    j3 = s.find("footer_y = client.bottom")
    controls_block = s[j2:j3]

    nav_seats, footer_seats, assoc = 3, 3, 11
    gen_seats = (len(re.findall(r"_viv_settings_ctl_add\(", gen_block))
                 + assoc - 1 + nav_seats + footer_seats)
    view_seats = (len(re.findall(r"_viv_settings_ctl_add\(", view_block))
                  + nav_seats + footer_seats)
    controls_seats = (len(re.findall(r"_viv_settings_ctl_add\(", controls_block))
                      + nav_seats + footer_seats)

    check("the general page's seats fit the array (the 300 percent report: 32 seats on 30 dropped the footer)",
          gen_seats <= ctl_max, f"general {gen_seats} vs {ctl_max}")
    check("the view page's seats fit the array",
          view_seats <= ctl_max, f"view {view_seats} vs {ctl_max}")
    check("the controls page's seats fit the array",
          controls_seats <= ctl_max, f"controls {controls_seats} vs {ctl_max}")
    check("the capacity keeps four spare seats over the busiest page",
          ctl_max >= gen_seats + 4, f"{ctl_max} vs {gen_seats}+4")

    # 2. THE APPLY BUTTON - five machines, one row. the pill switch
    #    taught the lesson: furniture without machines is a dead row.
    check("the apply id joins the id block (no renumbering - the pair keeps its byte shape)",
          "#define _VIV_SETTINGS_ID_APPLY\t\t45" in s and
          "#define _VIV_SETTINGS_ID_CANCEL\t\t40" in s and
          "#define _VIV_SETTINGS_ID_OK\t\t\t41" in s)

    foot = s[s.find("footer_y = client.bottom"):s.find("footer_y = client.bottom") + 2400]
    order = re.findall(r"_viv_settings_ctl_add\(_VIV_SETTINGS_CT_BUTTON,_VIV_SETTINGS_(ID_\w+)", foot)
    check("the footer registers three buttons - apply, cancel, ok (ok keeps its learned seat)",
          order == ["ID_APPLY", "ID_CANCEL", "ID_OK"], str(order))

    act = body_of("static void _viv_settings_activate(int index,int x,int y)")
    i = act.find("case _VIV_SETTINGS_CT_BUTTON:")
    btn_case = act[i:]
    check("the activate machine answers all three buttons (the binary else once read an apply click as a cancel)",
          "_VIV_SETTINGS_ID_APPLY" in btn_case and "_viv_settings_apply();" in btn_case,
          btn_case[:120])

    apply_body = body_of("static void _viv_settings_apply(void)")
    check("apply commits, re-baselines and stays open",
          "_viv_settings_commit();" in apply_body and
          "_viv_settings_snapshot();" in apply_body and
          "_viv_settings_close" not in apply_body)
    commit_body = body_of("static void _viv_settings_commit(void)")
    ok_body = body_of("static void _viv_settings_ok(void)")
    check("the commit carries the ok machine's persist tail",
          "config_save_settings(config_appdata);" in commit_body and
          "_viv_settings_close" not in commit_body)
    check("ok rides the commit and closes",
          "_viv_settings_commit();" in ok_body and "_viv_settings_close();" in ok_body)

    check("the dirty lamp exists and watches the live families plus the key latch",
          "static int _viv_settings_dirty(void)" in s and
          "_viv_settings_keys_dirty" in body_of("static int _viv_settings_dirty(void)"))
    en_body = body_of("static int _viv_settings_ctl_enabled(const _viv_settings_ctl_t *ctl)")
    check("the apply button gates on the dirty lamp (hit test, focus walk, activate and the grey face all ride ctl_enabled)",
          "case _VIV_SETTINGS_CT_BUTTON:" in en_body and "_viv_settings_dirty()" in en_body)
    check("the key latch is armed by the editor and cleared by the snapshot",
          "_viv_settings_keys_dirty = 1" in s and "_viv_settings_keys_dirty = 0" in s)

    check("the apply label is declared and localized",
          "LOCALIZATION_ID_SETTINGS_APPLY" in local_h and
          '"Apply"' in local_en and '"\u5e94\u7528"' in local_zh)
    check("the draw machine gives apply its own face (the default branch once painted the remove-key label)",
          "if (ctl->id == _VIV_SETTINGS_ID_APPLY)" in s and
          "LOCALIZATION_ID_SETTINGS_APPLY" in s)

    # 3. THE CONTENT SCROLL - the clamped work areas (720p at 100, 1080p
    #    at 150, the 4k at 300) cut the client under the content; the
    #    pages scroll instead of sinking their tails under the footer.
    check("the scroll state exists", "static int _viv_settings_scroll_y;" in s)
    lay = body_of("static void _viv_settings_layout(void)")
    check("the layout bakes the scroll offset into the content rects (the dropdown popups anchor on the bake)",
          "OffsetRect(&_viv_settings_ctls[i].rect,0,-_viv_settings_scroll_y)" in lay and
          "OffsetRect(&_viv_settings_ctls[i].value,0,-_viv_settings_scroll_y)" in lay and
          "scroll_max" in lay)
    check("the nav and the footer never scroll (the footer registers after the bake)",
          lay.find("_viv_settings_ctl_add(_VIV_SETTINGS_CT_BUTTON") > lay.find("OffsetRect(&_viv_settings_ctls[i].rect"))
    check("the wheel reaches the settings window",
          "case WM_MOUSEWHEEL:" in s and "GET_WHEEL_DELTA_WPARAM" in s)
    check("the paint pass clips the scrolled content to the viewport",
          "IntersectClipRect(" in s)
    check("the focus walker scrolls its target into view",
          "_viv_settings_ensure_visible(" in s)
    check("the scrollbar paints when the content overflows",
          "_viv_settings_draw_scrollbar(" in s)
    check("the page switches reset the scroll",
          s.count("_viv_settings_scroll_y = 0;") >= 2)

    # 4. THE MONITOR AWARE CLAMP - the spi query only knows the primary
    #    monitor's work area; a settings window on a secondary screen
    #    clamped against the wrong ceiling.
    wsz = body_of("static void _viv_settings_window_size_px(int *wide,int *high)")
    check("the height clamp asks the owner's monitor for its work area",
          "os_MonitorRectFromWindow" in wsz and "SystemParametersInfo" not in wsz)

    # 5. THE KEYBOARD REPAIRS - the review round's three findings.
    kd = body_of("static LRESULT CALLBACK _viv_settings_proc(HWND hwnd,UINT msg,WPARAM wParam,LPARAM lParam)")
    check("an arrow wrap onto the navigation column is a focus move, not a page selection (the tab rule)",
          "a wrap arrival is not a page selection" in kd)
    i = kd.find("case VK_RETURN:")
    ret = kd[i:kd.find("case VK_ESCAPE:", i)] if i >= 0 else ""
    check("enter answers the focused button before the global ok",
          "_VIV_SETTINGS_CT_BUTTON" in ret, ret[:160])
    check("the navigation column draws its focus ring",
          "static void _viv_settings_draw_nav(" in s and
          "_viv_settings_draw_focus_ring" in body_of("static void _viv_settings_draw_nav(HDC hdc,const _viv_settings_ctl_t *ctl,int selected,int hot,int inactive,int focus)"))
    check("the menu entry and the window title agree on the name (the options/settings split)",
          '"&Settings..."' in local_en and
          '"\u8bbe\u7f6e(&S)..."' in local_zh)

    # 6. the version, the changelog and the readme twins.
    top = changes.lstrip("\ufeff").split("\r\n")[0]
    check("the changelog top entry is the settings footer round pre-release",
          top == "Pre-release: Version 1.1.15-rc.16 (the parallel evaluation round)", top)
    check("the version is 1.1.15-rc.16 build 95",
          "#define VERSION_BUILD 95" in version and
          '#define VERSION_STRING "1.1.15-rc.16"' in version)
    check("the readme twins carry the round as the candidate",
          "**1.1.15-rc.16 \u2014 the parallel evaluation round (the current release candidate):**" in readme and
          "**1.1.15-rc.16 \u2014 \u5e76\u884c\u8bc4\u4f30\u8f6e\uff08\u5f53\u524d\u5019\u9009\u7248\uff09\uff1a**" in readme_cn)


def t_pill_field_round127():
    """Guards for the pill field round (1.1.15-rc.15). the report read
    three symptoms on the floating bar. one: the pill oversized on the
    desktop - the whole row scales to 90 percent (the 48x44 touch
    capsules become 43x39 dip). two: a fullscreen flicker the reporter
    blamed on a focus fight with screen recorders - the code owns no
    foreground grab at all (no SetForegroundWindow, the pill is a
    WS_CHILD), so the blame shifts to the real mechanism: the 15ms fade
    timer fought the 15.6ms system clock resolution, its fixed 17-unit
    steps clumping into visible jumps at 66 layered submits a second,
    right where capture hooks race the composition. three: the pill
    never showing in windowed mode - the fade machinery only runs in
    the fullscreen autohide mode, so a show that started at alpha zero
    left the pill an invisible window (the rc.14 hot-switch fix
    uncovered it: the toggle now works, the windowed pill it reveals
    was a ghost)."""
    print("the pill field round (1.1.15-rc.15)")
    zc = read("src/zoomui.c").decode("utf-8", errors="replace")
    zh_ = read("src/zoomui.h").decode()
    viv = read("src/viv.c").decode("utf-8", errors="replace")
    version = read("src/version.h").decode()
    changes = read("Changes.txt").decode("utf-8", errors="replace")
    readme = read("README.md").decode("utf-8", errors="replace")
    readme_cn = read("README_CN.md").decode("utf-8", errors="replace")

    # 1. THE 90 PERCENT PILL - one scale pair drives every metric so a
    #    later retune moves one define, not eleven literals.
    check("the pill scale pair is declared",
          "#define _ZOOMUI_PILL_SCALE_NUM 9" in zc and
          "#define _ZOOMUI_PILL_SCALE_DEN 10" in zc)
    check("the capsule metrics ride the scale pair",
          "_zoomui_cell_wide = (48 * _ZOOMUI_PILL_SCALE_NUM * os_logical_wide) / (_ZOOMUI_PILL_SCALE_DEN * 96);" in zc and
          "_zoomui_cell_high = (44 * _ZOOMUI_PILL_SCALE_NUM * os_logical_high) / (_ZOOMUI_PILL_SCALE_DEN * 96);" in zc)
    check("the margin, the gap, the separator and the text pad ride the scale too",
          "(6 * _ZOOMUI_PILL_SCALE_NUM * os_logical_high) / (_ZOOMUI_PILL_SCALE_DEN * 96)" in zc and
          "(4 * _ZOOMUI_PILL_SCALE_NUM * os_logical_high) / (_ZOOMUI_PILL_SCALE_DEN * 96)" in zc and
          "(24 * _ZOOMUI_PILL_SCALE_NUM * os_logical_high) / (_ZOOMUI_PILL_SCALE_DEN * 96)" in zc and
          "(16 * _ZOOMUI_PILL_SCALE_NUM * os_logical_wide) / (_ZOOMUI_PILL_SCALE_DEN * 96)" in zc)
    check("the hairline separator stays one pixel (a scaled hairline vanishes)",
          "_zoomui_sep_wide = (1 * os_logical_wide) / 96;" in zc)
    check("the metric floors shrink with the row (29/25/11/7)",
          "_zoomui_cell_wide < 29" in zc and "_zoomui_cell_high < 25" in zc and
          "_zoomui_sep_high < 11" in zc and "_zoomui_pct_pad < 7" in zc)
    check("the old unscaled 48x44 metrics are gone",
          "_zoomui_cell_wide = (48 * os_logical_wide) / 96;" not in zc and
          "_zoomui_cell_high = (44 * os_logical_high) / 96;" not in zc)

    # 2. THE WINDOWED SNAP - the ghost pill. the fade timer only runs in
    #    the fullscreen autohide mode; every other show must own its
    #    alpha directly.
    check("a windowed show starts opaque (a zero start with no fade timer behind it was the ghost)",
          "((_zoomui_layered_ok) && (_zoomui_autohide_enabled())) ? 0 : _ZOOMUI_ALPHA_OPAQUE" in zc)
    check("the windowed show branch snaps a stale fade alpha back to opaque",
          "the windowed row runs no fade timer" in zc and
          zc.count("_zoomui_set_alpha(_ZOOMUI_ALPHA_OPAQUE);") >= 2)
    check("leaving fullscreen snaps an in-flight fade (the mode switch is the ghost's other door)",
          "leaving fullscreen while a fade is in flight" in zc)

    # 3. THE WALL CLOCK FADE - the flicker. the fixed step is retired;
    #    the advance follows the real tick spacing so a jittery timer
    #    cannot clump the steps, and the submit rate halves.
    check("the fade interval rides the stable 30ms clock multiple",
          "#define _ZOOMUI_FADE_INTERVAL 30" in zc and
          "#define _ZOOMUI_FADE_MS 225" in zc)
    check("the fixed step constant is retired",
          "#define _ZOOMUI_ALPHA_STEP" not in zc)
    check("the fade keeps a wall clock of its own ticks",
          "static DWORD _zoomui_fade_tick" in zc and
          "(now_ms - _zoomui_fade_tick) * _ZOOMUI_ALPHA_OPAQUE) / _ZOOMUI_FADE_MS" in zc)
    check("a first tick creeps and a stalled timer catches up in one submit",
          "advance = 1;" in zc and "creep" in zc and
          "advance = _ZOOMUI_ALPHA_OPAQUE;" in zc)

    # 4. the pill stays one row in both modes (the round shrinks it, it
    #    does not fork it).
    check("the seven cell row survives the diet",
          "#define _ZOOMUI_CELL_COUNT 7" in zc and
          "void zoomui_set_fullscreen(int fullscreen);" in zh_)
    check("viv.c keeps feeding the activity hook",
          "zoomui_activity();" in viv)

    # 5. the version, the changelog and the readme twins.
    top = changes.lstrip("\ufeff").split("\r\n")[0]
    check("the changelog top entry is the pill field round pre-release",
          top == "Pre-release: Version 1.1.15-rc.16 (the parallel evaluation round)", top)
    check("the version is 1.1.15-rc.16 build 95",
          "#define VERSION_BUILD 95" in version and
          '#define VERSION_STRING "1.1.15-rc.16"' in version)
    check("the readme twins carry the round as the candidate",
          "**1.1.15-rc.16 \u2014 the parallel evaluation round (the current release candidate):**" in readme and
          "**1.1.15-rc.16 \u2014 \u5e76\u884c\u8bc4\u4f30\u8f6e\uff08\u5f53\u524d\u5019\u9009\u7248\uff09\uff1a**" in readme_cn)


def t_parallel_eval_round128():
    """Guards for the parallel evaluation round (1.1.15-rc.16: four
    read-only agents swept the rc.15 tree - the deferred ledger, the
    six-dimension review, the mutation census, the hygiene walk - and
    the main thread re-read every load-bearing claim before a single
    byte moved. the verdict lands in three layers. the behavior layer
    takes the claims that survived the re-read: the ghost drag (the
    capture-lost handler cleared three press states and forgot the
    scroll thumb), the delete queue (a queued next file over the
    post-delete reload decodes twice), the silent ini (a failed save
    only ever reached a debug build), the rotate ask (the verb rewrites
    the source file while the wallpaper asks first), and the usage pair
    (two implemented switches shipped commented out). the claims that
    did not survive stayed code: the refresh and the jump-to read the
    requested identity on purpose - the retry and the directory
    context belong to the last ask, not the last success. the teeth
    layer pins what the mutation census proved unpinned: the stall
    threshold, the pill's two tables, the fade retire and the dropdown
    bindings - the constants, tables and bindings the machine pins of
    the last rounds never reached. the hygiene layer freezes the round
    docstrings against the version sweep (it dragged round-123 from
    rc.12 to rc.14 across two rounds - the fifth recurrence)."""
    print("the parallel evaluation round (1.1.15-rc.16)")
    settings = read("src/viv_settings.c").decode()
    view = read("src/viv_view.c").decode()
    config = read("src/config.c").decode()
    dialogs = read("src/viv_dialogs.c").decode()
    zoomui = read("src/zoomui.c").decode()
    version = read("src/version.h").decode()
    changes = read("Changes.txt").decode("utf-8", errors="replace")

    s = settings.replace("\r\n", "\n")
    v = view.replace("\r\n", "\n")
    c = config.replace("\r\n", "\n")
    d = dialogs.replace("\r\n", "\n")
    z = zoomui.replace("\r\n", "\n")

    # 1. THE GHOST DRAG (the six-dimension review's spill find): the
    #    capture-lost handler cleared the button press, the title press
    #    and the hot swatch - the scroll thumb rode the same capture
    #    and outlived it. an alt-tab mid drag left the list following
    #    the mouse (a drag without a button).
    i = s.find("case WM_CAPTURECHANGED:")
    cap = s[i:s.find("case WM_SETFOCUS:", i)]
    check("the capture-lost handler parks the scroll thumb",
          "_viv_settings_scroll_drag = 0;" in cap)
    check("the park sits inside the lost-capture test (not on any capture)",
          cap.find("_viv_settings_scroll_drag = 0;") > cap.find("if ((HWND)lParam != hwnd)") >= 0)
    # 2. THE DELETE QUEUE (the ledger's surviving fd-identity claim):
    #    a next file queued while a load is in flight lands over the
    #    post-delete reload - two decodes for one step. the delete
    #    success path retires the queue (the paste and blank paths set
    #    the shape in rc.13).
    def body128(s, sig):
        i = 0
        while True:
            i = s.find(sig, i)
            if i < 0:
                return ""
            b = s.find("{", i)
            semi = s.find(";", i)
            if 0 <= semi < b:
                i += 1
                continue
            break
        depth, j = 0, b
        while j < len(s):
            if s[j] == "{":
                depth += 1
            elif s[j] == "}":
                depth -= 1
                if depth == 0:
                    break
            j += 1
        return s[b:j]
    dele = body128(v, "static void _viv_delete(int permanently)")
    check("the delete path retires a queued next file",
          "if (_viv_load_image_next_fd)" in dele and
          "mem_free(_viv_load_image_next_fd);" in dele and
          "_viv_load_image_next_fd = NULL;" in dele)
    check("the retire sits after the confirmed delete (not on the abort path)",
          0 <= dele.find("_viv_playlist_delete(&fd);") < dele.find("if (_viv_load_image_next_fd)"))

    # 3. THE SILENT INI (the review's diagnostics find): both save
    #    failure paths kept the previous ini and told only a debug
    #    build. the boxes name the outcome so the "where did my
    #    settings go" question has an answer in a release build too.
    check("the save-failure boxes reach the release build",
          'localization_get_string(LOCALIZATION_ID_CONFIG_SAVE_FAILED_MESSAGE)' in c and
          c.count("viv_msgbox(_viv_hwnd,caption_wbuf,message_wbuf,MB_OK | MB_ICONWARNING)") == 2)
    # the second box must sit in the CopyFile else - a move fallback
    # that succeeded is not a failure.
    i = c.find("if (!MoveFileExW(tempname,filename,MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))")
    rep = c[i:]
    j = rep.find("if (CopyFile(tempname,filename,FALSE))")
    check("the replace-both-failed box sits in the copy fallback's else",
          -1 < j < rep.find("viv_msgbox("))
    check("the write-failed box precedes the replace leg",
          c.find("LOCALIZATION_ID_CONFIG_SAVE_FAILED_MESSAGE") <
          c.find("if (!MoveFileExW(tempname,filename,MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))"))

    # 4. THE ROTATE ASK (the review's asymmetry find): the verb
    #    rewrites the source file on disk - the wallpaper asks before
    #    it touches the desktop, the rotation asks before it touches
    #    the source. the memory rotation rides inside the yes.
    rot = body128(v, "static void _viv_edit_rotate(int counterclockwise)")
    check("the rotation asks before the verb",
          "LOCALIZATION_ID_ROTATE_IMAGE_MESSAGE" in rot and
          "viv_msgbox(_viv_hwnd,caption_wbuf,message_wbuf,MB_OKCANCEL | MB_ICONQUESTION) != IDOK" in rot)
    check("a refused ask returns before the verb and the memory rotation",
          -1 < rot.find("!= IDOK)") < rot.find("return;") < rot.find("os_shell_execute(") and
          rot.find("!= IDOK)") < rot.find("_viv_orientate_hbitmap("))

    # 5. THE USAGE PAIR: two implemented command line switches shipped
    #    commented out of the usage text (the /everything and /random
    #    legs answer in the parser).
    def usage_line(prefix):
        for ln in d.split("\n"):
            t = ln.strip()
            if t.startswith(prefix) and not t.startswith("//"):
                return True
        return False
    check("the usage names the everything search (uncommented)",
          usage_line('"/everything <search>'))
    check("the usage names the random search (uncommented)",
          usage_line('"/random <search>'))

    # 6. THE STALL THRESHOLD (the census's B1): the mutation dropped
    #    250 to 25 and every suite stayed green - on a 30ms clock every
    #    normal tick would finish the sweep in one submit, the rc.15
    #    flicker at full strength. the threshold is the fade budget
    #    plus its own guard band, pinned by spelling.
    check("the stall threshold is the fade budget's own guard band",
          "if (now_ms - _zoomui_fade_tick > 250)" in z and
          "a gap wider than the whole fade budget" in z)

    # 7. THE PILL TABLES (the census's B2): swapping PREV and NEXT in
    #    the command table and the glyph table left every suite green -
    #    the prev cell would fire next. both tables are census-pinned
    #    cell by cell now.
    i = z.find("static const int _zoomui_cell_command_ids[_ZOOMUI_CELL_COUNT] =")
    cmd_tbl = z[i:z.find("};", i)]
    cmd_cells = re.findall(r"^\t(VIV_ID_\w+|_ZOOMUI_CELL_COMMAND_NONE),$", cmd_tbl, re.M)
    check("the command table census is the seven seats",
          cmd_cells == ["VIV_ID_NAV_PREV", "_ZOOMUI_CELL_COMMAND_NONE", "VIV_ID_NAV_NEXT",
                        "_ZOOMUI_CELL_COMMAND_NONE", "VIV_ID_VIEW_ZOOM_OUT",
                        "_ZOOMUI_CELL_COMMAND_NONE", "VIV_ID_VIEW_ZOOM_IN"], str(cmd_cells))
    i = z.find("static const int _zoomui_cell_glyph_ids[_ZOOMUI_CELL_COUNT] =")
    gly_tbl = z[i:z.find("};", i)]
    gly_cells = re.findall(r"^\t(GLYPH_\w+|_ZOOMUI_CELL_GLYPH_NONE),$", gly_tbl, re.M)
    check("the glyph table census is the seven seats",
          gly_cells == ["GLYPH_PREV", "_ZOOMUI_CELL_GLYPH_NONE", "GLYPH_NEXT",
                        "_ZOOMUI_CELL_GLYPH_NONE", "GLYPH_ZOOMOUT",
                        "_ZOOMUI_CELL_GLYPH_NONE", "GLYPH_ZOOMIN"], str(gly_cells))
    check("the two tables agree seat by seat (prev fires prev, next fires next)",
          all((cmd == "_ZOOMUI_CELL_COMMAND_NONE") == (gly == "_ZOOMUI_CELL_GLYPH_NONE")
              for cmd, gly in zip(cmd_cells, gly_cells)))

    # 8. THE FADE RETIRE (the census's B3): deleting the retire left
    #    the idle fade starting from a two-second-old tick - one giant
    #    submit, the pill vanishing instead of fading.
    k = z.find("the sweep is done: retire the wall clock")
    j = z.rfind("if (_zoomui_alpha == _zoomui_alpha_target)", 0, k)
    check("the finished sweep retires the wall clock",
          k > -1 and j > -1 and
          z.find("_zoomui_fade_tick = 0;", k) > k)

    # 9. THE DROPDOWN LABEL BINDINGS (the census's B4, the ledger's
    #    P2-5 fully closed): the switch rows carry a three-face census
    #    since rc.14 - the dropdown rows answered with a case-presence
    #    pin, and a label swap inside the label machine stayed green.
    #    the label face is a binding table now.
    drop_labels = {
        "LANGUAGE": "LOCALIZATION_ID_SETTINGS_LANGUAGE",
        "THEME": "LOCALIZATION_ID_SETTINGS_THEME",
        "SHRINK": "LOCALIZATION_ID_SHRINK_BLIT_MODE_STATIC",
        "MAG": "LOCALIZATION_ID_MAGNIFY_BLIT_MODE",
        "TITLE": "LOCALIZATION_ID_OPTIONS_TITLE_BAR_FORMAT_STATIC",
        "LEFT": "LOCALIZATION_ID_LEFT_CLICK_ACTION_STATIC",
        "RIGHT": "LOCALIZATION_ID_RIGHT_CLICK_ACTION_STATIC",
        "WHEEL": "LOCALIZATION_ID_MOUSE_WHEEL_ACTION_STATIC",
        "CTRLWHEEL": "LOCALIZATION_ID_CTRL_WHEEL_ACTION_STATIC",
        "KEYS": "LOCALIZATION_ID_SHORTCUT_KEY",
        "PRELOAD": "LOCALIZATION_ID_SETTINGS_PRELOAD_COUNT",
        "CACHE": "LOCALIZATION_ID_SETTINGS_CACHE_COUNT",
    }
    def fn_body(sig):
        fi = s.find(sig + "\n")
        if fi < 0:
            return ""
        b = s.find("{", fi)
        depth, j = 0, b
        while j < len(s):
            if s[j] == "{":
                depth += 1
            elif s[j] == "}":
                depth -= 1
                if depth == 0:
                    break
            j += 1
        return s[b:j]
    paint = fn_body("static void _viv_settings_paint(HWND hwnd)")
    di = paint.find("case _VIV_SETTINGS_CT_DROPDOWN:")
    dw = paint.find("case _VIV_SETTINGS_CT_SWITCH:", di)
    drop_region = paint[di:dw]
    bound = {}
    for m in re.finditer(r"case _VIV_SETTINGS_ID_(\w+):\n(.*?)break;", drop_region, re.S):
        lid = re.search(r"_viv_settings_draw_label\(mem,&label_rect,(LOCALIZATION_ID_\w+)",
                        m.group(2))
        bound[m.group(1)] = lid.group(1) if lid else None
    for name, want in sorted(drop_labels.items()):
        check(f"the dropdown {name.lower()} label binds its own string",
              bound.get(name) == want, str(bound.get(name)))
    check("the autotype sub row stays label-free by design",
          bound.get("AUTOTYPE") is None and
          "the sub row of the auto size switch: no label" in drop_region)

    # 10. THE DROPDOWN RUN BINDINGS (the census's B5): the run face is
    #     a binding table too - a case wired to the wrong config
    #     variable reads self-consistent in the ui and answers with
    #     the wrong setting.
    run_bind = {
        "LANGUAGE": "config_language",
        "THEME": "config_dark_mode",
        "SHRINK": "config_shrink_blit_mode",
        "MAG": "config_mag_filter",
        "TITLE": "config_title_bar_format",
        "AUTOTYPE": "config_auto_zoom_type",
        "LEFT": "config_left_click_action",
        "RIGHT": "config_right_click_action",
        "WHEEL": "config_mouse_wheel_action",
        "CTRLWHEEL": "config_ctrl_mouse_wheel_action",
        "PRELOAD": "config_preload_count",
        "CACHE": "config_cache_count",
    }
    ri = s.find("static void _viv_settings_run_dropdown(HWND hwnd,const _viv_settings_ctl_t *ctl)")
    run = s[ri:]
    run = run[:run.find("\n}\n")]
    for m in re.finditer(r"case _VIV_SETTINGS_ID_(\w+):\n(.*?)break;", run, re.S):
        cfgs = set(re.findall(r"(config_\w+)", m.group(2)))
        name = m.group(1)
        if name in run_bind:
            check(f"the dropdown {name.lower()} run binds its own config",
                  run_bind[name] in cfgs, f"{name}: {sorted(cfgs)}")
            # round-128 (the mutation census's M13): a case can carry the
            # right variable three times and still hand the popup the
            # wrong one - the ui reads self-consistent and the setting
            # writes to its own name. the popup call's own argument is
            # the binding that matters.
            if "_viv_settings_popup(" in m.group(2):
                # the popup's last argument is either the config itself
                # (the direct handoff) or a folded local (the theme,
                # shrink and magnify cases compute a mode from their
                # config first) - the fold must still read its own
                # variable on the right of an assignment.
                check(f"the dropdown {name.lower()} popup hands its own config",
                      re.search(r"_viv_settings_popup\([^;]*" + run_bind[name] + r"\)",
                                m.group(2)) is not None or
                      re.search(r"[^=!<>]= [^;]*" + run_bind[name], m.group(2)) is not None, name)
    check("the preload run carries the apply coupling (the count change rule)",
          re.search(r"case _VIV_SETTINGS_ID_PRELOAD:\n(?:(?!break;).)*config_cache_count = config_preload_count;", run, re.S) is not None)

    # 11. THE DOCSTRING FREEZE (the hygiene walk's root cause): the
    #     version sweep is a plain rc-word replace and history's
    #     docstrings ride along - round-123 shipped as rc.12, two
    #     sweeps later it read rc.14. the freeze table pins every
    #     round's own version; a sweep that drags one goes red here.
    freeze = {
        "t_slot_architecture_round109": "rc.2",
        "t_audit_response_round110": "rc.2",
        "t_audit_response_round111": "rc.3",
        "t_command_picker_round112": "rc.4",
        "t_audit_response_round113": "rc.6",
        "t_judged_fixes_round114": "rc.6",
        "t_audit_response_round118": "rc.8",
        "t_memory_and_cache_round120": "rc.9",
        "t_gui_limits_round121": "rc.10",
        "t_resume_and_chain_round122": "rc.11",
        "t_report_fusion_round123": "rc.12",
        "t_field_response_round124": "rc.13",
        "t_settings_capacity_round126": "rc.14",
        "t_pill_field_round127": "rc.15",
    }
    here = open("tests/menu_structure_test.py", "r", encoding="utf-8").read()
    for fn, want in sorted(freeze.items()):
        m = re.search(r"def " + fn + r"\(\):\n    \"\"\"(.*?)\"\"\"", here, re.S)
        got = re.search(r"1\.1\.15-(rc\.\d+)", m.group(1)).group(1) if m and re.search(r"1\.1\.15-(rc\.\d+)", m.group(1)) else None
        check(f"the {fn[len('t_'):]}'s docstring keeps its own version ({want})",
              got == want, str(got))

    # 12. THE NEW STRINGS (four): the rotate ask and the ini boxes.
    check("the four new strings ride all three files",
          localization_count("src/localization.h") >= 313)
    check("the rotate caption and message live in the enum",
          "LOCALIZATION_ID_ROTATE_IMAGE_CAPTION," in read("src/localization.h").decode() and
          "LOCALIZATION_ID_ROTATE_IMAGE_MESSAGE," in read("src/localization.h").decode())
    check("the save-failed caption and message live in the enum",
          "LOCALIZATION_ID_CONFIG_SAVE_FAILED_CAPTION," in read("src/localization.h").decode() and
          "LOCALIZATION_ID_CONFIG_SAVE_FAILED_MESSAGE," in read("src/localization.h").decode())

    # 13. THE VERSION, THE CHANGELOG.
    check("version.h = 1.1.15-rc.16.95 (the parallel evaluation round)",
          '#define VERSION_MAJOR 1' in version and
          '#define VERSION_MINOR 1' in version and
          '#define VERSION_REVISION 15' in version and
          '#define VERSION_BUILD 95' in version and
          '#define VERSION_STRING "1.1.15-rc.16"' in version)
    top = changes.lstrip("\ufeff").split("\r\n")[0]
    check("the changelog top entry is the parallel evaluation round pre-release",
          top == "Pre-release: Version 1.1.15-rc.16 (the parallel evaluation round)", top)


if __name__ == "__main__":
    t_panscan_gone()
    t_view_menu_shape()
    t_localization_alignment()
    t_paint_guard()
    t_version()
    t_pixel_budget()
    t_status_vararg_safety()
    t_dark_mode_wiring()
    t_ladder_shape()
    t_dark_detection_wiring()
    t_dark_dialogs_wiring()
    t_backdrop_wiring()
    t_progressive_wiring()
    t_thumbnail_api()
    t_context_menu_shape()
    t_paste_wiring()
    t_zoom_percent_wiring()
    t_review_fixes()
    t_review_fixes_round2()
    t_review_fixes_round4()
    t_release_engineering_round5()
    t_modernization_round6()
    t_round7()
    t_stable_round()
    t_dark_menu_bar()
    t_dark_layers_round()
    t_audit_round14()
    t_audit_round16()
    t_audit_round18()
    t_second_review_round40()
    t_ux_round41()
    t_field_fixes_round42()
    t_field_fixes_round43()
    t_field_fixes_round44()
    t_field_fixes_round46()
    t_field_fixes_round47()
    t_field_fixes_round48()
    t_field_fixes_round49()
    t_field_fixes_round50()
    t_field_fixes_round51()
    t_about_band_round64()
    t_white_band_round67()
    t_split_architecture_round69()
    t_split_architecture_round70()
    t_theme_race_round72()
    t_structure_round76()
    t_carpet_repair_round76()
    t_platform_guardrails()
    t_field_repair_round78()
    t_zoom_pane_editor_round79()
    t_field_report_round80()
    t_open_intent_round81()
    t_review_absorption_round82()
    t_stable_promotion_round83()
    t_readme_diet_round85()
    t_association_guard_round86()
    t_halftone_palette_round89()
    t_high_dpi_icons_round90()
    t_dead_residue_round91()
    t_release_title_crlf_fix()
    t_peripheral_residue_round92()
    t_field_sweep_round93()
    t_format_horizons_round94()
    t_todo_closure_round96()
    t_fixture_round98()
    t_navigation_visibility_round99()
    t_audit_response_round101()
    t_pixel_oracle_round102()
    t_audit_hardening_round103()
    t_input_ceiling_round104()
    t_renderer_parity_round105()
    t_navigation_faces_round106()
    t_corrections_round107()
    t_stable_promotion_round108()
    t_slot_architecture_round109()
    t_audit_response_round110()
    t_audit_response_round111()
    t_command_picker_round112()
    t_audit_response_round113()
    t_judged_fixes_round114()
    t_audit_response_round118()
    t_memory_and_cache_round120()
    t_gui_limits_round121()
    t_resume_and_chain_round122()
    t_report_fusion_round123()
    t_field_response_round124()
    t_settings_capacity_round126()
    t_pill_field_round127()
    t_parallel_eval_round128()
    print()
    if failures:
        print(f"{len(failures)} FAILURE(S)")
        sys.exit(1)
    print("ALL MENU STRUCTURE TESTS PASS")
