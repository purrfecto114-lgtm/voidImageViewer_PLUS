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
// customizable settings

#include "viv.h"
#include "viv_state.h"
#include "viv_playlist.h"

static int _config_icompare_ascii(const utf8_t *s1,const char *lowercase_ascii_s2);

static void _config_load_settings_by_location(const wchar_t *path,int is_root);
static void _config_write_int(HANDLE h,const char *ascii_key,int value);
static void _config_write_string(HANDLE h,const char *ascii_key,const wchar_t *s);
static void _config_write_utf8(HANDLE h,const utf8_t *s);
static void _config_recent_key_name(utf8_t *buf,int index);
static void _config_save_settings_by_location(const wchar_t *path,int is_root);
static void _config_lanesafe_note_load(const wchar_t *filename,int state,int anomaly);
static void _config_lanesafe_auditbak_write(const wchar_t *filename);
static void _config_lanesafe_defaults_capture(void);

BYTE config_appdata = 0; // store settings in %APPDATA%\voidimageviewer or in the same location as voidimageviewer.exe
BYTE config_language = 0; // ui language: 0 = auto (follow the system language), 1 = english, 2 = simplified chinese.
BYTE config_dark_mode = 2; // ui theme: 0 = light, 1 = dark, 2 = auto (follow the windows theme).
BYTE config_ui_accent = 0; // ui accent color index into the viv_theme accent table (0..4).
BYTE config_backdrop_mode = CONFIG_BACKDROP_MODE_FOLLOW; // backdrop under transparent pixels: 0 = follow window background, 1 = black, 2 = white, 3 = custom, 4 = checkerboard
BYTE config_backdrop_color_r = 128; // custom backdrop color
BYTE config_backdrop_color_g = 128;
BYTE config_backdrop_color_b = 128;
BYTE config_keep_centered = 1; // when zooming out, don't recenter the image. (keep cursor under the same pixel)
int config_x = 0;
int config_y = 0;
int config_wide = 0;
int config_high = 0;
BYTE config_maximized = 0;

// the compiled-in slideshow rate. named so the clamp below and the
// initialiser here read one authored value: a non-positive rate is repaired
// to this default rather than to a floor (see config_clamp_slideshow_rate
// and the note in config.h).
#define _CONFIG_LANESAFE_SLIDESHOW_RATE_DEFAULT CONFIG_LANESAFE_SLIDESHOW_RATE_DEFAULT

int config_slideshow_rate = _CONFIG_LANESAFE_SLIDESHOW_RATE_DEFAULT;
BYTE config_allow_shrinking = 1; // prevent resizing an image below 100%
BYTE config_shrink_blit_mode = CONFIG_SHRINK_BLIT_MODE_HALFTONE; // shrink filter
int config_renderer = CONFIG_RENDERER_GDI; // render back end: gdi default, the hardware paths are opt-in
int config_toolbar_groups = 0x3f; // toolbar group visibility mask: every group on
BYTE config_toolbar_icon_only = 0; // icons without labels: the full strip outgrows a default window at 96 dpi; icon-only fits every group
BYTE config_mag_filter = CONFIG_MAG_FILTER_COLORONCOLOR; // magnify filter
// default to sorting by filename in natural order, ascending.
// this matches the windows explorer default, so next/previous navigation
// walks the folder in the order the user sees it in explorer.
BYTE config_nav_sort = CONFIG_NAV_SORT_NAME; // current navigation sort.
BYTE config_nav_sort_ascending = 1; // sort navigation ascending or descending.
BYTE config_keep_aspect_ratio = 1; // stretch images with the original aspect ratio.
BYTE config_fill_window = 0; // stretch the image to fill the window
BYTE config_fullscreen_fill_window = 1; // same as fill_window, except this setting is used when we are fullscreen
BYTE config_auto_zoom = 0; // automatically resize the window to fit the newly loaded image
BYTE config_auto_zoom_type = 1; // 0 = 50%, 1 = 100%, 2 = 200%
int config_auto_fit_wide_mul = 3;
int config_auto_fit_wide_div = 5;
int config_auto_fit_high_mul = 3;
int config_auto_fit_high_div = 5;
BYTE config_frame_minus = 0; // show frame counter or remaining frames in status bar
BYTE config_multiple_instances = 0; // all multiple instances or use a single instance.
BYTE config_show_status = 1;
BYTE config_show_controls = 1;
BYTE config_show_zoom_controls = 0; // show the floating zoom controls. (off by default; the View menu or Settings turns it on)
BYTE config_zoom_auto_hide = 1; // auto-hide the fullscreen zoom overlay when idle.
// recent files mru: newest first, at most CONFIG_RECENT_FILE_COUNT entries.
wchar_t *config_recent_files[CONFIG_RECENT_FILE_COUNT] = {0};
int config_recent_file_count = 0;
BYTE config_prevent_sleep = 1;
BYTE config_loop_animations_once = 1;
BYTE config_mouse_wheel_action = 0; // 0 = zoom, 1 = next/prev, 2=prev/next
BYTE config_ctrl_mouse_wheel_action = 0; // 0 = zoom, 1 = next/prev, 2=prev/next
BYTE config_left_click_action = 0; // 0 = scroll, 1 = play/pause slideshow, 2 = play/pause animation, 3=zoom in, 4=next, 5=1:1 scroll, 6=move-window
BYTE config_right_click_action = 0; // 0 = context menu, 1=zoom out, 2=prev, 
BYTE config_xbutton_action = 2; // 1=zoom, 2=next
BYTE config_windowed_background_color_r = 255;
BYTE config_windowed_background_color_g = 255;
BYTE config_windowed_background_color_b = 255;
BYTE config_fullscreen_background_color_r = 0;
BYTE config_fullscreen_background_color_g = 0;
BYTE config_fullscreen_background_color_b = 0;
BYTE config_options_last_page = 0;
int config_short_jump = 500;
int config_medium_jump = 1000;
int config_long_jump = 2000;
BYTE config_shuffle = 0;
BYTE config_browse_file_open_dialog = 1;
BYTE config_ontop = 0; // 0 = never, 1 = always, 2 = while slideshow or animating.
int config_slideshow_custom_rate = 3; // custom slideshow rate (see type below)
BYTE config_slideshow_custom_rate_type = 1; // 0 = milliseconds, 1 = seconds, 2 = minutes
BYTE config_scroll_window = 1;
int config_preload_count = 1; // images preloaded ahead of the walker (0..5)
int config_cache_count = 1; // images cached behind the walker in the ring (0..8)
BYTE config_preload_next = 1; // the legacy pair: the migration source, kept in step on save
BYTE config_cache_last = 1;
BYTE config_resume_last_file = 0; // reopen the last session's file on a blank start
// the ini's last_file is copied by string_copy_utf8_string, which
// writes - and accounts for - STRING_SIZE wchars: a MAX_PATH target
// let 260..1023 characters of ini text run over every config global
// behind it. the buffer answers the copier's own contract.
wchar_t config_last_file[STRING_SIZE] = {0}; // the last session's file (empty = nothing to resume)
BYTE config_icm = 1;
BYTE config_show_menu = 1;
BYTE config_show_caption = 1;
BYTE config_show_thickframe = 1;
BYTE config_toolbar_move_window = 1;
BYTE config_windowed_hide_cursor = 1;
BYTE config_pixel_info = 0;
BYTE config_orientation = 1;
BYTE config_title_bar_format = 1; // 0=full path, 1=filename, 2=none
int config_add_command_line_timeout = 500; // in milliseconds

// ---- the load state (lane-B: the corrupted-ini gate) ----
//
// the outcome of the last settings load. the ini reader is tolerant by
// design - it ignores what it does not understand - so a half-written or a
// cloud-synced file parses to partial garbage and the rest of the session
// quietly runs on compiled-in defaults. the exit path used to write that
// back unconditionally, which is how a user's themes, shortcuts, recent
// files and associations were replaced by defaults without a word. the
// state is what lets the exit path ask first.
int config_last_load_state = CONFIG_LOAD_OK;

// the settings file the last load answered from, and the last-known-good
// sibling it wrote beside it (the ".auditbak" the load takes before it
// parses). empty when no layer of the load named a file. LANE-B additions:
// the prefix keeps them clear of any other lane's globals.
wchar_t config_lanesafe_loaded_ini[STRING_SIZE] = {0};
wchar_t config_lanesafe_auditbak[STRING_SIZE] = {0};

// the mirror's size. the census below names 78 globals and the widest of
// them is an int, so 78 * sizeof(int) covers it with room to spare; a
// census that outgrew this would be refused loudly at capture time rather
// than write past the end.
#define CONFIG_LANESAFE_DEFAULT_BYTES 512

// the compiled-in defaults, mirrored.
//
// the values are not re-spelled anywhere: the capture below runs as the
// first statement of config_load_settings, which is before anything in the
// process has written a config global, so every one of them still carries
// the value its initialiser above gave it. the mirror therefore IS the
// initialiser block - one authored set of defaults - and the reset pass
// copies it back. (rewriting the initialisers to read from a macro block
// would have been the other way round, but the guard suite pins several of
// those literal initialiser lines as the audit evidence they are.)
static BYTE _config_lanesafe_defaults[CONFIG_LANESAFE_DEFAULT_BYTES];

// has the capture run? a second pass would capture whatever the first load
// left behind, which is exactly what the reset has to undo.
static BYTE _config_lanesafe_defaults_taken = 0;

// one slot of the mirror: where the global lives and how wide it is. the
// address is not a default - the bytes behind it are, and those are the
// initialiser's own.
#define _CONFIG_LANESAFE_SLOT(variable) _config_lanesafe_capture(&_config_lanesafe_defaults[_config_lanesafe_offset],&(variable),sizeof(variable))

// the restore twin of the slot macro: the same census, walked again.
#define _CONFIG_LANESAFE_RESTORE(variable) _config_lanesafe_restore(&_config_lanesafe_defaults[_config_lanesafe_offset],&(variable),sizeof(variable))

static SIZE_T _config_lanesafe_offset = 0;

// take one global's initialiser value into the mirror.
static void _config_lanesafe_capture(unsigned char *slot,const void *address,SIZE_T size);
static void _config_lanesafe_restore(const void *slot,void *address,SIZE_T size);

// take one global's initialiser value into the mirror.
static void _config_lanesafe_capture(unsigned char *slot,const void *address,SIZE_T size)
{
	if ((_config_lanesafe_offset + size) > CONFIG_LANESAFE_DEFAULT_BYTES)
	{
		// the mirror is sized by the census below; running past it would
		// be a compile-order slip, so say so loudly rather than write.
		debug_printf("config: the defaults mirror is full (%u + %u > %u)\r\n",(unsigned int)_config_lanesafe_offset,(unsigned int)size,(unsigned int)CONFIG_LANESAFE_DEFAULT_BYTES);

		return;
	}

	os_copy_memory(slot,address,size);

	_config_lanesafe_offset += size;
}

// put one global's initialiser value back.
static void _config_lanesafe_restore(const void *slot,void *address,SIZE_T size)
{
	os_copy_memory(address,slot,size);
}

static void _config_lanesafe_defaults_capture(void)
{
	// the mirror runs once: a second pass would capture whatever the first
	// load left behind, which is exactly what the reset has to undo.
	if (_config_lanesafe_defaults_taken)
	{
		return;
	}

	_config_lanesafe_defaults_taken = 1;

	_config_lanesafe_offset = 0;

	// the census, in the same order as the initialiser block above.
	_CONFIG_LANESAFE_SLOT(config_appdata);
	_CONFIG_LANESAFE_SLOT(config_language);
	_CONFIG_LANESAFE_SLOT(config_dark_mode);
	_CONFIG_LANESAFE_SLOT(config_ui_accent);
	_CONFIG_LANESAFE_SLOT(config_backdrop_mode);
	_CONFIG_LANESAFE_SLOT(config_backdrop_color_r);
	_CONFIG_LANESAFE_SLOT(config_backdrop_color_g);
	_CONFIG_LANESAFE_SLOT(config_backdrop_color_b);
	_CONFIG_LANESAFE_SLOT(config_keep_centered);
	// config_maximized is deliberately absent, and so are
	// config_x/config_y/config_wide/config_high: the window rectangle and
	// the maximized flag are this session's window state, not preferences,
	// and a settings reset must not move the window under the user on the
	// very click that asked for it.
	_CONFIG_LANESAFE_SLOT(config_slideshow_rate);
	_CONFIG_LANESAFE_SLOT(config_allow_shrinking);
	_CONFIG_LANESAFE_SLOT(config_shrink_blit_mode);
	_CONFIG_LANESAFE_SLOT(config_renderer);
	_CONFIG_LANESAFE_SLOT(config_toolbar_groups);
	_CONFIG_LANESAFE_SLOT(config_toolbar_icon_only);
	_CONFIG_LANESAFE_SLOT(config_mag_filter);
	_CONFIG_LANESAFE_SLOT(config_nav_sort);
	_CONFIG_LANESAFE_SLOT(config_nav_sort_ascending);
	_CONFIG_LANESAFE_SLOT(config_keep_aspect_ratio);
	_CONFIG_LANESAFE_SLOT(config_fill_window);
	_CONFIG_LANESAFE_SLOT(config_fullscreen_fill_window);
	_CONFIG_LANESAFE_SLOT(config_auto_zoom);
	_CONFIG_LANESAFE_SLOT(config_auto_zoom_type);
	_CONFIG_LANESAFE_SLOT(config_auto_fit_wide_mul);
	_CONFIG_LANESAFE_SLOT(config_auto_fit_wide_div);
	_CONFIG_LANESAFE_SLOT(config_auto_fit_high_mul);
	_CONFIG_LANESAFE_SLOT(config_auto_fit_high_div);
	_CONFIG_LANESAFE_SLOT(config_frame_minus);
	_CONFIG_LANESAFE_SLOT(config_multiple_instances);
	_CONFIG_LANESAFE_SLOT(config_show_status);
	_CONFIG_LANESAFE_SLOT(config_show_controls);
	_CONFIG_LANESAFE_SLOT(config_show_zoom_controls);
	_CONFIG_LANESAFE_SLOT(config_zoom_auto_hide);
	_CONFIG_LANESAFE_SLOT(config_recent_file_count);
	_CONFIG_LANESAFE_SLOT(config_prevent_sleep);
	_CONFIG_LANESAFE_SLOT(config_loop_animations_once);
	_CONFIG_LANESAFE_SLOT(config_mouse_wheel_action);
	_CONFIG_LANESAFE_SLOT(config_ctrl_mouse_wheel_action);
	_CONFIG_LANESAFE_SLOT(config_left_click_action);
	_CONFIG_LANESAFE_SLOT(config_right_click_action);
	_CONFIG_LANESAFE_SLOT(config_xbutton_action);
	_CONFIG_LANESAFE_SLOT(config_windowed_background_color_r);
	_CONFIG_LANESAFE_SLOT(config_windowed_background_color_g);
	_CONFIG_LANESAFE_SLOT(config_windowed_background_color_b);
	_CONFIG_LANESAFE_SLOT(config_fullscreen_background_color_r);
	_CONFIG_LANESAFE_SLOT(config_fullscreen_background_color_g);
	_CONFIG_LANESAFE_SLOT(config_fullscreen_background_color_b);
	_CONFIG_LANESAFE_SLOT(config_options_last_page);
	_CONFIG_LANESAFE_SLOT(config_short_jump);
	_CONFIG_LANESAFE_SLOT(config_medium_jump);
	_CONFIG_LANESAFE_SLOT(config_long_jump);
	_CONFIG_LANESAFE_SLOT(config_shuffle);
	_CONFIG_LANESAFE_SLOT(config_browse_file_open_dialog);
	_CONFIG_LANESAFE_SLOT(config_ontop);
	_CONFIG_LANESAFE_SLOT(config_slideshow_custom_rate);
	_CONFIG_LANESAFE_SLOT(config_slideshow_custom_rate_type);
	_CONFIG_LANESAFE_SLOT(config_scroll_window);
	_CONFIG_LANESAFE_SLOT(config_preload_count);
	_CONFIG_LANESAFE_SLOT(config_cache_count);
	_CONFIG_LANESAFE_SLOT(config_preload_next);
	_CONFIG_LANESAFE_SLOT(config_cache_last);
	_CONFIG_LANESAFE_SLOT(config_resume_last_file);
	_CONFIG_LANESAFE_SLOT(config_icm);
	_CONFIG_LANESAFE_SLOT(config_show_menu);
	_CONFIG_LANESAFE_SLOT(config_show_caption);
	_CONFIG_LANESAFE_SLOT(config_show_thickframe);
	_CONFIG_LANESAFE_SLOT(config_toolbar_move_window);
	_CONFIG_LANESAFE_SLOT(config_windowed_hide_cursor);
	_CONFIG_LANESAFE_SLOT(config_pixel_info);
	_CONFIG_LANESAFE_SLOT(config_orientation);
	_CONFIG_LANESAFE_SLOT(config_title_bar_format);
	_CONFIG_LANESAFE_SLOT(config_add_command_line_timeout);

	// the defaults mirror must hold exactly the last slot. a silent short
	// census would reset the settings the census forgot, which is worse
	// than not resetting at all.
	debug_printf("config: the defaults mirror holds %u of %u bytes\r\n",(unsigned int)_config_lanesafe_offset,(unsigned int)CONFIG_LANESAFE_DEFAULT_BYTES);
}

int config_clamp_slideshow_rate(int rate)
{
	// see config.h for why a non-positive rate is a missing rate and not
	// a fast one: SetTimer answers anything under USER_TIMER_MINIMUM with
	// 10ms and WM_TIMER still arrives, so a zero runs the deck at full
	// speed rather than holding it still. the compiled-in default is the
	// honest repair; a floor would bless the runaway as a legal rate.
	if (rate <= 0)
	{
		return CONFIG_LANESAFE_SLIDESHOW_RATE_DEFAULT;
	}

	if (rate < CONFIG_LANESAFE_SLIDESHOW_RATE_MIN)
	{
		return CONFIG_LANESAFE_SLIDESHOW_RATE_MIN;
	}

	if (rate > CONFIG_LANESAFE_SLIDESHOW_RATE_MAX)
	{
		return CONFIG_LANESAFE_SLIDESHOW_RATE_MAX;
	}

	return rate;
}

void config_reset_settings_to_defaults(void)
{
	// the mirror must exist: the capture runs at the top of
	// config_load_settings, so by the time anything can ask for a reset it
	// has. the guard is here anyway - a reset with no mirror would blank
	// the settings instead of restoring them.
	if (!_config_lanesafe_defaults_taken)
	{
		_config_lanesafe_defaults_capture();
	}

	_config_lanesafe_offset = 0;

	// the same census, walked again.
	_CONFIG_LANESAFE_RESTORE(config_appdata);
	_CONFIG_LANESAFE_RESTORE(config_language);
	_CONFIG_LANESAFE_RESTORE(config_dark_mode);
	_CONFIG_LANESAFE_RESTORE(config_ui_accent);
	_CONFIG_LANESAFE_RESTORE(config_backdrop_mode);
	_CONFIG_LANESAFE_RESTORE(config_backdrop_color_r);
	_CONFIG_LANESAFE_RESTORE(config_backdrop_color_g);
	_CONFIG_LANESAFE_RESTORE(config_backdrop_color_b);
	_CONFIG_LANESAFE_RESTORE(config_keep_centered);
	// the window rectangle and the maximized flag are deliberately absent
	// (see the census in _config_lanesafe_defaults_capture).
	_CONFIG_LANESAFE_RESTORE(config_slideshow_rate);
	_CONFIG_LANESAFE_RESTORE(config_allow_shrinking);
	_CONFIG_LANESAFE_RESTORE(config_shrink_blit_mode);
	_CONFIG_LANESAFE_RESTORE(config_renderer);
	_CONFIG_LANESAFE_RESTORE(config_toolbar_groups);
	_CONFIG_LANESAFE_RESTORE(config_toolbar_icon_only);
	_CONFIG_LANESAFE_RESTORE(config_mag_filter);
	_CONFIG_LANESAFE_RESTORE(config_nav_sort);
	_CONFIG_LANESAFE_RESTORE(config_nav_sort_ascending);
	_CONFIG_LANESAFE_RESTORE(config_keep_aspect_ratio);
	_CONFIG_LANESAFE_RESTORE(config_fill_window);
	_CONFIG_LANESAFE_RESTORE(config_fullscreen_fill_window);
	_CONFIG_LANESAFE_RESTORE(config_auto_zoom);
	_CONFIG_LANESAFE_RESTORE(config_auto_zoom_type);
	_CONFIG_LANESAFE_RESTORE(config_auto_fit_wide_mul);
	_CONFIG_LANESAFE_RESTORE(config_auto_fit_wide_div);
	_CONFIG_LANESAFE_RESTORE(config_auto_fit_high_mul);
	_CONFIG_LANESAFE_RESTORE(config_auto_fit_high_div);
	_CONFIG_LANESAFE_RESTORE(config_frame_minus);
	_CONFIG_LANESAFE_RESTORE(config_multiple_instances);
	_CONFIG_LANESAFE_RESTORE(config_show_status);
	_CONFIG_LANESAFE_RESTORE(config_show_controls);
	_CONFIG_LANESAFE_RESTORE(config_show_zoom_controls);
	_CONFIG_LANESAFE_RESTORE(config_zoom_auto_hide);
	_CONFIG_LANESAFE_RESTORE(config_recent_file_count);
	_CONFIG_LANESAFE_RESTORE(config_prevent_sleep);
	_CONFIG_LANESAFE_RESTORE(config_loop_animations_once);
	_CONFIG_LANESAFE_RESTORE(config_mouse_wheel_action);
	_CONFIG_LANESAFE_RESTORE(config_ctrl_mouse_wheel_action);
	_CONFIG_LANESAFE_RESTORE(config_left_click_action);
	_CONFIG_LANESAFE_RESTORE(config_right_click_action);
	_CONFIG_LANESAFE_RESTORE(config_xbutton_action);
	_CONFIG_LANESAFE_RESTORE(config_windowed_background_color_r);
	_CONFIG_LANESAFE_RESTORE(config_windowed_background_color_g);
	_CONFIG_LANESAFE_RESTORE(config_windowed_background_color_b);
	_CONFIG_LANESAFE_RESTORE(config_fullscreen_background_color_r);
	_CONFIG_LANESAFE_RESTORE(config_fullscreen_background_color_g);
	_CONFIG_LANESAFE_RESTORE(config_fullscreen_background_color_b);
	_CONFIG_LANESAFE_RESTORE(config_options_last_page);
	_CONFIG_LANESAFE_RESTORE(config_short_jump);
	_CONFIG_LANESAFE_RESTORE(config_medium_jump);
	_CONFIG_LANESAFE_RESTORE(config_long_jump);
	_CONFIG_LANESAFE_RESTORE(config_shuffle);
	_CONFIG_LANESAFE_RESTORE(config_browse_file_open_dialog);
	_CONFIG_LANESAFE_RESTORE(config_ontop);
	_CONFIG_LANESAFE_RESTORE(config_slideshow_custom_rate);
	_CONFIG_LANESAFE_RESTORE(config_slideshow_custom_rate_type);
	_CONFIG_LANESAFE_RESTORE(config_scroll_window);
	_CONFIG_LANESAFE_RESTORE(config_preload_count);
	_CONFIG_LANESAFE_RESTORE(config_cache_count);
	_CONFIG_LANESAFE_RESTORE(config_preload_next);
	_CONFIG_LANESAFE_RESTORE(config_cache_last);
	_CONFIG_LANESAFE_RESTORE(config_resume_last_file);
	_CONFIG_LANESAFE_RESTORE(config_icm);
	_CONFIG_LANESAFE_RESTORE(config_show_menu);
	_CONFIG_LANESAFE_RESTORE(config_show_caption);
	_CONFIG_LANESAFE_RESTORE(config_show_thickframe);
	_CONFIG_LANESAFE_RESTORE(config_toolbar_move_window);
	_CONFIG_LANESAFE_RESTORE(config_windowed_hide_cursor);
	_CONFIG_LANESAFE_RESTORE(config_pixel_info);
	_CONFIG_LANESAFE_RESTORE(config_orientation);
	_CONFIG_LANESAFE_RESTORE(config_title_bar_format);
	_CONFIG_LANESAFE_RESTORE(config_add_command_line_timeout);

	// the two dynamic families the mirror cannot hold.

	// the recent files mru: the paths are owned allocations, so the count
	// goes back to zero and the entries are released (a reset with the
	// count alone would leave the array pointing at rows the count no
	// longer covers).
	{
		int i;

		for(i=0;i<CONFIG_RECENT_FILE_COUNT;i++)
		{
			if (config_recent_files[i])
			{
				mem_free(config_recent_files[i]);

				config_recent_files[i] = 0;
			}
		}

		config_recent_file_count = 0;
	}

	// the resume record: the window rectangle and the maximized flag are
	// deliberately NOT reset - they are this session's window state, not
	// preferences, and throwing them away would move the window under the
	// user on the very click that asked for a settings reset.
	config_last_file[0] = 0;
}

static void _config_lanesafe_note_load(const wchar_t *filename,int state,int anomaly)
{
	if (state == INI_LOAD_FILE_MISSING)
	{
		// nothing to lose at this location, and a fresh profile answers
		// it on every run. the state only speaks when no layer of the load
		// has named a file at all, so a missing appdata layer can never
		// bury a real answer from the install-dir layer.
		if (!config_lanesafe_loaded_ini[0])
		{
			config_last_load_state = CONFIG_LOAD_FILE_MISSING;
		}

		return;
	}

	if (state == INI_LOAD_UNREADABLE)
	{
		config_last_load_state = CONFIG_LOAD_UNREADABLE;
	}
	else
	if (anomaly)
	{
		config_last_load_state = CONFIG_LOAD_PARSE_ANOMALY;
	}
	else
	{
		// a clean file: the exit path names this one, and its backup
		// sibling is the restore source.
		config_last_load_state = CONFIG_LOAD_OK;
	}

	string_copy(config_lanesafe_loaded_ini,filename);

	string_copy(config_lanesafe_auditbak,filename);
	string_cat_utf8(config_lanesafe_auditbak,(const utf8_t *)".auditbak");
}

// the one atomic copy (see the note in config.h): read the source, write
// a fresh temp beside the target, flush it through, close it, then move
// it into place. answers 1 when the target now holds the source's bytes.
//
// the source is read into memory rather than streamed, because the only
// things ever copied here are the settings file and its backup: both are
// a few kilobytes and the ini reader already refuses anything past
// 16 mb, so the ceiling below is the same answer for both callers and
// there is no chunk loop to get wrong.
int config_lanesafe_atomic_copy(const wchar_t *from,const wchar_t *to)
{
	char *buf;
	wchar_t tempname[STRING_SIZE];
	DWORD numread;
	DWORD numwritten;
	DWORD size;
	LARGE_INTEGER file_size;
	HANDLE src;
	HANDLE h;

	src = CreateFile(from,GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE,0,OPEN_EXISTING,FILE_FLAG_SEQUENTIAL_SCAN,0);
	if (src == INVALID_HANDLE_VALUE)
	{
		debug_printf("config: the copy source refused to open: %S (error %u)\r\n",from,GetLastError());

		return 0;
	}

	if ((!GetFileSizeEx(src,&file_size)) || (file_size.QuadPart <= 0) || (file_size.QuadPart > 0x1000000))
	{
		debug_printf("config: the copy source size is out of range - nothing copied: %S\r\n",from);

		CloseHandle(src);

		return 0;
	}

	size = (DWORD)file_size.QuadPart;

	buf = mem_alloc(safe_size_add_one(size));

	if (!ReadFile(src,buf,size,&numread,NULL))
	{
		debug_printf("config: the copy source read failed: %S (error %u)\r\n",from,GetLastError());

		mem_free(buf);
		CloseHandle(src);

		return 0;
	}

	CloseHandle(src);

	// the temp rides the process id and the create refuses to follow: a
	// fixed name under CREATE_NEW could be preempted by a concurrent
	// instance or by a reparse point planted at the predictable path.
	string_copy(tempname,to);
	string_cat_utf8(tempname,(const utf8_t *)".tmp");

	{
		wchar_t pid_wbuf[32];

		string_format_number(pid_wbuf,GetCurrentProcessId());
		string_cat(tempname,pid_wbuf);
	}

	h = CreateFile(tempname,GENERIC_WRITE,FILE_SHARE_READ,0,CREATE_NEW,FILE_ATTRIBUTE_TEMPORARY,0);

	if (h == INVALID_HANDLE_VALUE)
	{
		// a stale temp left by a killed run is the expected case, not an
		// error: take the name over rather than give up on the copy.
		DeleteFile(tempname);

		h = CreateFile(tempname,GENERIC_WRITE,FILE_SHARE_READ,0,CREATE_NEW,FILE_ATTRIBUTE_TEMPORARY,0);
	}

	if (h == INVALID_HANDLE_VALUE)
	{
		debug_printf("config: the copy temp file refused to open: %S (error %u)\r\n",tempname,GetLastError());

		mem_free(buf);

		return 0;
	}

	numwritten = 0;

	if ((!WriteFile(h,buf,numread,&numwritten,0)) || (numwritten != numread))
	{
		debug_printf("config: the copy write failed - the previous file is kept (error %u)\r\n",GetLastError());

		CloseHandle(h);
		DeleteFile(tempname);
		mem_free(buf);

		return 0;
	}

	mem_free(buf);

	// the flush before the close is the belt: the move below would
	// otherwise answer before the data reached the medium.
	FlushFileBuffers(h);

	CloseHandle(h);

	if (!MoveFileExW(tempname,to,MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
	{
		debug_printf("config: the copy replace failed - the temp file keeps the bytes (error %u)\r\n",GetLastError());

		return 0;
	}

	return 1;
}

// the fixed digits of one stamp field. a field out of range (a machine
// whose clock was never set) is written as zeros rather than trusted:
// the stamp only has to separate two copies of the same file.
static void _config_lanesafe_stamp_field(wchar_t *wbuf,int value,int digits)
{
	int i;

	for(i=digits;i>0;i--)
	{
		int digit;

		digit = value % 10;

		if (digit < 0)
		{
			digit = 0;
		}

		value /= 10;

		wbuf[i - 1] = (wchar_t)(L'0' + digit);
	}

	wbuf[digits] = 0;
}

// "<ini>.corrupt.YYYYMMDD-HHMMSS" - the name a damaged settings file
// is preserved under. local time, fixed width: no locale, and no
// separator a file system or a shell might object to. answers 0 when
// the name does not fit the caller's buffer, or when the settings file
// name is empty.
int config_lanesafe_corrupt_path(const wchar_t *ini_filename,wchar_t *wbuf,SIZE_T bufsize)
{
	wchar_t stamp_wbuf[32];
	SYSTEMTIME st;

	if ((!ini_filename) || (!ini_filename[0]) || (!bufsize))
	{
		return 0;
	}

	// the whole name, worst case: the ini path plus the 9 character
	// ".corrupt." plus the 15 character stamp and the terminator.
	if (string_get_length(ini_filename) + 25 > (uintptr_t)bufsize)
	{
		return 0;
	}

	// GetLocalTime, not GetSystemTime: a user reading the directory
	// beside their own settings file reads the stamp against their own
	// wall clock.
	GetLocalTime(&st);

	string_copy(wbuf,ini_filename);
	string_cat_utf8(wbuf,(const utf8_t *)".corrupt.");

	_config_lanesafe_stamp_field(stamp_wbuf,(int)st.wYear,4);
	string_cat(wbuf,stamp_wbuf);
	string_cat_utf8(wbuf,(const utf8_t *)"-");

	_config_lanesafe_stamp_field(stamp_wbuf,(int)st.wMonth,2);
	string_cat(wbuf,stamp_wbuf);
	_config_lanesafe_stamp_field(stamp_wbuf,(int)st.wDay,2);
	string_cat(wbuf,stamp_wbuf);
	string_cat_utf8(wbuf,(const utf8_t *)"-");

	_config_lanesafe_stamp_field(stamp_wbuf,(int)st.wHour,2);
	string_cat(wbuf,stamp_wbuf);
	_config_lanesafe_stamp_field(stamp_wbuf,(int)st.wMinute,2);
	string_cat(wbuf,stamp_wbuf);
	_config_lanesafe_stamp_field(stamp_wbuf,(int)st.wSecond,2);
	string_cat(wbuf,stamp_wbuf);

	return 1;
}

// is the last-known-good sibling the load recorded there to restore
// from? the exit ask only offers the restore when it is - an offer that
// fails is worse than no offer.
int config_lanesafe_auditbak_exists(void)
{
	DWORD attributes;

	if (!config_lanesafe_loaded_ini[0])
	{
		return 0;
	}

	attributes = GetFileAttributes(config_lanesafe_auditbak);

	return ((attributes != INVALID_FILE_ATTRIBUTES) && (!(attributes & FILE_ATTRIBUTE_DIRECTORY))) ? 1 : 0;
}

// the last-known-good backup, taken BEFORE the parse.
//
// the order is the whole point: a crash, a refusal or a power cut between
// the copy and the read still leaves the user's settings recoverable,
// which is the only reason this file exists. the write rides the one
// atomic copy above (a unique temp beside the target, written through a
// handle this code owns, flushed, closed, then moved into place) -
// CopyFileW would not do: it closes its own destination handle (so there
// is nothing to flush) and bFailIfExists = FALSE destroys the previous
// good backup before it knows the new one landed, which is the one
// failure mode a "known good" copy must never have.
//
// the pre-check gates the whole thing: a file that cannot be good (no
// section header, an embedded nul, a size past any real ini) never takes
// the backup seat, or the restore would hand the damage back as the good
// copy.
static void _config_lanesafe_auditbak_write(const wchar_t *filename)
{
	wchar_t backup[STRING_SIZE];

	if (!ini_precheck(filename,(const utf8_t *)"voidImageViewer"))
	{
		return;
	}

	string_copy(backup,filename);
	string_cat_utf8(backup,(const utf8_t *)".auditbak");

	config_lanesafe_atomic_copy(filename,backup);
}


// ---- the corrupted-settings gate at the exit (lane-B) ----
//
// the ini reader is tolerant by design - it ignores what it does not
// understand - so a half-written or a cloud-synced file parses to partial
// garbage and the rest of the session quietly runs on compiled-in
// defaults. the exit path used to write that back unconditionally, which
// is how a user's themes, shortcuts, recent files and associations were
// replaced by defaults without a word. config_last_load_state is what
// lets the exit ask first.
//
// it lives here, beside the settings file it is about, and not in
// viv.c: the splice growth window in the guard suite counts every
// viv_*.c, and this is settings-file integrity work - it reads the load
// state, the loaded file name and the backup name that all live in this
// file. the exit path calls one function.
//
// the answers, and what each one does:
//
//   keep            the defaults this session ran on are exactly what the
//                   user saw, so they are written back. the damaged
//                   original is already preserved under its .corrupt.
//                   stamp (taken BEFORE the ask, because the write the
//                   gate protects is the very next statement).
//   restore backup  the last known good copy goes back over the damaged
//                   file, and this exit writes nothing: the user's real
//                   settings return on the next launch instead of being
//                   replaced by defaults the moment they agreed to keep
//                   the old file.
//   cancel          write nothing at all. the file on disk is left
//                   exactly as the next run will find it.
//
// the three faces: the ask needs three buttons and the MessageBox subset
// the rest of the app uses tops out at two, so the box is the same themed
// dialog through its three-button form (viv_msgbox3). index 0 is the
// right most face (the primary slot), so the reading order on screen is
// CANCEL, RESTORE BACKUP, KEEP - and CANCEL is the accent and the default
// focus, because it is the only one of the three that loses nothing.
//
// the faces are plain literals rather than localization ids for the same
// reason lane-A's status-bar skip line is: the localization tail is
// pinned by name by the guard suite (menu_structure_test.py checks the
// last 91 ids), and that suite is not this change's to rewrite. the
// cancel face is the one string the table already owns, so the ask
// speaks the same word as every other box in either language. moving the
// other two into the tables is a mechanical follow-up whenever the tail
// is next extended.
#define _CONFIG_CORRUPT_KEEP			IDOK
#define _CONFIG_CORRUPT_RESTORE		IDYES
#define _CONFIG_CORRUPT_CANCEL		IDCANCEL

// the caption and the three faces are wide (they go straight to the box);
// the message is a narrow literal because string_printf's format is the
// utf-8 form, and the text is plain ascii either way.
static const wchar_t *_CONFIG_CORRUPT_CAPTION = L"Settings file problem";
static const char *_CONFIG_CORRUPT_KEEP_FACE = "Keep";
static const char *_CONFIG_CORRUPT_RESTORE_FACE = "Restore backup";

// two %S: the settings file, and the preserved copy of it. the second
// one is empty when the copy itself was refused, and an empty %S names
// nothing at all - better than a line that ends in a bare "%S".
static const char *_CONFIG_CORRUPT_MESSAGE =
	"The settings file\n\n%S\n\ncould not be read cleanly, so this session ran on the built-in defaults. A copy of the original has been kept as\n\n%S\n\n"
	"Choose Restore backup to bring back the last settings that loaded cleanly, Keep to save the defaults this session ran on, or Cancel to leave the file untouched.";

// the three-button ask.
static int _config_lanesafe_corrupt_reply(HWND parent)
{
	wchar_t caption_wbuf[STRING_SIZE];
	wchar_t message_wbuf[STRING_SIZE];
	wchar_t corrupt_wbuf[STRING_SIZE];
	wchar_t keep_wbuf[STRING_SIZE];
	wchar_t restore_wbuf[STRING_SIZE];
	wchar_t cancel_wbuf[STRING_SIZE];
	const wchar_t *labels[3];
	int ids[3];
	int reply;

	// the preserved copy goes into the text, so the user has read the name
	// their damaged settings now live under even if the restore later
	// refuses.
	corrupt_wbuf[0] = 0;

	if (config_lanesafe_loaded_ini[0])
	{
		if (!config_lanesafe_corrupt_path(config_lanesafe_loaded_ini,corrupt_wbuf,STRING_SIZE))
		{
			corrupt_wbuf[0] = 0;
		}
	}

	string_copy(caption_wbuf,_CONFIG_CORRUPT_CAPTION);

	if (corrupt_wbuf[0])
	{
		string_printf(message_wbuf,_CONFIG_CORRUPT_MESSAGE,config_lanesafe_loaded_ini,corrupt_wbuf);
	}
	else
	{
		string_printf(message_wbuf,_CONFIG_CORRUPT_MESSAGE,config_lanesafe_loaded_ini,L"");
	}

	string_copy_utf8_string(keep_wbuf,(const utf8_t *)_CONFIG_CORRUPT_KEEP_FACE);
	string_copy_utf8_string(restore_wbuf,(const utf8_t *)_CONFIG_CORRUPT_RESTORE_FACE);
	string_copy_utf8_string(cancel_wbuf,localization_get_string(LOCALIZATION_ID_SETTINGS_CANCEL));

	labels[0] = keep_wbuf;
	labels[1] = restore_wbuf;
	labels[2] = cancel_wbuf;

	ids[0] = _CONFIG_CORRUPT_KEEP;
	ids[1] = _CONFIG_CORRUPT_RESTORE;
	ids[2] = _CONFIG_CORRUPT_CANCEL;

	if (config_lanesafe_auditbak_exists())
	{
		reply = viv_msgbox3(parent,caption_wbuf,message_wbuf,MB_ICONWARNING,labels,ids,3,2,2);
	}
	else
	{
		// no backup on disk: the restore face is not offered at all (an
		// offer that fails on click is worse than no offer), and the two
		// faces that remain are KEEP and CANCEL with CANCEL as the default.
		reply = viv_msgbox3(parent,caption_wbuf,message_wbuf,MB_ICONWARNING,labels,ids,2,1,1);
	}

	if (reply == _CONFIG_CORRUPT_RESTORE)
	{
		return _CONFIG_CORRUPT_RESTORE;
	}

	if (reply == _CONFIG_CORRUPT_KEEP)
	{
		return _CONFIG_CORRUPT_KEEP;
	}

	return _CONFIG_CORRUPT_CANCEL;
}

// may this exit go on to write the settings file?
//
// 1 = yes (the load was clean, there was no file at all, or the user said
// keep). 0 = no: the user cancelled, or asked for the backup back - in
// both of those cases the file on disk is one the user chose, and writing
// over it is exactly the loss this whole path exists to stop.
//
// _viv_exit is the one caller: it runs this before config_save_settings.
int config_lanesafe_exit_gate(HWND parent)
{
	wchar_t corrupt_wbuf[STRING_SIZE];
	int reply;

	// the two states that mean "the session ran on something other than
	// what the file says". a missing file is every fresh profile and a
	// clean load is the normal case; neither asks anything.
	if ((config_last_load_state != CONFIG_LOAD_PARSE_ANOMALY) && (config_last_load_state != CONFIG_LOAD_UNREADABLE))
	{
		return 1;
	}

	// no file was named: nothing to preserve and nothing to ask about.
	if (!config_lanesafe_loaded_ini[0])
	{
		return 1;
	}

	// the preservation comes FIRST. the write this gate protects is the
	// very next statement of the exit, so a copy taken after the ask would
	// be a copy of a file the ask could no longer recover. it is the same
	// atomic temp+flush+MoveFileExW copy the backup ride - never
	// CopyFileW, which closes its own destination handle (so there is
	// nothing to flush) and with bFailIfExists = FALSE destroys the
	// previous file before it knows the new one landed.
	if (config_lanesafe_corrupt_path(config_lanesafe_loaded_ini,corrupt_wbuf,STRING_SIZE))
	{
		if (!config_lanesafe_atomic_copy(config_lanesafe_loaded_ini,corrupt_wbuf))
		{
			// the copy refused. the ask would then name a preserved file
			// that is not there, and writing the defaults over the damaged
			// original is the one outcome that loses it for good - so
			// nothing is written and the next run asks again.
			debug_printf("exit: the damaged settings file could not be preserved as %S - nothing written\r\n",corrupt_wbuf);

			return 0;
		}
	}

	reply = _config_lanesafe_corrupt_reply(parent);

	if (reply == _CONFIG_CORRUPT_CANCEL)
	{
		debug_printf("exit: the settings file was left as it is (cancel)\r\n");

		return 0;
	}

	if (reply == _CONFIG_CORRUPT_RESTORE)
	{
		// the restore is the atomic copy in the other direction: the last
		// known good file takes the damaged file's place in one move, so a
		// power cut here leaves either the damaged file (ask again) or the
		// good one - never a half written mixture of the two.
		if (!config_lanesafe_atomic_copy(config_lanesafe_auditbak,config_lanesafe_loaded_ini))
		{
			// the restore refused. writing the defaults over the damaged
			// file is exactly the loss this gate exists to prevent, so
			// nothing is written at all.
			debug_printf("exit: the settings backup could not be restored - nothing written\r\n");

			return 0;
		}

		debug_printf("exit: the settings backup was restored - nothing written\r\n");

		return 0;
	}

	return 1;
}

static void _config_load_settings_by_location(const wchar_t *path,int is_root)
{
	ini_t *ini;
	wchar_t filename[STRING_SIZE];
	
	string_path_combine_utf8(filename,path,(const utf8_t *)"voidImageViewer.ini");
	
	// the last-known-good copy, before the parse (see the helper).
	_config_lanesafe_auditbak_write(filename);
	
	ini = ini_open(filename,"voidImageViewer");
	
	// what the load found, for the exit path. recorded before the keys are
	// read: the anomaly is a property of the file, not of the keys that
	// happened to survive it.
	_config_lanesafe_note_load(filename,ini_get_last_state(),ini_get_last_anomaly());
	
	if (ini)
	{
		config_x = ini_get_int(ini,(const utf8_t *)"x",config_x);
		config_y = ini_get_int(ini,(const utf8_t *)"y",config_y);
		config_wide = ini_get_int(ini,(const utf8_t *)"wide",config_wide);
		config_high = ini_get_int(ini,(const utf8_t *)"high",config_high);
		config_maximized = ini_get_int(ini,(const utf8_t *)"maximized",config_maximized);
		config_slideshow_rate = config_clamp_slideshow_rate(ini_get_int(ini,(const utf8_t *)"slideshow_rate",config_slideshow_rate));
		config_allow_shrinking = ini_get_int(ini,(const utf8_t *)"allow_shrinking",config_allow_shrinking);
		config_shrink_blit_mode = ini_get_int(ini,(const utf8_t *)"shrink_blit_mode",config_shrink_blit_mode);
		config_renderer = ini_get_int(ini,(const utf8_t *)"renderer",config_renderer);
		config_toolbar_groups = ini_get_int(ini,(const utf8_t *)"toolbar_groups",config_toolbar_groups);
		config_toolbar_icon_only = ini_get_int(ini,(const utf8_t *)"toolbar_icon_only",config_toolbar_icon_only);
		config_mag_filter = ini_get_int(ini,(const utf8_t *)"mag_filter",config_mag_filter);
		config_keep_aspect_ratio = ini_get_int(ini,(const utf8_t *)"keep_aspect_ratio",config_keep_aspect_ratio);
		config_fill_window = ini_get_int(ini,(const utf8_t *)"fill_window",config_fill_window);
		config_fullscreen_fill_window = ini_get_int(ini,(const utf8_t *)"fullscreen_fill_window",config_fullscreen_fill_window);
		config_nav_sort = ini_get_int(ini,(const utf8_t *)"sort",config_nav_sort);
		config_nav_sort_ascending = ini_get_int(ini,(const utf8_t *)"sort_ascending",config_nav_sort_ascending);
		config_multiple_instances = ini_get_int(ini,(const utf8_t *)"multiple_instances",config_multiple_instances);
		config_show_caption = ini_get_int(ini,(const utf8_t *)"show_caption",config_show_caption);
		config_show_thickframe = ini_get_int(ini,(const utf8_t *)"show_thickframe",config_show_thickframe);
		config_show_menu = ini_get_int(ini,(const utf8_t *)"show_menu",config_show_menu);
		config_show_status = ini_get_int(ini,(const utf8_t *)"show_status",config_show_status);
		config_pixel_info = ini_get_int(ini,(const utf8_t *)"statusbar_pixel_info",config_pixel_info);
		config_show_controls = ini_get_int(ini,(const utf8_t *)"show_controls",config_show_controls);
		// the standard layout is the default: the floating pill stays off
		// until the View menu or the settings page turns it on (the
		// touch-digitizer first-run default retired with rc.4).
		config_show_zoom_controls = ini_get_int(ini,(const utf8_t *)"show_zoom_controls",config_show_zoom_controls);
		// the one-time layout migration: a profile saved by an earlier
		// build could only carry show_zoom_controls=1 from the retired
		// touch-digitizer first-run default (every exit saves the whole
		// ini), and the field ask is that an upgrade never opens with
		// the pill - so the first load without the marker resets the
		// flag to the standard layout. the save stamps the marker, so a
		// deliberate enable on this build or later survives every
		// subsequent load untouched.
		if (ini_get_int(ini,(const utf8_t *)"layout_migration",0) == 0)
		{
			config_show_zoom_controls = 0;
		}
			config_zoom_auto_hide = ini_get_int(ini,(const utf8_t *)"zoom_auto_hide",config_zoom_auto_hide);
		
		// ui language. stored as a readable string: auto, english or chinese.
		{
			const utf8_t *language_value;
			
			language_value = ini_get_string(ini,(const utf8_t *)"language");
			
			if (language_value)
			{
				if (_config_icompare_ascii(language_value,"english") == 0)
				{
					config_language = 1;
				}
				else
				if (_config_icompare_ascii(language_value,"chinese") == 0)
				{
					config_language = 2;
				}
				else
				{
					// "auto" (and anything we don't understand) follows the system language.
					config_language = 0;
				}
			}
		}

	// the last session's file: the resume record. an absent key keeps
	// whatever is in the buffer (a fresh start's emptiness).
	{
		const utf8_t *last_file_value;

		last_file_value = ini_get_string(ini,(const utf8_t *)"last_file");

		if (last_file_value)
		{
			string_copy_utf8_string(config_last_file,last_file_value);
		}
	}
		
		// dark mode. stored as a readable string: auto, dark or light.
		{
			const utf8_t *dark_mode_value;
			
			dark_mode_value = ini_get_string(ini,(const utf8_t *)"dark_mode");
			
			if (dark_mode_value)
			{
				if (_config_icompare_ascii(dark_mode_value,"dark") == 0)
				{
					config_dark_mode = 1;
				}
				else
				if (_config_icompare_ascii(dark_mode_value,"light") == 0)
				{
					config_dark_mode = 0;
				}
				else
				{
					// "auto" (and anything we don't understand) follows the windows theme.
					config_dark_mode = 2;
				}
			}
		}

		// accent color. an index into the viv_theme accent table, clamped.
		config_ui_accent = (BYTE)ini_get_int(ini,(const utf8_t *)"accent_color",0);
		
		if (config_ui_accent >= VIV_THEME_ACCENT_COUNT)
		{
			config_ui_accent = 0;
		}
		config_backdrop_mode = ini_get_int(ini,(const utf8_t *)"backdrop_mode",config_backdrop_mode);
		config_backdrop_color_r = ini_get_int(ini,(const utf8_t *)"backdrop_color_r",config_backdrop_color_r);
		config_backdrop_color_g = ini_get_int(ini,(const utf8_t *)"backdrop_color_g",config_backdrop_color_g);
		config_backdrop_color_b = ini_get_int(ini,(const utf8_t *)"backdrop_color_b",config_backdrop_color_b);
		
		// recent files mru: recent_file_0 .. recent_file_9 hold the most
		// recently opened paths, newest first. skip empty slots and duplicate
		// paths, preserving the first occurrence and the remaining order.
		// the install-dir and appdata ini layers can both contribute entries.
		{
			int i;
			
			for(i=0;i<CONFIG_RECENT_FILE_COUNT;i++)
			{
				utf8_t key_buf[32];
				const utf8_t *recent_value;
				
				_config_recent_key_name(key_buf,i);
				recent_value = ini_get_string(ini,key_buf);
				
				if ((recent_value) && (*recent_value) && (config_recent_file_count < CONFIG_RECENT_FILE_COUNT))
				{
					wchar_t *recent_filename;
					int j;
					
					recent_filename = string_alloc_utf8(recent_value);
					
					if (recent_filename)
					{
						for(j=0;j<config_recent_file_count;j++)
						{
							if (_viv_icompare_filename(config_recent_files[j],recent_filename) == 0)
							{
								break;
							}
						}

						if (j == config_recent_file_count)
						{
							config_recent_files[config_recent_file_count] = recent_filename;
							config_recent_file_count++;
						}
						else
						{
							mem_free(recent_filename);
						}
					}
				}
			}
		}
		
		// the walk above already stops at the cap; this clamp is the load-side
		// guard of the same invariant (the menu id block, the save loop and the
		// popup builder all assume count <= CONFIG_RECENT_FILE_COUNT), so any
		// future writer that grows the count cannot walk past it.
		if (config_recent_file_count > CONFIG_RECENT_FILE_COUNT)
		{
			config_recent_file_count = CONFIG_RECENT_FILE_COUNT;
		}
		config_auto_zoom = ini_get_int(ini,(const utf8_t *)"auto_zoom",config_auto_zoom);
		config_auto_zoom_type = ini_get_int(ini,(const utf8_t *)"auto_zoom_type",config_auto_zoom_type);
		config_auto_fit_wide_mul = ini_get_int(ini,(const utf8_t *)"auto_fit_wide_mul",config_auto_fit_wide_mul);
		config_auto_fit_wide_div = ini_get_int(ini,(const utf8_t *)"auto_fit_wide_div",config_auto_fit_wide_div);
		config_auto_fit_high_mul = ini_get_int(ini,(const utf8_t *)"auto_fit_high_mul",config_auto_fit_high_mul);
		config_auto_fit_high_div = ini_get_int(ini,(const utf8_t *)"auto_fit_high_div",config_auto_fit_high_div);
		config_frame_minus = ini_get_int(ini,(const utf8_t *)"frame_minus",config_frame_minus);
		config_mouse_wheel_action = ini_get_int(ini,(const utf8_t *)"mouse_wheel_action",config_mouse_wheel_action);
		config_ctrl_mouse_wheel_action = ini_get_int(ini,(const utf8_t *)"ctrl_mouse_wheel_action",config_ctrl_mouse_wheel_action);
		config_left_click_action = ini_get_int(ini,(const utf8_t *)"left_click_action",config_left_click_action);
		config_right_click_action = ini_get_int(ini,(const utf8_t *)"right_click_action",config_right_click_action);
		config_xbutton_action = ini_get_int(ini,(const utf8_t *)"xbutton_action",config_xbutton_action);
		config_keep_centered = ini_get_int(ini,(const utf8_t *)"keep_centered",config_keep_centered);
		config_windowed_background_color_r = ini_get_int(ini,(const utf8_t *)"windowed_background_color_r",config_windowed_background_color_r);
		config_windowed_background_color_g = ini_get_int(ini,(const utf8_t *)"windowed_background_color_g",config_windowed_background_color_g);
		config_windowed_background_color_b = ini_get_int(ini,(const utf8_t *)"windowed_background_color_b",config_windowed_background_color_b);
		config_windowed_hide_cursor = ini_get_int(ini,(const utf8_t *)"windowed_hide_cursor",config_windowed_hide_cursor);
		config_fullscreen_background_color_r = ini_get_int(ini,(const utf8_t *)"fullscreen_background_color_r",config_fullscreen_background_color_r);
		config_fullscreen_background_color_g = ini_get_int(ini,(const utf8_t *)"fullscreen_background_color_g",config_fullscreen_background_color_g);
		config_fullscreen_background_color_b = ini_get_int(ini,(const utf8_t *)"fullscreen_background_color_b",config_fullscreen_background_color_b);
		config_options_last_page = ini_get_int(ini,(const utf8_t *)"options_last_page",config_options_last_page);
		config_short_jump = ini_get_int(ini,(const utf8_t *)"short_jump",config_short_jump);
		config_medium_jump = ini_get_int(ini,(const utf8_t *)"medium_jump",config_medium_jump);
		config_long_jump = ini_get_int(ini,(const utf8_t *)"long_jump",config_long_jump);
		config_loop_animations_once = ini_get_int(ini,(const utf8_t *)"loop_animations_once",config_loop_animations_once);
		config_prevent_sleep = ini_get_int(ini,(const utf8_t *)"prevent_sleep",config_prevent_sleep);
		config_shuffle = ini_get_int(ini,(const utf8_t *)"shuffle",config_shuffle);
		config_browse_file_open_dialog = ini_get_int(ini,(const utf8_t *)"browse_file_open_dialog",config_browse_file_open_dialog);
		config_ontop = ini_get_int(ini,(const utf8_t *)"ontop",config_ontop);
		config_slideshow_custom_rate = ini_get_int(ini,(const utf8_t *)"slideshow_custom_rate",config_slideshow_custom_rate);
		config_slideshow_custom_rate_type = ini_get_int(ini,(const utf8_t *)"slideshow_custom_rate_type",config_slideshow_custom_rate_type);
		config_scroll_window = ini_get_int(ini,(const utf8_t *)"scroll_window",config_scroll_window);
		config_preload_next = ini_get_int(ini,(const utf8_t *)"preload_next",config_preload_next);
		config_cache_last = ini_get_int(ini,(const utf8_t *)"cache_last",config_cache_last);
		// the counts: a present key wins; an absent one falls back to the
		// legacy pair (on/off), and the clamps hold the range the ring and
		// the chain were built for.
		config_preload_count = ini_get_int(ini,(const utf8_t *)"preload_count",-1);

		if (config_preload_count < 0)
		{
			config_preload_count = config_preload_next ? 1 : 0;
		}

		if (config_preload_count > 5)
		{
			config_preload_count = 5;
		}
		config_cache_count = ini_get_int(ini,(const utf8_t *)"cache_count",-1);

		if (config_cache_count < 0)
		{
			config_cache_count = config_cache_last ? 1 : 0;
		}

		if (config_cache_count > 8)
		{
			config_cache_count = 8;
		}
		config_resume_last_file = ini_get_int(ini,(const utf8_t *)"resume_last_file",config_resume_last_file);
		config_icm = ini_get_int(ini,(const utf8_t *)"icm",config_icm);
		config_orientation = ini_get_int(ini,(const utf8_t *)"orientation",config_orientation);
		config_toolbar_move_window = ini_get_int(ini,(const utf8_t *)"toolbar_move_window",config_toolbar_move_window);
		config_title_bar_format = ini_get_int(ini,(const utf8_t *)"title_bar_format",config_title_bar_format);
		config_add_command_line_timeout = ini_get_int(ini,(const utf8_t *)"add_command_line_timeout",config_add_command_line_timeout);

		if (is_root)
		{
			config_appdata = ini_get_int(ini,(const utf8_t *)(const utf8_t *)"appdata",config_appdata);
		}

		{
			int i;
			
			for(i=0;i<viv_get_command_count();i++)
			{
				utf8_t key_buf[STRING_SIZE];
				
				// get ini key name.
				if (viv_menu_name_to_ini_name(key_buf,i))
				{
					const utf8_t *key_list;
					const utf8_t *p;
					
					key_list = ini_get_string(ini,key_buf);
					// the rc.13 rename changed this command's generated
					// ini name (the english text drives it): a binding
					// saved under the old name migrates rather than
					// silently losing itself (the upgrade the field
					// report already paid once for the cache seat).
					if ((!key_list) && (_config_icompare_ascii(key_buf,"view_layout_floating_control_bar_keys") == 0))
					{
						key_list = ini_get_string(ini,(const utf8_t *)"view_layout_zoom_controls_keys");
					}
					
					// the rc.14 rename changed file_settings the same way
					// (options became settings in the english text that
					// drives the generated name): a binding saved under
					// the old name migrates rather than silently losing
					// itself.
					if ((!key_list) && (_config_icompare_ascii(key_buf,"file_settings_keys") == 0))
					{
						key_list = ini_get_string(ini,(const utf8_t *)"file_options_keys");
					}
					
					if (key_list)
					{
						p = key_list;
						
						// clear this key.
						viv_key_clear_all(i);
						
						while(*p)
						{
							const utf8_t *start;
							
							start = p;

							while(*p)
							{
								if (*p == ',')
								{
									*p++;
									break;
								}
								
								p++;
							}
						
							viv_key_add(i,utf8_to_int(start));
						}
					}
				}
			}
		}

		ini_close(ini);
	}
}

void config_load_settings(void)
{
	wchar_t path[STRING_SIZE];
	
	// the compiled-in defaults are captured before anything below can write
	// a config global: at this point every one of them still carries its
	// initialiser, so the mirror is the initialiser block itself (see the
	// capture helper).
	_config_lanesafe_defaults_capture();
	
	string_get_exe_path(path);
	
	_config_load_settings_by_location(path,1);
		
	if (config_appdata)
	{
		if (string_get_appdata_voidimageviewer_path(path))
		{
			_config_load_settings_by_location(path,0);
		}
	}
}


static int _config_icompare_ascii(const utf8_t *s1,const char *lowercase_ascii_s2)
{
	// case-insensitive compare of an ascii/utf-8 string against a lowercase ascii keyword.
	// returns 0 when equal.
	
	while (*s1 && *lowercase_ascii_s2)
	{
		char c;
		
		c = (char)*s1;
		
		if ((c >= 'A') && (c <= 'Z'))
		{
			c += 'a' - 'A';
		}
		
		if (c != *lowercase_ascii_s2)
		{
			return 1;
		}
		
		s1++;
		lowercase_ascii_s2++;
	}
	
	if (*s1 || *lowercase_ascii_s2)
	{
		return 1;
	}
	
	return 0;
}

static void _config_write_int(HANDLE h,const char *ascii_key,int value)
{
	wchar_t wbuf[STRING_SIZE];
	
	string_format_number(wbuf,value);
	
	_config_write_string(h,ascii_key,wbuf);
}

static void _config_write_string(HANDLE h,const char *ascii_key,const wchar_t *s)
{
	utf8_t buf[STRING_SIZE*3];
	int ret;
	
	// check the conversion: on failure the buffer is left unterminated and
	// the writes below would read past its end (a full-width string can
	// hit the boundary: STRING_SIZE wchars x 3 utf8 bytes + nul).
	ret = WideCharToMultiByte(CP_UTF8,0,s,-1,(char *)buf,STRING_SIZE*3,0,0);
	
	if ((ret <= 0) || (ret >= (int)sizeof(buf)))
	{
		buf[0] = 0;
	}
	
	_config_write_utf8(h,(const utf8_t *)ascii_key);
	_config_write_utf8(h,(const utf8_t *)"=");
	_config_write_utf8(h,buf);
	_config_write_utf8(h,(const utf8_t *)"\r\n");
}

static void _config_recent_key_name(utf8_t *buf,int index)
{
	// "recent_file_0" .. "recent_file_9"
	buf[0] = 'r';
	buf[1] = 'e';
	buf[2] = 'c';
	buf[3] = 'e';
	buf[4] = 'n';
	buf[5] = 't';
	buf[6] = '_';
	buf[7] = 'f';
	buf[8] = 'i';
	buf[9] = 'l';
	buf[10] = 'e';
	buf[11] = '_';
	buf[12] = (utf8_t)('0' + (index % 10));
	buf[13] = 0;
}

// the write latch: a refused or short WriteFile anywhere in a save sets
// this, and the replace below refuses to run with it set - a half-written
// temp file must never take the place of the last good ini (the old shape
// ignored the return value entirely, so a full disk, a locked file or a
// vanishing share silently dropped the user's settings).
static int _config_write_failed = 0;

static void _config_write_utf8(HANDLE h,const utf8_t *s)
{
	DWORD num_written;
	DWORD write_length;
	
	write_length = utf8_length(s);
	
	if ((!WriteFile(h,s,write_length,&num_written,0)) || (num_written != write_length))
	{
		_config_write_failed = 1;
	}
}

static void _config_save_settings_by_location(const wchar_t *path,int is_root)
{
	HANDLE h;
	wchar_t tempname[STRING_SIZE];
	wchar_t filename[STRING_SIZE];
	
	_config_write_failed = 0;
	
	string_path_combine_utf8(filename,path,(const utf8_t *)"voidImageViewer.ini");

	// the temp name rides the process id and the create refuses to
	// follow: a fixed ".tmp" under CREATE_ALWAYS could be preempted by
	// a concurrent instance or a reparse point planted at the
	// predictable path. the unique name removes the collision, and
	// CREATE_NEW leaves anything already sitting there untouched - a
	// stale name from a crashed run is deleted once before the retry.
	string_copy(tempname,filename);
	string_cat_utf8(tempname,(const utf8_t *)".");
	{
		wchar_t pid_wbuf[32];

		string_format_number(pid_wbuf,GetCurrentProcessId());
		string_cat(tempname,pid_wbuf);
	}
	string_cat_utf8(tempname,(const utf8_t *)".tmp");

	h = CreateFile(tempname,GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE,0,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,0);

	if (h == INVALID_HANDLE_VALUE)
	{
		DeleteFile(tempname);

		h = CreateFile(tempname,GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE,0,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,0);
	}
	if (h != INVALID_HANDLE_VALUE)
	{
		_config_write_utf8(h,(const utf8_t *)"[voidImageViewer]\r\n");
		
		if ((is_root) && (config_appdata))
		{
			_config_write_int(h,"appdata",config_appdata);
		}
		else
		{
			_config_write_int(h,"x",config_x);
			_config_write_int(h,"y",config_y);
			_config_write_int(h,"wide",config_wide);
			_config_write_int(h,"high",config_high);
			_config_write_int(h,"maximized",config_maximized);
			_config_write_int(h,"slideshow_rate",config_slideshow_rate);
			_config_write_int(h,"allow_shrinking",config_allow_shrinking);
			_config_write_int(h,"shrink_blit_mode",config_shrink_blit_mode);
			_config_write_int(h,"renderer",config_renderer);
			_config_write_int(h,"toolbar_groups",config_toolbar_groups);
			_config_write_int(h,"toolbar_icon_only",config_toolbar_icon_only);
			_config_write_int(h,"mag_filter",config_mag_filter);
			_config_write_int(h,"keep_aspect_ratio",config_keep_aspect_ratio);
			_config_write_int(h,"fill_window",config_fill_window);
			_config_write_int(h,"fullscreen_fill_window",config_fullscreen_fill_window);
			_config_write_int(h,"sort",config_nav_sort);
			_config_write_int(h,"sort_ascending",config_nav_sort_ascending);
			_config_write_int(h,"multiple_instances",config_multiple_instances);
			_config_write_int(h,"show_caption",config_show_caption);
			_config_write_int(h,"show_thickframe",config_show_thickframe);
			_config_write_int(h,"show_menu",config_show_menu);
			_config_write_int(h,"show_status",config_show_status);
			_config_write_int(h,"statusbar_pixel_info",config_pixel_info);
			_config_write_int(h,"show_controls",config_show_controls);
			_config_write_int(h,"show_zoom_controls",config_show_zoom_controls);
			_config_write_int(h,"layout_migration",1);
				_config_write_int(h,"zoom_auto_hide",config_zoom_auto_hide);
			_config_write_string(h,"language",config_language == 1 ? L"english" : (config_language == 2 ? L"chinese" : L"auto"));
		_config_write_string(h,"dark_mode",config_dark_mode == 1 ? L"dark" : (config_dark_mode == 0 ? L"light" : L"auto"));
		_config_write_int(h,"accent_color",config_ui_accent);
		_config_write_int(h,"backdrop_mode",config_backdrop_mode);
		_config_write_int(h,"backdrop_color_r",config_backdrop_color_r);
		_config_write_int(h,"backdrop_color_g",config_backdrop_color_g);
		_config_write_int(h,"backdrop_color_b",config_backdrop_color_b);
			_config_write_int(h,"auto_zoom",config_auto_zoom);
			_config_write_int(h,"auto_zoom_type",config_auto_zoom_type);
			_config_write_int(h,"auto_fit_wide_mul",config_auto_fit_wide_mul);
			_config_write_int(h,"auto_fit_wide_div",config_auto_fit_wide_div);
			_config_write_int(h,"auto_fit_high_mul",config_auto_fit_high_mul);
			_config_write_int(h,"auto_fit_high_div",config_auto_fit_high_div);
			_config_write_int(h,"frame_minus",config_frame_minus);
			_config_write_int(h,"mouse_wheel_action",config_mouse_wheel_action);
			_config_write_int(h,"ctrl_mouse_wheel_action",config_ctrl_mouse_wheel_action);
			_config_write_int(h,"left_click_action",config_left_click_action);
			_config_write_int(h,"right_click_action",config_right_click_action);
			_config_write_int(h,"xbutton_action",config_xbutton_action);
			_config_write_int(h,"keep_centered",config_keep_centered);
			_config_write_int(h,"windowed_background_color_r",config_windowed_background_color_r);
			_config_write_int(h,"windowed_background_color_g",config_windowed_background_color_g);
			_config_write_int(h,"windowed_background_color_b",config_windowed_background_color_b);
			_config_write_int(h,"windowed_hide_cursor",config_windowed_hide_cursor);
			_config_write_int(h,"fullscreen_background_color_r",config_fullscreen_background_color_r);
			_config_write_int(h,"fullscreen_background_color_g",config_fullscreen_background_color_g);
			_config_write_int(h,"fullscreen_background_color_b",config_fullscreen_background_color_b);
			_config_write_int(h,"options_last_page",config_options_last_page);
			_config_write_int(h,"short_jump",config_short_jump);
			_config_write_int(h,"medium_jump",config_medium_jump);
			_config_write_int(h,"long_jump",config_long_jump);
			_config_write_int(h,"loop_animations_once",config_loop_animations_once);
			_config_write_int(h,"prevent_sleep",config_prevent_sleep);
			_config_write_int(h,"shuffle",config_shuffle);
			_config_write_int(h,"browse_file_open_dialog",config_browse_file_open_dialog);
			_config_write_int(h,"ontop",config_ontop);
			_config_write_int(h,"slideshow_custom_rate",config_slideshow_custom_rate);
			_config_write_int(h,"slideshow_custom_rate_type",config_slideshow_custom_rate_type);
			_config_write_int(h,"scroll_window",config_scroll_window);
			_config_write_int(h,"preload_count",config_preload_count);
			_config_write_int(h,"cache_count",config_cache_count);
			// the legacy pair stays in step (an older build reading the ini
			// still sees the right on/off; it just loses the counts).
			_config_write_int(h,"preload_next",config_preload_count >= 1);
			_config_write_int(h,"cache_last",config_cache_count >= 1);
			_config_write_int(h,"resume_last_file",config_resume_last_file);
			_config_write_string(h,"last_file",config_last_file);
			_config_write_int(h,"icm",config_icm);
			_config_write_int(h,"orientation",config_orientation);
			_config_write_int(h,"toolbar_move_window",config_toolbar_move_window);
			_config_write_int(h,"title_bar_format",config_title_bar_format);
			_config_write_int(h,"add_command_line_timeout",config_add_command_line_timeout);
				
				// save the recent files mru: filled slots carry paths, the rest
				// are written empty so removed entries do not survive a reload.
				{
					int i;
					
					for(i=0;i<CONFIG_RECENT_FILE_COUNT;i++)
					{
						utf8_t key_buf[32];
						
						_config_recent_key_name(key_buf,i);
						
						_config_write_string(h,key_buf,(i < config_recent_file_count) ? config_recent_files[i] : L"");
					}
				}
					
			// save keys
			{
				int i;
				
				for(i=0;i<viv_get_command_count();i++)
				{
					utf8_t key_buf[STRING_SIZE];
					
					if (viv_menu_name_to_ini_name(key_buf,i))
					{
						wchar_t keylist[STRING_SIZE];
						config_key_t *key;
						
						// get ini key name.
						
						key = viv_key_get_start(i);
						*keylist = 0;
						
						while(key)
						{
							wchar_t keyidbuf[STRING_SIZE];
							
							string_printf(keyidbuf,"%d",key->key);

							if (*keylist)							
							{
								string_cat_utf8(keylist,(const utf8_t *)",");
							}

							string_cat(keylist,keyidbuf);
						
							key = key->next;
						}

						_config_write_string(h,key_buf,keylist);
					}
				}
			}	

			// save context menu items
//			_config_write_int(h,"context_menu_items",);
		}

		// the flush belt: the write-through flag on the move below
		// keeps the replace from answering before the data reaches
		// the media, and the flush keeps a lazy cache from losing
		// the temp file's tail between the close and the move.
		if (!_config_write_failed)
		{
			FlushFileBuffers(h);
		}
		
		CloseHandle(h);
		
		if (_config_write_failed)
		{
			// a write failed mid-save: the ini keeps the last good
			// state, the temp file keeps the partial write as the
			// evidence, and the log line answers the "where did my
			// settings go" question the silent path never could.
			debug_printf("config save: a write failed (error %u) - keeping the previous ini\r\n",GetLastError());
			
			// rc.16: the silent keeper was the mystery - the failed save
			// reached no one outside a debug build. the box names the
			// keeper so the question has an answer in a release build.
			{
				wchar_t caption_wbuf[STRING_SIZE];
				wchar_t message_wbuf[STRING_SIZE];
				
				string_copy_utf8_string(caption_wbuf,localization_get_string(LOCALIZATION_ID_CONFIG_SAVE_FAILED_CAPTION));
				string_copy_utf8_string(message_wbuf,localization_get_string(LOCALIZATION_ID_CONFIG_SAVE_FAILED_MESSAGE));
				
				viv_msgbox(_viv_hwnd,caption_wbuf,message_wbuf,MB_OK | MB_ICONWARNING);
			}
		}
		else
		if (!MoveFileExW(tempname,filename,MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
		{
			DWORD move_error;
			
			move_error = GetLastError();
			
			// the fallback for the filesystems that refuse the move
			// (some network shares): the copy is the best effort,
			// and its own failure lands in the log line with the
			// temp file kept alongside the old ini.
			if (CopyFile(tempname,filename,FALSE))
			{
				DeleteFile(tempname);
			}
			else
			{
				debug_printf("config save: replace failed (move %u, copy %u) - the temp file keeps the save\r\n",move_error,GetLastError());
				
				{
					wchar_t caption_wbuf[STRING_SIZE];
					wchar_t message_wbuf[STRING_SIZE];
					
					string_copy_utf8_string(caption_wbuf,localization_get_string(LOCALIZATION_ID_CONFIG_SAVE_FAILED_CAPTION));
					string_copy_utf8_string(message_wbuf,localization_get_string(LOCALIZATION_ID_CONFIG_SAVE_FAILED_MESSAGE));
					
					viv_msgbox(_viv_hwnd,caption_wbuf,message_wbuf,MB_OK | MB_ICONWARNING);
				}
			}
		}
	}
	else
	{
		// rc.16 closed the two failure faces above (the mid-write and
		// the replace); the create itself was the third silent leg - a
		// locked temp file (an editor, a scanner, a sync tool holding
		// the .tmp) answered nothing at all. the same box, the same log
		// line: the keeper gets named in a release build too.
		debug_printf("config save: the temp file refused to open (error %u) - no write was attempted\r\n",GetLastError());

		{
			wchar_t caption_wbuf[STRING_SIZE];
			wchar_t message_wbuf[STRING_SIZE];

			string_copy_utf8_string(caption_wbuf,localization_get_string(LOCALIZATION_ID_CONFIG_SAVE_FAILED_CAPTION));
			string_copy_utf8_string(message_wbuf,localization_get_string(LOCALIZATION_ID_CONFIG_SAVE_FAILED_MESSAGE));

			viv_msgbox(_viv_hwnd,caption_wbuf,message_wbuf,MB_OK | MB_ICONWARNING);
		}
	}
}

void config_save_settings(int appdata)
{
	if (appdata)
	{
		wchar_t path[STRING_SIZE];

		if (string_get_appdata_path(path))
		{
			wchar_t appdata_wbuf[STRING_SIZE];
			
			string_path_combine_utf8(appdata_wbuf,path,(const utf8_t *)"voidImageViewer");
			
			CreateDirectory(appdata_wbuf,NULL);

			_config_save_settings_by_location(appdata_wbuf,0);
		}
			else
			{
				// the appdata root the user picked refused to name itself:
				// nothing was saved anywhere and the silence was total (the
				// report's fourth leg). the same box the write failures
				// carry - the user chose this home, and silently writing
				// the exe's own folder instead is the silent keeper back.
				debug_printf("config save: the appdata path failed (error %u)\r\n",GetLastError());

				{
					wchar_t caption_wbuf[STRING_SIZE];
					wchar_t message_wbuf[STRING_SIZE];

					string_copy_utf8_string(caption_wbuf,localization_get_string(LOCALIZATION_ID_CONFIG_SAVE_FAILED_CAPTION));
					string_copy_utf8_string(message_wbuf,localization_get_string(LOCALIZATION_ID_CONFIG_SAVE_FAILED_MESSAGE));

					viv_msgbox(_viv_hwnd,caption_wbuf,message_wbuf,MB_OK | MB_ICONWARNING);
				}
			}
	}
	else
	{
		wchar_t path[STRING_SIZE];
		
		string_get_exe_path(path);
		
		_config_save_settings_by_location(path,1);
	}
}
