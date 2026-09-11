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
//
// single window self drawn pill (no embedded BUTTON child windows): one
// WS_CHILD window paints the stadium tray and every capsule button in
// WM_PAINT, tracks hover / press with capture + hit testing, fires the
// real commands with WM_COMMAND, hosts one rect based tooltip, and
// answers HTTRANSPARENT outside the buttons so the rounded corners and
// the tray gaps never eat clicks. no Mica, plain GDI only.

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

#ifndef BN_CLICKED
#define BN_CLICKED 0
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

static int _zoomui_button_count = _ZOOMUI_WINDOWED_COUNT; // active buttons.
static int _zoomui_button_first = _ZOOMUI_WINDOWED_FIRST; // first master table entry in use.

static int _zoomui_button_wide = 0;
static int _zoomui_button_high = 0;
static int _zoomui_button_gap = 0; // spacing between the capsule buttons.
static int _zoomui_margin = 0;
static RECT _zoomui_button_rects[_ZOOMUI_BUTTON_COUNT_MAX]; // client coords, 0..count-1 valid.
static int _zoomui_is_registered = 0;
static int _zoomui_hot_index = -1; // button under the cursor, or -1.
static int _zoomui_pressed_index = -1; // button pressed with capture, or -1.
static int _zoomui_dark = 0; // 1 = draw with the dark mode palette.
static int _zoomui_is_fullscreen = 0; // 1 = six button fullscreen bar.

static int _zoomui_layered_ok = 0; // WS_EX_LAYERED child support (win8+).
static int _zoomui_alpha = _ZOOMUI_ALPHA_OPAQUE; // current alpha value.
static int _zoomui_alpha_target = _ZOOMUI_ALPHA_OPAQUE;
static DWORD _zoomui_last_activity = 0; // GetTickCount of the last user input.
static int _zoomui_visible_wanted = 0; // the state zoomui_show() latched.

static void _zoomui_apply_tooltip_colors(void);

static void _zoomui_draw_button(HDC hdc,const RECT *rect,int buttoni,int is_pressed,int is_disabled,int is_hot,int has_focus);
static void _zoomui_draw_icon(HDC hdc,const RECT *rect,int buttoni,int offset,int is_disabled);
static LRESULT CALLBACK _zoomui_proc(HWND hwnd,UINT msg,WPARAM wParam,LPARAM lParam);
static void _zoomui_invalidate(void);
static int _zoomui_hit_test(int x,int y);
static int _zoomui_is_button_disabled(int buttoni);
static void _zoomui_fire_button(int buttoni);
static void _zoomui_track_leave(HWND hwnd);
static void _zoomui_update_button_rects(void);
static void _zoomui_tooltip_create(void);
static void _zoomui_tooltip_destroy(void);
static void _zoomui_tooltip_rebuild(void);
static void _zoomui_tooltip_update_rects(void);
static void _zoomui_tooltip_update_texts(void);
static void _zoomui_clear_hover_press(int invalidate);

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
	// breathing room between the capsule buttons: the old zero gap layout
	// packed the two borders back to back in the middle, which read as a
	// double line and crushed the two glyphs into one control.
	_zoomui_button_gap = (4 * os_logical_high) / 96;

	if (_zoomui_button_wide < 32)
	{
		_zoomui_button_wide = 32;
	}

	if (_zoomui_button_high < 28)
	{
		_zoomui_button_high = 28;
	}

	if (_zoomui_button_gap < 2)
	{
		_zoomui_button_gap = 2;
	}
}

// recompute the per button client rects from the current metrics and
// mode. the container client size is:
//   wide = count*button_wide + (count-1)*gap + 2*margin
//   high = button_high + 2*margin
static void _zoomui_update_button_rects(void)
{
	int i;

	for(i=0;i<_ZOOMUI_BUTTON_COUNT_MAX;i++)
	{
		_zoomui_button_rects[i].left = 0;
		_zoomui_button_rects[i].top = 0;
		_zoomui_button_rects[i].right = 0;
		_zoomui_button_rects[i].bottom = 0;
	}

	for(i=0;i<_zoomui_button_count;i++)
	{
		int left;

		left = _zoomui_margin + (i * (_zoomui_button_wide + _zoomui_button_gap));

		_zoomui_button_rects[i].left = left;
		_zoomui_button_rects[i].top = _zoomui_margin;
		_zoomui_button_rects[i].right = left + _zoomui_button_wide;
		_zoomui_button_rects[i].bottom = _zoomui_margin + _zoomui_button_high;
	}
}

// hit test a client point against the live button rects.
static int _zoomui_hit_test(int x,int y)
{
	int i;
	POINT pt;

	pt.x = x;
	pt.y = y;

	for(i=0;i<_zoomui_button_count;i++)
	{
		if (PtInRect(&_zoomui_button_rects[i],pt))
		{
			return i;
		}
	}

	return -1;
}

// per button enabled state. the pill mirrors the container: disabling
// the zoomui window dims every button and blocks their commands while
// keeping the same hit area so clicks never fall through to the image
// by accident. (the viewer never disables single pill buttons today;
// the path exists so a future per command gate has a stable visual.)
static int _zoomui_is_button_disabled(int buttoni)
{
	(void)buttoni;

	if (_zoomui_hwnd)
	{
		if (!IsWindowEnabled(_zoomui_hwnd))
		{
			return 1;
		}
	}

	return 0;
}

// repaint the whole pill. the window is at most a few hundred pixels
// wide: a full repaint on hover / press change is cheaper than tracking
// per button regions and avoids seam artifacts where the capsule arcs
// meet the tray.
static void _zoomui_invalidate(void)
{
	if (_zoomui_hwnd)
	{
		InvalidateRect(_zoomui_hwnd,0,FALSE);
	}
}

static void _zoomui_clear_hover_press(int invalidate)
{
	_zoomui_hot_index = -1;
	_zoomui_pressed_index = -1;

	if ((invalidate) && (_zoomui_hwnd))
	{
		InvalidateRect(_zoomui_hwnd,0,FALSE);
	}
}

static void _zoomui_track_leave(HWND hwnd)
{
	TRACKMOUSEEVENT tme;

	os_zero_memory(&tme,sizeof(tme));

	tme.cbSize = sizeof(tme);
	tme.dwFlags = TME_LEAVE;
	tme.hwndTrack = hwnd;

	TrackMouseEvent(&tme);
}

// fire a pill button: send the real command to the main window with the
// same BN_CLICKED notification a BUTTON would have sent, then return
// focus to the viewer so keyboard shortcuts keep working.
static void _zoomui_fire_button(int buttoni)
{
	int command_id;

	if ((!_zoomui_hwnd) || (!_zoomui_parent_hwnd))
	{
		return;
	}

	if ((buttoni < 0) || (buttoni >= _zoomui_button_count))
	{
		return;
	}

	if (_zoomui_is_button_disabled(buttoni))
	{
		return;
	}

	command_id = _zoomui_command_ids[_zoomui_button_first + buttoni];

	SendMessage(_zoomui_parent_hwnd,WM_COMMAND,MAKEWPARAM(command_id,BN_CLICKED),(LPARAM)_zoomui_hwnd);

	// return focus to the viewer so keyboard shortcuts keep working.
	SetFocus(_zoomui_parent_hwnd);
}

static void _zoomui_tooltip_destroy(void)
{
	if (_zoomui_tooltip_hwnd)
	{
		DestroyWindow(_zoomui_tooltip_hwnd);

		_zoomui_tooltip_hwnd = 0;
	}
}

// create the single rect based tooltip for the pill. one tool per live
// button (uId = button index), TTF_SUBCLASS so the tooltip relays the
// mouse messages itself.
static void _zoomui_tooltip_create(void)
{
	if ((!_zoomui_hwnd) || (_zoomui_tooltip_hwnd))
	{
		return;
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
		int i;

		for(i=0;i<_zoomui_button_count;i++)
		{
			TOOLINFOW ti;
			wchar_t wbuf[STRING_SIZE];

			os_zero_memory(&ti,sizeof(ti));

			ti.cbSize = sizeof(ti);
			ti.uFlags = TTF_SUBCLASS;
			ti.hwnd = _zoomui_hwnd;
			ti.uId = (UINT_PTR)i;
			ti.rect = _zoomui_button_rects[i];
			ti.hinst = 0;
			string_copy_utf8_string(wbuf,localization_get_string(_zoomui_tooltip_localization_ids[_zoomui_button_first + i]));
			ti.lpszText = wbuf;

			SendMessage(_zoomui_tooltip_hwnd,TTM_ADDTOOLW,0,(LPARAM)&ti);
		}

		SendMessage(_zoomui_tooltip_hwnd,TTM_ACTIVATE,_zoomui_visible_wanted ? TRUE : FALSE,0);

		// match the tooltip colors to the current palette.
		_zoomui_apply_tooltip_colors();
	}
}

// rebuild the tooltip tools after a mode switch (the tool count
// changed). deletes every possible id, then adds the live ones.
static void _zoomui_tooltip_rebuild(void)
{
	int i;

	if (!_zoomui_tooltip_hwnd)
	{
		_zoomui_tooltip_create();

		return;
	}

	for(i=0;i<_ZOOMUI_BUTTON_COUNT_MAX;i++)
	{
		TOOLINFOW ti;

		os_zero_memory(&ti,sizeof(ti));

		ti.cbSize = sizeof(ti);
		ti.hwnd = _zoomui_hwnd;
		ti.uId = (UINT_PTR)i;

		SendMessage(_zoomui_tooltip_hwnd,TTM_DELTOOLW,0,(LPARAM)&ti);
	}

	for(i=0;i<_zoomui_button_count;i++)
	{
		TOOLINFOW ti;
		wchar_t wbuf[STRING_SIZE];

		os_zero_memory(&ti,sizeof(ti));

		ti.cbSize = sizeof(ti);
		ti.uFlags = TTF_SUBCLASS;
		ti.hwnd = _zoomui_hwnd;
		ti.uId = (UINT_PTR)i;
		ti.rect = _zoomui_button_rects[i];
		ti.hinst = 0;
		string_copy_utf8_string(wbuf,localization_get_string(_zoomui_tooltip_localization_ids[_zoomui_button_first + i]));
		ti.lpszText = wbuf;

		SendMessage(_zoomui_tooltip_hwnd,TTM_ADDTOOLW,0,(LPARAM)&ti);
	}
}

// refresh the tooltip rects after a layout / dpi change.
static void _zoomui_tooltip_update_rects(void)
{
	int i;

	if (!_zoomui_tooltip_hwnd)
	{
		return;
	}

	for(i=0;i<_zoomui_button_count;i++)
	{
		TOOLINFOW ti;
		wchar_t wbuf[STRING_SIZE];

		os_zero_memory(&ti,sizeof(ti));

		ti.cbSize = sizeof(ti);
		ti.uFlags = TTF_SUBCLASS;
		ti.hwnd = _zoomui_hwnd;
		ti.uId = (UINT_PTR)i;
		ti.rect = _zoomui_button_rects[i];
		ti.hinst = 0;
		string_copy_utf8_string(wbuf,localization_get_string(_zoomui_tooltip_localization_ids[_zoomui_button_first + i]));
		ti.lpszText = wbuf;

		SendMessage(_zoomui_tooltip_hwnd,TTM_SETTOOLINFOW,0,(LPARAM)&ti);
	}
}

// refresh the tooltip texts after a language change.
static void _zoomui_tooltip_update_texts(void)
{
	int i;

	if (!_zoomui_tooltip_hwnd)
	{
		return;
	}

	for(i=0;i<_zoomui_button_count;i++)
	{
		TOOLINFOW ti;
		wchar_t wbuf[STRING_SIZE];

		os_zero_memory(&ti,sizeof(ti));

		ti.cbSize = sizeof(ti);
		ti.hwnd = _zoomui_hwnd;
		ti.uId = (UINT_PTR)i;
		ti.hinst = 0;
		string_copy_utf8_string(wbuf,localization_get_string(_zoomui_tooltip_localization_ids[_zoomui_button_first + i]));
		ti.lpszText = wbuf;

		SendMessage(_zoomui_tooltip_hwnd,TTM_UPDATETIPTEXTW,0,(LPARAM)&ti);
	}

	_zoomui_tooltip_update_rects();
}

void zoomui_init(HWND parent)
{
	_zoomui_parent_hwnd = parent;

	_zoomui_calc_metrics();
	_zoomui_update_button_rects();

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
			WS_CHILD | WS_CLIPSIBLINGS | WS_TABSTOP,
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

		_zoomui_update_button_rects();
		_zoomui_tooltip_create();
	}
}

void zoomui_kill(void)
{
	_zoomui_kill_timer();

	if ((GetCapture()) && (_zoomui_hwnd) && (GetCapture() == _zoomui_hwnd))
	{
		ReleaseCapture();
	}

	_zoomui_tooltip_destroy();

	if (_zoomui_hwnd)
	{
		DestroyWindow(_zoomui_hwnd);

		_zoomui_hwnd = 0;
	}

	_zoomui_clear_hover_press(0);

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

        _zoomui_invalidate();
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
	_zoomui_tooltip_update_texts();
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
			if ((GetCapture() == _zoomui_hwnd) && (_zoomui_hwnd))
			{
				ReleaseCapture();
			}

			_zoomui_clear_hover_press(0);

			_zoomui_calc_metrics();
			_zoomui_update_button_rects();
			_zoomui_tooltip_rebuild();

			_zoomui_invalidate();

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
			_zoomui_clear_hover_press(0);

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
		// no WM_MOUSELEAVE arrives when hiding under the cursor: drop
		// any capture first so the viewer never keeps a stale grab.
		if (GetCapture() == _zoomui_hwnd)
		{
			ReleaseCapture();
		}

		// no WM_MOUSELEAVE arrives when hiding under the cursor.
		_zoomui_clear_hover_press(0);

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
			if (GetCapture() == _zoomui_hwnd)
			{
				ReleaseCapture();
			}

			_zoomui_clear_hover_press(0);

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
			if (GetCapture() == _zoomui_hwnd)
			{
				ReleaseCapture();
			}

			_zoomui_clear_hover_press(0);

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
	_zoomui_update_button_rects();

	container_wide = (_zoomui_button_count * _zoomui_button_wide) + ((_zoomui_button_count - 1) * _zoomui_button_gap) + (_zoomui_margin * 2);
	container_high = _zoomui_button_high + (_zoomui_margin * 2);

	if (wide < 0)
	{
		wide = 0;
	}

	if (high < 0)
	{
		high = 0;
	}

	if (_zoomui_is_fullscreen)
	{
		// the fullscreen overlay bar sits centered at the bottom.
		x = (wide - container_wide) / 2;
	}
	else
	{
		// the windowed pill hugs the bottom right of the image area.
		x = wide - container_wide - _zoomui_margin;
	}

	y = high - container_high - _zoomui_margin;

	// clamp inside the image area so a tiny window never pushes the
	// pill off screen; when the area is smaller than the pill, pin to
	// the origin and let the parent clip.
	if (container_wide >= wide)
	{
		x = 0;
	}
	else
	{
		if (x < 0)
		{
			x = 0;
		}

		if (x + container_wide > wide)
		{
			x = wide - container_wide;
		}
	}

	if (container_high >= high)
	{
		y = 0;
	}
	else
	{
		if (y < 0)
		{
			y = 0;
		}

		if (y + container_high > high)
		{
			y = high - container_high;
		}
	}

	SetWindowPos(_zoomui_hwnd,HWND_TOP,x,y,container_wide,container_high,SWP_NOACTIVATE);

	_zoomui_update_button_rects();
	_zoomui_tooltip_update_rects();

	_zoomui_invalidate();
}

static void _zoomui_draw_button(HDC hdc,const RECT *rect,int buttoni,int is_pressed,int is_disabled,int is_hot,int has_focus)
{
	RECT fill_rect;
	HBRUSH brush;
	HPEN pen;
	HPEN old_pen;
	HGDIOBJ old_brush;
	int offset;

	offset = 0;

	if ((is_pressed) && (!is_disabled))
	{
		offset = 1;
	}

	CopyRect(&fill_rect,rect);

	// the button body is a capsule: the corner ellipse is the full
	// button height, so each end is a true semicircle and the zoom pair
	// reads as the two signature pills instead of the square cornered
	// blocks (which sat next to the flat toolbar like a patch from
	// another toolkit). roundrect fills and outlines the same
	// silhouette in one call.
	{
		COLORREF fill_color;

		if ((is_disabled))
		{
			fill_color = _zoomui_dark ? RGB(0x25,0x25,0x25) : GetSysColor(COLOR_BTNFACE);
		}
		else if ((is_pressed) && (is_hot))
		{
			fill_color = _zoomui_dark ? RGB(0x42,0x42,0x42) : GetSysColor(COLOR_3DLIGHT);
		}
		else if (is_hot)
		{
			fill_color = _zoomui_dark ? RGB(0x38,0x38,0x38) : GetSysColor(COLOR_3DLIGHT);
		}
		else
		{
			fill_color = _zoomui_dark ? RGB(0x25,0x25,0x25) : GetSysColor(COLOR_BTNFACE);
		}

		brush = CreateSolidBrush(fill_color);
		pen = CreatePen(PS_SOLID,1,_zoomui_dark ? ((is_pressed && is_hot) ? RGB(0x80,0x80,0x80) : RGB(0x45,0x45,0x45)) : GetSysColor(((is_pressed) && (is_hot)) ? COLOR_3DDKSHADOW : COLOR_3DSHADOW));

		old_pen = SelectObject(hdc,pen);
		old_brush = SelectObject(hdc,brush);

		{
			int corner;

			// full stadium: the ellipse diameters equal the button
			// height so the end radius is exactly half the height at
			// every dpi. clamp to at least 2px so a degenerate rect
			// never collapses into sharp corners.
			corner = fill_rect.bottom - fill_rect.top;

			if (corner < 2)
			{
				corner = 2;
			}

			RoundRect(hdc,fill_rect.left,fill_rect.top,fill_rect.right,fill_rect.bottom,corner,corner);
		}

		SelectObject(hdc,old_brush);
		SelectObject(hdc,old_pen);
		DeleteObject(pen);
		DeleteObject(brush);
	}

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
	_zoomui_draw_icon(hdc,rect,buttoni,offset,is_disabled);

	// keyboard focus: a dotted ring inside the hot capsule.
	if ((has_focus) && (is_hot) && (!is_disabled))
	{
		RECT focus_rect;

		CopyRect(&focus_rect,rect);

		InflateRect(&focus_rect,-4,-4);

		if ((focus_rect.right > focus_rect.left) && (focus_rect.bottom > focus_rect.top))
		{
			DrawFocusRect(hdc,&focus_rect);
		}
	}
}

// draw the glyph icon, centered in the button.
static void _zoomui_draw_icon(HDC hdc,const RECT *rect,int buttoni,int offset,int is_disabled)
{
	int wide;
	int high;
	int size;
	HICON icon;

	wide = rect->right - rect->left;
	high = rect->bottom - rect->top;

	// the button is wider than tall, so the height drives the glyph box and
	// the sides absorb the extra width: the padding stays even on all four
	// sides of the glyph at every dpi (the old min() box leaned on the
	// width and squeezed the glyphs against the capsule arcs).
	size = high - 2 * (high / 9);

	if (size < 8)
	{
		size = 8;
	}

	icon = glyphs_icon(_zoomui_glyph_ids[_zoomui_button_first + buttoni],_zoomui_dark,size);

	if (icon)
	{
		int x;
		int y;

		x = ((wide - size) / 2) + offset;
		y = ((high - size) / 2) + offset;

		if (is_disabled)
		{
			// embossed disabled glyph through the stock raster op: no
			// extra cache, same theme aware icon.
			DrawState(hdc,NULL,NULL,(LPARAM)icon,0,x + rect->left,y + rect->top,size,size,DST_ICON | DSS_DISABLED);
		}
		else
		{
			DrawIconEx(hdc,x + rect->left,y + rect->top,icon,size,size,0,NULL,DI_NORMAL);
		}
	}
}

static LRESULT CALLBACK _zoomui_proc(HWND hwnd,UINT msg,WPARAM wParam,LPARAM lParam)
{
	switch (msg)
	{
		case WM_NCHITTEST:
		{
			POINT pt;
			RECT client_rect;

			pt.x = GET_X_LPARAM(lParam);
			pt.y = GET_Y_LPARAM(lParam);

			ScreenToClient(hwnd,&pt);

			GetClientRect(hwnd,&client_rect);

			if (!PtInRect(&client_rect,pt))
			{
				return HTTRANSPARENT;
			}

			// a fully transparent (fading out) or unwanted pill never
			// eats clicks: let them fall through to the viewer canvas.
			if ((!_zoomui_visible_wanted) || (_zoomui_alpha == 0) || (!IsWindowVisible(hwnd)))
			{
				return HTTRANSPARENT;
			}

			// only the capsule buttons hit: the stadium corners and the
			// tray gaps are click through to the image behind.
			if (_zoomui_hit_test(pt.x,pt.y) >= 0)
			{
				return HTCLIENT;
			}

			return HTTRANSPARENT;
		}

		case WM_MOUSEMOVE:
		{
			int x;
			int y;
			int hit;

			x = GET_X_LPARAM(lParam);
			y = GET_Y_LPARAM(lParam);

			hit = _zoomui_hit_test(x,y);

			if (GetCapture() == hwnd)
			{
				// captured drag: the hot capsule follows the cursor so
				// the press visual tracks, the command still fires only
				// on release over the same button.
				if (hit != _zoomui_hot_index)
				{
					_zoomui_hot_index = hit;

					_zoomui_invalidate();
				}
			}
			else
			{
				if (hit != _zoomui_hot_index)
				{
					_zoomui_hot_index = hit;

					_zoomui_invalidate();
				}

				_zoomui_track_leave(hwnd);
			}

			// hovering the bar counts as activity (idle fade timer).
			if (hit >= 0)
			{
				zoomui_activity();
			}

			return 0;
		}

		case WM_MOUSELEAVE:
		{
			// while captured the press owns the cursor: keep the hot
			// state until the button up resolves it.
			if (GetCapture() != hwnd)
			{
				if (_zoomui_hot_index != -1)
				{
					_zoomui_hot_index = -1;

					_zoomui_invalidate();
				}
			}

			return 0;
		}

		case WM_LBUTTONDOWN:
		{
			int x;
			int y;
			int hit;

			SetFocus(hwnd);

			x = GET_X_LPARAM(lParam);
			y = GET_Y_LPARAM(lParam);

			hit = _zoomui_hit_test(x,y);

			if ((hit >= 0) && (!_zoomui_is_button_disabled(hit)))
			{
				SetCapture(hwnd);

				_zoomui_pressed_index = hit;
				_zoomui_hot_index = hit;

				_zoomui_invalidate();

				// hovering the bar counts as activity (idle fade timer).
				zoomui_activity();

				// dismiss the hover tip while pressed.
				if (_zoomui_tooltip_hwnd)
				{
					SendMessage(_zoomui_tooltip_hwnd,TTM_POP,0,0);
				}
			}

			return 0;
		}

		case WM_LBUTTONDBLCLK:
		{
			int x;
			int y;
			int hit;

			x = GET_X_LPARAM(lParam);
			y = GET_Y_LPARAM(lParam);

			hit = _zoomui_hit_test(x,y);

			if ((hit >= 0) && (!_zoomui_is_button_disabled(hit)))
			{
				SetCapture(hwnd);

				_zoomui_pressed_index = hit;
				_zoomui_hot_index = hit;

				_zoomui_invalidate();

				zoomui_activity();

				if (_zoomui_tooltip_hwnd)
				{
					SendMessage(_zoomui_tooltip_hwnd,TTM_POP,0,0);
				}
			}

			return 0;
		}

		case WM_LBUTTONUP:
		{
			int x;
			int y;
			int hit;
			int down;

			x = GET_X_LPARAM(lParam);
			y = GET_Y_LPARAM(lParam);

			hit = _zoomui_hit_test(x,y);
			down = _zoomui_pressed_index;

			if (GetCapture() == hwnd)
			{
				ReleaseCapture();
			}

			_zoomui_pressed_index = -1;
			_zoomui_hot_index = hit;

			_zoomui_invalidate();

			if ((down >= 0) && (hit == down))
			{
				_zoomui_fire_button(down);
			}

			return 0;
		}

		case WM_CAPTURECHANGED:
		{
			// the capture went elsewhere (hide, menu, another press):
			// drop the press visual. the button up already fired before
			// releasing, so this path never double fires.
			if ((HWND)lParam != hwnd)
			{
				if (_zoomui_pressed_index != -1)
				{
					_zoomui_pressed_index = -1;

					_zoomui_invalidate();
				}
			}

			break;
		}

		case WM_CANCELMODE:
		{
			if (GetCapture() == hwnd)
			{
				ReleaseCapture();
			}

			if ((_zoomui_pressed_index != -1) || (_zoomui_hot_index != -1))
			{
				// keep the hover under the cursor: the next mousemove
				// restores it; the press is always dropped.
				_zoomui_pressed_index = -1;

				if (GetCapture() != hwnd)
				{
					_zoomui_hot_index = -1;
				}

				_zoomui_invalidate();
			}

			break;
		}

		case WM_SETFOCUS:
		{
			// tabbed in with no hover yet: highlight the first capsule
			// so space / enter has a visible target.
			if ((_zoomui_hot_index == -1) && (_zoomui_button_count > 0))
			{
				_zoomui_hot_index = 0;

				_zoomui_invalidate();
			}
			else
			{
				_zoomui_invalidate();
			}

			return 0;
		}

		case WM_KILLFOCUS:
		{
			_zoomui_invalidate();

			return 0;
		}

		case WM_GETDLGCODE:
		{
			// arrows move between capsules, chars feed space / enter;
			// tab itself stays free so focus can leave the pill.
			return DLGC_WANTARROWS | DLGC_WANTCHARS;
		}

		case WM_KEYDOWN:
		{
			int hot;

			hot = _zoomui_hot_index;

			if ((hot < 0) && (_zoomui_button_count > 0))
			{
				hot = 0;
			}

			switch ((int)wParam)
			{
				case VK_LEFT:
				case VK_UP:
				{
					if (_zoomui_button_count > 0)
					{
						if (_zoomui_hot_index < 0)
						{
							_zoomui_hot_index = 0;
						}
						else
						{
							_zoomui_hot_index = (_zoomui_hot_index + _zoomui_button_count - 1) % _zoomui_button_count;
						}

						_zoomui_invalidate();

						zoomui_activity();
					}

					return 0;
				}

				case VK_RIGHT:
				case VK_DOWN:
				{
					if (_zoomui_button_count > 0)
					{
						if (_zoomui_hot_index < 0)
						{
							_zoomui_hot_index = 0;
						}
						else
						{
							_zoomui_hot_index = (_zoomui_hot_index + 1) % _zoomui_button_count;
						}

						_zoomui_invalidate();

						zoomui_activity();
					}

					return 0;
				}

				case VK_TAB:
				{
					if (_zoomui_button_count > 0)
					{
						// shift+tab walks backwards, plain tab forwards.
						if (GetKeyState(VK_SHIFT) < 0)
						{
							if (_zoomui_hot_index < 0)
							{
								_zoomui_hot_index = 0;
							}
							else
							{
								_zoomui_hot_index = (_zoomui_hot_index + _zoomui_button_count - 1) % _zoomui_button_count;
							}
						}
						else
						{
							if (_zoomui_hot_index < 0)
							{
								_zoomui_hot_index = 0;
							}
							else
							{
								_zoomui_hot_index = (_zoomui_hot_index + 1) % _zoomui_button_count;
							}
						}

						_zoomui_invalidate();

						zoomui_activity();
					}

					return 0;
				}

				case VK_SPACE:
				case VK_RETURN:
				{
					if ((hot >= 0) && (!_zoomui_is_button_disabled(hot)))
					{
						_zoomui_fire_button(hot);
					}

					return 0;
				}
			}

			break;
		}

		case WM_ENABLE:
		{
			_zoomui_invalidate();

			break;
		}

		case WM_DPICHANGED:
		{
			// the main window handler updates os_logical_* and rebuilds
			// the icon caches; refresh the metrics and the container for
			// the new scale so this window is correct whenever it paints
			// next. the parent on_size repositions right after.
			int container_wide;
			int container_high;

			_zoomui_calc_metrics();
			_zoomui_update_button_rects();

			container_wide = (_zoomui_button_count * _zoomui_button_wide) + ((_zoomui_button_count - 1) * _zoomui_button_gap) + (_zoomui_margin * 2);
			container_high = _zoomui_button_high + (_zoomui_margin * 2);

			SetWindowPos(hwnd,0,0,0,container_wide,container_high,SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE);

			_zoomui_update_button_rects();
			_zoomui_tooltip_update_rects();

			_zoomui_invalidate();

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
			// all painting happens in WM_PAINT (tray + capsules in one
			// pass): skipping the erase avoids flicker on hover.
			return 1;
		}

		case WM_PAINT:
		{
			PAINTSTRUCT ps;
			HDC hdc;
			RECT client_rect;
			int has_focus;

			hdc = BeginPaint(hwnd,&ps);

			if (hdc)
			{
				GetClientRect(hwnd,&client_rect);

				has_focus = (GetFocus() == hwnd) ? 1 : 0;

				// the pill tray: one stadium in the bar color with a hairline
				// border. the capsule buttons float on it with gaps around them,
				// so the tray reads as a soft rail under the pair instead of
				// the old raised square block with its double edge.
				{
					HBRUSH brush;
					HPEN pen;
					HPEN old_pen;
					HGDIOBJ old_brush;
					int corner;

					brush = CreateSolidBrush(_zoomui_dark ? RGB(0x20,0x20,0x20) : GetSysColor(COLOR_BTNFACE));
					pen = CreatePen(PS_SOLID,1,_zoomui_dark ? RGB(0x45,0x45,0x45) : GetSysColor(COLOR_3DSHADOW));

					old_pen = SelectObject(hdc,pen);
					old_brush = SelectObject(hdc,brush);

					// full stadium: the ellipse diameters equal the tray
					// height so the ends are true semicircles at every dpi.
					corner = client_rect.bottom - client_rect.top;

					if (corner < 2)
					{
						corner = 2;
					}

					RoundRect(hdc,client_rect.left,client_rect.top,client_rect.right,client_rect.bottom,corner,corner);

					SelectObject(hdc,old_brush);
					SelectObject(hdc,old_pen);
					DeleteObject(pen);
					DeleteObject(brush);
				}

				{
					int i;

					for(i=0;i<_zoomui_button_count;i++)
					{
						int is_pressed;
						int is_disabled;
						int is_hot;

						is_pressed = (_zoomui_pressed_index == i) ? 1 : 0;
						is_disabled = _zoomui_is_button_disabled(i);
						is_hot = (_zoomui_hot_index == i) ? 1 : 0;

						_zoomui_draw_button(hdc,&_zoomui_button_rects[i],i,is_pressed,is_disabled,is_hot,has_focus);
					}
				}
			}

			EndPaint(hwnd,&ps);

			return 0;
		}
	}

	return DefWindowProc(hwnd,msg,wParam,lParam);
}
