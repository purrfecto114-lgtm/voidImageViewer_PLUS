#!/usr/bin/env python3
"""Run RC3 production layout and queue decisions with host C and UBSan."""
import re
import sys

from refine_regression_test import ROOT, extract, run_c


def toolbar():
    source = (ROOT / "src/viv_toolbar.c").read_text()
    table = re.search(r"static const _viv_toolbar_item_t _viv_toolbar_items.*?\n\};", source, re.S)[0]
    types = re.search(r"typedef struct _viv_toolbar_item_s.*?_viv_toolbar_item_t;", source, re.S)[0]
    constants = dict(re.findall(r"(?m)^#define (_VIV_TOOLBAR_(?:ITEM_COUNT|ITEM_ZOOM|ITEM_FULLSCREEN|GROUP_MAX|TARGET|TYPE_[A-Z]+))\s+(\d+)", source))
    symbols = set(re.findall(r"\b(?:VIV_ID_|GLYPH_|LOCALIZATION_ID_)\w+", table))
    symbols.update(("VIV_ID_VIEW_FULLSCREEN", "VIV_ID_NAV_PREV", "VIV_ID_NAV_NEXT"))
    stub = '''
#include <assert.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
typedef unsigned char BYTE;
typedef int localization_id_t;
static int dpi=300;
#define _VIV_TOOLBAR_DIP_X(x) (((x) * dpi) / 96)
'''
    stub += "\n".join(f"#define {k} {v}" for k, v in constants.items()) + "\n"
    stub += "enum {" + ", ".join(sorted(symbols)) + "};\n" + types + "\n" + table
    stub += '''
int config_toolbar_groups = 0x3f, config_toolbar_icon_only = 0;
static int _viv_toolbar_wide, _viv_toolbar_button_gap = 12;
static int _viv_toolbar_item_x[_VIV_TOOLBAR_ITEM_COUNT];
static int _viv_toolbar_item_wide[_VIV_TOOLBAR_ITEM_COUNT];
static int _viv_toolbar_item_label_wide[_VIV_TOOLBAR_ITEM_COUNT];
static BYTE _viv_toolbar_item_visible[_VIV_TOOLBAR_ITEM_COUNT];
int _viv_toolbar_more_wide, _viv_toolbar_more_x, _viv_toolbar_compact;
static BYTE _viv_toolbar_item_overflow[_VIV_TOOLBAR_ITEM_COUNT];
'''
    helpers = ("_viv_toolbar_visible_total", "_viv_toolbar_walk_place", "_viv_toolbar_walk")
    bodies = "\n".join(extract(ROOT, "viv_toolbar.c", name) for name in helpers if name + "(" in source)
    result = run_c(stub + "\n" + bodies + r'''
static int shown(int command)
{
    int i;
    for (i = 0; i < _VIV_TOOLBAR_ITEM_COUNT; i++)
        if (_viv_toolbar_items[i].command_id == command) return _viv_toolbar_item_visible[i];
    return 0;
}
static void trace_layout(void)
{
    int i;
    if (!getenv("VIV_RC3_LAYOUT_TRACE")) return;
    printf("layout,%d,%d,%d,%d\n",dpi,_viv_toolbar_wide,_viv_toolbar_more_x,_viv_toolbar_more_wide);
    for (i=0; i<_VIV_TOOLBAR_ITEM_COUNT; i++) {
        if (_viv_toolbar_item_visible[i] || _viv_toolbar_item_overflow[i]) {
            printf("item,%d,%d,%d,%d\n",i,_viv_toolbar_item_x[i],
                _viv_toolbar_item_wide[i],_viv_toolbar_item_visible[i]);
        }
    }
}
int main(void)
{
    const int dpis[]={96,144,192,288,300,384};
    int i, right, d, width, group_case;
    for (i=0; i<_VIV_TOOLBAR_ITEM_COUNT; i++) {
        const int type = _viv_toolbar_items[i].type;
        _viv_toolbar_item_label_wide[i] = _viv_toolbar_item_wide[i] =
            type == _VIV_TOOLBAR_TYPE_SEP ? 20 : type == _VIV_TOOLBAR_TYPE_ZOOM ? 500 : 295;
    }
    /* 2304 physical px restored on a 4K screen, 864 px reserved for menu roots. */
    _viv_toolbar_wide = 1440;
    _viv_toolbar_walk();
    for (i=0; i<_VIV_TOOLBAR_ITEM_COUNT; i++) {
        if (_viv_toolbar_items[i].type != _VIV_TOOLBAR_TYPE_SEP)
            assert(_viv_toolbar_item_visible[i] && "restored 300 DPI window lost a command");
    }
    assert(shown(VIV_ID_VIEW_FULLSCREEN));
    trace_layout();
    _viv_toolbar_wide = 550;
    _viv_toolbar_walk();
    assert(shown(VIV_ID_VIEW_FULLSCREEN) && shown(VIV_ID_NAV_NEXT) && shown(VIV_ID_NAV_PREV));
    assert(_viv_toolbar_more_wide > 0 && "hidden commands need a reachable popup");
    for (i=0, right=0; i<_VIV_TOOLBAR_ITEM_COUNT; i++) {
        if (_viv_toolbar_item_visible[i]) {
            assert(_viv_toolbar_item_x[i] >= right);
            right = _viv_toolbar_item_x[i] + _viv_toolbar_item_wide[i];
        }
        if (_viv_toolbar_items[i].type != _VIV_TOOLBAR_TYPE_SEP)
            assert(_viv_toolbar_item_visible[i] + _viv_toolbar_item_overflow[i] == 1);
    }
    assert(_viv_toolbar_more_x >= right &&
           _viv_toolbar_more_x + _viv_toolbar_more_wide <= _viv_toolbar_wide);
    trace_layout();
    config_toolbar_groups &= ~(1 << 4);
    _viv_toolbar_walk();
    for (i=0; i<_VIV_TOOLBAR_ITEM_COUNT; i++)
        if (_viv_toolbar_items[i].group == 4)
            assert(!_viv_toolbar_item_visible[i] && !_viv_toolbar_item_overflow[i]);
    for (d=0; d<(int)(sizeof(dpis)/sizeof(dpis[0])); d++) {
        dpi=dpis[d];
        for (group_case=0; group_case<4; group_case++) {
            config_toolbar_groups = group_case & 1 ? 0x3f : 0x1a;
            config_toolbar_icon_only = group_case & 2;
            for (i=0; i<_VIV_TOOLBAR_ITEM_COUNT; i++) {
                _viv_toolbar_item_label_wide[i] = _VIV_TOOLBAR_DIP_X(
                    _viv_toolbar_items[i].type == _VIV_TOOLBAR_TYPE_SEP ? 9 :
                    _viv_toolbar_items[i].type == _VIV_TOOLBAR_TYPE_ZOOM ? 160 : 110);
            }
            for (width=1; width<=_VIV_TOOLBAR_DIP_X(850); width+=7) {
                _viv_toolbar_wide=width;
                _viv_toolbar_walk();
                for (i=0,right=0; i<_VIV_TOOLBAR_ITEM_COUNT;i++) {
                    if (_viv_toolbar_item_visible[i]) {
                        assert(_viv_toolbar_item_x[i]>=right);
                        right=_viv_toolbar_item_x[i]+_viv_toolbar_item_wide[i];
                    }
                    if (_viv_toolbar_item_overflow[i]) assert(_viv_toolbar_more_wide>0);
                }
                assert(right<=width);
                if (_viv_toolbar_more_wide) assert(right<=_viv_toolbar_more_x);
            }
        }
    }
    puts("restored 4K layout and narrow overflow across 6 DPI levels PASS");
    return 0;
}
''').strip()
    return result


def queue():
    body = extract(ROOT, "viv_view.c", "_viv_nav_neighbor_available")
    return run_c(r'''
#include <assert.h>
#include <stdio.h>
#include <wchar.h>
typedef struct { wchar_t cFileName[80]; int id; } WIN32_FIND_DATA;
typedef struct { WIN32_FIND_DATA fd; } entry;
static entry row = {{L"C:\\photos\\queued.png", 7}};
static entry *_viv_playlist_start = &row;
static WIN32_FIND_DATA cursor, *_viv_current_fd = &cursor;
static int _viv_random, _viv_file_not_found, _viv_load_failed, _viv_nav_folder_neighbor;
static int _viv_playlist_count = 1;
static int _viv_fd_compare(const WIN32_FIND_DATA *a, const WIN32_FIND_DATA *b)
{ return wcscmp(a->cFileName,b->cFileName) != 0 || a->id != b->id; }
static int _viv_icompare_filename(const wchar_t *a, const wchar_t *b)
{ return wcscmp(a,b); }
''' + body + r'''
int main(void)
{
    assert(_viv_nav_neighbor_available() && "a queued file is reachable from an empty viewer");
    wcscpy(cursor.cFileName,L"C:\\photos\\missing.png");
    _viv_file_not_found = 1;
    assert(_viv_nav_neighbor_available() && "a broken file must not disable the queue");
    wcscpy(cursor.cFileName,row.fd.cFileName);
    _viv_file_not_found = 0;
    assert(!_viv_nav_neighbor_available() && "the queue ID cannot make the same path a neighbor");
    _viv_playlist_start = 0;
    assert(!_viv_nav_neighbor_available());
    puts("queue availability from blank and failed images, and path identity PASS");
    return 0;
}
''').strip()



def queued_load():
    body = extract(ROOT, "viv_load.c", "_viv_open")
    return run_c(r'''
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
typedef struct { wchar_t cFileName[80]; int id; } WIN32_FIND_DATA;
typedef struct { WIN32_FIND_DATA fd; int state; } slot;
typedef struct { int top,left,right,bottom; } RECT;
typedef unsigned long DWORD;
slot _viv_slot_current,_viv_slot_preload,_viv_slot_cache[1];
static WIN32_FIND_DATA current, loading;
static WIN32_FIND_DATA *_viv_current_fd=&current, *_viv_load_fd=&loading, *_viv_load_image_next_fd;
static wchar_t *_viv_load_image_filename;
static long _viv_load_image_terminate;
static int _viv_hwnd,_viv_load_image_thread=1,_viv_load_is_preload,_viv_load_image_allow_draw=1;
static int _viv_load_failed,_viv_file_not_found,_viv_hw_render_fallback;
static int _viv_load_image_next_is_preload,_viv_should_activate_preload_on_load,_viv_preload_chain_count;
static int _viv_load_render_wide,_viv_load_render_high,_viv_load_frame_count,_viv_nav_folder_neighbor;
static void *_viv_load_image_thread_proc;
#define debug_printf(...) ((void)0)
#define os_copy_memory memcpy
#define mem_alloc malloc
#define mem_free free
#define string_compare wcscmp
#define _VIV_LOAD_REFUSED_CLEAR(x) ((void)0)
#define _VIV_LOAD_TERMINATED() (_viv_load_image_terminate != 0)
static long InterlockedExchange(long *p,long n) { long old=*p; *p=n; return old; }
static wchar_t *string_alloc(const wchar_t *s) {
    size_t bytes=(wcslen(s)+1)*sizeof(*s);
    return memcpy(malloc(bytes),s,bytes);
}
static int _viv_cache_find(const wchar_t *file) { return -1; }
static int _viv_get_status_high(void) { return 0; }
static int _viv_get_view_top(void) { return 0; }
static int GetLastError(void) { return 0; }
static void GetClientRect(int w,RECT *r) { *r=(RECT){0,0,640,480}; }
static int CreateThread(void *a,int b,void *c,void *d,int e,DWORD *id) { return 1; }
static void _viv_status_update(void) {}
static void _viv_update_title(void) {}
static void _viv_cache_activate(int hit) {}
static void _viv_preload_next(void) {}
static void _viv_open_preload(void) {}
static void _viv_clear_preload(void) { _viv_slot_preload.fd.cFileName[0]=0; }
''' + body + r'''
int main(void)
{
    WIN32_FIND_DATA a={L"a.png",1},b={L"b.png",2},c={L"c.png",3};
    current=a;
    _viv_load_image_filename=string_alloc(a.cFileName);
    _viv_open(&b,0);
    assert(_viv_load_image_next_fd && _viv_load_image_next_fd->id==2);
    _viv_open(&a,0);
    assert(_viv_load_image_next_fd->id==1 && current.id==1 &&
        "the active but cancelled A cannot swallow the latest A request");
    assert(!_viv_load_image_allow_draw && "late cancelled frames must not repaint the viewer");
    _viv_open(&c,1);
    assert(_viv_load_image_next_fd->id==1 && !_viv_load_image_next_is_preload &&
        "background speculation cannot replace an explicit queued open");
    free(_viv_load_image_next_fd);
    free(_viv_load_image_filename);
    puts("rapid A/B/A navigation keeps the latest foreground dispatch PASS");
    return 0;
}
''').strip()

if __name__ == "__main__":
    selected = sys.argv[1:] or ["toolbar", "queue", "queued_load"]
    failed = False
    for name in selected:
        try:
            print({"toolbar": toolbar, "queue": queue, "queued_load": queued_load}[name]())
        except AssertionError as error:
            failed = True
            print(f"{name}: {error}", file=sys.stderr)
    sys.exit(int(failed))
