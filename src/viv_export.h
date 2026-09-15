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

// the hidden render export: a command line mode for the pixel
// regression harness. -render-export <file> walks the real pipeline -
// the threaded loader, the fit math, the canvas paint, the selected
// renderer - then reads the exact pixels back and writes a 32bpp
// bitmap the ci hashes against the committed goldens. the switches
// stay out of the help text: they are a test surface, not a user
// face.

#ifndef VIV_VIV_EXPORT_H
#define VIV_VIV_EXPORT_H

// the export canvas the harness pins (wide x high client pixels). the
// width clears the minimum window width the wm_getminmaxinfo math
// asks for at any dpi up to 133 percent.
#define _VIV_EXPORT_DEFAULT_WIDE 640
#define _VIV_EXPORT_DEFAULT_HIGH 480

// the load must answer within a minute or the harness reports a timeout.
#define _VIV_EXPORT_TIMEOUT_MS 60000

// the export state. _viv_export_mode answers before the install
// options and the single instance mutex (a render export never
// redirects to a live viewer); the renderer, the canvas size and the
// output path ride the same early parse.
extern int _viv_export_mode;
extern int _viv_export_renderer;
extern int _viv_export_wide;
extern int _viv_export_high;
extern wchar_t _viv_export_path[STRING_SIZE];

// the early parse: recognizes the render-* switches and their values
// before anything else can claim the process.
int _viv_export_probe_command_line(void);

// the deterministic pins: the ini, the system theme, icm profiles and
// the exif orientation all vary by host - the defaults they would
// override are pinned instead.
void _viv_export_apply_config_pins(void);

// force the client area to the exact export canvas before the first
// frame arrives. returns 0 when the window refuses the size.
int _viv_export_resize_window(void);

// the run: pumps until the first frame answers (or the load refuses
// or the timeout expires), paints once through the real pipeline,
// reads the pixels back and writes the bitmap. the exit codes are the
// harness contract: 0 rendered, 1 generic failure, 2 the load was
// refused, 3 the renderer was unavailable, 4 the load timed out,
// 5 the window refused the canvas size.
int _viv_export_run(void);

#endif
