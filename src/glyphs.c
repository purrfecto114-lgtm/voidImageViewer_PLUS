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
// vector glyph icon rendering. the eight toolbar / zoom control icons are
// drawn as 48x48 grid line art (round capped strokes, the media transport
// and magnifier metaphors of the previous .ico frames) into a 32bpp argb
// bitmap through the gdi+ flat api, then converted to an HICON. icons are
// cached per (glyph, theme, size) so the hot draw paths never rebuild.

#include "viv.h"
#include "glyphs.h"

// point in the 48x48 design grid.
typedef struct _glyphs_point_s
{
	int x;
	int y;
}_glyphs_point_t;

// one stroke: a polyline with a round capped pen of the given grid width.
typedef struct _glyphs_stroke_s
{
	int point_count;
	int width; // 48 grid units.
	const _glyphs_point_t *points;
}_glyphs_stroke_t;

typedef struct _glyphs_glyph_s
{
	int stroke_count;
	const _glyphs_stroke_t *strokes;
}_glyphs_glyph_t;

// gdi+ flat api table, resolved from gdiplus.dll on first use.
static int (__stdcall *_glyphs_gdipCreatePen1)(unsigned int argb,float width,int unit,void **pen) = 0;
static int (__stdcall *_glyphs_gdipSetPenStartCap)(void *pen,int cap) = 0;
static int (__stdcall *_glyphs_gdipSetPenEndCap)(void *pen,int cap) = 0;
static int (__stdcall *_glyphs_gdipDeletePen)(void *pen) = 0;
static int (__stdcall *_glyphs_gdipDrawLinesI)(void *graphics,void *pen,const _glyphs_point_t *points,int count) = 0;
static int (__stdcall *_glyphs_gdipCreateBitmapFromScan0)(int wide,int high,int stride,int format,unsigned char *scan0,void **bitmap) = 0;
static int (__stdcall *_glyphs_gdipGetImageGraphicsContext)(void *image,void **graphics) = 0;
static int (__stdcall *_glyphs_gdipSetSmoothingMode)(void *graphics,int mode) = 0;
static int (__stdcall *_glyphs_gdipDeleteGraphics)(void *graphics) = 0;
static int (__stdcall *_glyphs_gdipDisposeImage)(void *image) = 0;
static int (__stdcall *_glyphs_gdipCreateHICONFromBitmap)(void *bitmap,HICON *icon) = 0;

// 0 = not loaded, 1 = ready, 2 = permanently unavailable.
static int _glyphs_state = 0;
static ULONG_PTR _glyphs_gdiplus_token = 0;

// cache: linear table, the oldest entry is evicted when full.
#define _GLYPHS_CACHE_MAX 36

typedef struct _glyphs_cache_entry_s
{
	int glyph;
	int dark;
	int size;
	HICON icon;
}_glyphs_cache_entry_t;

static _glyphs_cache_entry_t _glyphs_cache[_GLYPHS_CACHE_MAX];
static int _glyphs_cache_count = 0;

// theme stroke colors (argb).
#define _GLYPHS_COLOR_DARK  0xFFE8E8E8
#define _GLYPHS_COLOR_LIGHT 0xFF3C4043

// gdi+ constants used below.
// PixelFormat32bppARGB, UnitPixel, LineCapRound, SmoothingModeAntiAlias.
#define _GLYPHS_PIXEL_FORMAT_32ARGB 0x26200A
#define _GLYPHS_UNIT_PIXEL 2
#define _GLYPHS_LINE_CAP_ROUND 2
#define _GLYPHS_SMOOTHING_ANTIALIAS 4

// magnifier circle: 20 segments around center (21,21) with radius 13.
static const _glyphs_point_t _glyphs_circle_points[] =
{
	{34,21},{33,25},{32,29},{29,32},{25,33},{21,34},{17,33},{13,32},
	{10,29},{9,25},{8,21},{9,17},{10,13},{13,10},{17,9},{21,8},
	{25,9},{29,10},{32,13},{33,17},{34,21}
};

// prev: a left pointing triangle with a bar on its left.
static const _glyphs_point_t _glyphs_prev_tri[] = { {32,12},{16,24},{32,36},{32,12} };
static const _glyphs_point_t _glyphs_prev_bar[] = { {8,14},{8,34} };
static const _glyphs_stroke_t _glyphs_prev_strokes[] =
{
	{4,4,_glyphs_prev_tri},
	{2,6,_glyphs_prev_bar}
};

// next: a right pointing triangle with a bar on its right.
static const _glyphs_point_t _glyphs_next_tri[] = { {16,12},{32,24},{16,36},{16,12} };
static const _glyphs_point_t _glyphs_next_bar[] = { {40,14},{40,34} };
static const _glyphs_stroke_t _glyphs_next_strokes[] =
{
	{4,4,_glyphs_next_tri},
	{2,6,_glyphs_next_bar}
};

// play: a right pointing triangle.
static const _glyphs_point_t _glyphs_play_tri[] = { {18,12},{36,24},{18,36},{18,12} };
static const _glyphs_stroke_t _glyphs_play_strokes[] =
{
	{4,4,_glyphs_play_tri}
};

// pause: two vertical bars.
static const _glyphs_point_t _glyphs_pause_bar1[] = { {18,14},{18,34} };
static const _glyphs_point_t _glyphs_pause_bar2[] = { {30,14},{30,34} };
static const _glyphs_stroke_t _glyphs_pause_strokes[] =
{
	{2,8,_glyphs_pause_bar1},
	{2,8,_glyphs_pause_bar2}
};

// bestfit: corner brackets with a single headed diagonal arrow (shrink
// the image into the frame).
static const _glyphs_point_t _glyphs_fit_tl[] = { {12,20},{12,12},{20,12} };
static const _glyphs_point_t _glyphs_fit_tr[] = { {28,12},{36,12},{36,20} };
static const _glyphs_point_t _glyphs_fit_br[] = { {36,28},{36,36},{28,36} };
static const _glyphs_point_t _glyphs_fit_bl[] = { {20,36},{12,36},{12,28} };
static const _glyphs_point_t _glyphs_fit_shaft[] = { {30,18},{18,30} };
static const _glyphs_point_t _glyphs_fit_head[] = { {18,22},{18,30},{26,30} };
static const _glyphs_stroke_t _glyphs_bestfit_strokes[] =
{
	{3,4,_glyphs_fit_tl},
	{3,4,_glyphs_fit_tr},
	{3,4,_glyphs_fit_br},
	{3,4,_glyphs_fit_bl},
	{2,4,_glyphs_fit_shaft},
	{3,4,_glyphs_fit_head}
};

// 1:1: corner brackets with a double headed diagonal arrow (the actual
// size, both directions).
static const _glyphs_point_t _glyphs_1to1_shaft[] = { {16,32},{32,16} };
static const _glyphs_point_t _glyphs_1to1_head1[] = { {16,24},{16,32},{24,32} };
static const _glyphs_point_t _glyphs_1to1_head2[] = { {24,16},{32,16},{32,24} };
static const _glyphs_stroke_t _glyphs_1to1_strokes[] =
{
	{3,4,_glyphs_fit_tl},
	{3,4,_glyphs_fit_tr},
	{3,4,_glyphs_fit_br},
	{3,4,_glyphs_fit_bl},
	{2,4,_glyphs_1to1_shaft},
	{3,4,_glyphs_1to1_head1},
	{3,4,_glyphs_1to1_head2}
};

// zoom out: magnifier with a minus, handle to the bottom right.
static const _glyphs_point_t _glyphs_zoomout_handle[] = { {29,29},{38,38} };
static const _glyphs_point_t _glyphs_zoomout_minus[] = { {16,21},{26,21} };
static const _glyphs_stroke_t _glyphs_zoomout_strokes[] =
{
	{21,4,_glyphs_circle_points},
	{2,5,_glyphs_zoomout_handle},
	{2,4,_glyphs_zoomout_minus}
};

// zoom in: magnifier with a plus.
static const _glyphs_point_t _glyphs_zoomin_handle[] = { {29,29},{38,38} };
static const _glyphs_point_t _glyphs_zoomin_minus[] = { {16,21},{26,21} };
static const _glyphs_point_t _glyphs_zoomin_plus[] = { {21,16},{21,26} };
static const _glyphs_stroke_t _glyphs_zoomin_strokes[] =
{
	{21,4,_glyphs_circle_points},
	{2,5,_glyphs_zoomin_handle},
	{2,4,_glyphs_zoomin_minus},
	{2,4,_glyphs_zoomin_plus}
};

static const _glyphs_glyph_t _glyphs_table[GLYPH_COUNT] =
{
	{2,_glyphs_prev_strokes},
	{1,_glyphs_play_strokes},
	{2,_glyphs_pause_strokes},
	{2,_glyphs_next_strokes},
	{6,_glyphs_bestfit_strokes},
	{7,_glyphs_1to1_strokes},
	{3,_glyphs_zoomout_strokes},
	{4,_glyphs_zoomin_strokes}
};

// resolve the gdi+ flat api table. gdi+ must have been started before
// the flat api is used: viv.c does it during init, and a second startup
// with our own token is explicitly allowed, so do it here as well.
static int _glyphs_load(void)
{
	HMODULE module;

	if (_glyphs_state)
	{
		return (_glyphs_state == 1) ? 1 : 0;
	}

	module = LoadLibraryA("gdiplus.dll");

	if (!module)
	{
		_glyphs_state = 2;

		return 0;
	}

	_glyphs_gdipCreatePen1 = (void *)GetProcAddress(module,"GdipCreatePen1");
	_glyphs_gdipSetPenStartCap = (void *)GetProcAddress(module,"GdipSetPenStartCap");
	_glyphs_gdipSetPenEndCap = (void *)GetProcAddress(module,"GdipSetPenEndCap");
	_glyphs_gdipDeletePen = (void *)GetProcAddress(module,"GdipDeletePen");
	_glyphs_gdipDrawLinesI = (void *)GetProcAddress(module,"GdipDrawLinesI");
	_glyphs_gdipCreateBitmapFromScan0 = (void *)GetProcAddress(module,"GdipCreateBitmapFromScan0");
	_glyphs_gdipGetImageGraphicsContext = (void *)GetProcAddress(module,"GdipGetImageGraphicsContext");
	_glyphs_gdipSetSmoothingMode = (void *)GetProcAddress(module,"GdipSetSmoothingMode");
	_glyphs_gdipDeleteGraphics = (void *)GetProcAddress(module,"GdipDeleteGraphics");
	_glyphs_gdipDisposeImage = (void *)GetProcAddress(module,"GdipDisposeImage");
	_glyphs_gdipCreateHICONFromBitmap = (void *)GetProcAddress(module,"GdipCreateHICONFromBitmap");

	if ((!_glyphs_gdipCreatePen1) || (!_glyphs_gdipSetPenStartCap) || (!_glyphs_gdipSetPenEndCap) ||
		(!_glyphs_gdipDeletePen) || (!_glyphs_gdipDrawLinesI) || (!_glyphs_gdipCreateBitmapFromScan0) ||
		(!_glyphs_gdipGetImageGraphicsContext) || (!_glyphs_gdipSetSmoothingMode) ||
		(!_glyphs_gdipDeleteGraphics) || (!_glyphs_gdipDisposeImage) || (!_glyphs_gdipCreateHICONFromBitmap))
	{
		_glyphs_state = 2;

		return 0;
	}

	if (os_GdiplusStartup)
	{
		os_GdiplusStartupInput_t input;

		input.GdiplusVersion = 1;
		input.DebugEventCallback = 0;
		input.SuppressBackgroundThread = 0;
		input.SuppressExternalCodecs = 0;

		os_GdiplusStartup(&_glyphs_gdiplus_token,&input,0);
	}

	_glyphs_state = 1;

	return 1;
}

// render one glyph into a new HICON.
static HICON _glyphs_build(int glyph_id,int dark,int size)
{
	unsigned char *bits;
	void *bitmap;
	void *graphics;
	HICON icon;
	int stride;

	bitmap = 0;
	graphics = 0;
	icon = 0;

	stride = size * 4;

	bits = (unsigned char *)mem_alloc((size_t)stride * (size_t)size);

	if (!bits)
	{
		return 0;
	}

	os_zero_memory(bits,(size_t)stride * (size_t)size);

	// the bitmap wraps our buffer without copying it: disposeImage does
	// not free it, we do after the icon was created.
	if (_glyphs_gdipCreateBitmapFromScan0(size,size,stride,_GLYPHS_PIXEL_FORMAT_32ARGB,bits,&bitmap) == 0)
	{
		if (_glyphs_gdipGetImageGraphicsContext(bitmap,&graphics) == 0)
		{
			float scale;
			unsigned int argb;
			int strokei;

			_glyphs_gdipSetSmoothingMode(graphics,_GLYPHS_SMOOTHING_ANTIALIAS);

			scale = (float)size / 48.0f;

			argb = dark ? _GLYPHS_COLOR_DARK : _GLYPHS_COLOR_LIGHT;

			for(strokei=0;strokei<_glyphs_table[glyph_id].stroke_count;strokei++)
			{
				const _glyphs_stroke_t *stroke;
				void *pen;

				stroke = &_glyphs_table[glyph_id].strokes[strokei];

				pen = 0;

				if (_glyphs_gdipCreatePen1(argb,(float)stroke->width * scale,_GLYPHS_UNIT_PIXEL,&pen) == 0)
				{
					_glyphs_point_t *pts;

					// round caps: the strokes end in soft dots instead of
					// square cuts, the signature of the glyph family.
					_glyphs_gdipSetPenStartCap(pen,_GLYPHS_LINE_CAP_ROUND);
					_glyphs_gdipSetPenEndCap(pen,_GLYPHS_LINE_CAP_ROUND);

					pts = (_glyphs_point_t *)mem_alloc(sizeof(_glyphs_point_t) * (size_t)stroke->point_count);

					if (pts)
					{
						int pi;

						for(pi=0;pi<stroke->point_count;pi++)
						{
							pts[pi].x = (int)((((float)stroke->points[pi].x) * scale) + 0.5f);
							pts[pi].y = (int)((((float)stroke->points[pi].y) * scale) + 0.5f);
						}

						_glyphs_gdipDrawLinesI(graphics,pen,pts,stroke->point_count);

						mem_free(pts);
					}

					_glyphs_gdipDeletePen(pen);
				}
			}

			_glyphs_gdipDeleteGraphics(graphics);
		}

		if (_glyphs_gdipCreateHICONFromBitmap(bitmap,&icon) != 0)
		{
			icon = 0;
		}

		_glyphs_gdipDisposeImage(bitmap);
	}

	mem_free(bits);

	return icon;
}

HICON glyphs_icon(int glyph_id,int dark,int size)
{
	int i;

	if (!_glyphs_load())
	{
		return 0;
	}

	if ((glyph_id < 0) || (glyph_id >= GLYPH_COUNT) || (size <= 0))
	{
		return 0;
	}

	for(i=0;i<_glyphs_cache_count;i++)
	{
		if ((_glyphs_cache[i].glyph == glyph_id) && (_glyphs_cache[i].dark == (dark ? 1 : 0)) && (_glyphs_cache[i].size == size))
		{
			return _glyphs_cache[i].icon;
		}
	}

	// evict the oldest entry when the table is full.
	if (_glyphs_cache_count == _GLYPHS_CACHE_MAX)
	{
		if (_glyphs_cache[0].icon)
		{
			DestroyIcon(_glyphs_cache[0].icon);
		}

		os_copy_memory(&_glyphs_cache[0],&_glyphs_cache[1],sizeof(_glyphs_cache[0]) * (_GLYPHS_CACHE_MAX - 1));

		_glyphs_cache_count--;
	}

	_glyphs_cache[_glyphs_cache_count].glyph = glyph_id;
	_glyphs_cache[_glyphs_cache_count].dark = dark ? 1 : 0;
	_glyphs_cache[_glyphs_cache_count].size = size;
	_glyphs_cache[_glyphs_cache_count].icon = _glyphs_build(glyph_id,dark ? 1 : 0,size);

	return _glyphs_cache[_glyphs_cache_count++].icon;
}

void glyphs_flush_cache(void)
{
	int i;

	for(i=0;i<_glyphs_cache_count;i++)
	{
		if (_glyphs_cache[i].icon)
		{
			DestroyIcon(_glyphs_cache[i].icon);
		}
	}

	_glyphs_cache_count = 0;
}

