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
// Temporary rotation previews and their worker lifecycle.
#include "viv.h"
#include "viv_state.h"
#include "viv_anim.h"
#include "viv_chrome.h"
#include "viv_dialogs.h"
#include "viv_render.h"
#include "viv_rotation.h"
#include "viv_view.h"

// The UI keeps the source frames alive until the worker stops reading them.
typedef struct
{
	HANDLE thread;
	HANDLE snapshot;
	HANDLE preview;
	_viv_frame_t *frames;
	BITMAP *bitmaps;
	HBITMAP *prepared;
	int count;
	int frame_position;
	int turns;
	int release_frames;
	volatile LONG cancel;
	wchar_t original[STRING_SIZE];
} _viv_rotation_job_t;

static _viv_rotation_job_t *_viv_rotation_job;
static HANDLE _viv_rotation_source;
static HANDLE _viv_rotation_preview;
static int _viv_rotation_turns;

int _viv_rotation_active(void)
{
	return _viv_rotation_preview != NULL;
}

int _viv_rotation_busy(void)
{
	return _viv_rotation_turns || (_viv_rotation_job && _viv_rotation_job->frames == _viv_slot_current.frames);
}

void _viv_rotation_clear(void)
{
	_viv_rotation_turns = 0;
	if (_viv_rotation_job && _viv_rotation_job->frames == _viv_slot_current.frames)
	{
		InterlockedExchange(&_viv_rotation_job->cancel,1);
		_viv_rotation_job->release_frames = 1;
		_viv_slot_current.frames = NULL;
		_viv_slot_current.frame_loaded_count = 0;
	}
	if (_viv_rotation_preview) CloseHandle(_viv_rotation_preview);
	if (_viv_rotation_source) CloseHandle(_viv_rotation_source);
	_viv_rotation_preview = NULL;
	_viv_rotation_source = NULL;
}

int _viv_rotation_ready(void)
{
	return *_viv_slot_current.fd.cFileName && !_viv_image_is_low_res && _viv_slot_current.frames &&
		_viv_slot_current.frame_count > 0 &&
		_viv_slot_current.frame_loaded_count == _viv_slot_current.frame_count &&
		_viv_frame_position >= 0 && _viv_frame_position < _viv_slot_current.frame_count;
}

static DWORD CALLBACK _viv_rotation_copy_progress(LARGE_INTEGER total,LARGE_INTEGER copied,
	LARGE_INTEGER stream_size,LARGE_INTEGER stream_copied,DWORD stream,DWORD reason,
	HANDLE source,HANDLE target,LPVOID context)
{
	return _viv_rotation_cancelled(context) ? PROGRESS_CANCEL : PROGRESS_CONTINUE;
}

static HANDLE _viv_rotation_copy_source(_viv_rotation_job_t *job,const wchar_t *directory)
{
	wchar_t path[MAX_PATH];
	HANDLE file = INVALID_HANDLE_VALUE;
	if (!GetTempFileNameW(directory,L"viv",0,path)) return NULL;
	if (CopyFileExW(job->original,path,_viv_rotation_copy_progress,(LPVOID)&job->cancel,NULL,0) &&
		SetFileAttributesW(path,FILE_ATTRIBUTE_TEMPORARY))
	{
		file = CreateFileW(path,DELETE,FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
			NULL,OPEN_EXISTING,FILE_ATTRIBUTE_TEMPORARY | FILE_FLAG_DELETE_ON_CLOSE,NULL);
	}
	if (file == INVALID_HANDLE_VALUE)
	{
		SetFileAttributesW(path,FILE_ATTRIBUTE_NORMAL);
		DeleteFileW(path);
		return NULL;
	}
	return file;
}

static int _viv_rotation_write(HANDLE file,const void *data,DWORD size)
{
	DWORD size_written = 0;
	return WriteFile(file,data,size,&size_written,NULL) && size_written == size;
}

static HANDLE _viv_rotation_write_preview(HBITMAP bitmap,const wchar_t *directory,volatile LONG *cancel)
{
	BITMAP dib;
	BITMAPFILEHEADER file_header;
	BITMAPINFOHEADER info;
	HANDLE file;
	SIZE_T bytes,offset;
	wchar_t path[MAX_PATH];

	if (!GetObject(bitmap,sizeof(dib),&dib) || !dib.bmBits) return NULL;
	bytes = safe_size_mul((SIZE_T)dib.bmWidthBytes,(SIZE_T)dib.bmHeight);
	if (bytes > VIV_MAX_ROTATION_BYTES || _viv_rotation_cancelled(cancel) ||
		!GetTempFileNameW(directory,L"viv",0,path)) return NULL;
	file = CreateFileW(path,GENERIC_WRITE | DELETE,FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
		NULL,TRUNCATE_EXISTING,FILE_ATTRIBUTE_TEMPORARY | FILE_FLAG_DELETE_ON_CLOSE,NULL);
	if (file == INVALID_HANDLE_VALUE)
	{
		DeleteFileW(path);
		return NULL;
	}

	// Serialize the existing DIB bytes with a BMP header. Save As still
	// uses the user's chosen encoder and the committed display frame.
	os_zero_memory(&file_header,sizeof(file_header));
	os_zero_memory(&info,sizeof(info));
	file_header.bfType = 0x4d42;
	file_header.bfOffBits = sizeof(file_header) + sizeof(info);
	file_header.bfSize = file_header.bfOffBits + (DWORD)bytes;
	info.biSize = sizeof(info);
	info.biWidth = dib.bmWidth;
	info.biHeight = -dib.bmHeight;
	info.biPlanes = 1;
	info.biBitCount = dib.bmBitsPixel;
	info.biCompression = BI_RGB;
	info.biSizeImage = (DWORD)bytes;
	if (_viv_rotation_write(file,&file_header,sizeof(file_header)) && _viv_rotation_write(file,&info,sizeof(info)))
	{
		for (offset=0; offset<bytes;)
		{
			DWORD chunk = bytes - offset > 1024*1024 ? 1024*1024 : (DWORD)(bytes - offset);
			if (_viv_rotation_cancelled(cancel) || !_viv_rotation_write(file,(const BYTE *)dib.bmBits + offset,chunk))
				break;
			offset += chunk;
		}
		if (offset == bytes) return file;
	}
	CloseHandle(file);
	return NULL;
}

static DWORD WINAPI _viv_rotation_worker(void *param)
{
	_viv_rotation_job_t *job = param;
	const int orientation[] = {1,6,3,8};
	wchar_t directory[MAX_PATH];
	DWORD length = GetTempPathW(MAX_PATH,directory);

	// GetTempFileName needs room for its generated name within MAX_PATH.
	if (!length || length >= MAX_PATH - 14 || _viv_rotation_cancelled(&job->cancel)) return 0;
	if (*job->original)
	{
		job->snapshot = _viv_rotation_copy_source(job,directory);
		if (!job->snapshot) return 0;
	}
	job->prepared = _viv_prepare_oriented_frames(job->bitmaps,job->count,orientation[job->turns],&job->cancel);
	if (job->prepared)
		job->preview = _viv_rotation_write_preview(job->prepared[job->frame_position],directory,&job->cancel);
	return 0;
}

static void _viv_rotation_dispose(_viv_rotation_job_t *job)
{
	int i;
	if (job->thread) CloseHandle(job->thread);
	if (job->snapshot) CloseHandle(job->snapshot);
	if (job->preview) CloseHandle(job->preview);
	if (job->prepared)
	{
		for (i=0; i<job->count; i++) DeleteObject(job->prepared[i]);
		mem_free(job->prepared);
	}
	if (job->release_frames) _viv_clear_frames(job->frames,job->count);
	if (job->bitmaps) mem_free(job->bitmaps);
	mem_free(job);
}

static void _viv_rotation_commit(_viv_rotation_job_t *job)
{
	int i;
	if (_viv_rotation_preview) CloseHandle(_viv_rotation_preview);
	_viv_rotation_preview = job->preview;
	job->preview = NULL;

	_viv_hw_release_image();
	_viv_hw_render_fallback = 0;
	for (i=0; i<job->count; i++)
	{
		DeleteObject(job->frames[i].hbitmap);
		job->frames[i].hbitmap = job->prepared[i];
		if (job->frames[i].mipmap) _viv_mipmap_free(job->frames[i].mipmap);
		job->frames[i].mipmap = NULL;
	}
	mem_free(job->prepared);
	job->prepared = NULL;
	if (job->turns & 1)
	{
		int old_width = _viv_slot_current.image_wide;
		_viv_slot_current.image_wide = _viv_slot_current.image_high;
		_viv_slot_current.image_high = old_width;
	}
	_viv_view_set(_viv_view_x,_viv_view_y,1);
	_viv_update_src_pixel(1,0);
	InvalidateRect(_viv_hwnd,0,FALSE);
}

static void _viv_rotation_failed(void)
{
	wchar_t caption[STRING_SIZE],message[STRING_SIZE];
	string_copy_utf8_string(caption,localization_get_string(LOCALIZATION_ID_ROTATE_IMAGE_CAPTION));
	string_copy_utf8_string(message,localization_get_string(LOCALIZATION_ID_ROTATE_IMAGE_FAILED));
	viv_msgbox(_viv_hwnd,caption,message,MB_OK | MB_ICONERROR);
}

static int _viv_rotation_start(void)
{
	_viv_rotation_job_t *job = mem_try_alloc(sizeof(*job));
	int i;

	if (!job) return 0;
	os_zero_memory(job,sizeof(*job));
	job->frames = _viv_slot_current.frames;
	job->count = _viv_slot_current.frame_count;
	job->frame_position = _viv_frame_position;
	job->turns = _viv_rotation_turns;
	job->bitmaps = mem_try_alloc(safe_size_mul((SIZE_T)job->count,sizeof(*job->bitmaps)));
	if (!job->bitmaps || !GdiFlush() ||
		(!_viv_rotation_source && !string_path_long(job->original,STRING_SIZE,_viv_slot_current.fd.cFileName)))
		goto fail;
	for (i=0; i<job->count; i++)
	{
		if (!GetObject(job->frames[i].hbitmap,sizeof(job->bitmaps[i]),&job->bitmaps[i])) goto fail;
	}

	if (!SetTimer(_viv_hwnd,VIV_ID_ROTATION_TIMER,25,NULL)) goto fail;
	job->thread = CreateThread(NULL,0,_viv_rotation_worker,job,0,NULL);
	if (!job->thread)
	{
		KillTimer(_viv_hwnd,VIV_ID_ROTATION_TIMER);
		goto fail;
	}
	_viv_rotation_job = job;
	return 1;

fail:
	_viv_rotation_dispose(job);
	return 0;
}

void _viv_rotation_poll(void)
{
	_viv_rotation_job_t *job = _viv_rotation_job;
	int failed = 0;

	if (!job || WaitForSingleObject(job->thread,0) != WAIT_OBJECT_0) return;
	_viv_rotation_job = NULL;
	KillTimer(_viv_hwnd,VIV_ID_ROTATION_TIMER);

	if (job->frames == _viv_slot_current.frames)
	{
		int requested = job->turns == _viv_rotation_turns && !_viv_rotation_cancelled(&job->cancel);
		// Keep a completed copy for the committed angle or the next request.
		if (job->snapshot && (requested ? job->preview != NULL : _viv_rotation_turns != 0))
		{
			_viv_rotation_source = job->snapshot;
			job->snapshot = NULL;
		}
		if (requested)
		{
			_viv_rotation_turns = 0;
			if (job->preview) _viv_rotation_commit(job);
			else failed = 1;
		}
	}
	_viv_rotation_dispose(job);
	if (_viv_rotation_turns && (!_viv_rotation_ready() || !_viv_rotation_start()))
	{
		_viv_rotation_turns = 0;
		failed = 1;
	}
	_viv_status_update();
	if (failed) _viv_rotation_failed();
}

void _viv_rotation_shutdown(void)
{
	if (_viv_rotation_job)
	{
		// _viv_clear has cancelled the job and transferred its source.
		if (WaitForSingleObject(_viv_rotation_job->thread,10000) != WAIT_OBJECT_0)
		{
			debug_printf("rotation worker timeout\n");
			ExitProcess(1);
		}
		KillTimer(_viv_hwnd,VIV_ID_ROTATION_TIMER);
		_viv_rotation_dispose(_viv_rotation_job);
		_viv_rotation_job = NULL;
	}
}

void _viv_edit_rotate(int counterclockwise)
{
	int was_busy = _viv_rotation_busy();
	if (!_viv_rotation_ready()) return;
	_viv_rotation_turns = (_viv_rotation_turns + (counterclockwise ? 3 : 1)) & 3;
	if (_viv_rotation_job)
	{
		if (_viv_rotation_job->turns != _viv_rotation_turns)
			InterlockedExchange(&_viv_rotation_job->cancel,1);
	}
	else if (!_viv_rotation_start())
	{
		_viv_rotation_turns = 0;
		_viv_rotation_failed();
		return;
	}
	if (!was_busy) _viv_status_update();
}
