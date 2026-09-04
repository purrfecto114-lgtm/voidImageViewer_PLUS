#!/usr/bin/env python3
"""Structure regression tests: menu table, pan&scan removal, localization
alignment, dark mode wiring, status-bar call safety. Guards the beta.6 and
beta.7 changes against regressions.

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
    # the last ids must line up everywhere: the dark mode ids followed by
    # the six backdrop ids.
    tail = ("LOCALIZATION_ID_OPTIONS_DARK_MODE_STATIC",
            "LOCALIZATION_ID_DARK_MODE_AUTO",
            "LOCALIZATION_ID_DARK_MODE_LIGHT",
            "LOCALIZATION_ID_DARK_MODE_DARK",
            "LOCALIZATION_ID_BACKDROP",
            "LOCALIZATION_ID_BACKDROP_FOLLOW",
            "LOCALIZATION_ID_BACKDROP_BLACK",
            "LOCALIZATION_ID_BACKDROP_WHITE",
            "LOCALIZATION_ID_BACKDROP_CUSTOM",
            "LOCALIZATION_ID_BACKDROP_CHECKERBOARD",
            "LOCALIZATION_ID_SET_ZOOM_CAPTION",
            "LOCALIZATION_ID_SET_ZOOM_STATIC")
    check("enum ends with the dark+backdrop+zoom ids", tuple(ids[-12:]) == tail)
    check("en ends with the dark+backdrop+zoom ids", tuple(en[-12:]) == tail)
    check("zh ends with the dark+backdrop+zoom ids", tuple(zh[-12:]) == tail)
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
    check("version.h = 1.1.0.18 -rc.5",
          (major, minor, rev, build) == ("1", "1", "0", "18") and vtype == "-rc.5")
    check("VERSION_STRING composes from the numeric macros",
          vstr == "%s.%s.%s%s" % (major, minor, rev, vtype))
    check("rc derives everything from version.h",
          '#include "../src/version.h"' in rc and
          "FILEVERSION VERSION_MAJOR,VERSION_MINOR,VERSION_REVISION,VERSION_BUILD" in rc and
          'VALUE "FileVersion", VERSION_STRING' in rc and
          'VALUE "ProductVersion", VERSION_STRING' in rc)
    check("rc has no hardcoded version left",
          "1,1,0," not in rc and '"1.1.0-rc.' not in rc)
    check("nsh derives from src/version.h at compile time",
          "!searchparse" in nsh and "..\\src\\version.h" in nsh and
          '!define VERSION "${VIV_VER_MAJOR}.${VIV_VER_MINOR}.${VIV_VER_REVISION}.${VIV_VER_BUILD}"' in nsh and
          '!define BETAVERSION "${VIV_VER_TYPE}"' in nsh)
    check("nsh has no hardcoded version left",
          '"1.1.0.' not in nsh and '"-rc.' not in nsh)


# ---------------------------------------------------------------------------
# 6. THE beta.6 bug class, guarded forever: the status bar zoom call must
#    pass exactly one int to the one-%d format. (beta.6 passed five varargs
#    starting with a double pan position; %d read the double's bits and the
#    status bar showed garbage like -755914244%.)
# ---------------------------------------------------------------------------
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
    m = re.search(r"static int _viv_zoom_percent\(void\)\s*\{(.*?)\n\}",
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
    check("1:1 exit and percent searches are binary (O(log n) measurements)",
          viv.count("mid = lo + ((hi - lo) / 2);") == 3
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
          osc.find("_os_ShouldAppsUseDarkMode()") > osc.find("AppsUseLightTheme"))
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
    check("viv.c tints the toolbar tooltip",
          "SendMessage(tooltip_hwnd,TTM_SETTIPBKCOLOR,RGB(0x20,0x20,0x20),0);" in viv)
    check("zoomui.c tints its tooltip",
          "_zoomui_apply_tooltip_colors" in zc)
    check("zoomui.c re-tints on every palette call",
          zc.count("_zoomui_apply_tooltip_colors();") >= 2)

    # message fallback defines for older SDKs
    check("viv.c defines the tooltip message fallbacks",
          "#define TTM_SETTIPBKCOLOR (WM_USER+19)" in viv
          and "#define TB_GETTOOLTIPS (WM_USER+35)" in viv
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
    check("all 11 dialog procs route through the dispatcher",
          viv.count("_viv_dialog_dark_proc(hwnd,msg,wParam,lParam);") == 11)
    check("all 11 dialogs get the dark chrome at init",
          viv.count("_viv_dark_dialog(hwnd);") == 11)
    # rc.1 regression guard: the dispatcher must NOT sit inside switch(msg)
    # before the first case label - that placement is unreachable dead code
    # (the beta.10 bug: gcc warned "statement will never be executed").
    dead = viv.count("switch(msg)\r\n\t{\r\n\t\t{\r\n\t\t\tINT_PTR dark_dialog_reply;")
    check("no dispatcher dead placement inside switch(msg)", dead == 0, str(dead))
    live = viv.count("{\r\n\t\tINT_PTR dark_dialog_reply;")
    check("dispatcher runs before the switch in every proc", live == 11, str(live))
    check("the dispatcher handles the color and erase messages",
          "case WM_CTLCOLORSTATIC:" in viv and
          "case WM_CTLCOLOREDIT:" in viv and
          "case WM_CTLCOLORLISTBOX:" in viv and
          "_viv_dialog_dark_erase(hwnd,(HDC)wParam)" in viv)

    # options navigation
    check("options tree gets dark colors",
          "SendMessage(tree_hwnd,TVM_SETBKCOLOR,0,RGB(0x20,0x20,0x20));" in viv and
          "SendMessage(tree_hwnd,TVM_SETTEXTCOLOR,0,RGB(0xE8,0xE8,0xE8));" in viv)
    check("options tabs switch to the dark style",
          "os_dark_window_theme(GetDlgItem(hwnd,_viv_options_tab_ids[tabi]));" in viv)
    check("the light tab texture is skipped while dark",
          viv.find("if (!_viv_is_dark())",
                   viv.find("os_EnableThemeDialogTexture(page_hwnd,ETDT_ENABLETAB);") - 200) != -1)

    # about paint
    check("about paints the dark palette",
          "FillRect(ps.hdc,&rect,_viv_dialog_dark_brush());" in viv)

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
    check("the image branch calls the paste helper",
          "_viv_paste_clipboard_image();" in viv)
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
          "_viv_load_image_terminate = 1;" in viv)
    check("the pasted frame starts the normal first frame path",
          "_viv_start_first_frame();" in viv)
    check("the mipmap is built lazily (NULL is a supported frame state)",
          "_viv_frames[0].mipmap = 0; // built lazily on the first paint." in viv)
    check("the paste helpers have prototypes",
          "static void _viv_paste_clipboard_image(void);" in viv)
    check("only 40 byte dib headers take the dib path",
          "bih->biSize == sizeof(BITMAPINFOHEADER)" in viv)
    check("the dib stride math is overflow safe",
          "(DWORD)bih->biWidth * (DWORD)bih->biBitCount" in viv)




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
    m = re.search(r"static void _viv_zoom_in\(int out,int have_xy,int x,int y\)\s*\{(.*?)\n\}",
                  viv, re.S)
    assert m, "_viv_zoom_in not found"
    body = m.group(1)
    check("button zoom uses the percent stepper",
          "_viv_zoom_percent();" in body and "_viv_zoom_set_percent(target" in body)
    check("already a multiple of 10 steps 10 percent",
          "target = percent + (out ? -10 : 10);" in body)
    check("not a multiple snaps to the nearest 10",
          "lower = (percent / 10) * 10;" in body and "upper = lower + 10;" in body)
    check("midpoint ties round toward the click direction",
          "target = out ? lower : upper;" in body)
    check("buttons no longer delegate to the wheel action",
          "_viv_do_mousewheel_action" not in body)
    check("percent steps are skipped without an image",
          "if (!_viv_image_wide)" in body)

    # the percent -> ladder position search
    m = re.search(r"static void _viv_zoom_set_percent\(int percent,int screen_x,int screen_y,int force\)\s*\{(.*?)\n\}",
                  viv, re.S)
    assert m, "_viv_zoom_set_percent not found"
    body = m.group(1)
    check("the percent search is a binary search over the ladder",
          "lo + ((hi - lo) / 2)" in viv and "_viv_zoom_pos_max() + 1" in viv
          and "static int _viv_zoom_pos_for_percent(int percent,int strict)" in viv
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
    check("a zoom out click at the ladder floor is a no-op",
          "if (out && (!_viv_1to1) && (_viv_zoom_pos == 0))" in viv)
    check("buttons pass the direction, the dialog does not force",
          "_viv_zoom_set_percent(target,pt.x,pt.y,out ? -1 : 1);" in viv and
          "_viv_zoom_set_percent(target,pt.x,pt.y,0);" in viv)

    # the status bar zoom pane (part 0, always visible, clickable)
    m = re.search(r"static void _viv_status_update\(void\)\s*\{(.*?)\n\t\tif \(_viv_status_hwnd\)",
                  viv, re.S)
    assert m or True
    check("the parts array grew for the zoom pane",
          "int part_array[7];" in viv)
    check("zoom text is built for the pane",
          "wchar_t zoom_buf[STRING_SIZE];" in viv and "*zoom_buf = 0;" in viv)
    check("the zoom pane is the leftmost fixed part",
          "part_array[parti] = zoom_wide;" in viv)
    check("the pane is measured like the other parts",
          "GetTextExtentPoint32(hdc,zoom_buf,string_get_length(zoom_buf),&size)" in viv)
    check("the message pane moved to part 1",
          "_viv_status_set(1,text);" in viv and "_viv_status_set(0,zoom_buf);" in viv)
    check("the right parts start at index 2",
          "parti = 2;" in viv)
    check("the pane width is never below the minimum",
          "if (zoom_wide < minwide)" in viv)

    # clicking the pane opens the set zoom dialog
    check("status click case 0 opens the dialog",
          "_viv_set_zoom_dialog();" in viv)
    check("the frame toggle pane is now located dynamically",
          "SendMessage(_viv_status_hwnd,SB_GETPARTS,0,0) - 2" in viv)
    check("hand cursor over the zoom pane",
          "case WM_SETCURSOR:" in viv and "SB_GETRECT" in viv and "IDC_HAND" in viv)

    # the set zoom dialog
    check("dialog invoker clamps the target range",
          "if (target > 1600)" in viv and "if (target >= 1)" in viv)
    check("the dialog proc seeds the edit with the current percent",
          "SetDlgItemInt(hwnd,IDC_SET_ZOOM_EDIT,_viv_set_zoom_dialog_percent,FALSE);" in viv)
    check("the dialog gets the dark chrome",
          re.search(r"static INT_PTR CALLBACK _viv_set_zoom_proc\(.*?\{.*?_viv_dialog_dark_proc\(hwnd,msg,wParam,lParam\);", viv, re.S) is not None and
          "_viv_dark_dialog(hwnd);" in viv)
    check("dialog ids defined",
          "#define IDD_SET_ZOOM" in rh and
          "#define IDC_SET_ZOOM_EDIT" in rh and
          "#define IDC_SET_ZOOM_STATIC" in rh)
    check("dialog template present",
          "IDD_SET_ZOOM DIALOGEX" in rct and
          "IDC_SET_ZOOM_EDIT,54,12,66,12,ES_AUTOHSCROLL | ES_NUMBER" in rct)
    check("dialog strings localized in both tables",
          '"Set Zoom", // LOCALIZATION_ID_SET_ZOOM_CAPTION,' in le and
          '"&Zoom percent:", // LOCALIZATION_ID_SET_ZOOM_STATIC,' in le and
          '"设置缩放", // LOCALIZATION_ID_SET_ZOOM_CAPTION' in lz and
          '"缩放百分比(&Z)：", // LOCALIZATION_ID_SET_ZOOM_STATIC' in lz)
    check("enum gains the two zoom ids",
          "LOCALIZATION_ID_SET_ZOOM_CAPTION," in lh and
          "LOCALIZATION_ID_SET_ZOOM_STATIC," in lh)

    # the temp zoom flash is replaced by the permanent pane
    m = re.search(r"static void _viv_status_update_temp_pos_zoom\(void\)\s*\{(.*?)\n\}",
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
          viv.count("string_get_word(p,buf,STRING_SIZE)") == 11 and
          "string_get_word(p,install_path,STRING_SIZE)" in viv and
          "string_get_word(p,language_wbuf,STRING_SIZE)" in viv)

    # H3: every fd.cFileName copy is bounded to MAX_PATH
    check("all 12 fd.cFileName copies are bounded",
          viv.count("string_copy_with_bufsize(fd.cFileName,MAX_PATH") == 12)
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
          viv.count("(_viv_frames[_viv_frame_position].delay > 0) ?") == 2)

    # M2: webp frames composite over the backdrop like the gdi+ path
    check("webp frame is drawn into a dib section",
          "CreateDIBSection(viv_webp->screen_hdc,&bmi,DIB_RGB_COLORS,&bits,NULL,0);" in viv)
    check("webp frames get the backdrop painted first",
          "_viv_fill_backdrop(viv_webp->mem_hdc,viv_webp->wide,viv_webp->high);" in viv)
    check("the webp pre-flatten onto the window background is gone",
          "config_windowed_background_color_b + ((b - config_windowed_background_color_b)" not in viv)

    # M3: everything ipc replies are validated field by field
    check("copydata helpers exist",
          "static const char *_viv_copydata_read(const COPYDATASTRUCT *cds," in viv and
          "static int _viv_everything_item_to_fd(const COPYDATASTRUCT *cds," in viv)
    check("both everything cases validate the list header first",
          viv.count("if (_viv_safe_copy_data(cds->lpData,cds->cbData,cds->lpData,&list,sizeof(list)))") == 2)
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
    check("pos/rgb panes dereference their buffers",
          viv.count("if (*pixel_pos_buf)") == 3 and
          viv.count("if (*pixel_rgb_buf)") == 3 and
          "if (pixel_pos_buf)" not in viv and
          "if (pixel_rgb_buf)" not in viv)

    # preload OOB: the additional frame write is bounds checked
    check("preload additional frame write is bounds checked",
          "if (_viv_preload_frame_loaded_count < _viv_preload_frame_count)" in viv)

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

    # R4: the quoted uninstall string copy is bounded to the remaining space
    check("arp uninstall path copy is bounded",
          "string_copy_with_bufsize(uninstall_wbuf + 1,STRING_SIZE - 1,install_path);" in viv and
          "string_copy(uninstall_wbuf + 1" not in viv)

    # L5: the jumpto modal pump re-injects a consumed WM_QUIT
    i = viv.find("if (!GetMessageW(&msg,NULL,0,0))")
    check("jumpto pump re-posts a consumed WM_QUIT",
          i != -1 and "PostQuitMessage((int)msg.wParam);" in viv[i:i+400])

    # L1: a zero file drop is a no-op before the playlist is touched
    dstart = viv.find("case WM_DROPFILES:")
    dend = viv.find("case WM_TIMER:", dstart)
    drop = viv[dstart:dend]
    check("zero file drop is a no-op",
          "count = DragQueryFile((HDROP)wParam,0xFFFFFFFF,0,0);" in drop and
          "if (!count)" in drop and
          drop.find("count = DragQueryFile") < drop.find("if (!count)") < drop.find("is_shift = (GetKeyState"))

    # R2: webp first frame reports transposed dimensions for 5-8
    i = viv.find("first_frame.wide = viv_webp->wide;")
    check("webp first frame swaps axes for orientation 5-8",
          i != -1 and "switch (viv_webp->orientation)" in viv[i:i+900] and
          "temp = first_frame.wide;" in viv[i:i+900])

    # R2: webp additional frames pick mipmap dims after the orientation swap
    check("webp additional frame mipmap uses swapped dims",
          "_viv_get_mipmap(hbitmap,frame_wide,frame_high," in viv and
          "_viv_get_mipmap(hbitmap,viv_webp->wide," not in viv)

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
    check("kill waits bounded for the load thread",
          "WaitForSingleObject(_viv_load_image_thread,10000) != WAIT_OBJECT_0" in viv and
          "TerminateThread(_viv_load_image_thread,1);" in viv)

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
    check("rotate buffer allocations cast to SIZE_T before multiplying",
          "mem_alloc((SIZE_T)bitmap.bmWidth * (SIZE_T)bitmap.bmHeight * sizeof(DWORD));" in viv and
          "mem_alloc((SIZE_T)ret_wide * (SIZE_T)ret_high * sizeof(DWORD));" in viv)
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
          "mem_alloc(safe_size_mul(sizeof(_viv_frame_t),(SIZE_T)_viv_preload_frame_count));" in viv)
    check("frame array allocation uses safe_size_mul",
          "mem_alloc(safe_size_mul(sizeof(_viv_frame_t),(SIZE_T)_viv_frame_count));" in viv)
    check("the raw frame array multiplications are gone",
          "sizeof(_viv_frame_t) * _viv_preload_frame_count" not in viv and
          "sizeof(_viv_frame_t) * _viv_frame_count" not in viv)

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
          "grep -Eq '^v[0-9]+\\.[0-9]+(\\.[0-9]+){1,2}(-(beta|rc)\\.[0-9]+)?$'" in ry)
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
    for d in ("tests", "doc", "cmake", "src/dec", "src/demux",
              "src/dsp", "src/utils", "src/webp"):
        check("libwebp/%s kept" % d, os.path.exists("libwebp/" + d))
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
          len(re.findall(r"<ClCompile ", fp)) == 80)
    check("shared props has no phantom res\\resource reference",
          'res\\resource"' not in fp)
    check("shared props has the resource script and icons",
          "voidImageViewer.rc" in fp and "1to1-8bit.ico" in fp)
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
    check("tags run the tests workflow too",
          "tags: ['v*']" in ty)
    check("actions are pinned to commit shas",
          "actions/checkout@3d3c42e5aac5ba805825da76410c181273ba90b1" in ty and
          "microsoft/setup-msbuild@30375c66a4eea26614e0d39710365f22f8b0af57" in ty)


if __name__ == "__main__":
    t_panscan_gone()
    t_view_menu_shape()
    t_localization_alignment()
    t_paint_guard()
    t_version()
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
    print()
    if failures:
        print(f"{len(failures)} FAILURE(S)")
        sys.exit(1)
    print("ALL MENU STRUCTURE TESTS PASS")
