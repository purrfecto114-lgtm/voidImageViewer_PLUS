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
     '_viv_set_registry_string(hkey,(const utf8_t *)"void Image Viewer",capabilities_wbuf);',
     '_viv_set_registry_string(hkey,(const utf8_t *)"voidImageViewer",capabilities_wbuf);',
     ["menu"]),

    ("the applications seat loses its icon (rc.7)",
     "src/viv_install.c",
     'string_cat_utf8(icon_wbuf,(const utf8_t *)",0");',
     'string_cat_utf8(icon_wbuf,(const utf8_t *)",1");',
     ["menu"]),

    ("the installer forgets the wic family (rc.7)",
     "src/viv_state.h",
     "#define _VIV_ASSOCIATION_COUNT\t19",
     "#define _VIV_ASSOCIATION_COUNT\t11",
     ["menu"]),

    ("an extension word falls off the nsis phase (rc.7)",
     "nsis/installer.nsi",
     'MUI_INSTALLOPTIONS_READ $R0 "InstallOptions2.ini" "Field 15" "State"',
     'MUI_INSTALLOPTIONS_READ $R0 "InstallOptions2.ini" "Field 99" "State"',
     ["menu"]),

    ("the options page loses its fields (rc.7)",
     "nsis/InstallOptions2.ini",
     "NumFields=23",
     "NumFields=14",
     ["menu"]),

    ("the wic note falls off the options page (rc.7)",
     "nsis/InstallOptions2.ini",
     "Text=AVIF / HEIF / JPEG-XR / DDS ride the system's WIC codecs",
     "Text=AVIF / HEIF / JPEG-XR / DDS ride the system's WIC codec",
     ["menu"]),

    ("a localization pair goes missing (rc.7)",
     "src/localization_en_us.h",
     '"AV1 Image", // LOCALIZATION_ID_ASSOCIATION_DESCRIPTION_AVIF',
     '"AV1 Image", // LOCALIZATION_ID_ASSOCIATION_DESCRIPTION_AVIFX',
     ["menu"]),

    ("the ctl capacity shrinks below the nineteen-box page (rc.7)",
     "src/viv_settings.c",
     "#define _VIV_SETTINGS_CTL_MAX 48",
     "#define _VIV_SETTINGS_CTL_MAX 40",
     ["menu"]),

    ("the ico icon stops being the file itself (rc.7)",
     "src/viv.c",
     '"%1",',
     '"%2",',
     ["menu"]),

    ("the dead digitizer probe returns (rc.7)",
     "src/os.h",
     "// vista+ regdeletekeyexw, resolved lazily (see os.c). returns 1 when the",
     "int os_is_touch_available(void);\n\n// vista+ regdeletekeyexw, resolved lazily (see os.c). returns 1 when the",
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

    ("the first-frame settle loses its caption refresh (rc.6)",
     "src/viv_wndproc.c",
     "opened under the bare app name.\r\n\t\t\t\t\t\t\t_viv_update_title();",
     "opened under the bare app name.\r\n\t\t\t\t\t\t\t\t_viv_update_title_();",
     ["menu"]),

    ("the cache hit runs its chrome before the take again (rc.6)",
     "src/viv_load.c",
     "half the time.\r\n\t_viv_update_title();\r\n\t_viv_status_update();",
     "half the time.\r\n\t_viv_update_title_();\r\n\t_viv_status_update();",
     ["menu"]),

    ("the preload landing forgets the caption (rc.6)",
     "src/viv_load.c",
     "// screen).\r\n\t_viv_update_title();",
     "// screen).\r\n\t_viv_update_title_();",
     ["menu"]),

    ("the interpolation pair ignores the renderer again (rc.6)",
     "src/viv_settings.c",
     "return (config_renderer == CONFIG_RENDERER_GDI) ? 1 : 0;",
     "return (config_renderer == CONFIG_RENDERER_GDI) ? 1 : 1;",
     ["menu"]),

    ("the touch claim drops the single finger (rc.6)",
     "src/viv_settings.c",
     "gesture_configs[0].dwWant = 0x13;",
     "gesture_configs[0].dwWant = 0x11;",
     ["menu"]),

    ("the relay ignores the directory probe (rc.6)",
     "src/viv_install.c",
     "\t\t\t\trelay = 1;\r\n",
     "\t\t\t\t\trelay = 0;\r\n",
     ["menu"]),

    ("the refused elevation reads as success again (rc.6)",
     "src/viv.c",
     "return (install_ret == 2) ? 2 : 0;",
     "return (install_ret == 2) ? 0 : 0;",
     ["menu"]),

    ("the start menu forgets the per-user seat (rc.6)",
     "src/viv_install.c",
     "os_is_admin() ? CSIDL_COMMON_PROGRAMS : CSIDL_PROGRAMS",
     "os_is_admin() ? CSIDL_COMMON_PROGRAMS : CSIDL_COMMON_PROGRAMS",
     ["menu"]),

    ("the uninstall sweeps one seat again (rc.6)",
     "src/viv_install.c",
     "for(folderi=0;folderi<2;folderi++)",
     "for(folderi=0;folderi<1;folderi++)",
     ["menu"]),

    ("the alpha product narrows again (codeql #5)",
     "libwebp/src/dec/alpha_dec.c",
     "const size_t alpha_decoded_size = (size_t)dec->width * dec->height;",
     "const size_t alpha_decoded_size = dec->width * dec->height;",
     ["menu"]),

    ("the palette square narrows again (codeql #4)",
     "libwebp/src/utils/palette.c",
     "(size_t)num_colors * num_colors",
     "(num_colors * num_colors)",
     ["menu"]),

    ("the quantizer scratch narrows again (codeql #3)",
     "libwebp/src/utils/quant_levels_dec_utils.c",
     "(R + 1) * (size_t)width * sizeof(*p->start)",
     "(R + 1) * width * sizeof(*p->start)",
     ["menu"]),

    ("the frame f_info product narrows again (codeql #2)",
     "libwebp/src/dec/frame_dec.c",
     "(size_t)mb_w * (dec->mt_method > 0 ? 2 : 1) * sizeof(VP8FInfo)",
     "mb_w * (dec->mt_method > 0 ? 2 : 1) * sizeof(VP8FInfo)",
     ["menu"]),

    ("the frame mb_data product narrows again (codeql #1)",
     "libwebp/src/dec/frame_dec.c",
     "(dec->mt_method == 2 ? 2 : 1) * (size_t)mb_w * sizeof(*dec->mb_data)",
     "(dec->mt_method == 2 ? 2 : 1) * mb_w * sizeof(*dec->mb_data)",
     ["menu"]),

    ("the un-relayed second stage raises uac again (rc.6)",
     "src/viv_install.c",
     'string_cat_utf8(install_options,(const utf8_t *)" /isrunas");',
     'string_cat_utf8(install_options,(const utf8_t *)" /isrunasX");',
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
