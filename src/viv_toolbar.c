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
// VoidImageViewer
// The command strip shares the top band with the menu bar. At reduced
// widths it drops labels first, then exposes the remaining actions in More.
#include "viv.h"
#include "viv_state.h"
#include "viv_chrome.h"
#include "viv_render.h"
#include "viv_menubar.h"
#include "viv_toolbar.h"
#include "viv_menu.h"
#include "viv_view.h"

// not defined in older sdks.
#ifndef BN_CLICKED
#define BN_CLICKED 0
#endif

// design metrics in 96 dpi units, scaled through the dip macros below.
#define _VIV_TOOLBAR_BAR_HIGH 48 // the strip height: the one band - menus left, commands right - against the 84 dip the menu bar plus the toolbar stacked before.
#define _VIV_TOOLBAR_ICON_SIZE 20 // the glyph box.
#define _VIV_TOOLBAR_TARGET 44 // the icon-only touch target: the 20 dip icon with 12 dip of padding each side, riding 2 dip of air top and bottom in the 48-dip band.
#define _VIV_TOOLBAR_ZOOM_CELL_WIDE 72
#define _VIV_TOOLBAR_ICON_TEXT_GAP VIV_UI_GAP // between the glyph and the label.
#define _VIV_TOOLBAR_BUTTON_PAD VIV_UI_PAD // inside the button, left and right.
#define _VIV_TOOLBAR_BUTTON_GAP VIV_UI_GAP // between the buttons.
#define _VIV_TOOLBAR_SEP_HIGH 20 // the separator line height: the icon's own height, not a stub beside it.
#define _VIV_TOOLBAR_RADIUS VIV_UI_RADIUS // the hover face corner radius.

// Keep the existing customization bits for navigation, fit, zoom and rotate.
#define _VIV_TOOLBAR_ITEM_COUNT 11
#define _VIV_TOOLBAR_GROUP_MAX 4
#define _VIV_TOOLBAR_ITEM_PLAY 2
#define _VIV_TOOLBAR_ITEM_ZOOM 7
#define _VIV_TOOLBAR_ITEM_FULLSCREEN 10

#define _VIV_TOOLBAR_TYPE_BUTTON 0
#define _VIV_TOOLBAR_TYPE_SEP 1
#define _VIV_TOOLBAR_TYPE_ZOOM 2

typedef struct _viv_toolbar_item_s
{
	int type; // _VIV_TOOLBAR_TYPE_*.
	int command_id; // the wm_command id (separators carry 0).
	int glyph_play; // the glyph of the idle face.
	int glyph_pause; // the glyph of the playing face.
	localization_id_t localization_play; // the idle label.
	localization_id_t localization_pause; // the playing label.
	int group; // the overflow group.
}_viv_toolbar_item_t;

static const _viv_toolbar_item_t _viv_toolbar_items[_VIV_TOOLBAR_ITEM_COUNT] =
{
	{_VIV_TOOLBAR_TYPE_BUTTON,VIV_ID_NAV_PREV,GLYPH_PREV,GLYPH_PREV,LOCALIZATION_ID_TOOLBAR_PREVIOUS_IMAGE_BUTTON,LOCALIZATION_ID_TOOLBAR_PREVIOUS_IMAGE_BUTTON,1},
	{_VIV_TOOLBAR_TYPE_BUTTON,VIV_ID_NAV_NEXT,GLYPH_NEXT,GLYPH_NEXT,LOCALIZATION_ID_TOOLBAR_NEXT_IMAGE_BUTTON,LOCALIZATION_ID_TOOLBAR_NEXT_IMAGE_BUTTON,1},
	{_VIV_TOOLBAR_TYPE_BUTTON,VIV_ID_VIEW_SLIDESHOW,GLYPH_PLAY,GLYPH_PAUSE,LOCALIZATION_ID_TOOLBAR_PLAY_SLIDESHOW_BUTTON,LOCALIZATION_ID_TOOLBAR_PAUSE_SLIDESHOW_BUTTON,1},
	{_VIV_TOOLBAR_TYPE_SEP,0,0,0,0,0,2},
	{_VIV_TOOLBAR_TYPE_BUTTON,VIV_ID_VIEW_1TO1,GLYPH_1TO1,GLYPH_1TO1,LOCALIZATION_ID_TOOLBAR_ACTUAL_SIZE_BUTTON,LOCALIZATION_ID_TOOLBAR_ACTUAL_SIZE_BUTTON,2},
	{_VIV_TOOLBAR_TYPE_BUTTON,VIV_ID_VIEW_BESTFIT,GLYPH_BESTFIT,GLYPH_BESTFIT,LOCALIZATION_ID_TOOLBAR_BEST_FIT_BUTTON,LOCALIZATION_ID_TOOLBAR_BEST_FIT_BUTTON,2},
	{_VIV_TOOLBAR_TYPE_SEP,0,0,0,0,0,3},
	{_VIV_TOOLBAR_TYPE_ZOOM,0,0,0,0,0,3},
	{_VIV_TOOLBAR_TYPE_SEP,0,0,0,0,0,4},
	{_VIV_TOOLBAR_TYPE_BUTTON,VIV_ID_EDIT_ROTATE_90,GLYPH_ROTATE_CW,GLYPH_ROTATE_CW,LOCALIZATION_ID_TOOLBAR_ROTATE,LOCALIZATION_ID_TOOLBAR_ROTATE,4},

	{_VIV_TOOLBAR_TYPE_BUTTON,VIV_ID_VIEW_FULLSCREEN,GLYPH_FULLSCREEN,GLYPH_RESTORE,LOCALIZATION_ID_TOOLBAR_FULLSCREEN,LOCALIZATION_ID_TOOLBAR_EXIT_FULLSCREEN,0}
};

static HWND _viv_toolbar_hwnd = 0;
static int _viv_toolbar_wide = 0; // the strip width the last layout ran with.
static int _viv_toolbar_bar_high = 0;
static int _viv_toolbar_icon_size = 0;
static int _viv_toolbar_icon_text_gap = 0;
static int _viv_toolbar_button_pad = 0;
static int _viv_toolbar_button_gap = 0;
static int _viv_toolbar_sep_wide = 0; // the whole separator slot.
static int _viv_toolbar_sep_high = 0;
static int _viv_toolbar_radius = 0;

static int _viv_toolbar_item_glyph[_VIV_TOOLBAR_ITEM_COUNT]; // the face in use.
static int _viv_toolbar_item_x[_VIV_TOOLBAR_ITEM_COUNT]; // client coords.
static int _viv_toolbar_item_wide[_VIV_TOOLBAR_ITEM_COUNT];
static int _viv_toolbar_item_label_wide[_VIV_TOOLBAR_ITEM_COUNT];
static BYTE _viv_toolbar_item_overflow[_VIV_TOOLBAR_ITEM_COUNT];
static BYTE _viv_toolbar_item_visible[_VIV_TOOLBAR_ITEM_COUNT];
static BYTE _viv_toolbar_item_enabled[_VIV_TOOLBAR_ITEM_COUNT];
static wchar_t _viv_toolbar_item_text[_VIV_TOOLBAR_ITEM_COUNT][STRING_SIZE];

static int _viv_toolbar_dark = 0; // 1 = draw with the dark palette.
static int _viv_toolbar_playing = 0; // 1 = the pause face on the play slot.
static int _viv_toolbar_hover = -1; // the item under the mouse, or -1.
static int _viv_toolbar_pressed = -1; // the item held with capture, or -1.
static BYTE _viv_toolbar_tracking = 0; // the mouse leave tracking is armed.
static HWND _viv_toolbar_tooltip_hwnd = 0; // the strip rect tooltip (the zoomui pill clone).
static BYTE _viv_toolbar_item_tool[_VIV_TOOLBAR_ITEM_COUNT + 1]; // a tooltip tool exists for the item.
static int _viv_toolbar_more_x;
static int _viv_toolbar_more_wide;
static int _viv_toolbar_compact;
static int _viv_toolbar_more_hover;
static int _viv_toolbar_more_pressed;
static int _viv_toolbar_focus = -1; // item index, or ITEM_COUNT for More.
static int _viv_toolbar_zoom_hover = -2; // the cluster zone under the mouse (-1 minus, 0 percent, 1 plus, -2 none).
static int _viv_toolbar_zoom_pressed = -2; // the cluster zone held with capture, or -2.
static int _viv_toolbar_zoom_last = -1; // the percent the strip last painted.
// the faces resolve through the theme tokens now: one palette for every
// surface (the private dark values duplicated the token table), and a
// theme or accent flip re-skins the strip through the cache flush in the
// apply path. the light hover is the quiet 3dlight face, not the loud
// selection blue the first cut painted.
static void _viv_toolbar_measure(void);
static LRESULT CALLBACK _viv_toolbar_proc(HWND hwnd,UINT msg,WPARAM wParam,LPARAM lParam);
static void _viv_toolbar_context_menu(HWND toolbar_hwnd,int screen_x,int screen_y);
static int _viv_toolbar_tooltip_id(int itemi);
static void _viv_toolbar_tooltip_sync(void);
static void _viv_toolbar_tooltip_destroy(void);
static void _viv_toolbar_tooltip_apply_colors(void);
static void _viv_toolbar_walk(void);
static void _viv_toolbar_open_overflow(void);
void _viv_start_move_window(void); // viv_view.c: the strip background drag
void _viv_zoom_edit_layout(void); // viv_dialogs.c: the in place zoom editor parks on its cell
void _viv_set_zoom_dialog(void); // viv_dialogs.c: the in place zoom editor opens on its cell

// dip macros: the 96 dpi design units at the window's current dpi.
#define _VIV_TOOLBAR_DIP_X(dip) (((dip) * os_logical_wide) / 96)
#define _VIV_TOOLBAR_DIP_Y(dip) (((dip) * os_logical_high) / 96)

static HBRUSH _viv_toolbar_face_brush(void)
{
	return viv_theme_brush(VIV_TK_CHROME);
}

static HBRUSH _viv_toolbar_hot_brush(void)
{
	return viv_theme_brush(VIV_TK_HOVER);
}

static HBRUSH _viv_toolbar_press_brush(void)
{
	return viv_theme_brush(VIV_TK_DOWN);
}

static HBRUSH _viv_toolbar_line_brush(void)
{
	return viv_theme_brush(VIV_TK_CHROME_LINE);
}

// the label and the glyph of one slot: the play slot swaps both while the
// slideshow or the animation runs.
static int _viv_toolbar_item_label_id(int itemi)
{
	if (itemi == _VIV_TOOLBAR_ITEM_FULLSCREEN)
	{
		return _viv_is_fullscreen ? _viv_toolbar_items[itemi].localization_pause : _viv_toolbar_items[itemi].localization_play;
	}

	if ((itemi == _VIV_TOOLBAR_ITEM_PLAY) && (_viv_toolbar_playing))
	{
		return _viv_toolbar_items[itemi].localization_pause;
	}
	
	return _viv_toolbar_items[itemi].localization_play;
}

static int _viv_toolbar_item_glyph_id(int itemi)
{
	if (itemi == _VIV_TOOLBAR_ITEM_FULLSCREEN)
	{
		return _viv_is_fullscreen ? _viv_toolbar_items[itemi].glyph_pause : _viv_toolbar_items[itemi].glyph_play;
	}

	if ((itemi == _VIV_TOOLBAR_ITEM_PLAY) && (_viv_toolbar_playing))
	{
		return _viv_toolbar_items[itemi].glyph_pause;
	}
	
	return _viv_toolbar_items[itemi].glyph_play;
}

static int _viv_toolbar_visible_total(const BYTE *visible)
{
	int total = 0;
	int itemi;
	int previous_button = 0;

	for (itemi = 0; itemi < _VIV_TOOLBAR_ITEM_COUNT; itemi++)
	{
		if (visible[itemi])
		{
			int button = _viv_toolbar_items[itemi].type == _VIV_TOOLBAR_TYPE_BUTTON;
			if (!_viv_toolbar_compact && button && previous_button)
			{
				total += _viv_toolbar_button_gap;
			}
			total += _viv_toolbar_item_wide[itemi];
			previous_button = button;
		}
	}
	return total;
}

static void _viv_toolbar_walk_place(int x)
{
	int itemi;
	int previous_button = 0;

	for (itemi = 0; itemi < _VIV_TOOLBAR_ITEM_COUNT; itemi++)
	{
		if (_viv_toolbar_item_visible[itemi])
		{
			int button = _viv_toolbar_items[itemi].type == _VIV_TOOLBAR_TYPE_BUTTON;
			if (!_viv_toolbar_compact && button && previous_button)
			{
				x += _viv_toolbar_button_gap;
			}
			_viv_toolbar_item_x[itemi] = x;
			x += _viv_toolbar_item_wide[itemi];
			previous_button = button;
		}
	}
}

static void _viv_toolbar_walk(void)
{
	// Preserve the on-screen order while reserving space for Fullscreen
	// and adjacent-file navigation before secondary commands.
	static const int priority[] = {_VIV_TOOLBAR_ITEM_FULLSCREEN,0,1,_VIV_TOOLBAR_ITEM_ZOOM,2,5,4,9};
	int itemi;
	int target = _VIV_TOOLBAR_DIP_X(_VIV_TOOLBAR_TARGET);
	int remaining;
	int have_command = 0;

	_viv_toolbar_compact = config_toolbar_icon_only;
	_viv_toolbar_more_wide = 0;
	_viv_toolbar_more_x = _viv_toolbar_wide;
	for (itemi = 0; itemi < _VIV_TOOLBAR_ITEM_COUNT; itemi++)
	{
		const _viv_toolbar_item_t *item = &_viv_toolbar_items[itemi];
		int enabled = !item->group || (config_toolbar_groups & (1 << item->group));
		int separator = item->type == _VIV_TOOLBAR_TYPE_SEP;
		_viv_toolbar_item_wide[itemi] = _viv_toolbar_item_label_wide[itemi];
		_viv_toolbar_item_visible[itemi] = enabled && (!separator || have_command);
		_viv_toolbar_item_overflow[itemi] = 0;
		if (enabled && !separator) have_command = 1;
	}

	if (_viv_toolbar_compact || _viv_toolbar_visible_total(_viv_toolbar_item_visible) > _viv_toolbar_wide)
	{
		_viv_toolbar_compact = 1;
		for (itemi = 0; itemi < _VIV_TOOLBAR_ITEM_COUNT; itemi++)
		{
			if (_viv_toolbar_items[itemi].type == _VIV_TOOLBAR_TYPE_SEP)
				_viv_toolbar_item_visible[itemi] = 0;
			else
				_viv_toolbar_item_wide[itemi] = itemi == _VIV_TOOLBAR_ITEM_ZOOM ? target * 3 : target;
		}
	}

	if (_viv_toolbar_visible_total(_viv_toolbar_item_visible) > _viv_toolbar_wide)
	{
		_viv_toolbar_more_wide = target < _viv_toolbar_wide ? target : _viv_toolbar_wide;
		_viv_toolbar_more_x = _viv_toolbar_wide - _viv_toolbar_more_wide;
		remaining = _viv_toolbar_more_x;

		for (itemi = 0; itemi < (int)(sizeof(priority) / sizeof(priority[0])); itemi++)
		{
			int index = priority[itemi];
			if (!_viv_toolbar_item_visible[index]) continue;
			if (_viv_toolbar_item_wide[index] <= remaining)
			{
				remaining -= _viv_toolbar_item_wide[index];
			}
			else
			{
				_viv_toolbar_item_visible[index] = 0;
				_viv_toolbar_item_overflow[index] = 1;
			}
		}
	}
	_viv_toolbar_walk_place(0);
}

static void _viv_toolbar_more_rect(RECT *rect)
{
	SetRect(rect,_viv_toolbar_more_x,0,_viv_toolbar_wide,_viv_toolbar_bar_high);
}

static int _viv_toolbar_more_hit_test(int x,int y)
{
	return _viv_toolbar_more_wide > 0 && x >= _viv_toolbar_more_x &&
		x < _viv_toolbar_wide && y >= 0 && y < _viv_toolbar_bar_high;
}

static void _viv_toolbar_invalidate_more(void)
{
	RECT rect;
	_viv_toolbar_more_rect(&rect);
	InvalidateRect(_viv_toolbar_hwnd,&rect,FALSE);
}

static void _viv_toolbar_measure(void)
{
	HDC hdc;
	HFONT font;
	HFONT old_font;
	SIZE size;
	int itemi;
	
	// the strip metrics at the current dpi. the floors keep a failed metric
	// query from collapsing the strip or the glyphs.
	_viv_toolbar_bar_high = _VIV_TOOLBAR_DIP_Y(_VIV_TOOLBAR_BAR_HIGH);
	
	if (_viv_toolbar_bar_high < 24)
	{
		_viv_toolbar_bar_high = 24;
	}
	
	_viv_toolbar_icon_size = _VIV_TOOLBAR_DIP_X(_VIV_TOOLBAR_ICON_SIZE);
	
	if (_viv_toolbar_icon_size < 8)
	{
		_viv_toolbar_icon_size = 8;
	}
	
	_viv_toolbar_icon_text_gap = _VIV_TOOLBAR_DIP_X(_VIV_TOOLBAR_ICON_TEXT_GAP);
	_viv_toolbar_button_pad = _VIV_TOOLBAR_DIP_X(_VIV_TOOLBAR_BUTTON_PAD);
	_viv_toolbar_button_gap = _VIV_TOOLBAR_DIP_X(_VIV_TOOLBAR_BUTTON_GAP);
	_viv_toolbar_sep_wide = (_viv_toolbar_button_gap * 2) + 1; // the 1px line with its air.
	_viv_toolbar_sep_high = _VIV_TOOLBAR_DIP_Y(_VIV_TOOLBAR_SEP_HIGH);
	
	if (_viv_toolbar_sep_high < 4)
	{
		_viv_toolbar_sep_high = 4;
	}
	
	_viv_toolbar_radius = _VIV_TOOLBAR_DIP_Y(_VIV_TOOLBAR_RADIUS);
	
	if (_viv_toolbar_radius < 1)
	{
		_viv_toolbar_radius = 1;
	}
	
	hdc = GetDC(_viv_toolbar_hwnd);
	
	if (!hdc)
	{
		return;
	}
	
	font = _viv_ui_font(VIV_UI_FONT_TOOLBAR);
	old_font = 0;
	
	if (font)
	{
		old_font = SelectObject(hdc,font);
	}
	
	for(itemi=0;itemi<_VIV_TOOLBAR_ITEM_COUNT;itemi++)
	{
		if (_viv_toolbar_items[itemi].type == _VIV_TOOLBAR_TYPE_SEP)
		{
			_viv_toolbar_item_label_wide[itemi] = _viv_toolbar_sep_wide;
		}
		else if (_viv_toolbar_items[itemi].type == _VIV_TOOLBAR_TYPE_ZOOM)
		{
			_viv_toolbar_item_label_wide[itemi] = _VIV_TOOLBAR_DIP_X(_VIV_TOOLBAR_TARGET) * 2 + _VIV_TOOLBAR_DIP_X(_VIV_TOOLBAR_ZOOM_CELL_WIDE);
		}
		else
		{
			size.cx = 0;
			size.cy = 0;
			string_copy_utf8_string(_viv_toolbar_item_text[itemi],localization_get_string((localization_id_t)_viv_toolbar_item_label_id(itemi)));
			GetTextExtentPoint32W(hdc,_viv_toolbar_item_text[itemi],(int)string_get_length(_viv_toolbar_item_text[itemi]),&size);
			_viv_toolbar_item_label_wide[itemi] = (_viv_toolbar_button_pad * 2) + _viv_toolbar_icon_size + _viv_toolbar_icon_text_gap + size.cx;
		}

		_viv_toolbar_item_glyph[itemi] = _viv_toolbar_item_glyph_id(itemi);
	}
	
	if (old_font)
	{
		SelectObject(hdc,old_font);
	}
	
	ReleaseDC(_viv_toolbar_hwnd,hdc);
	

	_viv_toolbar_walk();
	
	// the tools follow the fresh layout (rects, visibility, the play face).
	_viv_toolbar_tooltip_sync();

	// the editor is the strip's child: the strip moved, the editor parks
	// on its cell again (the status update used to own this call).
	_viv_zoom_edit_layout();
}

// Tooltips follow the live command rectangles, including More.
static int _viv_toolbar_tooltip_id(int itemi)
{
	if (_viv_toolbar_items[itemi].type != _VIV_TOOLBAR_TYPE_BUTTON)
	{
		return 0;
	}
	
	return _viv_toolbar_item_label_id(itemi);
}

// tint the tooltip control with the palette (the zoomui rule): comctl
// tooltips have no dark theme of their own, the colors arrive by message.
static void _viv_toolbar_tooltip_apply_colors(void)
{
	if (_viv_toolbar_tooltip_hwnd)
	{
		if (_viv_toolbar_dark)
		{
			SendMessage(_viv_toolbar_tooltip_hwnd,TTM_SETTIPBKCOLOR,viv_theme_color(VIV_TK_FACE),0);
			SendMessage(_viv_toolbar_tooltip_hwnd,TTM_SETTIPTEXTCOLOR,viv_theme_color(VIV_TK_TEXT),0);
		}
		else
		{
			SendMessage(_viv_toolbar_tooltip_hwnd,TTM_SETTIPBKCOLOR,GetSysColor(COLOR_INFOBK),0);
			SendMessage(_viv_toolbar_tooltip_hwnd,TTM_SETTIPTEXTCOLOR,GetSysColor(COLOR_INFOTEXT),0);
		}
	}
}

// create the control on the first layout, then keep the tools level with
// the strip: a visible button gains (or refreshes) its tool, anything
// else loses it. the play slot text refresh rides the same pass (the
// face swap runs the measure).
static void _viv_toolbar_tooltip_sync(void)
{
	int itemi;
	
	if (!_viv_toolbar_hwnd)
	{
		return;
	}
	
	if (!_viv_toolbar_tooltip_hwnd)
	{
		_viv_toolbar_tooltip_hwnd = os_CreateWindowEx(
			WS_EX_TOPMOST,
			TOOLTIPS_CLASSA,
			"",
			WS_POPUP | TTS_NOPREFIX | TTS_ALWAYSTIP,
			CW_USEDEFAULT,CW_USEDEFAULT,CW_USEDEFAULT,CW_USEDEFAULT,
			_viv_toolbar_hwnd,
			0,
			os_hinstance,
			NULL);
		
		if (_viv_toolbar_tooltip_hwnd)
		{
			_viv_toolbar_tooltip_apply_colors();
		}
	}
	
	if (!_viv_toolbar_tooltip_hwnd)
	{
		return;
	}
	
	for(itemi=0;itemi<=_VIV_TOOLBAR_ITEM_COUNT;itemi++)
	{
		int id;
		
		id = itemi == _VIV_TOOLBAR_ITEM_COUNT ? LOCALIZATION_ID_TOOLBAR_MORE : _viv_toolbar_tooltip_id(itemi);
		
		if ((id) && ((itemi == _VIV_TOOLBAR_ITEM_COUNT) ? _viv_toolbar_more_wide : _viv_toolbar_item_visible[itemi]))
		{
			TOOLINFOW ti;
			wchar_t wbuf[STRING_SIZE];
			
			os_zero_memory(&ti,sizeof(ti));
			
			ti.cbSize = sizeof(ti);
			ti.uFlags = TTF_SUBCLASS;
			ti.hwnd = _viv_toolbar_hwnd;
			ti.uId = (UINT_PTR)itemi;
			if (itemi == _VIV_TOOLBAR_ITEM_COUNT)
				_viv_toolbar_more_rect(&ti.rect);
			else
				SetRect(&ti.rect,_viv_toolbar_item_x[itemi],0,
					_viv_toolbar_item_x[itemi] + _viv_toolbar_item_wide[itemi],_viv_toolbar_bar_high);
			ti.hinst = 0;
			string_copy_utf8_string(wbuf,localization_get_string((localization_id_t)id));
			ti.lpszText = wbuf;
			
			if (_viv_toolbar_item_tool[itemi])
			{
				SendMessage(_viv_toolbar_tooltip_hwnd,TTM_SETTOOLINFOW,0,(LPARAM)&ti);
			}
			else
			{
				if (SendMessage(_viv_toolbar_tooltip_hwnd,TTM_ADDTOOLW,0,(LPARAM)&ti))
				{
					_viv_toolbar_item_tool[itemi] = 1;
				}
			}
		}
		else
		if (_viv_toolbar_item_tool[itemi])
		{
			TOOLINFOW ti;
			
			os_zero_memory(&ti,sizeof(ti));
			
			ti.cbSize = sizeof(ti);
			ti.hwnd = _viv_toolbar_hwnd;
			ti.uId = (UINT_PTR)itemi;
			
			SendMessage(_viv_toolbar_tooltip_hwnd,TTM_DELTOOLW,0,(LPARAM)&ti);
			
			_viv_toolbar_item_tool[itemi] = 0;
		}
	}
}

static void _viv_toolbar_tooltip_destroy(void)
{
	if (_viv_toolbar_tooltip_hwnd)
	{
		DestroyWindow(_viv_toolbar_tooltip_hwnd);
		
		_viv_toolbar_tooltip_hwnd = 0;
	}
	
	os_zero_memory(_viv_toolbar_item_tool,sizeof(_viv_toolbar_item_tool));
}

static void _viv_toolbar_invalidate_item(int itemi)
{
	RECT rect;
	
	if ((itemi >= 0) && (itemi < _VIV_TOOLBAR_ITEM_COUNT) && (_viv_toolbar_hwnd) && (_viv_toolbar_item_visible[itemi]))
	{
		rect.left = _viv_toolbar_item_x[itemi];
		rect.top = 0;
		rect.right = _viv_toolbar_item_x[itemi] + _viv_toolbar_item_wide[itemi];
		rect.bottom = _viv_toolbar_bar_high;
		
		InvalidateRect(_viv_toolbar_hwnd,&rect,FALSE);
	}
}

// the zoom cluster's percent cell, in the strip's client coordinates: the
// home the painted percent centres in and the in place editor anchors to.
// 0 answers a strip without the cluster (masked out, paged away, hidden).
static int _viv_toolbar_zoom_cell_rect(RECT *rect)
{
	int itemi;

	for(itemi=0;itemi<_VIV_TOOLBAR_ITEM_COUNT;itemi++)
	{
		if ((_viv_toolbar_items[itemi].type == _VIV_TOOLBAR_TYPE_ZOOM) && (_viv_toolbar_item_visible[itemi]))
		{
			rect->left = _viv_toolbar_item_x[itemi] + _VIV_TOOLBAR_DIP_X(_VIV_TOOLBAR_TARGET);
			rect->top = (_viv_toolbar_bar_high - _VIV_TOOLBAR_DIP_Y(_VIV_TOOLBAR_TARGET)) / 2;
			rect->right = _viv_toolbar_item_x[itemi] + _viv_toolbar_item_wide[itemi] - _VIV_TOOLBAR_DIP_X(_VIV_TOOLBAR_TARGET);
			rect->bottom = rect->top + _VIV_TOOLBAR_DIP_Y(_VIV_TOOLBAR_TARGET);

			return 1;
		}
	}

	return 0;
}

// the exported anchors (viv_dialogs.c): the editor is the strip's child
// while it is open, parked on the cell it edits.
HWND _viv_toolbar_window(void)
{
	return _viv_toolbar_hwnd;
}

int _viv_toolbar_zoom_cell(RECT *rect)
{
	int target;
	if (!_viv_toolbar_hwnd || !IsWindowVisible(_viv_toolbar_hwnd)) return 0;
	if (_viv_toolbar_zoom_cell_rect(rect)) return 1;
	if (!_viv_toolbar_item_overflow[_VIV_TOOLBAR_ITEM_ZOOM]) return 0;

	// An exact zoom selected from More edits in the right end of the bar.
	target = _VIV_TOOLBAR_DIP_X(_VIV_TOOLBAR_ZOOM_CELL_WIDE);
	if (_viv_toolbar_wide < target) return 0;
	SetRect(rect,_viv_toolbar_wide - target,0,_viv_toolbar_wide,_viv_toolbar_bar_high);
	return 1;
}

// the cluster zone under the point: -1 the minus, 0 the percent, 1 the
// plus, -2 nothing (the point is not on the cluster).
static int _viv_toolbar_zoom_zone(int x,int y)
{
	RECT cell;
	int zone_x;

	if ((y < 0) || (y >= _viv_toolbar_bar_high) || (!_viv_toolbar_zoom_cell_rect(&cell)))
	{
		return -2;
	}

	zone_x = cell.left - _VIV_TOOLBAR_DIP_X(_VIV_TOOLBAR_TARGET);

	if ((x >= zone_x) && (x < cell.left))
	{
		return -1;
	}

	if ((x >= cell.left) && (x < cell.right))
	{
		return 0;
	}

	if ((x >= cell.right) && (x < cell.right + _VIV_TOOLBAR_DIP_X(_VIV_TOOLBAR_TARGET)))
	{
		return 1;
	}

	return -2;
}

// the zone's own invalidation: the whole cluster rect is cheap, the
// faces are small, and one helper answers hover, press and release.
static void _viv_toolbar_invalidate_zoom(int zone)
{
	RECT cell;

	if ((zone == -2) || (!_viv_toolbar_hwnd) || (!_viv_toolbar_zoom_cell_rect(&cell)))
	{
		return;
	}

	if (zone == -1)
	{
		cell.left -= _VIV_TOOLBAR_DIP_X(_VIV_TOOLBAR_TARGET);
	}
	else if (zone == 1)
	{
		cell.right += _VIV_TOOLBAR_DIP_X(_VIV_TOOLBAR_TARGET);
	}

	InvalidateRect(_viv_toolbar_hwnd,&cell,FALSE);
}

// fire a cluster zone: the same wm_command the old zoom pair sent, and
// the percent cell opens the in place editor that used to live on the
// status pane.
static void _viv_toolbar_zoom_fire(int zone)
{
	int command_id;

	command_id = 0;

	if (zone < 0)
	{
		command_id = VIV_ID_VIEW_ZOOM_OUT;
	}
	else if (zone > 0)
	{
		command_id = VIV_ID_VIEW_ZOOM_IN;
	}

	if (command_id)
	{
		SendMessage(_viv_hwnd,WM_COMMAND,MAKEWPARAM(command_id,BN_CLICKED),0);
	}
	else
	{
		_viv_set_zoom_dialog();
	}

	// the strip never keeps a focus a click can land on it (the fire
	// path's own rule): the keyboard shortcuts answer the next key press.
	if (zone) SetFocus(_viv_hwnd);
}

static int _viv_toolbar_hit_test(int x,int y)
{
	int itemi;
	
	if ((y < 0) || (y >= _viv_toolbar_bar_high))
	{
		return -1;
	}
	
	for(itemi=0;itemi<_VIV_TOOLBAR_ITEM_COUNT;itemi++)
	{
		if ((_viv_toolbar_item_visible[itemi]) && (_viv_toolbar_items[itemi].type == _VIV_TOOLBAR_TYPE_BUTTON) &&
			(x >= _viv_toolbar_item_x[itemi]) && (x < _viv_toolbar_item_x[itemi] + _viv_toolbar_item_wide[itemi]))
		{
			return itemi;
		}
	}
	
	return -1;
}

// fire a button: the same bn_clicked notification a real button would have
// sent, then the focus back to the viewer so the keyboard shortcuts keep
// working (the zoom pill's rule).
static void _viv_toolbar_fire(int itemi)
{
	int command_id;
	
	if ((itemi < 0) || (itemi >= _VIV_TOOLBAR_ITEM_COUNT))
	{
		return;
	}
	
	command_id = _viv_toolbar_items[itemi].command_id;
	
	// the play slot resolves from the live state (the pill rule): a
	// running slideshow pauses in place, an animated image toggles the
	// animation clock, idle starts the windowed slideshow - the slot
	// never forces the fullscreen jump the old command did. (rc.7: the
	// animation branch carries the frame-count gate the rc.13 face rule
	// already pinned - a static image no longer eats the first click.)
	if (itemi == _VIV_TOOLBAR_ITEM_PLAY)
	{
		if (_viv_is_slideshow)
		{
			command_id = VIV_ID_SLIDESHOW_PAUSE_ONLY;
		}
		// an animated image toggles its own clock here - playing or
		// paused alike. the paused state used to fall through to the
		// slideshow start: the face said "resume", the click started a
		// slideshow, and the animation could never resume from the
		// toolbar (the field report's dead animation toolbar).
		else if (_viv_slot_current.frame_count > 1)
		{
			command_id = VIV_ID_ANIMATION_PLAY_PAUSE;
		}
		else
		{
			command_id = VIV_ID_SLIDESHOW_PLAY_ONLY;
		}
	}
	
	SendMessage(_viv_hwnd,WM_COMMAND,MAKEWPARAM(command_id,BN_CLICKED),0);
	
	SetFocus(_viv_hwnd);
}

static void _viv_toolbar_overflow_row(HMENU menu, UINT_PTR id, int label, int enabled)
{
	wchar_t text[STRING_SIZE];
	void *row = _viv_menu_row_alloc(_VIV_MENU_POOL_CONTEXT,_VIV_MENU_DRAW_LOCALIZED,0,label,0);
	string_copy_utf8_string(text,localization_get_string((localization_id_t)label));
	AppendMenuW(menu,MF_STRING | (enabled ? MF_ENABLED : MF_GRAYED) |
		(row ? MF_OWNERDRAW : 0),id,row ? (LPCWSTR)row : text);
}

static void _viv_toolbar_open_overflow(void)
{
	HMENU menu;
	POINT point;
	int itemi, choice;

	if (!_viv_toolbar_more_wide) return;
	menu = CreatePopupMenu();
	if (!menu) return;
	_viv_menu_row_pool_reset(_VIV_MENU_POOL_CONTEXT);

	for (itemi = 0; itemi < _VIV_TOOLBAR_ITEM_COUNT; itemi++)
	{
		if (!_viv_toolbar_item_overflow[itemi]) continue;
		if (_viv_toolbar_items[itemi].type == _VIV_TOOLBAR_TYPE_ZOOM)
		{
			_viv_toolbar_overflow_row(menu,_VIV_TOOLBAR_ITEM_COUNT + 1,LOCALIZATION_ID_TOOLBAR_ZOOM_OUT_BUTTON,1);
			_viv_toolbar_overflow_row(menu,_VIV_TOOLBAR_ITEM_COUNT + 2,LOCALIZATION_ID_ZOOM_SET,1);
			_viv_toolbar_overflow_row(menu,_VIV_TOOLBAR_ITEM_COUNT + 3,LOCALIZATION_ID_TOOLBAR_ZOOM_IN_BUTTON,1);
		}
		else
		{
			_viv_toolbar_overflow_row(menu,itemi + 1,_viv_toolbar_item_label_id(itemi),_viv_toolbar_item_enabled[itemi]);
		}
	}
	point.x = _viv_toolbar_wide;
	point.y = _viv_toolbar_bar_high;
	ClientToScreen(_viv_toolbar_hwnd,&point);
	_viv_show_cursor();
	choice = TrackPopupMenu(menu,TPM_RETURNCMD | TPM_RIGHTALIGN | TPM_RIGHTBUTTON,point.x,point.y,0,_viv_hwnd,0);
	DestroyMenu(menu);
	_viv_update_show_cursor();

	if (choice > _VIV_TOOLBAR_ITEM_COUNT)
		_viv_toolbar_zoom_fire(choice - _VIV_TOOLBAR_ITEM_COUNT - 2);
	else if (choice > 0)
		_viv_toolbar_fire(choice - 1);
}

static int _viv_toolbar_keyboard_step(int current,int delta)
{
	int remaining;
	for (remaining = 0; remaining <= _VIV_TOOLBAR_ITEM_COUNT; remaining++)
	{
		current = (current + delta + _VIV_TOOLBAR_ITEM_COUNT + 1) % (_VIV_TOOLBAR_ITEM_COUNT + 1);
		if (current == _VIV_TOOLBAR_ITEM_COUNT)
		{
			if (_viv_toolbar_more_wide) return current;
		}
		else if (_viv_toolbar_item_visible[current] && _viv_toolbar_item_enabled[current] &&
			_viv_toolbar_items[current].type != _VIV_TOOLBAR_TYPE_SEP)
		{
			return current;
		}
	}
	return -1;
}

static LRESULT CALLBACK _viv_toolbar_proc(HWND hwnd,UINT msg,WPARAM wParam,LPARAM lParam)
{
	switch (msg)
	{
		case WM_GETDLGCODE:
			return DLGC_WANTARROWS;
		case WM_SETFOCUS:
			_viv_toolbar_focus = _viv_toolbar_keyboard_step(-1,1);
			InvalidateRect(hwnd,0,FALSE);
			return 0;
		case WM_KILLFOCUS:
			_viv_toolbar_focus = -1;
			InvalidateRect(hwnd,0,FALSE);
			return 0;
		case WM_KEYDOWN:
			switch (wParam)
			{
				case VK_LEFT:
				case VK_RIGHT:
					_viv_toolbar_focus = _viv_toolbar_keyboard_step(
						_viv_toolbar_focus < 0 ? _VIV_TOOLBAR_ITEM_COUNT : _viv_toolbar_focus,
						wParam == VK_LEFT ? -1 : 1);
					InvalidateRect(hwnd,0,FALSE);
					return 0;
				case VK_RETURN:
				case VK_SPACE:
				case VK_DOWN:
					if (_viv_toolbar_focus == _VIV_TOOLBAR_ITEM_COUNT)
						_viv_toolbar_open_overflow();
					else if (_viv_toolbar_focus == _VIV_TOOLBAR_ITEM_ZOOM && _viv_toolbar_item_visible[_VIV_TOOLBAR_ITEM_ZOOM])
						_viv_set_zoom_dialog();
					else if (_viv_toolbar_focus >= 0 && _viv_toolbar_item_visible[_viv_toolbar_focus] &&
						_viv_toolbar_item_enabled[_viv_toolbar_focus] && wParam != VK_DOWN)
						_viv_toolbar_fire(_viv_toolbar_focus);
					return 0;
				case VK_TAB:
				case VK_ESCAPE:
					SetFocus(_viv_hwnd);
					return 0;
			}
			break;

		case WM_PAINT:
		{
			PAINTSTRUCT ps;
			RECT rect;
			RECT fill_rect;
			RECT text_rect;
			HDC hdc;
			HFONT font;
			HFONT old_font;
			HGDIOBJ old_pen;
			HICON icon;
			COLORREF text_color;
			COLORREF disabled_color;
			int itemi;
			int hot;
			int offset;
			int sep_x;
			int text_left;
			
			hdc = BeginPaint(hwnd,&ps);
			
			GetClientRect(hwnd,&rect);
			
			// the face is one flat fill: the dark toolbar face or the system
			// button face. no system painter ever touches this strip.
			FillRect(hdc,&rect,_viv_toolbar_face_brush());
			
			// the top edge: the one chrome line the strip carries (the classic
			// 3d shadow in the light ui, the palette edge line in the dark).
			fill_rect.left = rect.left;
			fill_rect.top = rect.top;
			fill_rect.right = rect.right;
			fill_rect.bottom = rect.top + 1;
			
			FillRect(hdc,&fill_rect,_viv_toolbar_line_brush());

			// the seam: one hairline where the menus hand the band over
			// to the commands (a hidden menu bar leaves the strip alone).
			if (_viv_menubar_strip_wide())
			{
				fill_rect.left = rect.left;
				fill_rect.top = (_viv_toolbar_bar_high - _viv_toolbar_sep_high) / 2;
				fill_rect.right = rect.left + 1;
				fill_rect.bottom = fill_rect.top + _viv_toolbar_sep_high;

				FillRect(hdc,&fill_rect,_viv_toolbar_line_brush());
			}

			font = _viv_ui_font(VIV_UI_FONT_TOOLBAR);
			old_font = 0;
			
			if (font)
			{
				old_font = SelectObject(hdc,font);
			}
			
			SetBkMode(hdc,TRANSPARENT);
			
			text_color = viv_theme_color(VIV_TK_TEXT);
			disabled_color = viv_theme_color(VIV_TK_TEXTOFF);
			
			// the hover faces draw through a null pen so the rounded fill
			// carries no border of its own.
			old_pen = SelectObject(hdc,GetStockObject(NULL_PEN));
			
			for(itemi=0;itemi<_VIV_TOOLBAR_ITEM_COUNT;itemi++)
			{
				if (!_viv_toolbar_item_visible[itemi])
				{
					continue;
				}
				
				if (_viv_toolbar_items[itemi].type == _VIV_TOOLBAR_TYPE_SEP)
				{
					sep_x = _viv_toolbar_item_x[itemi] + (_viv_toolbar_sep_wide / 2);
					
					fill_rect.left = sep_x;
					fill_rect.top = (_viv_toolbar_bar_high - _viv_toolbar_sep_high) / 2;
					fill_rect.right = sep_x + 1;
					fill_rect.bottom = fill_rect.top + _viv_toolbar_sep_high;
					
					FillRect(hdc,&fill_rect,_viv_toolbar_line_brush());
					
					continue;
				}
				
					if (_viv_toolbar_items[itemi].type == _VIV_TOOLBAR_TYPE_ZOOM)
					{
						// the zoom cluster: the one control that owns a value, so the
						// one control with a face - the input capsule with its
						// hairline, the painted percent between the two handles. the
						// handles answer with the strip's own hover and press faces.
						{
							RECT zcell;
							HPEN zpen;
							HPEN zold_pen;
							HGDIOBJ zold_brush;
							HGDIOBJ zold_font;
							HICON zicon;
							HFONT zfont;
							wchar_t zoom_buf[STRING_SIZE];
							int zface_top;
							int zone;
							int zx;

							_viv_toolbar_zoom_cell_rect(&zcell);
							zface_top = zcell.top;

							zpen = CreatePen(PS_SOLID,1,viv_theme_color(VIV_TK_CHROME_LINE));
							zold_pen = (HPEN)SelectObject(hdc,zpen);
							zold_brush = SelectObject(hdc,viv_theme_brush(VIV_TK_INPUT));
							RoundRect(hdc,zcell.left - _VIV_TOOLBAR_DIP_X(_VIV_TOOLBAR_TARGET),zface_top,zcell.right + _VIV_TOOLBAR_DIP_X(_VIV_TOOLBAR_TARGET),zcell.bottom,zcell.bottom - zface_top,zcell.bottom - zface_top);
							SelectObject(hdc,zold_brush);
							SelectObject(hdc,zold_pen);
							DeleteObject(zpen);

							// the press owns the zone faces while the capture holds (the
							// arrows' rule): one cluster, so the state needs no item index.
							zone = (_viv_toolbar_zoom_pressed != -2) ? _viv_toolbar_zoom_pressed : _viv_toolbar_zoom_hover;
							if ((zone == -1) || (zone == 1))
							{
								zx = (zone < 0) ? (zcell.left - _VIV_TOOLBAR_DIP_X(_VIV_TOOLBAR_TARGET)) : zcell.right;
								SelectObject(hdc,(zone == _viv_toolbar_zoom_pressed) ? _viv_toolbar_press_brush() : _viv_toolbar_hot_brush());
								SelectObject(hdc,GetStockObject(NULL_PEN));
								RoundRect(hdc,zx + _VIV_TOOLBAR_DIP_X(2),zface_top + _VIV_TOOLBAR_DIP_Y(2),zx + _VIV_TOOLBAR_DIP_X(_VIV_TOOLBAR_TARGET) - _VIV_TOOLBAR_DIP_X(2),zcell.bottom - _VIV_TOOLBAR_DIP_Y(2),_viv_toolbar_radius * 2,_viv_toolbar_radius * 2);
							}

							zicon = glyphs_icon(GLYPH_MAGNIFIER_MINUS,_viv_toolbar_dark,_viv_toolbar_icon_size);
							if (zicon)
							{
								DrawIconEx(hdc,zcell.left - _VIV_TOOLBAR_DIP_X(_VIV_TOOLBAR_TARGET) + ((_VIV_TOOLBAR_DIP_X(_VIV_TOOLBAR_TARGET) - _viv_toolbar_icon_size) / 2),(zface_top + ((zcell.bottom - zface_top) - _viv_toolbar_icon_size) / 2),zicon,_viv_toolbar_icon_size,_viv_toolbar_icon_size,0,NULL,DI_NORMAL);
							}
							zicon = glyphs_icon(GLYPH_MAGNIFIER_PLUS,_viv_toolbar_dark,_viv_toolbar_icon_size);
							if (zicon)
							{
								DrawIconEx(hdc,zcell.right + ((_VIV_TOOLBAR_DIP_X(_VIV_TOOLBAR_TARGET) - _viv_toolbar_icon_size) / 2),(zface_top + ((zcell.bottom - zface_top) - _viv_toolbar_icon_size) / 2),zicon,_viv_toolbar_icon_size,_viv_toolbar_icon_size,0,NULL,DI_NORMAL);
							}

							// the percent is painted text (no edit control owns it): the
							// value's one persistent home, read live at paint time.
							string_printf(zoom_buf,localization_get_string(LOCALIZATION_ID_STATUS_BAR_ZOOM_CHIP_FORMAT),_viv_zoom_percent());
							zfont = _viv_ui_font(VIV_UI_FONT_PERCENT);
							zold_font = 0;
							if (zfont)
							{
								zold_font = SelectObject(hdc,zfont);
							}
							SetTextColor(hdc,text_color);
							DrawTextW(hdc,zoom_buf,-1,&zcell,DT_SINGLELINE | DT_CENTER | DT_VCENTER);
							if (zold_font)
							{
								SelectObject(hdc,zold_font);
							}
						}

						continue;
					}

				hot = ((_viv_toolbar_item_enabled[itemi]) && ((itemi == _viv_toolbar_hover) || (itemi == _viv_toolbar_pressed) || (itemi == _viv_toolbar_focus))) ? 1 : 0;
				
				// the pressed content nudges one pixel down right, but only
				// while the press is still under the cursor.
				offset = ((itemi == _viv_toolbar_pressed) && (itemi == _viv_toolbar_hover)) ? 1 : 0;
				
				if (hot)
				{
					SelectObject(hdc,(itemi == _viv_toolbar_pressed) ? _viv_toolbar_press_brush() : _viv_toolbar_hot_brush());
					
					// the face is the touch target itself: 44 x 44 centred in the
					// band, so the air above and below stays quiet.
					RoundRect(hdc,_viv_toolbar_item_x[itemi],(_viv_toolbar_bar_high - _VIV_TOOLBAR_DIP_Y(_VIV_TOOLBAR_TARGET)) / 2,_viv_toolbar_item_x[itemi] + _viv_toolbar_item_wide[itemi],(_viv_toolbar_bar_high + _VIV_TOOLBAR_DIP_Y(_VIV_TOOLBAR_TARGET)) / 2,_viv_toolbar_radius * 2,_viv_toolbar_radius * 2);
				}
				
				icon = glyphs_icon(_viv_toolbar_item_glyph[itemi],_viv_toolbar_dark,_viv_toolbar_icon_size);
				
				if (icon)
				{
					int icon_x;
					int icon_y;
					
					// the icon rides at the pad in the full strip and centered
					// in the icon-only strip (the label never enters the box).
					icon_x = _viv_toolbar_compact ? (_viv_toolbar_item_x[itemi] + ((_viv_toolbar_item_wide[itemi] - _viv_toolbar_icon_size) / 2) + offset) : (_viv_toolbar_item_x[itemi] + _viv_toolbar_button_pad + offset);
					icon_y = ((_viv_toolbar_bar_high - _viv_toolbar_icon_size) / 2) + offset;
					
					if (!_viv_toolbar_item_enabled[itemi])
					{
						// the embossed disabled glyph through the stock raster
						// op (the zoom pill's disabled state, same trick: no
						// extra cache, same theme aware icon).
						DrawState(hdc,NULL,NULL,(LPARAM)icon,0,icon_x,icon_y,_viv_toolbar_icon_size,_viv_toolbar_icon_size,DST_ICON | DSS_DISABLED);
					}
					else
					{
						DrawIconEx(hdc,icon_x,icon_y,icon,_viv_toolbar_icon_size,_viv_toolbar_icon_size,0,NULL,DI_NORMAL);
					}
				}
				
				if (!_viv_toolbar_compact)
				{
					text_left = _viv_toolbar_item_x[itemi] + _viv_toolbar_button_pad + _viv_toolbar_icon_size + _viv_toolbar_icon_text_gap + offset;
					
					text_rect.left = text_left;
					text_rect.top = 0;
					text_rect.right = _viv_toolbar_item_x[itemi] + _viv_toolbar_item_wide[itemi] - _viv_toolbar_button_pad;
					text_rect.bottom = _viv_toolbar_bar_high;
					
					SetTextColor(hdc,_viv_toolbar_item_enabled[itemi] ? text_color : disabled_color);
					
					DrawTextW(hdc,_viv_toolbar_item_text[itemi],-1,&text_rect,DT_SINGLELINE | DT_LEFT | DT_VCENTER);
				}
			}
			
			if (_viv_toolbar_more_wide)
			{
				RECT more;
				int size = _viv_toolbar_icon_size < _viv_toolbar_more_wide ? _viv_toolbar_icon_size : _viv_toolbar_more_wide;
				_viv_toolbar_more_rect(&more);
				if (_viv_toolbar_more_hover || _viv_toolbar_more_pressed || _viv_toolbar_focus == _VIV_TOOLBAR_ITEM_COUNT)
				{
					SelectObject(hdc,_viv_toolbar_more_pressed ? _viv_toolbar_press_brush() : _viv_toolbar_hot_brush());
					RoundRect(hdc,more.left,more.top,more.right,more.bottom,_viv_toolbar_radius * 2,_viv_toolbar_radius * 2);
				}
				icon = glyphs_icon(GLYPH_MORE,_viv_toolbar_dark,size);
				if (icon)
				{
					DrawIconEx(hdc,more.left + (_viv_toolbar_more_wide - size) / 2,
						(_viv_toolbar_bar_high - size) / 2,icon,size,size,0,NULL,DI_NORMAL);
				}
			}

			SelectObject(hdc,old_pen);
			
			if (old_font)
			{
				SelectObject(hdc,old_font);
			}
			
			EndPaint(hwnd,&ps);
			
			return 0;
		}
		
		case WM_ERASEBKGND:
		{
			RECT rect;
			
			GetClientRect(hwnd,&rect);
			
			FillRect((HDC)wParam,&rect,_viv_toolbar_face_brush());
			
			return 1;
		}
		
		case WM_MOUSEMOVE:
		{
			TRACKMOUSEEVENT tme;
			int hit;
			int more;
			int zone;
			
			// re-arm the leave track on every uncaptured move: a press
			// swallows leave generation, and without the re-arm the hover
			// sticks after a drag-off release (the zoomui pattern).
			if (GetCapture() != hwnd)
			{
				os_zero_memory(&tme,sizeof(tme));
				tme.cbSize = sizeof(tme);
				tme.dwFlags = TME_LEAVE;
				tme.hwndTrack = hwnd;
				
				TrackMouseEvent(&tme);
				
				_viv_toolbar_tracking = 1;
			}
			
			// More and the zoom zones take precedence over items

			more = _viv_toolbar_more_hit_test(GET_X_LPARAM(lParam),GET_Y_LPARAM(lParam));
			
			zone = _viv_toolbar_zoom_zone(GET_X_LPARAM(lParam),GET_Y_LPARAM(lParam));

			if (zone != _viv_toolbar_zoom_hover)
			{
				_viv_toolbar_invalidate_zoom(_viv_toolbar_zoom_hover);
				_viv_toolbar_invalidate_zoom(zone);

				_viv_toolbar_zoom_hover = zone;
			}

			if (more != _viv_toolbar_more_hover)
			{
				_viv_toolbar_more_hover = more;
				_viv_toolbar_invalidate_more();
			}
			
			hit = (more || (zone != -2)) ? -1 : _viv_toolbar_hit_test(GET_X_LPARAM(lParam),GET_Y_LPARAM(lParam));
			
			if (hit != _viv_toolbar_hover)
			{
				// a captured drag moves the hover with the cursor (the press
				// visual tracks, the command still fires only on the release
				// over the same button).
				_viv_toolbar_invalidate_item(_viv_toolbar_hover);
				_viv_toolbar_invalidate_item(hit);
				
				_viv_toolbar_hover = hit;
			}
			
			return 0;
		}
		
		case WM_MOUSELEAVE:
		
			_viv_toolbar_tracking = 0;
			
			// during a captured press the leave is stale: the release
			// handler owns the hover accounting.
			if (GetCapture() == hwnd)
			{
				return 0;
			}
			
			if (_viv_toolbar_more_hover)
			{
				_viv_toolbar_more_hover = 0;
				_viv_toolbar_invalidate_more();
			}

			if (_viv_toolbar_zoom_hover != -2)
			{
				_viv_toolbar_invalidate_zoom(_viv_toolbar_zoom_hover);

				_viv_toolbar_zoom_hover = -2;
			}

			if (_viv_toolbar_hover != -1)
			{
				_viv_toolbar_invalidate_item(_viv_toolbar_hover);
				
				_viv_toolbar_hover = -1;
			}
			
			return 0;
		
		case WM_LBUTTONDBLCLK:
		// the double click lands as a fresh press (the zoomui rule: the
		// face follows the physical button, not the click count).
		case WM_LBUTTONDOWN:
		{
			int hit;
			int more;
			int zone;
			
			more = _viv_toolbar_more_hit_test(GET_X_LPARAM(lParam),GET_Y_LPARAM(lParam));
			
			if (more)
			{
				_viv_toolbar_more_pressed = _viv_toolbar_more_hover = 1;
				SetCapture(hwnd);
				_viv_toolbar_invalidate_more();
				return 0;
			}

			hit = _viv_toolbar_hit_test(GET_X_LPARAM(lParam),GET_Y_LPARAM(lParam));
			
			// a disabled button never takes the press: no capture, no face,
			// so the release can never fire it (the classic rule).
			zone = _viv_toolbar_zoom_zone(GET_X_LPARAM(lParam),GET_Y_LPARAM(lParam));

			if (zone != -2)
			{
				// the zones press like the arrows: capture, the face, the
				// fire on the release inside the same zone.
				_viv_toolbar_invalidate_zoom(_viv_toolbar_zoom_pressed);

				_viv_toolbar_zoom_pressed = zone;
				_viv_toolbar_zoom_hover = zone;

				SetCapture(hwnd);

				_viv_toolbar_invalidate_zoom(zone);

				return 0;
			}

			if ((hit >= 0) && (_viv_toolbar_item_enabled[hit]))
			{
				// press and hold: the command fires on the release inside the
				// same button (drag off the strip to cancel, the classic rule).
				_viv_toolbar_invalidate_item(_viv_toolbar_hover);
				
				_viv_toolbar_pressed = hit;
				_viv_toolbar_hover = hit;
				
				SetCapture(hwnd);
				
				_viv_toolbar_invalidate_item(hit);
			}
			else if ((hit < 0) && (config_toolbar_move_window))
			{
				// the rebar band drag lives on: dragging the strip
				// background moves the window (the caption drag).
				_viv_start_move_window();
			}
			
			return 0;
		}
		
		case WM_LBUTTONUP:
		{
			int hit;
			int pressed;
			int more;
			int zone;
			int hit_zone;
			
			if (_viv_toolbar_more_pressed)
			{
				_viv_toolbar_more_pressed = 0;
				ReleaseCapture();
				more = _viv_toolbar_more_hit_test(GET_X_LPARAM(lParam),GET_Y_LPARAM(lParam));
				_viv_toolbar_more_hover = more;
				_viv_toolbar_invalidate_more();
				if (more) _viv_toolbar_open_overflow();
				return 0;
			}

			if (_viv_toolbar_zoom_pressed != -2)
			{
				zone = _viv_toolbar_zoom_pressed;

				_viv_toolbar_zoom_pressed = -2;

				ReleaseCapture();

				hit_zone = _viv_toolbar_zoom_zone(GET_X_LPARAM(lParam),GET_Y_LPARAM(lParam));

				_viv_toolbar_invalidate_zoom(zone);

				_viv_toolbar_zoom_hover = hit_zone;

				_viv_toolbar_invalidate_zoom(hit_zone);

				if (hit_zone == zone)
				{
					_viv_toolbar_zoom_fire(zone);
				}

				return 0;
			}

			if (_viv_toolbar_pressed >= 0)
			{
				pressed = _viv_toolbar_pressed;
				
				_viv_toolbar_pressed = -1;
				
				ReleaseCapture();
				
				hit = _viv_toolbar_hit_test(GET_X_LPARAM(lParam),GET_Y_LPARAM(lParam));
				
				_viv_toolbar_invalidate_item(pressed);
				
				_viv_toolbar_hover = hit;
				
				_viv_toolbar_invalidate_item(hit);
				
				if ((hit == pressed) && (_viv_toolbar_item_enabled[hit]))
				{
					_viv_toolbar_fire(hit);
				}
			}
			
			return 0;
		}
		
		case WM_CTLCOLOREDIT:

			// the in place zoom editor: the strip palette owns the colors so
			// the field reads as part of the chrome, not a foreign control -
			// the nav face and text token, the same pair the status pane's
			// paint rode before the cluster gave the value this home. (the
			// selection highlight stays the system color - the edit control
			// offers no custom one; it lives only while the editor is open.)
			SetBkColor((HDC)wParam,viv_theme_color(VIV_TK_NAV_FACE));
			SetTextColor((HDC)wParam,viv_theme_color(VIV_TK_TEXT));

			return (LRESULT)viv_theme_brush(VIV_TK_NAV_FACE);

		case WM_SETCURSOR:
		{
			// the hand over the percent cell: the cell opens the in place
			// editor, the same promise the status pane made.
			POINT pt;
			RECT cell;

			if ((LOWORD(lParam) == HTCLIENT) && (_viv_toolbar_zoom_cell_rect(&cell)))
			{
				GetCursorPos(&pt);
				ScreenToClient(hwnd,&pt);

				if (PtInRect(&cell,pt))
				{
					SetCursor(LoadCursor(NULL,IDC_HAND));

					return TRUE;
				}
			}

			break;
		}

		case WM_CONTEXTMENU:
		{
			// the customize popup: right-clicking the strip toggles the
			// button groups (the upstream TODO's control toolbar
			// customization item, closed).
			_viv_toolbar_context_menu(hwnd,(int)(short)LOWORD(lParam),(int)(short)HIWORD(lParam));
			
			return 0;
		}
		
		case WM_CAPTURECHANGED:
			_viv_toolbar_invalidate_item(_viv_toolbar_pressed);
			_viv_toolbar_pressed = -1;
			_viv_toolbar_invalidate_zoom(_viv_toolbar_zoom_pressed);
			_viv_toolbar_zoom_pressed = -2;
			_viv_toolbar_more_pressed = 0;
			_viv_toolbar_invalidate_more();
			return 0;

		default:
		
			break;
	}
	
	return DefWindowProc(hwnd,msg,wParam,lParam);
}

// the customize popup: the six group toggles and the show-all row ride
// the context pool's owner-drawn pipeline (the dark rows the context
// menu already paints); the ids answer through the wndproc interceptor
// and the labels live in the localization tail.
static void _viv_toolbar_context_menu(HWND toolbar_hwnd,int screen_x,int screen_y)
{
	HMENU hmenu;

	hmenu = CreatePopupMenu();

	if (hmenu)
	{
		static const int group_ids[_VIV_TOOLBAR_GROUP_MAX + 1] =
		{
			0,
			LOCALIZATION_ID_TOOLBAR_GROUP_NAV,
			LOCALIZATION_ID_TOOLBAR_GROUP_FIT,
			LOCALIZATION_ID_TOOLBAR_GROUP_ZOOM,
			LOCALIZATION_ID_TOOLBAR_GROUP_ROTATE,
		};
		int i;

		_viv_menu_row_pool_reset(_VIV_MENU_POOL_CONTEXT);

		// the title row: a grayed label so the popup names itself.
		{
			wchar_t text_wbuf[STRING_SIZE];
			void *row;

			string_copy_utf8_string(text_wbuf,localization_get_string(LOCALIZATION_ID_TOOLBAR_CUSTOMIZE));

			row = _viv_menu_row_alloc(_VIV_MENU_POOL_CONTEXT,_VIV_MENU_DRAW_LOCALIZED,0,LOCALIZATION_ID_TOOLBAR_CUSTOMIZE,0);

			if (row)
			{
				AppendMenuW(hmenu,MF_STRING | MF_OWNERDRAW | MF_GRAYED,_VIV_TOOLBAR_CONTEXT_ID_FIRST,(LPCWSTR)row);
			}
			else
			{
				AppendMenuW(hmenu,MF_STRING | MF_GRAYED,_VIV_TOOLBAR_CONTEXT_ID_FIRST,text_wbuf);
			}
		}

		for(i=1;i<=_VIV_TOOLBAR_GROUP_MAX;i++)
		{
			wchar_t text_wbuf[STRING_SIZE];
			UINT command_id;
			void *row;

			command_id = (UINT)(_VIV_TOOLBAR_CONTEXT_ID_FIRST + 1 + i);

			string_copy_utf8_string(text_wbuf,localization_get_string((localization_id_t)group_ids[i]));

			row = _viv_menu_row_alloc(_VIV_MENU_POOL_CONTEXT,_VIV_MENU_DRAW_LOCALIZED,0,group_ids[i],0);

			if (row)
			{
				AppendMenuW(hmenu,MF_STRING | MF_OWNERDRAW,command_id,(LPCWSTR)row);
			}
			else
			{
				AppendMenuW(hmenu,MF_STRING,command_id,text_wbuf);
			}

			CheckMenuItem(hmenu,command_id,(config_toolbar_groups & (1 << i)) ? MF_CHECKED : MF_UNCHECKED);
		}

		// the separator and the show-all row.
		{
			wchar_t text_wbuf[STRING_SIZE];
			void *row;

			row = _viv_menu_row_alloc(_VIV_MENU_POOL_CONTEXT,_VIV_MENU_DRAW_SEPARATOR,0,0,0);

			if (row)
			{
				AppendMenuW(hmenu,MF_SEPARATOR | MF_OWNERDRAW,0,(LPCWSTR)row);
			}
			else
			{
				AppendMenuW(hmenu,MF_SEPARATOR,0,0);
			}

			string_copy_utf8_string(text_wbuf,localization_get_string(LOCALIZATION_ID_TOOLBAR_SHOW_ALL));

			row = _viv_menu_row_alloc(_VIV_MENU_POOL_CONTEXT,_VIV_MENU_DRAW_LOCALIZED,0,LOCALIZATION_ID_TOOLBAR_SHOW_ALL,0);

			if (row)
			{
				AppendMenuW(hmenu,MF_STRING | MF_OWNERDRAW,_VIV_TOOLBAR_CONTEXT_ID_FIRST + 7,(LPCWSTR)row);
			}
			else
			{
				AppendMenuW(hmenu,MF_STRING,_VIV_TOOLBAR_CONTEXT_ID_FIRST + 7,text_wbuf);
			}
		}

		// the icon-only row: labels off, the strip shrinks to the glyphs.
		{
			wchar_t text_wbuf[STRING_SIZE];
			void *row;
			UINT command_id;

			command_id = (UINT)(_VIV_TOOLBAR_CONTEXT_ID_FIRST + 8);

			string_copy_utf8_string(text_wbuf,localization_get_string(LOCALIZATION_ID_TOOLBAR_ICON_ONLY));

			row = _viv_menu_row_alloc(_VIV_MENU_POOL_CONTEXT,_VIV_MENU_DRAW_LOCALIZED,0,LOCALIZATION_ID_TOOLBAR_ICON_ONLY,0);

			if (row)
			{
				AppendMenuW(hmenu,MF_STRING | MF_OWNERDRAW,command_id,(LPCWSTR)row);
			}
			else
			{
				AppendMenuW(hmenu,MF_STRING,command_id,text_wbuf);
			}

			CheckMenuItem(hmenu,command_id,config_toolbar_icon_only ? MF_CHECKED : MF_UNCHECKED);
		}

		_viv_show_cursor();

		TrackPopupMenu(hmenu,TPM_RIGHTBUTTON,screen_x,screen_y,0,_viv_hwnd,0);

		_viv_update_show_cursor();

		DestroyMenu(hmenu);
	}
}

void _viv_toolbar_context_command(int command_id)
{
	int index;

	index = command_id - _VIV_TOOLBAR_CONTEXT_ID_FIRST;

	if (index == 0)
	{
		// the title row is grayed; it never arrives, the gate is belt.
		return;
	}

	if (index == 7)
	{
		// show all: the mask returns to its default.
		config_toolbar_groups = 0x3f;
	}
	else
	if ((index >= 2) && (index <= _VIV_TOOLBAR_GROUP_MAX + 1))
	{
		config_toolbar_groups ^= (1 << (index - 1));
	}
	else
	if (index == 8)
	{
		// icon-only: the toggle re-measures with the labels gone (the
		// overflow walk below sees the shorter strip).
		config_toolbar_icon_only = config_toolbar_icon_only ? 0 : 1;
	}

	// relayout and repaint: the strip re-measures with the new mask
	// (the ini write rides the exit path like every menu toggle).
	_viv_toolbar_measure();

	if (_viv_toolbar_hwnd)
	{
		InvalidateRect(_viv_toolbar_hwnd,0,FALSE);
	}
}

void _viv_toolbar_create(HWND parent)
{
	RECT rect;
	
	if (_viv_toolbar_hwnd)
	{
		return;
	}
	
	// no class brush: the strip face is painted by the erase and the paint
	// handlers above, a draw that bypasses them must not erase white (the
	// 1.1.03 lesson).
	os_RegisterClassEx(
		CS_DBLCLKS,
		_viv_toolbar_proc,
		0,
		LoadCursor(NULL,IDC_ARROW),
		NULL,
		"_VIV_TOOLBAR",
		0);
	
	_viv_toolbar_hwnd = os_CreateWindowEx(
		0,
		"_VIV_TOOLBAR",
		"",
		WS_CLIPCHILDREN | WS_CLIPSIBLINGS | WS_CHILD | WS_TABSTOP,
		0,0,0,0,
		parent,(HMENU)VIV_ID_TOOLBAR,os_hinstance,NULL);
	
	if (!_viv_toolbar_hwnd)
	{
		return;
	}
	
	// the palette latch starts from the live theme so a paint that lands
	// before the first apply dark mode sweep still draws the right face.
	_viv_toolbar_set_dark(_viv_is_dark());
	
	GetClientRect(parent,&rect);
	
	_viv_toolbar_layout(rect.right - rect.left);
	
	_viv_toolbar_update_buttons();
	
	ShowWindow(_viv_toolbar_hwnd,SW_SHOW);
}

void _viv_toolbar_destroy(void)
{
	if (_viv_toolbar_hwnd)
	{
		_viv_toolbar_tooltip_destroy();
		
		DestroyWindow(_viv_toolbar_hwnd);
		
		_viv_toolbar_hwnd = 0;
		_viv_toolbar_hover = -1;
		_viv_toolbar_pressed = -1;
		_viv_toolbar_more_hover = 0;
		_viv_toolbar_more_pressed = 0;
		_viv_toolbar_focus = -1;
		_viv_toolbar_tracking = 0;
		
		// rc.13: the play latch rides the face state, not the window -
		// a rebuild re-reads it anyway, but a stale latch must never
		// survive into the next create.
		_viv_toolbar_playing = 0;
	}
}

void _viv_toolbar_layout(int wide)
{
	if (!_viv_toolbar_hwnd)
	{
		return;
	}
	
	// the one band: the menus own the left at their natural width, the
	// cluster takes the rest (a hidden menu bar answers zero and the
	// strip owns the whole band).
	_viv_toolbar_wide = wide - _viv_menubar_strip_wide();
	if (_viv_toolbar_wide < 0) _viv_toolbar_wide = 0;
	
	_viv_toolbar_measure();
	
	// the strip spans the rest of the band, anchored beside the menus at
	// the band's own y (the layout owns the x; the on-size sweep owns the
	// width the strip may walk).
	SetWindowPos(_viv_toolbar_hwnd,0,_viv_menubar_strip_wide(),0,_viv_toolbar_wide,_viv_toolbar_bar_high,SWP_NOZORDER | SWP_NOACTIVATE);
	
	InvalidateRect(_viv_toolbar_hwnd,0,FALSE);
}

void _viv_toolbar_update_buttons(void)
{
	int rw;
	int rh;
	int playing;
	int has_image;
	int changed;
	int itemi;
	int zoom_now;
	
	if (!_viv_toolbar_hwnd)
	{
		return;
	}
	
	// the play / pause face follows the slideshow and the animation clock.
	// (rc.13: the animation clock only counts when frames exist - the
	// bare preference bit sits at 1 on every static image, so the face
	// showed the pause bars on an idle photo. the ontop rule and the
	// animation timer gate the same way.)
	playing = ((_viv_is_slideshow) || ((_viv_slot_current.frame_count > 1) && (_viv_animation_play))) ? 1 : 0;
	
	// the menu bar's no image gate: no file, a missed file or a failed load
	// disables everything that acts on the image.
	// the faces answer the image on screen: during a first load the
	// requested fd names a file nothing shows yet.
	has_image = ((*_viv_slot_current.fd.cFileName) && (!_viv_file_not_found) && (!_viv_load_failed)) ? 1 : 0;
	
	_viv_get_render_size(&rw,&rh);
	
	changed = 0;
	
	// the cluster's percent is a painted value: a zoom change with no
	// enable-state movement still owes the strip a repaint (the value
	// moved, the face is the value's home).
	zoom_now = _viv_zoom_percent();

	if (zoom_now != _viv_toolbar_zoom_last)
	{
		_viv_toolbar_zoom_last = zoom_now;

		changed = 1;
	}

	for(itemi=0;itemi<_VIV_TOOLBAR_ITEM_COUNT;itemi++)
	{
		int enable;
		int command_id;
		
		command_id = _viv_toolbar_items[itemi].command_id;
		
		enable = 1;
		
		switch(command_id)
		{
			case VIV_ID_NAV_PREV:
			case VIV_ID_NAV_NEXT:
			
				// the step faces answer the same question the navigation
				// itself walks: is there anywhere to go? the old rule read
				// the jump-to dialog's nav cache, which only ever fills
				// when that dialog opens - on a plain open the count sits
				// at zero and the faces stay gray while the keys, the menu
				// and the pill keep stepping. the predicate lives in the
				// navigation domain now (the fourth audit's consistency
				// finding: the menu carried the bare has-an-image rule
				// while the faces carried this one, so a lone image in an
				// empty folder lit the menu's next and left the face gray).
				enable = _viv_nav_neighbor_available();
				break;
			
			case VIV_ID_VIEW_1TO1:
			
				// already at 1:1 (the old toolbar rule, the no image gate added).
				enable = ((has_image) && !((rw == _viv_slot_current.image_wide) && (rh == _viv_slot_current.image_high))) ? 1 : 0;
				break;
			
			case VIV_ID_VIEW_BESTFIT:

				// already at best fit (the old toolbar rule, the no image gate
				// added). the fill stretch holds the view at position zero, but
				// it is not the fit: the face stays live there, the command now
				// leaves the fill and answers (the field report's dead face).
				enable = ((has_image) && (!((_viv_zoom_pos == 0) && (!_viv_1to1)) || (_viv_is_fullscreen ? config_fullscreen_fill_window : config_fill_window))) ? 1 : 0;
				break;
			
			case VIV_ID_EDIT_ROTATE_90:
			
				enable = has_image && _viv_rotation_ready();
				break;
			
			default:
			
				// open, the slideshow toggle and the zoom pair stay live.
				enable = 1;
				break;
		}
		
		if (enable != (int)_viv_toolbar_item_enabled[itemi])
		{
			_viv_toolbar_item_enabled[itemi] = (BYTE)enable;
			
			changed = 1;
		}
	}
	
	if (playing != _viv_toolbar_playing ||
		_viv_toolbar_item_glyph[_VIV_TOOLBAR_ITEM_FULLSCREEN] != _viv_toolbar_item_glyph_id(_VIV_TOOLBAR_ITEM_FULLSCREEN))
	{
		_viv_toolbar_playing = playing;
		
		// the face swap changes the label: re-read the strings, re-measure
		// and re-run the overflow (the widths move).
		_viv_toolbar_measure();
		
		changed = 1;
	}
	
	if (changed)
	{
		InvalidateRect(_viv_toolbar_hwnd,0,FALSE);
	}
}

void _viv_toolbar_set_dark(int dark)
{
	dark = dark ? 1 : 0;
	
	if (dark != _viv_toolbar_dark)
	{
		// the colors resolve through the tokens at paint time now; the
		// latch only owns this repaint (and the glyph dark variant).
		_viv_toolbar_dark = dark;
		
		if (_viv_toolbar_hwnd)
		{
			InvalidateRect(_viv_toolbar_hwnd,0,FALSE);
		}
	}
	
	// the tooltip control may exist before the first palette flip and a
	// fresh control always starts light: tint it on every call.
	_viv_toolbar_tooltip_apply_colors();
}

int _viv_toolbar_high(void)
{
	if ((_viv_toolbar_hwnd) && (IsWindowVisible(_viv_toolbar_hwnd)))
	{
		return _viv_toolbar_bar_high;
	}
	
	return 0;
}
