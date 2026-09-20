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
// viv_dialogs.c - modal dialogs: options, jump-to, about, rename, rate.
// the zoom editor lives in place on the status bar pane, not in a box.
// Pure physical move from viv.c (R70 split): bodies are unchanged.
#include "viv.h"
#include "viv_state.h"
#include "viv_dialogs.h"
#include "viv_menubar.h"
#include "viv_chrome.h"
#include "viv_dark.h"
#include "viv_install.h"
#include "viv_load.h"
#include "viv_menu.h"
#include "viv_playlist.h"
#include "viv_recent.h"
#include "viv_render.h"
#include "viv_theme.h"
#include "viv_view.h"

// forward declarations (order preserved from viv.c)
static HBRUSH _viv_about_light_brush(int which);
static BOOL CALLBACK _viv_dialog_font_child(HWND hwnd,LPARAM lParam);
void _viv_dialog_apply_font(HWND hwnd);
static INT_PTR CALLBACK _viv_rename_proc(HWND hwnd,UINT msg,WPARAM wParam,LPARAM lParam);
void _viv_rename(void);
INT_PTR CALLBACK _viv_custom_rate_proc(HWND hwnd,UINT msg,WPARAM wParam,LPARAM lParam);
static INT_PTR _viv_about_colors(HWND hwnd,UINT msg,WPARAM wParam,LPARAM lParam);
INT_PTR CALLBACK _viv_about_proc(HWND hwnd,UINT msg,WPARAM wParam,LPARAM lParam);
void _viv_set_zoom_dialog(void);
static void _viv_zoom_edit_end(HWND hwnd,int apply);
static LRESULT CALLBACK _viv_zoom_edit_proc(HWND hwnd,UINT msg,WPARAM wParam,LPARAM lParam);
void _viv_command_line_options(void);
static void _viv_update_color_button_bitmap(HWND hwnd);
static void _viv_delete_color_button_bitmap(HWND hwnd);
static void _viv_jumpto_on_size(HWND hwnd);
static void _viv_jumpto_on_search(HWND hwnd);
static void _viv_jumpto_open_sel(HWND hwnd);
static LRESULT CALLBACK _viv_jumpto_proc(HWND hwnd,UINT msg,WPARAM wParam,LPARAM lParam);
void _viv_show_jumpto(void);
static void _viv_center_listbox_item(HWND listbox_hwnd,int item_index);


static BYTE _viv_jump_ret = 0;
// the about dialog band in the light ui takes the fixed win11 command
// bar palette instead of the system button colors (the band used to
// follow the windows theme of the machine - the fixed face holds one
// look everywhere). 0 = the separator line, 1 = the face. released in
// _viv_kill with the other cached brushes.
static HBRUSH _viv_about_light_brush(int which)
{
	static const COLORREF colors[2] = {RGB(0xEC,0xEC,0xEC),RGB(0xFF,0xFF,0xFF)};
	
	if ((which < 0) || (which > 1))
	{
		return 0;
	}
	
	if (!_viv_about_light_hbrushes[which])
	{
		_viv_about_light_hbrushes[which] = CreateSolidBrush(colors[which]);
	}
	
	return _viv_about_light_hbrushes[which];
}
static BOOL CALLBACK _viv_dialog_font_child(HWND hwnd,LPARAM lParam)
{
	SendMessage(hwnd,WM_SETFONT,(WPARAM)lParam,MAKELPARAM(TRUE,0));
	
	return TRUE;
}
// give a dialog and every child control the message font. the shared
// dialog proc runs this from wm_initdialog before each dialog's own
// case (and again from wm_dpichanged when the window crosses
// monitors): the dark flip captures the native combo field heights
// against the final font, the about title derives its larger face
// from it and the localized labels render with it.
void _viv_dialog_apply_font(HWND hwnd)
{
	LOGFONTW lf;
	HFONT font;
	HFONT old_font;
	
	if (!os_dialog_font(&lf,hwnd))
	{
		return;
	}
	
	font = CreateFontIndirectW(&lf);
	
	if (!font)
	{
		return;
	}
	
	old_font = (HFONT)GetPropW(hwnd,_VIV_DIALOG_FONT_PROP);
	
	SetPropW(hwnd,_VIV_DIALOG_FONT_PROP,(HANDLE)font);
	
	// the dialog window itself first: WM_GETFONT on the dialog
	// reports the template face until this set.
	SendMessage(hwnd,WM_SETFONT,(WPARAM)font,MAKELPARAM(TRUE,0));
	
	EnumChildWindows(hwnd,_viv_dialog_font_child,(LPARAM)font);
	
	// every child took the new face inside this same message: the old
	// handle has no user left, so it can die here (a paint can never
	// interleave the synchronous broadcast).
	if (old_font)
	{
		DeleteObject(old_font);
	}
}
static INT_PTR CALLBACK _viv_rename_proc(HWND hwnd,UINT msg,WPARAM wParam,LPARAM lParam)
{
	{
		INT_PTR dark_dialog_reply;
		
		dark_dialog_reply = _viv_dialog_dark_proc(hwnd,msg,wParam,lParam);
		
		if (dark_dialog_reply != -1)
		{
			return dark_dialog_reply;
		}
	}
	
	switch(msg)
	{
		case WM_INITDIALOG:
			// dark chrome: title bar and dark explorer control style.
			_viv_dark_dialog(hwnd);
			
		{
			wchar_t name[STRING_SIZE];
			
			os_center_dialog(hwnd);
			
			os_SetWindowText_localization_id(hwnd,LOCALIZATION_ID_RENAME_CAPTION);
			
			string_copy(name,string_get_filename_part((wchar_t *)lParam));
			string_remove_extension(name);

			SetDlgItemText(hwnd,IDC_RENAME_OLD_EDIT,(wchar_t *)lParam);
			SetDlgItemText(hwnd,IDC_RENAME_EDIT,name);

			return TRUE;
		}
		
		case WM_COMMAND:
		
			switch(LOWORD(wParam))
			{
				case IDOK:
					
					{
						wchar_t old_filename[STRING_SIZE];
						wchar_t new_filename[STRING_SIZE];
						wchar_t path[STRING_SIZE];
						wchar_t new_full_path_and_filename[STRING_SIZE];
						wchar_t *ext;
						int dont_end_dialog;
						
						GetDlgItemText(hwnd,IDC_RENAME_OLD_EDIT,old_filename,STRING_SIZE);
						GetDlgItemText(hwnd,IDC_RENAME_EDIT,new_filename,STRING_SIZE);
						
						string_get_path_part(path,old_filename);
						ext = string_get_extension(old_filename);
						dont_end_dialog = 0;
						
						string_path_combine(new_full_path_and_filename,path,new_filename);
						if (*ext)
						{
							string_cat_utf8(new_full_path_and_filename,(const utf8_t *)".");
							string_cat(new_full_path_and_filename,ext);
						}
						
						if (string_compare(old_filename,new_full_path_and_filename) != 0)
						{
							SHFILEOPSTRUCT fo;
							
							wchar_t old_filename_list[STRING_SIZE+1];
							wchar_t new_filename_list[STRING_SIZE+1];
							
							string_copy_double_null(old_filename_list,old_filename);
							string_copy_double_null(new_filename_list,new_full_path_and_filename);
							
							ZeroMemory(&fo,sizeof(SHFILEOPSTRUCT));
							fo.hwnd = _viv_hwnd;
							fo.wFunc = FO_RENAME;
							fo.pFrom = old_filename_list;
							fo.pTo = new_filename_list;
							fo.fFlags = FOF_ALLOWUNDO | FOF_WANTMAPPINGHANDLE;

							// returns ERROR_CANCELLED if user cancelled.
							if (SHFileOperation(&fo) == 0)
							{
								if (fo.fAnyOperationsAborted)
								{
									// user clicked No to a rename collision
									// (not cancelled).
									dont_end_dialog = 1;
								}
								else
								{	
									const wchar_t *file_op_new_name;
									
									file_op_new_name = new_full_path_and_filename;
									
									if (fo.hNameMappings)
									{
										_viv_name_mapping_t *mappings;
										
										mappings = (_viv_name_mapping_t *)fo.hNameMappings;
											
										if (mappings->count == 1)
										{
											// use the resolved name incase there was a rename collision.
											file_op_new_name = mappings->mappings[0].pszNewPath;
										}
									}	
									
									// has the file on screen changed? the rename binds to the displayed
// slot (the delete family's identity): a load in flight must not
// retitle the image the user is looking at.
									if (string_compare(old_filename,_viv_slot_current.fd.cFileName) == 0)
									{
										// rename
										string_copy_with_bufsize(_viv_current_fd->cFileName,MAX_PATH,file_op_new_name);
										
										_viv_playlist_rename(old_filename,file_op_new_name);

										// the recent entry follows the rename in place
										// (the old name would become a dead row).
										_viv_recent_file_rename(old_filename,file_op_new_name);

										// the title bar reads the current fd while the status strip
										// reads the slot's: mirror the new name into both or the two
										// faces disagree until the next load.
										string_copy_with_bufsize(_viv_slot_current.fd.cFileName,MAX_PATH,file_op_new_name);

										_viv_status_update();

										_viv_update_title();
									}						
								}

								if (fo.hNameMappings)
								{
									SHFreeNameMappings(fo.hNameMappings);
								}							
							}
						}

						if (!dont_end_dialog)
						{
							EndDialog(hwnd,1);
						}
					}
					
					break;
				

				case IDCANCEL:
					EndDialog(hwnd,0);
					break;
			}

			break;
	}
	
	return FALSE;
}
void _viv_rename(void)
{
	if (*_viv_slot_current.fd.cFileName)
	{
		DialogBoxParam(os_hinstance,MAKEINTRESOURCE(IDD_RENAME),_viv_hwnd,_viv_rename_proc,(LPARAM)_viv_slot_current.fd.cFileName);
	}
}
INT_PTR CALLBACK _viv_custom_rate_proc(HWND hwnd,UINT msg,WPARAM wParam,LPARAM lParam)
{
	{
		INT_PTR dark_dialog_reply;
		
		dark_dialog_reply = _viv_dialog_dark_proc(hwnd,msg,wParam,lParam);
		
		if (dark_dialog_reply != -1)
		{
			return dark_dialog_reply;
		}
	}
	
	switch(msg)
	{
		case WM_INITDIALOG:
			// dark chrome: title bar and dark explorer control style.
			_viv_dark_dialog(hwnd);
			

			{
				int static_wide;
				RECT rect;
				int wide;
				
				os_center_dialog(hwnd);
				GetClientRect(hwnd,&rect);
				wide = rect.right - rect.left - 12 - 12;

				os_SetWindowText_localization_id(hwnd,LOCALIZATION_ID_SET_CUSTOM_RATE_CAPTION);

				os_SetDlgItemText_localization_id(hwnd,IDOK,LOCALIZATION_ID_OK_BUTTON);
				os_SetDlgItemText_localization_id(hwnd,IDCANCEL,LOCALIZATION_ID_CANCEL_BUTTON);

				SetDlgItemInt(hwnd,IDC_CUSTOM_RATE_EDIT,config_slideshow_custom_rate,FALSE);

				os_SetDlgItemText_localization_id(hwnd,IDC_CUSTOM_RATE_STATIC,LOCALIZATION_ID_CUSTOM_RATE_STATIC);
				static_wide = os_get_static_wide(hwnd,IDC_CUSTOM_RATE_STATIC);
				os_set_dialog_item_x_wide(hwnd,IDC_CUSTOM_RATE_STATIC,12,static_wide+6);
	
				{
					int edit_combo_wide;
					
					edit_combo_wide = (wide - (static_wide+6) - 6) / 2;
					
					os_set_dialog_item_x_wide(hwnd,IDC_CUSTOM_RATE_EDIT,12 + static_wide+6,edit_combo_wide);
					os_set_dialog_item_x_wide(hwnd,IDC_CUSTOM_RATE_TYPE_COMBOBOX,12 + static_wide+6 + edit_combo_wide + 6,edit_combo_wide);
				}
				
				os_ComboBox_AddString_localization_id(hwnd,IDC_CUSTOM_RATE_TYPE_COMBOBOX,LOCALIZATION_ID_CUSTOM_RATE_MILLISECONDS);
				os_ComboBox_AddString_localization_id(hwnd,IDC_CUSTOM_RATE_TYPE_COMBOBOX,LOCALIZATION_ID_CUSTOM_RATE_SECONDS);
				os_ComboBox_AddString_localization_id(hwnd,IDC_CUSTOM_RATE_TYPE_COMBOBOX,LOCALIZATION_ID_CUSTOM_RATE_MINUTES);
				
				ComboBox_SetCurSel(GetDlgItem(hwnd,IDC_CUSTOM_RATE_TYPE_COMBOBOX),config_slideshow_custom_rate_type);
			}

			return TRUE;
		
		case WM_COMMAND:
		
			switch(LOWORD(wParam))
			{
				case IDOK:
					config_slideshow_custom_rate_type = ComboBox_GetCurSel(GetDlgItem(hwnd,IDC_CUSTOM_RATE_TYPE_COMBOBOX));
					config_slideshow_custom_rate = GetDlgItemInt(hwnd,IDC_CUSTOM_RATE_EDIT,NULL,FALSE);
					EndDialog(hwnd,1);
					break;

				case IDCANCEL:
					EndDialog(hwnd,0);
					break;
			}

			break;
	}
	
	return FALSE;
}
// the about dialog colors. the banner (aboutback + abouttitle) keeps its
// 1.1.11 face: the black band with the white title in the light ui, the
// shared dark canvas face in the dark. the band chrome (the two separator
// lines and the button strip) answers with the same palettes the wm_paint
// passes painted before the template round moved the geometry into the
// resource template. the rest of the controls answer the light face the
// 1.1.11 ctlcolor case painted and fall through to the shared dark handler
// in the dark ui (the caller runs this before the shared proc so the band
// controls never fall into its flat 0x20 reply).
static INT_PTR _viv_about_colors(HWND hwnd,UINT msg,WPARAM wParam,LPARAM lParam)
{
	if ((msg == WM_CTLCOLORSTATIC) || (msg == WM_CTLCOLOREDIT))
	{
		HDC hdc;
		HWND control;
		
		hdc = (HDC)wParam;
		control = (HWND)lParam;
		
		if ((control == GetDlgItem(hwnd,IDC_ABOUTBACK)) || (control == GetDlgItem(hwnd,IDC_ABOUTTITLE)))
		{
			if (_viv_is_dark())
			{
				SetTextColor(hdc,RGB(0xE8,0xE8,0xE8));
				SetBkColor(hdc,RGB(0x20,0x20,0x20));
				
				return (INT_PTR)_viv_dialog_dark_brush();
			}
			
			SetTextColor(hdc,RGB(255,255,255));
			SetBkColor(hdc,RGB(0,0,0));
			
			return (LRESULT)GetStockObject(BLACK_BRUSH);
		}
		
		if (control == GetDlgItem(hwnd,IDC_ABOUTLINE1))
		{
			if (_viv_is_dark())
			{
				SetBkColor(hdc,RGB(0x45,0x45,0x45));
				
				return (INT_PTR)_viv_dark_chrome_brush(1);
			}
			
			SetBkColor(hdc,RGB(0xEC,0xEC,0xEC));
			
			return (INT_PTR)_viv_about_light_brush(0);
		}
		
		if (control == GetDlgItem(hwnd,IDC_ABOUTLINE2))
		{
			if (_viv_is_dark())
			{
				SetBkColor(hdc,RGB(0x70,0x70,0x70));
				
				return (INT_PTR)_viv_dark_chrome_brush(2);
			}
			
			SetBkColor(hdc,RGB(0xEC,0xEC,0xEC));
			
			return (INT_PTR)_viv_about_light_brush(0);
		}
		
		if (control == GetDlgItem(hwnd,IDC_ABOUTBAND))
		{
			if (_viv_is_dark())
			{
				SetBkColor(hdc,RGB(0x25,0x25,0x25));
				
				return (INT_PTR)_viv_dark_chrome_brush(0);
			}
			
			SetBkColor(hdc,RGB(0xFF,0xFF,0xFF));
			
			return (INT_PTR)_viv_about_light_brush(1);
		}
		
		// the rest of the controls keep the 1.1.11 light face; the dark ui
		// answers them through the shared dark handler - this reply only
		// covers the light theme.
		if (!_viv_is_dark())
		{
			SetTextColor(hdc,RGB(0,0,0));
			SetBkColor(hdc,RGB(255,255,255));
			
			return (LRESULT)GetStockObject(WHITE_BRUSH);
		}
	}
	
	if (msg == WM_CTLCOLORDLG)
	{
		// the dialog face: the wm_paint pass used to paint it after the
		// default erase; the ctldlg reply now hands the dialog manager the
		// same brush for its own erase (the template round owns the
		// geometry, the palette travels the official pipeline).
		if (_viv_is_dark())
		{
			return (INT_PTR)_viv_dialog_dark_brush();
		}
		
		return (LRESULT)GetStockObject(WHITE_BRUSH);
	}
	
	return -1;
}
INT_PTR CALLBACK _viv_about_proc(HWND hwnd,UINT msg,WPARAM wParam,LPARAM lParam)
{
	{
		INT_PTR own_reply;
		
		// the banner and the band controls answer here (the shared dark
		// handler below flattens every static to the 0x20 face and the
		// band needs the line and strip colors); the dialog face answers
		// here because the shared handler carries no ctldlg case. any
		// other message falls through to the shared pair, then to the
		// switch below.
		own_reply = _viv_about_colors(hwnd,msg,wParam,lParam);
		
		if (own_reply != -1)
		{
			return own_reply;
		}
	}
	
	{
		INT_PTR dark_dialog_reply;
		
		dark_dialog_reply = _viv_dialog_dark_proc(hwnd,msg,wParam,lParam);
		
		if (dark_dialog_reply != -1)
		{
			return dark_dialog_reply;
		}
	}
	
	switch(msg)
	{
		case WM_INITDIALOG:
			// dark chrome: title bar and dark explorer control style.
			_viv_dark_dialog(hwnd);
			
		{
			HFONT hfont;
			HFONT old_hfont;
			LOGFONTW lf;
			wchar_t version_wbuf[STRING_SIZE];

			os_center_dialog(hwnd);

			string_copy_utf8_string(version_wbuf,localization_get_string(LOCALIZATION_ID_ABOUT_CAPTION));
			SetWindowText(hwnd,version_wbuf);
			os_SetDlgItemText_localization_id(hwnd,IDC_ABOUTTITLE,LOCALIZATION_ID_APP_NAME);
			os_SetDlgItemText_localization_id(hwnd,IDC_ABOUTVOIDIMAGEVIEWER,LOCALIZATION_ID_APP_NAME);
			string_printf(version_wbuf,"%s%s %s",VERSION_STRING,VERSION_TYPE,VERSION_TARGET_MACHINE);
			SetDlgItemText(hwnd,IDC_ABOUTVERSION,version_wbuf);
			string_printf(version_wbuf,localization_get_string(LOCALIZATION_ID_ABOUT_COPYRIGHT_FORMAT),VERSION_YEAR);
			SetDlgItemText(hwnd,IDC_ABOUTCOPYRIGHT,version_wbuf);
			os_SetDlgItemText_localization_id(hwnd,IDC_ABOUTEMAIL,LOCALIZATION_ID_ABOUT_EMAIL);
			os_SetDlgItemText_localization_id(hwnd,IDC_ABOUTWEBSITE,LOCALIZATION_ID_ABOUT_WEBSITE);
			os_SetDlgItemText_localization_id(hwnd,IDOK,LOCALIZATION_ID_OK_BUTTON);
			os_SetDlgItemText_localization_id(hwnd,IDCANCEL,LOCALIZATION_ID_CANCEL_BUTTON);
			
			// the title face derives from the live dialog font end to
			// end: the message font the shared dialog proc applied at this
			// dialog window own dpi (wm_getfont reports it here: the apply
			// ran before this case), through the wide pipeline so a face
			// name survives whole (the unsuffixed forms borrowed the wide
			// path from the project unicode define), with the height
			// scaled by 8/3 - the ratio the literal 32 encoded at the 96
			// dpi design point (the message font is -12 there and
			// -12 * 8 / 3 = -32) - so the family, the weight and the dpi
			// scale all travel with the font the dialog actually draws.
			hfont = (HFONT)SendMessage(GetDlgItem(hwnd,IDC_ABOUTTITLE),WM_GETFONT,0,0);
			
			if ((hfont) && (GetObjectW(hfont,sizeof(LOGFONTW),&lf)))
			{
				lf.lfHeight = (lf.lfHeight * 8) / 3;
				
				old_hfont = _viv_about_hfont;
				
				_viv_about_hfont = CreateFontIndirectW(&lf);
				
				if (_viv_about_hfont)
				{
					// the face is rebuilt at every open: the old handle dies
					// inside this same message after the control took the new
					// face (a paint can never interleave the synchronous
					// wm_setfont), and a creation failure keeps the previous
					// face drawing instead of a null font.
					if (old_hfont)
					{
						DeleteObject(old_hfont);
					}
				}
				else
				{
					_viv_about_hfont = old_hfont;
				}
			}
			
			if (_viv_about_hfont)
			{
				SendMessage(GetDlgItem(hwnd,IDC_ABOUTTITLE),WM_SETFONT,(WPARAM)_viv_about_hfont,0);
			}
			
			return TRUE;
		}
		
		case WM_DESTROY:
			break;
		
		case WM_COMMAND:
		
			switch(LOWORD(wParam))
			{
				case IDOK:
				case IDCANCEL:
					EndDialog(hwnd,0);
					break;
			}

			break;
	}
	
	return FALSE;
}
static HWND _viv_zoom_edit_hwnd = 0; // the open in place zoom editor, or 0.
void _viv_set_zoom_dialog(void)
{
	// the status bar zoom pane click target: type an exact percent in place,
	// right on the pane. (the old centered dialog box carried a sunken edit,
	// native buttons and a select-all blue block that never met the remade
	// chrome, and the click itself had to survive the pane's drag subclass
	// first - a down eaten by the move loop, the box left to luck.)
	RECT pane_rect;
	wchar_t wbuf[STRING_SIZE];
	HFONT hfont;
	HWND hwnd;
	int wide;
	
	if (!_viv_slot_current.image_wide)
	{
		return;
	}
	
	if (!_viv_status_hwnd)
	{
		return;
	}
	
	// one editor at a time: a click while one is open commits it, then
	// re-opens on the current percent.
	if (_viv_zoom_edit_hwnd)
	{
		_viv_zoom_edit_end(_viv_zoom_edit_hwnd,1);
	}
	
	// pane 0's rect, in status bar client coordinates.
	if (!SendMessage(_viv_status_hwnd,SB_GETRECT,0,(LPARAM)&pane_rect))
	{
		return;
	}
	
	// the field: the pane width minus breathing room, floor 32 (four digits).
	wide = (pane_rect.right - pane_rect.left) - 8;
	
	if (wide < 32)
	{
		wide = 32;
	}
	
	hwnd = os_CreateWindowEx(
		0,
		"EDIT",
		"",
		WS_CHILD|WS_VISIBLE|ES_NUMBER|ES_AUTOHSCROLL,
		pane_rect.left + 4,pane_rect.top + 1,wide,(pane_rect.bottom - pane_rect.top) - 2,
		_viv_status_hwnd,0,os_hinstance,NULL);
	
	if (!hwnd)
	{
		return;
	}
	
	_viv_zoom_edit_hwnd = hwnd;
	
	// digits only, never text: dissociate the ime so even a full-width
	// digit ime mode types plain digits into the field (see
	// os_imm_associate_disable).
	os_imm_associate_disable(hwnd);
	
	// the subclass stores the old proc in the userdata (the edit key
	// dialog's own editor idiom).
	{
		WNDPROC old_proc;
		
		old_proc = (WNDPROC)SetWindowLongPtr(hwnd,GWLP_WNDPROC,(LONG_PTR)_viv_zoom_edit_proc);
		
		SetWindowLongPtr(hwnd,GWLP_USERDATA,(LONG_PTR)old_proc);
	}
	
	// the pane's own font, so the digits match the strip text.
	hfont = (HFONT)SendMessage(_viv_status_hwnd,WM_GETFONT,0,0);
	
	if (hfont)
	{
		SendMessage(hwnd,WM_SETFONT,(WPARAM)hfont,0);
	}
	
	// the current percent as plain digits, selected: typing replaces.
	string_printf(wbuf,"%d",_viv_zoom_percent());
	SetWindowTextW(hwnd,wbuf);
	
	SendMessage(hwnd,EM_SETSEL,0,-1);
	
	SetFocus(hwnd);
}
void _viv_zoom_edit_refont(void)
{
	// the in-place zoom editor carries the strip's font handle from
	// its creation; a font drop (the dpi change, the theme change)
	// frees that handle while the editor may still be open. re-pin it
	// to the fresh menu font, or the digits draw with a freed one.
	if (_viv_zoom_edit_hwnd)
	{
		HFONT hfont;
		
		hfont = _viv_menu_font();
		
		if (hfont)
		{
			SendMessage(_viv_zoom_edit_hwnd,WM_SETFONT,(WPARAM)hfont,0);
		}
	}
}
static void _viv_zoom_edit_end(HWND hwnd,int apply)
{
	wchar_t wbuf[STRING_SIZE];
	RECT rect;
	POINT pt;
	int percent;
	
	// the guard stops the re-entries: the kill focus that the destroy
	// itself raises, and the status bar teardown cascading into a child
	// editor.
	if (hwnd != _viv_zoom_edit_hwnd)
	{
		return;
	}
	
	_viv_zoom_edit_hwnd = 0;
	
	if (apply)
	{
		GetWindowTextW(hwnd,wbuf,STRING_SIZE);
		
		percent = string_to_int(wbuf);
		
		// the ladder covers 1% .. 1600%: clamp anything above, ignore
		// empty or zero input (the old dialog's contract).
		if (percent > 1600)
		{
			percent = 1600;
		}
		
		if (percent >= 1)
		{
			GetClientRect(_viv_hwnd,&rect);
			pt.x = (rect.right - rect.left) / 2;
			pt.y = (_viv_get_view_top() + (rect.bottom - rect.top - _viv_get_status_high())) / 2;
			
			ClientToScreen(_viv_hwnd,&pt);
			
			_viv_zoom_set_percent(percent,pt.x,pt.y,0);
		}
	}
	
	// the keyboard goes home to the main window only while the editor
	// still owns it: a kill focus end means the focus already has a new
	// home (inside or outside the app) - stealing it back would make the
	// app a focus thief. (the kill focus this raises re-enters the guard
	// above.)
	if (GetFocus() == hwnd)
	{
		SetFocus(_viv_hwnd);
	}
	
	DestroyWindow(hwnd);
}
static LRESULT CALLBACK _viv_zoom_edit_proc(HWND hwnd,UINT msg,WPARAM wParam,LPARAM lParam)
{
	WNDPROC old_proc;
	
	old_proc = (WNDPROC)GetWindowLongPtr(hwnd,GWLP_USERDATA);
	
	switch(msg)
	{
		case WM_KEYDOWN:
		
			// enter commits, escape cancels: one short lived field, no
			// dialog machinery above it.
			if (wParam == VK_RETURN)
			{
				_viv_zoom_edit_end(hwnd,1);
				
				return 0;
			}
			
			if (wParam == VK_ESCAPE)
			{
				_viv_zoom_edit_end(hwnd,0);
				
				return 0;
			}
			
			break;
		
		case WM_KILLFOCUS:
		
			// focus went elsewhere: commit (the field never keeps a half
			// typed value).
			_viv_zoom_edit_end(hwnd,1);
			
			return 0;
		
		case WM_DESTROY:
		
			_viv_zoom_edit_hwnd = 0;
			
			break;
	}
	
	return CallWindowProc(old_proc,hwnd,msg,wParam,lParam);
}

void _viv_command_line_options(void)
{
	wchar_t *text_wbuf;
	wchar_t caption_wbuf[STRING_SIZE];

	text_wbuf = string_alloc_utf8("Usage:\nvoidImageViewer.exe [/switches] [filename(s)]\n"
		"\n"
		"Switches:\n"
		"/slideshow\tStart a slideshow.\n"
		"/fullscreen\tStart fullscreen.\n"
		"/maximized\tStart maximized.\n"
		"/window\t\tStart windowed.\n"
		"/ontop\t\tShow on top of other windows.\n"
		"/minimal\t\tBorderless window.\n"
		"/compact\t\tBordered window.\n"
		"/x <x> /y <y> /width <width> /height <height>\n"
		"\t\tSet the Window position and size.\n"
		"/rate <rate>\tSet the slideshow rate in milliseconds.\n"
		"/name\t\tSort by name.\n"
		"/path\t\tSort by full path.\n"
		"/size\t\tSort by size.\n"
		"/dm\t\tSort by date modified.\n"
		"/dc\t\tSort by date created.\n"
		"/ascending\tSort in ascending order.\n"
		"/descending\tSort in descending order.\n"
//		"/everything <search> Open files from an Everything search.\n"
//		"/random <search>\tOpen random files from an Everything search.\n"
		"/shuffle\t\tShuffle playlist.\n"
		"/<bmp|gif|ico|jpeg|jpg|png|tif|tiff|webp|emf|wmf>\n"
		"\t\tInstall association.\n"
		"/no<bmp|gif|ico|jpeg|jpg|png|tif|tiff|webp|emf|wmf>\n"
		"\t\tUninstall association.\n"
		"/appdata\t\tSave settings in appdata.\n"
		"/noappdata\tSave settings in exe path.\n"
		"/startmenu\tAdd Start menu shortcuts.\n"
		"/nostartmenu\tRemove Start menu shortcuts.\n"
		"/install <path>\tInstall to the specified path.\n"
		"/install-options <...> Run with the specified options after installation.\n"
		"/language <lang>\tSet the interface language: auto, english or chinese.\n"
		"/uninstall <path>\tUninstall from the specified path.\n");
		
	string_copy_utf8_string(caption_wbuf,localization_get_string(LOCALIZATION_ID_APP_NAME));

	// MB_ICONQUESTION avoids the messagebeep.
	viv_msgbox(_viv_hwnd,caption_wbuf,text_wbuf,MB_OK|MB_ICONQUESTION);
		
	mem_free(text_wbuf);
}
static void _viv_update_color_button_bitmap(HWND hwnd)
{	
	HBITMAP hbitmap;
	HDC screen_hdc;
	HDC mem_hdc;
	HGDIOBJ last_hbitmap;
	RECT rect;
	HBRUSH hbrush;
	COLORREF colorref;
	int wide;
	int high;
	
	wide = (64 * os_logical_wide) / 96;
	high = (13 * os_logical_high) / 96;
	
	colorref = (COLORREF)GetWindowLongPtr(hwnd,GWLP_USERDATA);
	
	screen_hdc = GetDC(0);
	mem_hdc = CreateCompatibleDC(screen_hdc);
	
	hbitmap = CreateCompatibleBitmap(screen_hdc,wide,high);

	last_hbitmap = SelectObject(mem_hdc,hbitmap);
	
	hbrush = CreateSolidBrush(colorref);

	rect.left = 1;
	rect.top = 1;
	rect.right = wide-1;
	rect.bottom = high-1;
	FillRect(mem_hdc,&rect,hbrush);
	ExcludeClipRect(mem_hdc,rect.left,rect.top,rect.right,rect.bottom);
	
	rect.left = 0;
	rect.top = 0;
	rect.right = wide;
	rect.bottom = high;
	FillRect(mem_hdc,&rect,(HBRUSH)GetStockObject(BLACK_BRUSH));

	DeleteObject(hbrush);
	
	SelectObject(mem_hdc,last_hbitmap);
	
	DeleteDC(mem_hdc);
	ReleaseDC(0,screen_hdc);
	
	last_hbitmap = (HBITMAP)SendMessage(hwnd,BM_SETIMAGE,IMAGE_BITMAP,(LPARAM)hbitmap);
	if (last_hbitmap)
	{
		DeleteObject(last_hbitmap);
	}
}
static void _viv_delete_color_button_bitmap(HWND hwnd)
{	
	HGDIOBJ last_hbitmap;
	
	last_hbitmap = (HBITMAP)SendMessage(hwnd,BM_SETIMAGE,IMAGE_BITMAP,0);
	
	if (last_hbitmap)
	{
		DeleteObject(last_hbitmap);
	}
}
static void _viv_jumpto_on_size(HWND hwnd)
{
	RECT rect;
	int wide;
	int high;
	int edit_high;
	int button_wide;
	int button_high;
	int y;
	int edge;
	int list_high;

	GetClientRect(hwnd,&rect);
	wide = rect.right - rect.left;
	high = rect.bottom - rect.top;

	GetWindowRect(GetDlgItem(hwnd,IDC_JUMPTO_EDIT),&rect);
	edit_high = rect.bottom - rect.top;

	GetWindowRect(GetDlgItem(hwnd,IDOK),&rect);
	button_wide = rect.right - rect.left;
	button_high = rect.bottom - rect.top;

	edge = (12 * os_logical_high) / 96;
	y = edge;

	SetWindowPos(GetDlgItem(hwnd,IDC_JUMPTO_EDIT),0,edge,y,wide-edge-edge,edit_high,SWP_NOACTIVATE|SWP_NOZORDER);

	y += edit_high + (6 * os_logical_high) / 96;

	high -= button_high + edge;

	list_high = (high - y) - edge;
	if (list_high < 0)
	{
		list_high = 0;
	}

	SetWindowPos(GetDlgItem(hwnd,IDC_JUMPTO_LIST),0,edge,y,wide-edge-edge,list_high,SWP_NOACTIVATE|SWP_NOZORDER);

	y += list_high + edge;

	SetWindowPos(GetDlgItem(hwnd,IDOK),0,wide - edge - button_wide - edge - button_wide,y,button_wide,button_high,SWP_NOACTIVATE|SWP_NOZORDER);
	SetWindowPos(GetDlgItem(hwnd,IDCANCEL),0,wide - edge - button_wide,y,button_wide,button_high,SWP_NOACTIVATE|SWP_NOZORDER);
}
static void _viv_jumpto_on_search(HWND hwnd)
{
	wchar_t search_wbuf[STRING_SIZE];
	int is_path_search;
	
	GetDlgItemText(hwnd,IDC_JUMPTO_EDIT,search_wbuf,STRING_SIZE);
	
	SendDlgItemMessage(hwnd,IDC_JUMPTO_LIST,WM_SETREDRAW,FALSE,0);
	
	ListBox_ResetContent(GetDlgItem(hwnd,IDC_JUMPTO_LIST));
	
	is_path_search = 0;
	
	{
		const wchar_t *p;
		
		p = search_wbuf;

		while(*p)
		{
			if ((*p == '\\') || (*p == '/') || (*p == ':'))
			{
				is_path_search = 1;
				break;
			}
			
			p++;
		}
	}
	
	{
		int i;
		
		for(i=0;i<_viv_nav_item_count;i++)
		{
			const wchar_t *name;
			const wchar_t *search_name;
			
			name = string_get_filename_part(_viv_nav_items[i]->fd.cFileName);
			
			if (is_path_search)
			{
				search_name = _viv_nav_items[i]->fd.cFileName;
			}
			else
			{
				search_name = name;
			}
			
			if ((!(*search_wbuf)) || (StrStrI(search_name,search_wbuf)))
			{
				int lb_index;
				
				lb_index = ListBox_AddString(GetDlgItem(hwnd,IDC_JUMPTO_LIST),string_get_filename_part(name));
				
				ListBox_SetItemData(GetDlgItem(hwnd,IDC_JUMPTO_LIST),lb_index,i);
			}
		}
	}
	
	ListBox_SetCurSel(GetDlgItem(hwnd,IDC_JUMPTO_LIST),0);
	
	SendDlgItemMessage(hwnd,IDC_JUMPTO_LIST,WM_SETREDRAW,TRUE,0);
}
static void _viv_jumpto_open_sel(HWND hwnd)
{
	int lb_index;
	
	lb_index = ListBox_GetCurSel(GetDlgItem(hwnd,IDC_JUMPTO_LIST));

	if (lb_index != LB_ERR)
	{	
		int index;
		
		index = ListBox_GetItemData(GetDlgItem(hwnd,IDC_JUMPTO_LIST),lb_index);
	
		if ((index >= 0) && (index < _viv_nav_item_count))
		{
			_viv_open(&_viv_nav_items[index]->fd,0);
		}
	}
}
static LRESULT CALLBACK _viv_jumpto_proc(HWND hwnd,UINT msg,WPARAM wParam,LPARAM lParam)
{
	{
		INT_PTR dark_dialog_reply;
		
		dark_dialog_reply = _viv_dialog_dark_proc(hwnd,msg,wParam,lParam);
		
		if (dark_dialog_reply != -1)
		{
			return dark_dialog_reply;
		}
	}
	
	switch(msg)
	{
		case WM_INITDIALOG:
			// dark chrome: title bar and dark explorer control style.
			_viv_dark_dialog(hwnd);
		

			{
				int cur_index;
				
				cur_index = LB_ERR;

				os_SetWindowText_localization_id(hwnd,LOCALIZATION_ID_JUMP_TO_TITLE);
				os_SetDlgItemText_localization_id(hwnd,IDOK,LOCALIZATION_ID_OK_BUTTON);
				os_SetDlgItemText_localization_id(hwnd,IDCANCEL,LOCALIZATION_ID_CANCEL_BUTTON);
					
				os_center_dialog(hwnd);
				
				_viv_nav_item_free_all();
				
				if (_viv_playlist_start)
				{
					_viv_playlist_t *d;
					
					d = _viv_playlist_start;
					while(d)
					{
						_viv_nav_item_add(&d->fd);
					
						d = d->next;
					}
				}
				else
				if (*_viv_current_fd->cFileName)
				{		
					WIN32_FIND_DATA fd;
					HANDLE h;
					wchar_t search_wbuf[STRING_SIZE];
					wchar_t path_wbuf[STRING_SIZE];

					string_get_path_part(path_wbuf,_viv_current_fd->cFileName);
					
					string_copy(search_wbuf,path_wbuf);
					string_cat_utf8(search_wbuf,(const utf8_t *)"\\*.*");

					h = FindFirstFile(search_wbuf,&fd);
					
					if (h != INVALID_HANDLE_VALUE)
					{
						for(;;)
						{
							if (_viv_is_valid_filename(&fd))
							{
								string_path_combine(search_wbuf,path_wbuf,fd.cFileName);
								string_copy_with_bufsize(fd.cFileName,MAX_PATH,search_wbuf);
								
								_viv_nav_item_add(&fd);
							}
							
							if (!FindNextFile(h,&fd)) break;
						}

						FindClose(h);
					}
				}
				
				if (_viv_nav_item_count)
				{
					_viv_nav_item_t **d;
					_viv_nav_item_t *navitem;
					
					_viv_nav_items = (_viv_nav_item_t **)mem_alloc(safe_size_mul_sizeof_pointer((SIZE_T)_viv_nav_item_count));
					d = _viv_nav_items;
					
					navitem = __viv_nav_item_start;
					while(navitem)
					{
						*d++ = navitem;
						
						navitem = navitem->next;
					}
				}
				
				// sort by name 
				os_qsort(_viv_nav_items,_viv_nav_item_count,_viv_nav_compare);
				
				// find current.
				{
					_viv_nav_item_t **p;
					SIZE_T run;
					int index;
					
					p = _viv_nav_items;
					run = _viv_nav_item_count;
					index = 0;
					
					while(run)
					{
						if (string_compare((*p)->fd.cFileName,_viv_current_fd->cFileName) == 0)
						{
							cur_index = index;
						}
					
						index++;
						p++;
						run--;
					}
				}
				
				_viv_jumpto_on_size(hwnd);
				_viv_jumpto_on_search(hwnd);
				
				if (cur_index != LB_ERR)
				{
					ListBox_SetCurSel(GetDlgItem(hwnd,IDC_JUMPTO_LIST),cur_index);
					
					_viv_center_listbox_item(GetDlgItem(hwnd,IDC_JUMPTO_LIST),cur_index);
				}
			}
			
			return TRUE;
			
		case WM_COMMAND:
		
			switch(LOWORD(wParam))
			{
				case IDC_JUMPTO_EDIT:
					if (HIWORD(wParam) == EN_CHANGE)
					{
						_viv_jumpto_on_search(hwnd);
						break;
					}
					break;

				case IDC_JUMPTO_LIST:
					if (HIWORD(wParam) == LBN_DBLCLK)
					{
						_viv_jumpto_open_sel(hwnd);
						_viv_jump_ret = 1;
						break;
					}
					break;
					
				case IDOK:
					_viv_jumpto_open_sel(hwnd);
					_viv_jump_ret = 1;
					break;

				case IDCANCEL:
					_viv_jump_ret = 1;
					break;
			}
			
			break;
		
		case WM_SIZE:
			_viv_jumpto_on_size(hwnd);
			break;

		case WM_CLOSE:
			_viv_jump_ret = 1;
			return TRUE;
	    
		case WM_DESTROY:
			_viv_nav_item_free_all();
			break;
	}
	
	return FALSE;
}
void _viv_show_jumpto(void)
{
	HWND dialog_hwnd;
	
	_viv_jump_ret = 0;
	
	dialog_hwnd = CreateDialog(os_hinstance,MAKEINTRESOURCE(IDD_JUMPTO),_viv_hwnd,_viv_jumpto_proc);
	
	if (dialog_hwnd)
	{
		ShowWindow(dialog_hwnd,SW_SHOW);
		EnableWindow(_viv_hwnd,FALSE);

		while(!_viv_jump_ret)
		{
			MSG msg;
			
			if (!GetMessageW(&msg,NULL,0,0))
			{
				// the modal pump consumed a WM_QUIT (GetMessage returned 0):
				// re-inject it so the main message loop terminates instead of
				// blocking in WaitMessage forever.
				PostQuitMessage((int)msg.wParam);
				break;
			}
			
			if ((msg.hwnd == dialog_hwnd) || (IsChild(dialog_hwnd,msg.hwnd)))
			{
				switch (msg.message)
				{
					case WM_MOUSEWHEEL:
						
						SendMessage(GetDlgItem(dialog_hwnd,IDC_JUMPTO_LIST),WM_MOUSEWHEEL,msg.wParam,msg.lParam);
						continue;
						
					case WM_SYSKEYDOWN:
					case WM_KEYDOWN:
					
						if (msg.hwnd == GetDlgItem(dialog_hwnd,IDC_JUMPTO_EDIT))
						{
							int key_flags;
							int vk;
							
							key_flags = _viv_get_current_key_mod_flags();
							vk = msg.wParam;
							
							switch(vk)
							{
								case VK_UP:
								case VK_DOWN:
								case VK_NEXT:
								case VK_PRIOR:
									
									SendMessage(GetDlgItem(dialog_hwnd,IDC_JUMPTO_LIST),msg.message,msg.wParam,msg.lParam);

									
									continue;
							}
						}
						
						break;
							
				}
			}
			
			if (!IsDialogMessageW(dialog_hwnd,&msg))
			{
				TranslateMessage(&msg);
				DispatchMessageW(&msg);
			}
		}
		
		EnableWindow(_viv_hwnd,TRUE);
		DestroyWindow(dialog_hwnd);
	}
}
static void _viv_center_listbox_item(HWND listbox_hwnd,int item_index)
{
    int item_high;
    
    item_high = ListBox_GetItemHeight(listbox_hwnd,item_index);
    
    if (item_high > 0)
    {
		RECT client_rect;
		int visible_count;
		
		GetClientRect(listbox_hwnd,&client_rect);

		visible_count = (client_rect.bottom - client_rect.top) / item_high;
		
		if (visible_count > 0)
		{
			int new_top;
			int item_count;
			
			item_count = ListBox_GetCount(listbox_hwnd);

			new_top = item_index - (visible_count / 2);
			
			if (new_top < 0)
			{
				new_top = 0;
			}

			if (new_top > item_count - visible_count)
			{
				new_top = item_count - visible_count;
			}
			
			if (new_top < 0)
			{
				new_top = 0;
			}

			ListBox_SetTopIndex(listbox_hwnd,new_top);
		}
	}
}											
