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
// shell context menu section: the upstream TODO's CDefFolderMenu_Create2
// item. the explorer verbs for the current file (open, open with, cut,
// copy, properties ...) ride below the app's own context rows and the
// selection leaves through InvokeCommand the moment it comes back. the
// whole shell32 5.0+ export set resolves dynamically - the win9x shell
// never grew these entry points, so the section quietly stays off those
// systems (the online second-verification round pinned the api to its
// nine-parameter windows 2000 signature).

#ifndef VIV_SHELLMENU_H
#define VIV_SHELLMENU_H

// the private command id range the shell verbs return through. every app
// command id lives far below; the system's own menu ids live at 0xf000+.
#define _VIV_SHELL_MENU_ID_FIRST 0x7000
#define _VIV_SHELL_MENU_ID_LAST 0x7fff

// appends the shell section (a leading separator included) to a popup
// menu. returns the first shell command id, or 0 when the section could
// not ride (no file, no export set, no pidl - the caller's menu simply
// stays app-only).
int _viv_shell_context_menu_append(HMENU hmenu,HWND hwnd,const wchar_t *full_filename);

// releases the menu object after the popup tracks (InvokeCommand already
// released it when a verb ran; this is the nobody-clicked path).
void _viv_shell_context_menu_finish(HWND hwnd);

// routes a wm_command id from the shell range; returns 1 when consumed.
int _viv_shell_context_menu_invoke(HWND hwnd,int command_id);

// forwards the owner-draw menu messages (wm_initmenupopup,
// wm_measureitem, wm_drawitem) while a shell section is on screen;
// returns 1 when the message was consumed.
int _viv_shell_context_menu_handle_menu_msg(HWND hwnd,UINT msg,WPARAM wParam,LPARAM lParam);

#endif
