#!/usr/bin/env python3
"""The mutation teeth table: every pin this round landed must bite.

Each row names one mutation - an exact source swap the round's own
guards must catch. the harness refuses to start until every suite it
names has run green on the unmutated tree (a suite that is already
red would name every tooth after itself: a killed run's resident
mutation, a broken environment - no verdict rides a broken meter,
the baseline answers before the first tooth is cut), then applies
the mutation, runs the suites the row names, expects a nonzero exit
(a red), restores the tree, and reports the percentage. the table is
deterministic: the same rows, the same order, the same verdicts -
"we caught it" is a number anyone can recompute, not a sentence in
a worklog.

Usage: python3 tools/mutation_teeth.py  (from the repository root)
"""
import subprocess
import sys
import os

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..")
os.chdir(ROOT)

# the full guard family: the baseline gate runs every suite in the
# vocabulary on the unmutated tree, and the rows below name their
# verdict suites from the same table (a red anywhere is a broken
# meter - the audit's scenario was the menu suite crashing on
# windows, but the refusal does not wait to learn which suite
# poisoned which rows).
SUITES = {
    "menu": ["python3", "tests/menu_structure_test.py"],
    "sim": ["python3", "tests/simulation_test.py"],
    "zoom": ["python3", "tests/zoom_math_test.py"],
    "byte": ["python3", "tests/byte_invariant_test.py"],
    "pixel": ["python3", "tests/pixel_golden_test.py"],
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

   ("the options relay trusts the caller's word again (rc.11)",
     "src/viv_install.c",
     "if (string_icompare_lowercase_ascii(word_body,allowed[i]) == 0)",
     "if (string_icompare_lowercase_ascii(word_body,allowed[i]) != 0)",
     ["menu"]),

    ("the locked-elsewhere probe spells the class mixed-case again (rc.11)",
     "src/viv_install.c",
     's = "voidimageviewer.";',
     's = "voidImageViewer.";',
     ["menu"]),

    ("the debug allocation adds unwrapped again (rc.11)",
     "src/mem.c",
     "alloc_size = safe_size_add(safe_size_add(sizeof(mem_debug_t),size),safe_size_mul_sizeof_pointer(MEM_MAGIC_SIZE));",
     "alloc_size = sizeof(mem_debug_t) + size + (sizeof(void *) * MEM_MAGIC_SIZE);",
     ["menu"]),

    ("the copydata stride answers the raw length again (rc.11)",
     "src/viv.c",
     "d += ((command_line_length + 1) * sizeof(wchar_t));",
     "d += ((string_get_length(command_line) + 1) * sizeof(wchar_t));",
     ["menu"]),

    ("the zeroing sister narrows to int again (rc.11)",
     "src/os.c",
     "void os_zero_memory(void *data,SIZE_T size)",
     "void os_zero_memory(void *data,int size)",
     ["menu"]),

    ("the pill's dib zero narrows again (rc.11)",
     "src/zoomui.c",
     "((SIZE_T)_zoomui_dib_wide * 4) * (SIZE_T)_zoomui_dib_high",
     "(_zoomui_dib_wide * 4) * _zoomui_dib_high",
     ["menu"]),

    ("the webp frame-delay zero takes the int cast again (rc.11)",
     "src/webp.c",
     "os_zero_memory(frame_delays,frame_delay_bytes);",
     "os_zero_memory(frame_delays,(int)frame_delay_bytes);",
     ["menu"]),

    ("the main window drops the capture hand-off again (rc.11)",
     "src/viv_wndproc.c",
     "case WM_CAPTURECHANGED:\r\n\t\t\treturn _viv_on_wm_capturechanged(hwnd,msg,wParam,lParam);",
     "case 0x0200:\r\n\t\t\treturn _viv_on_wm_capturechanged(hwnd,msg,wParam,lParam);",
     ["menu"]),

    ("the slideshow flag rises on a dead timer again (rc.11)",
     "src/viv_view.c",
     "if (SetTimer(_viv_hwnd,VIV_ID_SLIDESHOW_TIMER,config_slideshow_rate,0))",
     "if (SetTimer(_viv_hwnd,VIV_ID_SLIDESHOW_TIMER,config_slideshow_rate,0),1)",
     ["menu"]),

    ("the shuffle close shifts bytes again (rc.11)",
     "src/viv_playlist.c",
     "(_viv_playlist_count - (index + 1)) * sizeof(_viv_playlist_t *)",
     "(_viv_playlist_count - (index + 1))",
     ["menu"]),

    ("the last-file buffer shrinks to max_path again (rc.11)",
     "src/config.c",
     "wchar_t config_last_file[STRING_SIZE] = {0};",
     "wchar_t config_last_file[MAX_PATH] = {0};",
     ["menu"]),

    ("the init canvas refusal answers the generic one again (rc.11)",
     "src/viv.c",
     "if (init_ret == -6)\r\n\t{\r\n\t\treturn 6;\r\n\t}",
     "if (init_ret == -6)\r\n\t{\r\n\t\treturn 1;\r\n\t}",
     ["menu"]),

    ("the export probe forgets the install namespace again (rc.11)",
     "src/viv_export.c",
     "if (install_word_seen && _viv_export_mode)",
     "if (install_word_seen && 0)",
     ["menu"]),

    ("the timer stop drops the completion event again (rc.12)",
     "src/viv_anim.c",
     "os_DeleteTimerQueueTimer(NULL,_viv_timer_queue_timer_handle,INVALID_HANDLE_VALUE);",
     "os_DeleteTimerQueueTimer(NULL,_viv_timer_queue_timer_handle,NULL);",
     ["menu"]),

    ("the config temp name loses its process id again (rc.12)",
     "src/config.c",
     "string_format_number(pid_wbuf,GetCurrentProcessId());",
     "string_format_number(pid_wbuf,4);",
     ["menu"]),

    ("the config temp create truncates again (rc.12)",
     "src/config.c",
     "0,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,0);\r\n\r\n\tif (h == INVALID_HANDLE_VALUE)",
     "0,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,0);\r\n\r\n\tif (h == INVALID_HANDLE_VALUE)",
     ["menu"]),

    ("the bmp header sum rides raw arithmetic again (rc.12)",
     "src/viv_load.c",
     "safe_size_add(safe_size_add(safe_size_add((SIZE_T)bih->biSize,(SIZE_T)mask_size),safe_size_mul((SIZE_T)color_count,4)),pixels_size)",
     "safe_size_add((SIZE_T)bih->biSize + (SIZE_T)mask_size + (SIZE_T)color_count * 4,pixels_size)",
     ["menu"]),

    ("the install probe truncates again (rc.12)",
     "src/viv_install.c",
     "file = CreateFileW(probe_path,GENERIC_WRITE,0,0,CREATE_NEW,FILE_ATTRIBUTE_TEMPORARY,0);\r\n\r\n\tif (file == INVALID_HANDLE_VALUE)",
     "file = CreateFileW(probe_path,GENERIC_WRITE,0,0,CREATE_ALWAYS,FILE_ATTRIBUTE_TEMPORARY,0);\r\n\r\n\tif (file == INVALID_HANDLE_VALUE)",
     ["menu"]),

    ("the help url launch stops naming its refusals again (rc.12)",
     "src/viv_view.c",
     "(INT_PTR)ShellExecuteA(_viv_hwnd,NULL,url,NULL,NULL,SW_SHOWNORMAL) <= 32",
     "(INT_PTR)ShellExecuteA(_viv_hwnd,NULL,url,NULL,NULL,SW_SHOWNORMAL) > 32",
     ["menu"]),

    ("the help url contract loses its prefix check again (rc.12)",
     "src/viv_view.c",
     "memcmp(url,\"https://\",8) == 0",
     "memcmp(url,\"https://\",8) != 0",
     ["menu"]),
]

def run_suite(key):
    try:
        r = subprocess.run(SUITES[key], capture_output=True, text=True,
                           timeout=300)
        return r.returncode
    except subprocess.TimeoutExpired:
        # a suite that wedges past five minutes is not a verdict; the
        # conventional timeout exit answers for it (nonzero, and named).
        print("TIMEOUT %s (over 300s)" % SUITES[key][1])
        return 124
    except OSError as e:
        # a suite that cannot even spawn (a missing interpreter on
        # windows, a drifted path) is the same class of broken meter:
        # named and nonzero, never a traceback that hides the verdict.
        print("SPAWN-FAILURE %s (%s)" % (SUITES[key][1], e))
        return 127


def main():
    # the baseline gate: the audit's finding (and this session's own
    # incident - a killed run left its resident mutation in the tree,
    # and the next run read the poisoned reds as forty-six catches)
    # closed at the source. every suite in the vocabulary runs on
    # the unmutated tree first; anything already red is a broken meter,
    # and the run refuses to start rather than minting caught coins.
    baseline = {k: run_suite(k) for k in SUITES}
    red = sorted(k for k, rc in baseline.items() if rc != 0)
    if red:
        print("BROKEN BASELINE: %s already red on the unmutated tree"
              " (%s) - fix the suite or the tree before asking the"
              " teeth to bite" % (",".join(red),
                                  {k: baseline[k] for k in red}))
        return 1
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
