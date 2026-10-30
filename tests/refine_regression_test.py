#!/usr/bin/env python3
"""Execute production UI decisions with host adapters; never claims Windows GUI coverage."""
import argparse
import json
import math
import os
from pathlib import Path
import re
import shlex
import subprocess
import tempfile

from repair_regression_test import extract
from native_regression_test import block

ROOT = Path(__file__).resolve().parents[1]


def require(condition, message):
    if not condition:
        raise AssertionError(message)


def run_c(code):
    with tempfile.TemporaryDirectory(prefix="viv-refine-") as tmp:
        path = Path(tmp) / "probe.c"
        exe = Path(tmp) / "probe"
        path.write_text(code)
        command = shlex.split(os.environ.get("CC", "cc")) + [
            "-std=c99", "-Wall", "-Wextra", "-Werror",
            "-Wno-unused-function", "-Wno-unused-parameter",
            "-fsanitize=undefined", "-fno-sanitize-recover=all",
            str(path), "-o", str(exe), "-lm"]
        compiled = subprocess.run(command, capture_output=True, text=True)
        require(compiled.returncode == 0, "probe compilation failed:\n" + compiled.stderr)
        ran = subprocess.run([str(exe)], capture_output=True, text=True)
        require(ran.returncode == 0, "production probe failed:\n" + ran.stdout + ran.stderr)
        return ran.stdout


def luminance(color):
    channels = [int(color[i:i + 2], 16) / 255 for i in (1, 3, 5)]
    linear = [c / 12.92 if c <= .04045 else ((c + .055) / 1.055) ** 2.4
              for c in channels]
    return sum(w * c for w, c in zip((.2126, .7152, .0722), linear))


def contrast(a, b):
    x, y = sorted((luminance(a), luminance(b)))
    return (y + .05) / (x + .05)


def lightness(color):
    y = luminance(color)
    return 116 * (y ** (1 / 3) if y > (6 / 29) ** 3 else y / (3 * (6 / 29) ** 2) + 4 / 29) - 16


def theme(root):
    header = (root / "src/viv_theme.h").read_text()
    source = (root / "src/viv_theme.c").read_text()
    source = re.sub(r'^#include[^\n]*', '', source, flags=re.M)
    system = {"COLOR_BTNFACE": "#f0f0f0", "COLOR_MENU": "#f0f0f0",
              "COLOR_3DSHADOW": "#a0a0a0", "COLOR_GRAYTEXT": "#6d6d6d",
              "COLOR_WINDOW": "#ffffff", "COLOR_WINDOWTEXT": "#000000"}
    names = re.findall(r'^\s*(VIV_TK_\w+),', header, re.M)
    definitions = '\n'.join(f'#define {key} {i}' for i, key in enumerate(system))
    cases = '\n'.join(f'case {i}: return RGB(0x{value[1:3]},0x{value[3:5]},0x{value[5:7]});'
                      for i, value in enumerate(system.values()))
    code = r'''
#include <stdio.h>
typedef unsigned long COLORREF;
typedef unsigned long HBRUSH;
typedef unsigned char BYTE;
#define RGB(r,g,b) ((COLORREF)(r) | ((COLORREF)(g)<<8) | ((COLORREF)(b)<<16))
static int dark;
static BYTE config_ui_accent;
static int _viv_is_dark(void) { return dark; }
static HBRUSH CreateSolidBrush(COLORREF color) { return color + 1; }
static void DeleteObject(HBRUSH brush) { (void)brush; }
''' + definitions + '\nstatic COLORREF GetSysColor(int id) { switch(id) {\n' + cases + '\n} return 0; }\n'
    code += header + '\n' + source + r'''
int main(void) {
    int mode, accent, token;
    for(mode=0; mode<2; mode++) {
        dark=mode;
        for(accent=0; accent<VIV_THEME_ACCENT_COUNT; accent++) {
            viv_theme_set_accent(accent);
            for(token=0; token<VIV_TK_COUNT; token++) {
                COLORREF c=viv_theme_color(token);
                printf("%d %d %d #%02lx%02lx%02lx\n", mode,accent,token,c&255,(c>>8)&255,(c>>16)&255);
            }
        }
    }
    return 0;
}
'''
    palettes = {}
    for line in run_c(code).splitlines():
        mode, accent, token, color = line.split()
        palettes.setdefault((int(mode), int(accent)), {})[names[int(token)]] = color
    d, l = palettes[1, 0], palettes[0, 0]
    ratios = {
        "dark_hover_face": contrast(d["VIV_TK_HOVER"], d["VIV_TK_FACE"]),
        "dark_hover_delta_Lstar": abs(lightness(d["VIV_TK_HOVER"]) - lightness(d["VIV_TK_FACE"])),
        "dark_down_hover": contrast(d["VIV_TK_DOWN"], d["VIV_TK_HOVER"]),
        "light_hover_face": contrast(l["VIV_TK_HOVER"], l["VIV_TK_FACE"]),
        "light_hover_nav": contrast(l["VIV_TK_HOVER"], l["VIV_TK_NAV_FACE"]),
        "light_outline_white": contrast(l["VIV_TK_LINE"], "#ffffff"),
        "on_accent_min": min(contrast(p["VIV_TK_ON_ACCENT"], p[t])
                             for p in palettes.values()
                             for t in ("VIV_TK_ACCENT", "VIV_TK_ACCENT_HOT", "VIV_TK_ACCENT_DOWN")),
    }
    require(ratios["dark_hover_face"] >= 1.48, "dark hover contrast")
    require(ratios["dark_hover_delta_Lstar"] >= 12, "dark hover lightness")
    require(ratios["dark_down_hover"] >= 1.15, "distinct dark pressed state")
    require(ratios["light_hover_face"] >= 1.31, "light hover contrast")
    require(ratios["light_outline_white"] >= 2.8, "tray outline on white")
    require(ratios["on_accent_min"] >= 3, "text on every accent state")
    return {"passed": True, "coverage": "production theme C + classic Windows light system-color adapter",
            "ratios": ratios, "dark": d, "light": l, "system_color_fixture": system}


def hold(root):
    source = (root / "src/zoomui.c").read_text()
    constants = '\n'.join(re.findall(r'^#define _ZOOMUI_(?:IDLE_MS|ALPHA_OPAQUE|FADE_MS)\s+\d+', source, re.M))
    code = r'''
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
typedef uint32_t DWORD;
typedef int HWND;
#define SW_HIDE 0
static HWND _zoomui_hwnd=1, capture;
static DWORD clock_ms, _zoomui_last_activity, _zoomui_fade_tick;
static int _zoomui_alpha, _zoomui_alpha_target, _zoomui_layered_ok;
static int autohide, hidden, releases;
static HWND GetCapture(void) { return capture; }
static DWORD GetTickCount(void) { return clock_ms; }
static int _zoomui_autohide_enabled(void) { return autohide; }
static void _zoomui_kill_timer(void) {}
static void _zoomui_kill_poll_timer(void) {}
static void _zoomui_set_alpha(int value) {}
static void _zoomui_clear_hover_press(int value) {}
static void ShowWindow(HWND hwnd,int value) { hidden++; }
static void ReleaseCapture(void) { capture=0; releases++; }
''' + constants + '\n' + extract(root, "zoomui.c", "_zoomui_on_timer") + r'''
int main(void) {
    int layered;
    for(layered=0; layered<=1; layered++) {
        _zoomui_layered_ok=layered; autohide=1; hidden=releases=0;
        _zoomui_alpha=_zoomui_alpha_target=_ZOOMUI_ALPHA_OPAQUE;
        _zoomui_last_activity=_zoomui_fade_tick=0; capture=_zoomui_hwnd;
        for(clock_ms=30; clock_ms<=6000; clock_ms+=30) {
            _zoomui_on_timer();
            assert(!hidden && !releases && _zoomui_alpha==255 && _zoomui_alpha_target==255);
        }
        capture=0; clock_ms=7800; _zoomui_on_timer();
        assert(!hidden);
        for(clock_ms=8010; clock_ms<=8500; clock_ms+=30) _zoomui_on_timer();
        assert(hidden && !releases);
        autohide=0; hidden=0; capture=_zoomui_hwnd;
        _zoomui_alpha=128; clock_ms=20000; _zoomui_on_timer();
        assert(!hidden && _zoomui_alpha==255);
    }
    /* unsigned clock wrap must not turn an active hold into idle. */
    autohide=1; _zoomui_layered_ok=1; capture=_zoomui_hwnd;
    _zoomui_alpha=_zoomui_alpha_target=255; hidden=0;
    _zoomui_last_activity=UINT32_MAX-100; clock_ms=100;
    _zoomui_on_timer(); assert(!hidden && _zoomui_alpha==255);
    puts("capture: 6s holds, release idle, fallback, windowed and clock wrap passed");
    return 0;
}
'''
    return {"passed": True, "coverage": "production timer C + clock/capture adapters", "result": run_c(code).strip()}


def status(root):
    source = (root / "src/viv_chrome.c").read_text()
    enum = re.search(r'enum\s*\{\s*_VIV_STATUS_INDEX,.*?\};', source, re.S)
    require(enum is not None, "semantic pane roles missing")
    cap = re.search(r'^#define _VIV_STATUS_PART_MAX\s+\d+', (root / "src/viv_state.h").read_text(), re.M)
    code = '#include <assert.h>\n#include <stdio.h>\n' + cap.group() + '\n' + enum.group() + '\n'
    code += extract(root, "viv_chrome.c", "_viv_status_layout_parts") + r'''
static void fill(int *w,int index,int preload,int mask) {
    int i;
    for(i=0;i<_VIV_STATUS_ROLE_COUNT;i++) w[i]=0;
    w[_VIV_STATUS_INDEX]=index?90:0; w[_VIV_STATUS_PRELOAD]=preload?128:0;
    w[_VIV_STATUS_ZOOM]=100; w[_VIV_STATUS_DIMENSION]=200;
    w[_VIV_STATUS_POS]=(mask&1)?80:0; w[_VIV_STATUS_RGB]=(mask&2)?90:0;
    w[_VIV_STATUS_FRAME]=(mask&4)?70:0; w[_VIV_STATUS_DATE]=(mask&8)?150:0;
}
int main(void) {
    int w[_VIV_STATUS_ROLE_COUNT], edges[_VIV_STATUS_PART_MAX+1], roles[_VIV_STATUS_PART_MAX+1];
    int index,preload,mask,avail,n,i,cases=0;
    for(index=0;index<2;index++) for(preload=0;preload<2;preload++)
    for(mask=0;mask<16;mask++) for(avail=0;avail<=2048;avail+=7) {
        int previous=0, seen=0;
        fill(w,index,preload,mask); edges[_VIV_STATUS_PART_MAX]=roles[_VIV_STATUS_PART_MAX]=0x12345678;
        n=_viv_status_layout_parts(w,avail,edges,roles);
        assert(n>=3 && n<=_VIV_STATUS_PART_MAX && edges[n-1]==-1);
        assert(edges[_VIV_STATUS_PART_MAX]==0x12345678 && roles[_VIV_STATUS_PART_MAX]==0x12345678);
        for(i=0;i<n;i++) {
            assert(roles[i]>=0 && roles[i]<_VIV_STATUS_ROLE_COUNT);
            assert(!(seen&(1<<roles[i]))); seen|=1<<roles[i];
            if(i) assert(roles[i]>roles[i-1]);
            if(i<n-1) { assert(edges[i]>=previous && edges[i]<=avail); previous=edges[i]; }
        }
        assert(seen&(1<<_VIV_STATUS_MESSAGE)); assert(seen&(1<<_VIV_STATUS_ZOOM));
        assert(seen&(1<<_VIV_STATUS_DIMENSION)); cases++;
    }
    { const int widths[]={690,610,520,450,380};
      const int remaining[]={15,14,12,8,0};
      for(i=0;i<5;i++) {
        int actual;
        fill(w,0,0,15); _viv_status_layout_parts(w,widths[i],edges,roles);
        actual=(w[_VIV_STATUS_POS]?1:0)|(w[_VIV_STATUS_RGB]?2:0)|
               (w[_VIV_STATUS_FRAME]?4:0)|(w[_VIV_STATUS_DATE]?8:0);
        assert(actual==remaining[i]);
      }
    }
    fill(w,1,1,15); n=_viv_status_layout_parts(w,10000,edges,roles);
    assert(n==8 && !w[_VIV_STATUS_POS] && w[_VIV_STATUS_RGB] && w[_VIV_STATUS_FRAME] && w[_VIV_STATUS_DATE]);
    printf("status: %d width/mask cases + priority thresholds + ninth-candidate budget passed\n",cases);
    return 0;
}
'''
    result = run_c(code).strip()
    update = extract(root, "viv_chrome.c", "_viv_status_update")
    frame = extract(root, "viv_chrome.c", "_viv_status_update_frame")
    require('string_get_filename_part(_viv_slot_current.fd.cFileName)' in update, "filename-only pane")
    require('_viv_status_frame_text_at(reserve_buf,_viv_slot_current.frame_count)' in update, "GIF ceiling measure")
    require('SB_SETPARTS' not in frame and '_viv_status_set(_viv_status_frame_pane,frame_buf)' in frame, "frame fast path")
    for anchor in ('_viv_status_temp_text', 'LOCALIZATION_ID_STATUS_BAR_LOADING', '_viv_status_failure_line',
                   'LOCALIZATION_ID_STATUS_BAR_SLIDESHOW_PLAYING', 'LOCALIZATION_ID_STATUS_BAR_RENDERER_FALLBACK',
                   'LOCALIZATION_ID_STATUS_BAR_PRELOAD'):
        require(anchor in update, "status message chain: " + anchor)
    for name in ("viv_dialogs.c", "viv_wndproc.c"):
        require('_viv_status_zoom_pane_index()' in (root / 'src' / name).read_text(), "live zoom hit target")
    return {"passed": True, "coverage": "production layout C + source wiring, not native status-control paint", "result": result}


def decimal(root):
    update = extract(root, "viv_chrome.c", "_viv_status_update")
    start = update.index('if (string_get_length(widebuf) < (STRING_SIZE - 2))')
    brace = update.index('{', start)
    production = update[start:brace] + block(update, brace)
    code = r'''
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <wchar.h>
typedef unsigned long DWORD;
#define STRING_SIZE 1024
''' + extract(root, "string.c", "string_get_length") + '\n'
    code += 'static void append(wchar_t *widebuf,DWORD frac) {\n' + production + '\n}\n' + r'''
int main(void) {
    wchar_t buf[STRING_SIZE+16];
    const int lengths[]={0,1,3,STRING_SIZE-3,STRING_SIZE-2,STRING_SIZE-1};
    unsigned j;
    for(j=0;j<sizeof(lengths)/sizeof(lengths[0]);j++) {
        int i, length=lengths[j];
        for(i=0;i<STRING_SIZE+16;i++) buf[i]=L'Q';
        buf[length]=0; buf[STRING_SIZE+8]=0;
        append(buf,7);
        if(length<STRING_SIZE-2) {
            assert(buf[length]==L'7' && buf[length+1]==0);
            assert(string_get_length(buf)==(uintptr_t)length+1);
        } else assert(buf[length]==0);
        assert(buf[STRING_SIZE]==L'Q');
    }
    puts("decimal: poisoned tails, exact terminator and buffer limits passed");
    return 0;
}
'''
    return {"passed": True, "coverage": "production decimal append block + production string length", "result": run_c(code).strip()}


def typography(root):
    header = (root / 'src/viv_chrome.h').read_text()
    roles = re.search(r'enum\s*\{\s*VIV_UI_FONT_TOOLBAR,.*?\};', header, re.S)
    require(roles is not None, 'font roles')
    source = (root / 'src/viv_chrome.c').read_text()
    constants = '\n'.join(re.findall(r'^#define VIV_UI_\w+\s+\d+', (root / 'src/viv_theme.h').read_text(), re.M))
    constants += '\n' + re.search(r'^#define VIV_UI_SCALE[^\n]+', (root / 'src/viv_theme.h').read_text(), re.M).group()
    code = r'''
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>
typedef int HFONT;
typedef int HGDIOBJ;
typedef int HDC;
typedef struct { int left,top,right,bottom; } RECT;
typedef struct { int tmHeight,tmAscent; } TEXTMETRICW;
typedef struct { int lfHeight,lfWidth,lfWeight,lfCharSet,lfPitchAndFamily; wchar_t lfFaceName[32]; } LOGFONTW;
#define FW_NORMAL 400
#define FW_SEMIBOLD 600
#define DEFAULT_CHARSET 1
#define FIXED_PITCH 1
#define FF_MODERN 48
#define DEFAULT_GUI_FONT 17
static void os_copy_memory(void *dest,const void *src,size_t count) { memcpy(dest,src,count); }
static int os_logical_wide=96,os_logical_high=96;
static int created=1,alive,selected=1,allow_font=1;
static LOGFONTW fonts[256];
static int live[256];
static int MulDiv(int a,int b,int c) { return (a*b+c/2)/c; }
static int os_menu_font(LOGFONTW *lf) { memset(lf,0,sizeof(*lf)); wcscpy(lf->lfFaceName,L"system"); return allow_font; }
static HFONT CreateFontIndirectW(const LOGFONTW *lf) { assert(created<255); fonts[++created]=*lf; live[created]=1; alive++; return created; }
static void DeleteObject(HFONT f) { assert(f>1 && live[f]); live[f]=0; alive--; }
static HGDIOBJ GetStockObject(int id) { return 1; }
static HGDIOBJ SelectObject(HDC dc,HGDIOBJ f) { int old=selected; assert(f==1 || live[f]); selected=f; return old; }
static int GetTextMetricsW(HDC dc,TEXTMETRICW *tm) { int height=-fonts[selected].lfHeight; tm->tmHeight=height+3; tm->tmAscent=height; return 1; }
''' + constants + '\n' + roles.group() + '\n'
    # The declarations and the real cache/drop functions are used verbatim.
    for name in ('_viv_menu_font_handle', '_viv_menu_font_dpi', '_viv_ui_fonts', '_viv_ui_font_dpi', '_viv_ui_font_dpi_y'):
        match = re.search(r'^static (?:HFONT|int) ' + name + r'(?:\[[^\]]+\])?(?: = [^;]+)?;', source, re.M)
        require(match is not None, 'cache declaration: ' + name)
        code += match.group() + '\n'
    for name in ('_viv_menu_font', '_viv_ui_fonts_drop', '_viv_ui_font', '_viv_ui_text_rect', '_viv_menu_font_drop'):
        code += extract(root, 'viv_chrome.c', name) + '\n'
    code += r'''
int main(void) {
    const int dpis[]={96,120,144,192,216,288};
    unsigned i;
    int role,menu,font,cache,baseline=-1;
    for(i=0;i<sizeof(dpis)/sizeof(dpis[0]);i++) {
        os_logical_wide=os_logical_high=dpis[i];
        menu=_viv_menu_font();
        assert(fonts[menu].lfHeight==-MulDiv(26,dpis[i],192) && fonts[menu].lfWeight==600);
        for(role=0;role<VIV_UI_FONT_COUNT;role++) {
            int dip2=role==VIV_UI_FONT_TOOLBAR?24:(role==VIV_UI_FONT_PERCENT?32:23);
            font=_viv_ui_font(role); cache=created;
            assert(fonts[font].lfHeight==-MulDiv(dip2,dpis[i],192) && fonts[font].lfWeight==400);
            assert(_viv_ui_font(role)==font && created==cache);
            if(role==VIV_UI_FONT_NUMBER || role==VIV_UI_FONT_PERCENT)
                assert(fonts[font].lfPitchAndFamily==(FIXED_PITCH|FF_MODERN));
        }
        assert(alive==VIV_UI_FONT_COUNT+1);
        baseline=-1;
        for(role=VIV_UI_FONT_STATUS;role<=VIV_UI_FONT_PERCENT;role++) {
            RECT rect={0,100,200,100+MulDiv(26,dpis[i],96)};
            TEXTMETRICW tm;
            selected=_viv_ui_font(role); GetTextMetricsW(0,&tm);
            _viv_ui_text_rect(0,&rect);
            if(baseline<0) baseline=rect.top+tm.tmAscent;
            assert(rect.top+tm.tmAscent==baseline);
        }
        selected=1; _viv_menu_font_drop(); assert(alive==0);
    }
    allow_font=0;
    assert(_viv_ui_font(VIV_UI_FONT_STATUS)==1 && !alive);
    assert(!_viv_ui_font(-1) && !_viv_ui_font(VIV_UI_FONT_COUNT));
    _viv_menu_font_drop(); assert(!alive);
    puts("typography: 96/120/144/192/216/288 DPI, font hierarchy, cache lifetimes, fixed pitch and shared baseline passed");
    return 0;
}
'''
    result = run_c(code).strip()
    for name in ('viv_toolbar.c', 'viv_menubar.c', 'viv_msgbox.c', 'viv_settings.c', 'zoomui.c'):
        require('VIV_UI_RADIUS' in (root / 'src' / name).read_text(), 'shared radius: ' + name)
    return {"passed": True, "coverage": "production font/baseline C with GDI metric adapters; real font rasterization pending", "result": result}


def corners(root):
    source = (root / 'src/zoomui.c').read_text()
    header = (root / 'src/viv_theme.h').read_text()
    constants = re.search(r'^#define VIV_UI_RADIUS[^\n]+', header, re.M).group() + '\n'
    constants += re.search(r'^#define VIV_UI_SCALE[^\n]+', header, re.M).group() + '\n'
    constants += '\n'.join(re.findall(r'^#define _ZOOMUI_GDIP_\w+\s+\d+', source, re.M)) + '\n'
    code = r'''
#include <assert.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
typedef uint32_t COLORREF;
typedef struct { int left,top,right,bottom; } RECT;
static int os_logical_high=96,arcs,lines,paths,brushes,pens,fills,strokes;
static float diameter;
static int os_GdipCreatePath(int mode,void **out) { *out=(void *)1; paths++; return 0; }
static void os_GdipAddPathLine(void *path,float x,float y,float a,float b) { lines++; }
static void os_GdipAddPathArc(void *path,float x,float y,float w,float h,float start,float sweep) {
    assert(fabsf(w-h)<.001f && w>=2 && sweep==90);
    if(arcs) assert(fabsf(w-diameter)<.001f);
    diameter=w; arcs++;
}
static void os_GdipClosePathFigure(void *path) {}
static int os_GdipCreateSolidFill(unsigned color,void **out) { *out=(void *)1; brushes++; return 0; }
static void os_GdipFillPath(void *graphics,void *brush,void *path) { fills++; }
static void os_GdipDeleteBrush(void *brush) { brushes--; }
static int os_GdipCreatePen1(unsigned color,float width,int unit,void **out) { *out=(void *)1; pens++; return 0; }
static void os_GdipDrawPath(void *graphics,void *pen,void *path) { strokes++; }
static void os_GdipDeletePen(void *pen) { pens--; }
static void os_GdipDeletePath(void *path) { paths--; }
''' + constants + extract(root, 'zoomui.c', '_zoomui_argb') + '\n'
    code += extract(root, 'zoomui.c', '_zoomui_gdip_stadium') + r'''
int main(void) {
    const int dpis[]={96,120,144,192,216,288};
    const RECT rects[]={{0,0,340,50},{10,20,18,30}};
    unsigned i,j;
    for(i=0;i<sizeof(dpis)/sizeof(dpis[0]);i++) for(j=0;j<2;j++) {
        float expected;
        os_logical_high=dpis[i]; arcs=lines=fills=strokes=0;
        _zoomui_gdip_stadium(0,&rects[j],0x112233,0x445566);
        expected=VIV_UI_SCALE(VIV_UI_RADIUS,dpis[i],96);
        if(expected>(rects[j].bottom-rects[j].top)/2.0f) expected=(rects[j].bottom-rects[j].top)/2.0f;
        if(expected>(rects[j].right-rects[j].left)/2.0f) expected=(rects[j].right-rects[j].left)/2.0f;
        assert(arcs==4 && lines==4 && fills==1 && strokes==1);
        assert(fabsf(diameter-expected*2)<.001f && !paths && !brushes && !pens);
    }
    puts("corners: shared radius, four quarter-arcs, small bounds and GDI+ cleanup at six DPIs passed");
    return 0;
}
'''
    return {"passed": True, "coverage": "production rounded path C with GDI+ call adapters; raster pixels pending", "result": run_c(code).strip()}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source-root", type=Path, default=ROOT)
    parser.add_argument("--out", type=Path)
    parser.add_argument("suites", nargs="*", choices=("theme", "hold", "status", "decimal", "typography", "corners"))
    args = parser.parse_args()
    suites = {"theme": theme, "hold": hold, "status": status, "decimal": decimal, "typography": typography, "corners": corners}
    result = {name: suites[name](args.source_root) for name in args.suites or suites}
    output = json.dumps(result, ensure_ascii=False, indent=2)
    if args.out:
        args.out.parent.mkdir(parents=True, exist_ok=True)
        args.out.write_text(output + '\n')
    print(output)


if __name__ == "__main__":
    main()
