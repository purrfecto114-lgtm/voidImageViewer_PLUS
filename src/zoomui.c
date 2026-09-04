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

#include "viv.h"
#include "zoomui.h"

// button order. the bar is intentionally only zoom out and zoom in: every
// click is one visible zoom step, nothing jumps to a zoom limit and nothing
// resembles a window caption button. 1:1 / best fit / reset stay in the
// View - Zoom menu and the right click menu.
#define _ZOOMUI_BUTTON_COUNT 2

#define _ZOOMUI_ID_ZOOMOUT 0
#define _ZOOMUI_ID_ZOOMIN 1

static const int _zoomui_command_ids[_ZOOMUI_BUTTON_COUNT] =
{
        VIV_ID_VIEW_ZOOM_OUT,
        VIV_ID_VIEW_ZOOM_IN,
};

static const localization_id_t _zoomui_tooltip_localization_ids[_ZOOMUI_BUTTON_COUNT] =
{
        LOCALIZATION_ID_ZOOMUI_TOOLTIP_ZOOM_OUT,
        LOCALIZATION_ID_ZOOMUI_TOOLTIP_ZOOM_IN,
};

// icon resources shared with the toolbar zoom buttons.
static const int _zoomui_icon_resource_ids[_ZOOMUI_BUTTON_COUNT] =
{
        IDI_ZOOMOUT,
        IDI_ZOOMIN,
};

static HWND _zoomui_hwnd = 0;
static HWND _zoomui_parent_hwnd = 0;
static HWND _zoomui_tooltip_hwnd = 0;
static HWND _zoomui_button_hwnds[_ZOOMUI_BUTTON_COUNT];

static int _zoomui_button_wide = 0;
static int _zoomui_button_high = 0;
static int _zoomui_margin = 0;
static int _zoomui_is_registered = 0;
static int _zoomui_hot_index = -1; // button under the cursor, or -1.

static HICON _zoomui_icons[_ZOOMUI_BUTTON_COUNT]; // cached icons, loaded at the drawn size.
static int _zoomui_icon_size = 0; // the size the cached icons were loaded at, or 0.
static int _zoomui_dark = 0; // 1 = draw with the dark mode palette.

static void _zoomui_apply_tooltip_colors(void);

static void _zoomui_draw_button(HDC hdc,const RECT *rect,int buttoni,int is_selected,int is_disabled,int is_hot);
static HICON _zoomui_get_icon(int buttoni,int size);
static void _zoomui_draw_icon(HDC hdc,const RECT *rect,int buttoni,int offset);
static LRESULT CALLBACK _zoomui_proc(HWND hwnd,UINT msg,WPARAM wParam,LPARAM lParam);
static void _zoomui_invalidate_button(int buttoni);
static LRESULT CALLBACK _zoomui_button_subclass_proc(HWND hwnd,UINT msg,WPARAM wParam,LPARAM lParam,UINT_PTR uSubclass,DWORD_PTR dwRefData);

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

        for(i=0;i<_ZOOMUI_BUTTON_COUNT;i++)
        {
                if (_zoomui_button_hwnds[i])
                {
                        SetWindowPos(_zoomui_button_hwnds[i],0,_zoomui_margin + (i * _zoomui_button_wide),_zoomui_margin,_zoomui_button_wide,_zoomui_button_high,SWP_NOZORDER|SWP_NOACTIVATE);
                }
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
                int i;

                _zoomui_hwnd = os_CreateWindowEx(
                        WS_EX_TOOLWINDOW,
                        "_VIV_ZOOMUI",
                        "",
                        WS_CLIPCHILDREN | WS_CLIPSIBLINGS | WS_CHILD,
                        0,0,0,0,
                        parent,(HMENU)VIV_ID_ZOOMUI,os_hinstance,NULL);

                os_zero_memory(_zoomui_button_hwnds,sizeof(_zoomui_button_hwnds));

                for(i=0;i<_ZOOMUI_BUTTON_COUNT;i++)
                {
                        _zoomui_button_hwnds[i] = os_CreateWindowEx(
                                0,
                                "BUTTON",
                                "",
                                WS_VISIBLE | WS_CHILD | BS_OWNERDRAW | BS_PUSHBUTTON | WS_TABSTOP,
                                0,0,0,0,
                                _zoomui_hwnd,
                                (HMENU)(UINT_PTR)_zoomui_command_ids[i],
                                os_hinstance,
                                NULL);
                }

                _zoomui_layout_buttons();

                // install hover tracking on each button.
                for(i=0;i<_ZOOMUI_BUTTON_COUNT;i++)
                {
                        if (_zoomui_button_hwnds[i])
                        {
                                SetWindowSubclass(_zoomui_button_hwnds[i],_zoomui_button_subclass_proc,1,(DWORD_PTR)i);
                        }
                }

                // tooltips.
                {
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
                                for(i=0;i<_ZOOMUI_BUTTON_COUNT;i++)
                                {
                                        TOOLINFOW ti;
                                        wchar_t wbuf[STRING_SIZE];

                                        os_zero_memory(&ti,sizeof(ti));

                                        ti.cbSize = sizeof(ti);
                                        ti.uFlags = TTF_IDISHWND | TTF_SUBCLASS;
                                        ti.hwnd = _zoomui_hwnd;
                                        ti.uId = (UINT_PTR)_zoomui_button_hwnds[i];
                                        ti.hinst = os_hinstance;
                                        string_copy_utf8_string(wbuf,localization_get_string(_zoomui_tooltip_localization_ids[i]));
                                        ti.lpszText = wbuf;

                                        SendMessage(_zoomui_tooltip_hwnd,TTM_ADDTOOLW,0,(LPARAM)&ti);
                                }

                                SendMessage(_zoomui_tooltip_hwnd,TTM_ACTIVATE,TRUE,0);

                                // match the tooltip colors to the current palette.
                                _zoomui_apply_tooltip_colors();
                        }
                }
        }
}

void zoomui_kill(void)
{
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
                        
                        for(i=0;i<_ZOOMUI_BUTTON_COUNT;i++)
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

        // release the cached icons.
        {
                int i;

                for(i=0;i<_ZOOMUI_BUTTON_COUNT;i++)
                {
                        if (_zoomui_icons[i])
                        {
                                DestroyIcon(_zoomui_icons[i]);

                                _zoomui_icons[i] = 0;
                        }
                }
        }

        _zoomui_icon_size = 0;

        os_zero_memory(_zoomui_button_hwnds,sizeof(_zoomui_button_hwnds));

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
                
                for(i=0;i<_ZOOMUI_BUTTON_COUNT;i++)
                {
                        TOOLINFOW ti;
                        wchar_t wbuf[STRING_SIZE];
                        
                        os_zero_memory(&ti,sizeof(ti));
                        
                        ti.cbSize = sizeof(ti);
                        ti.uFlags = TTF_IDISHWND;
                        ti.hwnd = _zoomui_hwnd;
                        ti.uId = (UINT_PTR)_zoomui_button_hwnds[i];
                        ti.hinst = os_hinstance;
                        string_copy_utf8_string(wbuf,localization_get_string(_zoomui_tooltip_localization_ids[i]));
                        ti.lpszText = wbuf;
                        
                        SendMessage(_zoomui_tooltip_hwnd,TTM_UPDATETIPTEXTW,0,(LPARAM)&ti);
                }
        }
}

void zoomui_show(int show)
{
        if (_zoomui_hwnd)
        {
                if (!show)
                {
                        // no WM_MOUSELEAVE arrives when hiding under the cursor.
                        _zoomui_hot_index = -1;
                }
                
                ShowWindow(_zoomui_hwnd,show ? SW_SHOW : SW_HIDE);

                if (_zoomui_tooltip_hwnd)
                {
                        SendMessage(_zoomui_tooltip_hwnd,TTM_ACTIVATE,show ? TRUE : FALSE,0);
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

        container_wide = (_ZOOMUI_BUTTON_COUNT * _zoomui_button_wide) + (_zoomui_margin * 2);
        container_high = _zoomui_button_high + (_zoomui_margin * 2);

        x = wide - container_wide - _zoomui_margin;
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

        // the icons make the buttons unmistakably zoom in / zoom out.
        _zoomui_draw_icon(hdc,rect,buttoni,offset);
}

// return the button icon at the requested size, loading and caching the
// shared toolbar zoom icons. non shared LoadImage icons must be destroyed,
// which zoomui_kill() and the size change below take care of.
static HICON _zoomui_get_icon(int buttoni,int size)
{
        if ((buttoni < 0) || (buttoni >= _ZOOMUI_BUTTON_COUNT))
        {
                return 0;
        }

        if (size <= 0)
        {
                return 0;
        }

        if (_zoomui_icon_size != size)
        {
                int i;

                for(i=0;i<_ZOOMUI_BUTTON_COUNT;i++)
                {
                        if (_zoomui_icons[i])
                        {
                                DestroyIcon(_zoomui_icons[i]);

                                _zoomui_icons[i] = 0;
                        }
                }

                _zoomui_icon_size = size;
        }

        if (!_zoomui_icons[buttoni])
        {
                _zoomui_icons[buttoni] = (HICON)LoadImage(os_hinstance,MAKEINTRESOURCE(_zoomui_icon_resource_ids[buttoni]),IMAGE_ICON,size,size,LR_DEFAULTCOLOR);
        }

        return _zoomui_icons[buttoni];
}

// draw the shared zoom icon, centered in the button.
static void _zoomui_draw_icon(HDC hdc,const RECT *rect,int buttoni,int offset)
{
        int wide;
        int high;
        int size;
        HICON icon;

        wide = rect->right - rect->left;
        high = rect->bottom - rect->top;

        size = (wide < high) ? wide : high;

        // padding so the icon does not touch the button border.
        size -= size / 6;

        if (size < 8)
        {
                size = 8;
        }

        icon = _zoomui_get_icon(buttoni,size);

        if (icon)
        {
                DrawIconEx(hdc,((wide - size) / 2) + offset,((high - size) / 2) + offset,icon,size,size,0,NULL,DI_NORMAL);
        }
}

// invalidate a single button. keeps hover repaints cheap.
static void _zoomui_invalidate_button(int buttoni)
{
        if ((buttoni >= 0) && (buttoni < _ZOOMUI_BUTTON_COUNT))
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

                                buttoni = -1;

                                switch(dis->CtlID)
                                {
                                        case VIV_ID_VIEW_ZOOM_OUT:
                                                buttoni = _ZOOMUI_ID_ZOOMOUT;
                                                break;

                                        case VIV_ID_VIEW_ZOOM_IN:
                                                buttoni = _ZOOMUI_ID_ZOOMIN;
                                                break;

                                }

                                if (buttoni != -1)
                                {
                                        _zoomui_draw_button(dis->hDC,&dis->rcItem,buttoni,(dis->itemState & ODS_SELECTED) ? 1 : 0,(dis->itemState & ODS_DISABLED) ? 1 : 0,(buttoni == _zoomui_hot_index) ? 1 : 0);

                                        return TRUE;
                                }
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
