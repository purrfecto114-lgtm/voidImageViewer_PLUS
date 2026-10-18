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
// viv_playlist.c - playlist, everything search, sorting and navigation items.
// Pure physical move from viv.c (R70 split): bodies are unchanged.
#include "viv.h"
#include "viv_state.h"
#include "viv_playlist.h"
#include "viv_chrome.h"
#include "viv_dark.h"
#include "viv_load.h"
#include "viv_view.h"

// forward declarations (order preserved from viv.c)
int _viv_icompare_filename(const wchar_t *s1,const wchar_t *s2);
static int _viv_compare_id(const WIN32_FIND_DATA *a,const WIN32_FIND_DATA *b);
static int _viv_fd_compare_name(const WIN32_FIND_DATA *a,const WIN32_FIND_DATA *b);
static int _viv_fd_compare_path_and_name(const WIN32_FIND_DATA *a,const WIN32_FIND_DATA *b);
int _viv_fd_compare(const WIN32_FIND_DATA *a,const WIN32_FIND_DATA *b);
int _viv_is_valid_filename(WIN32_FIND_DATA *fd);
const char *_viv_get_copydata_string(const char *p,const char *e,wchar_t *buf,int bufsize);
void _viv_playlist_add_current_if_empty(void);
void _viv_playlist_clearall(void);
void _viv_playlist_delete(const WIN32_FIND_DATA *fd);
void _viv_playlist_rename(const wchar_t *old_filename,const wchar_t *new_filename);
_viv_playlist_t *_viv_playlist_add(const WIN32_FIND_DATA *fd);
_viv_open_outcome_t _viv_playlist_add_path(const wchar_t *full_path_and_filename);
_viv_open_outcome_t _viv_playlist_add_filename(const wchar_t *filename);
static _viv_open_outcome_t _viv_playlist_add_path_ex(const wchar_t *full_path_and_filename,_viv_open_skip_report_t *report);
static void _viv_shuffle_playlist(void);
void _viv_nav_item_free_all(void);
void _viv_nav_item_add(WIN32_FIND_DATA *fd);
int _viv_nav_compare(const void *va,const void *vb);
static INT_PTR CALLBACK _viv_search_everything_proc(HWND hwnd,UINT msg,WPARAM wParam,LPARAM lParam);
void _viv_search_everything(int add);
int _viv_send_everything_search(HWND hwnd,int add,int randomize,const wchar_t *search);
static void _viv_add_current_path_to_playlist(void);
int _viv_playlist_shuffle_index_from_fd(const WIN32_FIND_DATA *fd);
_viv_playlist_t *_viv_playlist_from_fd(const WIN32_FIND_DATA *fd);
void _viv_do_initial_shuffle(void);
void _viv_send_random_everything_search(void);
static SIZE_T _viv_copydata_read(const COPYDATASTRUCT *cds,SIZE_T offset,void *dst,DWORD size);
int _viv_everything_item_to_fd(const COPYDATASTRUCT *cds,const EVERYTHING_IPC_ITEM2 *item,WIN32_FIND_DATA *fd);


static _viv_playlist_t *_viv_playlist_last = 0;
static LARGE_INTEGER _viv_playlist_id = {0};
static _viv_nav_item_t *_viv_nav_item_last = 0;
static DWORD _viv_everything_request_flags = 0;
int _viv_icompare_filename(const wchar_t *s1,const wchar_t *s2)
{
	// windows paths are case preserving but case insensitive: fold ascii
	// letters before comparing so one file cannot enter the mru twice with
	// different capitalization.
	while((*s1) && (*s2))
	{
		wchar_t c1;
		wchar_t c2;
		
		c1 = *s1;
		c2 = *s2;
		
		if ((c1 >= 'A') && (c1 <= 'Z'))
		{
			c1 = (wchar_t)(c1 + ('a' - 'A'));
		}
		
		if ((c2 >= 'A') && (c2 <= 'Z'))
		{
			c2 = (wchar_t)(c2 + ('a' - 'A'));
		}
		
		if (c1 != c2)
		{
			return 1;
		}
		
		s1++;
		s2++;
	}
	
	if ((*s1) || (*s2))
	{
		return 1;
	}
	
	return 0;
}
static int _viv_compare_id(const WIN32_FIND_DATA *a,const WIN32_FIND_DATA *b)
{
	if (a->dwReserved0 < b->dwReserved0)
	{
		return -1;
	}
	
	if (a->dwReserved0 > b->dwReserved0)
	{
		return 1;
	}
	
	if (a->dwReserved1 < b->dwReserved1)
	{
		return -1;
	}
	
	if (a->dwReserved1 > b->dwReserved1)
	{
		return 1;
	}
	
	return 0;	
}
static int _viv_fd_compare_name(const WIN32_FIND_DATA *a,const WIN32_FIND_DATA *b)
{
	int ret;
	const wchar_t *afilename;
	const wchar_t *bfilename;
	DWORD dwCmpFlags;
	
	dwCmpFlags = SORT_STRINGSORT|NORM_IGNORECASE;
	
	if (os_is_windows_7_or_later())
	{
		// #define SORT_DIGITSASNUMBERS      0x00000008  // use digits as numbers sort method
		dwCmpFlags |= 0x00000008;
	}

	afilename = string_get_filename_part(a->cFileName);
	bfilename = string_get_filename_part(b->cFileName);

	ret = CompareString(LOCALE_USER_DEFAULT,dwCmpFlags,afilename,string_get_length(afilename),bfilename,string_get_length(bfilename));
	
	switch(ret)
	{
		case CSTR_LESS_THAN:	
			return -1;
			
		case CSTR_GREATER_THAN:
			return 1;
	}

	return _viv_compare_id(a,b);
}
static int _viv_fd_compare_path_and_name(const WIN32_FIND_DATA *a,const WIN32_FIND_DATA *b)
{
	int ret;
	DWORD dwCmpFlags;
	
	dwCmpFlags = SORT_STRINGSORT|NORM_IGNORECASE;
	
	if (os_is_windows_7_or_later())
	{
		// #define SORT_DIGITSASNUMBERS      0x00000008  // use digits as numbers sort method
		dwCmpFlags |= 0x00000008;
	}

	ret = CompareString(LOCALE_USER_DEFAULT,dwCmpFlags,a->cFileName,string_get_length(a->cFileName),b->cFileName,string_get_length(b->cFileName));
	
	switch(ret)
	{
		case CSTR_LESS_THAN:	
			return -1;
			
		case CSTR_GREATER_THAN:
			return 1;
	}

	return _viv_compare_id(a,b);
}
int _viv_fd_compare(const WIN32_FIND_DATA *a,const WIN32_FIND_DATA *b)
{
	int ret;
	
	ret = 0;
	
	switch(config_nav_sort)
	{
		case CONFIG_NAV_SORT_NAME:
			ret = _viv_fd_compare_name(a,b);
			break;

		case CONFIG_NAV_SORT_FULL_PATH_AND_FILENAME:
			ret = _viv_fd_compare_path_and_name(a,b);
			break;

		case CONFIG_NAV_SORT_SIZE:
		{
			LARGE_INTEGER sizea;
			LARGE_INTEGER sizeb;
			
			sizea.HighPart = a->nFileSizeHigh;
			sizea.LowPart = a->nFileSizeLow;

			sizeb.HighPart = b->nFileSizeHigh;
			sizeb.LowPart = b->nFileSizeLow;

			if (sizea.QuadPart < sizeb.QuadPart)
			{
				ret = -1;
			}
			else
			if (sizea.QuadPart > sizeb.QuadPart)
			{
				ret = 1;
			}
			else
			{
				// we want name ascending when we are size descending.
				ret = -_viv_fd_compare_name(a,b);
			}
			
			break;
		}

		case CONFIG_NAV_SORT_DATE_MODIFIED:
		{
			LARGE_INTEGER datea;
			LARGE_INTEGER dateb;
			
			datea.HighPart = a->ftLastWriteTime.dwHighDateTime;
			datea.LowPart = a->ftLastWriteTime.dwLowDateTime;

			dateb.HighPart = b->ftLastWriteTime.dwHighDateTime;
			dateb.LowPart = b->ftLastWriteTime.dwLowDateTime;

			if (datea.QuadPart < dateb.QuadPart)
			{
				ret = -1;
			}
			else
			if (datea.QuadPart > dateb.QuadPart)
			{
				ret = 1;
			}
			else
			{
				ret = -_viv_fd_compare_name(a,b);
			}
			
			break;
		}

		case CONFIG_NAV_SORT_DATE_CREATED:
		{
			LARGE_INTEGER datea;
			LARGE_INTEGER dateb;
			
			datea.HighPart = a->ftCreationTime.dwHighDateTime;
			datea.LowPart = a->ftCreationTime.dwLowDateTime;

			dateb.HighPart = b->ftCreationTime.dwHighDateTime;
			dateb.LowPart = b->ftCreationTime.dwLowDateTime;

			if (datea.QuadPart < dateb.QuadPart)
			{
				ret = -1;
			}
			else
			if (datea.QuadPart > dateb.QuadPart)
			{
				ret = 1;
			}
			else
			{
				ret = -_viv_fd_compare_name(a,b);
			}
			
			break;
		}
	}
	
	if (!config_nav_sort_ascending)
	{
		ret *= -1;
	}
	
	return ret;
}
// the ONE decision: not a folder, and an extension the viewer decodes.
// this is the function every entry point answers through - file->open,
// the drop, the folder walk, the command line, the recent click - so the
// set of openable names is written once and a whitelist change edits one
// function. (the open dialog used to open whatever it was handed while
// every other path refused it, so the same file could open or vanish
// depending on the route in.)
int _viv_is_valid_filename(WIN32_FIND_DATA *fd)
{
	const wchar_t *s;
	const wchar_t *e;
	int exti;

	// a folder is never an image, whatever it is named.
	if (fd->dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
	{
		return 0;
	}
	
	// the last dot wins, and a name with no dot has no extension to
	// compare against at all (the ".bashrc" shape: a leading dot is not
	// an extension).
	e = 0;
	s = fd->cFileName;
	
	while(*s)
	{
		if (*s == '.')
		{
			e = s + 1;
		}
		
		s++;
	}
	
	if (!e)
	{
		return 0;
	}
	
	// the supported table, not the association table: the two are
	// deliberately different sets (the association list carries only the
	// formats the installer registers).
	for(exti=0;exti<_VIV_SUPPORTED_EXTENSION_COUNT;exti++)
	{
		if (string_icompare_lowercase_ascii(e,_viv_supported_extensions[exti]) == 0) 
		{
			return 1;
		}
	}
	
	return 0;
}
// the reason text for one outcome. plain ascii: the line is assembled
// from the names the caller already holds, so it stays readable in any
// language the viewer runs in.
static const char *_viv_open_outcome_reason(_viv_open_outcome_t outcome)
{
	switch(outcome)
	{
		case VIV_OPEN_SKIPPED_BAD_EXTENSION:
			return "unsupported extension";
			
		case VIV_OPEN_SKIPPED_NOT_FOUND:
			return "not found";
			
		case VIV_OPEN_SKIPPED_ACCESS_DENIED:
			return "access denied";
			
		case VIV_OPEN_SKIPPED_TOO_LONG:
			return "path too long";
			
		case VIV_OPEN_SKIPPED_NOT_A_FILE:
			return "not a file";
			
		// the two ends of the enum are not refusals and never reach
		// here, but the switch stays exhaustive so a new outcome
		// cannot slip past the line without a reason.
		case VIV_OPEN_OPENED:
		case VIV_OPEN_SKIPPED_ERROR:
		default:
			break;
	}
	
	return "could not be read";
}
// the api's answer as an outcome. the last error is read once, right
// after the call that set it.
_viv_open_outcome_t _viv_open_outcome_from_last_error(void)
{
	switch(GetLastError())
	{
		case ERROR_FILE_NOT_FOUND:
		case ERROR_PATH_NOT_FOUND:
			return VIV_OPEN_SKIPPED_NOT_FOUND;
			
		case ERROR_ACCESS_DENIED:
			return VIV_OPEN_SKIPPED_ACCESS_DENIED;
			
		case ERROR_FILENAME_EXCED_RANGE:
			return VIV_OPEN_SKIPPED_TOO_LONG;
	}
	
	return VIV_OPEN_SKIPPED_ERROR;
}
// the attributes of one path, through the long path gate: a short path
// is handed to the api exactly as it was, a long one carries the "\\?\"
// prefix. a name the prefixed buffer cannot hold is refused here with
// error_filename_exced_range, so the caller maps it to the same
// SKIPPED_TOO_LONG a too long name from the api itself would give.
int _viv_file_attributes(const wchar_t *full_path_and_filename,WIN32_FIND_DATA *fd)
{
	wchar_t path_wbuf[STRING_SIZE];
	
	// the caller's buffer is stack memory and the api fills only the
	// attribute block of the find-data layout, so every field past it
	// answers whatever the stack held. the name fill below covers the
	// one field every consumer reads; this zeroing covers the rest of
	// the layout, so the garbage class stays dead even if a future
	// round retires that fill (the rc.1 regression read the whitelist's
	// extension out of exactly that garbage).
	os_zero_memory(fd,sizeof(WIN32_FIND_DATA));

	if (!os_GetFileAttributesExW)
	{
		return 0;
	}
	
	if (!string_path_long(path_wbuf,STRING_SIZE,full_path_and_filename))
	{
		debug_printf("path too long\n");
		
		SetLastError(ERROR_FILENAME_EXCED_RANGE);
		
		return 0;
	}
	
	if (!os_GetFileAttributesExW(path_wbuf,GetFileExInfoStandard,fd))
	{
		return 0;
	}
	
	// GetFileAttributesExW fills only the attribute block of the
	// find-data layout - cFileName is one of FindFirstFile's fields
	// and the caller's buffer here answers whatever the stack left.
	// every consumer names the entry through this field (the
	// whitelist's extension, the playlist row's combine), so it
	// holds the bare name of the path that was asked about before
	// the caller reads on.
	string_copy_with_bufsize(fd->cFileName,MAX_PATH,string_get_filename_part(full_path_and_filename));
	
	return 1;
}
void _viv_open_report_skip(_viv_open_skip_report_t *report,const wchar_t *name,_viv_open_outcome_t outcome)
{
	if (!report)
	{
		// a walk that only wanted the gate (the startup seed) keeps no
		// tally: nothing to count, nothing to say.
		return;
	}
	
	// a name is only ever a skip. OPENED never reaches here: the
	// callers check it first and an outcome list full of "opened"
	// would be a lie, not a report.
	if (outcome == VIV_OPEN_OPENED)
	{
		return;
	}
	
	report->count++;
	
	// the line names the first few and counts the rest.
	if (report->named_count < _VIV_OPEN_SKIP_NAMES_MAX)
	{
		report->outcomes[report->named_count] = outcome;
		
		// the name part, not the whole path: the line is about what
		// was refused, and a full path would eat the pane.
		string_copy_with_bufsize(report->names[report->named_count],MAX_PATH,string_get_filename_part(name));
		
		report->named_count++;
	}
}
// the tally on the status bar. the temporary status text is the
// existing helper every transient line uses (the slideshow rate, the
// zoom readout): it shows for three seconds, then the ordinary file
// line returns underneath it.
void _viv_playlist_report_skips(_viv_open_skip_report_t *report)
{
	wchar_t wbuf[STRING_SIZE];
	wchar_t more_wbuf[64];
	int i;
	
	if ((!report) || (!report->count))
	{
		// nothing was refused: leave any other line alone.
		return;
	}
	
	string_printf(wbuf,"Skipped %d: ",report->count);
	
	for(i=0;i<report->named_count;i++)
	{
		if (i)
		{
			string_cat_utf8(wbuf,(const utf8_t *)", ");
		}
		
		string_cat(wbuf,report->names[i]);
		string_cat_utf8(wbuf,(const utf8_t *)" (");
		string_cat_utf8(wbuf,(const utf8_t *)_viv_open_outcome_reason(report->outcomes[i]));
		string_cat_utf8(wbuf,(const utf8_t *)")");
	}
	
	if (report->count > report->named_count)
	{
		// its own buffer: string_printf measures its STRING_SIZE budget
		// from the pointer it is handed, so an offset into wbuf would
		// size the budget past the end of wbuf.
		string_printf(more_wbuf,", and %d more",report->count - report->named_count);
		
		string_cat(wbuf,more_wbuf);
	}
	
	string_cat_utf8(wbuf,(const utf8_t *)".");
	
	_viv_status_set_temp_text(wbuf);
}
const char *_viv_get_copydata_string(const char *p,const char *e,wchar_t *buf,int bufsize)
{
	wchar_t *d;
	
	d = buf;
	
	while(p + sizeof(wchar_t) <= e)
	{
		if (!*(wchar_t *)p)
		{
			p += sizeof(wchar_t);
			break;
		}
		
		if (bufsize > 1)
		{
			*d++ = *(wchar_t *)p;
			bufsize--;
		}
		
		p+=sizeof(wchar_t);
	}
	
	if (bufsize > 0)
	{
		*d = 0;
	}
	
	return p;
}
void _viv_playlist_add_current_if_empty(void)
{	
	// add current?
	if (!_viv_playlist_start)
	{
		if (*_viv_current_fd->cFileName)
		{
			_viv_playlist_add(_viv_current_fd);
		}
	}
}
void _viv_playlist_clearall(void)
{	
	// the folder branch resumes when the playlist goes - a window
	// captured before the playlist era must not answer its steps.
	_viv_nav_window_invalidate();
	_viv_playlist_t *d;
	
	d = _viv_playlist_start;
	while(d)
	{
		_viv_playlist_t *next_d;
		
		next_d = d->next;
		
		mem_free(d);
		
		d = next_d;
	}
	
	if (_viv_playlist_shuffle_indexes)
	{
		mem_free(_viv_playlist_shuffle_indexes);

		_viv_playlist_shuffle_indexes = 0;
		_viv_playlist_shuffle_allocated = 0;
	}
	
	_viv_playlist_start = 0;
	_viv_playlist_last = 0;
	_viv_playlist_count = 0;
	_viv_playlist_id.QuadPart = 0;
}
void _viv_playlist_delete(const WIN32_FIND_DATA *fd)
{
	if (_viv_playlist_shuffle_indexes)
	{
		int index;
		
		index = _viv_playlist_shuffle_index_from_fd(fd);
		if (index != -1)
		{
			_viv_playlist_t *d;
	
			d = _viv_playlist_shuffle_indexes[index];
			
			// the shuffle array moves in elements, not bytes: the bare count
			// shifted n bytes and left a half-spliced pointer behind for every
			// later reader of the row the delete just punched.
			os_move_memory(_viv_playlist_shuffle_indexes + index,_viv_playlist_shuffle_indexes + index + 1,(_viv_playlist_count - (index + 1)) * sizeof(_viv_playlist_t *));
			
			if (_viv_playlist_start == d)
			{
				_viv_playlist_start = d->next;
			}
			else
			{
				d->prev->next = d->next;
			}
		
			if (_viv_playlist_last == d)
			{
				_viv_playlist_last = d->prev;
			}
			else
			{
				d->next->prev = d->prev;
			}
		
			mem_free(d);

			_viv_playlist_count--;		
		}
	}
	else
	{
		_viv_playlist_t *d;
	
		d = _viv_playlist_from_fd(fd);
		if (d)
		{
			if (_viv_playlist_start == d)
			{
				_viv_playlist_start = d->next;
			}
			else
			{
				d->prev->next = d->next;
			}
		
			if (_viv_playlist_last == d)
			{
				_viv_playlist_last = d->prev;
			}
			else
			{
				d->next->prev = d->prev;
			}
		
			mem_free(d);

			_viv_playlist_count--;
		}
	}
}
void _viv_playlist_rename(const wchar_t *old_filename,const wchar_t *new_filename)
{
	_viv_playlist_t *d;
	
	d = _viv_playlist_start;
	while(d)
	{
		if (string_compare(d->fd.cFileName,old_filename) == 0)
		{
			string_copy(d->fd.cFileName,new_filename);
		
			break;
		}
		
		d = d->next;
	}
}
_viv_playlist_t *_viv_playlist_add(const WIN32_FIND_DATA *fd)
{
	_viv_playlist_t *d;
	
	d = (_viv_playlist_t *)mem_alloc(sizeof(_viv_playlist_t));
	
	os_copy_memory(&d->fd,fd,sizeof(WIN32_FIND_DATA));
	d->fd.dwReserved0 = _viv_playlist_id.HighPart;
	d->fd.dwReserved1 = _viv_playlist_id.LowPart;
	_viv_playlist_id.QuadPart = _viv_playlist_id.QuadPart + 1;
	
	if (_viv_playlist_start)
	{
		_viv_playlist_last->next = d;
		d->prev = _viv_playlist_last;
	}
	else
	{
		_viv_playlist_start = d;
		d->prev = 0;
	}
	
	_viv_playlist_last = d;
	d->next = 0;
	
	if (_viv_playlist_shuffle_indexes)
	{
		// make sure shuffle list has enough room...
		if (_viv_playlist_count + 1 > _viv_playlist_shuffle_allocated)
		{
			_viv_playlist_t **new_indexes;
			
			_viv_playlist_shuffle_allocated	*= 2;
			
			if (!_viv_playlist_shuffle_allocated)
			{
				_viv_playlist_shuffle_allocated = _VIV_DEFAULT_SHUFFLE_ALLOCATED;
			}
			
			new_indexes = mem_alloc(safe_size_mul_sizeof_pointer((SIZE_T)_viv_playlist_shuffle_allocated));
			
			os_copy_memory(new_indexes,_viv_playlist_shuffle_indexes,_viv_playlist_count * sizeof(_viv_playlist_t *));
			
			mem_free(_viv_playlist_shuffle_indexes);
			
			_viv_playlist_shuffle_indexes = new_indexes;
		}
		
		// place in shuffle list in a random position.
		{
			int index;
			
			index = ((rand() * RAND_MAX) + rand()) % (_viv_playlist_count + 1);

			if (_viv_playlist_count != index)
			{
				_viv_playlist_shuffle_indexes[_viv_playlist_count] = _viv_playlist_shuffle_indexes[index];
			}
			
			_viv_playlist_shuffle_indexes[index] = d;
		}
	}
	
	_viv_playlist_count++;
	
	return d;
}
// one directory entry of a walk: the whitelist decision, the full
// path, and the playlist entry. the long path gate is here, not at the
// caller: every entry reaches the playlist through
// WIN32_FIND_DATA.cFileName, a MAX_PATH buffer, so a path that would
// not fit there cannot be named and used to be copied in clipped - a
// playlist entry cannot represent it and used to be copied in clipped
// - a playlist entry pointing at a name that does not exist. the
// caller owns the tally (report may be 0).
// the first parameter is a directory: the entry's name is built as
// directory\cFileName. the walk callers pass the folder they are
// enumerating and the add-by-name caller passes the file's parent
// with its trailing separator kept - the documented combine
// maintains a trailing backslash, so the separator form is the
// deterministic join.
_viv_open_outcome_t _viv_playlist_add_fd(const wchar_t *directory,WIN32_FIND_DATA *fd,_viv_open_skip_report_t *report)
{
	WIN32_FIND_DATA entry_fd;
	wchar_t path_wbuf[STRING_SIZE];
	
	// the whitelist: the directory attribute came from the directory
	// itself, the extension from the name.
	if (!_viv_is_valid_filename(fd))
	{
		return VIV_OPEN_SKIPPED_BAD_EXTENSION;
	}
	
	if (!string_path_combine(path_wbuf,directory,fd->cFileName))
	{
		return VIV_OPEN_SKIPPED_TOO_LONG;
	}
	
	if (!string_path_is_short(path_wbuf))
	{
		// FindFirstFile cannot name an entry longer than MAX_PATH, so
		// the name it answered with is already a clipped prefix and
		// the real file is out of reach for the playlist.
		// FindFirstFileExW(FindExInfoBasic + FIND_FIRST_EX_LARGE_FETCH)
		// is the documented way to fetch the full name, but the flag
		// only exists on windows 7 / 2008 r2 and later: this tree
		// declares _WIN32_WINNT 0x0501, so using it would mean either
		// a _WIN32_WINNT bump or a runtime os_is_windows_7_or_later()
		// guard around a second enumeration. the walk is also bounded
		// by the playlist's own storage - every entry is named through
		// WIN32_FIND_DATA.cFileName, a MAX_PATH buffer - so the long
		// name has nowhere to land even when it is fetched. the honest
		// answer is the skip, not a clipped entry that names a file
		// which does not exist.
		debug_printf("path too long: %S\n",path_wbuf);
		
		return VIV_OPEN_SKIPPED_TOO_LONG;
	}
	
	os_copy_memory(&entry_fd,fd,sizeof(WIN32_FIND_DATA));
	
	string_copy_with_bufsize(entry_fd.cFileName,MAX_PATH,path_wbuf);
	
	_viv_playlist_add(&entry_fd);
	
	return VIV_OPEN_OPENED;
}
// the walk. the tally rides in through report so one drop, one command
// line or one open dialog names every refusal the whole tree produced
// instead of one line per folder.
static _viv_open_outcome_t _viv_playlist_add_path_ex(const wchar_t *full_path_and_filename,_viv_open_skip_report_t *report)
{
	WIN32_FIND_DATA fd;
	wchar_t search_wbuf[STRING_SIZE];
	wchar_t path_wbuf[STRING_SIZE];
	HANDLE h;
	int added;

	// the walk answers the folder's own outcome: at least one entry
	// became a playlist row.
	added = 0;
	
	// the search pattern rides the long path gate too: FindFirstFile
	// cannot enumerate a folder whose own path is past MAX_PATH.
	if (!string_path_combine(search_wbuf,full_path_and_filename,(const wchar_t *)L"*.*"))
	{
		_viv_open_report_skip(report,full_path_and_filename,VIV_OPEN_SKIPPED_TOO_LONG);
		
		return VIV_OPEN_SKIPPED_TOO_LONG;
	}
	
	if (!string_path_long(path_wbuf,STRING_SIZE,search_wbuf))
	{
		_viv_open_report_skip(report,full_path_and_filename,VIV_OPEN_SKIPPED_TOO_LONG);
		
		return VIV_OPEN_SKIPPED_TOO_LONG;
	}
	
	h = FindFirstFile(path_wbuf,&fd);
	if (h != INVALID_HANDLE_VALUE)
	{
		for(;;)
		{
			_viv_open_outcome_t outcome;
			
			if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
			{
				if (!((string_compare_ascii(fd.cFileName,".") == 0) || (string_compare_ascii(fd.cFileName,"..") == 0)))
				{
					if (!string_path_combine(search_wbuf,full_path_and_filename,fd.cFileName))
					{
						_viv_open_report_skip(report,fd.cFileName,VIV_OPEN_SKIPPED_TOO_LONG);
					}
					else
					{
						outcome = _viv_playlist_add_path_ex(search_wbuf,report);
						
						if (outcome == VIV_OPEN_OPENED)
						{
							added = 1;
						}
					}
				}
			}
			else
			{
				outcome = _viv_playlist_add_fd(full_path_and_filename,&fd,report);
				
				if (outcome == VIV_OPEN_OPENED)
				{
					added = 1;
				}
				else
				{
					_viv_open_report_skip(report,fd.cFileName,outcome);
				}
			}

			if (!FindNextFile(h,&fd)) break;
		}
		
		FindClose(h);
	}
	else
	if (GetLastError() == ERROR_FILE_NOT_FOUND)
	{
		// an empty folder is not a refusal.
	}
	else
	if (GetLastError() == ERROR_ACCESS_DENIED)
	{
		_viv_open_report_skip(report,full_path_and_filename,VIV_OPEN_SKIPPED_ACCESS_DENIED);
		
		return VIV_OPEN_SKIPPED_ACCESS_DENIED;
	}
	
	// a folder that yielded no openable file is itself a refusal, but
	// only the caller names it - the entries inside it are already in
	// the tally.
	return added ? VIV_OPEN_OPENED : VIV_OPEN_SKIPPED_BAD_EXTENSION;
}
_viv_open_outcome_t _viv_playlist_add_path(const wchar_t *full_path_and_filename)
{
	_viv_open_skip_report_t report;
	_viv_open_outcome_t outcome;
	
	// the tally is this call's own local: the whole walk, one line.
	os_zero_memory(&report,sizeof(_viv_open_skip_report_t));
	
	outcome = _viv_playlist_add_path_ex(full_path_and_filename,&report);
	
	_viv_playlist_report_skips(&report);
	
	return outcome;
}
_viv_open_outcome_t _viv_playlist_add_filename_report(const wchar_t *filename,_viv_open_skip_report_t *report)
{
	WIN32_FIND_DATA fd;
	wchar_t full_path_and_filename[STRING_SIZE];
	wchar_t directory_wbuf[STRING_SIZE];
	wchar_t cwd[STRING_SIZE];
	_viv_open_outcome_t outcome;

	GetCurrentDirectory(STRING_SIZE,cwd);
	
	if (!string_path_combine(full_path_and_filename,cwd,filename))
	{
		_viv_open_report_skip(report,filename,VIV_OPEN_SKIPPED_TOO_LONG);
		
		return VIV_OPEN_SKIPPED_TOO_LONG;
	}
	
	if (!_viv_file_attributes(full_path_and_filename,&fd))
	{
		outcome = _viv_open_outcome_from_last_error();
		
		_viv_open_report_skip(report,filename,outcome);
		
		return outcome;
	}
	
	if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
	{
		outcome = _viv_playlist_add_path_ex(full_path_and_filename,report);
		
		if (outcome != VIV_OPEN_OPENED)
		{
			// a folder with nothing the viewer decodes in it: the
			// entries that were passed over are already counted.
			_viv_open_report_skip(report,filename,VIV_OPEN_SKIPPED_BAD_EXTENSION);
		}
		
		return outcome;
	}
	
	// the entry rides its parent directory: add_fd's first parameter
	// is a directory the entry name is joined onto, and this file's
	// own full path used to sit in that seat, so a file added by name
	// queued a playlist row at ...\\a.png\\a.png (the rc.1 field report's
	// File, Add File ask). the parent keeps its trailing separator -
	// string_get_path_part drops it, and the documented combine
	// maintains a trailing backslash, so the separator form is the
	// deterministic join.
	{
		const wchar_t *filename_part;
		int dir_len;

		filename_part = string_get_filename_part(full_path_and_filename);

		dir_len = (int)(filename_part - full_path_and_filename);

		if ((dir_len <= 0) || (dir_len >= STRING_SIZE))
		{
			_viv_open_report_skip(report,filename,VIV_OPEN_SKIPPED_TOO_LONG);

			return VIV_OPEN_SKIPPED_TOO_LONG;
		}

		os_copy_memory(directory_wbuf,full_path_and_filename,(size_t)dir_len * sizeof(wchar_t));
		directory_wbuf[dir_len] = 0;
	}

	outcome = _viv_playlist_add_fd(directory_wbuf,&fd,report);
	
	if (outcome != VIV_OPEN_OPENED)
	{
		_viv_open_report_skip(report,filename,outcome);
	}
	
	return outcome;
}
_viv_open_outcome_t _viv_playlist_add_filename(const wchar_t *filename)
{
	_viv_open_skip_report_t report;
	_viv_open_outcome_t outcome;
	
	os_zero_memory(&report,sizeof(_viv_open_skip_report_t));
	
	outcome = _viv_playlist_add_filename_report(filename,&report);
	
	_viv_playlist_report_skips(&report);
	
	return outcome;
}
static void _viv_shuffle_playlist(void)
{
	if (_viv_playlist_count)
	{
		{
			LARGE_INTEGER counter;
			
			QueryPerformanceCounter(&counter);
			
			srand((unsigned int)(counter.LowPart ^ counter.HighPart));
		}
		
		_viv_playlist_shuffle_allocated = _VIV_DEFAULT_SHUFFLE_ALLOCATED;
		
		while(_viv_playlist_shuffle_allocated < _viv_playlist_count)
		{
			_viv_playlist_shuffle_allocated *= 2;
		}
		
		// allocate indexes.
		// release the previous indexes first: the grow path in
		// _viv_playlist_add_item frees before replacing, the initial
		// shuffle must not leak the old block.
		if (_viv_playlist_shuffle_indexes)
		{
			mem_free(_viv_playlist_shuffle_indexes);
			
			_viv_playlist_shuffle_indexes = 0;
		}
		
		_viv_playlist_shuffle_indexes = mem_alloc(safe_size_mul_sizeof_pointer((SIZE_T)_viv_playlist_shuffle_allocated));
		
		// fill in indexes
		{
			_viv_playlist_t **d;
			_viv_playlist_t *p;
		
			d = _viv_playlist_shuffle_indexes;
			p = _viv_playlist_start;
		
			while(p)
			{
				*d++ = p;
				
				p = p->next;
			}
		}
		
		// shuffle
		{
			int i;
			
			for(i=0;i<_viv_playlist_count-1;i++)
			{
				int j;
				_viv_playlist_t *tmp;
				
				j = i + rand() % (_viv_playlist_count - i);
				
				tmp = _viv_playlist_shuffle_indexes[i];
				_viv_playlist_shuffle_indexes[i] = _viv_playlist_shuffle_indexes[j];
				_viv_playlist_shuffle_indexes[j] = tmp;
			}
		}
	}
}
void _viv_nav_item_free_all(void)
{
	if (_viv_nav_items)
	{
		int i;
		
		for(i=0;i<_viv_nav_item_count;i++)
		{
			mem_free(_viv_nav_items[i]);
		}
		
		mem_free(_viv_nav_items);

		__viv_nav_item_start = 0;
		_viv_nav_item_last = 0;
		_viv_nav_items = 0;
		_viv_nav_item_count = 0;
	}
}
void _viv_nav_item_add(WIN32_FIND_DATA *fd)
{
	_viv_nav_item_t *navitem;
	
	navitem = mem_alloc(sizeof(_viv_nav_item_t));
	
	os_copy_memory(&navitem->fd,fd,sizeof(WIN32_FIND_DATA));

	if (__viv_nav_item_start)
	{
		_viv_nav_item_last->next = navitem;
	}
	else
	{
		__viv_nav_item_start = navitem;
	}
	
	navitem->next = 0;
	_viv_nav_item_last = navitem;
	_viv_nav_item_count++;				
}
int _viv_nav_compare(const void *va,const void *vb)
{
	const _viv_nav_item_t *a;
	const _viv_nav_item_t *b;

	a = (_viv_nav_item_t *)va;
	b = (_viv_nav_item_t *)vb;

	return _viv_fd_compare_name(&a->fd,&b->fd);
}
static INT_PTR CALLBACK _viv_search_everything_proc(HWND hwnd,UINT msg,WPARAM wParam,LPARAM lParam)
{
	{
		INT_PTR dark_dialog_reply;
		
		dark_dialog_reply = _viv_dialog_dark_proc(hwnd,msg,wParam,lParam);
		
		if (dark_dialog_reply != -1)
		{
			return dark_dialog_reply;
		}
	}
	
	switch(msg)
	{
		case WM_INITDIALOG:
			// dark chrome: title bar and dark explorer control style.
			_viv_dark_dialog(hwnd);
			
			
			os_center_dialog(hwnd);
			
			SetWindowLongPtr(hwnd,GWLP_USERDATA,lParam);
			
			os_SetWindowText_localization_id(hwnd,lParam ? LOCALIZATION_ID_EVERYTHING_ADD_EVERYTHING_SEARCH_CAPTION : LOCALIZATION_ID_EVERYTHING_LOAD_EVERYTHING_SEARCH_CAPTION);
			os_SetDlgItemText_localization_id(hwnd,IDC_SEARCH_EVERYTHING_RANDOM_CHECKBOX,LOCALIZATION_ID_RANDOMIZE);
			
			return TRUE;
		
		case WM_COMMAND:
		
			switch(LOWORD(wParam))
			{
				case IDOK:
				{
					wchar_t search_wbuf[STRING_SIZE];
					
					GetDlgItemText(hwnd,IDC_EVERYTHING_EDIT,search_wbuf,STRING_SIZE);
					
					if (_viv_send_everything_search(hwnd,GetWindowLongPtr(hwnd,GWLP_USERDATA),IsDlgButtonChecked(hwnd,IDC_SEARCH_EVERYTHING_RANDOM_CHECKBOX) == BST_CHECKED,search_wbuf))
					{
						EndDialog(hwnd,0);
					}
					
					break;
				}
					
				case IDCANCEL:
					EndDialog(hwnd,0);
					break;
			}

			break;
	}
	
	return FALSE;
}
void _viv_search_everything(int add)
{
	DialogBoxParam(os_hinstance,MAKEINTRESOURCE(IDD_EVERYTHING),_viv_hwnd,_viv_search_everything_proc,add);
}
int _viv_send_everything_search(HWND hwnd,int add,int randomize,const wchar_t *search)
{
	if (randomize)
	{	
		// add is ignored in this case.
		_viv_random = string_alloc(search);

		_viv_random_tot_results = 0xffffffff;

		_viv_playlist_clearall();

		{
			LARGE_INTEGER counter;
			
			QueryPerformanceCounter(&counter);
			
			srand((unsigned int)(counter.LowPart ^ counter.HighPart));
		}
									
		_viv_home(0,0);
		
		return 1;
	}
	else
	{
		HWND everything_hwnd;
		
		if (_viv_random)
		{
			mem_free(_viv_random);
			
			_viv_random = 0;
		}
		
		everything_hwnd = FindWindowA(EVERYTHING_IPC_WNDCLASSA,0);
		
		if (everything_hwnd)
		{
			EVERYTHING_IPC_QUERY2 *q;
			COPYDATASTRUCT cds;
			DWORD size;
			wchar_t new_search[STRING_SIZE];
			
			string_copy_utf8_string(new_search,"ext:avif;bmp;dds;gif;hdp;heic;heif;ico;jpeg;jpg;jxr;png;qoi;tif;tiff;wdp;webp;emf;wmf <");
			string_cat(new_search,search);
			string_cat_utf8(new_search,">");

			size = (DWORD)safe_size_add(sizeof(EVERYTHING_IPC_QUERY2),safe_size_mul_sizeof_wchar(safe_size_add_one(string_get_length(new_search))));
			
			_viv_everything_request_flags = EVERYTHING_IPC_QUERY2_REQUEST_FULL_PATH_AND_NAME; 
					
			if (SendMessage(everything_hwnd,EVERYTHING_WM_IPC,EVERYTHING_IPC_IS_FILE_INFO_INDEXED,EVERYTHING_IPC_FILE_INFO_FILE_SIZE))
			{
				_viv_everything_request_flags |= EVERYTHING_IPC_QUERY2_REQUEST_SIZE; // date modified is requested by its own indexed check below 
			}
			
			if (SendMessage(everything_hwnd,EVERYTHING_WM_IPC,EVERYTHING_IPC_IS_FILE_INFO_INDEXED,EVERYTHING_IPC_FILE_INFO_DATE_MODIFIED))
			{
				_viv_everything_request_flags |= EVERYTHING_IPC_QUERY2_REQUEST_DATE_MODIFIED; 
			}
			
			if (SendMessage(everything_hwnd,EVERYTHING_WM_IPC,EVERYTHING_IPC_IS_FILE_INFO_INDEXED,EVERYTHING_IPC_FILE_INFO_DATE_CREATED))
			{
				_viv_everything_request_flags |= EVERYTHING_IPC_QUERY2_REQUEST_DATE_CREATED; 
			}
			
			q = mem_alloc(size);
			
			q->reply_hwnd = (DWORD)_viv_hwnd;
			q->reply_copydata_message = add ? _VIV_COPYDATA_ADD_EVERYTHING_SEARCH : _VIV_COPYDATA_OPEN_EVERYTHING_SEARCH;
			q->search_flags = 0;
			q->offset = 0;
			q->max_results = EVERYTHING_IPC_ALLRESULTS;
			q->request_flags = _viv_everything_request_flags;
			
			q->sort_type = EVERYTHING_IPC_SORT_NAME_ASCENDING;
			os_copy_memory(q+1,new_search,(string_get_length(new_search) + 1) * sizeof(wchar_t));
			
			cds.dwData = EVERYTHING_IPC_COPYDATA_QUERY2;
			cds.cbData = size;
			cds.lpData = q;
			
			SendMessage(everything_hwnd,WM_COPYDATA,(WPARAM)_viv_hwnd,(LPARAM)&cds);
			
			mem_free(q);
			
			return 1;
		}
		else
		{
			wchar_t *text_wbuf;
			wchar_t caption_wbuf[STRING_SIZE];
			
			text_wbuf = string_alloc_utf8(localization_get_string(LOCALIZATION_ID_EVERYTHING_NOT_AVAILABLE_MESSAGE));
				
			string_copy_utf8_string(caption_wbuf,localization_get_string(LOCALIZATION_ID_APP_NAME));

			// MB_ICONQUESTION avoids the messagebeep.
			viv_msgbox(hwnd,caption_wbuf,text_wbuf,MB_OK|MB_ICONERROR);
				
			mem_free(text_wbuf);
			
			return 0;
		}
	}
}
static void _viv_add_current_path_to_playlist(void)
{
	WIN32_FIND_DATA fd;
	HANDLE h;
	wchar_t pattern_wbuf[STRING_SIZE];
	wchar_t search_wbuf[STRING_SIZE];
	wchar_t path_wbuf[STRING_SIZE];

	string_get_path_part(path_wbuf,_viv_current_fd->cFileName);

	// the shared combines: a folder around the current file that does
	// not fit, or one the long path gate cannot carry, has no seed
	// walk at all - the old copy clamped and enumerated a truncated
	// name instead.
	if (!string_path_combine(pattern_wbuf,path_wbuf,(const wchar_t *)L"*.*"))
	{
		return;
	}

	if (!string_path_long(search_wbuf,STRING_SIZE,pattern_wbuf))
	{
		return;
	}

	h = FindFirstFile(search_wbuf,&fd);
	if (h != INVALID_HANDLE_VALUE)
	{
		for(;;)
		{
			if (_viv_is_valid_filename(&fd))
			{
				_viv_playlist_t *d;
				
				// the shared per entry gate: the whitelist, the full
				// path and the MAX_PATH cFileName limit are decided in
				// one place for every walk now, so this seed cannot
				// queue an entry whose name was clipped into a file
				// that does not exist. the seed keeps no tally (a
				// startup shuffle is not a user refusal), so an
				// unopenable entry is passed over rather than named.
				if (_viv_playlist_add_fd(path_wbuf,&fd,0) == VIV_OPEN_OPENED)
				{
					// the entry's full path is in the playlist, not in
					// the walk's find data: read the row back by name.
					d = _viv_playlist_from_fd(&fd);
					
					if ((d) && (string_compare(fd.cFileName,_viv_current_fd->cFileName) == 0))
					{
						// copy back the FD ID.
						// so when we get the next image we get the next image from the current image instead of the home image.
						_viv_current_fd->dwReserved0 = d->fd.dwReserved0;
						_viv_current_fd->dwReserved1 = d->fd.dwReserved1;
					}
				}
			}
			
			if (!FindNextFile(h,&fd)) break;
		}

		FindClose(h);
	}
}
int _viv_playlist_shuffle_index_from_fd(const WIN32_FIND_DATA *fd)
{
	int index;

	if (_viv_playlist_shuffle_indexes)
	{
		// the identity is the file name: the reserved pair is a
		// per-playlist counter, and an fd from outside the playlist
		// (the directory scan, an external open) carries (0,0) - the
		// first entry's exact id. matching ids pairs a stranger with
		// the first entry; matching names cannot.
		for(index=0;index < _viv_playlist_count;index++)
		{
			if (_viv_icompare_filename(_viv_playlist_shuffle_indexes[index]->fd.cFileName,fd->cFileName) == 0)
			{
				return index;
			}
		}
	}
	
	return -1;
}
_viv_playlist_t *_viv_playlist_from_fd(const WIN32_FIND_DATA *fd)
{
	_viv_playlist_t *d;
	
	// same identity as the shuffle lookup above: the file name,
	// the same case-folded compare the recent list has always
	// used.
	d = _viv_playlist_start;
	while(d)
	{
		if (_viv_icompare_filename(d->fd.cFileName,fd->cFileName) == 0)
		{
			return d;
		}
		
		d = d->next;
	}

	return 0;
}
void _viv_do_initial_shuffle(void)
{
	// if we don't have a playlist, but we have a current image, add the current path to the playlist and shuffle it.
	if (config_shuffle)
	{
		if ((!_viv_playlist_start) && (*_viv_current_fd->cFileName))
		{
			_viv_add_current_path_to_playlist();
			
			// shuffle_playlist
			_viv_shuffle_playlist();
		}
		else
		if ((_viv_playlist_start) && (!_viv_playlist_shuffle_indexes))
		{
			// shuffle_playlist
			_viv_shuffle_playlist();
		}
	}
}
void _viv_send_random_everything_search(void)
{
	HWND everything_hwnd;
	
	everything_hwnd = FindWindowA(EVERYTHING_IPC_WNDCLASSA,0);
	
	if (everything_hwnd)
	{
		EVERYTHING_IPC_QUERY2 *q;
		COPYDATASTRUCT cds;
		DWORD size;
		wchar_t new_search[STRING_SIZE];
		
		string_copy_utf8_string(new_search,"ext:avif;bmp;dds;gif;hdp;heic;heif;ico;jpeg;jpg;jxr;png;qoi;tif;tiff;wdp;webp;emf;wmf <");
		string_cat(new_search,_viv_random);
		string_cat_utf8(new_search,">");

		size = (DWORD)safe_size_add(sizeof(EVERYTHING_IPC_QUERY2),safe_size_mul_sizeof_wchar(safe_size_add_one(string_get_length(new_search))));
		
		_viv_everything_request_flags = EVERYTHING_IPC_QUERY2_REQUEST_FULL_PATH_AND_NAME; 
				
		if (SendMessage(everything_hwnd,EVERYTHING_WM_IPC,EVERYTHING_IPC_IS_FILE_INFO_INDEXED,EVERYTHING_IPC_FILE_INFO_FILE_SIZE))
		{
			_viv_everything_request_flags |= EVERYTHING_IPC_QUERY2_REQUEST_SIZE; // date modified is requested by its own indexed check below 
		}
		
		if (SendMessage(everything_hwnd,EVERYTHING_WM_IPC,EVERYTHING_IPC_IS_FILE_INFO_INDEXED,EVERYTHING_IPC_FILE_INFO_DATE_MODIFIED))
		{
			_viv_everything_request_flags |= EVERYTHING_IPC_QUERY2_REQUEST_DATE_MODIFIED; 
		}
		
		if (SendMessage(everything_hwnd,EVERYTHING_WM_IPC,EVERYTHING_IPC_IS_FILE_INFO_INDEXED,EVERYTHING_IPC_FILE_INFO_DATE_CREATED))
		{
			_viv_everything_request_flags |= EVERYTHING_IPC_QUERY2_REQUEST_DATE_CREATED; 
		}
		
		q = mem_alloc(size);
		
		q->reply_hwnd = (DWORD)_viv_hwnd;
		q->reply_copydata_message = _VIV_COPYDATA_RANDOM_EVERYTHING_SEARCH;
		q->search_flags = 0;
		q->offset = ((rand() * RAND_MAX) + rand()) % _viv_random_tot_results;
		q->max_results = 1;
		q->request_flags = _viv_everything_request_flags;
		
		debug_printf("rand index %d\n",q->offset);
		
		q->sort_type = EVERYTHING_IPC_SORT_NAME_ASCENDING;
		os_copy_memory(q+1,new_search,(string_get_length(new_search) + 1) * sizeof(wchar_t));
		
		cds.dwData = EVERYTHING_IPC_COPYDATA_QUERY2;
		cds.cbData = size;
		cds.lpData = q;
		
		SendMessage(everything_hwnd,WM_COPYDATA,(WPARAM)_viv_hwnd,(LPARAM)&cds);
		
		mem_free(q);
	}
}
// read size bytes at p from a WM_COPYDATA message into dst, advancing
// p. returns 0 when the read would leave the message: WM_COPYDATA
// arrives from arbitrary processes and every offset and length in it
// must be validated before it is trusted.
// one validated read at an offset into the copydata buffer: the range
// is proven inside _viv_safe_copy_data before any pointer forms, and
// the walk advances by offset - the returned distance, or (SIZE_T)-1
// when the read would leave the buffer. no pointer into the message
// is ever built outside the validator.
static SIZE_T _viv_copydata_read(const COPYDATASTRUCT *cds,SIZE_T offset,void *dst,DWORD size)
{
	if (_viv_safe_copy_data(cds->lpData,cds->cbData,offset,dst,size))
	{
		return offset + size;
	}
	
	return (SIZE_T)-1;
}
// parse one Everything IPC item into a find data. the list2 header and
// the item array are validated by the caller; the per item data walk
// (filename, size, dates) is validated here field by field. returns 0
// when the item data lies outside the message: the item is then skipped
// instead of reading whatever the sender pointed at.
int _viv_everything_item_to_fd(const COPYDATASTRUCT *cds,const EVERYTHING_IPC_ITEM2 *item,WIN32_FIND_DATA *fd)
{
	SIZE_T off;
	DWORD filename_len;
	
	os_zero_memory(fd,sizeof(WIN32_FIND_DATA));
	
	// EVERYTHING_IPC_QUERY2_REQUEST_FULL_PATH_AND_NAME. the item's
	// data_offset is a fully untrusted dword: it goes straight into the
	// validator as a distance - the old shape built base + offset first
	// and asked the validator to forgive the pointer after the fact.
	off = item->data_offset;
	
	if ((off = _viv_copydata_read(cds,off,&filename_len,sizeof(DWORD))) == (SIZE_T)-1)
	{
		return 0;
	}
	
	if (filename_len >= MAX_PATH)
	{
		return 0;
	}
	
	if ((off = _viv_copydata_read(cds,off,fd->cFileName,filename_len * sizeof(wchar_t))) == (SIZE_T)-1)
	{
		return 0;
	}
	
	fd->cFileName[filename_len] = 0;
	
	// the null terminator after the filename, when present.
	if ((cds->cbData - off) >= sizeof(wchar_t))
	{
		off += sizeof(wchar_t);
	}
	
	// EVERYTHING_IPC_QUERY2_REQUEST_SIZE
	if (_viv_everything_request_flags & EVERYTHING_IPC_QUERY2_REQUEST_SIZE)
	{
		if ((off = _viv_copydata_read(cds,off,&fd->nFileSizeLow,sizeof(DWORD))) == (SIZE_T)-1) return 0;
		if ((off = _viv_copydata_read(cds,off,&fd->nFileSizeHigh,sizeof(DWORD))) == (SIZE_T)-1) return 0;
	}
	
	// EVERYTHING_IPC_QUERY2_REQUEST_DATE_CREATED
	if (_viv_everything_request_flags & EVERYTHING_IPC_QUERY2_REQUEST_DATE_CREATED)
	{
		if ((off = _viv_copydata_read(cds,off,&fd->ftCreationTime.dwLowDateTime,sizeof(DWORD))) == (SIZE_T)-1) return 0;
		if ((off = _viv_copydata_read(cds,off,&fd->ftCreationTime.dwHighDateTime,sizeof(DWORD))) == (SIZE_T)-1) return 0;
	}
	
	// EVERYTHING_IPC_QUERY2_REQUEST_DATE_MODIFIED
	if (_viv_everything_request_flags & EVERYTHING_IPC_QUERY2_REQUEST_DATE_MODIFIED)
	{
		if ((off = _viv_copydata_read(cds,off,&fd->ftLastWriteTime.dwLowDateTime,sizeof(DWORD))) == (SIZE_T)-1) return 0;
		if ((off = _viv_copydata_read(cds,off,&fd->ftLastWriteTime.dwHighDateTime,sizeof(DWORD))) == (SIZE_T)-1) return 0;
	}
	
	return 1;
}
