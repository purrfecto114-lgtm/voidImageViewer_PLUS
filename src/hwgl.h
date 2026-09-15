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
// the opengl renderer: view -> renderer -> opengl. an opt-in back end -
// the default gdi path never changes, any failure falls back to it for
// the rest of the session, and the module never touches the exe's import
// table (the os.c gdi+ table precedent).

#ifndef VIV_HWGL_H
#define VIV_HWGL_H

// renders the frame as one textured quad at the view rectangle. returns 1
// when the hardware presented the frame; 0 means the caller paints the
// gdi path instead (the sticky failure keeps the answer stable).
int _viv_hwgl_render(HWND hwnd,HDC hdc,HBITMAP hbitmap,int dst_x,int dst_y,int dst_wide,int dst_high,COLORREF clear_color);

// releases the context and the texture (the window keeps its pixel
// format - setPixelFormat answers once per window by contract).
void _viv_hwgl_shutdown(void);

// the export harness hooks (viv_export.c): arm a top-down bgra buffer
// before the paint, the next render fills it instead of presenting. the
// query tells a refused renderer (the gdi fallback painted instead)
// from a successful readback.
int _viv_hwgl_export_begin(BYTE *bits,int wide,int high);
int _viv_hwgl_export_filled(void);

#endif
