#!/usr/bin/env python3
"""The mutation teeth table: every pin this round landed must bite.

Each row names one mutation - an exact source swap the round's own
guards must catch. the harness applies the mutation, runs the suites
the row names, expects a nonzero exit (a red), restores the tree, and
reports the percentage. the table is deterministic: the same rows,
the same order, the same verdicts - "we caught it" is a number
anyone can recompute, not a sentence in a worklog.

Usage: python3 tools/mutation_teeth.py  (from the repository root)
"""
import subprocess
import sys
import os

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..")
os.chdir(ROOT)

SUITES = {
    "menu": ["python3", "tests/menu_structure_test.py"],
    "sim": ["python3", "tests/simulation_test.py"],
}

# (name, file, find, replace, suites-that-must-go-red)
MUTATIONS = [
    ("the toggle anchor regresses to the wrong pane",
     "src/viv_wndproc.c",
     "if (item == _viv_status_frame_pane_index())",
     "if (item == (int)SendMessage(_viv_status_hwnd,SB_GETPARTS,0,0) - 2)",
     ["menu"]),

    ("the layout measures the opening text again (the 9-to-10 hitch)",
     "src/viv_chrome.c",
     "_viv_status_frame_text_at(reserve_buf,_viv_slot_current.frame_count);",
     "_viv_status_frame_text(reserve_buf);",
     ["sim"]),

    ("the paint-level frame guard comes off",
     "src/viv_wndproc.c",
     'frame_state_ok = (_viv_slot_current.frame_count) && (_viv_frame_state_guard("paint"));',
     "frame_state_ok = (_viv_slot_current.frame_count);",
     ["menu"]),

    ("the truncated wrap waits forever again",
     "src/viv_wndproc.c",
     "_viv_frame_looped = 1;\r\n\t\t\t\t\t\t\t\t\t_viv_frame_position = -1;",
     "_viv_frame_looped = 0;\r\n\t\t\t\t\t\t\t\t\t_viv_frame_position = -1;",
     ["menu"]),

    ("the delete pair loses its fourth element",
     "src/viv_view.c",
     "_viv_should_activate_preload_on_load = 0;\r\n\t\t\t\t_viv_slot_preload.fd.cFileName[0] = 0;",
     "_viv_should_activate_preload_on_load = 0;\r\n\t\t\t\t_viv_slot_preload.fd.cFileName[0] = 1;",
     ["menu"]),

    ("the strip stops answering the keyboard",
     "src/viv_toolbar.c",
     "case WM_KEYDOWN:",
     "case 0x0BAD:",
     ["menu"]),

    ("the dead arrow lights for any hidden group again",
     "src/viv_toolbar.c",
     "_viv_toolbar_arrow_left = (itemi < _VIV_TOOLBAR_ITEM_COUNT) ? 1 : 0;",
     "_viv_toolbar_arrow_left = (_viv_toolbar_page > 0) ? 1 : 0;",
     ["menu"]),

    ("the frame step pauses on a refused step again",
     "src/viv_anim.c",
     "// the pause rides the step that actually moved: refusing",
     "// the pause rides any step request: refusing",
     ["menu"]),

    ("the render comparison loses its 64-bit ceiling",
     "src/viv_render.c",
     "if ((high * (__int64)_viv_slot_current.image_wide) / _viv_slot_current.image_high < wide)",
     "if ((high * _viv_slot_current.image_wide) / _viv_slot_current.image_high < wide)",
     ["menu"]),

    ("the clipboard pair forgets its error leg again",
     "src/viv_view.c",
     "if (!SetClipboardData(CF_HDROP,hmem))\r\n\t\t\t\t\t{\r\n\t\t\t\t\t\tGlobalFree(hmem);",
     "if (!SetClipboardData(CF_HDROP,hmem))\r\n\t\t\t\t\t{\r\n\t\t\t\t\t\t;",
     ["menu"]),

       ("the truncation advance leaves the sentinel behind again",
     "src/viv_wndproc.c",
     "lands).\r\n\t\t\t\t\t\t\t\t\t\t_viv_frame_position = 0;\r\n\t\t\t\t\t\t\t\t\t\t_viv_next(0,1,0,0);",
     "lands).\r\n\t\t\t\t\t\t\t\t\t\t_viv_frame_position",
     ["menu"]),

    ("the options rename drops the saved binding again",
     "src/config.c",
     '(_config_icompare_ascii(key_buf,"file_settings_keys") == 0)',
     '(_config_icompare_ascii(key_buf,"file_settings_keys") == 1)',
     ["menu"]),

    ("the strip's tab leg goes missing again",
     "src/viv_toolbar.c",
     "if (wParam == VK_TAB)",
     "if (wParam == 0x0BAD)",
     ["menu"]),

    ("the zero-delay webp burns a core again",
     "src/webp.c",
     "iter.duration ? (DWORD)iter.duration : 100",
     "iter.duration ? (DWORD)iter.duration : 1",
     ["menu"]),

    ("the poison probe comes back (the slwa sense flip)",
     "src/zoomui.c",
     "if (!SetLayeredWindowAttributes(_zoomui_hwnd,0,_ZOOMUI_ALPHA_OPAQUE,LWA_ALPHA))",
     "if (SetLayeredWindowAttributes(_zoomui_hwnd,0,_ZOOMUI_ALPHA_OPAQUE,LWA_ALPHA))",
     ["menu"]),

    ("the refusal self-heal loses its bookkeeping snap",
     "src/zoomui.c",
     "_zoomui_layered_ok = 0;\r\n\t\t\t_zoomui_alpha = _ZOOMUI_ALPHA_OPAQUE;\r\n\t\t\t_zoomui_alpha_target = _ZOOMUI_ALPHA_OPAQUE;\r\n\t\t\t_zoomui_fade_tick = 0;",
     "_zoomui_layered_ok = 0;",
     ["menu"]),

    ("the touch digitizer turns the pill on again",
     "src/config.c",
     'ini_get_int(ini,(const utf8_t *)"show_zoom_controls",config_show_zoom_controls)',
     'ini_get_int(ini,(const utf8_t *)"show_zoom_controls",os_is_touch_available())',
     ["menu"]),

    ("the message box paints a re-derived rect again",
     "src/viv_msgbox.c",
     "text_rect = _viv_msgbox_text_rect;",
     "text_rect.bottom = rect.bottom - _viv_msgbox_dip(24 + 32 + 16);",
     ["menu"]),

    ("the registration index loses its value name",
     "src/viv_install.c",
     '_viv_set_registry_string(hkey,(const utf8_t *)"voidImageViewer",capabilities_wbuf);',
     '_viv_set_registry_string(hkey,(const utf8_t *)"voidImageViewerX",capabilities_wbuf);',
     ["menu"]),

    ("the hardware acceleration switch stops persisting",
     "src/viv_install.c",
     "\t\tconfig_renderer = CONFIG_RENDERER_DIRECT3D;",
     "\t\tconfig_renderer = CONFIG_RENDERER_GDI;",
     ["menu"]),

    ("the wm_command source gate opens for anyone",
     "src/viv_wndproc.c",
     "if ((lParam != 0) && (!zoomui_is_pill_hwnd((HWND)lParam)))",
     "if (0)",
     ["menu"]),

    ("the unmarked profile keeps the pill (the migration never runs)",
     "src/config.c",
     'if (ini_get_int(ini,(const utf8_t *)"layout_migration",0) == 0)',
     'if (ini_get_int(ini,(const utf8_t *)"layout_migration",1) == 0)',
     ["menu"]),

    ("the save forgets to stamp the marker (the reset rides every launch)",
     "src/config.c",
     '_config_write_int(h,"layout_migration",1);\r\n',
     '',
     ["menu"]),

]


def run_suite(key):
    r = subprocess.run(SUITES[key], capture_output=True, text=True)
    return r.returncode


def main():
    caught = 0
    escaped = []
    for name, path, find, replace, suites in MUTATIONS:
        with open(path, "rb") as f:
            original = f.read()
        src = original.decode("latin-1")
        if src.count(find) != 1:
            print("SKIP (anchor drifted): %s" % name)
            escaped.append(name)
            continue
        with open(path, "wb") as f:
            f.write(src.replace(find, replace).encode("latin-1"))
        try:
            # a control-c between the write and the restore must
            # not leave the tree mutated behind it (the sweep's
            # own bookkeeping finding, applied to the tool itself).
            reds = {k: run_suite(k) for k in suites}
        finally:
            with open(path, "wb") as f:
                f.write(original)
        if all(rc != 0 for rc in reds.values()):
            caught += 1
            print("CAUGHT  %s  (red in %s)" % (name, ",".join(suites)))
        else:
            escaped.append(name)
            print("ESCAPED %s  (exit codes %s)" % (name, reds))
    total = len(MUTATIONS)
    pct = (100 * caught) // total if total else 100
    print()
    print("teeth: %d/%d CAUGHT (%d%%)" % (caught, total, pct))
    if escaped:
        print("escaped rows: %s" % escaped)
    return 0 if not escaped else 1


if __name__ == "__main__":
    sys.exit(main())
