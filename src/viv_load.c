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
// viv_load.c - image loading, decode thread, clipboard images, preload.
// Pure physical move from viv.c (R70 split): bodies are unchanged.
#include "viv.h"
#include "viv_state.h"
#include "viv_load.h"
#include "viv_anim.h"
#include "viv_chrome.h"
#include "viv_playlist.h"
#include "viv_recent.h"
#include "viv_render.h"
#include "viv_view.h"

// forward declarations (order preserved from viv.c)
void _viv_clear(void);
void _viv_process_pending_clear(void);
void _viv_clear_loading_preload(void);
void _viv_clear_preload_frames(void);
void _viv_clear_preload(void);
BOOL _viv_open_from_filename(const wchar_t *filename,int recent_policy);
void _viv_open(WIN32_FIND_DATA *fd,int is_preload);
void _viv_set_clipboard_image(void);
static void _viv_show_clipboard_image(HBITMAP hbitmap,int wide,int high);
BOOL _viv_paste_clipboard_image(void);
static int _viv_get_extension_format(const wchar_t *filename);
void _viv_save_image_as(void);
void _viv_doing_cancel(void);
void _viv_blank(void);
static DWORD WINAPI _viv_load_image_thread_proc(void *param);
void _viv_reply_free(_viv_reply_t *e);
_viv_reply_t *_viv_reply_add(DWORD type,DWORD size,void *data);
void _viv_reply_clear_all(void);
void _viv_preload_next(void);
void _viv_activate_preload(void);
void viv_copy_current_image_to_last_image(void);
static int _viv_cache_find(const wchar_t *filename);
static void _viv_cache_insert(_viv_image_slot_t *src);
static void _viv_cache_activate(int index);
void _viv_clear_last(void);
void _viv_refresh(void);
void _viv_open_preload(void);
static int _viv_pixel_budget_refused(SIZE_T pixels);
static void _viv_cache_set_trim(void);
static int _viv_animation_budget_refused(DWORD frame_count,SIZE_T canvas_pixels);
int _viv_safe_copy_data(const void *base,SIZE_T src_size,SIZE_T offset,void *dst,SIZE_T dst_size);


static GUID _viv_FrameDimensionTime = {0x6aedbd6d,0x3fb5,0x418a,{0x83,0xa6,0x7f,0x45,0x22,0x9d,0xc8,0x72}};
static _viv_frame_t *_viv_pending_clear_frames = NULL;
static int _viv_pending_clear_frame_loaded_count = 0;
// the slot primitives: the lifecycle of a held image in two moves.
// the clear releases a slot's frames and empties it; the take moves a
// whole slot into another slot. every transition the three slots know
// - the preload activation, the last-cache fill, the ping-pong behind
// the back-navigation - is one of these two, so a field added to a
// held image is moved and cleared everywhere or nowhere. the state
// member is not content: it is the preload role's dispatch lifecycle
// and stays with its slot through every take.
static void _viv_slot_clear_frames(_viv_image_slot_t *slot)
{
	if (slot->frames)
	{
		_viv_clear_frames(slot->frames,slot->frame_loaded_count);

		slot->frames = NULL;
	}

	slot->frame_count = 0;
	slot->frame_loaded_count = 0;
	slot->image_wide = 0;
	slot->image_high = 0;
}
static void _viv_slot_take(_viv_image_slot_t *dst,_viv_image_slot_t *src)
{
	os_copy_memory(&dst->fd,&src->fd,sizeof(WIN32_FIND_DATA));
	dst->frames = src->frames;
	dst->frame_count = src->frame_count;
	dst->frame_loaded_count = src->frame_loaded_count;
	dst->image_wide = src->image_wide;
	dst->image_high = src->image_high;
	src->fd.cFileName[0] = 0;
	src->frames = NULL;
	src->frame_count = 0;
	src->frame_loaded_count = 0;
	src->image_wide = 0;
	src->image_high = 0;
}
// clearing is really slow.
// delay this until after the new image is shown.
// we add the clear to a queue which is cleared with _viv_process_pending_clear.
// _viv_process_pending_clear should be called after the new frame is shown.
void _viv_clear(void)
{
	// the mailbox is single-slot by the single-flight invariant: every
	// _viv_clear call site pairs with its drain before the next set (the
	// ten call pairs the reply paths run, plus the kill path). the belt
	// below turns a future violation of that pairing from a silent leak
	// into an immediate free - the frames are ui-owned until the mailbox
	// takes them, so freeing here is the same thread and the same
	// allocator the drain would have used.
	if (_viv_pending_clear_frames)
	{
		_viv_clear_frames(_viv_pending_clear_frames,_viv_pending_clear_frame_loaded_count);
		
		_viv_pending_clear_frames = NULL;
		_viv_pending_clear_frame_loaded_count = 0;
	}
	
	_viv_pending_clear_frames = _viv_slot_current.frames;
	_viv_pending_clear_frame_loaded_count = _viv_slot_current.frame_loaded_count;
	_viv_slot_current.frames = NULL;
	_viv_slot_current.fd.cFileName[0] = 0;

	_viv_timer_stop();	

	_viv_frame_position = 0;
	_viv_frame_looped = 0;
	_viv_is_slideshow_timeup = 0;
	_viv_slot_current.frame_count = 0;
	_viv_slot_current.frame_loaded_count = 0;
	_viv_zoom_pos = 0;
	_viv_view_x = 0;
	_viv_view_y = 0;
	_viv_view_ix = 0.0;
	_viv_view_iy = 0.0;
	_viv_1to1 = 0;
	_viv_have_old_zoom = 0;
	_viv_image_is_low_res = 0;
	_viv_slot_current.image_wide = 0;
	_viv_slot_current.image_high = 0;
	_viv_slot_current.alpha_baked = 0;
	_viv_animation_play = 1;
}
void _viv_process_pending_clear(void)
{
	if (_viv_pending_clear_frames)
	{
		_viv_clear_frames(_viv_pending_clear_frames,_viv_pending_clear_frame_loaded_count);
		
		_viv_pending_clear_frames = NULL;
	}

	_viv_pending_clear_frame_loaded_count = 0;
}
void _viv_clear_loading_preload(void)
{
	if (_viv_load_image_thread)
	{
		if (_viv_load_is_preload)
		{
			InterlockedExchange(&_viv_load_image_terminate,1);
		}
	}
}
// the preload slot's frames clear through the one slot primitive -
// this used to be the family's own hand-written clear and is now a
// one-line routing, so the extern surface (the kill path, the
// activation unwind) keeps its name.
void _viv_clear_preload_frames(void)
{
	_viv_slot_clear_frames(&_viv_slot_preload);
}
void _viv_clear_preload(void)
{
	_viv_clear_preload_frames();
	
	_viv_slot_preload.fd.cFileName[0] = 0;
}
BOOL _viv_open_from_filename(const wchar_t *filename,int recent_policy)
{
	BOOL ret;
	WIN32_FIND_DATA fd;
	HANDLE h;
	wchar_t full_path_and_filename[STRING_SIZE];
	wchar_t cwd[STRING_SIZE];

	ret = FALSE;
	GetCurrentDirectory(STRING_SIZE,cwd);
	string_path_combine(full_path_and_filename,cwd,filename);

debug_printf("open filename: %S\n",full_path_and_filename);
	
	if ((os_GetFileAttributesExW) && (os_GetFileAttributesExW(full_path_and_filename,GetFileExInfoStandard,&fd)))
	{
		fd.dwReserved0 = 0;
		fd.dwReserved1 = 0;
		
		ret = TRUE;

		if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
		{
			// add subfolders and subsubfolders...
			_viv_playlist_add_path(full_path_and_filename);

			_viv_home(0,0);
		}
		else
		{
			string_copy_with_bufsize(fd.cFileName,MAX_PATH,full_path_and_filename);
			
			// the recent-list policy is declared by the caller, never guessed
			// here: a user command (the open dialog, the drag-drop, the recent
			// click) feeds the list unconditionally - the standard mru
			// contract, a re-open of the displayed file re-tops it. the
			// forwarded open (the single-instance re-entry the rotate verb's
			// refresh and the recheck double-click ride) feeds it only when
			// the file matches neither known identity - not the file on
			// screen (the slot) and not the file on its way there (the
			// request): a same-file forward is a reload, not a recent open,
			// and must not masquerade as a new open and silently reorder the
			// recent list. the request side catches the in-flight window
			// (the load that never displayed yet), the slot side catches
			// everything shown. the compare folds ascii case like the mru
			// itself.
			if ((recent_policy) || ((_viv_icompare_filename(full_path_and_filename,_viv_current_fd->cFileName) != 0) && (_viv_icompare_filename(full_path_and_filename,_viv_slot_current.fd.cFileName) != 0)))
			{
				_viv_recent_file_push(full_path_and_filename);
			}
			
			_viv_open(&fd,0);
		}
	}
	else
	{
		h = FindFirstFile(full_path_and_filename,&fd);
		if (h != INVALID_HANDLE_VALUE)
		{
			wchar_t path[STRING_SIZE];
			
			ret = TRUE;

			string_get_path_part(path,full_path_and_filename);
		
			for(;;)
			{
				string_path_combine(full_path_and_filename,path,fd.cFileName);
					
				if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
				{
					_viv_playlist_add_path(full_path_and_filename);
				}
				else
				{
					string_copy_with_bufsize(fd.cFileName,MAX_PATH,full_path_and_filename);
					_viv_playlist_add(&fd);
				}
					
				if (!FindNextFile(h,&fd))
				{
					break;
				}
			}

			_viv_home(0,0);
			
			FindClose(h);
		}
	}
	
	return ret;
}
// rc.16: the stale will-be-null claim retired - the queued
// reality answers below (a preload parks a request and waits).
void _viv_open(WIN32_FIND_DATA *fd,int is_preload)
{
int cache_hit;

debug_printf("open: %S cache %S frame %S is_preload %d\n",fd->cFileName,_viv_slot_cache[0].fd.cFileName,_viv_slot_current.fd.cFileName,is_preload);

if ((_viv_load_image_thread) && (_viv_load_image_filename))
{
debug_printf("CURRENTLY LOADING %S preload %d\n",_viv_load_image_filename,_viv_load_is_preload);
	
}

	if (!is_preload)
	{
		if (_viv_file_not_found)
		{
			_viv_file_not_found = 0;
			
			_viv_status_update();
		}

		if (_viv_load_failed)
		{
			_viv_load_failed = 0;
			_VIV_LOAD_REFUSED_CLEAR(_viv_load_refused_budget);
			_VIV_LOAD_REFUSED_CLEAR(_viv_load_refused_input_size);
			_viv_hw_render_fallback = 0;

			_viv_status_update();
		}
	}

	cache_hit = _viv_cache_find(fd->cFileName);

	if ((is_preload) && (cache_hit >= 0))
	{
		// don't preload what the cache ring already holds. this occurs
		// with a short playlist or folder, and it is how the preload
		// chain stops walking - the next image the ring already cached
		// answers the chain without another decode.
		return;
	}
	else
	if ((!is_preload) && (cache_hit >= 0))
	{
		// activate the cache ring hit.
		// don't activate preload.
		_viv_should_activate_preload_on_load = 0;
		
		// stop loading
		debug_printf("SET TERMINATE (LAST)\n");
		_viv_load_image_allow_draw = 0;
		InterlockedExchange(&_viv_load_image_terminate,1);
		
		if (_viv_load_image_next_fd)
		{
			mem_free(_viv_load_image_next_fd);
			
			_viv_load_image_next_fd = NULL;
		}

		_viv_slot_preload.fd.cFileName[0] = 0;
		
		// landing on a cached image is a settle: the chain starts over
		// from the file now on screen.
		_viv_preload_chain_count = 0;
		
		_viv_status_update();

		_viv_cache_activate(cache_hit);

		_viv_preload_next();

		return;
	}
	else
	if ((!is_preload) && (_viv_load_is_preload) && (*_viv_slot_preload.fd.cFileName) && (string_compare(_viv_slot_preload.fd.cFileName,fd->cFileName) == 0))
	{
		_viv_open_preload();

		return;
	}
	else
	if ((!is_preload) && (!_viv_load_is_preload) && (_viv_load_image_thread) && (_viv_load_image_filename) && (string_compare(_viv_load_image_filename,fd->cFileName) == 0))
	{
		debug_printf("already loading...\n");
		// already loading this one - and only a load headed for the
		// screen can honor that promise: an in-flight preload whose fd
		// was cleared (the cache-set abandonment, or a displaced
		// decode) is discarding its frames, so the request queues as
		// the normal load it is instead of trusting it.
		return;
	}
	
	if ((is_preload) && (_viv_slot_preload.state != 2) && (*_viv_slot_preload.fd.cFileName) && (string_compare(_viv_slot_preload.fd.cFileName,fd->cFileName) == 0))
	{
		// the walk asked for the file the slot already holds -
		// parked after a finished decode, or still in flight. both
		// answer the request as they are: a re-dispatch would clear
		// the parked frames and pay the same decode again (the ring
		// hit's settle used to discard the chain's parked end, then
		// re-preload the very file it threw away). a failed preload
		// falls through - the retry may answer a transient lock.
		return;
	}
	
	// clear any existing preload and start a fresh one.
	_viv_clear_preload();
	
	if (_viv_load_image_thread)
	{
		// already loading a different image.
		// add it to the queue and cancel this one.
		debug_printf("SET TERMINATE (next)\n");
		InterlockedExchange(&_viv_load_image_terminate,1);
		
		if (_viv_load_image_next_fd)
		{
			mem_free(_viv_load_image_next_fd);
		}
		
		_viv_load_image_next_fd = (WIN32_FIND_DATA *)mem_alloc(sizeof(WIN32_FIND_DATA));
		os_copy_memory(_viv_load_image_next_fd,fd,sizeof(WIN32_FIND_DATA));
		_viv_load_image_next_is_preload = is_preload;
		
		_viv_status_update();
	}
	else
	{
		DWORD thread_id;
		RECT rect;

		GetClientRect(_viv_hwnd,&rect);

		_viv_load_image_allow_draw = 1;
		InterlockedExchange(&_viv_load_image_terminate,0);
		_VIV_LOAD_REFUSED_CLEAR(_viv_load_refused_budget);
		_VIV_LOAD_REFUSED_CLEAR(_viv_load_refused_input_size);
		_viv_hw_render_fallback = 0;
		
		if (_viv_load_image_filename)
		{
			mem_free(_viv_load_image_filename);
		}
		
		_viv_load_image_filename = string_alloc(fd->cFileName);
			_viv_load_is_preload = is_preload;
			_viv_slot_preload.state = 0;
		_viv_should_activate_preload_on_load = 0;
		_viv_load_render_wide = rect.right - rect.left;
		_viv_load_render_high = rect.bottom - rect.top - _viv_get_status_high() - _viv_get_view_top();
		_viv_load_frame_count = 0;
		os_copy_memory(_viv_load_fd,fd,sizeof(WIN32_FIND_DATA));
		
		if (is_preload)
		{
			os_copy_memory(&_viv_slot_preload.fd,fd,sizeof(WIN32_FIND_DATA));
		}
		
debug_printf("LOAD %S is_preload %d\n",_viv_load_image_filename,is_preload);

		_viv_load_image_thread = CreateThread(NULL,0,_viv_load_image_thread_proc,0,0,&thread_id);

		if (_viv_load_image_thread)
		{
			_viv_status_update();
		}
		else
		{
			// the thread never started: no reply will ever come for the state
			// this dispatch just staged, so it unwinds here - the filename
			// frees, the draw gate closes and the load fails like any refused
			// file instead of a "loading..." line no thread will ever answer.
			debug_printf("CreateThread failed %x\n",GetLastError());
			
			mem_free(_viv_load_image_filename);
			_viv_load_image_filename = 0;
			_viv_load_image_allow_draw = 0;
			
			// a failed preload mirrors the background reply's shape: the
			// preload state carries the failure (navigation onto the file
			// re-reports it through _viv_open_preload) and the on-screen
			// status line stays as it was - a background preload's failure
			// is not the user's failure.
			if (is_preload)
			{
				_viv_slot_preload.state = 2;
			}
			else
			{
				_viv_load_failed = 1;
				
				_viv_status_update();
			}
		}
	}
	
	if (!is_preload)
	{
		os_copy_memory(_viv_current_fd,fd,sizeof(WIN32_FIND_DATA));
		
		// the folder fact belongs to the position being left.
		_viv_nav_folder_neighbor = -1;
		
		_viv_update_title();
	}
}
void _viv_set_clipboard_image(void)
{
	if (_viv_slot_current.frame_count)
	{
		if (_viv_slot_current.frames[_viv_frame_position].hbitmap)
		{
			HDC screen_hdc;
			
			screen_hdc = GetDC(0);
			if (screen_hdc)
			{
				HDC mem1_hdc;
				
				mem1_hdc = CreateCompatibleDC(screen_hdc);
				if (mem1_hdc)
				{
					HDC mem2_hdc;
					
					mem2_hdc = CreateCompatibleDC(screen_hdc);
					if (mem2_hdc)
					{
						HBITMAP mem1_hbitmap;

						mem1_hbitmap = CreateCompatibleBitmap(screen_hdc,_viv_slot_current.image_wide,_viv_slot_current.image_high);
						if (mem1_hbitmap)
						{
							HGDIOBJ last_mem1_hbitmap;
							HGDIOBJ last_mem2_hbitmap;
							
							last_mem1_hbitmap = SelectObject(mem1_hdc,mem1_hbitmap);
							last_mem2_hbitmap = SelectObject(mem2_hdc,_viv_slot_current.frames[_viv_frame_position].hbitmap);
							
							BitBlt(mem1_hdc,0,0,_viv_slot_current.image_wide,_viv_slot_current.image_high,mem2_hdc,0,0,SRCCOPY);

							SelectObject(mem2_hdc,last_mem2_hbitmap);
							SelectObject(mem1_hdc,last_mem1_hbitmap);
							
							// if SetClipboardData fails we keep ownership
							// and must free the bitmap ourselves.
							if (!SetClipboardData(CF_BITMAP,mem1_hbitmap))
							{
								DeleteObject(mem1_hbitmap);
							}
						}

						DeleteDC(mem2_hdc);
					}

					DeleteDC(mem1_hdc);
				}
				
				ReleaseDC(0,screen_hdc);
			}
		}
	}
}
// show a bitmap that came from the clipboard.
// pasted images have no filename: the title falls back to the application
// name and every filename based command (save as, delete, rename, copy to,
// ...) already checks for an empty filename and quietly does nothing.
// the frame contract: every hbitmap the slots hold is a top-down dib.
// the clipboard is the one publisher that answers bottom-up rows (a
// positive-height cf_dib, the format's own convention), and the
// renderers draw one mapping for all frames - the orientation is
// unrecoverable downstream (getobject answers the absolute height
// for both orientations; the sign that said top-down is already
// gone), so the paste normalizes before the pixels become a frame.
// a normalization failure keeps the original bitmap: the gdi leg
// never cared about the row order, and the hardware legs are no
// worse than they were.
static HBITMAP _viv_clipboard_top_down(HBITMAP hbitmap)
{
	BITMAP bm;
	BITMAPINFOHEADER bih;
	HDC hdc;
	HBITMAP dib;
	void *bits;

	if (!GetObject(hbitmap,sizeof(BITMAP),&bm))
	{
		return 0;
	}

	if ((bm.bmWidth <= 0) || (bm.bmHeight <= 0))
	{
		return 0;
	}

	os_zero_memory(&bih,sizeof(BITMAPINFOHEADER));
	bih.biSize = sizeof(BITMAPINFOHEADER);
	bih.biWidth = bm.bmWidth;
	bih.biHeight = -bm.bmHeight;
	bih.biPlanes = 1;
	bih.biBitCount = 32;
	bih.biCompression = BI_RGB;

	dib = 0;
	bits = NULL;

	hdc = GetDC(0);
	if (hdc)
	{
		dib = CreateDIBSection(hdc,(BITMAPINFO *)&bih,DIB_RGB_COLORS,&bits,NULL,0);
		if (dib)
		{
			// the negative request height answers top-down rows; gdi
			// reads the source's own orientation either way, so the
			// channel is idempotent for a paste that was already
			// top-down.
			// the full-line contract (the paint readback's own
			// check): a partial copy would leave the tail rows
			// uninitialized in the frame.
			if (GetDIBits(hdc,hbitmap,0,bm.bmHeight,bits,(BITMAPINFO *)&bih,DIB_RGB_COLORS) != bm.bmHeight)
			{
				DeleteObject(dib);
				dib = 0;
			}
		}

		ReleaseDC(0,hdc);
	}

	return dib;
}

static void _viv_show_clipboard_image(HBITMAP hbitmap,int wide,int high)
{
	// the pasted image is the new current - a successor window from
	// the folder it interrupted cannot serve the next step.
	_viv_nav_window_invalidate();
	
	{
		HBITMAP hbitmap_top_down;
		
		// the top-down frame contract: the renderers draw one
		// mapping for every frame, and the paste normalizes
		// before the pixels become a frame.
		hbitmap_top_down = _viv_clipboard_top_down(hbitmap);
		if (hbitmap_top_down)
		{
			DeleteObject(hbitmap);
			hbitmap = hbitmap_top_down;
		}
	}

	// stop an in flight file load from clobbering the pasted image.
	_viv_load_image_allow_draw = 0;
	InterlockedExchange(&_viv_load_image_terminate,1);
	
	// the queued next must not survive either: its COMPLETE would
	// dispatch the stranger over the pasted image (the terminate
	// above only stops the in-flight leg).
	if (_viv_load_image_next_fd)
	{
		mem_free(_viv_load_image_next_fd);
		
		_viv_load_image_next_fd = NULL;
	}
	
	// the activation exception rides above the terminate pair: a
	// preload the user navigated onto would still land over the
	// pasted image - retire the activation ask and the parked name
	// with the same stroke (the ring-hit path in _viv_open clears
	// the same pair).
	_viv_should_activate_preload_on_load = 0;
	_viv_slot_preload.fd.cFileName[0] = 0;
	
	// the current image moves to the last image slot, exactly like
	// navigating to a new image does.
	viv_copy_current_image_to_last_image();
	
	_viv_clear();
	
	_viv_slot_current.image_wide = wide;
	_viv_slot_current.image_high = high;
	_viv_slot_current.frame_count = 1;
	_viv_slot_current.alpha_baked = 0;
	_viv_slot_current.frame_loaded_count = 1;
	_viv_slot_current.frames = (_viv_frame_t *)mem_alloc(sizeof(_viv_frame_t));
	
	_viv_slot_current.frames[0].hbitmap = hbitmap;
	_viv_slot_current.frames[0].mipmap = 0; // built lazily on the first paint.
	_viv_slot_current.frames[0].delay = 0;
	
	// a clipboard image has no filename.
	_viv_current_fd->cFileName[0] = 0;
	
	_viv_update_title();
	
	_viv_start_first_frame();
	
	_viv_process_pending_clear();

	// the paste path never reaches a load-settle hook, so the
	// cache-set trim runs here: a pasted image is exactly as binding
	// on the ceiling as a loaded one, and the last cache it displaced
	// is the cache to drop.
	_viv_cache_set_trim();
}
// the clipboard must already be open by the caller.
// dib first: the system synthesizes a CF_DIB for nearly every image
// source. a plain bitmap handle is the fallback.
BOOL _viv_paste_clipboard_image(void)
{
	HGLOBAL hglobal;
	
	hglobal = GetClipboardData(CF_DIB);
	
	if (hglobal)
	{
		BITMAPINFOHEADER *bih;
		SIZE_T dib_size;
		
		// size before shape: the global's length is the fact every read
		// below hangs on. a short or hostile clipboard (one byte, a
		// truncated producer, a hand-built global) used to reach the
		// header dereference first, and the GlobalSize check only ran
		// after the copy math - every malformed entry read past its
		// allocation before the rejection. the length gate now runs
		// before the lock, and the whole header is proven present
		// before any field of it is read.
		dib_size = (SIZE_T)GlobalSize(hglobal);
		
		if (dib_size >= sizeof(BITMAPINFOHEADER))
		{
			bih = (BITMAPINFOHEADER *)GlobalLock(hglobal);
			if (bih)
			{
				// only the classic 40 byte header with an uncompressed (or
				// bitfields) layout is handled here, anything else falls through
				// to the CF_BITMAP copy below. the two new gates answer the
				// hostile corners the size math cannot carry: a height of
				// INT_MIN passes the nonzero test but negates into signed
				// overflow, and a palette count past the bit depth's own
				// table is malformed the same way - both fall through.
				if ((bih->biSize == sizeof(BITMAPINFOHEADER)) && (bih->biPlanes == 1) && (bih->biWidth > 0) && (bih->biHeight != 0) && (bih->biHeight != (-2147483647 - 1)) && ((bih->biCompression == 0 /* BI_RGB */) || (bih->biCompression == 3 /* BI_BITFIELDS */)) && ((bih->biBitCount == 1) || (bih->biBitCount == 4) || (bih->biBitCount == 8) || (bih->biBitCount == 16) || (bih->biBitCount == 24) || (bih->biBitCount == 32)) && ((bih->biBitCount > 8) || (bih->biClrUsed <= (DWORD)(1 << bih->biBitCount))))
				{
					DWORD color_count;
					int mask_size;
					int height;
					SIZE_T stride;
					SIZE_T pixels_size;
					SIZE_T total_needed;
					
					// the clipboard dib layout: header, bitfield masks (40 byte
					// headers with BI_BITFIELDS only), palette, bits.
					color_count = 0;
					if (bih->biBitCount <= 8)
					{
						color_count = bih->biClrUsed ? bih->biClrUsed : (1 << bih->biBitCount);
					}
					
					mask_size = ((bih->biCompression == 3 /* BI_BITFIELDS */) && ((bih->biBitCount == 16) || (bih->biBitCount == 32))) ? 12 : 0;
					
					height = bih->biHeight;
					if (height < 0)
					{
						height = -height;
					}
					
					// stride rides the safe multipliers end to end: width *
					// bitcount crosses dword_max for wide hostile headers (the
					// 32 bit leg) and the wrapped stride used to pass the size
					// check as a small number. every overflow saturates to
					// size_max and the total check below rejects it.
					stride = safe_size_mul(safe_size_add(safe_size_mul((SIZE_T)bih->biWidth,(SIZE_T)bih->biBitCount),31) / 32,4);
					
					pixels_size = safe_size_mul(stride,(SIZE_T)height);
					
					total_needed = safe_size_add((SIZE_T)bih->biSize + (SIZE_T)mask_size + (SIZE_T)color_count * 4,pixels_size);
					
					// the clipboard global must actually contain the whole
					// dib (header, masks, palette and bits) BEFORE the dib
					// section is created: CreateDIBSection reads the palette
					// and the bitfield masks off this header, so the old
					// order dereferenced them before proving they existed.
					if ((total_needed != SIZE_MAX) && (dib_size >= total_needed))
					{
						HDC screen_hdc;
						
						screen_hdc = GetDC(0);
						if (screen_hdc)
						{
							// the null initializer answers the sdl elevation on the older
							// analyzer: the create-dib-section call is the only writer and
							// the copy below rides the hbitmap guard it pairs with.
							void *bits = NULL;
							HBITMAP hbitmap;
							
							// apply the same decode-time pixel budget as the file loaders: a
							// hostile clipboard dib must not force a giant allocation.
							hbitmap = 0;
							if (!_viv_pixel_budget_refused(safe_size_mul((SIZE_T)bih->biWidth,(SIZE_T)height)))
							{
								hbitmap = CreateDIBSection(screen_hdc,(BITMAPINFO *)bih,DIB_RGB_COLORS,&bits,NULL,0);
							}
							if (hbitmap)
							{
								const char *src;
								
								src = (const char *)bih + bih->biSize + mask_size + color_count * 4;
								
								// size_t length: stride*height crosses int_max inside the 64-bit
								// pixel budget (400 mp x 4 bytes per pixel).
								os_copy_memory(bits,src,pixels_size);
								
								_viv_show_clipboard_image(hbitmap,(int)bih->biWidth,height);
								
								ReleaseDC(0,screen_hdc);
								GlobalUnlock(hglobal);
								
								return TRUE;
							}
							
							ReleaseDC(0,screen_hdc);
						}
					}
				}
				
				GlobalUnlock(hglobal);
			}
		}
	}
	
	// fall back to a plain bitmap handle.
	{
		HBITMAP hbitmap;
		
		hbitmap = (HBITMAP)GetClipboardData(CF_BITMAP);
		
		if (hbitmap)
		{
			HBITMAP hbitmap_copy;
			BITMAP bm;
			
			// the clipboard owns the original: make a private dib copy.
			// the source bitmap's own geometry prices the copy before it
			// runs - CopyImage allocates the full dib section up front,
			// and this path used to bypass the pixel budget every file
			// loader answers to (a hostile publisher's gigabyte-sized
			// clipboard bitmap replicated into the ui with no gate at
			// all). the budget reads the source's numbers; the copy is
			// re-validated through GetObject as before.
			if ((GetObject(hbitmap,sizeof(BITMAP),&bm)) && (bm.bmWidth > 0) && (bm.bmHeight > 0) && (!_viv_pixel_budget_refused(safe_size_mul((SIZE_T)bm.bmWidth,(SIZE_T)bm.bmHeight))))
			{
				hbitmap_copy = (HBITMAP)CopyImage(hbitmap,IMAGE_BITMAP,0,0,LR_CREATEDIBSECTION);
				
				if (hbitmap_copy)
				{
					if ((GetObject(hbitmap_copy,sizeof(BITMAP),&bm)) && (bm.bmWidth > 0) && (bm.bmHeight > 0))
					{
						_viv_show_clipboard_image(hbitmap_copy,bm.bmWidth,bm.bmHeight);
						
						return TRUE;
					}
					
					DeleteObject(hbitmap_copy);
				}
			}
		}
	}
	
	// nothing the paste path reads was on the clipboard (the caller
	// offers the text fallback when it wants it).
	return FALSE;
}
// get the save format for a filename extension. (0 = png, 1 = jpeg, 2 = bmp, -1 = unknown)
static int _viv_get_extension_format(const wchar_t *filename)
{
	int i;
	int last_dot;
	
	last_dot = -1;
	
	for(i=string_get_length(filename)-1;i>=0;i--)
	{
		if (filename[i] == L'\\')
		{
			break;
		}
		
		if (filename[i] == L'.')
		{
			last_dot = i;
			break;
		}
	}
	
	if (last_dot == -1)
	{
		return -1;
	}
	
	if (_viv_icompare_w(filename + last_dot + 1,L"png") == 0)
	{
		return 0;
	}
	
	if ((_viv_icompare_w(filename + last_dot + 1,L"jpg") == 0) || (_viv_icompare_w(filename + last_dot + 1,L"jpeg") == 0))
	{
		return 1;
	}
	
	if (_viv_icompare_w(filename + last_dot + 1,L"bmp") == 0)
	{
		return 2;
	}
	
	return -1;
}
// save the current image to a new file. (png, jpeg or bmp)
// re-encodes with the GDI+ built-in encoders, no new dependencies.
// the in-memory rotation is included, alpha is flattened over the window background.
void _viv_save_image_as(void)
{
	// the gate and the seed answer the image on screen (the pixels
	// below already do): during a first load the request fd names a
	// file nothing shows yet - that window would save one file's
	// name over another file's pixels.
	if (*_viv_slot_current.fd.cFileName)
	{
		if (_viv_slot_current.frame_count)
		{
			// do not save while the progressive preview is on screen:
			// frames[0] is still the low resolution thumbnail until the
			// full first frame lands.
			if (_viv_image_is_low_res)
			{
				wchar_t message_wbuf[STRING_SIZE];
				wchar_t caption_wbuf[STRING_SIZE];
				
				string_copy_utf8_string(message_wbuf,localization_get_string(LOCALIZATION_ID_STATUS_BAR_LOADING));
				string_copy_utf8_string(caption_wbuf,localization_get_string(LOCALIZATION_ID_APP_NAME));
				
				viv_msgbox(_viv_hwnd,caption_wbuf,message_wbuf,MB_OK|MB_ICONINFORMATION);
				
				return;
			}
			
			OPENFILENAME ofn;
			wchar_t tobuf[STRING_SIZE+1];
			wchar_t filter_wbuf[STRING_SIZE];
			wchar_t title_wbuf[STRING_SIZE];
			int i;
			int last_dot;
			
			os_zero_memory(&ofn,sizeof(OPENFILENAME));
			
			// default to the current filename with a .png extension.
			string_copy(tobuf,_viv_slot_current.fd.cFileName);
			
			last_dot = -1;
			
			for(i=string_get_length(tobuf)-1;i>=0;i--)
			{
				if (tobuf[i] == L'\\')
				{
					break;
				}
				
				if (tobuf[i] == L'.')
				{
					last_dot = i;
					break;
				}
			}
			
			if (last_dot != -1)
			{
				tobuf[last_dot] = 0;
			}
			
			string_cat(tobuf,L".png");
			
			string_printf(filter_wbuf,"%s (*.png)%c*.png%c%s (*.jpg)%c*.jpg%c%s (*.bmp)%c*.bmp%c",
				localization_get_string(LOCALIZATION_ID_SAVE_AS_PNG),0,
				localization_get_string(LOCALIZATION_ID_SAVE_AS_JPEG),0,
				localization_get_string(LOCALIZATION_ID_SAVE_AS_BMP),0,0);
			
			string_copy_utf8_string(title_wbuf,localization_get_string(LOCALIZATION_ID_SAVE_AS_CAPTION));
			
			ofn.lStructSize = sizeof(OPENFILENAME);
			ofn.hwndOwner = _viv_hwnd;
			ofn.hInstance = os_hinstance;
			ofn.lpstrFilter = filter_wbuf;
			ofn.nFilterIndex = 1;
			ofn.lpstrFile = tobuf;
			ofn.nMaxFile = STRING_SIZE;
			ofn.lpstrTitle = title_wbuf;
			ofn.Flags = OFN_ENABLESIZING | OFN_OVERWRITEPROMPT | OFN_NOCHANGEDIR;
			
			if (GetSaveFileName(&ofn))
			{
				int format;
				
				// use the typed extension when we recognize it, otherwise the selected filter.
				format = _viv_get_extension_format(tobuf);
				
				if (format == -1)
				{
					const wchar_t *extension;
					
					format = (int)ofn.nFilterIndex - 1;
					
					if ((format < 0) || (format > 2))
					{
						format = 0;
					}
					
					extension = (format == 1) ? L".jpg" : ((format == 2) ? L".bmp" : L".png");
					
					// make room for the extension: string_cat stops at the
					// STRING_SIZE budget, a full buffer would silently save
					// the file without its extension.
					if (string_get_length(tobuf) > ((STRING_SIZE - 1) - string_get_length(extension)))
					{
						tobuf[(STRING_SIZE - 1) - string_get_length(extension)] = 0;
					}
					
					string_cat(tobuf,extension);
				}
				
				// save the frame on screen, not frame 0.
				if (!os_save_hbitmap(_viv_slot_current.frames[_viv_frame_position].hbitmap,tobuf,format))
				{
					wchar_t message_wbuf[STRING_SIZE];
					wchar_t caption_wbuf[STRING_SIZE];
					
					string_copy_utf8_string(message_wbuf,localization_get_string(LOCALIZATION_ID_SAVE_AS_FAILED));
					string_copy_utf8_string(caption_wbuf,localization_get_string(LOCALIZATION_ID_APP_NAME));
					
					viv_msgbox(_viv_hwnd,caption_wbuf,message_wbuf,MB_OK|MB_ICONERROR);
				}
			}
		}
	}
}
void _viv_doing_cancel(void)
{
	if (_viv_doing)
	{
		int was_doing;
		
		was_doing = _viv_doing;
		
		_viv_doing = _VIV_DOING_NOTHING;
		
		if (was_doing == _VIV_DOING_MSCROLL)
		{
			ShowCursor(TRUE);
		}

		if (was_doing == _VIV_DOING_1TO1SCROLL)
		{
//			ShowCursor(TRUE);
			_viv_view_set(_viv_doing_x,_viv_doing_y,1);
			InvalidateRect(_viv_hwnd,0,FALSE);
		}
		
		ReleaseCapture();
	}
}
void _viv_blank(void)
{
	// close means close: the load it closed on must not answer over
	// the blank state - the FIRST_FRAME of the abandoned load would
	// land on screen and the queued next would follow it (the paste
	// path stops the same pair for the same reason).
	_viv_load_image_allow_draw = 0;
	InterlockedExchange(&_viv_load_image_terminate,1);
	
	if (_viv_load_image_next_fd)
	{
		mem_free(_viv_load_image_next_fd);
		
		_viv_load_image_next_fd = NULL;
	}
	
	// the activation exception rides above the terminate pair: a
	// preload the user navigated onto would still land over the
	// pasted image - retire the activation ask and the parked name
	// with the same stroke (the ring-hit path in _viv_open clears
	// the same pair).
	_viv_should_activate_preload_on_load = 0;
	_viv_slot_preload.fd.cFileName[0] = 0;
	
	_viv_clear();

	// the blank state has no file: the stale error flags from the last
	// load attempt must not outlive it (the field report: close after a
	// failed load kept "failed to load image" on the status bar with no
	// image open - the flags only reset on the next _viv_open).
	if (_viv_file_not_found)
	{
		_viv_file_not_found = 0;
	}

	if (_viv_load_failed)
	{
		_viv_load_failed = 0;
		_VIV_LOAD_REFUSED_CLEAR(_viv_load_refused_budget);
		_VIV_LOAD_REFUSED_CLEAR(_viv_load_refused_input_size);
		_viv_hw_render_fallback = 0;
	}

	if (_viv_random)
	{
		mem_free(_viv_random);
		
		_viv_random = 0;
	}
	
	_viv_current_fd->cFileName[0] = 0;

	_viv_update_src_pixel(1,0);
	_viv_status_update();
	_viv_update_title();

	// the toolbar's faces answer the blank state too: close, deleting
	// the last image and the empty-folder home all arrive here, and the
	// rotate, 1:1, best-fit and info faces staying enabled after the
	// last image left is the same face-state lie the navigation
	// sweeps kept finding.
	_viv_toolbar_update_buttons();

	// free all dropfiles
	_viv_playlist_clearall();

	_viv_start_first_frame();
	_viv_process_pending_clear();
}
// the input ceiling (the input-size round): the whole-file buffer is
// the first allocation a load makes and it happens before any pixel
// budget can see the file - a normal-sized image carrying a huge
// appended payload, a garbage tail or an oversized raw frame commits
// its bytes before the decoders ever answer. GetFileSizeEx reads the
// 64-bit size (the 32-bit GetFileSize cannot see a file past 4 gb: it
// answers the low dword there, and INVALID_FILE_SIZE only when that
// dword happens to be 0xffffffff). the over-ceiling refusal marks the
// status line so the reason reaches the user; an empty or unreadable
// size is simply an unloadable file. returns 0 when the size is sane
// (file_size filled in for the caller).
static int _viv_input_size_refused(HANDLE h,LARGE_INTEGER *file_size)
{
	if (!GetFileSizeEx(h,file_size))
	{
		debug_printf("GetFileSizeEx %x\n",GetLastError());
		
		return 1;
	}
	
	if (file_size->QuadPart <= 0)
	{
		debug_printf("empty file\n");
		
		return 1;
	}
	
	if ((VIV_UINT64)file_size->QuadPart > VIV_MAX_INPUT_FILE_BYTES)
	{
		debug_printf("input ceiling: refusing a %u mb file (ceiling %u mb)\n",(unsigned int)((VIV_UINT64)file_size->QuadPart / 1000000),(unsigned int)(VIV_MAX_INPUT_FILE_BYTES / 1000000));
		
		_VIV_LOAD_REFUSED_SET(_viv_load_refused_input_size);
		
		return 1;
	}
	
	return 0;
}

// the gdi+ frames answer as 24bpp top-down dib sections, the webp path's
// own shape (viv_anim.c). the hardware renderers read the bits through the
// dib contract and a ddb answers GetObject with bmBits == NULL: the gl/d3d
// gates refuse such frames, so every png/gif/bmp/jpeg frame used to fall
// back to the gdi path silently - the user picked a hardware renderer and
// the viewer painted with gdi anyway, while the pixel oracle recorded null
// after null that read as "no renderer on this machine". the drawing is
// unchanged (gdi+ into the selected section, the alpha composites over the
// backdrop exactly as before); only the storage answers to the contract
// both hardware paths read.
static HBITMAP _viv_load_create_frame_dib(HDC dc,int wide,int high)
{
	BITMAPINFO bmi;
	void *bits;
	
	ZeroMemory(&bmi,sizeof(BITMAPINFO));
	bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
	bmi.bmiHeader.biWidth = wide;
	bmi.bmiHeader.biHeight = -high; // top-down, the webp frames' own shape
	bmi.bmiHeader.biPlanes = 1;
	bmi.bmiHeader.biBitCount = 24;
	bmi.bmiHeader.biCompression = BI_RGB;
	
	return CreateDIBSection(dc,&bmi,DIB_RGB_COLORS,&bits,NULL,0);
}

// 14.447 - CreateStreamOnHGlobal - this is too slow over slow networks -which doesn't matter because the gif wont show the first frame until the entire gif is loaded anyway.
// 14.520 - CreateStreamOnHGlobal
// 15.834 - SHCreateStreamOnFile
// 15.850 - SHCreateStreamOnFile
// 14.914 - _viv stream - 1MB file buffer
// 14.914 - _viv stream
static DWORD WINAPI _viv_load_image_thread_proc(void *param)
{
	_viv_reply_load_image_first_frame_t first_frame;
	IStream *stream;
	int ret;
	int com_initialized;
	DWORD tickstart;
	int is_preload_job;
	
	tickstart = GetTickCount();
	
	// the job snapshot: the preload flag is ui-thread state (the dispatch
	// stages it before createthread, the reply handlers flip it when a
	// preload becomes the load to show, the activation paths clear it).
	// the thread reads it exactly twice, both before the first frame
	// posts - the snapshot pins those reads to the value the dispatch
	// staged, so no mid-flight ui write can ever race them: the formal
	// happens-before rides createthread, not the protocol's ordering.
	is_preload_job = _viv_load_is_preload;
	
	// the pairing flag, same rule as the ui thread's (see _viv_init):
	// a failed init owes no couninitialize; s_false succeeds and does.
	com_initialized = SUCCEEDED(CoInitializeEx(NULL, COINIT_APARTMENTTHREADED|COINIT_DISABLE_OLE1DDE));

	first_frame.wide = 0;
	first_frame.high = 0;
	first_frame.frame_count = 0;
	first_frame.frame.hbitmap = 0;
	first_frame.frame.mipmap = 0;
	first_frame.frame.delay = 0;
	first_frame.is_low_res = 0;
	first_frame.alpha_baked = 0;

	ret = 0;
	stream = NULL;
	
	debug_printf("%s %S...\n",is_preload_job ? "PRELOAD" : "LOAD",_viv_load_image_filename);

	// the stage marker: "open" while the file reads into memory, then
	// "decode" through the gdi+ attempt, "frames" inside the animation
	// loop, "done" when the reply posts (the fallback decoders set their
	// own stages at entry). the exit timeout reads it.
	InterlockedExchangePointer(&_viv_load_stage,(PVOID)"open");
	
	{
		HANDLE h;
		LARGE_INTEGER file_size;
		
		h = CreateFile(_viv_load_image_filename,GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE|((os_major_version >= 5) ? FILE_SHARE_DELETE : 0),0,OPEN_EXISTING,FILE_FLAG_SEQUENTIAL_SCAN,0);
		if ((h != INVALID_HANDLE_VALUE) && (!_viv_input_size_refused(h,&file_size)))
		{
			DWORD size;
			HANDLE global_handle;

			size = (DWORD)file_size.QuadPart;
			
			global_handle = GlobalAlloc(GMEM_MOVEABLE,size);
			if (global_handle)
			{
				char *buf;
				
				buf = (char *)GlobalLock(global_handle);
				
				if (buf)
				{
					DWORD numread;
					DWORD totreadsize;
					char *d;
					
					d = buf;
					totreadsize = size;
					
					while(totreadsize)
					{
						DWORD readsize;


						readsize = 1 * 1024 * 1024;
						
						if (readsize > totreadsize)
						{
							readsize = totreadsize;
						}
						
						if (ReadFile(h,d,readsize,&numread,0))
						{
							if (!numread)
							{
								break;
							}

							d += numread;
							totreadsize -= numread;
						}
						else
						{
							break;
						}
					}

					GlobalUnlock(global_handle);
					
					if (totreadsize == 0)
					{
						HRESULT hresult;
						
						hresult = CreateStreamOnHGlobal(global_handle,TRUE,&stream);
						
						if (SUCCEEDED(hresult))
						{
							debug_printf("got stream in %f seconds\n",(double)(GetTickCount()-tickstart) * 0.001);
							
							// the stream owns this handle now.
							global_handle = 0;
						}
						else
						{
							debug_printf("CreateStreamOnHGlobal %x\n",hresult);
							stream = 0;
						}
					}
					else
					{
						// the short read: the file changed size under the load (or
						// the media failed) - the truncated stream refuses like any
						// other unloadable file instead of feeding the decoders a
						// partial buffer that was sized for a file that no longer is.
						debug_printf("short read: %u of %u bytes (the file changed size or the media failed)\n",size - totreadsize,size);
					}
				}
				
				if (global_handle)
				{
					GlobalFree(global_handle);
				}
			}

			CloseHandle(h);
		}
		else
		{
			if (h != INVALID_HANDLE_VALUE)
			{
				// the input ceiling refusal: the helper already recorded it
				// (the debug channel and the status line name the reason);
				// the handle still closes.
				CloseHandle(h);
			}
			else
			{
				debug_printf("CreateFile %x\n",GetLastError());
			}
		}
	}



	if (stream)
	{
		int orientation;
		
		debug_printf("stream %d\n",stream);
		
		if (config_orientation)
		{
			orientation = os_get_orientation(_viv_load_image_filename);

			debug_printf("orientation %d\n",orientation);
		}
		else
		{
			orientation = 0;
		}

//		if (!_viv_load_image_terminate)
		{
			// the null initializer answers the sdl elevation: the proc-table
			// miss path reads image at the debug print before any assignment.
			void *image = NULL;
			int load_ret;
			
			// not implemented.
			load_ret = 1; 
			
			if ((os_GdipLoadImageFromStream) && (os_GdipLoadImageFromStreamICM) && (os_GdipGetImageWidth) && (os_GdipGetImageHeight) && (os_GdipImageGetFrameDimensionsCount) && (os_GdipImageGetFrameDimensionsList) && (os_GdipImageGetFrameCount) && (os_GdipGetPropertyItemSize) && (os_GdipGetPropertyItem) && (os_GdipImageSelectActiveFrame) && (os_GdipGetImageFlags) && (os_GdipDisposeImage) && (os_GdipCreateFromHDC) && (os_GdipSetCompositingMode) && (os_GdipSetCompositingQuality) && (os_GdipSetInterpolationMode) && (os_GdipSetPixelOffsetMode) && (os_GdipSetSmoothingMode) && (os_GdipDrawImageRectI) && (os_GdipDeleteGraphics))
			{
				// the decode attempt begins: gdi+ owns the file until it answers.
				InterlockedExchangePointer(&_viv_load_stage,(PVOID)"decode");

				if (config_icm)
				{
					load_ret = os_GdipLoadImageFromStreamICM(stream,&image);
				}
				else
				{
					load_ret = os_GdipLoadImageFromStream(stream,&image);
				}
			}
			
			debug_printf("image %p\n",image);

			if (load_ret == 0)
			{
//				if (!_viv_load_image_terminate)
				{
					if (os_GdipGetImageWidth(image,&first_frame.wide) == 0)
					{
						// pixel budget: a hostile or corrupted header can claim
						// a canvas that decodes to gigabytes of rgba. refuse the
						// load before any frame allocation happens; the dispose
						// below still runs so this fails like an unloadable file.
						if ((os_GdipGetImageHeight(image,&first_frame.high) == 0) && (!_viv_pixel_budget_refused(safe_size_mul((SIZE_T)first_frame.wide,(SIZE_T)first_frame.high))))
						{
							UINT count;
							int load_wide;
							int load_high;
							
							load_wide = first_frame.wide;
							load_high = first_frame.high;
							
							// apply orientation.
							switch (orientation)
							{
								case 2: // #define PHOTO_ORIENTATION_FLIPHORIZONTAL    2u
									break;
									
								case 5: // #define PHOTO_ORIENTATION_TRANSPOSE         5u
								case 6: // #define PHOTO_ORIENTATION_ROTATE270         6u
								case 7: // #define PHOTO_ORIENTATION_TRANSVERSE        7u
								case 8: // #define PHOTO_ORIENTATION_ROTATE90          8u
									{
										int temp;
										temp = first_frame.wide;
										first_frame.wide = first_frame.high;
										first_frame.high = temp;
									}
									break;
							}
							// progressive display: post the embedded thumbnail as a
							// low resolution first frame while the full decode continues.
							// cameras and phones embed a small thumbnail in the exif data;
							// gdi+ returns it without decoding a single scanline of the
							// full image, so a big image appears instantly (blurry) and
							// sharpens when the full frame arrives.
							if ((!is_preload_job) && (os_GdipGetImageThumbnail) && (((VIV_UINT64)load_wide * (VIV_UINT64)load_high) > 2000000))
							{
								UINT thumb_data_size;
								
								// PropertyTagThumbnailData 0x501A: only take this path when an
								// embedded thumbnail actually exists, otherwise gdi+ would
								// decode the full image just to build one.
								thumb_data_size = 0;
								
								if ((os_GdipGetPropertyItemSize(image,0x501A,&thumb_data_size) == 0) && (thumb_data_size > 0))
								{
									void *thumb_image;
									
									thumb_image = NULL;
									
									// request at most 160x120: gdi+ serves the embedded thumbnail
									// without decoding when the request fits inside it.
									if (os_GdipGetImageThumbnail(image,160,120,&thumb_image,NULL,NULL) == 0)
									{
										UINT thumb_wide;
										UINT thumb_high;
										
										thumb_wide = 0;
										thumb_high = 0;
										
										if ((os_GdipGetImageWidth(thumb_image,&thumb_wide) == 0) && (os_GdipGetImageHeight(thumb_image,&thumb_high) == 0) && (thumb_wide) && (thumb_high))
										{
											HDC screen_hdc;
											
											screen_hdc = GetDC(0);
											
											if (screen_hdc)
											{
												HDC mem_hdc;
												
												mem_hdc = CreateCompatibleDC(screen_hdc);
												
												if (mem_hdc)
												{
													HBITMAP hbitmap;
													
													hbitmap = _viv_load_create_frame_dib(screen_hdc,(int)thumb_wide,(int)thumb_high);
													
													if (hbitmap)
													{
														HGDIOBJ last_hbitmap;
														
														last_hbitmap = SelectObject(mem_hdc,hbitmap);
														
														{
															void *g;
															
															if (os_GdipCreateFromHDC(mem_hdc,&g) == 0)
															{
																// thumbnails carry no alpha worth preserving.
																os_GdipSetCompositingMode(g,1);
																
																// Gdiplus::InterpolationModeNearestNeighbor: the thumbnail
																// is drawn at its own size.
																os_GdipSetInterpolationMode(g,5);
																
																if (os_GdipDrawImageRectI(g,thumb_image,0,0,(int)thumb_wide,(int)thumb_high) == 0)
																{
																	// apply orientation: 5-8 swap the axes.
																	if ((orientation >= 5) && (orientation <= 8))
																	{
																		UINT temp;
																		
																		temp = thumb_wide;
																		thumb_wide = thumb_high;
																		thumb_high = temp;
																	}
																	
																	if (orientation > 1)
																	{
																		HBITMAP new_hbitmap;
																		
																		// GetDIBits must not see a bitmap still selected in a dc -
																		// the orientate helper reads the source through its own
																		// GetDIBits (the chrome module documents the same contract
																		// for its readbacks). the restore further down answers the
																		// non-oriented path and runs as a no-op here.
																		SelectObject(mem_hdc,last_hbitmap);
																		
																		new_hbitmap = _viv_orientate_hbitmap(hbitmap,orientation);
																		
																		if (new_hbitmap)
																		{
																			DeleteObject(hbitmap);
																			
																			hbitmap = new_hbitmap;
																		}
																	}
																	
																	{
																		_viv_reply_load_image_first_frame_t low_res_first_frame;
																		
																		low_res_first_frame.wide = thumb_wide;
																		low_res_first_frame.high = thumb_high;
																		low_res_first_frame.frame_count = 1;
																		low_res_first_frame.frame.hbitmap = hbitmap;
																		low_res_first_frame.frame.mipmap = NULL; // no mipmap: the full frame provides them
																		low_res_first_frame.frame.delay = 0;
																		low_res_first_frame.is_low_res = 1;
																		low_res_first_frame.alpha_baked = 0;
																		
																		_viv_reply_add(_VIV_REPLY_LOAD_IMAGE_FIRST_FRAME,sizeof(low_res_first_frame),&low_res_first_frame);
																		
																		// ownership moved to the reply.
																		hbitmap = 0;
																	}
																}
																
																os_GdipDeleteGraphics(g);
															}
														}
														
														SelectObject(mem_hdc,last_hbitmap);
													}
													
													if (hbitmap)
													{
														DeleteObject(hbitmap);
													}
												}
												
												DeleteDC(mem_hdc);
											}
											
											ReleaseDC(0,screen_hdc);
										}
									}
									
									if (thumb_image)
									{
										os_GdipDisposeImage(thumb_image);
									}
								}
							}
							


							//First of all we should get the number of frame dimensions
							//Images considered by GDI+ as:
							//frames[animation_frame_index][how_many_animation];
							if (os_GdipImageGetFrameDimensionsCount(image,&count) == 0)
							{
								os_PropertyItem_t *frame_delay;
								SIZE_T frame_delay_size;
								
								frame_delay = NULL;
								frame_delay_size = 0;
								
								//Now we should get the identifiers for the frame dimensions 
								{
									GUID *DimensionIDs;
									
									// validate the count from gdi+ before trusting it: a zero count
									// would read dimension set #0 outside a zero length allocation and
									// an oversized count would wrap the size multiplication.
									if ((count >= 1) && (count <= (SIZE_MAX / sizeof(GUID))))
									{
										DimensionIDs = mem_alloc(sizeof(GUID) * count);
										
										if (os_GdipImageGetFrameDimensionsList(image,DimensionIDs,count) == 0)
										{
											//For gif image , we only care about animation set#0
											os_GdipImageGetFrameCount(image,&DimensionIDs[0],&first_frame.frame_count);
										}
										
										mem_free(DimensionIDs);
									}
								}

								// the animation budget gates the frame array below: the canvas
								// ceiling above bounds one frame, but the array holds every decoded
								// frame at once, so the frame count and the total frame bytes carry
								// their own ceilings. on refusal the frame count drops to zero: the
								// loop builds nothing, the first-frame reply never fires and the load
								// fails like any other unloadable file (a low-res thumbnail already
								// posted is cleared by the failure reply the same way any failed load
								// is).
								if (_viv_animation_budget_refused(first_frame.frame_count,safe_size_mul((SIZE_T)first_frame.wide,(SIZE_T)first_frame.high)))
								{
									first_frame.frame_count = 0;
								}

								// get frame delays.
								if (first_frame.frame_count > 1)
								{
									UINT size;
									
									size = 0;
									
									// PropertyTagFrameDelay 0x5100: gdi+ leaves *size untouched
									// when the property is absent, so seed it and check the return.
									if ((os_GdipGetPropertyItemSize(image,0x5100,&size) == 0) && (size >= sizeof(os_PropertyItem_t)))
									{
										frame_delay = (os_PropertyItem_t *)mem_alloc(size);
										
										if (frame_delay)
										{
											frame_delay_size = size;
											
											// PropertyTagFrameDelay 0x5100: only trust the buffer when
											// gdi+ actually filled it.
											if (os_GdipGetPropertyItem(image,0x5100,size,frame_delay) != 0)
											{
												mem_free(frame_delay);
												frame_delay = 0;
												frame_delay_size = 0;
											}
										}
									}
									
									debug_printf("frame delay size %u\n",(unsigned int)frame_delay_size);
								}

								// draw frames.
								// the animation frame loop owns the next stretch of time.
								InterlockedExchangePointer(&_viv_load_stage,(PVOID)"frames");

								{
									HDC screen_hdc;
								
									screen_hdc = GetDC(0);
									
									if (screen_hdc)
									{
										HDC mem_hdc;
									
										mem_hdc = CreateCompatibleDC(screen_hdc);
										if (mem_hdc)
										{
											DWORD i;
											
											for(i=0;i<first_frame.frame_count;i++)
											{
												HBITMAP hbitmap;
												HGDIOBJ last_hbitmap;
												
												if ((i) && (_VIV_LOAD_TERMINATED()))
												{
													break;
												}
												
												hbitmap = _viv_load_create_frame_dib(screen_hdc,load_wide,load_high);
												if (hbitmap)
												{
													UINT image_flags;
													
													last_hbitmap = SelectObject(mem_hdc,hbitmap);
													
													os_GdipImageSelectActiveFrame(image,&_viv_FrameDimensionTime,i);

													os_GdipGetImageFlags(image,&image_flags);
													
													{
														void *g;
														int draw_ret;

														if (os_GdipCreateFromHDC(mem_hdc,&g) == 0)
														{
															// ImageFlagsHasAlpha = 0x0002,
															if (image_flags & 2)
															{
																// fill the backdrop under the transparent
															// pixels (cached brushes, a single FillRect).
															_viv_fill_backdrop(mem_hdc,load_wide,load_high);
														
																os_GdipSetCompositingMode(g,0);
															}
															else
															{
																os_GdipSetCompositingMode(g,1);
															}

															os_GdipSetCompositingQuality(g,1);
															
															// Gdiplus::InterpolationModeNearestNeighbor
															os_GdipSetInterpolationMode(g,5);
															
															// Gdiplus::PixelOffsetModeNone
															os_GdipSetPixelOffsetMode(g,3);
															
															// Gdiplus::SmoothingModeNone
															os_GdipSetSmoothingMode(g,3);

															draw_ret = os_GdipDrawImageRectI(g,image,0,0,load_wide,load_high);
															if (draw_ret != 0)
															{
																debug_printf("DrawImage failed %d\n",draw_ret);
															}
															
															os_GdipDeleteGraphics(g);
														}
													}

													if (orientation > 1)
													{
														HBITMAP new_hbitmap;
														
														// GetDIBits must not see a bitmap still selected in a dc -
														// the orientate helper reads the source through its own
														// GetDIBits (the chrome module documents the same contract
														// for its readbacks). the restore further down answers the
														// non-oriented path and runs as a no-op here.
														SelectObject(mem_hdc,last_hbitmap);
														
														new_hbitmap = _viv_orientate_hbitmap(hbitmap,orientation);
														if (new_hbitmap)
														{
															DeleteObject(hbitmap);
															hbitmap = new_hbitmap;
														}
													}
													
													SelectObject(mem_hdc,last_hbitmap);
												
													if (i)
													{
														_viv_frame_t frame;
														int mip_wide;
														int mip_high;
														UINT frame_data_value;

														frame.hbitmap = hbitmap;
														frame.mipmap = NULL;
														
														// therube: we are accessing bad data here for some images.
														// just use a value of 0 for bad data.
														frame_data_value = _viv_frame_delay_at(frame_delay,frame_delay_size,i);
														
														frame.delay = frame_data_value * 10;
															
														if (!frame.delay)
														{
															frame.delay = 100;
														}
														
														// the mipmap chain stays lazy here: the paint path builds it
														// for the frame on screen when the scaling wants it - an
														// animation paused on frame one no longer pays for a
														// thousand chains nobody looks at.

														_viv_reply_add(_VIV_REPLY_LOAD_IMAGE_ADDITIONAL_FRAME,sizeof(_viv_frame_t),&frame);
													}
													else
													{
														int mip_wide;
														int mip_high;
														UINT frame_data_value;

														first_frame.frame.hbitmap = hbitmap;
														first_frame.frame.mipmap = NULL;
														first_frame.frame.delay = 0;
														
														if (first_frame.frame_count > 1)
														{
															// therube: we are accessing bad data here for some images.
															// just use a value of 0 for bad data.
															frame_data_value = _viv_frame_delay_at(frame_delay,frame_delay_size,i);
															
															first_frame.frame.delay = frame_data_value * 10;
															
															if (!first_frame.frame.delay)
															{
																first_frame.frame.delay = 100;
															}
														}
															
														// preload first mipmap
														_viv_get_mipmap(hbitmap,first_frame.wide,first_frame.high,_viv_load_render_wide/2,_viv_load_render_high/2,&mip_wide,&mip_high,&first_frame.frame.mipmap);
							
														// the bake the first frame ran (alpha pixels got the mat
														// painted under them) rides the reply: the commit stamps
														// the slot, and the backdrop apply reloads only what
														// actually baked.
														first_frame.alpha_baked = (image_flags & 2) ? 1 : 0;
														_viv_reply_add(_VIV_REPLY_LOAD_IMAGE_FIRST_FRAME,sizeof(_viv_reply_load_image_first_frame_t),&first_frame);

														ret = 1;
													}
												}
											}
											
											DeleteDC(mem_hdc);
										}
										
										ReleaseDC(0,screen_hdc);
									}
								}

								if (frame_delay)
								{
									mem_free(frame_delay);
								}

								debug_printf("image loaded\n");
							}
						}
					}
				}

				os_GdipDisposeImage(image);
			}
			else
			{
				_viv_webp_t viv_webp;
				
				debug_printf("GDIPlus failed to load image %S %d\n",_viv_load_image_filename,load_ret);
				
				viv_webp.screen_hdc = NULL;
				viv_webp.mem_hdc = NULL;
				viv_webp.frame_index = 0;
				viv_webp.last_delay = 0;
				viv_webp.orientation = orientation;
				
				if (webp_load(stream,&viv_webp,
					(int (*)(void *,DWORD,DWORD,DWORD,int))_viv_webp_info_proc,
					(int (*)(void *,BYTE *,int))_viv_webp_frame_proc))
				{
					ret = 1;
				}
				else
				{
					debug_printf("libwebp failed to load image %S\n",_viv_load_image_filename);

					// we loaded at least one frame, so treat it as complete.
					if (viv_webp.frame_index > 0)
					{
						ret = 1;
					}
					else
					{
						// the format horizons: qoi is hand-rolled (no system codec knows
						// it, works everywhere the viewer runs), wic defers to the system
						// codecs for everything else (jpeg-xr, dds, heif, avif where the
						// os carries them). the webp state is untouched at this point (the
						// info callback never ran), so both ride the same generic frame
						// delivery.
						if (qoi_load(stream,&viv_webp,
							(int (*)(void *,DWORD,DWORD,DWORD,int))_viv_webp_info_proc,
							(int (*)(void *,BYTE *,int))_viv_webp_frame_proc))
						{
							ret = 1;
						}
						else if (wic_load(stream,&viv_webp,
							(int (*)(void *,DWORD,DWORD,DWORD,int))_viv_webp_info_proc,
							(int (*)(void *,BYTE *,int))_viv_webp_frame_proc))
						{
							ret = 1;
						}
						else
						{
							debug_printf("qoi and wic failed to load image %S\n",_viv_load_image_filename);
						}
					}
				}

				if (viv_webp.mem_hdc)
				{
					DeleteDC(viv_webp.mem_hdc);
				}

				if (viv_webp.screen_hdc)
				{
					ReleaseDC(NULL,viv_webp.screen_hdc);
				}
			}
		}

		stream->lpVtbl->Release(stream);				
	}
	else
	{
		debug_printf("Failed to create stream from %S\n",_viv_load_image_filename);
	}

	InterlockedExchangePointer(&_viv_load_stage,(PVOID)"done");

	_viv_reply_add(ret ? _VIV_REPLY_LOAD_IMAGE_COMPLETE : _VIV_REPLY_LOAD_IMAGE_FAILED,0,0);
	
	if (com_initialized)
	{
		CoUninitialize();
	}
	
	debug_printf("loaded in %f seconds\n",(double)(GetTickCount()-tickstart) * 0.001);

    return 0;
}
void _viv_reply_free(_viv_reply_t *e)
{
	switch (e->type)
	{
		case _VIV_REPLY_LOAD_IMAGE_FIRST_FRAME:
			
			if (((_viv_reply_load_image_first_frame_t *)(e+1))->frame.hbitmap)
			{
				DeleteObject(((_viv_reply_load_image_first_frame_t *)(e+1))->frame.hbitmap);
			}
		
			if (((_viv_reply_load_image_first_frame_t *)(e+1))->frame.mipmap)
			{
				_viv_mipmap_free(((_viv_reply_load_image_first_frame_t *)(e+1))->frame.mipmap);
			}

			break;

		case _VIV_REPLY_LOAD_IMAGE_ADDITIONAL_FRAME:
		
			if (((_viv_frame_t *)(e + 1))->hbitmap)
			{	
				DeleteObject(((_viv_frame_t *)(e + 1))->hbitmap);
			}

			if (((_viv_frame_t *)(e + 1))->mipmap)
			{	
				_viv_mipmap_free(((_viv_frame_t *)(e + 1))->mipmap);
			}
		
			break;
	}
	
	mem_free(e);
}
_viv_reply_t *_viv_reply_add(DWORD type,DWORD size,void *data)
{
	_viv_reply_t *e;
	int is_first;
	
	e = (_viv_reply_t *)mem_alloc(safe_size_add(sizeof(_viv_reply_t),size));
	
	e->type = type;
	e->size = size;
	
	os_copy_memory(e + 1,data,size);
	
	EnterCriticalSection(&_viv_cs);
	
	if (_viv_reply_start)
	{
		_viv_reply_last->next = e;
	}
	else
	{
		_viv_reply_start = e;
	}
	
	_viv_reply_last = e;
	e->next = 0;
	
	// the wakeup duty: exactly one enqueue per drain cycle posts. the
	// old shape posted only on the empty-to-nonempty transition and
	// dropped the post's return value - one refused post (queue full,
	// window gone) and the queue stayed non-empty forever: no later
	// enqueue would post again, the drain never ran, and the exit path
	// waited its ten seconds and exitprocess(1)'d. the duty flag makes
	// the retry explicit: a refused post hands the duty back.
	is_first = !_viv_reply_posted;
	_viv_reply_posted = 1;
	
	LeaveCriticalSection(&_viv_cs);
	
	if ((is_first) && (!PostMessage(_viv_hwnd,_VIV_WM_REPLY,0,0)))
	{
		// the post was refused. give the duty back so the next enqueue
		// posts again (and the drain's tail repost takes the entries if
		// one runs first). the reset races no one: the drain only clears
		// the flag after taking the entries, and a refused post means no
		// drain ever saw them.
		EnterCriticalSection(&_viv_cs);
		
		_viv_reply_posted = 0;
		
		LeaveCriticalSection(&_viv_cs);
	}
	
	return e;
}
void _viv_reply_clear_all(void)
{
	_viv_reply_t *e;
	
	EnterCriticalSection(&_viv_cs);
	
	e = _viv_reply_start;
	_viv_reply_start = 0;
	_viv_reply_last = 0;
	
	LeaveCriticalSection(&_viv_cs);
	
	while(e)
	{
		_viv_reply_t *next_e;
		
		next_e = e->next;
		
		_viv_reply_free(e);
		
		e = next_e;
	}
}
// the cache-set arithmetic: one slot's held bytes, priced as the
// worst-case 32bpp frame set with the mipmap chain's extra third
// (the gdi+ path builds 24bpp frames, so the estimate runs a third
// heavy there - the conservative side is the safe side for a gate
// whose whole job is to refuse). every multiplication goes through
// the safe helpers: a hostile dimension pair must not wrap the
// arithmetic into an "it fits" answer, and the SIZE_MAX sentinel
// refuses on its own.
static SIZE_T _viv_frame_set_bytes(int wide,int high,int frame_count)
{
	SIZE_T pixels;
	SIZE_T bytes;

	if ((wide <= 0) || (high <= 0) || (frame_count <= 0))
	{
		return 0;
	}

	pixels = safe_size_mul((SIZE_T)(unsigned int)wide,(SIZE_T)(unsigned int)high);
	bytes = safe_size_mul(pixels,4);
	bytes = safe_size_mul(bytes,(SIZE_T)(unsigned int)frame_count);
	bytes = safe_size_mul(bytes,4);

	if (bytes == SIZE_MAX)
	{
		return SIZE_MAX;
	}

	return bytes / 3;
}
// one slot's held bytes: the estimator above is the arithmetic, this
// is the slot-domain wrapper the ceiling prices through - the three
// slots, and any fourth the future adds, all price the same way.
static SIZE_T _viv_slot_bytes(const _viv_image_slot_t *slot)
{
	return _viv_frame_set_bytes(slot->image_wide,slot->image_high,slot->frame_count);
}
// the cache-set ceiling: the budget gates price one image at a time
// while the viewer holds up to three - the current image, the
// last-image cache and the preload slot. the sum answers against
// the same byte ceiling the per-image gates use, but at a different
// unit price, and the fourth audit is right that the old comment
// papered over the difference: the working-set gate prices a whole
// image's residency at 12 bytes per pixel (the decode canvas, the
// display bitmap, the render scratch), while this sum prices only
// the held frames at 32bpp plus the mipmap chain's extra third
// (16/3 bytes per pixel) - so three slots of held frames compete
// against the same ceiling one image's whole working set does, and
// the gdi+ path's 24bpp frames make the estimate a third heavy
// besides (the conservative side is the safe side for a gate whose
// whole job is to refuse). the
// pending-clear slot never joins the sum: every handler that fills
// it frees it before returning, so it is empty wherever these gates
// run.
static int _viv_cache_set_over_ceiling(SIZE_T incoming_bytes)
{
	SIZE_T total;
	int i;

	total = _viv_slot_bytes(&_viv_slot_current);

	// the ring seats join the sum one by one - the cache count no
	// longer caps what the ceiling prices at three.
	for(i=0;i<VIV_CACHE_SLOTS;i++)
	{
		total = safe_size_add(total,_viv_slot_bytes(&_viv_slot_cache[i]));
	}

	total = safe_size_add(total,_viv_slot_bytes(&_viv_slot_preload));
	total = safe_size_add(total,incoming_bytes);

	if (total == SIZE_MAX)
	{
		return 1;
	}

	// the cache set answers its own ceiling, half the image line:
	// opportunistic residency never deserved the same budget a
	// displayed image's whole working set rides.
	return total > VIV_CACHE_SET_MAX_BYTES;
}
// the preload fill gate: the one load nobody asked for must never
// push the set over the ceiling. the current image and the last
// cache are priced beside the incoming frames; over the ceiling the
// answer is "don't cache this" - silent, exactly like every other
// background preload failure, and navigation onto the file takes the
// normal load path (the caller clears the staged fd).
int _viv_preload_set_refused(int wide,int high,int frame_count)
{
	return _viv_cache_set_over_ceiling(_viv_frame_set_bytes(wide,high,frame_count));
}
// the settle-point trim: over the ceiling the last cache goes and
// the current image stays - a cache is opportunistic and the image
// on screen is not. the preload slot is never trimmed here: an
// in-flight decode may still be filling it, and its own fill gate
// already priced it.
static void _viv_cache_set_trim(void)
{
	int i;

	if (_viv_cache_set_over_ceiling(0))
	{
		// the ring goes first, oldest seat to newest: a cache is
		// opportunistic and the image on screen is not.
		for(i=VIV_CACHE_SLOTS-1;i>=0;i--)
		{
			if (_viv_slot_cache[i].frames)
			{
				_viv_slot_clear_frames(&_viv_slot_cache[i]);
				
				if (!_viv_cache_set_over_ceiling(0))
				{
					return;
				}
			}
		}
		
		// the ring is empty and the set is still over the line: the
		// finished preload leaves too. nothing is in flight inside a
		// state-1 slot - an in-flight decode never pays this price.
		if (_viv_slot_preload.state == 1)
		{
			_viv_clear_preload();
		}
	}
}
void _viv_preload_next(void)
{
	WIN32_FIND_DATA window_fd;

	// the trim runs before the config gate: the cache-set ceiling is a
	// memory promise, not a convenience, and it binds the users who
	// turned preloading off just as much as the ones who left it on.
	_viv_cache_set_trim();

	if (config_preload_count > 0)
	{
		//UpdateWindow(_viv_hwnd);
		
		// the successor window answers the chain without a scan:
		// the entry at the chain's own depth is the next image the
		// walk has not promoted. the chain count is the cursor - a
		// landing resets it to zero at the same moment the window's
		// head moves to the landed file, so both point at the same
		// next image.
		if (_viv_nav_window_peek(_viv_preload_chain_count,&window_fd))
		{
			_viv_open(&window_fd,1);
		}
		else
		{
			_viv_next(_viv_last_is_prev,0,1,0);
		}
	}
}
// the chain walk: a finished preload that is not the last of its chain
// promotes into the cache ring and the walk continues. the last image
// of the chain keeps the slot - the navigation's first hit lands there.
// the gates: the count gate stops the walk at the promised images, and
// the seat gate stops it one seat short of the ring's capacity - the
// reserved seat is the back-navigation's: a walk that filled every
// seat would evict the image the user just left, and every direction
// change would pay the disk again (the upgrade field report: preload
// raised past a one-seat cache, the walk took the only seat, the
// cached previous image was gone). the parked slot plus the unreserved
// seats is all the ahead the machine can hold - the effective promise
// is min(preload, cache); a cache count of zero or one parks the chain
// at one image (the second would have nowhere to live).
void _viv_preload_chain_walk(void)
{
	// the activation just took the slot's contents - a counter that
	// passes while the slot sits empty inserts the empty seat anyway:
	// a ghost seat that evicts a real neighbour and counts itself
	// forever. the frames term refuses the walk until a finished
	// preload is actually in hand.
	if ((_viv_preload_chain_count + 1 < config_preload_count) && (_viv_preload_chain_count + 1 < config_cache_count) && (_viv_slot_preload.frames))
	{
		_viv_cache_insert(&_viv_slot_preload);
		_viv_preload_chain_count++;

		_viv_preload_next();
	}
}

// the preload has completed and we 
// want to set it as the current image.
void _viv_activate_preload(void)
{
debug_printf("activate preload\n");

	_viv_clear();

	// the whole slot moves in one take: the file identity, the frames,
	// both counts and the dimensions, and the source is left empty.
	// the take moves the fd with the frames, one status update
	// earlier than the hand-written path did - the empty-name flash
	// between the clear and the late fd copy is gone.
	_viv_slot_take(&_viv_slot_current,&_viv_slot_preload);

	_viv_start_first_frame();

	_viv_process_pending_clear();
}
// the ring find: the first seat whose file matches. both navigation
// directions read the same ring - the preload chain promotes ahead
// of the walker, the browsing history sits behind it.
static int _viv_cache_find(const wchar_t *filename)
{
	int i;

	for(i=0;i<VIV_CACHE_SLOTS;i++)
	{
		if ((_viv_slot_cache[i].frames) && (string_compare(_viv_slot_cache[i].fd.cFileName,filename) == 0))
		{
			return i;
		}
	}

	return -1;
}
// the ring insert: the source slot's whole content takes the head
// seat. a full active run drops its oldest entry first, everyone
// shifts down one seat. the settings page clears the ring whenever
// config_cache_count changes, so the active run stays dense and this
// walk never crosses a hole. a count of zero never reaches the walk
// (every caller gates on it) and the guard here makes that doubly
// true.
static void _viv_cache_insert(_viv_image_slot_t *src)
{
	int i;

	if (config_cache_count <= 0)
	{
		return;
	}

	// a full run drops the oldest entry.
	if (_viv_slot_cache[config_cache_count-1].frames)
	{
		_viv_slot_clear_frames(&_viv_slot_cache[config_cache_count-1]);
	}

	for(i=config_cache_count-1;i>0;i--)
	{
		_viv_slot_cache[i] = _viv_slot_cache[i-1];
	}

	_viv_slot_take(&_viv_slot_cache[0],src);
	
	// the ceiling settled only at the two quiet points; a navigation
	// burst could push past it in between. one trim on insert keeps
	// the promise live at every seat change instead of only at the
	// settle.
	_viv_cache_set_trim();
}
// push the current image into the cache ring's head (the name keeps
// the historical export surface; the single last slot became the
// ring).
void viv_copy_current_image_to_last_image(void)
{
debug_printf("*** Cache PUSH : %S\n",_viv_slot_current.fd.cFileName);

	// only cache if the whole image was loaded.
	// Otherwise we need to reload the whole image again..
	if (config_cache_count > 0)
	{
		if (_viv_slot_current.frame_count == _viv_slot_current.frame_loaded_count)
		{
			// the whole current slot moves into the ring's head in one
			// take: the file identity, the frames, both counts and the
			// dimensions. the source is left empty for the incoming
			// image.
			_viv_cache_insert(&_viv_slot_current);
		}
	}
}
static void _viv_cache_activate(int index)
{
	// a zero-count ring is empty by construction (the insert
	// refuses, the settings change clears); the tail write below
	// would answer seat [-1]. the guard asks the same question the
	// insert's own entry guard does.
	if (config_cache_count <= 0)
	{
		return;
	}
	_viv_image_slot_t old_slot;
	BYTE old_valid;
	int i;
	int active;

debug_printf("activate cache %d\n",index);

	old_valid = 0;

	// save the current image so it can enter the ring as the newest
	// entry below. the whole current image is saved in one copy -
	// the frames belong to the save now: detach them before
	// _viv_clear runs, or its pending-clear mailbox would stash
	// (and later free) the very frames being saved.
	if (config_cache_count > 0)
	{
		if (_viv_slot_current.frame_count == _viv_slot_current.frame_loaded_count)
		{
debug_printf("*** Cache ACTIVATE : %S\n",_viv_slot_current.fd.cFileName);

			old_slot = _viv_slot_current;
			old_valid = 1;

			_viv_slot_current.frames = NULL;
			_viv_slot_current.frame_count = 0;
			_viv_slot_current.frame_loaded_count = 0;
			_viv_slot_current.image_wide = 0;
			_viv_slot_current.image_high = 0;
			_viv_slot_current.alpha_baked = 0;
			_viv_slot_current.fd.cFileName[0] = 0;
		}
	}
	
	os_copy_memory(_viv_current_fd,&_viv_slot_cache[index].fd,sizeof(WIN32_FIND_DATA));
	
	// the folder fact belongs to the position being left.
	_viv_nav_folder_neighbor = -1;
	
	_viv_update_title();
	_viv_status_update();
	
	_viv_clear();

	// the whole hit slot moves into the current slot in one take.
	_viv_slot_take(&_viv_slot_current,&_viv_slot_cache[index]);
	
	// the hole closes: the entries above the hit slide down one
	// seat. the active run is dense ([0..active-1], the count the
	// settings pin) and the hit sat inside it, so the vacated tail
	// detaches its stale copies instead of freeing them - the
	// frames already moved down with the shift.
	active = config_cache_count;
	
	for(i=index;i<active-1;i++)
	{
		_viv_slot_cache[i] = _viv_slot_cache[i+1];
	}
	
	_viv_slot_cache[active-1].frames = NULL;
	_viv_slot_cache[active-1].frame_count = 0;
	_viv_slot_cache[active-1].frame_loaded_count = 0;
	_viv_slot_cache[active-1].image_wide = 0;
	_viv_slot_cache[active-1].image_high = 0;
	_viv_slot_cache[active-1].fd.cFileName[0] = 0;
	
	_viv_start_first_frame();

	_viv_process_pending_clear();

	if (old_valid)
	{
		// the saved image becomes the newest cache entry - the
		// ping-pong the two takes used to spell, generalized to the
		// ring.
		_viv_cache_insert(&old_slot);
	}
}
// the ring clears through the same slot primitive the preload slot
// uses, one seat at a time - the settings page calls this when the
// cache count changes, so the dense-run invariant every walk relies
// on is rebuilt from zero.
void _viv_clear_last(void)
{
	int i;
	
	for(i=0;i<VIV_CACHE_SLOTS;i++)
	{
		_viv_slot_clear_frames(&_viv_slot_cache[i]);
	}
}

void _viv_refresh(void)
{
	WIN32_FIND_DATA fd;
	
	// the user asked for the directory's truth - the successor
	// window's memory of it is gone.
	_viv_nav_window_invalidate();
	
	os_copy_memory(&fd,_viv_current_fd,sizeof(WIN32_FIND_DATA));

	// clear last cache
	_viv_clear_last();

	// clear preload.
	_viv_clear_loading_preload();
	_viv_clear_preload();

	_viv_clear();
	_viv_start_first_frame();
	_viv_process_pending_clear();

	// no file open: the reload below would only fail against the empty
	// name and pin a load error on the blank window (the field report:
	// changing the backdrop or the canvas color on the bare program showed
	// a load failure at the bottom left). the blank state is the correct
	// result there: reset the stale flags and keep the canvas.
	if (!fd.cFileName[0])
	{
		if (_viv_file_not_found)
		{
			_viv_file_not_found = 0;
			
			_viv_status_update();
		}
		
		if (_viv_load_failed)
		{
			_viv_load_failed = 0;
			_VIV_LOAD_REFUSED_CLEAR(_viv_load_refused_budget);
			_VIV_LOAD_REFUSED_CLEAR(_viv_load_refused_input_size);
			_viv_hw_render_fallback = 0;
			
			_viv_status_update();
		}
		
		return;
	}

	_viv_open(&fd,0);
}
void _viv_open_preload(void)
{
	os_copy_memory(_viv_current_fd,&_viv_slot_preload.fd,sizeof(WIN32_FIND_DATA));

	// landing on the parked preload is a settle: the chain starts
	// over from the file now on screen.
	_viv_preload_chain_count = 0;
	
	// the folder fact belongs to the position being left.
	_viv_nav_folder_neighbor = -1;
	
	_viv_update_title();
	
	if (_viv_slot_preload.state == 0)
	{
		// loading...
		// do we have the first frame?
		if (_viv_slot_preload.frame_loaded_count)
		{
			// save current image to last image.
			viv_copy_current_image_to_last_image();

			// switch now..
			_viv_load_is_preload = 0;
			_viv_activate_preload();
			_viv_status_update();
		}
		else
		{
			// still loading...
			// wait...
			if (!_viv_should_activate_preload_on_load)
			{
				_viv_should_activate_preload_on_load = 1;
			
				// show loading...
				_viv_status_update();
			}
			
			debug_printf("STILL LOADING...\n");
		}
	}
	else
	if (_viv_slot_preload.state == 1)
	{
		// save current image to last image.
		viv_copy_current_image_to_last_image();

		// loaded
		// preload next below..
		_viv_load_is_preload = 0;
		_viv_status_update();
		
		_viv_activate_preload();
		
		_viv_preload_next();
	}
	else
	{
		// save current image to last image.
		viv_copy_current_image_to_last_image();

		// failed.
		// preload next below..
		_viv_load_failed = 1;
		_viv_load_is_preload = 0;
		_viv_status_update();
		
		_viv_clear();
		
		_viv_start_first_frame();
		
		_viv_process_pending_clear();
		
		_viv_preload_next();
	}	
}
// read the 10ms delay of frame i. returns 0 on any failure, the caller
// falls back to 100ms. the gdi+ value pointer is validated against the
// property buffer before it is used, and the delay array may hold fewer
// entries than there are frames (the gif GCE block is optional), so delays
// are reused modulo the available count.
static int _viv_pixel_budget_refused(SIZE_T pixels)
{
	if (pixels > VIV_MAX_IMAGE_PIXELS)
	{
		debug_printf("pixel budget: refusing a %u mp canvas (ceiling %u mp)\r\n",(unsigned int)(pixels / 1000000),(unsigned int)(VIV_MAX_IMAGE_PIXELS / 1000000));
		
		_VIV_LOAD_REFUSED_SET(_viv_load_refused_budget);
		
		return 1;
	}
	
	// the working-set gate: the canvas ceiling bounds one buffer, but the
	// load holds several at once (the decode canvas, the display dib, the
	// renderer staging - 12 bytes per pixel priced). the refusal marks the
	// status line so the reason reaches the user, not only the debug
	// channel.
	if ((VIV_UINT64)pixels * VIV_IMAGE_WORKING_SET_BYTES_PER_PIXEL > VIV_MAX_IMAGE_BYTES)
	{
		debug_printf("working set budget: refusing a %u mp canvas (%u mb estimated, ceiling %u mb)\r\n",(unsigned int)(pixels / 1000000),(unsigned int)(((VIV_UINT64)pixels * VIV_IMAGE_WORKING_SET_BYTES_PER_PIXEL) / 1000000),(unsigned int)(VIV_MAX_IMAGE_BYTES / 1000000));
		
		_VIV_LOAD_REFUSED_SET(_viv_load_refused_budget);
		
		return 1;
	}
	
	return 0;
}
// the animation frame-array gate: the canvas ceilings above bound one
// frame, but the loader holds every decoded frame at once. a small canvas
// carrying tens of thousands of frames is a multi-gigabyte commitment the
// canvas gate never sees, so the frame count and the total frame bytes
// carry their own ceilings (4 bytes per canvas pixel per frame - the
// display bitmap size the gdi+ path builds per frame).
static int _viv_animation_budget_refused(DWORD frame_count,SIZE_T canvas_pixels)
{
	if (frame_count > VIV_MAX_ANIMATION_FRAMES)
	{
		debug_printf("animation budget: refusing %u frames (ceiling %u)\r\n",(unsigned int)frame_count,(unsigned int)VIV_MAX_ANIMATION_FRAMES);
		
		_VIV_LOAD_REFUSED_SET(_viv_load_refused_budget);
		
		return 1;
	}
	
	// 16/3 bytes per canvas pixel per frame: the 32bpp DIB frames the
	// loader holds plus the mipmap chain's extra third the lazy build
	// still fills in as the animation plays.
	if ((VIV_UINT64)frame_count * (VIV_UINT64)canvas_pixels * 16 / 3 > VIV_MAX_ANIMATION_TOTAL_BYTES)
	{
		debug_printf("animation budget: refusing %u frames of a %u mp canvas (%u mb of frames, ceiling %u mb)\r\n",(unsigned int)frame_count,(unsigned int)(canvas_pixels / 1000000),(unsigned int)(((VIV_UINT64)frame_count * (VIV_UINT64)canvas_pixels * 16 / 3) / 1000000),(unsigned int)(VIV_MAX_ANIMATION_TOTAL_BYTES / 1000000));
		
		_VIV_LOAD_REFUSED_SET(_viv_load_refused_budget);
		
		return 1;
	}
	
	return 0;
}
int _viv_safe_copy_data(const void *base,SIZE_T src_size,SIZE_T offset,void *dst,SIZE_T dst_size)
{
	const BYTE *p;
	BYTE *d;
	SIZE_T run;
	SIZE_T end;
	
	// the caller hands a distance into the buffer, never a pointer it
	// formed first: the range is proven (offset + dst_size inside
	// src_size, overflow-checked) before the one pointer this function
	// ever forms. the old pointer-in signature made every caller build
	// base + offset before the validation ran - harmless on windows
	// flat addressing, but the offset is the shape that cannot lie.
	end = safe_size_add(offset,dst_size);
	
	if (end == SIZE_MAX)
	{
		return 0;
	}
	
	if (end > src_size)
	{
		return 0;
	}
	
	p = ((const BYTE *)base) + offset;
	d = (BYTE *)dst;
	run = dst_size;
	
	while(run)
	{
		*d++ = *p;
		p++;
		run--;
	}
	
	return 1;
}
