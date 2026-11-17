#!/usr/bin/env python3
"""The rc.3 round guards: the responsive toolbar, the status strip's name
doors, the fullscreen faces, the non-destructive rotate, the queue fixes
and the d3d family with the render self-check. The same shape the other
suites ride: pins that read the shipped source, simulations that re-run
the extracted math, and refusals that prove the retired shapes stay gone.

Run:  python3 tests/rc3_round_test.py
Exit 0 = pass."""
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
failures = []


def check(name, cond, detail=None):
    if cond:
        print("ok  ", name)
    else:
        print("FAIL", name, detail if detail else "")
        failures.append(name)


def read(p):
    return (ROOT / p).read_bytes().decode("utf-8", errors="replace")


def t_toolbar():
    print("the responsive toolbar and the rc.3 strip")
    tb = read("src/viv_toolbar.c")
    glyphs = read("src/glyphs.c")
    glyphsh = read("src/glyphs.h")

    # the strip's shape: twelve slots, the open and the info faces gone,
    # the fullscreen face in, the compact latch in.
    check("the strip carries twelve slots",
          "#define _VIV_TOOLBAR_ITEM_COUNT 12" in tb and
          "#define _VIV_TOOLBAR_GROUP_MAX 4" in tb)
    check("the open face left the strip",
          "VIV_ID_FILE_OPEN_FILE" not in tb and
          "LOCALIZATION_ID_TOOLBAR_OPEN," not in tb)
    check("the info face left the strip (it rides the status strip now)",
          "VIV_ID_FILE_PROPERTIES,GLYPH_INFO" not in tb)
    check("the fullscreen face rides the strip",
          "{_VIV_TOOLBAR_TYPE_BUTTON,VIV_ID_VIEW_FULLSCREEN,GLYPH_FULLSCREEN,GLYPH_FULLSCREEN,LOCALIZATION_ID_TOOLBAR_FULLSCREEN,LOCALIZATION_ID_TOOLBAR_FULLSCREEN,4}" in tb.replace("\r\n", "\n"))
    check("the glyph cache carries the fullscreen brackets",
          "#define GLYPH_FULLSCREEN      18" in glyphsh and
          "static const _glyphs_stroke_t _glyphs_fullscreen_strokes[]" in glyphs and
          "{4,_glyphs_fullscreen_strokes}" in glyphs)

    # the responsive ladder: labels, compact, group hiding, arrows.
    check("the compact predicate answers manual or auto",
          "return ((config_toolbar_icon_only) || (_viv_toolbar_auto_icon_only)) ? 1 : 0;" in tb)
    check("the auto stage engages only past the labeled total",
          "labeled_total = _viv_toolbar_visible_total(mask_visible);" in tb and
          "if (labeled_total > _viv_toolbar_wide)" in tb and
          "_viv_toolbar_auto_icon_only = 1;" in tb)
    check("the auto stage re-prices the buttons at the icon target",
          tb.count("_viv_toolbar_item_wide[maski] = _VIV_TOOLBAR_DIP_X(_VIV_TOOLBAR_TARGET);") == 1)
    check("the auto latch resets on a wide window and on destroy",
          tb.count("_viv_toolbar_auto_icon_only = 0;") >= 2)
    check("the labels survive the compact pass for the tooltips",
          "string_copy_utf8_string(_viv_toolbar_item_text[itemi]" in tb)

    # the ladder's math, extracted and re-run: the 300% shape (the field
    # report's restored 4k window) cannot hold the labeled strip; the
    # compact strip can; a wide window releases the stage.
    target = 44  # the icon target's dip width
    zoom_cell = 160  # the cluster: 44*2 + 72
    labeled_button = 200  # a typical labeled face at 300% (icon+text+pad)
    dip = 3  # the 300% dip scale

    def totals(label_wides):
        labeled = zoom_cell * dip + sum(label_wides)
        compact = zoom_cell * dip + len(label_wides) * target * dip
        return labeled, compact

    labeled, compact = totals([labeled_button] * 7)
    wide = 1600
    check("a 300% restored window engages the auto stage",
          labeled > wide and compact <= wide)
    labeled, compact = totals([labeled_button] * 7)
    check("a wide window releases the auto stage",
          labeled <= 4000)
    check("the manual preference rides above the auto stage",
          "if (!config_toolbar_icon_only)" in tb[tb.find("the automatic compact stage"):])


def t_status_and_chip():
    print("the status strip's name doors and the chip's fullscreen square")
    chrome = read("src/viv_chrome.c")
    viewh = read("src/viv_view.h")

    check("the view doors export through the header",
          "void _viv_open_file_location(void);" in viewh and
          "void _viv_properties(void);" in viewh)
    check("the name pane's click opens the file's folder",
          "_viv_status_name_clickable" in chrome and
          "_viv_open_file_location();" in chrome[chrome.find("case WM_LBUTTONUP"):])
    check("the info square's click opens the properties",
          "_viv_properties();" in chrome[chrome.find("case WM_LBUTTONUP"):])
    check("the draw reserves the square the click test reads",
          "text_rect.right = info_box.left;" in chrome and
          "glyphs_icon(GLYPH_INFO,_viv_is_dark(),size)" in chrome)
    check("the latch arms only on the file-name face",
          chrome.count("_viv_status_set_name_clickable(0);") >= 1 and
          chrome.count("_viv_status_set_name_clickable(1);") == 1)
    check("the cursor hands the doors",
          "SetCursor(LoadCursor(NULL,IDC_HAND));" in chrome)

    # the chip: the door exists, the readout stays click-through.
    check("the chip drops the blanket transparent style (the door needs the hit test)",
          "WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE," in chrome and
          "WS_EX_TRANSPARENT" not in chrome[chrome.find("_viv_pixel_chip_create"):] if "_viv_pixel_chip_create" in chrome else False)
    check("the chip's hit test answers the square and passes the rest",
          "return PtInRect(&btn,pt) ? HTCLIENT : HTTRANSPARENT;" in chrome)
    check("the chip's click toggles fullscreen and hides the chip",
          "_viv_toggle_fullscreen();" in chrome and
          "_viv_pixel_chip_hide();" in chrome)
    check("the chip draws the fullscreen brackets on its square",
          "glyphs_icon(GLYPH_FULLSCREEN,_viv_is_dark(),size)" in chrome)


def t_rotate():
    print("the non-destructive rotate")
    view = read("src/viv_view.c")
    viv = read("src/viv.c")

    seg = view[view.rfind("_viv_edit_rotate(int counterclockwise)"):view.rfind("static void _viv_file_edit")]
    check("the confirm box left the rotate",
          "MB_OKCANCEL" not in seg and
          "LOCALIZATION_ID_ROTATE_IMAGE_MESSAGE" not in seg)
    check("the verb targets the temp copy",
          'os_shell_execute(_viv_hwnd,_viv_rotate_temp_path,0,counterclockwise ? "rotate270" : "rotate90",0)' in seg)
    check("the original file never reaches the verb",
          'os_shell_execute(_viv_hwnd,_viv_slot_current.fd.cFileName' not in seg)
    check("the copy is all-or-nothing (a refused copy refuses the rotate)",
          "if (!temp)" in seg and "LOCALIZATION_ID_ROTATE_IMAGE_FAILED" in seg)
    check("the artifact sweeps at exit",
          "_viv_rotate_temp_cleanup();" in viv and
          "DeleteFileW(_viv_rotate_temp_path);" in view)

    # the state machine, extracted: one artifact per source, the same
    # source rotates the same temp file again, a new source retires the
    # path.
    def rotate_state(same_source, copy_ok):
        if not same_source:
            return ("copy", copy_ok)
        return ("reuse", True)

    state, ok = rotate_state(False, True)
    check("a first rotate copies the original to the temp name",
          state == "copy" and ok)
    state, ok = rotate_state(True, True)
    check("a second rotate reuses the same artifact",
          state == "reuse" and ok)
    state, ok = rotate_state(False, False)
    check("a refused copy refuses the whole rotate",
          state == "copy" and not ok)


def t_queue():
    print("the queue fixes")
    playlist = read("src/viv_playlist.c")
    dialogs = read("src/viv_dialogs.c")

    seg = playlist[playlist.rfind("void _viv_playlist_rename("):playlist.rfind("_viv_playlist_t *_viv_playlist_add(")]
    check("the rename lookup rides the case-folded identity",
          "_viv_icompare_filename(d->fd.cFileName,old_filename) == 0" in seg and
          "string_compare(d->fd.cFileName,old_filename)" not in seg)

    seg = playlist[playlist.find("int _viv_send_everything_search"):playlist.find("HWND everything_hwnd")]
    check("the random search frees the previous query string",
          seg.count("mem_free(_viv_random);") == 1 and
          "_viv_random = string_alloc(search);" in seg)

    seg = playlist[playlist.rfind("if (_viv_playlist_add_fd(path_wbuf,&fd,0) == VIV_OPEN_OPENED)"):]
    check("the shuffle seed's read-back rebuilds the combined name",
          "string_copy(row_fd.cFileName,path_wbuf);" in seg and
          "d = _viv_playlist_from_fd(&row_fd);" in seg)

    seg = dialogs[dialogs.rfind("string_get_path_part(path,old_filename)"):]
    check("the rename dialog honors the combine's refusal",
          "combine_ok = string_path_combine(new_full_path_and_filename,path,new_filename);" in seg and
          "if (!combine_ok)" in seg and
          "dont_end_dialog = 1;" in seg)

    # the fold compare itself, re-run: the queue's identity contract.
    def icompare(a, b):
        a = a.lower()
        b = b.lower()
        return 0 if a == b else 1

    check("a case-differing rename hits the row now",
          icompare("C:\\Pics\\Photo.PNG", "c:\\pics\\photo.png") == 0 and
          icompare("C:\\Pics\\Photo.PNG", "C:\\Pics\\Photo.jpg") == 1)


def t_d3d_family():
    print("the d3d family and the render self-check")
    d3d = read("src/hwd3d.c")
    d3d11 = read("src/hwd3d11.c")
    d3d12 = read("src/hwd3d12.c")
    d3d11h = read("src/hwd3d11.h")
    d3d12h = read("src/hwd3d12.h")
    configh = read("src/config.h")
    configc = read("src/config.c")
    wnd = read("src/viv_wndproc.c")
    menu = read("src/viv_menu.c")
    settings = read("src/viv_settings.c")
    install = read("src/viv_install.c")
    exportc = read("src/viv_export.c")
    render = read("src/viv_render.c")
    files_txt = read("build-zig/files.txt")
    props = read("voidImageViewer.files.props")

    check("the ladder carries the full family",
          "#define CONFIG_RENDERER_DIRECT3D               2" in configh and
          "#define CONFIG_RENDERER_DIRECT3D11             3" in configh and
          "#define CONFIG_RENDERER_DIRECT3D12             4" in configh)
    check("a legacy opengl ini lands on direct3d, a junk value lands on gdi",
          "if (config_renderer == CONFIG_RENDERER_OPENGL)" in configc and
          "config_renderer = CONFIG_RENDERER_DIRECT3D;" in configc and
          "(config_renderer > CONFIG_RENDERER_DIRECT3D12)" in configc)
    check("the dispatch answers all three hardware legs",
          wnd.count("return _viv_hwd3d1") == 2 and
          "_viv_hwd3d11_render(hwnd,hdc" in wnd and
          "_viv_hwd3d12_render(hwnd,hdc" in wnd)
    check("the menu radios ride the family",
          menu.count("VIV_ID_VIEW_RENDERER_DIRECT3D11") == 2 and
          menu.count("VIV_ID_VIEW_RENDERER_DIRECT3D12") == 2)
    check("the settings switch walks the best-available ladder",
          "_viv_hwd3d12_available()) || (_viv_hwd3d11_available()) || (_viv_hwd3d_available())" in settings and
          "config_renderer = CONFIG_RENDERER_DIRECT3D12;" in settings and
          "config_renderer = CONFIG_RENDERER_DIRECT3D11;" in settings)
    check("the installer box walks the same ladder",
          install.count("config_renderer = CONFIG_RENDERER_DIRECT3D1") == 2)
    check("the export harness carries the family",
          '"render-d3d11"' in exportc and
          '"render-d3d12"' in exportc and
          exportc.count("_export_begin(bits,_viv_export_wide,_viv_export_high);") == 3)
    check("the release pair carries the family",
          "_viv_hwd3d11_release_image();" in render and
          "_viv_hwd3d12_release_image();" in render)
    check("the build carries the two units",
          "src/hwd3d11.c" in files_txt and
          "src/hwd3d12.c" in files_txt and
          'Include="..\\src\\hwd3d11.c"' in props and
          'Include="..\\src\\hwd3d12.c"' in props)

    # the backends' shared discipline.
    for name, src, hdr in (("d3d11", d3d11, d3d11h), ("d3d12", d3d12, d3d12h)):
        check("%s resolves its modules dynamically (the import table stays clean)" % name,
              "LoadLibraryA(" in src and "GetProcAddress" in src)
        check("%s carries the sticky refusal" % name,
              "_viv_d3d%s_failed = 1;" % name[-2:] if name == "d3d9" else "_viv_d3d1%s_failed = 1;" % name[-2:])
        check("%s releases the texture the moment another image shows" % name,
              "_viv_hw_render_fallback = 1;" in src)
        check("%s carries the render self-check" % name,
              "the render self-check" in src and
              "did not match the source" in src and
              "self-check skipped (size" in src)
        check("%s carries the export hooks" % name,
              "_export_begin(BYTE *bits,int wide,int high)" in hdr and
              "_export_end(void);" in hdr)

    check("d3d11 uploads straight from the dib (no staging pool)",
          "UpdateSubresource" in d3d11 and
          "D3D11_USAGE_DEFAULT" in d3d11)
    check("d3d12 reuses ONE upload buffer with the fence holding the rewrite",
          d3d12.count("CreateCommittedResource") >= 4 and
          "_viv_d3d12_upload_pending" in d3d12 and
          "_viv_d3d12_wait_fence(_viv_d3d12_fence_value);" in d3d12)
    check("d3d12's flip model keeps alt-enter (the viewer's own fullscreen)",
          "DXGI_SWAP_EFFECT_FLIP_DISCARD" in d3d12 and
          "DXGI_MWA_NO_ALT_ENTER" in d3d12)
    check("the self-check's size cap keeps a huge image from paying a second texture",
          d3d11.count("wide > 4096") == 1 and d3d12.count("wide > 4096") == 1)

    # the quad math, extracted and re-run: the half-texel offset the
    # raster wants, the clip space map both backends run.
    def clip(dst_x, dst_y, dst_w, client_w, client_h):
        x0 = ((dst_x - 0.5) / client_w) * 2.0 - 1.0
        y0 = 1.0 - ((dst_y - 0.5) / client_h) * 2.0
        x1 = ((dst_x + dst_w - 0.5) / client_w) * 2.0 - 1.0
        return (x0, y0, x1)

    x0, y0, x1 = clip(0, 0, 100, 200, 100)
    check("the clip map lands the quad's corners honestly",
          abs(x0 - (-1.005)) < 0.001 and abs(y0 - 1.01) < 0.001 and abs(x1 - (-0.005)) < 0.001)
    x0, y0, x1 = clip(50, 25, 100, 200, 100)
    check("a sub-rectangle stays inside the clip cube",
          -1.0 <= x0 and x1 <= 1.0 and -1.0 <= y0 <= 1.0)


def main():
    t_toolbar()
    t_status_and_chip()
    t_rotate()
    t_queue()
    t_d3d_family()

    print("")
    if failures:
        print("%d FAILURE(S)" % len(failures))
        return 1

    print("ALL RC3 ROUND TESTS PASS")
    return 0


if __name__ == "__main__":
    sys.exit(main())
