//
// Copyright 2025 voidtools / David Carpenter
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
// viv_menu.h - command and key tables, menus, menu localization.
#ifndef VIV_MENU_H
#define VIV_MENU_H

#include "viv.h"

// exported to other domains / the viv.c core
void _viv_check_menus(HMENU hmenu);
void _viv_get_command_name(wchar_t *wbuf,int command_index);
int _viv_command_index_from_command_id(int command_id);
void _viv_get_key_text(wchar_t *wbuf,DWORD keyflags);
HMENU _viv_create_menu(void);
void _viv_key_add(_viv_key_list_t *key_list,int command_index,DWORD keyflags);
void _viv_key_list_copy(_viv_key_list_t *dst,const _viv_key_list_t *src);
void _viv_key_clear_all(_viv_key_list_t *list);
void _viv_key_list_init(_viv_key_list_t *list);
int _viv_get_current_key_mod_flags(void);
void _viv_key_remove(_viv_key_list_t *keylist,int command_index,DWORD keyflags);

#endif
