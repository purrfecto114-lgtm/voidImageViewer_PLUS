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
// viv_playlist.h - playlist, everything search, sorting and navigation items.
#ifndef VIV_PLAYLIST_H
#define VIV_PLAYLIST_H

#include "viv.h"

// exported to other domains / the viv.c core
int _viv_icompare_filename(const wchar_t *s1,const wchar_t *s2);
int _viv_fd_compare(const WIN32_FIND_DATA *a,const WIN32_FIND_DATA *b);
// the single source of truth for "may the viewer open this name": a
// non-directory whose extension is on the supported table. every entry
// point (the drop, the folder walk, the command line, file->open, the
// recent click) asks this one function, so a name can never be opened
// on one path and refused on another.
int _viv_is_valid_filename(WIN32_FIND_DATA *fd);

// the outcome of offering one name to the viewer. the add-filename
// entry points used to answer void and every refusal - a name that is
// not there, a denied directory, an extension off the whitelist, a
// path past MAX_PATH - was the same silence. the outcome is the typed
// answer each of them now carries and names on the status bar.
typedef enum
{
	VIV_OPEN_OPENED					= 0,
	VIV_OPEN_SKIPPED_BAD_EXTENSION	= 1,
	VIV_OPEN_SKIPPED_NOT_FOUND		= 2,
	VIV_OPEN_SKIPPED_ACCESS_DENIED	= 3,
	VIV_OPEN_SKIPPED_TOO_LONG		= 4,
	VIV_OPEN_SKIPPED_NOT_A_FILE		= 5,
	VIV_OPEN_SKIPPED_ERROR			= 6
} _viv_open_outcome_t;

// how many names the status line names before it just counts.
#define _VIV_OPEN_SKIP_NAMES_MAX 3

// the skip tally of one drop, one folder walk or one command line.
// the count lives here, in the caller's local: nothing accumulates in
// a global across entry points, and the entry point owns the lifetime.
typedef struct _viv_open_skip_report_t
{
	int count;
	int named_count;
	_viv_open_outcome_t outcomes[_VIV_OPEN_SKIP_NAMES_MAX];
	wchar_t names[_VIV_OPEN_SKIP_NAMES_MAX][MAX_PATH];
} _viv_open_skip_report_t;

// record one refusal into the caller's tally. report may be 0 (the
// seed walks that only want the gate, not the line).
void _viv_open_report_skip(_viv_open_skip_report_t *report,const wchar_t *name,_viv_open_outcome_t outcome);
// name the tally on the status bar through the existing temporary
// status text: "Skipped 3: photo.cr2 (unsupported extension), ...".
// a count of 0 says nothing and leaves any other line alone.
void _viv_playlist_report_skips(_viv_open_skip_report_t *report);
// the attributes of one path, through the long path gate. answers 0
// with the api's last error left in place so the caller can map it to
// an outcome (not found, access denied, too long, other).
int _viv_file_attributes(const wchar_t *full_path_and_filename,WIN32_FIND_DATA *fd);
// the api's own answer as an outcome. read the last error once, right
// after the call that set it.
_viv_open_outcome_t _viv_open_outcome_from_last_error(void);
// one entry of a walk: the whitelist decision, the full path and the
// MAX_PATH cFileName limit, in one place every walk answers through.
// report may be 0 - the caller owns the tally.
_viv_open_outcome_t _viv_playlist_add_fd(const wchar_t *full_path_and_filename,WIN32_FIND_DATA *fd,_viv_open_skip_report_t *report);
const char *_viv_get_copydata_string(const char *p,const char *e,wchar_t *buf,int bufsize);
void _viv_playlist_add_current_if_empty(void);
void _viv_playlist_clearall(void);
void _viv_playlist_delete(const WIN32_FIND_DATA *fd);
void _viv_playlist_rename(const wchar_t *old_filename,const wchar_t *new_filename);
_viv_playlist_t *_viv_playlist_add(const WIN32_FIND_DATA *fd);
// the walk and the add answer their outcome. both used to answer void;
// a caller that ignores the answer still compiles - only the callers
// that tally skips need the value.
_viv_open_outcome_t _viv_playlist_add_path(const wchar_t *full_path_and_filename);
_viv_open_outcome_t _viv_playlist_add_filename(const wchar_t *filename);
// the tallying form: the entry point keeps the report and names it
// once for the whole batch (the drop, the command line).
_viv_open_outcome_t _viv_playlist_add_filename_report(const wchar_t *filename,_viv_open_skip_report_t *report);
void _viv_nav_item_free_all(void);
void _viv_nav_item_add(WIN32_FIND_DATA *fd);
int _viv_nav_compare(const void *va,const void *vb);
void _viv_search_everything(int add);
int _viv_send_everything_search(HWND hwnd,int add,int randomize,const wchar_t *search);
int _viv_playlist_shuffle_index_from_fd(const WIN32_FIND_DATA *fd);
_viv_playlist_t *_viv_playlist_from_fd(const WIN32_FIND_DATA *fd);
void _viv_do_initial_shuffle(void);
void _viv_send_random_everything_search(void);
int _viv_everything_item_to_fd(const COPYDATASTRUCT *cds,const EVERYTHING_IPC_ITEM2 *item,WIN32_FIND_DATA *fd);

#endif
