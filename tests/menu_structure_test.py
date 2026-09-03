#!/usr/bin/env python3
"""Structure regression tests: menu table, pan&scan removal, localization
alignment, dark mode wiring, status-bar call safety. Guards the beta.6 and
beta.7 changes against regressions.

Run:  python3 tests/menu_structure_test.py
Exit 0 = pass.
"""
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
    return open(p, "rb").read()


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
    m = re.search(r"static _viv_command_t _viv_commands\[\]\s*=\s*\{(.*?)\n\};",
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
    check("View top level is decluttered (<= 13 rows)",
          len(view_rows) <= 13, f"{len(view_rows)} rows")
    for want in ("LOCALIZATION_ID_FULLSCREEN", "LOCALIZATION_ID_SLIDESHOW",
                 "LOCALIZATION_ID_OPTIONS"):
        check(f"{want} stays in View", any(r[0] == want for r in view_rows))
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
    # the four dark mode ids must be the LAST four entries everywhere
    tail = ("LOCALIZATION_ID_OPTIONS_DARK_MODE_STATIC",
            "LOCALIZATION_ID_DARK_MODE_AUTO",
            "LOCALIZATION_ID_DARK_MODE_LIGHT",
            "LOCALIZATION_ID_DARK_MODE_DARK")
    check("enum ends with the dark mode ids", tuple(ids[-4:]) == tail)
    check("en ends with the dark mode ids", tuple(en[-4:]) == tail)
    check("zh ends with the dark mode ids", tuple(zh[-4:]) == tail)
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
    check("version.h = 1.1.0.8 -beta.8",
          "VERSION_BUILD\t\t8" in vh and '"-beta.8"' in vh)
    check("rc = 1,1,0,8 + 1.1.0-beta.8",
          "1,1,0,8" in rc and rc.count("1.1.0-beta.8") >= 2)
    check("nsh = 1.1.0.8 + -beta.8",
          '!define VERSION "1.1.0.8"' in nsh and '!define BETAVERSION "-beta.8"' in nsh)


# ---------------------------------------------------------------------------
# 6. THE beta.6 bug class, guarded forever: the status bar zoom call must
#    pass exactly one int to the one-%d format. (beta.6 passed five varargs
#    starting with a double pan position; %d read the double's bits and the
#    status bar showed garbage like -755914244%.)
# ---------------------------------------------------------------------------
def t_status_vararg_safety():
    viv = read("src/viv.c").decode()
    m = re.search(r"static void _viv_status_update_temp_pos_zoom\(void\)\s*\{(.*?)\n\}",
                  viv, re.S)
    assert m, "status zoom function not found"
    body = m.group(1)
    call = re.search(
        r"string_printf\(\s*wbuf,\s*localization_get_string\(LOCALIZATION_ID_STATUS_BAR_POS_ZOOM_FORMAT\)\s*,([^;]*)\);",
        body, re.S)
    assert call, "string_printf call with the zoom format not found"
    args = call.group(1).strip()
    # exactly one argument after the format, and it is an int variable
    check("status call passes exactly one vararg", args == "percent", repr(args))
    check("percent is a local int", re.search(r"\bint percent;", body) is not None)
    # no double/float pan positions are passed any more
    check("no pan position doubles near the call",
          "x, y" not in args and "zoom_x" not in args)


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
    check("viv.c reads the dark combo in options OK",
          "ComboBox_GetCurSel(GetDlgItem(general_page,IDC_DARKMODE))" in viv)
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
    check("rc has the dark mode combobox row",
          "IDC_DARKMODE,54,70,132,87" in rc and "IDC_DARKMODE_STATIC,0,70,54,12" in rc)
    check("rc IDD_GENERAL grew to 218", "194, 218" in rc)
    check("resource.h has the ids",
          "#define IDC_DARKMODE_STATIC                     1069" in rh
          and "#define IDC_DARKMODE                            1070" in rh)


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
          "static int _viv_zoom_pos_max(void)" in viv
          and "_viv_zoom_scales[pos]" in viv)
    check("wheel clamp uses _viv_clamp_zoom_pos",
          "_viv_zoom_pos = _viv_clamp_zoom_pos(_viv_zoom_pos);" in viv)
    check("clamp_zoom_pos measures the live top",
          "pos_max = _viv_zoom_pos_max();" in viv)
    check("1:1 exit search starts at the live top",
          "hi = _viv_zoom_pos_max() + 1; // exclusive upper bound" in viv)
    check("1:1 exit searches are binary (O(log n) measurements)",
          viv.count("mid = lo + ((hi - lo) / 2);") == 2
          and "for(_viv_zoom_pos = 0;_viv_zoom_pos<_VIV_ZOOM_MAX;_viv_zoom_pos++)" not in viv)
    check("ladder top cache signature present",
          "_viv_zoom_pos_max_cache >= 0" in viv
          and "_viv_zoom_pos_max_cache_view_wide == wide" in viv)
    check("background brush cached across paints",
          "static HBRUSH _viv_background_hbrush = 0;" in viv
          and "CreateSolidBrush(brush_color)" in viv)
    check("render capped at 16x max(fit, native) in double space",
          "max_w = 16.0 * (double)((rw > _viv_image_wide) ? rw : _viv_image_wide);" in viv)
    check("int overflow guard for extreme panoramas",
          "_viv_clamp_double" in viv)


if __name__ == "__main__":
    t_panscan_gone()
    t_view_menu_shape()
    t_localization_alignment()
    t_paint_guard()
    t_version()
    t_status_vararg_safety()
    t_dark_mode_wiring()
    t_ladder_shape()
    print()
    if failures:
        print(f"{len(failures)} FAILURE(S)")
        sys.exit(1)
    print("ALL MENU STRUCTURE TESTS PASS")
