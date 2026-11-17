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
// the direct3d 12 renderer (rc.3): view -> renderer -> direct3d 12. an
// opt-in back end like the d3d9 one - the default gdi path never changes,
// any failure falls back to it, and d3d12.dll / dxgi.dll resolve
// dynamically (the pair rides windows 10 and every later windows; the
// module's absence is the probe's own refusal).

#ifndef VIV_HWD3D12_H
#define VIV_HWD3D12_H

// the same contract hwd3d.h carries: one pretransformed textured quad at
// the view rectangle. returns 1 when the device presented the frame; 0
// means the caller paints the gdi path instead.
int _viv_hwd3d12_render(HWND hwnd,HDC hdc,HBITMAP hbitmap,int dst_x,int dst_y,int dst_wide,int dst_high,COLORREF clear_color);

// releases the swapchain, the device and the texture.
void _viv_hwd3d12_shutdown(void);
void _viv_hwd3d12_release_image(void);

// the capability probe: can this machine bring direct3d 12 up at all?
// walks the init's first gates (the module, the export) without the
// device and without the sticky failure; the menu radio and the settings
// ladder gate on it.
int _viv_hwd3d12_available(void);

// the export harness hooks (viv_export.c): arm a top-down bgra buffer
// before the paint, the next render fills it instead of presenting. the
// query tells a refused renderer (the gdi fallback painted instead)
// from a successful readback.
int _viv_hwd3d12_export_begin(BYTE *bits,int wide,int high);
int _viv_hwd3d12_export_filled(void);
void _viv_hwd3d12_export_end(void);

#endif
