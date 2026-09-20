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

// TODO (the upstream list is closed - the todo closure round
// retired the last four items: the opengl and direct3d renderers
// ride view -> renderer, the toolbar customization rides the
// toolbar's right click, and the shell context menu
// (CDefFolderMenu_Create2) rides the canvas right click.
// every completed line lives in Changes.txt at the round that
// landed it):

// _VIV_STRETCH_BLT_STITCH_SIZE * 3.1 (pan+zoom) * 16 (zoom) MUST BE < 32768

#include "viv.h"
#include "viv_state.h"
#include "viv_recent.h"
#include "viv_playlist.h"
#include "viv_load.h"
#include "viv_anim.h"
#include "viv_render.h"
#include "viv_chrome.h"
#include "viv_menubar.h"
#include "viv_dark.h"
#include "viv_dialogs.h"
#include "viv_view.h"
#include "viv_install.h"
#include "viv_menu.h"
#include "viv_wndproc.h"
#include "viv_selfshot.h"
#include "viv_export.h"

// the recent-files mru command ids run VIV_ID_FILE_RECENT_0 .. +count-1 and
// the menu builder emits ids straight off that base. this compile-time check
// locks the enum block and CONFIG_RECENT_FILE_COUNT together: raising one
// without the other fails the build instead of silently emitting command ids
// owned by unrelated entries.
typedef char _viv_recent_id_block_matches_count[(VIV_ID_FILE_RECENT_9 - VIV_ID_FILE_RECENT_0 + 1 == CONFIG_RECENT_FILE_COUNT) ? 1 : -1];


// theme change message and tooltip color messages. (not defined in older SDKs)

// per monitor dpi change message. (not defined in older SDKs)






// a file descriptor or find data
// to describe the image.







typedef struct _viv_default_key_s
{
	WORD command_id;
	WORD key_flags;
	
}_viv_default_key_t;




static int _viv_init(int nCmdShow);
void _viv_kill(void);
void _viv_exit(void);
static int _viv_is_msg(MSG *msg);

// the init failure box: what failed is already known to the caller,
// the win32 last error is the number worth showing. there is no
// window yet, so this is the only surface the failure will ever have.
static void _viv_init_failed(int last_error)
{
	wchar_t title_wbuf[STRING_SIZE];
	wchar_t text_wbuf[STRING_SIZE];
	wchar_t number_wbuf[64];
	
	// localization strings are utf-8: the utf-8 bridge is the only way
	// they cross into a wide buffer (the wide copiers never consume
	// them - the mojibake rule the suites pin tree-wide).
	string_copy_utf8_string(text_wbuf,localization_get_string(LOCALIZATION_ID_INIT_FAILED));
	string_cat_utf8(text_wbuf,(const utf8_t *)" (error ");
	string_format_number(number_wbuf,last_error);
	string_cat(text_wbuf,number_wbuf);
	string_cat_utf8(text_wbuf,(const utf8_t *)")");
	
	string_copy_utf8_string(title_wbuf,localization_get_string(LOCALIZATION_ID_APP_NAME));
	
	MessageBoxW(0,text_wbuf,title_wbuf,MB_OK|MB_ICONERROR);
}
CLIPFORMAT _viv_get_CF_PREFERREDDROPEFFECT(void);

static int _viv_main(int nCmdShow);

HMODULE _viv_stobject_hmodule = 0;
_viv_playlist_t *_viv_playlist_start = 0;
int _viv_playlist_count = 0;
_viv_playlist_t **_viv_playlist_shuffle_indexes = 0;
int _viv_playlist_shuffle_allocated = 0;
HWND _viv_hwnd = 0;
HWND _viv_status_hwnd = 0;

// the status pane texts. the panes are owner drawn (dark ui support):
// the control hands the pane index back in each WM_DRAWITEM item data
// and the text is drawn from this store. declared here: the status bar
// creation (_viv_status_show) flushes it long before _viv_status_set is
// reached in the file.
wchar_t _viv_status_part_text[_VIV_STATUS_PART_MAX][STRING_SIZE];
static HANDLE _viv_mutex = 0;
float _viv_animation_rates[] = {0.125000f,0.142857f,0.166667f,0.200000f,0.250000f,0.333333f,0.500000f,0.571429f,0.666667f,0.800000f,1.000000f,1.250000f,1.500000f,1.750000f,2.000000f,3.000000f,4.000000f,5.000000f,6.000000f,7.000000f,8.000000f}; // fixed animation rates
typedef char _viv_animation_rates_count_assert[(sizeof(_viv_animation_rates) / sizeof(float) == _VIV_ANIMATION_RATE_MAX) ? 1 : -1]; // the count literal pins the table
int _viv_animation_rate_pos = _VIV_ANIMATION_RATE_ONE;
BYTE _viv_animation_play = 1; // play or pause animations
BYTE _viv_1to1 = 0; // temporarily show the image with 100% scaling
BYTE _viv_have_old_zoom = 0; // restore this zoom level after leaving 1:1 mode.
WIN32_FIND_DATA *_viv_current_fd = 0; // the current image find data including the full path and filename.
int _viv_view_x = 0; // the current image offset in pixels
int _viv_view_y = 0; // the current image offset in pixels
double _viv_view_ix = 0.0; // the current image offset in percent, used when resizing the window
double _viv_view_iy = 0.0; // the current image offset in percent, used when resizing the window
int _viv_zoom_pos = 0; // the current zoom level
// the zoom ladder is computed in _viv_init(): each step is a 1% multiplicative
// zoom increase (1.01x) from the best fit size (pos 0). the reachable top is
// measured at runtime (see _viv_zoom_pos_max): the first step that reaches
// the 16x size cap for the current image and window.
float _viv_zoom_scales[_VIV_ZOOM_MAX];

static ULONG_PTR os_GdiplusToken; // gdiplus handle
// the startup pairing flags: com and gdi+ each owe their teardown
// exactly when their startup succeeded (see _viv_init / _viv_kill).
static int _viv_com_initialized = 0;
static int _viv_gdiplus_started = 0;
BYTE _viv_image_is_low_res = 0; // 1 = the displayed image is a progressive preview frame
// the image slots (see viv_state.h): the image on screen, the cache
// ring and the preload slot. zero-initialized static storage - the
// embedded find-data needs no heap block, and the fd allocations the
// split era made for them are gone. the ring's zero pages are the
// empty seats beyond the active run.
_viv_image_slot_t _viv_slot_current;
_viv_image_slot_t _viv_slot_cache[VIV_CACHE_SLOTS];
_viv_image_slot_t _viv_slot_preload;
int _viv_preload_chain_count = 0;
int _viv_frame_position = 0; // the current frame position
BYTE _viv_frame_looped = 0; // all frames have been displayed for this animation
BYTE _viv_is_slideshow_timeup = 0; // the slideshow timer has expired, but we are still showing an animation at least once.
VIV_UINT64 _viv_timer_tick = 0; // the current tick for the current frame.
BYTE _viv_is_animation_timer = 0; // animation timer started?
VIV_UINT64 _viv_animation_timer_tick_start = 0; // the current start tick
BYTE _viv_doing = _VIV_DOING_NOTHING; // current mouse action, such as drag to scroll image
int _viv_doing_x;
int _viv_doing_y;
BYTE _viv_fullscreen_is_maxed = 0;
BYTE _viv_is_fullscreen = 0;
BYTE _viv_is_slideshow = 0;
BYTE _viv_is_hide_cursor_timer = 0;
static CLIPFORMAT _viv_CF_PREFERREDDROPEFFECT = 0; // copy or move? clipboard operation
BYTE _viv_is_cursor_shown = 1;
int _viv_mousemove_x = -1;
int _viv_mousemove_y = -1;
BYTE _viv_in_popup_menu = 0;
HMENU _viv_hmenu = 0;
int _viv_dst_pos_x = 500;
int _viv_dst_pos_y = 500;
CRITICAL_SECTION _viv_cs;
HANDLE _viv_load_image_thread = 0;
BYTE _viv_load_is_preload = 0;
wchar_t *_viv_load_image_filename = 0;
WIN32_FIND_DATA *_viv_load_image_next_fd = NULL;
BYTE _viv_load_image_next_is_preload = 0;
volatile LONG _viv_load_image_terminate = 0;
// the loader's stage marker ("open" / "decode" / "frames" / "webp" /
// "qoi" / "wic" / "done"): written only by the loader thread, read by the
// exit timeout so a hard kill can at least report where the thread spent
// its last seconds. a stale value only names the previous stage.
PVOID volatile _viv_load_stage = (PVOID)"";
// set by the budget refusals (canvas / working set / animation) and read
// by the status line: the user sees why a file was refused, not just that
// it failed. cleared when the next load dispatches.
volatile LONG _viv_load_refused_budget = 0;
// set by the input ceiling refusal (the whole-file read happens
// before any pixel budget can see the file) and read by the status
// line: like the budget flag, cleared when the next load dispatches.
volatile LONG _viv_load_refused_input_size = 0;
// set when a hardware renderer the user picked refuses the current
// image (no context on the machine, a canvas past the texture
// ceiling, a frame the upload paths do not take) and the gdi path
// paints it: the status line says so instead of a silent fallback.
// cleared when the next load dispatches, like the refusal flags
// above it.
BYTE _viv_hw_render_fallback = 0;
_viv_reply_t *_viv_reply_start = 0;
int _viv_reply_posted = 0;
_viv_reply_t *_viv_reply_last = 0;
wchar_t *_viv_status_temp_text = 0;
HFONT _viv_about_hfont = 0;
wchar_t *_viv_last_open_file = 0;
wchar_t *_viv_last_open_folder = 0;
_viv_nav_item_t **_viv_nav_items = 0;
_viv_nav_item_t *__viv_nav_item_start = 0;
int _viv_nav_item_count = 0;
int _viv_nav_folder_neighbor = -1; // single file mode: is there another navigable file to step to? -1 unknown, 0 no, 1 yes - the _viv_next resolution records it.
wchar_t *_viv_random = 0; // temp shuffle.
DWORD _viv_random_tot_results = 0xffffffff;
BYTE _viv_is_animation_timer_event = 0;
BYTE _viv_last_is_prev = 0; // preload next or previous?
BYTE _viv_should_activate_preload_on_load = 0;
int _viv_load_render_wide = 0;
int _viv_load_render_high = 0;
WIN32_FIND_DATA *_viv_load_fd = 0; // the load fd
BYTE _viv_load_image_allow_draw = 0; // allow the image to show if it loaded successfully after the load was terminated.
BYTE _viv_file_not_found = 0; // the current file was not found. -filename is shown in the window caption.
BYTE _viv_load_failed = 0; // the current file failed to load. -filename is shown in the window caption.
BYTE _viv_is_tracking_mouse = 0; // currently tracking mouse movement over our window.
BYTE _viv_is_mouseover = 0; // mouse is currently over our window.
BYTE _viv_prevent_on_deactivate = 0; // prevent handling of deactivate in WM_ACTIVATE.
int _viv_load_frame_count = 0; // the number of frames loaded, while loading.
int _viv_src_pixel_x = -1;
int _viv_src_pixel_y = -1;
BYTE _viv_src_pixel_r = 0;
BYTE _viv_src_pixel_g = 0;
BYTE _viv_src_pixel_b = 0;
static DWORD last_process_command_line_tick;
static BYTE got_last_process_command_line_tick = 0;

// MF_OWNERDRAW = don't show in menu.
_viv_command_t _viv_commands[] = 
{
	{LOCALIZATION_ID_FILE,MF_POPUP,_VIV_MENU_ROOT,_VIV_MENU_FILE},

	{LOCALIZATION_ID_OPEN_FILE,MF_STRING,_VIV_MENU_FILE,VIV_ID_FILE_OPEN_FILE},
	{LOCALIZATION_ID_OPEN_FOLDER,MF_STRING,_VIV_MENU_FILE,VIV_ID_FILE_OPEN_FOLDER},
	{LOCALIZATION_ID_OPEN_EVERYTHING_SEARCH,MF_STRING|MF_OWNERDRAW,_VIV_MENU_FILE,VIV_ID_FILE_OPEN_EVERYTHING_SEARCH},
	{LOCALIZATION_ID_ADD_FILE,MF_STRING|MF_OWNERDRAW,_VIV_MENU_FILE,VIV_ID_FILE_ADD_FILE},
	{LOCALIZATION_ID_ADD_FOLDER,MF_STRING|MF_OWNERDRAW,_VIV_MENU_FILE,VIV_ID_FILE_ADD_FOLDER},
	{LOCALIZATION_ID_ADD_EVERYTHING_SEARCH,MF_STRING|MF_OWNERDRAW,_VIV_MENU_FILE,VIV_ID_FILE_ADD_EVERYTHING_SEARCH},
	{LOCALIZATION_ID_INVALID,MF_SEPARATOR,_VIV_MENU_FILE,0},
	{LOCALIZATION_ID_OPEN_FILE_LOCATION,MF_STRING,_VIV_MENU_FILE,VIV_ID_FILE_OPEN_FILE_LOCATION},
	{LOCALIZATION_ID_EDIT,MF_STRING,_VIV_MENU_FILE,VIV_ID_FILE_EDIT},
	{LOCALIZATION_ID_PREVIEW,MF_STRING,_VIV_MENU_FILE,VIV_ID_FILE_PREVIEW},
	{LOCALIZATION_ID_PRINT,MF_STRING,_VIV_MENU_FILE,VIV_ID_FILE_PRINT},
	{LOCALIZATION_ID_SET_DESKTOP_WALLPAPER,MF_STRING,_VIV_MENU_FILE,VIV_ID_FILE_SET_DESKTOP_WALLPAPER},
	{LOCALIZATION_ID_SAVE_AS,MF_STRING,_VIV_MENU_FILE,VIV_ID_FILE_SAVE_AS},
	{LOCALIZATION_ID_CLOSE,MF_STRING|MF_OWNERDRAW,_VIV_MENU_FILE,VIV_ID_FILE_CLOSE}, // this just clears the image.
	{LOCALIZATION_ID_INVALID,MF_SEPARATOR,_VIV_MENU_FILE,0},
	{LOCALIZATION_ID_DELETE,MF_STRING|MF_DELETE,_VIV_MENU_FILE,VIV_ID_FILE_DELETE},
	{LOCALIZATION_ID_DELETE_RECYCLE,MF_STRING|MF_OWNERDRAW,_VIV_MENU_FILE,VIV_ID_FILE_DELETE_RECYCLE},
	{LOCALIZATION_ID_DELETE_PERMANENTLY,MF_STRING|MF_OWNERDRAW,_VIV_MENU_FILE,VIV_ID_FILE_DELETE_PERMANENTLY},
	{LOCALIZATION_ID_RENAME,MF_STRING,_VIV_MENU_FILE,VIV_ID_FILE_RENAME},
	{LOCALIZATION_ID_PROPERTIES,MF_STRING,_VIV_MENU_FILE,VIV_ID_FILE_PROPERTIES},
	{LOCALIZATION_ID_OPTIONS,MF_STRING,_VIV_MENU_FILE,VIV_ID_VIEW_OPTIONS},
	{LOCALIZATION_ID_INVALID,MF_SEPARATOR,_VIV_MENU_FILE,0},
	{LOCALIZATION_ID_EXIT,MF_STRING,_VIV_MENU_FILE,VIV_ID_FILE_EXIT},
	
	{LOCALIZATION_ID_EDIT_MENU,MF_POPUP,_VIV_MENU_ROOT,_VIV_MENU_EDIT},

	{LOCALIZATION_ID_CUT,MF_STRING,_VIV_MENU_EDIT,VIV_ID_EDIT_CUT},
	{LOCALIZATION_ID_COPY,MF_STRING,_VIV_MENU_EDIT,VIV_ID_EDIT_COPY},
	{LOCALIZATION_ID_COPY_FILENAME,MF_STRING|MF_OWNERDRAW,_VIV_MENU_EDIT,VIV_ID_EDIT_COPY_FILENAME},
	{LOCALIZATION_ID_COPY_IMAGE,MF_STRING,_VIV_MENU_EDIT,VIV_ID_EDIT_COPY_IMAGE},
	{LOCALIZATION_ID_PASTE,MF_STRING|MF_OWNERDRAW,_VIV_MENU_EDIT,VIV_ID_EDIT_PASTE},
	{LOCALIZATION_ID_INVALID,MF_SEPARATOR,_VIV_MENU_EDIT,0},
	{LOCALIZATION_ID_ROTATE_CLOCKWISE,MF_STRING,_VIV_MENU_EDIT,VIV_ID_EDIT_ROTATE_90},
	{LOCALIZATION_ID_ROTATE_COUNTERCLOCKWISE,MF_STRING,_VIV_MENU_EDIT,VIV_ID_EDIT_ROTATE_270},
	{LOCALIZATION_ID_INVALID,MF_SEPARATOR,_VIV_MENU_EDIT,0},
	{LOCALIZATION_ID_COPY_TO,MF_STRING,_VIV_MENU_EDIT,VIV_ID_EDIT_COPY_TO},
	{LOCALIZATION_ID_MOVE_TO,MF_STRING,_VIV_MENU_EDIT,VIV_ID_EDIT_MOVE_TO},

	{LOCALIZATION_ID_VIEW,MF_POPUP,_VIV_MENU_ROOT,_VIV_MENU_VIEW},
	
	{LOCALIZATION_ID_LAYOUT,MF_POPUP,_VIV_MENU_VIEW,_VIV_MENU_VIEW_LAYOUT},
	{LOCALIZATION_ID_CAPTION,MF_STRING,_VIV_MENU_VIEW_LAYOUT,VIV_ID_VIEW_CAPTION},
	{LOCALIZATION_ID_FRAME,MF_STRING,_VIV_MENU_VIEW_LAYOUT,VIV_ID_VIEW_THICKFRAME},
	{LOCALIZATION_ID_MENU,MF_STRING,_VIV_MENU_VIEW_LAYOUT,VIV_ID_VIEW_MENU},
	{LOCALIZATION_ID_STATUS_BAR,MF_STRING,_VIV_MENU_VIEW_LAYOUT,VIV_ID_VIEW_STATUS},
	{LOCALIZATION_ID_CONTROLS,MF_STRING,_VIV_MENU_VIEW_LAYOUT,VIV_ID_VIEW_CONTROLS},
	{LOCALIZATION_ID_ZOOM_CONTROLS,MF_STRING,_VIV_MENU_VIEW_LAYOUT,VIV_ID_VIEW_ZOOM_CONTROLS},
	{LOCALIZATION_ID_ZOOM_AUTO_HIDE,MF_STRING,_VIV_MENU_VIEW_LAYOUT,VIV_ID_VIEW_ZOOM_AUTO_HIDE},
	{LOCALIZATION_ID_INVALID,MF_SEPARATOR,_VIV_MENU_VIEW_LAYOUT,0},
	{LOCALIZATION_ID_PRESET,MF_POPUP,_VIV_MENU_VIEW_LAYOUT,_VIV_MENU_VIEW_PRESET},
	{LOCALIZATION_ID_MINIMAL,MF_STRING,_VIV_MENU_VIEW_PRESET,VIV_ID_VIEW_PRESET_1},
	{LOCALIZATION_ID_COMPACT,MF_STRING,_VIV_MENU_VIEW_PRESET,VIV_ID_VIEW_PRESET_2},
	{LOCALIZATION_ID_NORMAL,MF_STRING,_VIV_MENU_VIEW_PRESET,VIV_ID_VIEW_PRESET_3},
	{LOCALIZATION_ID_INVALID,MF_SEPARATOR,_VIV_MENU_VIEW,0},
	{LOCALIZATION_ID_FULLSCREEN,MF_STRING,_VIV_MENU_VIEW,VIV_ID_VIEW_FULLSCREEN},
	{LOCALIZATION_ID_SLIDESHOW,MF_STRING,_VIV_MENU_VIEW,VIV_ID_VIEW_SLIDESHOW},
	{LOCALIZATION_ID_REFRESH,MF_STRING,_VIV_MENU_VIEW,VIV_ID_VIEW_REFRESH},
	{LOCALIZATION_ID_INVALID,MF_SEPARATOR,_VIV_MENU_VIEW,0},
	{LOCALIZATION_ID_VIEW_WINDOW_SIZE,MF_POPUP,_VIV_MENU_VIEW,_VIV_MENU_VIEW_WINDOW_SIZE},
	{LOCALIZATION_ID_VIEW_WINDOW_SIZE_50_PERCENT,MF_STRING,_VIV_MENU_VIEW_WINDOW_SIZE,VIV_ID_VIEW_WINDOW_SIZE_50},
	{LOCALIZATION_ID_VIEW_WINDOW_SIZE_100_PERCENT,MF_STRING,_VIV_MENU_VIEW_WINDOW_SIZE,VIV_ID_VIEW_WINDOW_SIZE_100},
	{LOCALIZATION_ID_VIEW_WINDOW_SIZE_200_PERCENT,MF_STRING,_VIV_MENU_VIEW_WINDOW_SIZE,VIV_ID_VIEW_WINDOW_SIZE_200},
	{LOCALIZATION_ID_VIEW_WINDOW_SIZE_AUTO_FIT,MF_STRING,_VIV_MENU_VIEW_WINDOW_SIZE,VIV_ID_VIEW_WINDOW_SIZE_AUTO_FIT},
	{LOCALIZATION_ID_INVALID,MF_SEPARATOR,_VIV_MENU_VIEW,0},
	// all zoom related commands live in one submenu.
	{LOCALIZATION_ID_ZOOM,MF_POPUP,_VIV_MENU_VIEW,_VIV_MENU_VIEW_ZOOM},
	{LOCALIZATION_ID_ZOOM_IN,MF_STRING,_VIV_MENU_VIEW_ZOOM,VIV_ID_VIEW_ZOOM_IN},
	{LOCALIZATION_ID_ZOOM_OUT,MF_STRING,_VIV_MENU_VIEW_ZOOM,VIV_ID_VIEW_ZOOM_OUT},
	{LOCALIZATION_ID_INVALID,MF_SEPARATOR,_VIV_MENU_VIEW_ZOOM,0},
	{LOCALIZATION_ID_ONE_TO_ONE,MF_STRING,_VIV_MENU_VIEW_ZOOM,VIV_ID_VIEW_1TO1},
	{LOCALIZATION_ID_BEST_FIT,MF_STRING,_VIV_MENU_VIEW_ZOOM,VIV_ID_VIEW_BESTFIT},
	{LOCALIZATION_ID_RESET,MF_STRING,_VIV_MENU_VIEW_ZOOM,VIV_ID_VIEW_ZOOM_RESET},
	{LOCALIZATION_ID_INVALID,MF_SEPARATOR,_VIV_MENU_VIEW,0},
	{LOCALIZATION_ID_ALLOW_SHRINKING,MF_STRING,_VIV_MENU_VIEW,VIV_ID_VIEW_ALLOW_SHRINKING},
	{LOCALIZATION_ID_KEEP_ASPECT_RATIO,MF_STRING,_VIV_MENU_VIEW,VIV_ID_VIEW_KEEP_ASPECT_RATIO},
	{LOCALIZATION_ID_FILL_WINDOW,MF_STRING,_VIV_MENU_VIEW,VIV_ID_VIEW_FILL_WINDOW},
	{LOCALIZATION_ID_INVALID,MF_SEPARATOR,_VIV_MENU_VIEW,0},
	{LOCALIZATION_ID_ON_TOP,MF_POPUP,_VIV_MENU_VIEW,_VIV_MENU_VIEW_ONTOP},
	{LOCALIZATION_ID_ALWAYS,MF_STRING|MFT_RADIOCHECK,_VIV_MENU_VIEW_ONTOP,VIV_ID_VIEW_ONTOP_ALWAYS},
	{LOCALIZATION_ID_WHILE_PLAYING_OR_ANIMATING,MF_STRING|MFT_RADIOCHECK,_VIV_MENU_VIEW_ONTOP,VIV_ID_VIEW_ONTOP_WHILE_PLAYING_OR_ANIMATING},
	{LOCALIZATION_ID_NEVER,MF_STRING|MFT_RADIOCHECK,_VIV_MENU_VIEW_ONTOP,VIV_ID_VIEW_ONTOP_NEVER},
	{LOCALIZATION_ID_INVALID,MF_SEPARATOR,_VIV_MENU_VIEW,0},
	{LOCALIZATION_ID_WINDOWED_BACKGROUND_COLOR_MENU,MF_STRING,_VIV_MENU_VIEW,VIV_ID_VIEW_WINDOWED_BACKGROUND_COLOR},
	{LOCALIZATION_ID_BACKDROP,MF_POPUP,_VIV_MENU_VIEW,_VIV_MENU_VIEW_BACKDROP},
	{LOCALIZATION_ID_BACKDROP_FOLLOW,MF_STRING|MFT_RADIOCHECK,_VIV_MENU_VIEW_BACKDROP,VIV_ID_VIEW_BACKDROP_FOLLOW},
	{LOCALIZATION_ID_BACKDROP_BLACK,MF_STRING|MFT_RADIOCHECK,_VIV_MENU_VIEW_BACKDROP,VIV_ID_VIEW_BACKDROP_BLACK},
	{LOCALIZATION_ID_BACKDROP_WHITE,MF_STRING|MFT_RADIOCHECK,_VIV_MENU_VIEW_BACKDROP,VIV_ID_VIEW_BACKDROP_WHITE},
	{LOCALIZATION_ID_BACKDROP_CUSTOM,MF_STRING|MFT_RADIOCHECK,_VIV_MENU_VIEW_BACKDROP,VIV_ID_VIEW_BACKDROP_CUSTOM},
	{LOCALIZATION_ID_BACKDROP_CHECKERBOARD,MF_STRING|MFT_RADIOCHECK,_VIV_MENU_VIEW_BACKDROP,VIV_ID_VIEW_BACKDROP_CHECKERBOARD},
	{LOCALIZATION_ID_INVALID,MF_SEPARATOR,_VIV_MENU_VIEW,0},
	{LOCALIZATION_ID_RENDERER,MF_POPUP,_VIV_MENU_VIEW,_VIV_MENU_VIEW_RENDERER},
	{LOCALIZATION_ID_RENDERER_GDI,MF_STRING|MFT_RADIOCHECK,_VIV_MENU_VIEW_RENDERER,VIV_ID_VIEW_RENDERER_GDI},
	{LOCALIZATION_ID_RENDERER_OPENGL,MF_STRING|MFT_RADIOCHECK,_VIV_MENU_VIEW_RENDERER,VIV_ID_VIEW_RENDERER_OPENGL},
	{LOCALIZATION_ID_RENDERER_DIRECT3D,MF_STRING|MFT_RADIOCHECK,_VIV_MENU_VIEW_RENDERER,VIV_ID_VIEW_RENDERER_DIRECT3D},

	{LOCALIZATION_ID_SLIDESHOW_MENU,MF_POPUP,_VIV_MENU_ROOT,_VIV_MENU_SLIDESHOW},
	{LOCALIZATION_ID_PLAY_PAUSE,MF_STRING,_VIV_MENU_SLIDESHOW,VIV_ID_SLIDESHOW_PAUSE},
	{LOCALIZATION_ID_INVALID,MF_SEPARATOR,_VIV_MENU_SLIDESHOW,0},
	{LOCALIZATION_ID_RATE,MF_POPUP,_VIV_MENU_SLIDESHOW,_VIV_MENU_SLIDESHOW_RATE},
	{LOCALIZATION_ID_DECREASE_RATE,MF_STRING,_VIV_MENU_SLIDESHOW_RATE,VIV_ID_SLIDESHOW_RATE_DEC},
	{LOCALIZATION_ID_INCREASE_RATE,MF_STRING,_VIV_MENU_SLIDESHOW_RATE,VIV_ID_SLIDESHOW_RATE_INC},
	{LOCALIZATION_ID_INVALID,MF_SEPARATOR,_VIV_MENU_SLIDESHOW_RATE,0},
	{LOCALIZATION_ID_RATE_250_MILLISECONDS,MF_STRING|MFT_RADIOCHECK,_VIV_MENU_SLIDESHOW_RATE,VIV_ID_SLIDESHOW_RATE_250},
	{LOCALIZATION_ID_RATE_500_MILLISECONDS,MF_STRING|MFT_RADIOCHECK,_VIV_MENU_SLIDESHOW_RATE,VIV_ID_SLIDESHOW_RATE_500},
	{LOCALIZATION_ID_RATE_1_SECOND,MF_STRING|MFT_RADIOCHECK,_VIV_MENU_SLIDESHOW_RATE,VIV_ID_SLIDESHOW_RATE_1000},
	{LOCALIZATION_ID_RATE_2_SECONDS,MF_STRING|MFT_RADIOCHECK,_VIV_MENU_SLIDESHOW_RATE,VIV_ID_SLIDESHOW_RATE_2000},
	{LOCALIZATION_ID_RATE_3_SECONDS,MF_STRING|MFT_RADIOCHECK,_VIV_MENU_SLIDESHOW_RATE,VIV_ID_SLIDESHOW_RATE_3000},
	{LOCALIZATION_ID_RATE_4_SECONDS,MF_STRING|MFT_RADIOCHECK,_VIV_MENU_SLIDESHOW_RATE,VIV_ID_SLIDESHOW_RATE_4000},
	{LOCALIZATION_ID_RATE_5_SECONDS,MF_STRING|MFT_RADIOCHECK,_VIV_MENU_SLIDESHOW_RATE,VIV_ID_SLIDESHOW_RATE_5000},
	{LOCALIZATION_ID_RATE_6_SECONDS,MF_STRING|MFT_RADIOCHECK,_VIV_MENU_SLIDESHOW_RATE,VIV_ID_SLIDESHOW_RATE_6000},
	{LOCALIZATION_ID_RATE_7_SECONDS,MF_STRING|MFT_RADIOCHECK,_VIV_MENU_SLIDESHOW_RATE,VIV_ID_SLIDESHOW_RATE_7000},
	{LOCALIZATION_ID_RATE_8_SECONDS,MF_STRING|MFT_RADIOCHECK,_VIV_MENU_SLIDESHOW_RATE,VIV_ID_SLIDESHOW_RATE_8000},
	{LOCALIZATION_ID_RATE_9_SECONDS,MF_STRING|MFT_RADIOCHECK,_VIV_MENU_SLIDESHOW_RATE,VIV_ID_SLIDESHOW_RATE_9000},
	{LOCALIZATION_ID_RATE_10_SECONDS,MF_STRING|MFT_RADIOCHECK,_VIV_MENU_SLIDESHOW_RATE,VIV_ID_SLIDESHOW_RATE_10000},
	{LOCALIZATION_ID_RATE_20_SECONDS,MF_STRING|MFT_RADIOCHECK,_VIV_MENU_SLIDESHOW_RATE,VIV_ID_SLIDESHOW_RATE_20000},
	{LOCALIZATION_ID_RATE_30_SECONDS,MF_STRING|MFT_RADIOCHECK,_VIV_MENU_SLIDESHOW_RATE,VIV_ID_SLIDESHOW_RATE_30000},
	{LOCALIZATION_ID_RATE_40_SECONDS,MF_STRING|MFT_RADIOCHECK,_VIV_MENU_SLIDESHOW_RATE,VIV_ID_SLIDESHOW_RATE_40000},
	{LOCALIZATION_ID_RATE_50_SECONDS,MF_STRING|MFT_RADIOCHECK,_VIV_MENU_SLIDESHOW_RATE,VIV_ID_SLIDESHOW_RATE_50000},
	{LOCALIZATION_ID_RATE_1_MINUTE,MF_STRING|MFT_RADIOCHECK,_VIV_MENU_SLIDESHOW_RATE,VIV_ID_SLIDESHOW_RATE_60000},
	{LOCALIZATION_ID_CUSTOM,MF_STRING|MFT_RADIOCHECK,_VIV_MENU_SLIDESHOW_RATE,VIV_ID_SLIDESHOW_RATE_CUSTOM},

	{LOCALIZATION_ID_ANIMATION,MF_POPUP,_VIV_MENU_ROOT,_VIV_MENU_ANIMATION},
	{LOCALIZATION_ID_ANIMATION_PLAY_PAUSE,MF_STRING,_VIV_MENU_ANIMATION,VIV_ID_ANIMATION_PLAY_PAUSE},
	{LOCALIZATION_ID_INVALID,MF_SEPARATOR,_VIV_MENU_ANIMATION,0},
	{LOCALIZATION_ID_ANIMATION_JUMP_FORWARD,MF_STRING,_VIV_MENU_ANIMATION,VIV_ID_ANIMATION_JUMP_FORWARD_MEDIUM},
	{LOCALIZATION_ID_ANIMATION_JUMP_BACKWARD,MF_STRING,_VIV_MENU_ANIMATION,VIV_ID_ANIMATION_JUMP_BACKWARD_MEDIUM},
	{LOCALIZATION_ID_ANIMATION_SHORT_JUMP_FORWARD,MF_STRING|MF_OWNERDRAW,_VIV_MENU_ANIMATION,VIV_ID_ANIMATION_JUMP_FORWARD_SHORT},
	{LOCALIZATION_ID_ANIMATION_SHORT_JUMP_BACKWARD,MF_STRING|MF_OWNERDRAW,_VIV_MENU_ANIMATION,VIV_ID_ANIMATION_JUMP_BACKWARD_SHORT},
	{LOCALIZATION_ID_ANIMATION_LONG_JUMP_FORWARD,MF_STRING|MF_OWNERDRAW,_VIV_MENU_ANIMATION,VIV_ID_ANIMATION_JUMP_FORWARD_LONG},
	{LOCALIZATION_ID_ANIMATION_LONG_JUMP_BACKWARD,MF_STRING|MF_OWNERDRAW,_VIV_MENU_ANIMATION,VIV_ID_ANIMATION_JUMP_BACKWARD_LONG},
	{LOCALIZATION_ID_INVALID,MF_SEPARATOR,_VIV_MENU_ANIMATION,0},
	{LOCALIZATION_ID_ANIMATION_FRAME_STEP,MF_STRING,_VIV_MENU_ANIMATION,VIV_ID_ANIMATION_FRAME_STEP},
	{LOCALIZATION_ID_ANIMATION_PREVIOUS_FRAME,MF_STRING,_VIV_MENU_ANIMATION,VIV_ID_ANIMATION_FRAME_PREV},
	{LOCALIZATION_ID_ANIMATION_FIRST_FRAME,MF_STRING,_VIV_MENU_ANIMATION,VIV_ID_ANIMATION_FRAME_HOME},
	{LOCALIZATION_ID_ANIMATION_LAST_FRAME,MF_STRING,_VIV_MENU_ANIMATION,VIV_ID_ANIMATION_FRAME_END},
	{LOCALIZATION_ID_INVALID,MF_SEPARATOR,_VIV_MENU_ANIMATION,0},
	{LOCALIZATION_ID_ANIMATION_DECREASE_RATE,MF_STRING,_VIV_MENU_ANIMATION,VIV_ID_ANIMATION_RATE_DEC},
	{LOCALIZATION_ID_ANIMATION_INCREASE_RATE,MF_STRING,_VIV_MENU_ANIMATION,VIV_ID_ANIMATION_RATE_INC},
	{LOCALIZATION_ID_ANIMATION_RESET_RATE,MF_STRING,_VIV_MENU_ANIMATION,VIV_ID_ANIMATION_RATE_RESET},
	
	{LOCALIZATION_ID_NAVIGATE,MF_POPUP,_VIV_MENU_ROOT,_VIV_MENU_NAVIGATE},
	{LOCALIZATION_ID_NEXT,MF_STRING,_VIV_MENU_NAVIGATE,VIV_ID_NAV_NEXT},
	{LOCALIZATION_ID_PREVIOUS,MF_STRING,_VIV_MENU_NAVIGATE,VIV_ID_NAV_PREV},
	{LOCALIZATION_ID_HOME,MF_STRING,_VIV_MENU_NAVIGATE,VIV_ID_NAV_HOME},
	{LOCALIZATION_ID_END,MF_STRING,_VIV_MENU_NAVIGATE,VIV_ID_NAV_END},
	{LOCALIZATION_ID_INVALID,MF_SEPARATOR,_VIV_MENU_NAVIGATE,0},
	{LOCALIZATION_ID_SORT,MF_POPUP,_VIV_MENU_NAVIGATE,_VIV_MENU_NAVIGATE_SORT},
	{LOCALIZATION_ID_SORT_NAME,MF_STRING|MFT_RADIOCHECK,_VIV_MENU_NAVIGATE_SORT,VIV_ID_NAV_SORT_NAME},
	{LOCALIZATION_ID_SORT_FULL_PATH,MF_STRING|MFT_RADIOCHECK,_VIV_MENU_NAVIGATE_SORT,VIV_ID_NAV_SORT_FULL_PATH},
	{LOCALIZATION_ID_SORT_SIZE,MF_STRING|MFT_RADIOCHECK,_VIV_MENU_NAVIGATE_SORT,VIV_ID_NAV_SORT_SIZE},
	{LOCALIZATION_ID_SORT_DATE_MODIFIED,MF_STRING|MFT_RADIOCHECK,_VIV_MENU_NAVIGATE_SORT,VIV_ID_NAV_SORT_DATE_MODIFIED},
	{LOCALIZATION_ID_SORT_DATE_CREATED,MF_STRING|MFT_RADIOCHECK,_VIV_MENU_NAVIGATE_SORT,VIV_ID_NAV_SORT_DATE_CREATED},
	{LOCALIZATION_ID_INVALID,MF_SEPARATOR,_VIV_MENU_NAVIGATE_SORT,0},
	{LOCALIZATION_ID_SORT_ASCENDING,MF_STRING|MFT_RADIOCHECK,_VIV_MENU_NAVIGATE_SORT,VIV_ID_NAV_SORT_ASCENDING},
	{LOCALIZATION_ID_SORT_DESCENDING,MF_STRING|MFT_RADIOCHECK,_VIV_MENU_NAVIGATE_SORT,VIV_ID_NAV_SORT_DESCENDING},
	{LOCALIZATION_ID_SHUFFLE,MF_STRING,_VIV_MENU_NAVIGATE,VIV_ID_NAV_SHUFFLE},
	{LOCALIZATION_ID_INVALID,MF_SEPARATOR,_VIV_MENU_NAVIGATE,0},
	{LOCALIZATION_ID_JUMP_TO,MF_STRING,_VIV_MENU_NAVIGATE,VIV_ID_NAV_JUMPTO},

	{LOCALIZATION_ID_HELP,MF_POPUP,_VIV_MENU_ROOT,_VIV_MENU_HELP},
	{LOCALIZATION_ID_HELP_MENU,MF_STRING,_VIV_MENU_HELP,VIV_ID_HELP_HELP},
	{LOCALIZATION_ID_COMMAND_LINE_OPTIONS,MF_STRING,_VIV_MENU_HELP,VIV_ID_HELP_COMMAND_LINE_OPTIONS},
	{LOCALIZATION_ID_INVALID,MF_SEPARATOR,_VIV_MENU_HELP,0},
	{LOCALIZATION_ID_HOME_PAGE,MF_STRING,_VIV_MENU_HELP,VIV_ID_HELP_WEBSITE},
	{LOCALIZATION_ID_DONATE,MF_STRING,_VIV_MENU_HELP,VIV_ID_HELP_DONATE},
	{LOCALIZATION_ID_INVALID,MF_SEPARATOR,_VIV_MENU_HELP,0},
	{LOCALIZATION_ID_ABOUT,MF_STRING,_VIV_MENU_HELP,VIV_ID_HELP_ABOUT},
};
typedef char _viv_commands_count_assert[(sizeof(_viv_commands) / sizeof(_viv_command_t) == _VIV_COMMAND_COUNT) ? 1 : -1]; // the count literal pins the table


_viv_default_key_t _viv_default_keys[] =
{
	{VIV_ID_FILE_OPEN_FILE,CONFIG_KEYFLAG_CTRL | 'O'},
	{VIV_ID_FILE_OPEN_FOLDER,CONFIG_KEYFLAG_CTRL | 'B'},
	{VIV_ID_FILE_OPEN_EVERYTHING_SEARCH,CONFIG_KEYFLAG_CTRL | 'E'},
	{VIV_ID_FILE_ADD_FILE,CONFIG_KEYFLAG_CTRL | CONFIG_KEYFLAG_SHIFT | 'O'},
	{VIV_ID_FILE_ADD_FOLDER,CONFIG_KEYFLAG_CTRL | CONFIG_KEYFLAG_SHIFT | 'B'},
	{VIV_ID_FILE_ADD_EVERYTHING_SEARCH,CONFIG_KEYFLAG_CTRL | CONFIG_KEYFLAG_ALT | 'E'},
	{VIV_ID_FILE_OPEN_FILE_LOCATION,CONFIG_KEYFLAG_CTRL | VK_RETURN},
	{VIV_ID_FILE_SAVE_AS,CONFIG_KEYFLAG_CTRL | 'S'},
	{VIV_ID_FILE_EDIT,CONFIG_KEYFLAG_CTRL | CONFIG_KEYFLAG_SHIFT | 'E'}, // the everything search keeps plain ctrl+e; everything-add moved to ctrl+alt+e so the editor can take ctrl+shift+e
	{VIV_ID_FILE_PRINT,CONFIG_KEYFLAG_CTRL | 'P'},
	{VIV_ID_FILE_SET_DESKTOP_WALLPAPER,CONFIG_KEYFLAG_CTRL | 'D'}, // safe again: the handler asks before it touches the desktop
	{VIV_ID_FILE_CLOSE,CONFIG_KEYFLAG_CTRL | 'W'},
	{VIV_ID_FILE_DELETE_RECYCLE,VK_DELETE},
	{VIV_ID_FILE_DELETE_PERMANENTLY,CONFIG_KEYFLAG_SHIFT | VK_DELETE},
	{VIV_ID_FILE_RENAME,VK_F2},
	{VIV_ID_FILE_EXIT,CONFIG_KEYFLAG_CTRL | 'Q'},
	{VIV_ID_EDIT_CUT,CONFIG_KEYFLAG_CTRL | 'X'},
	{VIV_ID_EDIT_COPY,CONFIG_KEYFLAG_CTRL | 'C'},
	{VIV_ID_EDIT_COPY_FILENAME,CONFIG_KEYFLAG_CTRL | CONFIG_KEYFLAG_SHIFT | 'C'},
	{VIV_ID_EDIT_PASTE,CONFIG_KEYFLAG_CTRL | 'V'},
	{VIV_ID_VIEW_PRESET_1,'1'},
	{VIV_ID_VIEW_PRESET_2,'2'},
	{VIV_ID_VIEW_PRESET_3,'3'},
	{VIV_ID_VIEW_1TO1,CONFIG_KEYFLAG_CTRL | CONFIG_KEYFLAG_ALT | '0'},
	{VIV_ID_VIEW_FULLSCREEN,CONFIG_KEYFLAG_ALT | VK_RETURN},
	{VIV_ID_VIEW_SLIDESHOW,VK_F11},
	{VIV_ID_VIEW_WINDOW_SIZE_50,CONFIG_KEYFLAG_ALT | '1'},
	{VIV_ID_VIEW_WINDOW_SIZE_100,CONFIG_KEYFLAG_ALT | '2'},
	{VIV_ID_VIEW_WINDOW_SIZE_200,CONFIG_KEYFLAG_ALT | '3'},
	{VIV_ID_VIEW_WINDOW_SIZE_AUTO_FIT,CONFIG_KEYFLAG_ALT | '4'},
	{VIV_ID_VIEW_ZOOM_IN,VK_OEM_PLUS},
	{VIV_ID_VIEW_ZOOM_IN,VK_ADD},
	{VIV_ID_VIEW_ZOOM_IN,CONFIG_KEYFLAG_CTRL | VK_ADD},
	{VIV_ID_VIEW_ZOOM_OUT,VK_OEM_MINUS},
	{VIV_ID_VIEW_ZOOM_OUT,VK_SUBTRACT},
	{VIV_ID_VIEW_ZOOM_OUT,CONFIG_KEYFLAG_CTRL | VK_SUBTRACT},
	{VIV_ID_VIEW_ZOOM_RESET,CONFIG_KEYFLAG_CTRL | '0'},
	{VIV_ID_VIEW_ONTOP_ALWAYS,CONFIG_KEYFLAG_CTRL | 'T'},
	{VIV_ID_VIEW_OPTIONS,CONFIG_KEYFLAG_CTRL | VK_OEM_COMMA}, // ctrl+comma, the modern options key: a bare o opened a modal on every stray keypress
	{VIV_ID_VIEW_REFRESH,VK_F5},
	{VIV_ID_SLIDESHOW_PAUSE,VK_SPACE},
	{VIV_ID_SLIDESHOW_RATE_DEC,VK_DOWN},
	{VIV_ID_SLIDESHOW_RATE_INC,VK_UP},
	{VIV_ID_ANIMATION_PLAY_PAUSE,CONFIG_KEYFLAG_CTRL | VK_SPACE},
	{VIV_ID_ANIMATION_JUMP_FORWARD_MEDIUM,CONFIG_KEYFLAG_CTRL | VK_NEXT},
	{VIV_ID_ANIMATION_JUMP_BACKWARD_MEDIUM,CONFIG_KEYFLAG_CTRL | VK_PRIOR},
	{VIV_ID_ANIMATION_FRAME_STEP,CONFIG_KEYFLAG_CTRL | VK_RIGHT},
	{VIV_ID_ANIMATION_FRAME_PREV,CONFIG_KEYFLAG_CTRL | VK_LEFT},
	{VIV_ID_ANIMATION_FRAME_HOME,CONFIG_KEYFLAG_CTRL | VK_HOME},
	{VIV_ID_ANIMATION_FRAME_END,CONFIG_KEYFLAG_CTRL | VK_END},
	{VIV_ID_ANIMATION_RATE_DEC,CONFIG_KEYFLAG_CTRL | VK_DOWN},
	{VIV_ID_ANIMATION_RATE_INC,CONFIG_KEYFLAG_CTRL | VK_UP},
	{VIV_ID_ANIMATION_RATE_RESET,CONFIG_KEYFLAG_CTRL | 'R'},
	{VIV_ID_NAV_NEXT,VK_RIGHT},
	{VIV_ID_NAV_NEXT,VK_NEXT},
	{VIV_ID_NAV_PREV,VK_LEFT},
	{VIV_ID_NAV_PREV,VK_PRIOR},
	{VIV_ID_NAV_HOME,VK_HOME},
	{VIV_ID_NAV_END,VK_END},
	{VIV_ID_NAV_JUMPTO,'J'},
	{VIV_ID_HELP_HELP,VK_F1},
	{VIV_ID_HELP_ABOUT,CONFIG_KEYFLAG_CTRL | VK_F1},
};

#define _VIV_DEFAULT_KEY_COUNT (sizeof(_viv_default_keys) / sizeof(_viv_default_key_t))


_viv_key_list_t *_viv_key_list = 0;

const char *_viv_association_extensions[] = 
{
	"bmp",
	"gif",
	"ico",
	"jpeg",
	"jpg",
	"png",
	"tif",
	"tiff",
	"webp",
	"emf",
	"wmf",
};
typedef char _viv_association_extensions_count_assert[(sizeof(_viv_association_extensions) / sizeof(_viv_association_extensions[0]) == _VIV_ASSOCIATION_COUNT) ? 1 : -1]; // the count literal pins the table

// the navigation visibility set: the extensions the viewer itself opens
// (the open filter's own list, the everything search prefix's own list).
// the association table above is the installer contract and stays at its
// eleven classic extensions; the folder scan, the playlist build and the
// drop enumeration answer this table instead, so every extension the open
// dialog accepts is as navigable as it is openable (the round-99 cross-list
// guard pins the three lists as one set - one widened without the others is
// how the format horizons round left next/previous blind to its eight).
const char *_viv_supported_extensions[] = 
{
	"avif",
	"bmp",
	"dds",
	"gif",
	"hdp",
	"heic",
	"heif",
	"ico",
	"jpeg",
	"jpg",
	"jxr",
	"png",
	"qoi",
	"tif",
	"tiff",
	"wdp",
	"webp",
	"emf",
	"wmf",
};
typedef char _viv_supported_extensions_count_assert[(sizeof(_viv_supported_extensions) / sizeof(_viv_supported_extensions[0]) == _VIV_SUPPORTED_EXTENSION_COUNT) ? 1 : -1]; // the count literal pins the table

// registry description.
const localization_id_t _viv_association_description_localization_id_array[] = 
{
	LOCALIZATION_ID_ASSOCIATION_DESCRIPTION_BMP,
	LOCALIZATION_ID_ASSOCIATION_DESCRIPTION_GIF,
	LOCALIZATION_ID_ASSOCIATION_DESCRIPTION_ICO,
	LOCALIZATION_ID_ASSOCIATION_DESCRIPTION_JPEG,
	LOCALIZATION_ID_ASSOCIATION_DESCRIPTION_JPG,
	LOCALIZATION_ID_ASSOCIATION_DESCRIPTION_PNG,
	LOCALIZATION_ID_ASSOCIATION_DESCRIPTION_TIF,
	LOCALIZATION_ID_ASSOCIATION_DESCRIPTION_TIFF,
	LOCALIZATION_ID_ASSOCIATION_DESCRIPTION_WEBP,
	LOCALIZATION_ID_ASSOCIATION_DESCRIPTION_EMF,
	LOCALIZATION_ID_ASSOCIATION_DESCRIPTION_WMF,
};

const char *_viv_association_icon_locations[] = 
{
	NULL,
	NULL,
	"%1",
	NULL,
	NULL,
	NULL,
	NULL,
	NULL,
	NULL,
	NULL,
	NULL,
};



#ifdef VERSION_X86

// load unicode for windows 95/98
HMODULE LoadUnicowsProc(void);

extern FARPROC _PfnLoadUnicows = (FARPROC) &LoadUnicowsProc;

HMODULE LoadUnicowsProc(void)
{
	OSVERSIONINFOA osvi;
	
	// make sure we are win9x.
	// to prevent loading unicows.dll on NT as a securiry precausion.
	osvi.dwOSVersionInfoSize = sizeof(OSVERSIONINFOA);
	if (GetVersionExA(&osvi))
	{
		if (osvi.dwPlatformId == VER_PLATFORM_WIN32_WINDOWS)
		{
			return LoadLibraryA("unicows.dll");
		}
	}
	
	return NULL;
}

#endif










// the deferred recent-files save. every mru mutation used to write the
// whole settings file (create + write + replace, three file system ops)
// and rebuild the entire menu bar on the ui thread right before the image
// load was dispatched - the visible stutter when opening files, especially
// with real-time antivirus scanning the temp file. the write is now
// debounced: the timer coalesces a burst of opens into one save, and the
// exit / endsession saves fold any still-pending write in.
int _viv_recent_save_dirty = 0;











void _viv_exit(void)
{
	InterlockedExchange(&_viv_load_image_terminate,1);
	
	// the deferred recent-files save folds into the exit write below (the
	// debounce timer never gets to fire once the quit is posted).
	_viv_recent_save_fold();
	
	// the resume capture: the file on screen when the session ends is
	// where the next session resumes (the switch lives in the settings).
	// a blank screen clears the record - there is nothing to resume.
	string_copy_with_bufsize(config_last_file,MAX_PATH,_viv_slot_current.fd.cFileName);
	
	config_save_settings(config_appdata);
	PostQuitMessage(0);
}

// cached backbuffer used for double buffered painting.
HDC _viv_paint_hdc = 0;

HBRUSH _viv_dialog_dark_hbrush = 0; // dark dialog background brush, lazy created
// a cached dark chrome brush for the toolbar strip. which: 0 = the strip
// face (0x252525, one step above the canvas), 1 = the separator shadow
// line (0x454545), 2 = the separator highlight line (0x707070),
// 3 = the menu bar face (0x202020: the canvas and dark system menu
// color). the zoom bar uses the same palette. created lazily, released
// in _viv_kill with the other cached brushes.
HBRUSH _viv_dark_chrome_hbrushes[4];


// a cached light chrome brush for the toolbar strip lines. the light strip
// face is the system menu color (it matches the light menu bar exactly and
// tracks the theme for free), but the flat separator lines are fixed subtle
// grays like the dark palette: 0 = the shadow line, 1 = the highlight line.
// released in _viv_kill with the dark chrome brushes.


HBRUSH _viv_about_light_hbrushes[2];


HBRUSH _viv_backdrop_solid_hbrush = 0; // backdrop solid color brush, cached
COLORREF _viv_backdrop_solid_color = 0; // the color the solid brush was created with
HBRUSH _viv_backdrop_checker_hbrush = 0; // checkerboard pattern brush, cached
HBITMAP _viv_backdrop_checker_hbitmap = 0; // the pattern bitmap (owned while the brush lives)
int _viv_backdrop_checker_cell = 0; // the cell size the pattern was built with


static void _viv_apply_config_language(void)
{
	// apply the language setting from the config file.
	// config_language: 0 = auto (keep the detected system language), 1 = english, 2 = simplified chinese.
	
	if (config_language == 1)
	{
		localization_set_language(LOCALIZATION_LANGUAGE_ENGLISH);
	}
	else
	if (config_language == 2)
	{
		localization_set_language(LOCALIZATION_LANGUAGE_CHINESE_SIMPLIFIED);
	}
}


void _viv_process_command_line(wchar_t *cl)
{
	wchar_t *p;
	wchar_t buf[STRING_SIZE];
	wchar_t single[STRING_SIZE];
	int file_count;
	int start_slideshow;
	int start_fullscreen;
	int start_window;
	int window_x;
	int window_y;
	int window_wide;
	int window_high;
	int set_window_rect;
	RECT rect;
	int start_maximized;
	int is_add;

	debug_printf("cl: %S\n",cl);
	
	start_slideshow = 0;
	start_fullscreen = 0;
	start_window = 0;
	start_maximized = 0;
	is_add = 0;
	p = cl;
	file_count = 0;
	single[0] = 0;
	set_window_rect = 0;

	// the geometry defaults come from the live window rect - unless
	// the window sits minimized: the iconic window parks at -32000
	// with a sliver size, so the placement normal position answers
	// the defaults the /x /y /width /height flags do not override.
	if (IsIconic(_viv_hwnd))
	{
		WINDOWPLACEMENT wp;
		
		wp.length = sizeof(WINDOWPLACEMENT);
		
		if (!GetWindowPlacement(_viv_hwnd,&wp))
		{
			GetWindowRect(_viv_hwnd,&rect);
		}
		else
		{
			rect = wp.rcNormalPosition;
		}
	}
	else
	{
		GetWindowRect(_viv_hwnd,&rect);
	}
	window_x = rect.left;
	window_y = rect.top;
	window_wide = rect.right - rect.left;
	window_high = rect.bottom - rect.top;
	
	if (config_add_command_line_timeout)
	{
		if (got_last_process_command_line_tick)
		{
			if ((GetTickCount() - last_process_command_line_tick) < (DWORD)config_add_command_line_timeout)
			{
				// don't add if nothing is loaded.
				if (*_viv_current_fd->cFileName)
				{
					is_add = 1;
				}
			}
		}
	}
	
	p = string_skip_ws(p);
	
	// skip exe filename
	p = string_get_word(p,buf,STRING_SIZE);
	p = string_skip_ws(p);

	// skip first parameter.
	for(;;)
	{
		wchar_t *bufstart;
		BOOL was_quote;

		// no more text?
		if (!*p)
		{
			break;
		}
		
		was_quote = FALSE;
		if (*p == '"')
		{
			was_quote = TRUE;
		}

		p = string_get_word(p,buf,STRING_SIZE);
		p = string_skip_ws(p);
		
		bufstart = buf;
		
		// treat switches with a '.' as a filename.
		if ((!was_quote) && ((*bufstart == '/') || (*bufstart == '-')) && (!string_is_dot(buf)))
		{
			bufstart++;
			
			if (string_icompare_lowercase_ascii(bufstart,"slideshow") == 0)
			{
				start_slideshow = 1;
			}
			else
			if (string_icompare_lowercase_ascii(bufstart,"fullscreen") == 0)
			{
				start_fullscreen = 1;	
				start_window = 0;
			}
			else
			if (string_icompare_lowercase_ascii(bufstart,"window") == 0)
			{
				start_fullscreen = 0;	
				start_window = 1;
			}
			else
			if (string_icompare_lowercase_ascii(bufstart,"maximized") == 0)
			{
				start_fullscreen = 0;	
				start_window = 1;
				start_maximized = 1;
			}
			else
			if (string_icompare_lowercase_ascii(bufstart,"ontop") == 0)
			{
				_viv_command(VIV_ID_VIEW_ONTOP_ALWAYS);
			}
			else
			if (string_icompare_lowercase_ascii(bufstart,"shuffle") == 0)
			{
				config_shuffle = 1;
			}
			else
			if (string_icompare_lowercase_ascii(bufstart,"everything") == 0)
			{
				p = string_get_word(p,buf,STRING_SIZE);
				p = string_skip_ws(p);
				
				_viv_send_everything_search(0,0,0,buf);
			}
			else
			if (string_icompare_lowercase_ascii(bufstart,"random") == 0)
			{
				p = string_get_word(p,buf,STRING_SIZE);
				p = string_skip_ws(p);
				
				_viv_send_everything_search(0,0,1,buf);
			}
			else
			if (string_icompare_lowercase_ascii(bufstart,"render-export") == 0)
			{
				// the hidden render export owns its own early parse
				// (viv_export.c answers before the mutex); this case
				// only swallows the value word so the output path never
				// reads as a file to open.
				p = string_get_word(p,buf,STRING_SIZE);
				p = string_skip_ws(p);
			}
			else
			if (string_icompare_lowercase_ascii(bufstart,"render-size") == 0)
			{
				p = string_get_word(p,buf,STRING_SIZE);
				p = string_skip_ws(p);
			}
			else
			if ((string_icompare_lowercase_ascii(bufstart,"render-gdi") == 0) ||
			    (string_icompare_lowercase_ascii(bufstart,"render-gl") == 0) ||
			    (string_icompare_lowercase_ascii(bufstart,"render-d3d") == 0))
			{
				// the renderer words are value-less: the export probe
				// (viv_export.c) already answered them. without this case
				// they fall through to the usage box - a modal message the
				// harness export could never dismiss (its window is hidden
				// and its process waits for a click that never comes).
			}
			else
			if (string_icompare_lowercase_ascii(bufstart,"minimal") == 0)
			{
				_viv_command(VIV_ID_VIEW_PRESET_1);
			}
			else
			if (string_icompare_lowercase_ascii(bufstart,"compact") == 0)
			{
				_viv_command(VIV_ID_VIEW_PRESET_2);
			}
			else
			if (string_icompare_lowercase_ascii(bufstart,"x") == 0)
			{
				p = string_get_word(p,buf,STRING_SIZE);
				p = string_skip_ws(p);
				
				window_x = string_to_int(buf);
				set_window_rect = 1;
			}
			else
			if (string_icompare_lowercase_ascii(bufstart,"y") == 0)
			{
				p = string_get_word(p,buf,STRING_SIZE);
				p = string_skip_ws(p);
				
				window_y = string_to_int(buf);
				set_window_rect = 1;
			}		
			else
			if (string_icompare_lowercase_ascii(bufstart,"width") == 0)
			{
				p = string_get_word(p,buf,STRING_SIZE);
				p = string_skip_ws(p);
				
				window_wide = string_to_int(buf);
				set_window_rect = 1;
			}
			else
			if (string_icompare_lowercase_ascii(bufstart,"height") == 0)
			{
				p = string_get_word(p,buf,STRING_SIZE);
				p = string_skip_ws(p);
				
				window_high = string_to_int(buf);
				set_window_rect = 1;
			}		
			else
			if (string_icompare_lowercase_ascii(bufstart,"rate") == 0)
			{
				p = string_get_word(p,buf,STRING_SIZE);
				p = string_skip_ws(p);

				config_slideshow_rate = string_to_int(buf);
			}
			else
			if (string_icompare_lowercase_ascii(bufstart,"name") == 0)
			{
				config_nav_sort = CONFIG_NAV_SORT_NAME;
				config_nav_sort_ascending = 1;
			}
			else			
			if (string_icompare_lowercase_ascii(bufstart,"dm") == 0)
			{
				config_nav_sort = CONFIG_NAV_SORT_DATE_MODIFIED;
				config_nav_sort_ascending = 0;
			}
			else
			if (string_icompare_lowercase_ascii(bufstart,"dc") == 0)
			{
				config_nav_sort = CONFIG_NAV_SORT_DATE_CREATED;
				config_nav_sort_ascending = 0;
			}
			else
			if (string_icompare_lowercase_ascii(bufstart,"path") == 0)
			{
				config_nav_sort = CONFIG_NAV_SORT_FULL_PATH_AND_FILENAME;
				config_nav_sort_ascending = 1;
			}
			else
			if (string_icompare_lowercase_ascii(bufstart,"size") == 0)
			{
				config_nav_sort = CONFIG_NAV_SORT_SIZE;
				config_nav_sort_ascending = 0;
			}
			else
			if (string_icompare_lowercase_ascii(bufstart,"ascending") == 0)
			{
				config_nav_sort_ascending = 1;
			}
			else
			if (string_icompare_lowercase_ascii(bufstart,"descending") == 0)
			{
				config_nav_sort_ascending = 0;
			}
			else
			if (string_icompare_lowercase_ascii(bufstart,"isrunas") == 0)
			{
				// ignore this for non-install commands.
			}
			else
			if (string_icompare_lowercase_ascii(bufstart,"add") == 0)
			{
				// don't add if nothing is loaded.
				if (*_viv_current_fd->cFileName)
				{
					is_add = 1;
				}
			}
			else
			{
				_viv_command_line_options();
			}
		}
		else
		{
			if (*buf)
			{
				if (file_count == 0)
				{
					// new playlist
					if (_viv_random)
					{
						mem_free(_viv_random);
						
						_viv_random = 0;
					}
					
					if (!is_add)
					{
						_viv_playlist_clearall();
					}
				}
				
				if (file_count == 1)
				{
					// add the last single filename.
					// we have two modes:
					// 1) playlist mode. (navigate the playlist)
					// 2) single file mode. (navigate the folder from the single file)
					_viv_playlist_add_filename(single);
				}

				if (file_count >= 1)
				{
					// add this file
					_viv_playlist_add_filename(buf);
				}
			
				if (file_count == 0)
				{
					string_copy(single,buf);
				}
				
				file_count++;
			}
		}
	}

	// add the single image filename to the playlist if we are adding.
	if (is_add)
	{
		// if the playlist is empty, add the current item.
		if (!_viv_playlist_start)
		{
			if (*_viv_current_fd->cFileName)
			{
				_viv_playlist_add_filename(_viv_current_fd->cFileName);	
			}
		}
	
		if (file_count == 1)
		{
			_viv_playlist_add_filename(single);
		}
	}
	
	debug_printf("file count %d\n",file_count);

	// show the first image.
	// don't show anything if we are adding.
	// show something if nothing is already shown.
	if (!is_add)
	{
		// nothing was handed to this instance: when the resume switch is
		// on and the last session recorded a file, that file reopens
		// with the recent-click shape (the random order goes, the
		// playlist empties, the folder re-enumerates lazily around the
		// file). a vanished record-holder clears the record so the
		// next start does not chase it again.
		if ((!_viv_export_mode) && (config_resume_last_file) && (config_last_file[0]))
		{
			if (_viv_random)
			{
				mem_free(_viv_random);
			
				_viv_random = 0;
			}
		
			_viv_playlist_clearall();
		
			if (!_viv_open_from_filename(config_last_file,VIV_OPEN_RECENT))
			{
				config_last_file[0] = 0;
			}
		}
		else
		if (file_count >= 1)
		{
			const wchar_t *open_filename;
		
			if (_viv_random)
			{
				mem_free(_viv_random);
				
				_viv_random = 0;
			}
			
			open_filename = NULL;
			
			if (file_count > 1)
			{
				// treat as a playlist.
				if (_viv_playlist_start)
				{
					open_filename = _viv_playlist_start->fd.cFileName;
				}
			}
			else
			if (file_count == 1)
			{
				open_filename = single;
			}
			
			// open the first image found (if multiple images passed).
			// if we only specified a single image, use the single image filename.
			if ((open_filename) && (_viv_open_from_filename(open_filename,VIV_OPEN_FORWARDED)))
			{
				// all good.
			}
			else
			{
				_viv_file_not_found = 1;
				_viv_status_update();
			}
		}
	}
	
	if (start_slideshow)
	{
		_viv_slideshow();
	}
	
	if (start_fullscreen)
	{
		if (!_viv_is_fullscreen)
		{
			_viv_toggle_fullscreen();
			
			if (start_maximized)
			{
				_viv_fullscreen_is_maxed = 1;
			}
		}
	}
	else
	{
		if (start_window)
		{
			if (_viv_is_fullscreen)
			{
				_viv_toggle_fullscreen();
			}
		}
		else
		{
			if (set_window_rect)
			{
				SetWindowPos(_viv_hwnd,0,window_x,window_y,window_wide,window_high,SWP_NOZORDER|SWP_NOACTIVATE);
			}
		}

		if (start_maximized)
		{
			if (!_viv_is_window_maximized(_viv_hwnd))
			{
				ShowWindow(_viv_hwnd,SW_MAXIMIZE);
			}
		}
	}
	
	// save last processing time
	// if we make another immediate call, add items to the playlist, instead of setting the playlist.
	last_process_command_line_tick = GetTickCount();
	got_last_process_command_line_tick = 1;
}

static int _viv_init(int nCmdShow)
{
	RECT rect;
	STARTUPINFO si;
	int show_maximized;
	DWORD window_style;
	
	os_init();
	localization_init(); // Initialize language system
	
	// the render export probe answers before anything else can claim
	// the process: a render export never touches the installer and
	// never redirects to a live single instance.
	_viv_export_probe_command_line();
	
	show_maximized = 0;
	
	debug_printf("%u\n",sizeof(_viv_key_list_t));
	
	_viv_key_list = mem_alloc(sizeof(_viv_key_list_t));
	
	_viv_key_list_init(_viv_key_list);
	
	// setup keys.
	{
		int i;
		
		for(i=0;i<_VIV_DEFAULT_KEY_COUNT;i++)
		{
			viv_key_add(_viv_command_index_from_command_id(_viv_default_keys[i].command_id),_viv_default_keys[i].key_flags);
		}
	}
	
	// init the zoom ladder: a geometric 1.01x per step. the ladder is long
	// enough that the 16x size cap is reachable even for photos much larger
	// than the window.
	{
		int i;
		double f;
		
		f = 1.0;
		
		for(i=0;i<_VIV_ZOOM_MAX;i++)
		{
			_viv_zoom_scales[i] = (float)f;
			
			f *= 1.01;
		}
	}
	
	debug_printf("viv %s%s (build %d) %s\n",VERSION_STRING,VERSION_TYPE,VERSION_BUILD,VERSION_TARGET_MACHINE);
	
	os_hinstance = GetModuleHandle(0);
	
	_viv_current_fd = (WIN32_FIND_DATA *)mem_alloc(sizeof(WIN32_FIND_DATA));
	os_zero_memory(_viv_current_fd,sizeof(WIN32_FIND_DATA));

	_viv_load_fd = (WIN32_FIND_DATA *)mem_alloc(sizeof(WIN32_FIND_DATA));
	os_zero_memory(_viv_load_fd,sizeof(WIN32_FIND_DATA));
	
	debug_printf("CoInitializeEx\n");
	// the pairing flag: a failed init (rpc_e_changed_mode - someone
	// else owns this thread's apartment) owes no couninitialize, and an
	// unconditional one would unbalance whoever did own it. s_false
	// ("already initialized") succeeds and still owes the pairing call.
	_viv_com_initialized = SUCCEEDED(CoInitializeEx(NULL, COINIT_APARTMENTTHREADED|COINIT_DISABLE_OLE1DDE));
	
    // this resolves ATL window thunking problem when Microsoft Layer for Unicode (MSLU) is used
    DefWindowProc(NULL,0,0,0);
    
	// init common controls
	
	debug_printf("InitCommonControlsEx\n");
	{
		INITCOMMONCONTROLSEX icex;

		icex.dwSize = sizeof(INITCOMMONCONTROLSEX);
		icex.dwICC = ICC_STANDARD_CLASSES | ICC_BAR_CLASSES | ICC_WIN95_CLASSES;
		InitCommonControlsEx(&icex);
	}

	InitializeCriticalSection(&_viv_cs);
	
	// Initialize GDI+.
	if (os_GdiplusStartup)
	{
		os_GdiplusStartupInput_t gdiplusStartupInput;
		int gdiplus_ret;
		
		gdiplusStartupInput.GdiplusVersion = 1;
		gdiplusStartupInput.DebugEventCallback = NULL;
		gdiplusStartupInput.SuppressBackgroundThread = FALSE;
		gdiplusStartupInput.SuppressExternalCodecs = FALSE;
	
		// the started flag pairs the kill-path shutdown: a refused
		// startup leaves the token zero and the shutdown skipped, instead
		// of an unbalanced shutdown on a token nobody handed out.
		gdiplus_ret = os_GdiplusStartup(&os_GdiplusToken,&gdiplusStartupInput,NULL);
		
		_viv_gdiplus_started = (gdiplus_ret == 0) ? 1 : 0;
	}

	// load settings
	config_load_settings();
	
	// apply the language setting (config can override the system language).
	_viv_apply_config_language();
	
	// set the menu theme (light/dark) before any menu or window is created,
	// so the dark mode applies from the very first draw.
	os_dark_set_app_mode(config_dark_mode);
	
	// the render export pins its own deterministic settings over
	// whatever the ini just answered (the harness bytes must not vary
	// by host).
	if (_viv_export_mode)
	{
		_viv_export_apply_config_pins();
	}
	
	// config_maximized will be overwritten when we show are normal window
	// so save it now and apply it later.
	show_maximized = config_maximized;
	
	// process install command line options (a render export never
	// touches the installer)
	if (!_viv_export_mode)
	{
		if (_viv_process_install_command_line_options(GetCommandLineW()))
		{
			_viv_kill();
			
			return 0;
		}
	}

	// mutex (a render export is a headless one shot: it never redirects
	// to a live viewer and it must answer even when one is running)
	if ((!_viv_export_mode) && (!config_multiple_instances))
	{
		SetLastError(0);

		_viv_mutex = CreateMutexA(NULL,0,"VOIDIMAGEVIEWER");
		
		if (GetLastError() == ERROR_ALREADY_EXISTS)
		{
			HWND hwnd;

			hwnd = FindWindowA("VOIDIMAGEVIEWER",0);
			
			if (hwnd)
			{
				COPYDATASTRUCT cds;
				wchar_t *command_line;
				wchar_t cwd[STRING_SIZE];
				int size;
				char *buf;
				char *d;

				// allow this process to set focus
				SetForegroundWindow(hwnd);

				command_line = GetCommandLineW();
				GetCurrentDirectory(STRING_SIZE,cwd);
				
				si.cb = sizeof(STARTUPINFO);
				GetStartupInfo(&si);
				
				if (!(si.dwFlags & STARTF_USESHOWWINDOW))
				{
					si.wShowWindow = nCmdShow;
				}
				
				// calc size
				size = (int)safe_size_add(safe_size_add(sizeof(DWORD),safe_size_mul_sizeof_wchar(safe_size_add_one(string_get_length(command_line)))),safe_size_mul_sizeof_wchar(safe_size_add_one(string_get_length(cwd))));
				buf = (char *)mem_alloc(size);
				
				// fill in
				d = buf;
				*(DWORD *)d = si.wShowWindow;
				d += sizeof(DWORD);
				string_copy((wchar_t *)d,command_line);
				d += ((string_get_length(command_line) + 1) * sizeof(wchar_t));
				string_copy((wchar_t *)d,cwd);
				d += ((string_get_length(cwd) + 1) * sizeof(wchar_t));

				// setup copydata struct
				cds.lpData = buf;
				cds.cbData = size;
				cds.dwData = _VIV_COPYDATA_COMMAND_LINE;

				SendMessage(hwnd,WM_COPYDATA,(WPARAM)0,(LPARAM)&cds);
				
				mem_free(buf);
			}

			_viv_kill();
			
			return 0;
		}
	}
	
	if (!os_RegisterClassEx(
		CS_DBLCLKS | CS_VREDRAW | CS_HREDRAW,
		_viv_proc,
		(HICON)LoadImage(os_hinstance,MAKEINTRESOURCE(IDI_ICON1),IMAGE_ICON,GetSystemMetrics(SM_CXICON),GetSystemMetrics(SM_CXICON),0),
		LoadCursor(NULL,IDC_ARROW),
		(HBRUSH)(COLOR_BTNFACE+1),
		"VOIDIMAGEVIEWER",
		(HICON)LoadImage(os_hinstance,MAKEINTRESOURCE(IDI_ICON1),IMAGE_ICON,GetSystemMetrics(SM_CXSMICON),GetSystemMetrics(SM_CYSMICON),0)))
	{
		// a refused class registration used to vanish into the void
		// return: the window creation below would fail on a class that
		// never existed, and the message loop would wait forever on a
		// window nobody could close. fail the init out loud instead.
		_viv_init_failed((int)GetLastError());
		
		_viv_kill();
		
		return 0;
	}
	
	_viv_hmenu = _viv_create_menu();
	
	if (!_viv_hmenu)
	{
		_viv_init_failed((int)GetLastError());
		
		_viv_kill();
		
		return 0;
	}
	
	rect.left = config_x;
	rect.top = config_y;
	rect.right = config_x + config_wide;
	rect.bottom = config_y + config_high;
	
	// position the window nicely on first use
	// auto-fit and center on the monitor from the mouse cursor.
	if ((!config_wide) || (!config_high))
	{
		RECT monitor_rect;
		int default_wide;
		int default_high;
		
		default_wide = 640;
		default_high = 480;
		
		// use full screen to calculate auto-fit size.
		os_MonitorRectFromCursor(1,&monitor_rect);
		
		if (config_auto_fit_wide_div)
		{
			default_wide = ((monitor_rect.right - monitor_rect.left) * config_auto_fit_wide_mul) / config_auto_fit_wide_div;
		}

		if (config_auto_fit_high_div)
		{
			default_high = ((monitor_rect.bottom - monitor_rect.top) * config_auto_fit_high_mul) / config_auto_fit_high_div;
		}
		
		rect.left = ((monitor_rect.right - monitor_rect.left) / 2) - (default_wide / 2);
		rect.top = ((monitor_rect.bottom - monitor_rect.top) / 2) - (default_high / 2);
		rect.right = rect.left + default_wide;
		rect.bottom = rect.top + default_high;
	}
	
	window_style = WS_CAPTION | WS_THICKFRAME | WS_SYSMENU | WS_MINIMIZEBOX | WS_MAXIMIZEBOX | WS_CLIPCHILDREN | WS_CLIPSIBLINGS;

	_viv_hwnd = os_CreateWindowEx(
		0,
		"VOIDIMAGEVIEWER",
		localization_get_string(LOCALIZATION_ID_APP_NAME),
		window_style,
		rect.left,rect.top,rect.right - rect.left,rect.bottom - rect.top,
		0,NULL,os_hinstance,NULL);
	
	if (!_viv_hwnd)
	{
		// same class of refusal as the registration above: a null hwnd
		// in the loop below is a zombie process - no window, no quit
		// message, a waitmessage that never wakes.
		_viv_init_failed((int)GetLastError());
		
		_viv_kill();
		
		return 0;
	}
	
	// the canvas owns the hotkeys, and the canvas never composes text:
	// dissociate the ime so letter keys reach the key table as their
	// real virtual keys (an open chinese ime rewrites them into
	// vk_processkey, which matches no binding - see
	// os_imm_associate_disable for the window census).
	os_imm_associate_disable(_viv_hwnd);
	
	if ((!config_show_caption) || (!config_show_thickframe))
	{
		_viv_update_frame();
	}
		
	os_make_rect_completely_visible(_viv_hwnd,&rect);
		
	SetWindowPos(_viv_hwnd,0,rect.left,rect.top,rect.right - rect.left,rect.bottom - rect.top,SWP_NOZORDER|SWP_NOACTIVATE);
	
	// rc.17: the init-time dpi globals came from GetDC(0) - the primary
	// monitor. a launch that restores the window onto another monitor
	// would otherwise build every strip at the primary's scale
	// (wm_dpichanged only fires on a change, and a create-time send is
	// undocumented): one explicit sync after the window exists, before
	// the first strip is built from the globals.
	os_window_update_dpi(_viv_hwnd);

	// the render export canvas: the client must answer the exact export
	// size before the first frame arrives (the fit math reads the live
	// client rect).
	if (_viv_export_mode)
	{
		_viv_export_resize_window();
		
		// a hidden window never paints: the system defers wm_paint until
		// the window shows, so updatewindow answers nothing and the pixel
		// readback would feed on a backbuffer that was never drawn (the
		// first bootstrap caught every gdi leg dying exactly there). the
		// export shows the window without activating it instead - the ci
		// runner has no watching human, and a developer running the
		// harness locally sees the canvas for the duration of one sample.
		ShowWindow(_viv_hwnd,SW_SHOWNOACTIVATE);
	}
	
	// the frame icons ride the window dpi from the first show (the
	// class icons stay as the fallback).
	_viv_icons_apply(_viv_hwnd);
	
	// the top bar is a client side child now: show it before the first
	// layout sweep (the frame menu is gone for good).
	_viv_menubar_show(config_show_menu);

	// allow non-admin/admins to close this window.
	if (os_ChangeWindowMessageFilterEx)
	{
		// MSGFLT_ALLOW = 1
		os_ChangeWindowMessageFilterEx(_viv_hwnd,WM_CLOSE,1,0);
		
		// theme change broadcasts come from unelevated system processes:
		// allow them through the uipi filter so an elevated viewer still
		// follows the windows theme live.
		os_ChangeWindowMessageFilterEx(_viv_hwnd,WM_SETTINGCHANGE,1,0);
		os_ChangeWindowMessageFilterEx(_viv_hwnd,WM_THEMECHANGED,1,0);
	}
		
	_viv_status_show(config_show_status);
	_viv_controls_show(config_show_controls);
	_viv_zoomui_update();
	
	// apply the dark chrome (title bar, status bar, zoom controls) to the
	// freshly created window.
	_viv_apply_dark_mode(0);
	
	DragAcceptFiles(_viv_hwnd,TRUE);

	_viv_update_title();

	_viv_update_ontop();
	
	si.cb = sizeof(STARTUPINFO);
	GetStartupInfo(&si);

	if (!(si.dwFlags & STARTF_USESHOWWINDOW))
	{
		si.wShowWindow = nCmdShow;
	}
	
	// default is to shownormal.
	// if its anything else, like minimize/maximize to that before we apply the command line.
	if ((!_viv_export_mode) && (si.wShowWindow != SW_SHOWNORMAL))
	{
		ShowWindow(_viv_hwnd,si.wShowWindow);
		UpdateWindow(_viv_hwnd);
	}
	
	if ((!_viv_export_mode) && (show_maximized))
	{
		ShowWindow(_viv_hwnd,SW_MAXIMIZE);
	}
	
	_viv_process_command_line(GetCommandLineW());

	// if we didn't show the window above, make sure it 
	// is shown now (a render export stays hidden - the pixels answer
	// to the harness, not the screen).
	if ((!_viv_export_mode) && (si.wShowWindow == SW_SHOWNORMAL))
	{
		ShowWindow(_viv_hwnd,SW_SHOW);
		UpdateWindow(_viv_hwnd);
	}

	return 1;
}

void _viv_kill(void)
{
	int i;
	_viv_show_cursor();

	// don't load another image..
	if (_viv_load_image_next_fd)
	{
		mem_free(_viv_load_image_next_fd);
		
		_viv_load_image_next_fd = 0;
	}
	
	// stop load_image immediately...
	if (_viv_load_image_thread)
	{
		InterlockedExchange(&_viv_load_image_terminate,1);
		
		// it's critical we wait for load image to finish before we kill the main window.
		// the wait is bounded: a decoder wedged mid-decode must not make exit
		// impossible. (the terminate flag is checked between decode steps, so
		// the timeout below only fires on a truly stuck decoder.)
		if (WaitForSingleObject(_viv_load_image_thread,10000) != WAIT_OBJECT_0)
		{
			// the hard exit, not the hard kill: TerminateThread stops the
			// thread wherever it stands - inside a heap lock, inside gdi+
			// or libwebp or wic state, mid critical section - and every later
			// line of this teardown would then share that corrupted ground
			// (CloseHandle, mem_free and DestroyWindow all take the same locks
			// the killed thread may still hold). exiting the process instead
			// runs no further teardown at all: the kernel reclaims everything.
			// the timeout telemetry stays: the stage marker names where the
			// thread spent its last seconds (open / decode / frames / webp /
			// qoi / wic) and the file names the decoder family, so the exit is
			// a recorded event, not a silent hang.
			debug_printf("load thread timeout: exiting at stage %s (%S)\n",(const char *)InterlockedCompareExchangePointer(&_viv_load_stage,NULL,NULL),(_viv_load_image_filename) ? _viv_load_image_filename : L"?");

			ExitProcess(1);
		}
		
		CloseHandle(_viv_load_image_thread);
	}
	
	if (_viv_load_image_filename)
	{
		mem_free(_viv_load_image_filename);
	}
	
	for(i=0;i<VIV_CACHE_SLOTS;i++)
	{
		if (_viv_slot_cache[i].frames)
		{
			_viv_clear_frames(_viv_slot_cache[i].frames,_viv_slot_cache[i].frame_count);
			
			_viv_slot_cache[i].frames = NULL;
		}
	}

	_viv_clear_preload_frames();
	_viv_clear();
	_viv_process_pending_clear();

	if (_viv_hwnd)
	{
		_viv_status_show(0);
		_viv_controls_show(0);

		DestroyWindow(_viv_hwnd);
	}
	
	if (_viv_status_temp_text)
	{
		mem_free(_viv_status_temp_text);
	}
	
	_viv_nav_item_free_all();
	
	if (_viv_random)
	{
		mem_free(_viv_random);
	}
	
	if (_viv_hmenu)
	{
		DestroyMenu(_viv_hmenu);
	}

	if (_viv_about_hfont)
	{
		DeleteObject(_viv_about_hfont);
	}

	if (_viv_mutex)
	{
		CloseHandle(_viv_mutex);
	}

	_viv_playlist_clearall();
	_viv_reply_clear_all();

	// the glyphs module started gdi+ on its own token: it pairs
	// its shutdown here, before the viewer's own.
	glyphs_shutdown();

	if ((os_GdiplusShutdown) && (_viv_gdiplus_started))
	{
		os_GdiplusShutdown(os_GdiplusToken);
	}

	if (_viv_stobject_hmodule)
	{
		FreeLibrary(_viv_stobject_hmodule);
	}
	
	if (_viv_com_initialized)
	{
		CoUninitialize();
	}
	
	DeleteCriticalSection(&_viv_cs);

	if (_viv_last_open_file)
	{
		mem_free(_viv_last_open_file);
	}

	if (_viv_last_open_folder)
	{
		mem_free(_viv_last_open_folder);
	}

	mem_free(_viv_load_fd);
	mem_free(_viv_current_fd);
	
	
	if (_viv_background_hbrush)
	{
		DeleteObject(_viv_background_hbrush);
		
		_viv_background_hbrush = 0;
	}
	
	if (_viv_dialog_dark_hbrush)
	{
		DeleteObject(_viv_dialog_dark_hbrush);
		
		_viv_dialog_dark_hbrush = 0;
	}
	
	{
		int i;
		
				for(i=0;i<2;i++)
		{
			if (_viv_about_light_hbrushes[i])
			{
				DeleteObject(_viv_about_light_hbrushes[i]);
				
				_viv_about_light_hbrushes[i] = 0;
			}
		}
		
for(i=0;i<4;i++)
		{
			if (_viv_dark_chrome_hbrushes[i])
			{
				DeleteObject(_viv_dark_chrome_hbrushes[i]);
				
				_viv_dark_chrome_hbrushes[i] = 0;
			}
		}
	}
	
	if (_viv_backdrop_solid_hbrush)
	{
		DeleteObject(_viv_backdrop_solid_hbrush);
		
		_viv_backdrop_solid_hbrush = 0;
		_viv_backdrop_solid_color = 0;
	}
	
	if (_viv_backdrop_checker_hbrush)
	{
		DeleteObject(_viv_backdrop_checker_hbrush);
		
		_viv_backdrop_checker_hbrush = 0;
	}
	
	if (_viv_backdrop_checker_hbitmap)
	{
		DeleteObject(_viv_backdrop_checker_hbitmap);
		
		_viv_backdrop_checker_hbitmap = 0;
		_viv_backdrop_checker_cell = 0;
	}
	
	_viv_paint_kill();
	
	_viv_key_clear_all(_viv_key_list);
	mem_free(_viv_key_list);
	
	// the remake domains: the settings window dies with the owner, the
	// menubar drops its cached hover and press brushes.
	_viv_settings_kill();
	_viv_menubar_kill();

	// the hardware renderers release their contexts and devices.
	_viv_hwgl_shutdown();
	_viv_hwd3d_shutdown();

	zoomui_kill();

	os_kill();

#ifdef _DEBUG
	mem_debug();
#endif	
}

static int _viv_main(int nCmdShow)
{
	if (_viv_init(nCmdShow))
	{
		// the render export: one pump, one paint, one bitmap, one exit
		// code - the pixel regression harness contract (viv_export.c).
		if (_viv_export_mode)
		{
			int export_ret;
			
			export_ret = _viv_export_run();
			
			_viv_kill();
			
			return export_ret;
		}
		
#ifdef VIVP_SELF_SHOT
		vivp_selfshot_init();
#endif
		for(;;)
		{
			// Main message loop:
			for(;;)
			{
				MSG msg;
				
				if (!PeekMessage(&msg,0,0,0,PM_REMOVE)) break;
				
//				debug_printf("MSG %u %u %u\n",msg.message,msg.wParam,msg.lParam);
				
				if (msg.message == WM_QUIT) goto exit;
				
				if (!_viv_is_msg(&msg))
				{
					TranslateMessage(&msg);
					DispatchMessage(&msg);
				}
			}
			
			WaitMessage();
		}
		
	exit:
		
		_viv_kill();
	}

	return 0;
}

int APIENTRY WinMain(HINSTANCE hInstance,HINSTANCE hPrevInstance,LPSTR lpCmdLine,int nShowCmd)
{
	return _viv_main(nShowCmd);
}

int __cdecl main(int argc,char **argv)
{
	return _viv_main(SW_SHOW);
}








static int _viv_is_msg(MSG *msg)
{
  	switch (msg->message)
	{
		case WM_XBUTTONDOWN:
		case WM_XBUTTONDBLCLK:
		case WM_NCXBUTTONDOWN:
		case WM_NCXBUTTONDBLCLK:

			switch(config_xbutton_action)
			{
				case 1:
					
					switch(HIWORD(msg->wParam))
					{
						case XBUTTON1:
							_viv_zoom_in(1,1,GET_X_LPARAM(msg->lParam),GET_Y_LPARAM(msg->lParam));
							break;

						case XBUTTON2:
							_viv_zoom_in(0,1,GET_X_LPARAM(msg->lParam),GET_Y_LPARAM(msg->lParam));
							break;
					}
					
					break;					

				case 2:
					
					switch(HIWORD(msg->wParam))
					{
						case XBUTTON1:
							_viv_next(1,1,0,0);
							break;

						case XBUTTON2:
							_viv_next(0,1,0,0);
							break;
					}
					
					break;
					
			}
			
			break;
				
		case WM_SYSKEYDOWN:
		case WM_KEYDOWN:
			
			{
				int key_flags;
			
				key_flags = _viv_get_current_key_mod_flags();
				
				if (msg->hwnd == _viv_hwnd)
				{
					int key_index;

					// key presses are overlay activity (idle fade timer).
					zoomui_activity();

					// cancel action
					if ((key_flags == 0) && (msg->wParam == VK_ESCAPE))
					{
						if (_viv_doing)
						{
							_viv_doing_cancel();
						
							return 1;
						}

						if (_viv_is_fullscreen)
						{
							_viv_toggle_fullscreen();
							
							// also pause slideshow
							if (_viv_is_slideshow)
							{
								_viv_pause();
							}
						
							return 1;
						}
					}
					
					// find the key.
					for(key_index=0;key_index<_VIV_COMMAND_COUNT;key_index++)
					{
						config_key_t *k;
						
						k = _viv_key_list->start[key_index];
						while(k)
						{
							if ((k->key & CONFIG_KEYFLAG_MOD_MASK) == key_flags)
							{
								if ((k->key & CONFIG_KEYFLAG_VK_MASK) == msg->wParam)
								{
									_viv_command_with_is_key_repeat(_viv_commands[key_index].command_id,(msg->lParam & 0x40000000) ? 1 : 0);
									
									return 1;
								}
							}
							
							k = k->next;
						}
					}
				}
			}
			
			break;

	}

	return 0;
}














// the dialog font: the system message font at the dialog window own
// dpi, one handle per dialog (the template face only sized the dlu
// grid at creation; the message font is the family the menu bar and
// the status bar draw, so the locale text renders with real glyphs
// instead of the fallback the hard coded segoe ui forced). the handle
// belongs to the dialog that adopted it: the options container and
// its create dialog pages coexist and jumpto is a create dialog too,
// so no broadcast (a settings change, a theme flip) may delete a
// face some open dialog is still drawing with - the font dies with
// the dialog, at the ncdestroy after its children are gone, and a
// re-apply replaces the old face only inside the same message.











// the backdrop shown under transparent pixels.





// dark dialog support: the common dialogs (options and its pages, about,
// rename, edit key, custom rate and the everything search) get the dark
// chrome when the dark ui is active. the app mode is already dark app wide
// (the comctl controls draw dark), so what is missing is the dialog title
// bar, the control visual style, the control color replies and the
// background fill.



// the property that marks the controls this module flipped to owner
// drawn (the flip is undone when the ui goes back to light).








// the property that caches the button theme handle on a dark dialog:
// uxtheme handles are not free, and the notify path used to pay an open
// and a close pair on every single paint. the handle opens lazily on the
// first dark paint (a dialog born in the light ui opens it when the theme
// flips), rides in the window data, and closes when the dialog goes away
// or the visual style changes.


















// ascii, case-insensitive wide string compare. returns 0 if equal.
int _viv_icompare_w(const wchar_t *a,const wchar_t *b)
{
	while ((*a) && (*b))
	{
		wchar_t ca;
		wchar_t cb;
		
		ca = *a;
		cb = *b;
		
		if ((ca >= 'A') && (ca <= 'Z'))
		{
			ca = ca + 'a' - 'A';
		}
		
		if ((cb >= 'A') && (cb <= 'Z'))
		{
			cb = cb + 'a' - 'A';
		}
		
		if (ca != cb)
		{
			return 1;
		}
		
		a++;
		b++;
	}
	
	if (*a != *b)
	{
		return 1;
	}
	
	return 0;
}




CLIPFORMAT _viv_get_CF_PREFERREDDROPEFFECT(void)
{
	if (!_viv_CF_PREFERREDDROPEFFECT)
	{
		_viv_CF_PREFERREDDROPEFFECT = RegisterClipboardFormat(CFSTR_PREFERREDDROPEFFECT);
	}
	
	return _viv_CF_PREFERREDDROPEFFECT;
}





































































































































			

































