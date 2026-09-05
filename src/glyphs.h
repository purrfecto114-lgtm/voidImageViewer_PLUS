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
// vector glyph icons (toolbar + zoom controls).
// drawn from a 48x48 design grid (2.5 unit round capped strokes) through
// the gdi+ flat api at any requested size and in both theme colors,
// converted to HICONs and cached per (glyph, theme, size).

#ifndef _GLYPHS_H_INCLUDED_
#define _GLYPHS_H_INCLUDED_

#ifdef __cplusplus
extern "C" {
#endif

// glyph ids. the order matches the toolbar image list order:
// prev, play, pause, next, bestfit, 1to1, zoom out, zoom in.
#define GLYPH_PREV     0
#define GLYPH_PLAY     1
#define GLYPH_PAUSE    2
#define GLYPH_NEXT     3
#define GLYPH_BESTFIT  4
#define GLYPH_1TO1     5
#define GLYPH_ZOOMOUT  6
#define GLYPH_ZOOMIN   7
#define GLYPH_COUNT    8

// return the glyph icon at the requested size and theme. the icon is
// owned by the glyphs cache: do not destroy it. building needs gdi+;
// on failure 0 is returned and callers should skip drawing.
HICON glyphs_icon(int glyph_id,int dark,int size);

// drop every cached icon. call when the palette or the window dpi
// changes (both are baked into the cached bitmaps).
void glyphs_flush_cache(void);

#ifdef __cplusplus
}
#endif

#endif

