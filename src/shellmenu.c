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
// shell context menu section.

#include "viv.h"
#include "shellmenu.h"
#include <shlobj.h>

// the shell32 5.0+ export set, resolved at first use. the win9x shell
// never grew these entry points, so the section stays off those systems.
typedef HRESULT (__stdcall *_viv_shell_cdeffoldermenu_create2_t)(PCIDLIST_ABSOLUTE pidlFolder,HWND hwnd,UINT cidl,PCUITEMID_CHILD_ARRAY apidl,IShellFolder *psf,LPFNDFMCALLBACK pfn,UINT nKeys,const HKEY *ahkeys,IContextMenu **ppcm);
typedef PIDLIST_ABSOLUTE (__stdcall *_viv_shell_ilcreatefrompathw_t)(PCWSTR pszPath);
typedef PIDLIST_RELATIVE (__stdcall *_viv_shell_ilclone_t)(PCUIDLIST_RELATIVE pidl);
typedef PUITEMID_CHILD (__stdcall *_viv_shell_ilfindlastid_t)(PCUIDLIST_RELATIVE pidl);
typedef WINBOOL (__stdcall *_viv_shell_ilremovelastid_t)(PUIDLIST_RELATIVE pidl);

static HMODULE _viv_shell_module;
static _viv_shell_cdeffoldermenu_create2_t _viv_shell_cdeffoldermenu_create2;
static _viv_shell_ilcreatefrompathw_t _viv_shell_ilcreatefrompathw;
static _viv_shell_ilclone_t _viv_shell_ilclone;
static _viv_shell_ilfindlastid_t _viv_shell_ilfindlastid;
static _viv_shell_ilremovelastid_t _viv_shell_ilremovelastid;

static IContextMenu *_viv_shell_context_menu;
static int _viv_shell_id_first;
static int _viv_shell_id_last;

// self-spelled iids (the os.c file-dialog precedent - no sdk dependency
// rides the values; the mingw shlguid.h was the second-verified source).
static const GUID _viv_shell_iid_icontextmenu = {0x000214e4,0x0000,0x0000,{0xc0,0x00,0x00,0x00,0x00,0x00,0x00,0x46}};
static const GUID _viv_shell_iid_icontextmenu2 = {0x000214f4,0x0000,0x0000,{0xc0,0x00,0x00,0x00,0x00,0x00,0x00,0x46}};
static const GUID _viv_shell_iid_icontextmenu3 = {0xbcfce0a0,0xec17,0x11d0,{0x8d,0x10,0x00,0xa0,0xc9,0x0f,0x27,0x19}};

// resolves the export set once; any missing entry point turns the whole
// section off for the session (the menu stays app-only, nothing breaks).
static int _viv_shell_procs(void)
{
	if (!_viv_shell_module)
	{
		_viv_shell_module = LoadLibraryA("shell32.dll");
		if (!_viv_shell_module)
		{
			return 0;
		}
		
		_viv_shell_cdeffoldermenu_create2 = (_viv_shell_cdeffoldermenu_create2_t)GetProcAddress(_viv_shell_module,"CDefFolderMenu_Create2");
		_viv_shell_ilcreatefrompathw = (_viv_shell_ilcreatefrompathw_t)GetProcAddress(_viv_shell_module,"ILCreateFromPathW");
		_viv_shell_ilclone = (_viv_shell_ilclone_t)GetProcAddress(_viv_shell_module,"ILClone");
		_viv_shell_ilfindlastid = (_viv_shell_ilfindlastid_t)GetProcAddress(_viv_shell_module,"ILFindLastID");
		_viv_shell_ilremovelastid = (_viv_shell_ilremovelastid_t)GetProcAddress(_viv_shell_module,"ILRemoveLastID");
	}
	
	return (_viv_shell_cdeffoldermenu_create2) && (_viv_shell_ilcreatefrompathw) && (_viv_shell_ilclone) && (_viv_shell_ilfindlastid) && (_viv_shell_ilremovelastid);
}

// releases whatever a previous popup left behind (the finish call is the
// normal path; this covers a rebuilt menu inside one session).
static void _viv_shell_context_menu_release(void)
{
	if (_viv_shell_context_menu)
	{
		_viv_shell_context_menu->lpVtbl->Release(_viv_shell_context_menu);
		
		_viv_shell_context_menu = 0;
	}
	
	_viv_shell_id_first = 0;
	_viv_shell_id_last = 0;
}

int _viv_shell_context_menu_append(HMENU hmenu,HWND hwnd,const wchar_t *full_filename)
{
	PIDLIST_ABSOLUTE pidl;
	
	if ((!hmenu) || (!full_filename) || (!*full_filename))
	{
		return 0;
	}
	
	if (!_viv_shell_procs())
	{
		debug_printf("shell menu: the shell32 5.0+ export set is missing\r\n");
		
		return 0;
	}
	
	_viv_shell_context_menu_release();
	
	// the pidl walk: the full path resolves, the parent clones and sheds
	// its last id, the child rides the one-element array the default menu
	// builds itself from. long paths the shell layer cannot parse simply
	// keep the section off (the viewer's own io never depends on it).
	pidl = _viv_shell_ilcreatefrompathw(full_filename);
	if (pidl)
	{
		PIDLIST_RELATIVE pidl_folder;
		
		pidl_folder = _viv_shell_ilclone(pidl);
		if (pidl_folder)
		{
			PUITEMID_CHILD pidl_child;
			PCUITEMID_CHILD apidl[1];
			IContextMenu *context_menu;
			HRESULT hresult;
			
			_viv_shell_ilremovelastid(pidl_folder);
			pidl_child = _viv_shell_ilfindlastid(pidl);
			
			apidl[0] = pidl_child;
			context_menu = 0;
			
			hresult = _viv_shell_cdeffoldermenu_create2(pidl_folder,hwnd,1,apidl,0,0,0,0,&context_menu);
			
			if (SUCCEEDED(hresult))
			{
				// the separator keeps the shell rows a section of their own
				// below the app's rows.
				AppendMenuW(hmenu,MF_SEPARATOR,0,0);
				
				hresult = context_menu->lpVtbl->QueryContextMenu(context_menu,hmenu,GetMenuItemCount(hmenu),_VIV_SHELL_MENU_ID_FIRST,_VIV_SHELL_MENU_ID_LAST,CMF_NORMAL);
				
				if (SUCCEEDED(hresult))
				{
					int count;
					
					count = (int)(short)LOWORD(hresult);
					
					if (count > 0)
					{
						_viv_shell_context_menu = context_menu;
						_viv_shell_id_first = _VIV_SHELL_MENU_ID_FIRST;
						_viv_shell_id_last = _VIV_SHELL_MENU_ID_FIRST + count - 1;
						
						CoTaskMemFree(pidl_folder);
						CoTaskMemFree(pidl);
						
						return _VIV_SHELL_MENU_ID_FIRST;
					}
				}
				
				context_menu->lpVtbl->Release(context_menu);
			}
			
			CoTaskMemFree(pidl_folder);
		}
		
		CoTaskMemFree(pidl);
	}
	else
	{
		debug_printf("shell menu: the path did not resolve to a pidl\r\n");
	}
	
	return 0;
}

void _viv_shell_context_menu_finish(HWND hwnd)
{
	(void)hwnd;
	
	_viv_shell_context_menu_release();
}

int _viv_shell_context_menu_invoke(HWND hwnd,int command_id)
{
	if ((_viv_shell_context_menu) && (command_id >= _viv_shell_id_first) && (command_id <= _viv_shell_id_last))
	{
		CMINVOKECOMMANDINFO info;
		IContextMenu *context_menu;
		
		context_menu = _viv_shell_context_menu;
		
		ZeroMemory(&info,sizeof(info));
		info.cbSize = sizeof(info);
		info.hwnd = hwnd;
		info.lpVerb = (LPCSTR)MAKEINTRESOURCE(command_id - _viv_shell_id_first);
		
		// the shell's own dispatch: the verb offset rides the int-resource
		// form the documented contract spells (IS_INTRESOURCE semantics).
		context_menu->lpVtbl->InvokeCommand(context_menu,&info);
		
		_viv_shell_context_menu = 0;
		
		context_menu->lpVtbl->Release(context_menu);
		
		return 1;
	}
	
	return 0;
}

int _viv_shell_context_menu_handle_menu_msg(HWND hwnd,UINT msg,WPARAM wParam,LPARAM lParam)
{
	(void)hwnd;
	
	if (_viv_shell_context_menu)
	{
		IContextMenu3 *context_menu3;
		HRESULT hresult;
		
		context_menu3 = 0;
		hresult = _viv_shell_context_menu->lpVtbl->QueryInterface(_viv_shell_context_menu,&_viv_shell_iid_icontextmenu3,(void **)&context_menu3);
		
		if (SUCCEEDED(hresult))
		{
			context_menu3->lpVtbl->HandleMenuMsg2(context_menu3,msg,wParam,lParam,0);
			
			context_menu3->lpVtbl->Release(context_menu3);
			
			return 1;
		}
		else
		{
			IContextMenu2 *context_menu2;
			
			context_menu2 = 0;
			hresult = _viv_shell_context_menu->lpVtbl->QueryInterface(_viv_shell_context_menu,&_viv_shell_iid_icontextmenu2,(void **)&context_menu2);
			
			if (SUCCEEDED(hresult))
			{
				context_menu2->lpVtbl->HandleMenuMsg(context_menu2,msg,wParam,lParam);
				
				context_menu2->lpVtbl->Release(context_menu2);
				
				return 1;
			}
		}
	}
	
	return 0;
}
