//
// Copyright 2025 voidtools / David Carpenter
// 
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
// 
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
// 
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.
//
// VoidImageViewer

// include guard: upstream relied on each translation unit including this
// exactly once; the R70 split layers viv_state.h and the domain headers
// on top of it, so a plain guard is now required (double inclusion
// redefines the command-id enum).
#ifndef VIV_H
#define VIV_H

#ifdef __cplusplus
extern "C" {
#endif

// compiler options
#pragma warning(disable : 4311) // type cast void * to unsigned int
#pragma warning(disable : 4312) // type cast unsigned int to void *
#pragma warning(disable : 4244) // warning C4244: 'argument' : conversion from 'LONG_PTR' to 'LONG', possible loss of data
#pragma warning(disable : 4201) // nonstandard extension used : nameless struct/union
#pragma warning(disable : 4313) // 'debug_printf' : '%x' in format string conflicts with argument 2 of type 'line_t *'
#pragma warning(disable : 4996) // deprecation
#pragma warning(disable : 4701) // potentially uninitialized local variable 'last_stretch_mode' used

// REQUIRES IE 5.01
#define _WIN32_IE 0x0501

// WINNT required for some header definitions.
// minimum is really 0x0400
#define _WIN32_WINNT 0x0501

#define WIN32_LEAN_AND_MEAN

#define COBJMACROS // c object interface please
#define CINTERFACE // c interface only

#define VIV_UINT64_MAX	0xFFFFFFFFFFFFFFFFULL
#define VIV_DWORD_MAX	0xffffffff

// single image pixel budget: a corrupted or hostile header can
// claim dimensions that decode to gigabytes of rgba (a 428 kb png
// can ask for 20000 x 20000). the loaders refuse the file before
// any allocation happens so a huge canvas fails like any other
// unloadable file instead of dying inside the allocator.
//
// the working-set round (1.1.14-rc.6) answers the audit finding that
// a pixel ceiling alone never bounded memory: the estimate prices the
// peak a load actually holds - the decode canvas, the display dib and
// the renderer staging each carry the frame once - and refuses when
// it crosses the byte ceiling. on 64-bit builds the effective static
// cap lands at ~200 mp (2.4 gb of working set): twice the largest
// real panoramic stitch the 400 mp headroom was defended with
// (15000 x 9000), half the worst case it used to admit. 32-bit builds
// keep their 100 mp behavior (1.2 gb). the canvas ceilings stay on as
// the coarse first gate and give the hardware renderers headroom for
// their power-of-two padding above the accepted size.
#define VIV_IMAGE_WORKING_SET_BYTES_PER_PIXEL	12
#if defined(_WIN64)
#define VIV_MAX_IMAGE_PIXELS	400000000
#define VIV_MAX_IMAGE_BYTES	2400000000
// the cache-set ceiling is its own line: the ring and the preload
// are opportunistic residency and never deserved to compete against
// the same 2.4 gb ceiling one image's whole working set uses - three
// cached 800 mb frame sets rode that ceiling untouched. half the
// image ceiling keeps a promise the trim can actually keep.
#define VIV_CACHE_SET_MAX_BYTES	1200000000
// animated frames are stricter than stills on every axis: the canvas
// ceiling drops to 150 mp (a one-frame animation may not out-size the
// static budget), the frame count carries its own ceiling, and the
// frame array - which holds every decoded frame at once - is bounded
// in total bytes (4 bytes per canvas pixel per frame, priced like the
// display bitmaps the gdi+ path builds per frame).
#define VIV_MAX_ANIMATION_PIXELS	150000000
#define VIV_MAX_ANIMATION_FRAMES	10000
#define VIV_MAX_ANIMATION_TOTAL_BYTES	2000000000
#else
#define VIV_MAX_IMAGE_PIXELS	100000000
#define VIV_MAX_IMAGE_BYTES	1200000000
#define VIV_CACHE_SET_MAX_BYTES	600000000
// 32-bit: the static behavior is unchanged (100 mp prices to the same
// 1.2 gb working set); the animation canvas keeps its quarter-ceiling
// (25 mp) and the frame array is bounded to 400 mb of total frames
// inside the 2 gb address space.
#define VIV_MAX_ANIMATION_PIXELS	25000000
#define VIV_MAX_ANIMATION_FRAMES	10000
#define VIV_MAX_ANIMATION_TOTAL_BYTES	400000000
#endif
// the input ceiling (the input-size round): the whole-file buffer is
// the first allocation a load makes and it happens before any pixel
// budget can see the file - a normal-sized image carrying a huge
// appended payload, a garbage tail or an oversized raw frame commits
// its bytes before the decoders ever answer. the ceiling prices the
// largest honest input the accepted budgets can need: a 200 mp qoi
// or 32-bpp bmp runs near 800 mb on x64 (100 mp / 400 mb on x86), so
// 1 gb / 512 mb admits every file the pixel budgets would accept and
// refuses the rest before the allocation. GetFileSizeEx reads the
// 64-bit size: the 32-bit GetFileSize cannot see a file past 4 gb
// (it answers the low dword there, and INVALID_FILE_SIZE only when
// that dword happens to be 0xffffffff).
#if defined(_WIN64)
#define VIV_MAX_INPUT_FILE_BYTES	1000000000
#else
#define VIV_MAX_INPUT_FILE_BYTES	512000000
#endif

typedef unsigned char utf8_t;

// __int64 is msvc only: mingw and zig cc spell it long long.
#ifndef _MSC_VER
#define __int64 long long
#endif

typedef unsigned __int64 VIV_UINT64;

#include <windows.h>
#include <windowsx.h>
#include <shlwapi.h>
//#include <gdiplus.h>
#include "../res/resource.h"
//#include <stdio.h>
//#include <math.h>
//#include <shlobj.h>
//#include <istream>
#include <commdlg.h> // OPENFILENAME
#include <shellapi.h> // ShellExecute
#include <uxtheme.h>
//#include <process.h> // _beginthreadex
#include <shlobj.h> // DROPFILES

enum
{
	VIV_ID_EDIT_CUT = 1000,
	VIV_ID_EDIT_COPY,
	VIV_ID_EDIT_PASTE,
	VIV_ID_FILE_DELETE,
	VIV_ID_FILE_EDIT,
	VIV_ID_FILE_PREVIEW,
	VIV_ID_FILE_PRINT,
	VIV_ID_FILE_SET_DESKTOP_WALLPAPER,
	VIV_ID_FILE_CLOSE,
	VIV_ID_FILE_OPEN_FILE_LOCATION,
	VIV_ID_FILE_PROPERTIES,
	VIV_ID_EDIT_COPY_TO,
	VIV_ID_EDIT_MOVE_TO,
	
	VIV_ID_VIEW_MENU,
	VIV_ID_VIEW_STATUS,
	VIV_ID_VIEW_CONTROLS,
	VIV_ID_VIEW_PRESET_1,
	VIV_ID_VIEW_PRESET_2,
	VIV_ID_VIEW_PRESET_3,
	VIV_ID_VIEW_ALLOW_SHRINKING,
	VIV_ID_VIEW_KEEP_ASPECT_RATIO,
	VIV_ID_VIEW_FILL_WINDOW,
	VIV_ID_VIEW_1TO1,
	VIV_ID_VIEW_BESTFIT,
	VIV_ID_VIEW_FULLSCREEN,
	VIV_ID_VIEW_SLIDESHOW,
	VIV_ID_VIEW_ONTOP_ALWAYS,
	VIV_ID_VIEW_ONTOP_WHILE_PLAYING_OR_ANIMATING,
	VIV_ID_VIEW_ONTOP_NEVER,
	VIV_ID_VIEW_OPTIONS,

	VIV_ID_VIEW_WINDOW_SIZE_50,
	VIV_ID_VIEW_WINDOW_SIZE_100,
	VIV_ID_VIEW_WINDOW_SIZE_200,
	VIV_ID_VIEW_WINDOW_SIZE_AUTO_FIT,

	VIV_ID_VIEW_ZOOM_IN,
	VIV_ID_VIEW_ZOOM_OUT,
	VIV_ID_VIEW_ZOOM_RESET,

	VIV_ID_SLIDESHOW_PAUSE,
	VIV_ID_SLIDESHOW_PLAY_ONLY,
	VIV_ID_SLIDESHOW_PAUSE_ONLY,
	VIV_ID_SLIDESHOW_RATE_DEC,
	VIV_ID_SLIDESHOW_RATE_INC,
	VIV_ID_SLIDESHOW_RATE_1000,
	VIV_ID_SLIDESHOW_RATE_2000,
	VIV_ID_SLIDESHOW_RATE_3000,
	VIV_ID_SLIDESHOW_RATE_4000,
	VIV_ID_SLIDESHOW_RATE_5000,
	VIV_ID_SLIDESHOW_RATE_6000,
	VIV_ID_SLIDESHOW_RATE_7000,
	VIV_ID_SLIDESHOW_RATE_8000,
	VIV_ID_SLIDESHOW_RATE_9000,
	VIV_ID_SLIDESHOW_RATE_10000,
	VIV_ID_SLIDESHOW_RATE_20000,
	VIV_ID_SLIDESHOW_RATE_30000,
	VIV_ID_SLIDESHOW_RATE_40000,
	VIV_ID_SLIDESHOW_RATE_50000,
	VIV_ID_SLIDESHOW_RATE_60000,
	VIV_ID_SLIDESHOW_RATE_CUSTOM,

	VIV_ID_ANIMATION_PLAY_PAUSE,
	VIV_ID_ANIMATION_JUMP_FORWARD_MEDIUM,
	VIV_ID_ANIMATION_JUMP_BACKWARD_MEDIUM,
	VIV_ID_ANIMATION_JUMP_FORWARD_SHORT,
	VIV_ID_ANIMATION_JUMP_BACKWARD_SHORT,
	VIV_ID_ANIMATION_JUMP_FORWARD_LONG,
	VIV_ID_ANIMATION_JUMP_BACKWARD_LONG,
	VIV_ID_ANIMATION_FRAME_HOME,
	VIV_ID_ANIMATION_FRAME_END,
	VIV_ID_ANIMATION_FRAME_STEP,
	VIV_ID_ANIMATION_FRAME_PREV,
	VIV_ID_ANIMATION_RATE_DEC,
	VIV_ID_ANIMATION_RATE_INC,
	VIV_ID_ANIMATION_RATE_RESET,

	VIV_ID_NAV_PREV,
	VIV_ID_NAV_NEXT,
	VIV_ID_NAV_HOME,
	VIV_ID_NAV_END,
	VIV_ID_NAV_SORT_NAME,
	VIV_ID_NAV_SORT_SIZE,
	VIV_ID_NAV_SORT_DATE_MODIFIED,
	VIV_ID_NAV_SORT_DATE_CREATED,
	VIV_ID_NAV_SORT_FULL_PATH,
	VIV_ID_NAV_SORT_ASCENDING,
	VIV_ID_NAV_SORT_DESCENDING,
	
	VIV_ID_HELP_HELP,
	VIV_ID_HELP_COMMAND_LINE_OPTIONS,
	VIV_ID_HELP_WEBSITE,
	VIV_ID_HELP_DONATE,
	VIV_ID_HELP_ABOUT,
	
	VIV_ID_FILE_EXIT,
	VIV_ID_FILE_SAVE_AS,
	
	VIV_ID_SLIDESHOW_TIMER,
	VIV_ID_HIDE_CURSOR_TIMER,
	VIV_ID_STATUS_TEMP_TEXT_TIMER,
	VIV_ID_ANIMATION_TIMER,
	VIV_ID_DARK_RECHECK_TIMER,
	VIV_ID_RECENT_SAVE_TIMER,

	VIV_ID_STATUS,
	VIV_ID_TOOLBAR,
	VIV_ID_MENUBAR,
	
	// rc.16: five ids retired - the classic options pages, the rebar and
	// the dark-dialog assert timer outlived their callers (the hygiene
	// walk's dead-residue find).

	VIV_ID_FILE_OPEN_FILE,
	VIV_ID_FILE_OPEN_FOLDER,
	VIV_ID_FILE_ADD_FILE,
	VIV_ID_FILE_ADD_FOLDER,
	VIV_ID_FILE_RENAME,
	
	VIV_ID_NAV_JUMPTO,
	VIV_ID_NAV_SHUFFLE,
	VIV_ID_FILE_OPEN_EVERYTHING_SEARCH,
	VIV_ID_FILE_ADD_EVERYTHING_SEARCH,
	VIV_ID_EDIT_ROTATE_90,
	VIV_ID_EDIT_ROTATE_270,
	VIV_ID_FILE_DELETE_RECYCLE,
	VIV_ID_FILE_DELETE_PERMANENTLY,

	VIV_ID_EDIT_COPY_FILENAME,
	VIV_ID_EDIT_COPY_IMAGE,
	VIV_ID_VIEW_REFRESH,
	VIV_ID_VIEW_CAPTION,
	VIV_ID_VIEW_THICKFRAME,

	// touch / zoom controls (appended so existing ids are not shifted)
	VIV_ID_VIEW_ZOOM_CONTROLS,

	// backdrop shown under transparent pixels (appended, ids not shifted)
	VIV_ID_VIEW_BACKDROP_FOLLOW,
	VIV_ID_VIEW_BACKDROP_BLACK,
	VIV_ID_VIEW_BACKDROP_WHITE,
	VIV_ID_VIEW_BACKDROP_CUSTOM,
	VIV_ID_VIEW_BACKDROP_CHECKERBOARD,
	VIV_ID_ZOOMUI,

	// zoom overlay auto hide (appended so existing ids are not shifted)
	VIV_ID_VIEW_ZOOM_AUTO_HIDE,

	// recent files mru submenu items and the clear command
	// (appended so existing ids are not shifted)
	VIV_ID_FILE_RECENT_CLEAR,
	VIV_ID_FILE_RECENT_0,
	VIV_ID_FILE_RECENT_1,
	VIV_ID_FILE_RECENT_2,
	VIV_ID_FILE_RECENT_3,
	VIV_ID_FILE_RECENT_4,
	VIV_ID_FILE_RECENT_5,
	VIV_ID_FILE_RECENT_6,
	VIV_ID_FILE_RECENT_7,
	VIV_ID_FILE_RECENT_8,
	VIV_ID_FILE_RECENT_9,

	// view menu windowed background color picker
	// (appended so existing ids are not shifted)
	VIV_ID_VIEW_WINDOWED_BACKGROUND_COLOR,

	// view menu renderer radio trio (the todo closure round,
	// appended so existing ids are not shifted)
	VIV_ID_VIEW_RENDERER_GDI,
	VIV_ID_VIEW_RENDERER_OPENGL,
	VIV_ID_VIEW_RENDERER_DIRECT3D,
};

#include "version.h"
#include "debug.h"
#include "mem.h"
#include "localization.h"
#include "os.h"
#include "wchar.h"
#include "string.h"
#include "utf8.h"
#include "ini.h"
#include "config.h"
#include "viv_theme.h"
#include "viv_msgbox.h"
#include "zoomui.h"
#include "viv_toolbar.h"
#include "viv_settings.h"
#include "glyphs.h"
#include "webp.h"
#include "qoi.h"
#include "wic.h"
#include "hwgl.h"
#include "hwd3d.h"
#include "shellmenu.h"
#include "small_pool.h"
#include "safe_size.h"
#include "everything_ipc.h"

int viv_get_command_count(void);
int viv_menu_name_to_ini_name(utf8_t *buf,int command_index);
void viv_key_add(int command_index,DWORD keyflags);
void viv_key_clear_all(int command_index);
config_key_t *viv_key_get_start(int command_index);

#ifdef __cplusplus
}
#endif

#endif // VIV_H
