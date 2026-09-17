//
// Copyright 2026 voidtools / David Carpenter
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

// the hidden render export (see viv_export.h). the mode is the pixel
// regression harness's only window into the real pipeline: everything
// the harness checks - decode, scaling, backdrop, alpha, the renderer
// samplers - answers through the same code the shipped binary paints
// with, and the bitmap bytes are the oracle.

#include "viv.h"
#include "viv_state.h"
#include "viv_export.h"
#include "hwgl.h"
#include "hwd3d.h"
#include "viv_chrome.h"

int _viv_export_mode = 0;
int _viv_export_renderer = CONFIG_RENDERER_GDI;
int _viv_export_wide = _VIV_EXPORT_DEFAULT_WIDE;
int _viv_export_high = _VIV_EXPORT_DEFAULT_HIGH;
wchar_t _viv_export_path[STRING_SIZE];

// "wide x high" with any non digit separator. the bounds keep a
// hostile or fat fingered value from asking for a canvas the window
// manager would refuse anyway.
static void _viv_export_parse_size(const wchar_t *text)
{
	int wide;
	int high;
	int digits;
	
	wide = 0;
	high = 0;
	
	digits = 0;
	
	while((digits < 6) && (*text >= L'0') && (*text <= L'9'))
	{
		wide = (wide * 10) + (*text - L'0');
		text++;
		digits++;
	}
	
	while((*text) && ((*text < L'0') || (*text > L'9')))
	{
		text++;
	}
	
	digits = 0;
	
	while((digits < 6) && (*text >= L'0') && (*text <= L'9'))
	{
		high = (high * 10) + (*text - L'0');
		text++;
		digits++;
	}
	
	if ((wide >= 16) && (wide <= 8192) && (high >= 16) && (high <= 8192))
	{
		_viv_export_wide = wide;
		_viv_export_high = high;
	}
}

// the early parse. the word walk mirrors _viv_process_command_line's:
// quoted or not, a leading / or - starts a switch (the real parse
// later only has to swallow the two value switches - its consumption
// cases in viv.c).
int _viv_export_probe_command_line(void)
{
	wchar_t buf[STRING_SIZE];
	wchar_t *p;
	
	p = GetCommandLineW();
	p = string_skip_ws(p);
	
	// skip exe filename
	p = string_get_word(p,buf,STRING_SIZE);
	p = string_skip_ws(p);
	
	while(*p)
	{
		wchar_t *bufstart;
		int was_quote;
		
		was_quote = (*p == L'"');
		
		p = string_get_word(p,buf,STRING_SIZE);
		p = string_skip_ws(p);
		
		bufstart = buf;
		
		if ((!was_quote) && ((*bufstart == L'/') || (*bufstart == L'-')))
		{
			bufstart++;
			
			if (string_icompare_lowercase_ascii(bufstart,"render-export") == 0)
			{
				_viv_export_mode = 1;
				
				// the output path rides the next word.
				p = string_get_word(p,buf,STRING_SIZE);
				p = string_skip_ws(p);
				
				string_copy(_viv_export_path,buf);
			}
			else
			if (string_icompare_lowercase_ascii(bufstart,"render-gdi") == 0)
			{
				_viv_export_renderer = CONFIG_RENDERER_GDI;
			}
			else
			if (string_icompare_lowercase_ascii(bufstart,"render-gl") == 0)
			{
				_viv_export_renderer = CONFIG_RENDERER_OPENGL;
			}
			else
			if (string_icompare_lowercase_ascii(bufstart,"render-d3d") == 0)
			{
				_viv_export_renderer = CONFIG_RENDERER_DIRECT3D;
			}
			else
			if (string_icompare_lowercase_ascii(bufstart,"render-size") == 0)
			{
				p = string_get_word(p,buf,STRING_SIZE);
				p = string_skip_ws(p);
				
				_viv_export_parse_size(buf);
			}
		}
	}
	
	return _viv_export_mode;
}

// the deterministic pins. the ini answered whatever the host had - the
// harness bytes must not vary by host, so every setting the canvas
// pixels read is pinned to the compile time defaults here.
void _viv_export_apply_config_pins(void)
{
	// light ui: the auto mode reads the system theme and a runner's
	// theme is not a contract. light also keeps the windowed background
	// at the configured color instead of the dark mat mapping.
	config_dark_mode = 0;
	os_dark_set_app_mode(0);
	
	// the strips stay hidden: the whole client is the canvas and the
	// fit math answers to the export size alone.
	config_show_menu = 0;
	config_show_status = 0;
	config_show_controls = 0;
	
	// the host variable decoders: icm reads the machine's color
	// profiles and the exif orientation is its own coverage round -
	// both off.
	config_icm = 0;
	config_orientation = 0;
	
	// the fit contract: keep the aspect, never fill.
	config_keep_aspect_ratio = 1;
	config_fill_window = 0;
	
	// the fit gates: a machine with shrinking off renders the large
	// samples at 100 percent instead of the fit, and a machine with the
	// auto zoom on re-zooms every sample between the load and the paint
	// - both are ini keys the goldens must never read.
	config_allow_shrinking = 1;
	config_auto_zoom = 0;
	
	// the default filters and the windowed background, pinned explicitly
	// so a developer's ini cannot drift the export - the background is
	// the one setting whose pixels surround the image itself (a custom
	// mat color on the machine that bootstraps the goldens would pin
	// pixels no other machine can reproduce).
	config_backdrop_mode = CONFIG_BACKDROP_MODE_FOLLOW;
	config_windowed_background_color_r = 255;
	config_windowed_background_color_g = 255;
	config_windowed_background_color_b = 255;
	config_shrink_blit_mode = CONFIG_SHRINK_BLIT_MODE_HALFTONE;
	config_mag_filter = CONFIG_MAG_FILTER_COLORONCOLOR;
	
	// frame 0 stays frame 0: an animation timer must not advance the
	// position between the load and the paint.
	_viv_animation_play = 0;
}

// force the client area to the exact export canvas. the delta method
// answers any frame metrics (caption, dpi scaling) without guessing
// at styles; two passes catch a layout the first resize woke up.
int _viv_export_resize_window(void)
{
	RECT rect;
	int i;
	
	for(i=0;i<2;i++)
	{
		int wide;
		int high;
		
		GetClientRect(_viv_hwnd,&rect);
		wide = rect.right - rect.left;
		high = rect.bottom - rect.top;
		
		if ((wide == _viv_export_wide) && (high == _viv_export_high))
		{
			return 1;
		}
		
		GetWindowRect(_viv_hwnd,&rect);
		
		SetWindowPos(_viv_hwnd,0,rect.left,rect.top,
			(rect.right - rect.left) + (_viv_export_wide - wide),
			(rect.bottom - rect.top) + (_viv_export_high - high),
			SWP_NOZORDER|SWP_NOACTIVATE|SWP_NOMOVE);
	}
	
	GetClientRect(_viv_hwnd,&rect);
	
	return ((rect.right - rect.left == _viv_export_wide) && (rect.bottom - rect.top == _viv_export_high));
}

static int _viv_export_write_all(HANDLE h,const void *buf,DWORD size)
{
	DWORD written;
	
	if ((!WriteFile(h,buf,size,&written,0)) || (written != size))
	{
		debug_printf("render-export: WriteFile failed %d\r\n",GetLastError());
		
		return 0;
	}
	
	return 1;
}

// the classic bottom-up 32bpp bitmap: file header, info header, then
// the rows from the bottom up. the readback buffers are top-down, so
// the write walks them in reverse. every readback path pins the
// reserved byte to zero (the x byte of a d3d back buffer and the alpha
// byte of a gdi blit answer undefined values; the hash must never see
// them).
static int _viv_export_write_bmp(const wchar_t *path,const BYTE *bits,int wide,int high)
{
	BITMAPFILEHEADER file_header;
	BITMAPINFOHEADER info_header;
	DWORD row_bytes;
	HANDLE h;
	int y;
	int ok;
	
	row_bytes = (DWORD)wide * 4;
	
	ZeroMemory(&file_header,sizeof(file_header));
	file_header.bfType = 0x4d42;
	file_header.bfSize = sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER) + (row_bytes * (DWORD)high);
	file_header.bfOffBits = sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER);
	
	ZeroMemory(&info_header,sizeof(info_header));
	info_header.biSize = sizeof(BITMAPINFOHEADER);
	info_header.biWidth = wide;
	info_header.biHeight = high;
	info_header.biPlanes = 1;
	info_header.biBitCount = 32;
	info_header.biCompression = BI_RGB;
	info_header.biSizeImage = row_bytes * (DWORD)high;
	
	h = CreateFileW(path,GENERIC_WRITE,0,0,CREATE_ALWAYS,0,0);
	
	if (h == INVALID_HANDLE_VALUE)
	{
		debug_printf("render-export: CreateFileW failed %d\r\n",GetLastError());
		
		return 0;
	}
	
	ok = 0;
	
	if (_viv_export_write_all(h,&file_header,sizeof(file_header)))
	{
		if (_viv_export_write_all(h,&info_header,sizeof(info_header)))
		{
			ok = 1;
			
			for(y=high-1;y>=0;y--)
			{
				if (!_viv_export_write_all(h,bits + (uintptr_t)y * (uintptr_t)wide * 4,row_bytes))
				{
					ok = 0;
					
					break;
				}
			}
		}
	}
	
	CloseHandle(h);
	
	return ok;
}

// the gdi readback: the paint backbuffer holds the whole client frame
// (module cached in viv_chrome.c), the readback helper lands it
// straight in the export buffer as top-down 32bpp.
static int _viv_export_readback_gdi(BYTE *bits,int wide,int high)
{
	int y;
	int x;
	
	if (!_viv_paint_readback(bits,wide,high))
	{
		return 0;
	}
	
	// pin the reserved byte (getdibits answers whatever the blit left).
	for(y=0;y<high;y++)
	{
		BYTE *row;
		
		row = bits + (uintptr_t)y * (uintptr_t)wide * 4;
		
		for(x=0;x<wide;x++)
		{
			row[x * 4 + 3] = 0;
		}
	}
	
	return 1;
}

// the run: pump, paint, read back, write, answer the harness contract.
int _viv_export_run(void)
{
	BYTE *bits;
	DWORD start;
	MSG msg;
	RECT rect;
	
	if (!_viv_export_mode)
	{
		return 0;
	}
	
	if (!_viv_export_path[0])
	{
		return 1;
	}
	
	// the canvas must be the exact export size (the fit math answers
	// to the live client rect).
	GetClientRect(_viv_hwnd,&rect);
	
	if ((rect.right - rect.left != _viv_export_wide) || (rect.bottom - rect.top != _viv_export_high))
	{
		debug_printf("render-export: the window refused the canvas size\r\n");
		
		return 5;
	}
	
	// the renderer the harness asked for.
	config_renderer = _viv_export_renderer;
	
	bits = (BYTE *)mem_alloc((uintptr_t)_viv_export_wide * (uintptr_t)_viv_export_high * 4);
	
	if (!bits)
	{
		return 1;
	}
	
	// arm the readback before the pump: the first frame reply
	// invalidates, and the wm_paint it wakes can fire inside the
	// dispatch - that paint must land in the export buffer.
	if (_viv_export_renderer == CONFIG_RENDERER_OPENGL)
	{
		_viv_hwgl_export_begin(bits,_viv_export_wide,_viv_export_high);
	}
	else
	if (_viv_export_renderer == CONFIG_RENDERER_DIRECT3D)
	{
		_viv_hwd3d_export_begin(bits,_viv_export_wide,_viv_export_high);
	}
	
	// pump until the first frame answers, the load refuses, or the
	// harness timeout expires. the load replies only arrive through
	// the dispatch.
	start = GetTickCount();
	
	for(;;)
	{
		while(PeekMessage(&msg,0,0,0,PM_REMOVE))
		{
			if (msg.message == WM_QUIT)
			{
				mem_free(bits);
				
				return 1;
			}
			
			TranslateMessage(&msg);
			DispatchMessage(&msg);
		}
		
		if (_viv_slot_current.frame_count)
		{
			break;
		}
		
		if (_viv_load_failed)
		{
			debug_printf("render-export: the load was refused\r\n");
			
			mem_free(bits);
			
			return 2;
		}
		
		if ((GetTickCount() - start) > _VIV_EXPORT_TIMEOUT_MS)
		{
			debug_printf("render-export: the load timed out\r\n");
			
			mem_free(bits);
			
			return 4;
		}
		
		// a bounded wait: a wedged decoder must not hang the harness
		// past the timeout check.
		MsgWaitForMultipleObjects(0,0,0,1000,QS_ALLINPUT);
	}
	
	// the first-frame reply resets the animation play flag through
	// _viv_clear (the load path's own default) - re-assert the pause
	// so a timer that wakes between the pump and the paint cannot
	// advance the position under the readback.
	_viv_animation_play = 0;
	
	// paint once through the real pipeline. a paint that already fired
	// inside the pump leaves the region valid and this answers no-op -
	// the readback is armed either way.
	InvalidateRect(_viv_hwnd,0,TRUE);
	UpdateWindow(_viv_hwnd);
	
	if (_viv_export_renderer == CONFIG_RENDERER_GDI)
	{
		if (!_viv_export_readback_gdi(bits,_viv_export_wide,_viv_export_high))
		{
			debug_printf("render-export: the gdi readback failed\r\n");
			
			mem_free(bits);
			
			return 1;
		}
	}
	else
	if (_viv_export_renderer == CONFIG_RENDERER_OPENGL)
	{
		if (!_viv_hwgl_export_filled())
		{
			debug_printf("render-export: the opengl renderer refused\r\n");
			
			mem_free(bits);
			
			return 3;
		}
	}
	else
	{
		if (!_viv_hwd3d_export_filled())
		{
			debug_printf("render-export: the direct3d renderer refused\r\n");
			
			mem_free(bits);
			
			return 3;
		}
	}
	
	if (!_viv_export_write_bmp(_viv_export_path,bits,_viv_export_wide,_viv_export_high))
	{
		mem_free(bits);
		
		return 1;
	}
	
	mem_free(bits);
	
	debug_printf("render-export: wrote %S\r\n",_viv_export_path);
	
	return 0;
}
