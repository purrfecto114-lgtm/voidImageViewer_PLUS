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

// button order.
#define _ZOOMUI_BUTTON_COUNT 4

#define _ZOOMUI_ID_ZOOMOUT 0
#define _ZOOMUI_ID_ZOOMIN 1
#define _ZOOMUI_ID_1TO1 2
#define _ZOOMUI_ID_BESTFIT 3

static const int _zoomui_command_ids[_ZOOMUI_BUTTON_COUNT] =
{
        VIV_ID_VIEW_ZOOM_OUT,
        VIV_ID_VIEW_ZOOM_IN,
        VIV_ID_VIEW_1TO1,
        VIV_ID_VIEW_BESTFIT,
};

static const localization_id_t _zoomui_tooltip_localization_ids[_ZOOMUI_BUTTON_COUNT] =
{
        LOCALIZATION_ID_ZOOMUI_TOOLTIP_ZOOM_OUT,
        LOCALIZATION_ID_ZOOMUI_TOOLTIP_ZOOM_IN,
        LOCALIZATION_ID_ZOOMUI_TOOLTIP_1TO1,
        LOCALIZATION_ID_ZOOMUI_TOOLTIP_BESTFIT,
};

static HWND _zoomui_hwnd = 0;
static HWND _zoomui_parent_hwnd = 0;
static HWND _zoomui_tooltip_hwnd = 0;
static HWND _zoomui_button_hwnds[_ZOOMUI_BUTTON_COUNT];

static int _zoomui_button_wide = 0;
static int _zoomui_button_high = 0;
static int _zoomui_margin = 0;
static int _zoomui_is_registered = 0;

static void _zoomui_draw_button(HDC hdc,const RECT *rect,int buttoni,int is_selected,int is_disabled);
static void _zoomui_draw_zoom_glass(HDC hdc,const RECT *rect,int is_zoomin,int offset);
static void _zoomui_draw_1to1(HDC hdc,const RECT *rect,int offset);
static void _zoomui_draw_bestfit(HDC hdc,const RECT *rect,int offset);
static LRESULT CALLBACK _zoomui_proc(HWND hwnd,UINT msg,WPARAM wParam,LPARAM lParam);

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
                DestroyWindow(_zoomui_hwnd);

                _zoomui_hwnd = 0;
        }

        os_zero_memory(_zoomui_button_hwnds,sizeof(_zoomui_button_hwnds));

        _zoomui_parent_hwnd = 0;
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

static void _zoomui_draw_button(HDC hdc,const RECT *rect,int buttoni,int is_selected,int is_disabled)
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
                brush = GetSysColorBrush(COLOR_3DLIGHT);
                offset = 1;
        }
        else
        {
                brush = GetSysColorBrush(COLOR_BTNFACE);
        }

        CopyRect(&fill_rect,rect);

        FillRect(hdc,&fill_rect,brush);

        // border.
        pen = CreatePen(PS_SOLID,1,GetSysColor(is_selected ? COLOR_3DDKSHADOW : COLOR_3DSHADOW));
        old_pen = SelectObject(hdc,pen);

        old_brush = SelectObject(hdc,GetStockObject(NULL_BRUSH));

        Rectangle(hdc,fill_rect.left,fill_rect.top,fill_rect.right,fill_rect.bottom);

        SelectObject(hdc,old_brush);
        SelectObject(hdc,old_pen);
        DeleteObject(pen);

        if (is_disabled)
        {
                SetTextColor(hdc,GetSysColor(COLOR_3DSHADOW));
        }
        else
        {
                SetTextColor(hdc,GetSysColor(COLOR_BTNTEXT));
        }

        SetBkMode(hdc,TRANSPARENT);

        switch(buttoni)
        {
                case _ZOOMUI_ID_ZOOMOUT:
                case _ZOOMUI_ID_ZOOMIN:
                        _zoomui_draw_zoom_glass(hdc,rect,(buttoni == _ZOOMUI_ID_ZOOMIN),offset);
                        break;

                case _ZOOMUI_ID_1TO1:
                        _zoomui_draw_1to1(hdc,rect,offset);
                        break;

                case _ZOOMUI_ID_BESTFIT:
                        _zoomui_draw_bestfit(hdc,rect,offset);
                        break;
        }
}

// draw a magnifying glass with a minus or plus.
static void _zoomui_draw_zoom_glass(HDC hdc,const RECT *rect,int is_zoomin,int offset)
{
        int cx;
        int cy;
        int lens_r;
        HPEN pen;
        HPEN old_pen;

        cx = (rect->left + rect->right) / 2 + offset;
        cy = (rect->top + rect->bottom) / 2 + offset;

        // keep the lens clear of the button edges.
        lens_r = (rect->right - rect->left) / 4;

        if (lens_r > (rect->bottom - rect->top) / 4)
        {
                lens_r = (rect->bottom - rect->top) / 4;
        }

        if (lens_r < 4)
        {
                lens_r = 4;
        }

        pen = CreatePen(PS_SOLID,(lens_r > 10) ? 2 : 1,GetTextColor(hdc));
        old_pen = SelectObject(hdc,pen);

        SelectObject(hdc,GetStockObject(NULL_BRUSH));

        Ellipse(hdc,cx - lens_r,cy - lens_r,cx + lens_r + 1,cy + lens_r + 1);

        // handle to the lower-right (45 degrees).
        {
                int hx;
                int hy;
                int len;

                // cos(45) ~= 707/1000.
                hx = cx + ((lens_r * 707) / 1000);
                hy = cy + ((lens_r * 707) / 1000);
                len = lens_r + (lens_r / 2);

                MoveToEx(hdc,hx,hy,NULL);
                LineTo(hdc,hx + len,hy + len);
        }

        // plus / minus.
        if (is_zoomin)
        {
                MoveToEx(hdc,cx - (lens_r / 2),cy,NULL);
                LineTo(hdc,cx + (lens_r / 2) + 1,cy);

                MoveToEx(hdc,cx,cy - (lens_r / 2),NULL);
                LineTo(hdc,cx,cy + (lens_r / 2) + 1);
        }
        else
        {
                MoveToEx(hdc,cx - (lens_r / 2),cy,NULL);
                LineTo(hdc,cx + (lens_r / 2) + 1,cy);
        }

        SelectObject(hdc,old_pen);
        DeleteObject(pen);
}

static void _zoomui_draw_1to1(HDC hdc,const RECT *rect,int offset)
{
        RECT text_rect;
        wchar_t wbuf[STRING_SIZE];
        HFONT font;
        HFONT old_font;
        int high;

        high = rect->bottom - rect->top;

        font = CreateFontW(
                (high * 2) / 5,
                0,0,0,
                FW_BOLD,
                0,0,0,
                DEFAULT_CHARSET,
                OUT_DEFAULT_PRECIS,
                CLIP_DEFAULT_PRECIS,
                DEFAULT_QUALITY,
                DEFAULT_PITCH | FF_DONTCARE,
                L"Tahoma");

        old_font = SelectObject(hdc,font);

        string_copy_utf8_string(wbuf,(const utf8_t *)"1:1");

        CopyRect(&text_rect,rect);

        text_rect.left += offset;
        text_rect.top += offset;

        DrawTextW(hdc,wbuf,-1,&text_rect,DT_CENTER|DT_VCENTER|DT_SINGLELINE|DT_NOCLIP);

        SelectObject(hdc,old_font);

        DeleteObject(font);
}

// draw 4 corner brackets around an implied image rectangle.
static void _zoomui_draw_bestfit(HDC hdc,const RECT *rect,int offset)
{
        int cx;
        int cy;
        int wide;
        int high;
        int bracket;
        HPEN pen;
        HPEN old_pen;

        cx = (rect->left + rect->right) / 2 + offset;
        cy = (rect->top + rect->bottom) / 2 + offset;

        wide = ((rect->right - rect->left) * 5) / 8;
        high = ((rect->bottom - rect->top) * 5) / 8;

        bracket = (wide / 3);

        if (bracket < 4)
        {
                bracket = 4;
        }

        pen = CreatePen(PS_SOLID,2,GetTextColor(hdc));
        old_pen = SelectObject(hdc,pen);

        // top-left.
        MoveToEx(hdc,cx - (wide / 2),cy - (high / 2) + bracket,NULL);
        LineTo(hdc,cx - (wide / 2),cy - (high / 2));
        LineTo(hdc,cx - (wide / 2) + bracket,cy - (high / 2));

        // top-right.
        MoveToEx(hdc,cx + (wide / 2) - bracket,cy - (high / 2),NULL);
        LineTo(hdc,cx + (wide / 2),cy - (high / 2));
        LineTo(hdc,cx + (wide / 2),cy - (high / 2) + bracket);

        // bottom-left.
        MoveToEx(hdc,cx - (wide / 2),cy + (high / 2) - bracket,NULL);
        LineTo(hdc,cx - (wide / 2),cy + (high / 2));
        LineTo(hdc,cx - (wide / 2) + bracket,cy + (high / 2));

        // bottom-right.
        MoveToEx(hdc,cx + (wide / 2) - bracket,cy + (high / 2),NULL);
        LineTo(hdc,cx + (wide / 2),cy + (high / 2));
        LineTo(hdc,cx + (wide / 2),cy + (high / 2) - bracket);

        SelectObject(hdc,old_pen);
        DeleteObject(pen);
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

                                        case VIV_ID_VIEW_1TO1:
                                                buttoni = _ZOOMUI_ID_1TO1;
                                                break;

                                        case VIV_ID_VIEW_BESTFIT:
                                                buttoni = _ZOOMUI_ID_BESTFIT;
                                                break;
                                }

                                if (buttoni != -1)
                                {
                                        _zoomui_draw_button(dis->hDC,&dis->rcItem,buttoni,(dis->itemState & ODS_SELECTED) ? 1 : 0,(dis->itemState & ODS_DISABLED) ? 1 : 0);

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

                        FillRect(hdc,&rect,(HBRUSH)(COLOR_BTNFACE + 1));

                        // raised border.
                        {
                                HPEN pen;
                                HPEN old_pen;

                                pen = CreatePen(PS_SOLID,1,GetSysColor(COLOR_3DSHADOW));
                                old_pen = SelectObject(hdc,pen);

                                MoveToEx(hdc,rect.left,rect.bottom - 1,NULL);
                                LineTo(hdc,rect.left,rect.top);
                                LineTo(hdc,rect.right - 1,rect.top);

                                SelectObject(hdc,old_pen);
                                DeleteObject(pen);

                                pen = CreatePen(PS_SOLID,1,GetSysColor(COLOR_3DHIGHLIGHT));
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
