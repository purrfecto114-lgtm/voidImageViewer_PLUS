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

#ifdef __cplusplus
extern "C" {
#endif

enum
{
        CONFIG_NAV_SORT_NAME=0,
        CONFIG_NAV_SORT_SIZE,
        CONFIG_NAV_SORT_DATE_MODIFIED,
        CONFIG_NAV_SORT_DATE_CREATED,
        CONFIG_NAV_SORT_FULL_PATH_AND_FILENAME,
};

#define CONFIG_KEYFLAG_CTRL                                     0x0100
#define CONFIG_KEYFLAG_SHIFT                            0x0200
#define CONFIG_KEYFLAG_ALT                                      0x0400
#define CONFIG_KEYFLAG_VK_MASK                          0x00ff
#define CONFIG_KEYFLAG_MOD_MASK                         0xff00

// a keyboard shortcut

typedef struct config_key_s
{
        struct config_key_s *next;
        WORD key;
        
}config_key_t;

#define CONFIG_SHRINK_BLIT_MODE_COLORONCOLOR    0
#define CONFIG_SHRINK_BLIT_MODE_HALFTONE                1

#define CONFIG_MAG_FILTER_COLORONCOLOR                  0
#define CONFIG_MAG_FILTER_HALFTONE                              1

void config_load_settings(void);
void config_save_settings(int appdata);

// the settings load outcome. the ini reader is tolerant - it ignores what
// it does not understand - so a half-written or cloud-synced file parses to
// partial garbage and the rest of the session runs on compiled-in
// defaults. the old exit path wrote that back unconditionally, so the
// user's themes, shortcuts, recent files and associations were silently
// replaced by defaults. the load now records what it found and the exit
// path asks before it overwrites.
enum
{
        // the file opened and every line parsed cleanly.
        CONFIG_LOAD_OK = 0,
        // the file is not there. a fresh profile answers this on every
        // run; there is nothing to lose and nothing to ask about.
        CONFIG_LOAD_FILE_MISSING,
        // the file is there and parsed to something the reader could not
        // vouch for (see the INI_ANOMALY_* bits in ini.h).
        CONFIG_LOAD_PARSE_ANOMALY,
        // the file is there and could not be read at all: a refused
        // share, a size the reader refuses, a short read.
        CONFIG_LOAD_UNREADABLE
};

extern int config_last_load_state;

// the settings file the last load answered from, and the last-known-good
// sibling it wrote beside it (the ".auditbak" the load takes before it
// parses). both are empty when no layer of the load named a file. the exit
// path needs them to name the file in the corruption ask and to answer the
// restore. LANE-B additions: the prefix keeps them clear of any other
// lane's globals.
extern wchar_t config_lanesafe_loaded_ini[STRING_SIZE];
extern wchar_t config_lanesafe_auditbak[STRING_SIZE];

// the slideshow rate range. three writers reach config_slideshow_rate (the
// ini load, the /rate command line switch and the custom rate dialog) and
// two of them could put a zero or a wild value in front of SetTimer. the
// clamp is the one range the shared writers go through.
//
// a non-positive rate is not a "fast slideshow", it is a missing one:
// SetTimer answers an interval under USER_TIMER_MINIMUM with 10ms and
// WM_TIMER still arrives, so a zero does not freeze the deck - it runs it
// at full speed with nothing bounding it. the honest repair is the
// compiled-in default, not a floor, so the runaway never becomes a legal
// rate. (the custom rate dialog keeps its own one-millisecond floor: a
// tick that quick is a thing a person can type on purpose.)
#define CONFIG_LANESAFE_SLIDESHOW_RATE_DEFAULT    5000
#define CONFIG_LANESAFE_SLIDESHOW_RATE_MIN     100
#define CONFIG_LANESAFE_SLIDESHOW_RATE_MAX     3600000

int config_clamp_slideshow_rate(int rate);

// reset every preference to its compiled-in default. the defaults are not
// re-spelled here: the pass captures the initialiser block itself (see
// config.c) before the first load touches the globals, so no default
// literal is authored twice.
//
// the window rectangle and the maximized flag are session state, not
// preferences: they ride through untouched.
void config_reset_settings_to_defaults(void);

// ---- the ini file plumbing (lane-B) ----
//
// one atomic copy, one place. the shape is a temp file in the target's
// OWN directory (a move across volumes is a copy, which is not atomic),
// written through a handle this code owns, FlushFileBuffers BEFORE the
// close (a close does not promise the data reached the medium), then
// CloseHandle, then MoveFileExW with MOVEFILE_REPLACE_EXISTING |
// MOVEFILE_WRITE_THROUGH so the move answers only once the new file has
// really landed and a reader never sees the old and the new at once.
//
// CopyFileW is deliberately NOT used: it closes its own destination
// handle (so there is nothing to flush) and with bFailIfExists = FALSE
// it destroys the previous good file before it knows the new one landed
// - precisely the failure a "known good" copy or a preserved damaged
// original must never have.
int config_lanesafe_atomic_copy(const wchar_t *from,const wchar_t *to);

// the timestamped name a damaged settings file is preserved under:
// "<ini>.corrupt.YYYYMMDD-HHMMSS". the stamp is local time, fixed
// width (no locale, no separators that a file system may object to).
// answers 0 when the name does not fit the caller's buffer, or when the
// settings file name is empty.
int config_lanesafe_corrupt_path(const wchar_t *ini_filename,wchar_t *wbuf,SIZE_T bufsize);

// is the last-known-good sibling the load recorded there to restore
// from? the ask only offers the restore when it is.
int config_lanesafe_auditbak_exists(void);

// the corrupted-settings gate the exit path runs before it writes. see
// the long note over the definition in config.c. answers 1 when the
// write may go ahead.
int config_lanesafe_exit_gate(HWND parent);

extern BYTE config_appdata;
extern BYTE config_language; // ui language: 0 = auto (follow the system language), 1 = english, 2 = simplified chinese.
extern BYTE config_dark_mode; // ui theme: 0 = light, 1 = dark, 2 = auto (follow the windows theme).
extern BYTE config_ui_accent; // ui accent color index: 0..4 (azure, teal, violet, amber, rose), see viv_theme.

// what shows under the transparent pixels of an image with alpha (not
// the canvas around the image: that color is the windowed background).
// the render back end: gdi stays the default, opengl and direct3d are
// opt-in session toggles (view -> renderer) that fall back to gdi on
// any refusal.
#define CONFIG_RENDERER_GDI                    0
#define CONFIG_RENDERER_OPENGL                 1
#define CONFIG_RENDERER_DIRECT3D               2

#define CONFIG_BACKDROP_MODE_FOLLOW                     0
#define CONFIG_BACKDROP_MODE_BLACK                      1
#define CONFIG_BACKDROP_MODE_WHITE                      2
#define CONFIG_BACKDROP_MODE_CUSTOM                     3
#define CONFIG_BACKDROP_MODE_CHECKERBOARD       4
extern int config_renderer; // render back end: CONFIG_RENDERER_* (gdi default).
extern int config_toolbar_groups; // toolbar visibility mask: bit per group (0x3f = every group).
extern BYTE config_toolbar_icon_only; // toolbar without labels: the strip draws icons alone, so the narrow default window fits every group.

extern BYTE config_backdrop_mode;
extern BYTE config_backdrop_color_r;
extern BYTE config_backdrop_color_g;
extern BYTE config_backdrop_color_b;
extern BYTE config_keep_centered;
extern int config_x;
extern int config_y;
extern int config_wide;
extern int config_high;
extern BYTE config_maximized;
extern int config_slideshow_rate;
extern BYTE config_allow_shrinking;
extern BYTE config_shrink_blit_mode;
extern BYTE config_mag_filter;
extern BYTE config_nav_sort;
extern BYTE config_nav_sort_ascending;
extern BYTE config_keep_aspect_ratio;
extern BYTE config_fill_window;
extern BYTE config_fullscreen_fill_window;
extern BYTE config_auto_zoom;
extern BYTE config_auto_zoom_type;
extern int config_auto_fit_wide_mul;
extern int config_auto_fit_wide_div;
extern int config_auto_fit_high_mul;
extern int config_auto_fit_high_div;
extern BYTE config_frame_minus;
extern BYTE config_multiple_instances;
extern BYTE config_show_status;
extern BYTE config_show_controls;
extern BYTE config_prevent_sleep;
extern BYTE config_loop_animations_once;
extern BYTE config_mouse_wheel_action;
extern BYTE config_ctrl_mouse_wheel_action;
extern BYTE config_left_click_action;
extern BYTE config_right_click_action;
extern BYTE config_xbutton_action;
extern BYTE config_windowed_background_color_r;
extern BYTE config_windowed_background_color_g;
extern BYTE config_windowed_background_color_b;
extern BYTE config_fullscreen_background_color_r;
extern BYTE config_fullscreen_background_color_g;
extern BYTE config_fullscreen_background_color_b;
extern BYTE config_options_last_page;
extern int config_short_jump;
extern int config_medium_jump;
extern int config_long_jump;
extern BYTE config_shuffle;
extern BYTE config_browse_file_open_dialog;
extern BYTE config_ontop;
extern int config_slideshow_custom_rate;
extern BYTE config_slideshow_custom_rate_type;
extern BYTE config_scroll_window;
// the preload/cache counts (the memory and cache round): int images,
// 0..5 preloaded ahead / 0..8 cached behind. the legacy byte pair reads
// on as the migration source - the loader reads the counts only.
extern int config_preload_count;
extern int config_cache_count;
extern BYTE config_preload_next;
extern BYTE config_cache_last;
// the resume switch and the last session's file (empty = nothing to
// resume). the exit/endsession paths write it, the blank-start path
// reads it.
extern BYTE config_resume_last_file;
extern wchar_t config_last_file[STRING_SIZE];
extern BYTE config_icm;
extern BYTE config_show_menu;
extern BYTE config_show_caption;
extern BYTE config_show_thickframe;
extern BYTE config_toolbar_move_window;
extern BYTE config_windowed_hide_cursor;
extern BYTE config_auto_zoom;
extern BYTE config_pixel_info;
extern BYTE config_orientation;
extern BYTE config_title_bar_format;
extern int config_add_command_line_timeout;
extern BYTE config_show_zoom_controls;
extern BYTE config_zoom_auto_hide;

// recent files mru (most recently opened paths, newest first).
#define CONFIG_RECENT_FILE_COUNT        10
extern wchar_t *config_recent_files[CONFIG_RECENT_FILE_COUNT];
extern int config_recent_file_count;

#ifdef __cplusplus
}
#endif
