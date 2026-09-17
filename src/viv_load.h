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
// viv_load.h - image loading, decode thread, clipboard images, preload.
#ifndef VIV_LOAD_H
#define VIV_LOAD_H

#include "viv.h"

// exported to other domains / the viv.c core
void _viv_clear(void);
void _viv_process_pending_clear(void);
void _viv_clear_loading_preload(void);
void _viv_clear_preload_frames(void);
void _viv_clear_preload(void);
// the recent-list policy of an open-by-name, declared where the intent is
// knowable - never guessed at the push site.
// VIV_OPEN_RECENT: a user command (the open dialog, the drag-drop, the
// recent click) feeds the recent list unconditionally -
// the standard mru contract, a re-open of the displayed file re-tops it.
// VIV_OPEN_FORWARDED: an externally forwarded open (the single-instance
// forward of a second launch, the re-entry the rotate verb's refresh and
// the rotate-then-recheck double-click ride) feeds the list only when the
// file is not the one already on screen: a same-file forward is a reload,
// not a recent open. the startup command line shares this site and is
// vacuously fine - no file is on screen yet, so the first open still
// enters the list.
#define VIV_OPEN_RECENT     1
#define VIV_OPEN_FORWARDED  0
BOOL _viv_open_from_filename(const wchar_t *filename,int recent_policy);
void _viv_open(WIN32_FIND_DATA *fd,int is_preload);
void _viv_set_clipboard_image(void);
void _viv_paste_clipboard_image(void);
void _viv_save_image_as(void);
void _viv_doing_cancel(void);
void _viv_blank(void);
void _viv_reply_free(_viv_reply_t *e);
_viv_reply_t *_viv_reply_add(DWORD type,DWORD size,void *data);
void _viv_reply_clear_all(void);
void _viv_preload_next(void);
int _viv_preload_set_refused(int wide,int high,int frame_count);
void _viv_activate_preload(void);
void viv_copy_current_image_to_last_image(void);
void _viv_clear_last(void);
void _viv_refresh(void);
void _viv_open_preload(void);
int _viv_safe_copy_data(const void *base,SIZE_T src_size,const void *src,void *dst,SIZE_T dst_size);

#endif
