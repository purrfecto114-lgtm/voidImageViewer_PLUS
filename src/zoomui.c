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
// Floating zoom controls (touch friendly) implementation.
//
// windowed mode keeps the signature two button pill (zoom out / zoom in)
// in the bottom right corner. fullscreen mode grows the bar to six
// buttons (prev / play / pause / next / zoom out / zoom in) centered at
// the bottom, drawn on a WS_EX_LAYERED child window: after two idle
// seconds the bar fades out in 15ms alpha steps and any mouse, key or
// command activity fades it back in. on windows 7, where layered child
// windows are not supported, the bar hides without the fade.

#include "viv.h"
#include "zoomui.h"

// per monitor dpi change message. (not defined in older SDKs)
#ifndef WM_DPICHANGED
#define WM_DPICHANGED 0x02E0
#endif

// layered window alpha. (not defined in older SDKs)
#ifndef LWA_ALPHA
#define LWA_ALPHA 2
#endif

// master button table: the order matches the toolbar image list order.
// the fullscreen bar uses all six entries; windowed mode uses the last
// two only (the rc.1 signature pill).
#define _ZOOMUI_BUTTON_COUNT_MAX 6
#define _ZOOMUI_WINDOWED_FIRST 4
#define _ZOOMUI_WINDOWED_COUNT 2

static const int _zoomui_command_ids[_ZOOMUI_BUTTON_COUNT_MAX] =
{
	VIV_ID_NAV_PREV,
	VIV_ID_SLIDESHOW_PLAY_ONLY,
	VIV_ID_SLIDESHOW_PAUSE_ONLY,
	VIV_ID_NAV_NEXT,
	VIV_ID_VIEW_ZOOM_OUT,
	VIV_ID_VIEW_ZOOM_IN,
};

static const localization_id_t _zoomui_tooltip_localization_ids[_ZOOMUI_BUTTON_COUNT_MAX] =
{
	LOCALIZATION_ID_ZOOMUI_TOOLTIP_PREV,
	LOCALIZATION_ID_ZOOMUI_TOOLTIP_PLAY,
	LOCALIZATION_ID_ZOOMUI_TOOLTIP_PAUSE,
	LOCALIZATION_ID_ZOOMUI_TOOLTIP_NEXT,
	LOCALIZATION_ID_ZOOMUI_TOOLTIP_ZOOM_OUT,
	LOCALIZATION_ID_ZOOMUI_TOOLTIP_ZOOM_IN,
};

// vector glyphs shared with the toolbar (drawn at any size, both themes).
static const int _zoomui_glyph_ids[_ZOOMUI_BUTTON_COUNT_MAX] =
{
	GLYPH_PREV,
	GLYPH_PLAY,
	GLYPH_PAUSE,
	GLYPH_NEXT,
	GLYPH_ZOOMOUT,
	GLYPH_ZOOMIN,
};

// fade parameters.
#define _ZOOMUI_TIMER_ID 1
#define _ZOOMUI_FADE_INTERVAL 15     // ms per alpha step.
#define _ZOOMUI_ALPHA_STEP 17        // ~240ms for a full 0..255 fade.
#define _ZOOMUI_ALPHA_OPAQUE 255
#define _ZOOMUI_IDLE_MS 2000         // idle before the fade out starts.

static HWND _zoomui_hwnd = 0;
static HWND _zoomui_parent_hwnd = 0;
static HWND _zoomui_tooltip_hwnd = 0;
static HWND _zoomui_button_hwnds[_ZOOMUI_BUTTON_COUNT_MAX];

static int _zoomui_button_count = _ZOOMUI_WINDOWED_COUNT; // active buttons.
static int _zoomui_button_first = _ZOOMUI_WINDOWED_FIRST; // first master table entry in use.

static int _zoomui_button_wide = 0;
static int _zoomui_button_high = 0;
static int _zoomui_margin = 0;
static int _zoomui_is_registered = 0;
static int _zoomui_hot_index = -1; // active button under the cursor, or -1.
static int _zoomui_dark = 0; // 1 = draw with the dark mode palette.
static int _zoomui_is_fullscreen = 0; // 1 = six button fullscreen bar.

static int _zoomui_layered_ok = 0; // WS_EX_LAYERED child support (win8+).
static int _zoomui_alpha = _ZOOMUI_ALPHA_OPAQUE; // current alpha value.
static int _zoomui_alpha_target = _ZOOMUI_ALPHA_OPAQUE;
static DWORD _zoomui_last_activity = 0; // GetTickCount of the last user input.
static int _zoomui_visible_wanted = 0; // the state zoomui_show() latched.

static void _zoomui_apply_tooltip_colors(void);

static void _zoomui_draw_button(HDC hdc,const RECT *rect,int buttoni,int is_selected,int is_disabled,int is_hot);
static void _zoomui_draw_icon(HDC hdc,const RECT *rect,int buttoni,int offset);
static LRESULT CALLBACK _zoomui_proc(HWND hwnd,UINT msg,WPARAM wParam,LPARAM lParam);
static void _zoomui_invalidate_button(int buttoni);
static LRESULT CALLBACK _zoomui_button_subclass_proc(HWND hwnd,UINT msg,WPARAM wParam,LPARAM lParam,UINT_PTR uSubclass,DWORD_PTR dwRefData);
static void _zoomui_create_buttons(void);

// the auto hide only applies to the fullscreen bar: the windowed pill is
// the signature control and stays put.
static int _zoomui_autohide_enabled(void)
{
	return ((config_zoom_auto_hide) && (_zoomui_is_fullscreen)) ? 1 : 0;
}

static void _zoomui_ensure_timer(void)
{
	if (_zoomui_hwnd)
	{
		SetTimer(_zoomui_hwnd,_ZOOMUI_TIMER_ID,_ZOOMUI_FADE_INTERVAL,0);
	}
}

static void _zoomui_kill_timer(void)
{
	if (_zoomui_hwnd)
	{
		KillTimer(_zoomui_hwnd,_ZOOMUI_TIMER_ID);
	}
}

// push the current alpha to the layered window. ignored when the
// layered child support is missing (the bar is simply opaque then).
static void _zoomui_set_alpha(int alpha)
{
	if ((_zoomui_hwnd) && (_zoomui_layered_ok))
	{
		SetLayeredWindowAttributes(_zoomui_hwnd,0,(BYTE)alpha,LWA_ALPHA);
	}
}

static void _zoomui_calc_metrics(void)
{
	// touch friendly sizing: 48x44 logical units per button.
	_zoomui_button_wide = (48 * os_logical_wide) / 96;
	_zoomui_button_high = (44 * os_logical_high) / 96;
	_zoomui_margin = (6 * os_logical_high) / 96;

	if (_zoomui_button_wide < 32)
	{
		_zoomui_button_wide = 32;
	}

	if (_zoomui_button_high < 28)
	{
		_zoomui_button_high = 28;
	}
}

static void _zoomui_layout_buttons(void)
{
	int i;

	if (!_zoomui_hwnd)
	{
		return;
	}

	for(i=0;i<_zoomui_button_count;i++)
	{
		if (_zoomui_button_hwnds[i])
		{
			SetWindowPos(_zoomui_button_hwnds[i],0,_zoomui_margin + (i * _zoomui_button_wide),_zoomui_margin,_zoomui_button_wide,_zoomui_button_high,SWP_NOZORDER|SWP_NOACTIVATE);
		}
	}
}

// destroy and recreate the button windows for the current mode, together
// with the tooltip control. called from zoomui_init and every fullscreen
// mode switch.
static void _zoomui_create_buttons(void)
{
	int i;

	if (!_zoomui_hwnd)
	{
		return;
	}

	// remove the old buttons and their hover subclasses.
	for(i=0;i<_ZOOMUI_BUTTON_COUNT_MAX;i++)
	{
		if (_zoomui_button_hwnds[i])
		{
			RemoveWindowSubclass(_zoomui_button_hwnds[i],_zoomui_button_subclass_proc,1);

			DestroyWindow(_zoomui_button_hwnds[i]);

			_zoomui_button_hwnds[i] = 0;
		}
	}

	_zoomui_hot_index = -1;

	// the buttons send the real commands: no state has to be mirrored.
	for(i=0;i<_zoomui_button_count;i++)
	{
		_zoomui_button_hwnds[i] = os_CreateWindowEx(
			0,
			"BUTTON",
			"",
			WS_VISIBLE | WS_CHILD | BS_OWNERDRAW | BS_PUSHBUTTON | WS_TABSTOP,
			0,0,0,0,
			_zoomui_hwnd,
			(HMENU)(UINT_PTR)_zoomui_command_ids[_zoomui_button_first + i],
			os_hinstance,
			NULL);
	}

	_zoomui_layout_buttons();

	// install hover tracking on each button.
	for(i=0;i<_zoomui_button_count;i++)
	{
		if (_zoomui_button_hwnds[i])
		{
			SetWindowSubclass(_zoomui_button_hwnds[i],_zoomui_button_subclass_proc,1,(DWORD_PTR)i);
		}
	}

	// tooltips.
	if (_zoomui_tooltip_hwnd)
	{
		DestroyWindow(_zoomui_tooltip_hwnd);

		_zoomui_tooltip_hwnd = 0;
	}

	_zoomui_tooltip_hwnd = os_CreateWindowEx(
		WS_EX_TOPMOST,
		TOOLTIPS_CLASSA,
		"",
		WS_POPUP | TTS_NOPREFIX | TTS_ALWAYSTIP,
		CW_USEDEFAULT,CW_USEDEFAULT,CW_USEDEFAULT,CW_USEDEFAULT,
		_zoomui_hwnd,
		0,
		os_hinstance,
		NULL);

	if (_zoomui_tooltip_hwnd)
	{
		for(i=0;i<_zoomui_button_count;i++)
		{
			TOOLINFOW ti;
			wchar_t wbuf[STRING_SIZE];

			os_zero_memory(&ti,sizeof(ti));

			ti.cbSize = sizeof(ti);
			ti.uFlags = TTF_IDISHWND | TTF_SUBCLASS;
			ti.hwnd = _zoomui_hwnd;
			ti.uId = (UINT_PTR)_zoomui_button_hwnds[i];
			ti.hinst = os_hinstance;
			string_copy_utf8_string(wbuf,localization_get_string(_zoomui_tooltip_localization_ids[_zoomui_button_first + i]));
			ti.lpszText = wbuf;

			SendMessage(_zoomui_tooltip_hwnd,TTM_ADDTOOLW,0,(LPARAM)&ti);
		}

		SendMessage(_zoomui_tooltip_hwnd,TTM_ACTIVATE,_zoomui_visible_wanted ? TRUE : FALSE,0);

		// match the tooltip colors to the current palette.
		_zoomui_apply_tooltip_colors();
	}
}

void zoomui_init(HWND parent)
{
	_zoomui_parent_hwnd = parent;

	_zoomui_calc_metrics();

	if (!_zoomui_is_registered)
	{
		os_RegisterClassEx(
			CS_DBLCLKS,
			_zoomui_proc,
			0,
			LoadCursor(NULL,IDC_ARROW),
			0,
			"_VIV_ZOOMUI",
			0);

		_zoomui_is_registered = 1;
	}

	if (!_zoomui_hwnd)
	{
		_zoomui_hwnd = os_CreateWindowEx(
			WS_EX_TOOLWINDOW|WS_EX_LAYERED,
			"_VIV_ZOOMUI",
			"",
			WS_CLIPCHILDREN | WS_CLIPSIBLINGS | WS_CHILD,
			0,0,0,0,
			parent,(HMENU)VIV_ID_ZOOMUI,os_hinstance,NULL);

		if (_zoomui_hwnd)
		{
			// probe the layered child window support (windows 8+): if
			// alpha blending is refused the style is removed and the
			// bar hides without the fade.
			if (SetLayeredWindowAttributes(_zoomui_hwnd,0,_ZOOMUI_ALPHA_OPAQUE,LWA_ALPHA))
			{
				_zoomui_layered_ok = 1;
			}
			else
			{
				SetWindowLong(_zoomui_hwnd,GWL_EXSTYLE,GetWindowLong(_zoomui_hwnd,GWL_EXSTYLE) & ~WS_EX_LAYERED);

				_zoomui_layered_ok = 0;
			}
		}

		_zoomui_create_buttons();
	}
}

void zoomui_kill(void)
{
	_zoomui_kill_timer();

	if (_zoomui_tooltip_hwnd)
	{
		DestroyWindow(_zoomui_tooltip_hwnd);

		_zoomui_tooltip_hwnd = 0;
	}

	if (_zoomui_hwnd)
	{
		// remove the hover subclasses before the buttons are destroyed.
		{
			int i;

			for(i=0;i<_ZOOMUI_BUTTON_COUNT_MAX;i++)
			{
				if (_zoomui_button_hwnds[i])
				{
					RemoveWindowSubclass(_zoomui_button_hwnds[i],_zoomui_button_subclass_proc,1);
				}
			}
		}

		_zoomui_hot_index = -1;

		DestroyWindow(_zoomui_hwnd);

		_zoomui_hwnd = 0;
	}

	os_zero_memory(_zoomui_button_hwnds,sizeof(_zoomui_button_hwnds));

	_zoomui_layered_ok = 0;
	_zoomui_alpha = _ZOOMUI_ALPHA_OPAQUE;
	_zoomui_alpha_target = _ZOOMUI_ALPHA_OPAQUE;
	_zoomui_visible_wanted = 0;

	_zoomui_parent_hwnd = 0;
}

// tint the tooltip control with the palette: comctl tooltips have no dark
// theme of their own, the colors are set by message.
static void _zoomui_apply_tooltip_colors(void)
{
    if (_zoomui_tooltip_hwnd)
    {
        if (_zoomui_dark)
        {
            SendMessage(_zoomui_tooltip_hwnd,TTM_SETTIPBKCOLOR,RGB(0x20,0x20,0x20),0);
            SendMessage(_zoomui_tooltip_hwnd,TTM_SETTIPTEXTCOLOR,RGB(0xE8,0xE8,0xE8),0);
        }
        else
        {
            SendMessage(_zoomui_tooltip_hwnd,TTM_SETTIPBKCOLOR,GetSysColor(COLOR_INFOBK),0);
            SendMessage(_zoomui_tooltip_hwnd,TTM_SETTIPTEXTCOLOR,GetSysColor(COLOR_INFOTEXT),0);
        }
    }
}

// switch the palette between light and dark. called after creating and
// whenever the app dark mode or the windows theme changes.
void zoomui_set_dark(int dark)
{
    if (_zoomui_dark != (dark ? 1 : 0))
    {
        _zoomui_dark = dark ? 1 : 0;

        // the glyph colors are baked into the cached icons.
        glyphs_flush_cache();

        if (_zoomui_hwnd)
        {
            InvalidateRect(_zoomui_hwnd,0,FALSE);
        }
    }

    // the tooltip control may exist before the first palette flip and a
    // fresh control always starts light: tint it on every call.
    _zoomui_apply_tooltip_colors();
}

int zoomui_is_created(void)
{
	return _zoomui_hwnd ? 1 : 0;
}

void zoomui_localize(void)
{
	// refresh the tooltip texts after the language has changed.

	if (_zoomui_tooltip_hwnd)
	{
		int i;

		for(i=0;i<_zoomui_button_count;i++)
		{
			TOOLINFOW ti;
			wchar_t wbuf[STRING_SIZE];

			os_zero_memory(&ti,sizeof(ti));

			ti.cbSize = sizeof(ti);
			ti.uFlags = TTF_IDISHWND;
			ti.hwnd = _zoomui_hwnd;
			ti.uId = (UINT_PTR)_zoomui_button_hwnds[i];
			ti.hinst = os_hinstance;
			string_copy_utf8_string(wbuf,localization_get_string(_zoomui_tooltip_localization_ids[_zoomui_button_first + i]));
			ti.lpszText = wbuf;

			SendMessage(_zoomui_tooltip_hwnd,TTM_UPDATETIPTEXTW,0,(LPARAM)&ti);
		}
	}
}

// switch between the windowed pill (two buttons) and the fullscreen
// overlay bar (six buttons). viv.c repositions the container right
// after this through the regular on_size layout.
void zoomui_set_fullscreen(int fullscreen)
{
	if (_zoomui_is_fullscreen != (fullscreen ? 1 : 0))
	{
		_zoomui_is_fullscreen = fullscreen ? 1 : 0;

		if (_zoomui_is_fullscreen)
		{
			_zoomui_button_count = _ZOOMUI_BUTTON_COUNT_MAX;
			_zoomui_button_first = 0;
		}
		else
		{
			_zoomui_button_count = _ZOOMUI_WINDOWED_COUNT;
			_zoomui_button_first = _ZOOMUI_WINDOWED_FIRST;
		}

		if (_zoomui_hwnd)
		{
			_zoomui_create_buttons();

			// a mode switch is activity: restart the idle clock.
			_zoomui_last_activity = GetTickCount();

			if ((IsWindowVisible(_zoomui_hwnd)) && (_zoomui_autohide_enabled()))
			{
				_zoomui_ensure_timer();
			}
		}
	}
}

// user input arrived (mouse motion, key press or a command): keep the
// overlay awake, and show it again with a fade in when it was hidden.
void zoomui_activity(void)
{
	if ((!_zoomui_hwnd) || (!_zoomui_visible_wanted))
	{
		return;
	}

	_zoomui_last_activity = GetTickCount();

	if (!_zoomui_autohide_enabled())
	{
		return;
	}

	_zoomui_alpha_target = _ZOOMUI_ALPHA_OPAQUE;

	if (!IsWindowVisible(_zoomui_hwnd))
	{
		_zoomui_alpha = 0;

		ShowWindow(_zoomui_hwnd,SW_SHOW);

		_zoomui_set_alpha(0);
	}

	_zoomui_ensure_timer();
}

void zoomui_show(int show)
{
	_zoomui_visible_wanted = show ? 1 : 0;

	if (!_zoomui_hwnd)
	{
		return;
	}

	if (_zoomui_visible_wanted)
	{
		if (!IsWindowVisible(_zoomui_hwnd))
		{
			// no WM_MOUSELEAVE arrives when showing under the cursor.
			_zoomui_hot_index = -1;

			// the fade in starts from fully transparent (or is skipped
			// when the layered child support is missing).
			_zoomui_alpha = _zoomui_layered_ok ? 0 : _ZOOMUI_ALPHA_OPAQUE;
			_zoomui_alpha_target = _ZOOMUI_ALPHA_OPAQUE;
			_zoomui_last_activity = GetTickCount();

			ShowWindow(_zoomui_hwnd,SW_SHOW);

			_zoomui_set_alpha(_zoomui_alpha);
		}
		else
		{
			// already visible: cancel any in flight fade out.
			_zoomui_alpha_target = _ZOOMUI_ALPHA_OPAQUE;
		}

		if (_zoomui_autohide_enabled())
		{
			_zoomui_ensure_timer();
		}

		if (_zoomui_tooltip_hwnd)
		{
			SendMessage(_zoomui_tooltip_hwnd,TTM_ACTIVATE,TRUE,0);
		}
	}
	else
	{
		// no WM_MOUSELEAVE arrives when hiding under the cursor.
		_zoomui_hot_index = -1;

		_zoomui_kill_timer();
		_zoomui_alpha = 0;
		_zoomui_alpha_target = 0;

		ShowWindow(_zoomui_hwnd,SW_HIDE);

		if (_zoomui_tooltip_hwnd)
		{
			SendMessage(_zoomui_tooltip_hwnd,TTM_ACTIVATE,FALSE,0);
		}
	}
}

// the fade / idle tick.
static void _zoomui_on_timer(void)
{
	DWORD now;
	int idle;

	if (!_zoomui_hwnd)
	{
		_zoomui_kill_timer();

		return;
	}

	if (!_zoomui_autohide_enabled())
	{
		// windowed pill or auto hide disabled: stay opaque.
		if (_zoomui_alpha != _ZOOMUI_ALPHA_OPAQUE)
		{
			_zoomui_alpha = _ZOOMUI_ALPHA_OPAQUE;

			_zoomui_set_alpha(_zoomui_alpha);
		}

		_zoomui_kill_timer();

		return;
	}

	now = GetTickCount();
	idle = (now - _zoomui_last_activity) >= _ZOOMUI_IDLE_MS;

	if (idle)
	{
		_zoomui_alpha_target = 0;
	}

	if (!_zoomui_layered_ok)
	{
		// windows 7: no layered child windows, hide without a fade.
		if (idle)
		{
			_zoomui_hot_index = -1;

			ShowWindow(_zoomui_hwnd,SW_HIDE);

			_zoomui_kill_timer();
		}

		return;
	}

	if (_zoomui_alpha != _zoomui_alpha_target)
	{
		if (_zoomui_alpha > _zoomui_alpha_target)
		{
			_zoomui_alpha -= _ZOOMUI_ALPHA_STEP;

			if (_zoomui_alpha < _zoomui_alpha_target)
			{
				_zoomui_alpha = _zoomui_alpha_target;
			}
		}
		else
		{
			_zoomui_alpha += _ZOOMUI_ALPHA_STEP;

			if (_zoomui_alpha > _zoomui_alpha_target)
			{
				_zoomui_alpha = _zoomui_alpha_target;
			}
		}

		_zoomui_set_alpha(_zoomui_alpha);

		// a fully transparent layered window still eats mouse clicks:
		// hide it for real once the fade has finished.
		if (_zoomui_alpha == 0)
		{
			_zoomui_hot_index = -1;

			ShowWindow(_zoomui_hwnd,SW_HIDE);

			_zoomui_kill_timer();

			return;
		}
	}
}

// wide,high = the image area of the parent client.
// (status bar and toolbar space already excluded)
void zoomui_layout(int wide,int high)
{
	int container_wide;
	int container_high;
	int x;
	int y;

	if (!_zoomui_hwnd)
	{
		return;
	}

	_zoomui_calc_metrics();

	container_wide = (_zoomui_button_count * _zoomui_button_wide) + (_zoomui_margin * 2);
	container_high = _zoomui_button_high + (_zoomui_margin * 2);

	if (_zoomui_is_fullscreen)
	{
		// the fullscreen overlay bar sits centered at the bottom.
		x = (wide - container_wide) / 2;
	}
	else
	{
		x = wide - container_wide - _zoomui_margin;
	}

	y = high - container_high - _zoomui_margin;

	if (x < _zoomui_margin)
	{
		x = _zoomui_margin;
	}

	if (y < _zoomui_margin)
	{
		y = _zoomui_margin;
	}

	SetWindowPos(_zoomui_hwnd,HWND_TOP,x,y,container_wide,container_high,SWP_NOACTIVATE);

	_zoomui_layout_buttons();
}

static void _zoomui_draw_button(HDC hdc,const RECT *rect,int buttoni,int is_selected,int is_disabled,int is_hot)
{
	RECT fill_rect;
	HBRUSH brush;
	HPEN pen;
	HPEN old_pen;
	HGDIOBJ old_brush;
	int offset;

	offset = 0;

	if (is_selected)
	{
		offset = 1;
	}

	CopyRect(&fill_rect,rect);

	// hovered or pressed: highlighted fill, otherwise the bar color.
	{
		COLORREF fill_color;

		if (is_selected || is_hot)
		{
			fill_color = _zoomui_dark ? RGB(0x38,0x38,0x38) : GetSysColor(COLOR_3DLIGHT);
		}
		else
		{
			fill_color = _zoomui_dark ? RGB(0x25,0x25,0x25) : GetSysColor(COLOR_BTNFACE);
		}

		brush = CreateSolidBrush(fill_color);

		FillRect(hdc,&fill_rect,brush);

		DeleteObject(brush);
	}

	// border.
	pen = CreatePen(PS_SOLID,1,_zoomui_dark ? (is_selected ? RGB(0x80,0x80,0x80) : RGB(0x45,0x45,0x45)) : GetSysColor(is_selected ? COLOR_3DDKSHADOW : COLOR_3DSHADOW));
	old_pen = SelectObject(hdc,pen);

	old_brush = SelectObject(hdc,GetStockObject(NULL_BRUSH));

	Rectangle(hdc,fill_rect.left,fill_rect.top,fill_rect.right,fill_rect.bottom);

	SelectObject(hdc,old_brush);
	SelectObject(hdc,old_pen);
	DeleteObject(pen);

	if (is_disabled)
	{
		SetTextColor(hdc,_zoomui_dark ? RGB(0x90,0x90,0x90) : GetSysColor(COLOR_3DSHADOW));
	}
	else
	{
		SetTextColor(hdc,_zoomui_dark ? RGB(0xE8,0xE8,0xE8) : GetSysColor(COLOR_BTNTEXT));
	}

	SetBkMode(hdc,TRANSPARENT);

	// the vector glyphs make the buttons unmistakable.
	_zoomui_draw_icon(hdc,rect,buttoni,offset);
}

// draw the glyph icon, centered in the button.
static void _zoomui_draw_icon(HDC hdc,const RECT *rect,int buttoni,int offset)
{
	int wide;
	int high;
	int size;
	HICON icon;

	wide = rect->right - rect->left;
	high = rect->bottom - rect->top;

	size = (wide < high) ? wide : high;

	// padding so the glyph does not touch the button border.
	size -= size / 6;

	if (size < 8)
	{
		size = 8;
	}

	icon = glyphs_icon(_zoomui_glyph_ids[_zoomui_button_first + buttoni],_zoomui_dark,size);

	if (icon)
	{
		DrawIconEx(hdc,((wide - size) / 2) + offset,((high - size) / 2) + offset,icon,size,size,0,NULL,DI_NORMAL);
	}
}

// invalidate a single button. keeps hover repaints cheap.
static void _zoomui_invalidate_button(int buttoni)
{
	if ((buttoni >= 0) && (buttoni < _zoomui_button_count))
	{
		if (_zoomui_button_hwnds[buttoni])
		{
			InvalidateRect(_zoomui_button_hwnds[buttoni],0,0);
		}
	}
}

// per button subclass: tracks the hovered button to draw a highlight.
static LRESULT CALLBACK _zoomui_button_subclass_proc(HWND hwnd,UINT msg,WPARAM wParam,LPARAM lParam,UINT_PTR uSubclass,DWORD_PTR dwRefData)
{
	// uSubclass is part of the SetWindowSubclass signature, we use dwRefData instead.
	(void)uSubclass;

	switch (msg)
	{
		case WM_MOUSEMOVE:
		{
			int buttoni;

			buttoni = (int)dwRefData;

			if (buttoni != _zoomui_hot_index)
			{
				_zoomui_invalidate_button(_zoomui_hot_index);

				_zoomui_hot_index = buttoni;

				_zoomui_invalidate_button(buttoni);
			}

			// hovering the bar counts as activity (idle fade timer).
			zoomui_activity();

			// request a WM_MOUSELEAVE when the cursor leaves this button.
			{
				TRACKMOUSEEVENT tme;

				os_zero_memory(&tme,sizeof(tme));

				tme.cbSize = sizeof(tme);
				tme.dwFlags = TME_LEAVE;
				tme.hwndTrack = hwnd;

				TrackMouseEvent(&tme);
			}

			break;
		}

		case WM_MOUSELEAVE:
		{
			int buttoni;

			buttoni = (int)dwRefData;

			if (_zoomui_hot_index == buttoni)
			{
				_zoomui_hot_index = -1;

				_zoomui_invalidate_button(buttoni);
			}

			break;
		}
	}

	return DefSubclassProc(hwnd,msg,wParam,lParam);
}

static LRESULT CALLBACK _zoomui_proc(HWND hwnd,UINT msg,WPARAM wParam,LPARAM lParam)
{
	switch (msg)
	{
		case WM_COMMAND:
		{
			// forward button commands to the main window.
			if (_zoomui_parent_hwnd)
			{
				SendMessage(_zoomui_parent_hwnd,WM_COMMAND,wParam,lParam);

				// return focus to the viewer so keyboard shortcuts keep working.
				SetFocus(_zoomui_parent_hwnd);
			}

			return 0;
		}

		case WM_DRAWITEM:
		{
			DRAWITEMSTRUCT *dis;

			dis = (DRAWITEMSTRUCT *)lParam;

			if (dis->CtlType == ODT_BUTTON)
			{
				int buttoni;
				int i;

				buttoni = -1;

				for(i=0;i<_zoomui_button_count;i++)
				{
					if (_zoomui_command_ids[_zoomui_button_first + i] == (int)dis->CtlID)
					{
						buttoni = i;

						break;
					}
				}

				if (buttoni != -1)
				{
					_zoomui_draw_button(dis->hDC,&dis->rcItem,buttoni,(dis->itemState & ODS_SELECTED) ? 1 : 0,(dis->itemState & ODS_DISABLED) ? 1 : 0,(buttoni == _zoomui_hot_index) ? 1 : 0);

					return TRUE;
				}
			}

			break;
		}

		case WM_DPICHANGED:
		{
			// the main window handler updates os_logical_* and rebuilds
			// the icon caches; refresh the button metrics for the new
			// scale so this window is correct whenever it paints next.
			_zoomui_calc_metrics();

			_zoomui_layout_buttons();

			return 0;
		}

		case WM_TIMER:
		{
			if ((int)wParam == _ZOOMUI_TIMER_ID)
			{
				_zoomui_on_timer();

				return 0;
			}

			break;
		}

		case WM_ERASEBKGND:
		{
			HDC hdc;
			RECT rect;

			hdc = (HDC)wParam;

			GetClientRect(hwnd,&rect);

			{
				HBRUSH background_brush;

				background_brush = CreateSolidBrush(_zoomui_dark ? RGB(0x20,0x20,0x20) : GetSysColor(COLOR_BTNFACE));

				FillRect(hdc,&rect,background_brush);

				DeleteObject(background_brush);
			}

			// raised border.
			{
				HPEN pen;
				HPEN old_pen;

				pen = CreatePen(PS_SOLID,1,_zoomui_dark ? RGB(0x45,0x45,0x45) : GetSysColor(COLOR_3DSHADOW));
				old_pen = SelectObject(hdc,pen);

				MoveToEx(hdc,rect.left,rect.bottom - 1,NULL);
				LineTo(hdc,rect.left,rect.top);
				LineTo(hdc,rect.right - 1,rect.top);

				SelectObject(hdc,old_pen);
				DeleteObject(pen);

				pen = CreatePen(PS_SOLID,1,_zoomui_dark ? RGB(0x70,0x70,0x70) : GetSysColor(COLOR_3DHIGHLIGHT));
				old_pen = SelectObject(hdc,pen);

				MoveToEx(hdc,rect.left + 1,rect.bottom - 1,NULL);
				LineTo(hdc,rect.right - 1,rect.bottom - 1);
				LineTo(hdc,rect.right - 1,rect.top + 1);

				SelectObject(hdc,old_pen);
				DeleteObject(pen);
			}

			return 1;
		}

		case WM_PAINT:
		{
			PAINTSTRUCT ps;

			BeginPaint(hwnd,&ps);

			EndPaint(hwnd,&ps);

			return 0;
		}
	}

	return DefWindowProc(hwnd,msg,wParam,lParam);
}

