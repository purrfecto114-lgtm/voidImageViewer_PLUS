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
// viv_install.c - install/uninstall, associations, registry, shortcuts, elevation.
// Pure physical move from viv.c (R70 split): bodies are unchanged.
#include "viv.h"
#include "viv_state.h"
#include "viv_install.h"
#include "viv_msgbox.h"

// forward declarations (order preserved from viv.c)
int _viv_process_install_command_line_options(wchar_t *cl);
void _viv_install_association_by_extension(const char *association,const char *description,const char *icon_location,int force);
void _viv_uninstall_association_by_extension(const char *association);
int _viv_is_association(const char *association);
static int _viv_is_foreign_association(const char *association,const wchar_t *class_name);
static int _viv_get_registry_string(HKEY hkey,const utf8_t *value,wchar_t *wbuf,int size_in_wchars);
static int _viv_set_registry_string(HKEY hkey,const utf8_t *value,const wchar_t *wbuf);
static void _viv_install_association(DWORD flags);
static void _viv_install_class_definition_by_extension(const char *association,const char *description,const char *icon_location);
static void _viv_install_app_registration(void);
static void _viv_uninstall_app_registration(void);
static void _viv_uninstall_association(DWORD flags);
static int _viv_is_voidimageviewer_process(DWORD process_id);
static void _viv_close_existing_process(void);
static void _viv_uninstall_delete_file(const wchar_t *path,const utf8_t *filename);
static void _viv_uninstall_sweep_config_temps(const wchar_t *path);
int _viv_is_start_menu_shortcuts(void);
static void _viv_install_start_menu_shortcuts(void);
static void _viv_uninstall_start_menu_shortcuts(void);
void _viv_append_admin_param(wchar_t *wbuf,const utf8_t *param);
static void _viv_install_add_remove_programs(const wchar_t *install_path);
static void _viv_uninstall_add_remove_programs(void);
static void _viv_install_copy_file(const wchar_t *install_path,const wchar_t *temp_path,const utf8_t *filename,int critical);
void _viv_get_exe_filename(wchar_t filename[STRING_SIZE]);
static int _viv_install_path_user_writable(const wchar_t *install_path);
static int _viv_install_hklm_arp_present(void);
static void _viv_install_association_locked_box(DWORD install_flags);


int _viv_process_install_command_line_options(wchar_t *cl)
{
	wchar_t *p;
	wchar_t buf[STRING_SIZE];
	DWORD install_flags;
	DWORD uninstall_flags;
	int appdata;
	int is_runas;
	wchar_t install_path[STRING_SIZE];
	wchar_t install_options[STRING_SIZE];
	wchar_t uninstall_path[STRING_SIZE];
	int startmenu;
	int language;
	int language_set;
	int hardware_acceleration;
	wchar_t *cl_start;
	int is_admin_install;
	int is_standard_user_install;
	int extension_word_seen;
	int uninstall_keep_settings;
	
	startmenu = 0;
	install_flags = 0;
	uninstall_flags = 0;
	appdata = 0;
	is_runas = 0;
	is_admin_install = 0;
	is_standard_user_install = 0;
	extension_word_seen = 0;
	uninstall_keep_settings = 0;
	language = config_language;
	language_set = 0;
	hardware_acceleration = 0;
	install_path[0] = 0;
	install_options[0] = 0;
	uninstall_path[0] = 0;

	p = string_skip_ws(cl);
	
	// skip exe filename
	p = string_get_word(p,buf,STRING_SIZE);
	p = string_skip_ws(p);

	cl_start = p;

	// skip first parameter.
	for(;;)
	{
		wchar_t *bufstart;
		BOOL was_quote;
		
		// no more text?
		if (!*p)
		{
			break;
		}
		
		was_quote = FALSE;
		if (*p == '"')
		{
			was_quote = TRUE;
		}
		
		p = string_get_word(p,buf,STRING_SIZE);
		p = string_skip_ws(p);
		
		bufstart = buf;
		
		// treat switches with a '.' as a filename.
		// eg: voidimageviewer.exe -my-image.png
		if ((!was_quote) && ((*bufstart == '/') || (*bufstart == '-')) && (!string_is_dot(buf)))
		{
			bufstart++;
			
			if (string_icompare_lowercase_ascii(bufstart,"install") == 0)
			{
				p = string_get_word(p,install_path,STRING_SIZE);
				p = string_skip_ws(p);				

				uninstall_path[0] = 0;

				is_admin_install = 1;
			}
			else
			if (string_icompare_lowercase_ascii(bufstart,"install-options") == 0)
			{
				p = string_get_word(p,install_options,STRING_SIZE);
				p = string_skip_ws(p);				

				is_admin_install = 1;
			}
			else
			if (string_icompare_lowercase_ascii(bufstart,"uninstall") == 0)
			{
				p = string_get_word(p,uninstall_path,STRING_SIZE);
				p = string_skip_ws(p);			
				
				// no uninstall path?
				if (!uninstall_path[0])	
				{
					// caller will need to manually delete voidimageviewer.exe
					string_get_exe_path(uninstall_path);
				}

				// uninstall all
				uninstall_flags = 0xffffffff;
				install_flags = 0;
				startmenu = -1;

				install_path[0] = 0;

				is_admin_install = 1;
				is_standard_user_install = 1;
			}
			else
				if (string_icompare_lowercase_ascii(bufstart,"uninstall-keep-settings") == 0)
				{
					// the silent uninstall's answer to the settings ask: keep (the
					// visible path asks the question; a scripted uninstall must
					// not hang on a dialog it cannot see).
					uninstall_keep_settings = 1;
			}
			else
			if (string_icompare_lowercase_ascii(bufstart,"appdata") == 0)
			{
				appdata = 1;
				is_admin_install = 1;
			}
			else
			if (string_icompare_lowercase_ascii(bufstart,"noappdata") == 0)
			{
				appdata = -1;
				is_admin_install = 1;
			}
			else
			if (string_icompare_lowercase_ascii(bufstart,"startmenu") == 0)
			{
				startmenu = 1;
				is_admin_install = 1;
			}
			else
			if (string_icompare_lowercase_ascii(bufstart,"nostartmenu") == 0)
			{
				startmenu = -1;
				is_admin_install = 1;
			}
			else
			if (string_icompare_lowercase_ascii(bufstart,"hardware-acceleration") == 0)
			{
				hardware_acceleration = 1;
				is_admin_install = 1;
			}
			else
			if (string_icompare_lowercase_ascii(bufstart,"language") == 0)
			{
				wchar_t language_wbuf[STRING_SIZE];
				
				p = string_get_word(p,language_wbuf,STRING_SIZE);
				p = string_skip_ws(p);
				
				// map the language name to a config_language value.
				// 0 = auto (system), 1 = english, 2 = simplified chinese.
				// this does not require admin rights, the setting is stored in the ini.
				
				if (string_icompare_lowercase_ascii(language_wbuf,"english") == 0)
				{
					language = 1;
					language_set = 1;
				}
				else
				if (string_icompare_lowercase_ascii(language_wbuf,"chinese") == 0)
				{
					language = 2;
					language_set = 1;
				}
				else
				if (string_icompare_lowercase_ascii(language_wbuf,"auto") == 0)
				{
					language = 0;
					language_set = 1;
				}
			}
			else
			if (string_icompare_lowercase_ascii(bufstart,"isrunas") == 0)
			{
				is_runas = 1;
			}
			else
			{
				int exti;
				int is_no;
				
				is_no = 0;
			
				if (string_istartwith_lowercase_ascii(bufstart,"no"))
				{
					bufstart += 2;
					is_no = 1;
				}
				
				for(exti=0;exti<_VIV_ASSOCIATION_COUNT;exti++)
				{
					if (string_icompare_lowercase_ascii(bufstart,_viv_association_extensions[exti]) == 0)
					{
						if (is_no)
						{
							uninstall_flags |= 1 << exti;
						}
						else
						{
							install_flags |= 1 << exti;
						}
						
						is_standard_user_install = 1;
						extension_word_seen = 1;
						break;
					}
				}
			}
		}
	}
	
	// debug_printf("isrunas %d install_flags %u uninstall_flags %u\n",is_runas,install_flags,uninstall_flags);

	// do associationas as standard user
	if (!is_runas)
	{
		if (install_flags)
		{
			_viv_install_association(install_flags);
			
			// rc.6: the honest read-back. the keys above are ours the
			// moment they are written, but the question the user asks is
			// "does a double click open the viewer" - and on windows 10/11
			// that answer lives in the UserChoice hash. the box says so
			// once for the whole run (the settings page's per-extension
			// shape would ask once per extension - nineteen times
			// that can change it.
			_viv_install_association_locked_box(install_flags);
		}
	
		if (uninstall_flags)
		{
			_viv_uninstall_association(uninstall_flags);
		}
		
		// rc.4: the app registration rides the same user-mode pass the
		// associations do (the elevated relay would land it in the
		// relaying account's hive). the gate is the extension word
		// itself - the /uninstall switch also raises the standard
		// user flag, and re-registering an app mid-uninstall would
		// undo the sweep it is running.
		if (extension_word_seen)
		{
			_viv_install_app_registration();
		}
	}
		
	if (is_admin_install)
	{
		if (!os_is_admin())
		{
			if (!is_runas)
			{
				// rc.6: the relay exists for the seats a standard token
				// cannot write - program files, the all-users start
				// menu, the hklm add/remove entry. three runs never
				// need it: an install into a directory this token owns
				// (the setup's per-user default), an uninstall of a
				// per-user install, and the settings helper without
				// the start menu word. relaying any of them moved the
				// per-user work into the relaying account's hive, and a
				// user without an admin password saw the refused
				// elevation read as success (the field report: the
				// install "completed", nothing was copied, nothing was
				// associated).
				int relay;

				relay = 1;

				if ((install_path[0]) && (_viv_install_path_user_writable(install_path)))
				{
					// this process installs it all: the per-user
					// add/remove entry, the per-user shortcuts, the
					// right hive's ini.
					relay = 0;
				}
				else
				if ((uninstall_path[0]) && (!_viv_install_hklm_arp_present()))
				{
					// the per-user uninstall: the hklm entry is the
					// admin install's footprint - its absence is the
					// witness that every seat the sweep touches is
					// user owned.
					relay = 0;
				}
				else
				if ((!install_path[0]) && (!uninstall_path[0]) && (!startmenu))
				{
					// the settings helper without the start menu word:
					// the appdata flag is pure per-user work (the ini).
					relay = 0;
				}

				if (relay)
				{
					wchar_t exe_filename[STRING_SIZE];
					wchar_t params[STRING_SIZE];
					wchar_t cwd[STRING_SIZE];
					
					_viv_get_exe_filename(exe_filename);
					GetCurrentDirectory(STRING_SIZE,cwd);
					
					string_copy_utf8_string(params,(const utf8_t *)"/isrunas ");
					string_cat(params,cl_start);
					
					// rc.6: a refused elevation is a failed install - the
					// waiter (the nsis phase) must not read it as success.
					if (!os_shell_execute(0,exe_filename,1,"runas",params))
					{
						return 2;
					}
					
					return 1;
				}
			}
		}
	}
	
	if (language_set)
	{
		config_language = language;
		
		// save the language selection to the current settings location.
		// (before the appdata handling below so that its saves include the new language)
		config_save_settings(config_appdata);
	}
	
	if ((hardware_acceleration) && (_viv_hwd3d_available()))
	{
		// the installer's hardware acceleration box: the switch only
		// ever names direct3d (the exe default stays gdi; an unchecked
		// box sends no switch at all, so an upgrade keeps the renderer
		// the ini already carries). saved before the appdata handling
		// below so its saves include the new renderer. the probe gates
		// the save: a machine that cannot bring direct3d up keeps the
		// gdi default instead of promising a backend the first paint
		// would have to fall back from (the field round's detection
		// question).
		config_renderer = CONFIG_RENDERER_DIRECT3D;
		
		config_save_settings(config_appdata);
	}
	
	if (appdata > 0)
	{	
		config_appdata = 1;
		
		config_save_settings(config_appdata);

		// appdata enabled.
		config_save_settings(0);
	}
	else
	if (appdata < 0)
	{
		config_appdata = 0;
		
		// appdata disabled
		config_save_settings(0);
	}
	
	if (startmenu > 0)
	{
		_viv_install_start_menu_shortcuts();
	}
	else
	if (startmenu < 0)
	{
		_viv_uninstall_start_menu_shortcuts();
	}
	
	if (install_path[0])
	{
		wchar_t temp_path[STRING_SIZE];
		
		// make sure no other process is running.
		_viv_close_existing_process();
		
		string_get_exe_path(temp_path);
		
		os_make_sure_path_exists(install_path);
		
		_viv_install_copy_file(install_path,temp_path,(const utf8_t *)"voidImageViewer.exe",1);
		_viv_install_copy_file(install_path,temp_path,(const utf8_t *)"Uninstall.exe",0);
		_viv_install_copy_file(install_path,temp_path,(const utf8_t *)"LICENSE",0);
		_viv_install_copy_file(install_path,temp_path,(const utf8_t *)"THIRD_PARTY_NOTICES.md",0);

		// changes.txt left the installed payload (the size round: a
		// quarter megabyte of archive the release notes already carry,
		// on a machine that never asked for it) - an upgrade over an
		// older install still cleans the copy that older setup laid
		// down.
		_viv_uninstall_delete_file(install_path,(const utf8_t *)"Changes.txt");
		
		// register in add/remove programs so the app shows up in
		// programs and features.
		_viv_install_add_remove_programs(install_path);
		
		if (install_options[0])
		{
			wchar_t new_exe_filename_wbuf[STRING_SIZE];
			
			string_path_combine_utf8(new_exe_filename_wbuf,install_path,(const utf8_t *)"voidImageViewer.exe");
			
			// rc.11: the quote scan that lived here was dead code - the
			// tokenizer that filled this buffer eats quotes while it
			// builds the word, so no quote ever arrived to be found, and
			// the word it left behind kept its spaces: forwarded whole
			// to the elevated child, those spaces re-tokenize into words
			// of the caller's choosing ("-install-options "-render-export
			// C:\Windows\..."" would reach an admin CreateFileW). the
			// caller's string never rides the relay whole again: this side
			// re-tokenizes the buffer, keeps only the switches the nsis
			// wizard itself accumulates, re-emits each one in canonical
			// /word form, and drops every other word silently (the nsis
			// side's scan of the raw command line is the first belt - it
			// sees the quotes before any tokenizing does; this literal
			// whitelist is the second, and it trusts no word it cannot
			// spell itself).
			{
				static const char *allowed[] = {"appdata","noappdata",
				"startmenu","nostartmenu","hardware-acceleration"};
				wchar_t rebuilt[STRING_SIZE];
				wchar_t word[STRING_SIZE];
				wchar_t *q;
				int i;
				
				rebuilt[0] = 0;
				q = install_options;

				for(;;)
				{
					wchar_t *word_body;

					q = string_skip_ws(q);

					if (!*q)
					{
						break;
					}

					q = string_get_word(q,word,STRING_SIZE);

					// one leading slash is the wizard's switch spelling:
					// the whitelist compare runs on what follows it.
					word_body = word;

					if (*word_body == '/')
					{
						word_body++;
					}

					for(i=0;i<(int)(sizeof(allowed) / sizeof(allowed[0]));i++)
					{
						if (string_icompare_lowercase_ascii(word_body,allowed[i]) == 0)
						{
							string_cat_utf8(rebuilt,(const utf8_t *)" /");
							string_cat_utf8(rebuilt,(const utf8_t *)allowed[i]);

							break;
						}
					}
				}

				string_copy(install_options,rebuilt);
			}
			
			// rc.6: the un-relayed leg launches the second stage on
			// this process's own token - the /isrunas word tells it
			// not to raise uac for the options (the hive it writes is
			// already the right one; the word's other job, skipping
			// the association pass, is a no-op here - the extension
			// words ride the nsis phase c).
			if (!os_is_admin())
			{
				string_cat_utf8(install_options,(const utf8_t *)" /isrunas");
			}

			os_shell_execute(0,new_exe_filename_wbuf,1,NULL,install_options);
		}
	}
	
	if (uninstall_path[0])
	{
		wchar_t path[STRING_SIZE];
		int delete_settings;

		// make sure no other process is running.
		_viv_close_existing_process();

		// remove our add/remove programs entry (from both hives).
		_viv_uninstall_add_remove_programs();

		// rc.4: the app registration goes with it.
		_viv_uninstall_app_registration();

		// a legacy autolaunch value an old install may have written (the
		// settings-open pass retires it too - the uninstall owes the same
		// sweep: it is install footprint, not user settings, so it goes
		// whatever the settings answer says).
		{
			HKEY run_hkey;

			if (RegOpenKeyExW(HKEY_CURRENT_USER,L"Software\\Microsoft\\Windows\\CurrentVersion\\Run",0,KEY_SET_VALUE,&run_hkey) == ERROR_SUCCESS)
			{
				RegDeleteValueW(run_hkey,L"voidImageViewerPLUS");

				RegCloseKey(run_hkey);
			}
		}

		// the settings ask (the residue answer): the old sweep deleted
		// the user's settings silently and unconditionally. keep is the
		// default - the recoverable answer - and the silent uninstall's
		// word carries it.
		delete_settings = 0;

		if (!uninstall_keep_settings)
		{
			wchar_t caption_wbuf[STRING_SIZE];
			wchar_t message_wbuf[STRING_SIZE];

			string_copy_utf8_string(caption_wbuf,localization_get_string(LOCALIZATION_ID_UNINSTALL_KEEP_SETTINGS_CAPTION));
			string_copy_utf8_string(message_wbuf,localization_get_string(LOCALIZATION_ID_UNINSTALL_KEEP_SETTINGS_MESSAGE));

			if (viv_msgbox(0,caption_wbuf,message_wbuf,MB_YESNO|MB_ICONQUESTION) == IDNO)
			{
				delete_settings = 1;
			}
		}

		// remove %APPDATA%\voidimageviewer (the settings home in appdata
		// mode - the ask gates the ini; the failed-save temps are
		// garbage, never settings).
		if (string_get_appdata_voidimageviewer_path(path))
		{
			if (delete_settings)
			{
				_viv_uninstall_delete_file(path,(const utf8_t *)"voidImageViewer.ini");
			}

			_viv_uninstall_sweep_config_temps(path);

			RemoveDirectory(path);
		}

		_viv_uninstall_delete_file(uninstall_path,(const utf8_t *)"Uninstall.exe");
		_viv_uninstall_delete_file(uninstall_path,(const utf8_t *)"Changes.txt");
		_viv_uninstall_delete_file(uninstall_path,(const utf8_t *)"LICENSE");
		_viv_uninstall_delete_file(uninstall_path,(const utf8_t *)"THIRD_PARTY_NOTICES.md");

		// the install dir's own ini: the appdata=0 shape is the settings
		// themselves (kept when the answer says keep - the directory then
		// survives holding exactly what the user kept); the appdata=1
		// shape is the one-line pointer to the real home (install
		// footprint - it goes so the directory can).
		{
			int is_pointer_ini;

			is_pointer_ini = 0;

			string_path_combine_utf8(path,uninstall_path,(const utf8_t *)"voidImageViewer.ini");

			{
				ini_t *ini;

				ini = ini_open(path,(const utf8_t *)"voidImageViewer");

				if (ini)
				{
					if (ini_get_int(ini,(const utf8_t *)"appdata",0) == 1)
					{
						is_pointer_ini = 1;
					}

					ini_close(ini);
				}
			}

			if ((delete_settings) || (is_pointer_ini))
			{
				_viv_uninstall_delete_file(uninstall_path,(const utf8_t *)"voidImageViewer.ini");
			}
		}

		// the temps ride the install dir too (the appdata=0 writers leave
		// them beside the ini).
		_viv_uninstall_sweep_config_temps(uninstall_path);

		_viv_uninstall_delete_file(uninstall_path,(const utf8_t *)"voidImageViewer.exe");

		RemoveDirectory(uninstall_path);
	}
	
	if ((is_admin_install) || (is_standard_user_install))
	{
		return 1;
	}
	
	return 0;
}
// use the default class description, ie: TXT File
// the canonical windows classes for the guarded extensions - the
// owners a fresh system carries before any viewer claims them. the
// jpg family ships as jpegfile on every stock windows (there is no
// stock jpgfile class); the round-148 correction - the old
// "jpgfile" spelling read every stock machine as a foreign viewer
// and the takeover silently refused, the field report's
// "never associates" trio.
static const char *_viv_canonical_class_for(const char *association)
{
	static const char *canonical_extensions[] = {"bmp","jpg","jpeg"};
	static const char *canonical_classes[] = {"bmpfile","jpegfile","jpegfile"};
	wchar_t association_wbuf[STRING_SIZE];
	int i;

	string_copy_utf8_string(association_wbuf,(const utf8_t *)association);

	for(i=0;i<3;i++)
	{
		if (string_icompare_lowercase_ascii(association_wbuf,canonical_extensions[i]) == 0)
		{
			return canonical_classes[i];
		}
	}

	return 0;
}

static int _viv_is_foreign_association(const char *association,const wchar_t *class_name)
{
	const char *canonical_class;
	wchar_t key[STRING_SIZE];
	HKEY hkey;

	canonical_class = _viv_canonical_class_for(association);

	if (!canonical_class)
	{
		return 0;
	}

	// the effective owner is read from the merged view the shell
	// resolves: HKEY_CLASSES_ROOT is the per-user software\classes
	// over the machine ones, so a per-user owner (ours or a foreign
	// one) shadows the machine default exactly as the shell sees it.
	string_copy_utf8_string(key,(const utf8_t *)".");
	string_cat_utf8(key,association);

	if (RegOpenKeyExW(HKEY_CLASSES_ROOT,key,0,KEY_QUERY_VALUE,&hkey) == ERROR_SUCCESS)
	{
		wchar_t wbuf[STRING_SIZE];
		int ret;

		ret = 0;

		if ((_viv_get_registry_string(hkey,0,wbuf,STRING_SIZE)) && (*wbuf))
		{
			// an empty default (no owner yet), the canonical class
			// (the windows default) or our own class installs as
			// before; anything else is a foreign viewer the user
			// chose, and the extension is left alone.
			if ((string_icompare_lowercase_ascii(wbuf,canonical_class) != 0) && (string_compare(wbuf,class_name) != 0))
			{
				ret = 1;
			}
		}

		RegCloseKey(hkey);

		return ret;
	}

	return 0;
}


// rc.17: the honest read the checkbox never had. the per-user class
// registration answers "did we write our keys" (always true right
// after the install); the question the user asks is "does a double
// click open the viewer" - and on windows 10/11 that answer lives in
// the UserChoice hash, not in the keys we wrote. this reads the
// choice the shell honors: the ProgId under FileExts\.ext\UserChoice.
//
// the two blind spots of the raw read both answer the right way for
// this ask: a missing UserChoice means no lock exists (the per-user
// takeover above is in force - nothing to warn about), and a dangling
// ProgId (an uninstalled app the user once picked) reads as "locked
// elsewhere", which points the user at the settings page - exactly
// where a broken default gets fixed. the QueryCurrentDefault com
// call would close neither gap better: it resolves a dangling ProgId
// to itself, and it needs com initialized on paths that run before
// os_init.
int _viv_default_app_locked_elsewhere(const char *association)
{
	wchar_t dot_association[STRING_SIZE];
	char class_name[STRING_SIZE];
	wchar_t key[STRING_SIZE];
	HKEY hkey;
	
	string_copy_utf8_string(dot_association,(const utf8_t *)".");
	string_cat_utf8(dot_association,association);
	
	// the progid this build registers is narrow ascii from nose to
	// tail ("voidImageViewer." plus the extension), and the compare
	// below reads its second argument as narrow bytes - and as raw
	// bytes: the helper lowercases only its first argument, so the
	// second must arrive already lowercase. the wide buffer this
	// used to build failed the first contract (wchar_t storage read
	// byte by byte), and a mixed-case spelling would fail the
	// second just as finally - either way every read answered
	// "locked elsewhere" (the honest read's own false positive).
	{
		const char *s;
		char *d;

		s = "voidimageviewer.";
		d = class_name;

		while(*s)
		{
			*d++ = *s++;
		}

		s = association;

		while(*s)
		{
			*d++ = *s++;
		}

		*d = 0;
	}
	
	string_copy_utf8_string(key,(const utf8_t *)"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\FileExts\\");
	string_cat(key,dot_association);
	string_cat_utf8(key,"\\UserChoice");
	
	LONG open_ret;

	open_ret = RegOpenKeyExW(HKEY_CURRENT_USER,key,0,KEY_QUERY_VALUE,&hkey);

	if (open_ret == ERROR_SUCCESS)
	{
		wchar_t wbuf[STRING_SIZE];
		int ret;
		
		ret = 0;
		
		// the ProgId value names the app the shell launches for a
		// double click; the comparison is case insensitive (the
		// registry preserves case, the shell does not honor it).
		if ((_viv_get_registry_string(hkey,(const utf8_t *)"ProgId",wbuf,STRING_SIZE)) && (*wbuf))
		{
			if (string_icompare_lowercase_ascii(wbuf,class_name) != 0)
			{
				ret = 1;
			}
		}
		
		RegCloseKey(hkey);
		
		return ret;
	}
	
	// the third answer: a read the system refused is not a missing
	// lock. the managed profiles (mdm and friends) deny the
	// userchoice read while the shell still honors it - answering
	// "takeover effective" there signs what nobody verified. the
	// refused read lands with the lock: the box it raises is the
	// honest face (the settings page is where a locked default
	// gets sorted either way).
	if (open_ret == ERROR_ACCESS_DENIED)
	{
		return 1;
	}

	return 0;
}
// the progid side of an association: the default icon, the description,
// the open command and the pinned default verb. extracted from the
// takeover path so the app registration can pre-write every class the
// capabilities block references (a dangling progid there would read as
// a broken entry on the default apps page).
static void _viv_install_class_definition_by_extension(const char *association,const char *description,const char *icon_location)
{
	HKEY hkey;
	wchar_t class_name[STRING_SIZE];
	wchar_t key[STRING_SIZE];
	wchar_t default_icon[STRING_SIZE];
	wchar_t dot_association[STRING_SIZE];
	
	string_copy_utf8_string(dot_association,(const utf8_t *)".");
	string_cat_utf8(dot_association,association);
	
	string_copy_utf8_string(class_name,"voidImageViewer");
	string_cat(class_name,dot_association);
	
	string_copy_utf8_string(default_icon,"SOFTWARE\\Classes\\voidImageViewer");
	string_cat(default_icon,dot_association);
	string_cat_utf8(default_icon,"\\DefaultIcon");

	if (RegCreateKeyExW(HKEY_CURRENT_USER,default_icon,0,0,0,KEY_QUERY_VALUE|KEY_SET_VALUE,0,&hkey,0) == ERROR_SUCCESS)
	{
		if (icon_location)
		{
			wchar_t icon_location_wbuf[STRING_SIZE];
			
			string_copy_utf8_string(icon_location_wbuf,icon_location);

			_viv_set_registry_string(hkey,0,icon_location_wbuf);
		}
		else
		{
			wchar_t filename[STRING_SIZE];
			wchar_t command[STRING_SIZE];

			_viv_get_exe_filename(filename);
			
			string_copy(command,filename);

			string_cat_utf8(command,(const utf8_t *)",0");
			
			_viv_set_registry_string(hkey,0,command);
		}
			
		RegCloseKey(hkey);
	}	
		
	string_copy_utf8_string(key,"SOFTWARE\\Classes\\");
	string_cat(key,class_name);
	
	if (RegCreateKeyExW(HKEY_CURRENT_USER,key,0,0,0,KEY_QUERY_VALUE|KEY_SET_VALUE,0,&hkey,0) == ERROR_SUCCESS)
	{
		wchar_t description_wbuf[STRING_SIZE];
		
		string_copy_utf8_string(description_wbuf,description);
		
		_viv_set_registry_string(hkey,0,description_wbuf);
		
		RegCloseKey(hkey);
	}		
	
	string_copy_utf8_string(key,"SOFTWARE\\Classes\\");
	string_cat(key,class_name);
	string_cat_utf8(key,"\\shell\\open\\command");
	
	if (RegCreateKeyExW(HKEY_CURRENT_USER,key,0,0,0,KEY_QUERY_VALUE|KEY_SET_VALUE,0,&hkey,0) == ERROR_SUCCESS)
	{
		wchar_t filename[STRING_SIZE];
		wchar_t command[STRING_SIZE];

		_viv_get_exe_filename(filename);
		
		string_copy_utf8_string(command,(const utf8_t *)"\"");
		string_cat(command,filename);
		string_cat_utf8(command,(const utf8_t *)"\" \"%1\"");
		
		_viv_set_registry_string(hkey,0,command);
		
		RegCloseKey(hkey);
	}
	
	// rc.4: pin the default verb - the shell key's default value names
	// the verb a double click runs. explicit is the contract; convention
	// (open when present, else the first verb the merged view offers)
	// is what a machine may resolve differently.
	string_copy_utf8_string(key,"SOFTWARE\\Classes\\");
	string_cat(key,class_name);
	string_cat_utf8(key,"\\shell");
	
	if (RegCreateKeyExW(HKEY_CURRENT_USER,key,0,0,0,KEY_QUERY_VALUE|KEY_SET_VALUE,0,&hkey,0) == ERROR_SUCCESS)
	{
		wchar_t verb_wbuf[STRING_SIZE];
		
		string_copy_utf8_string(verb_wbuf,(const utf8_t *)"open");
		
		_viv_set_registry_string(hkey,0,verb_wbuf);
		
		RegCloseKey(hkey);
	}
}

void _viv_install_association_by_extension(const char *association,const char *description,const char *icon_location,int force)
{
	HKEY hkey;
	wchar_t class_name[STRING_SIZE];
	wchar_t key[STRING_SIZE];
	wchar_t default_icon[STRING_SIZE];
	wchar_t dot_association[STRING_SIZE];
	LONG reg_ret;
	
	string_copy_utf8_string(dot_association,(const utf8_t *)".");
	string_cat_utf8(dot_association,association);
	
	// make sure we uninstall old associations first.
	_viv_uninstall_association_by_extension(association);

	string_copy_utf8_string(class_name,"voidImageViewer");
	string_cat(class_name,dot_association);
	
	// the upstream todo's association guard: never take over an
	// extension a foreign viewer owns. the gate sits after the
	// uninstall-restore (an upgrade over an older no-gate install
	// heals to the pre-fork owner first, so a foreign owner reads
	// as foreign even then) and before any write of ours - the
	// class keys, the icons, the backup and the takeover are all
	// skipped for a foreign-owned extension.
	// force is the explicit user consent: every explicit selection -
	// the installer's wizard checkboxes, the settings single click, the
	// settings select all - passes one (the user's own directive: a
	// selected format changes hands, the previous owner loses it). only
	// the snapshot restore keeps the silent guard - reverting a cancelled
	// dialog must not steal an extension a foreign owner took in the
	// meantime.
	if ((!force) && (_viv_is_foreign_association(association,class_name)))
	{
		debug_printf("association .%s left alone (a foreign viewer owns it)\n",association);

		return;
	}

	_viv_install_class_definition_by_extension(association,description,icon_location);

	string_copy_utf8_string(key,"SOFTWARE\\Classes\\");
	string_cat(key,dot_association);
	
	reg_ret = RegCreateKeyExW(HKEY_CURRENT_USER,key,0,0,0,KEY_QUERY_VALUE|KEY_SET_VALUE,0,&hkey,0);
	if (reg_ret == ERROR_SUCCESS)
	{
		wchar_t wbuf[STRING_SIZE];
		
		if (!_viv_get_registry_string(hkey,(const utf8_t *)"voidImageViewer.Backup",wbuf,STRING_SIZE))
		{
			if (!_viv_get_registry_string(hkey,0,wbuf,STRING_SIZE))
			{
				// the machine owner (pngfile and friends) lives in the
				// hklm half of the merged view the shell resolves - the
				// user-side read alone sees nothing there, and the empty
				// backup it captured would restore as an empty default
				// that shadows the machine's own answer. read the merged
				// view before giving up (the foreign check below reads
				// the same view for the same reason).
				HKEY hkey_cr;

				if (RegOpenKeyExW(HKEY_CLASSES_ROOT,dot_association,0,KEY_QUERY_VALUE,&hkey_cr) == ERROR_SUCCESS)
				{
					if (!_viv_get_registry_string(hkey_cr,0,wbuf,STRING_SIZE))
					{
						*wbuf = 0;
					}

					RegCloseKey(hkey_cr);
				}
				else
				{
					*wbuf = 0;
				}
			}
			
			_viv_set_registry_string(hkey,(const utf8_t *)"voidImageViewer.Backup",wbuf);
		}

		_viv_set_registry_string(hkey,0,class_name);
		
		RegCloseKey(hkey);
	}
	else
	{
		debug_printf("RegCreateKeyExW failed %u\n",reg_ret);
	}
	// rc.17: win10/11 keep the default app behind the UserChoice
	// hash - a signature a third party cannot write. the per-user
	// class takeover above moves the default on windows 7 and stays
	// cosmetic on windows 10 and 11; the OpenWithProgids registration
	// is the part we can sign, and it is what puts the viewer in the
	// Open With list (and so within the default apps page's reach).
	// both homes carry it: the class view and the FileExts view the
	// explorer reads per user.
	string_copy_utf8_string(key,"SOFTWARE\\Classes\\");
	string_cat(key,dot_association);
	string_cat_utf8(key,"\\OpenWithProgids");
	
	if (RegCreateKeyExW(HKEY_CURRENT_USER,key,0,0,0,KEY_QUERY_VALUE|KEY_SET_VALUE,0,&hkey,0) == ERROR_SUCCESS)
	{
		// the shell's own entries sit beside ours as REG_NONE with
		// no data - the unsigned empty shape matches them.
		RegSetValueExW(hkey,class_name,0,REG_NONE,(const BYTE *)"",0);
		
		RegCloseKey(hkey);
	}
	
	string_copy_utf8_string(key,"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\FileExts\\");
	string_cat(key,dot_association);
	string_cat_utf8(key,"\\OpenWithProgids");
	
	if (RegCreateKeyExW(HKEY_CURRENT_USER,key,0,0,0,KEY_QUERY_VALUE|KEY_SET_VALUE,0,&hkey,0) == ERROR_SUCCESS)
	{
		RegSetValueExW(hkey,class_name,0,REG_NONE,(const BYTE *)"",0);
		
		RegCloseKey(hkey);
	}
	
	// the official docs: without the notify the explorer may not
	// notice the change until the next reboot.
	SHChangeNotify(SHCNE_ASSOCCHANGED,SHCNF_IDLIST,0,0);
}
// rc.4: the app registration - the face windows 10/11 settings needs.
// registeredapplications indexes the app into the default apps page,
// the capabilities block names it and lists every extension it can
// open, and app paths teaches the shell the exe by name (the run
// dialog, and the installed-app signal some integrations read).
// per-user on purpose: the same hive every other write of this
// installer lives in.
static void _viv_install_app_registration(void)
{
	HKEY hkey;
	wchar_t exe_filename[STRING_SIZE];
	wchar_t wbuf[STRING_SIZE];
	int exti;
	
	// the index: the value points at the capabilities key below.
	// the value name is the app's one true name - the msdn contract
	// for default programs is explicit: "applicationname must always
	// match the name that is registered under registeredapplications",
	// and the rc.4-rc.6 writes broke exactly that rule (the index
	// said voidImageViewer, the capabilities block said void Image
	// Viewer). the field machines never listed the app and the
	// mismatch was the one deviation from the documented contract
	// the registration carried - firefox and the portable-browser
	// registrations show the settings page tolerates a mismatch in
	// practice, so the repair is the contract's letter, not a proven
	// cause; the machine's answer stays a field question.
	if (RegCreateKeyExW(HKEY_CURRENT_USER,L"Software\\RegisteredApplications",0,0,0,KEY_QUERY_VALUE|KEY_SET_VALUE,0,&hkey,0) == ERROR_SUCCESS)
	{
		wchar_t capabilities_wbuf[STRING_SIZE];
		
		string_copy_utf8_string(capabilities_wbuf,(const utf8_t *)"SOFTWARE\\voidImageViewer\\Capabilities");
		
		_viv_set_registry_string(hkey,(const utf8_t *)"void Image Viewer",capabilities_wbuf);
		
		// the repair sweep: a machine that carries the rc.4-rc.6
		// spelling keeps a second index value forever otherwise -
		// one pointing at the same capabilities under a name the
		// contract rejects. the delete is best-effort; a machine
		// that never saw the old writes answers file-not-found.
		RegDeleteValueA(hkey,"voidImageViewer");
		
		RegCloseKey(hkey);
	}
	
	// the capabilities block: the display name and the description.
	if (RegCreateKeyExW(HKEY_CURRENT_USER,L"SOFTWARE\\voidImageViewer\\Capabilities",0,0,0,KEY_QUERY_VALUE|KEY_SET_VALUE,0,&hkey,0) == ERROR_SUCCESS)
	{
		wchar_t name_wbuf[STRING_SIZE];
		wchar_t description_wbuf[STRING_SIZE];
		
		string_copy_utf8_string(name_wbuf,(const utf8_t *)"void Image Viewer");
		string_copy_utf8_string(description_wbuf,(const utf8_t *)"A fast, minimal image viewer.");
		
		_viv_set_registry_string(hkey,(const utf8_t *)"ApplicationName",name_wbuf);
		_viv_set_registry_string(hkey,(const utf8_t *)"ApplicationDescription",description_wbuf);
		
		RegCloseKey(hkey);
	}
	
	// every extension the viewer opens: the class is pre-written (the
	// capabilities reference must never dangle) and the file
	// associations value names the progid. the .ext takeover itself
	// stays gated on the installer checkboxes.
	for(exti=0;exti<_VIV_ASSOCIATION_COUNT;exti++)
	{
		_viv_install_class_definition_by_extension(_viv_association_extensions[exti],localization_get_string(_viv_association_description_localization_id_array[exti]),_viv_association_icon_locations[exti]);
		
		string_copy_utf8_string(wbuf,(const utf8_t *)"SOFTWARE\\voidImageViewer\\Capabilities\\FileAssociations");
		
		if (RegCreateKeyExW(HKEY_CURRENT_USER,wbuf,0,0,0,KEY_QUERY_VALUE|KEY_SET_VALUE,0,&hkey,0) == ERROR_SUCCESS)
		{
			wchar_t dot_wbuf[STRING_SIZE];
			wchar_t class_wbuf[STRING_SIZE];
			
			string_copy_utf8_string(dot_wbuf,(const utf8_t *)".");
			string_cat_utf8(dot_wbuf,_viv_association_extensions[exti]);
			
			string_copy_utf8_string(class_wbuf,(const utf8_t *)"voidImageViewer");
			string_cat(class_wbuf,dot_wbuf);
			
			RegSetValueExW(hkey,dot_wbuf,0,REG_SZ,(BYTE *)class_wbuf,(string_get_length(class_wbuf) + 1) * sizeof(wchar_t));
			
			RegCloseKey(hkey);
		}
	}
	
	// app paths: the exe by name.
	_viv_get_exe_filename(exe_filename);
	
	if (RegCreateKeyExW(HKEY_CURRENT_USER,L"Software\\Microsoft\\Windows\\CurrentVersion\\App Paths\\voidImageViewer.exe",0,0,0,KEY_QUERY_VALUE|KEY_SET_VALUE,0,&hkey,0) == ERROR_SUCCESS)
	{
		_viv_set_registry_string(hkey,0,exe_filename);
		
		RegCloseKey(hkey);
	}
	
	// rc.7: the applications seat - the registration the shell's own
	// application lookups read (the msdn application registration
	// contract's other half; the app paths pair above answers "where
	// is the exe", this one answers "what can it do"). the open-with
	// dialog and the default programs cross-references resolve through
	// here: the friendly name, the icon, the pinned open verb.
	string_copy_utf8_string(wbuf,(const utf8_t *)"SOFTWARE\\Classes\\Applications\\voidImageViewer.exe");
	
	if (RegCreateKeyExW(HKEY_CURRENT_USER,wbuf,0,0,0,KEY_QUERY_VALUE|KEY_SET_VALUE,0,&hkey,0) == ERROR_SUCCESS)
	{
		wchar_t name_wbuf[STRING_SIZE];
		
		string_copy_utf8_string(name_wbuf,(const utf8_t *)"void Image Viewer");
		
		_viv_set_registry_string(hkey,0,name_wbuf);
		
		// the documented seat of the friendly name: assocstr reads
		// friendlyappname (falling back to the exe's file description);
		// the default value above is the belt to its braces.
		_viv_set_registry_string(hkey,(const utf8_t *)"FriendlyAppName",name_wbuf);
		
		RegCloseKey(hkey);
	}
	
	string_copy_utf8_string(wbuf,(const utf8_t *)"SOFTWARE\\Classes\\Applications\\voidImageViewer.exe\\DefaultIcon");
	
	if (RegCreateKeyExW(HKEY_CURRENT_USER,wbuf,0,0,0,KEY_QUERY_VALUE|KEY_SET_VALUE,0,&hkey,0) == ERROR_SUCCESS)
	{
		wchar_t icon_wbuf[STRING_SIZE];
		
		string_copy(icon_wbuf,exe_filename);
		string_cat_utf8(icon_wbuf,(const utf8_t *)",0");
		
		_viv_set_registry_string(hkey,0,icon_wbuf);
		
		RegCloseKey(hkey);
	}
	
	string_copy_utf8_string(wbuf,(const utf8_t *)"SOFTWARE\\Classes\\Applications\\voidImageViewer.exe\\shell\\open\\command");
	
	if (RegCreateKeyExW(HKEY_CURRENT_USER,wbuf,0,0,0,KEY_QUERY_VALUE|KEY_SET_VALUE,0,&hkey,0) == ERROR_SUCCESS)
	{
		wchar_t command[STRING_SIZE];
		
		string_copy_utf8_string(command,(const utf8_t *)"\"");
		string_cat(command,exe_filename);
		string_cat_utf8(command,(const utf8_t *)"\" \"%1\"");
		
		_viv_set_registry_string(hkey,0,command);
		
		RegCloseKey(hkey);
	}
	
	// the explorer hears the registration the same moment.
	SHChangeNotify(SHCNE_ASSOCCHANGED,SHCNF_IDLIST,0,0);
}
// remove the app registration: the index value, the capabilities tree
// and the app paths key. regdeletekeyw refuses keys with subkeys, so
// the leaves go first (the same shape the class tree delete walks).
static void _viv_uninstall_app_registration(void)
{
	HKEY hkey;
	wchar_t wbuf[STRING_SIZE];
	
	if (RegOpenKeyExW(HKEY_CURRENT_USER,L"Software\\RegisteredApplications",0,KEY_QUERY_VALUE|KEY_SET_VALUE,&hkey) == ERROR_SUCCESS)
	{
		DWORD value_count;
		DWORD subkey_count;

		// the rc.7 name first, then the spelling the rc.4-rc.6 writes
		// left on field machines (the install's repair sweep deletes
		// it too; this one covers a plain uninstall).
		RegDeleteValueW(hkey,L"void Image Viewer");
		RegDeleteValueA(hkey,"voidImageViewer");
		
		// a key holding nothing else is the residue the field report
		// caught (the values were swept, the seat stayed): the bare-key
		// rule takes it - another app's registration keeps its own
		// values and the key with them.
		if ((RegQueryInfoKeyW(hkey,0,0,0,&subkey_count,0,0,&value_count,0,0,0,0) == ERROR_SUCCESS) && (!value_count) && (!subkey_count))
		{
			if (!RegDeleteKeyW(HKEY_CURRENT_USER,L"Software\\RegisteredApplications"))
			{
				debug_printf("RegDeleteKeyW registered applications failed %u\n",GetLastError());
			}
		}

		RegCloseKey(hkey);
	}
	
	// the capabilities tree: file associations first, then the parents.
	// the software key is shared with the installer's language memory
	// (the nsis mui remembers its dialog language under the same name) -
	// an uninstall takes both with it.
	string_copy_utf8_string(wbuf,(const utf8_t *)"SOFTWARE\\voidImageViewer\\Capabilities\\FileAssociations");
	RegDeleteKeyW(HKEY_CURRENT_USER,wbuf);
	
	string_copy_utf8_string(wbuf,(const utf8_t *)"SOFTWARE\\voidImageViewer\\Capabilities");
	RegDeleteKeyW(HKEY_CURRENT_USER,wbuf);
	
	string_copy_utf8_string(wbuf,(const utf8_t *)"SOFTWARE\\voidImageViewer");
	RegDeleteKeyW(HKEY_CURRENT_USER,wbuf);
	
	string_copy_utf8_string(wbuf,(const utf8_t *)"Software\\Microsoft\\Windows\\CurrentVersion\\App Paths\\voidImageViewer.exe");
	RegDeleteKeyW(HKEY_CURRENT_USER,wbuf);
	
	// the applications seat: regdeletekeyw refuses keys with subkeys,
	// so the leaves go first (the same shape the capabilities tree
	// delete above walks).
	string_copy_utf8_string(wbuf,(const utf8_t *)"SOFTWARE\\Classes\\Applications\\voidImageViewer.exe\\DefaultIcon");
	RegDeleteKeyW(HKEY_CURRENT_USER,wbuf);
	
	string_copy_utf8_string(wbuf,(const utf8_t *)"SOFTWARE\\Classes\\Applications\\voidImageViewer.exe\\shell\\open\\command");
	RegDeleteKeyW(HKEY_CURRENT_USER,wbuf);
	
	string_copy_utf8_string(wbuf,(const utf8_t *)"SOFTWARE\\Classes\\Applications\\voidImageViewer.exe\\shell\\open");
	RegDeleteKeyW(HKEY_CURRENT_USER,wbuf);
	
	string_copy_utf8_string(wbuf,(const utf8_t *)"SOFTWARE\\Classes\\Applications\\voidImageViewer.exe\\shell");
	RegDeleteKeyW(HKEY_CURRENT_USER,wbuf);
	
	string_copy_utf8_string(wbuf,(const utf8_t *)"SOFTWARE\\Classes\\Applications\\voidImageViewer.exe");
	RegDeleteKeyW(HKEY_CURRENT_USER,wbuf);
	
	// the explorer hears the sweep the same moment.
	SHChangeNotify(SHCNE_ASSOCCHANGED,SHCNF_IDLIST,0,0);
}

void _viv_uninstall_association_by_extension(const char *association)
{
	long reg_ret;
	HKEY hkey;
	wchar_t key[STRING_SIZE];
	wchar_t class_name[STRING_SIZE];
	wchar_t dot_association[STRING_SIZE];

	string_copy_utf8_string(key,(const utf8_t *)"SOFTWARE\\Classes\\.");
	string_cat_utf8(key,association);

	// an open, not a create: the old regcreatekeyex minted the key on
	// every uninstall walk - a full uninstall left a fresh empty .ext
	// key behind on machines whose install the gate had skipped (the
	// residue report). the takeover path creates the key when it owns
	// the write; the sweep only ever reads what is there.
	if (RegOpenKeyExW(HKEY_CURRENT_USER,key,0,KEY_QUERY_VALUE|KEY_SET_VALUE,&hkey) == ERROR_SUCCESS)
	{
		wchar_t wbuf[STRING_SIZE];

		if (_viv_get_registry_string(hkey,(const utf8_t *)"voidImageViewer.Backup",wbuf,STRING_SIZE))
		{
			if (*wbuf)
			{
				_viv_set_registry_string(hkey,0,wbuf);
			}
			else
			{
				// an empty backup means "no owner anywhere" - writing
				// it back would shadow the machine default with an
				// empty string in the merged view. the default value
				// goes instead, so the shell falls through to hklm.
				RegDeleteValueW(hkey,0);
			}

			reg_ret = RegDeleteValueA(hkey,"voidImageViewer.Backup");
			if (reg_ret != ERROR_SUCCESS)
			{
				debug_printf("RegDeleteValueA failed %u\n",reg_ret);
			}
		}

		RegCloseKey(hkey);
	}

	string_copy_utf8_string(key,(const utf8_t *)"SOFTWARE\\Classes\\voidImageViewer.");
	string_cat_utf8(key,association);

	// the class key carries subkeys (defaulticon, shell\\open\\command)
	// and the bare regdeletekey refuses them all - the fourth report's
	// zombie tree: every uninstall left the command pointing at the
	// removed exe while both honest reads answered all-clear. the tree
	// delete takes the whole progid; a refusal still lands in the log
	// (the return was dropped on the floor before).
	if (!os_delete_key_tree(HKEY_CURRENT_USER,key))
	{
		debug_printf("os_delete_key_tree failed %u\n",GetLastError());
	}

	// rc.17: sweep the two OpenWithProgids homes the install wrote.
	// the keys may carry the shell's own entries beside ours, so
	// only our value goes - and a key left holding nothing at all
	// names a dead seat, so the truly empty one goes too.
	string_copy_utf8_string(dot_association,(const utf8_t *)".");
	string_cat_utf8(dot_association,association);

	string_copy_utf8_string(class_name,(const utf8_t *)"voidImageViewer");
	string_cat(class_name,dot_association);

	string_copy_utf8_string(key,"SOFTWARE\\Classes\\.");
	string_cat_utf8(key,association);
	string_cat_utf8(key,"\\OpenWithProgids");

	if (RegOpenKeyExW(HKEY_CURRENT_USER,key,0,KEY_QUERY_VALUE|KEY_SET_VALUE,&hkey) == ERROR_SUCCESS)
	{
		RegDeleteValueW(hkey,class_name);

		{
			DWORD value_count;
			DWORD subkey_count;

			if ((RegQueryInfoKeyW(hkey,0,0,0,&subkey_count,0,0,&value_count,0,0,0,0) == ERROR_SUCCESS) && (!value_count) && (!subkey_count))
			{
				if (!RegDeleteKeyW(HKEY_CURRENT_USER,key))
				{
					debug_printf("RegDeleteKeyW empty key failed %u\n",GetLastError());
				}
			}
		}

		RegCloseKey(hkey);
	}

	string_copy_utf8_string(key,"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\FileExts\\.");
	string_cat_utf8(key,association);
	string_cat_utf8(key,"\\OpenWithProgids");

	if (RegOpenKeyExW(HKEY_CURRENT_USER,key,0,KEY_QUERY_VALUE|KEY_SET_VALUE,&hkey) == ERROR_SUCCESS)
	{
		RegDeleteValueW(hkey,class_name);

		{
			DWORD value_count;
			DWORD subkey_count;

			if ((RegQueryInfoKeyW(hkey,0,0,0,&subkey_count,0,0,&value_count,0,0,0,0) == ERROR_SUCCESS) && (!value_count) && (!subkey_count))
			{
				if (!RegDeleteKeyW(HKEY_CURRENT_USER,key))
				{
					debug_printf("RegDeleteKeyW empty key failed %u\n",GetLastError());
				}
			}
		}

		RegCloseKey(hkey);
	}

	// the userchoice the shell honors: when it names the progid this
	// uninstall just removed, deleting the key lets the shell fall
	// back to the restored default instead of prompting against a
	// dead app (the hash only guards writes - a delete is a delete).
	// a foreign choice is never touched.
	string_copy_utf8_string(key,(const utf8_t *)"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\FileExts\\.");
	string_cat_utf8(key,(const utf8_t *)association);
	string_cat_utf8(key,(const utf8_t *)"\\UserChoice");

	if (RegOpenKeyExW(HKEY_CURRENT_USER,key,0,KEY_QUERY_VALUE|KEY_SET_VALUE,&hkey) == ERROR_SUCCESS)
	{
		wchar_t choice_wbuf[STRING_SIZE];

		if ((_viv_get_registry_string(hkey,(const utf8_t *)"ProgId",choice_wbuf,STRING_SIZE)) && (string_get_length(choice_wbuf)))
		{
			// the compare lowercases both sides by hand (the icompare
			// helper lowercases only its first argument, and the class
			// name carries capitals by design).
			wchar_t lower_wbuf[STRING_SIZE];
			wchar_t lower_class[STRING_SIZE];
			int is_ours;
			int i;

			for(i=0;(choice_wbuf[i]) && (i < STRING_SIZE);i++)
			{
				lower_wbuf[i] = ((choice_wbuf[i] >= L'A') && (choice_wbuf[i] <= L'Z')) ? (choice_wbuf[i] - L'A' + L'a') : choice_wbuf[i];
			}

			lower_wbuf[i] = 0;

			for(i=0;(class_name[i]) && (i < STRING_SIZE);i++)
			{
				lower_class[i] = ((class_name[i] >= L'A') && (class_name[i] <= L'Z')) ? (class_name[i] - L'A' + L'a') : class_name[i];
			}

			lower_class[i] = 0;

			is_ours = string_compare(lower_wbuf,lower_class) == 0;

			RegCloseKey(hkey);

			if (is_ours)
			{
				if (!RegDeleteKeyW(HKEY_CURRENT_USER,key))
				{
					// the ucpd driver widens its deny list by update - a refused
					// delete must at least be named, not silently dangle.
					debug_printf("RegDeleteKeyW UserChoice failed %u\n",GetLastError());
				}
			}
		}
		else
		{
			RegCloseKey(hkey);
		}
	}

	// the extension's own key: the restore empties it back toward its
	// pre-install shape, and the shells the sweep can leave behind are
	// the residue the field report caught in regedit. two shapes go:
	// the truly bare key, and the one-value shadow whose single unnamed
	// default spells exactly what hklm already answers - deleting the
	// shadow changes nothing the merged view resolves (a per-user
	// default that differs from hklm's is somebody else's setting and
	// stays).
	string_copy_utf8_string(key,(const utf8_t *)"SOFTWARE\\Classes\\.");
	string_cat_utf8(key,(const utf8_t *)association);

	if (RegOpenKeyExW(HKEY_CURRENT_USER,key,0,KEY_QUERY_VALUE|KEY_SET_VALUE,&hkey) == ERROR_SUCCESS)
	{
		DWORD value_count;
		DWORD subkey_count;
		int delete_key;

		delete_key = 0;

		if ((RegQueryInfoKeyW(hkey,0,0,0,&subkey_count,0,0,&value_count,0,0,0,0) == ERROR_SUCCESS) && (!subkey_count))
		{
			if (!value_count)
			{
				delete_key = 1;
			}
			else
			if (value_count == 1)
			{
				wchar_t local_wbuf[STRING_SIZE];
				wchar_t machine_wbuf[STRING_SIZE];
				HKEY machine_hkey;

				if ((_viv_get_registry_string(hkey,0,local_wbuf,STRING_SIZE)) && (*local_wbuf))
				{
					string_copy_utf8_string(machine_wbuf,(const utf8_t *)"SOFTWARE\\Classes\\.");
					string_cat_utf8(machine_wbuf,(const utf8_t *)association);

					if (RegOpenKeyExW(HKEY_LOCAL_MACHINE,machine_wbuf,0,KEY_QUERY_VALUE,&machine_hkey) == ERROR_SUCCESS)
					{
						wchar_t hklm_wbuf[STRING_SIZE];

						if ((_viv_get_registry_string(machine_hkey,0,hklm_wbuf,STRING_SIZE)) && (string_compare(local_wbuf,hklm_wbuf) == 0))
						{
							delete_key = 1;
						}

						RegCloseKey(machine_hkey);
					}
				}
			}
		}

		RegCloseKey(hkey);

		if (delete_key)
		{
			if (!RegDeleteKeyW(HKEY_CURRENT_USER,key))
			{
				debug_printf("RegDeleteKeyW extension shell failed %u\n",GetLastError());
			}
		}
	}

	// the fileexts parent: the openwithprogids and userchoice sweeps
	// above can leave the shell's own seat holding nothing - the same
	// bare-key rule takes the parent when it is truly empty (a parent
	// the shell still uses carries its own subkeys and stays).
	string_copy_utf8_string(key,(const utf8_t *)"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\FileExts\\.");
	string_cat_utf8(key,(const utf8_t *)association);

	if (RegOpenKeyExW(HKEY_CURRENT_USER,key,0,KEY_QUERY_VALUE|KEY_SET_VALUE,&hkey) == ERROR_SUCCESS)
	{
		DWORD value_count;
		DWORD subkey_count;

		if ((RegQueryInfoKeyW(hkey,0,0,0,&subkey_count,0,0,&value_count,0,0,0,0) == ERROR_SUCCESS) && (!value_count) && (!subkey_count))
		{
			if (!RegDeleteKeyW(HKEY_CURRENT_USER,key))
			{
				debug_printf("RegDeleteKeyW fileexts shell failed %u\n",GetLastError());
			}
		}

		RegCloseKey(hkey);
	}

	// the explorer hears the sweep the same moment.
	SHChangeNotify(SHCNE_ASSOCCHANGED,SHCNF_IDLIST,0,0);
}
int _viv_is_association(const char *association)
{
	int ret;
	HKEY hkey;
	wchar_t class_name[STRING_SIZE];
	wchar_t key[STRING_SIZE];
	
	string_copy_utf8_string(class_name,(const utf8_t *)"voidImageViewer.");
	string_cat_utf8(class_name,association);
	
	ret = 0;

	string_copy_utf8_string(key,"SOFTWARE\\Classes\\.");
	string_cat_utf8(key,association);
	
//debug_printf("key %S\n",key);
	
	if (RegOpenKeyExW(HKEY_CURRENT_USER,key,0,KEY_QUERY_VALUE,&hkey) == ERROR_SUCCESS)
	{
		wchar_t wbuf[STRING_SIZE];

//debug_printf("key OK\n",key);

		if (_viv_get_registry_string(hkey,0,wbuf,STRING_SIZE))
		{
//debug_printf("value cmp %S %S\n",wbuf,class_name);
			if (string_compare(wbuf,class_name) == 0)
			{
				ret++;
			}
		}

		RegCloseKey(hkey);
	}

	string_copy_utf8_string(key,"SOFTWARE\\Classes\\");
	string_cat(key,class_name);
	string_cat_utf8(key,(const utf8_t *)"\\shell\\open\\command");
	
//debug_printf("key %S\n",key);
	
	if (RegOpenKeyExW(HKEY_CURRENT_USER,key,0,KEY_QUERY_VALUE,&hkey) == ERROR_SUCCESS)
	{
		wchar_t wbuf[STRING_SIZE];

		if (_viv_get_registry_string(hkey,0,wbuf,STRING_SIZE))
		{
			wchar_t filename[STRING_SIZE];
			wchar_t command[STRING_SIZE];

//debug_printf("key OK\n",key);

			_viv_get_exe_filename(filename);
			
			string_copy_utf8_string(command,(const utf8_t *)"\"");
			string_cat(command,filename);
			string_cat_utf8(command,(const utf8_t *)"\" \"%1\"");

//debug_printf("value cmp %S %S\n",wbuf,command);
			
			if (string_compare(wbuf,command) == 0)
			{
				ret++;
			}
		}

		RegCloseKey(hkey);
	}

	return ret == 2;
}
static int _viv_get_registry_string(HKEY hkey,const utf8_t *value,wchar_t *wbuf,int size_in_wchars)
{
	wchar_t value_wbuf[STRING_SIZE];
	wchar_t *value_wp;
	DWORD cbData;
	DWORD type;
	
	if (value)
	{
		string_copy_utf8_string(value_wbuf,value);
		value_wp = value_wbuf;
	}
	else
	{
		value_wp = 0;
	}
	
	cbData = size_in_wchars * sizeof(wchar_t);
	
	if (RegQueryValueExW(hkey,value_wp,0,&type,(BYTE *)wbuf,&cbData) == ERROR_SUCCESS)
	{
		if ((type == REG_SZ) || (type == REG_EXPAND_SZ))
		{
			return 1;
		}
	}
	
	return 0;
}
static int _viv_set_registry_string(HKEY hkey,const utf8_t *value,const wchar_t *wbuf)
{
	wchar_t value_wbuf[STRING_SIZE];
	wchar_t *value_wp;
	LONG reg_ret;

	if (value)
	{
		string_copy_utf8_string(value_wbuf,value);
		value_wp = value_wbuf;
	}
	else
	{
		value_wp = 0;
	}	

	reg_ret = RegSetValueExW(hkey,value_wp,0,REG_SZ,(BYTE *)wbuf,(string_get_length(wbuf) + 1) * sizeof(wchar_t));
	if (reg_ret == ERROR_SUCCESS)
	{
		return 1;
	}
	else
	{
		debug_printf("failed to set reg value %u %s %S",reg_ret,value,wbuf);
	}
	
	return 0;
}
static void _viv_install_association(DWORD flags)
{
	int i;
	
	// the wizard's checkboxes are the consent: a format the user marked
	// changes hands directly, the previous owner loses the extension (the
	// field round's directive - the old silent guard skipped every
	// already-associated format the user had explicitly selected, and the
	// install read as complete with nothing associated).
	for(i=0;i<_VIV_ASSOCIATION_COUNT;i++)
	{
		if (flags & (1 << i))
		{
			_viv_install_association_by_extension(_viv_association_extensions[i],localization_get_string(_viv_association_description_localization_id_array[i]),_viv_association_icon_locations[i],1);
		}
	}
}
static void _viv_uninstall_association(DWORD flags)
{
	int i;
	
	for(i=0;i<_VIV_ASSOCIATION_COUNT;i++)
	{
		if (flags & 1 << i)
		{
			_viv_uninstall_association_by_extension(_viv_association_extensions[i]);
		}
	}
}
// close every running instance before (un)install. the original loop
// waited forever: a hung instance would block install, uninstall and
// exit forever. (this resolves the upstream FIXME: bounded waits, with
// a last resort terminate.)
// QueryFullProcessImageNameW (Vista+) is resolved lazily right here:
// the install/uninstall path runs before os_init so the central
// runtime table is not populated yet, and the headers gate the
// declaration behind a newer _WIN32_WINNT than the project targets.
static BOOL (WINAPI *_viv_query_full_process_image_name)(HANDLE,DWORD,wchar_t *,DWORD *);
static int _viv_is_voidimageviewer_process(DWORD process_id)
{
	HANDLE process_handle;
	int ret;
	
	// conservative default: if the image name can not be queried we
	// assume the window is ours. in an uninstall context closing a
	// real instance is preferred over leaving a locked file behind.
	ret = 1;
	
	process_handle = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION,FALSE,process_id);
	
	if (process_handle)
	{
		wchar_t image_name[STRING_SIZE];
		DWORD image_name_length;
		
		if (!_viv_query_full_process_image_name)
		{
			_viv_query_full_process_image_name = (void *)GetProcAddress(GetModuleHandleA("kernel32.dll"),"QueryFullProcessImageNameW");
		}
		
		image_name_length = STRING_SIZE;
		
		if ((_viv_query_full_process_image_name) && (_viv_query_full_process_image_name(process_handle,0,image_name,&image_name_length)))
		{
			const wchar_t *basename;
			const wchar_t *wanted;
			
			// the name past the last separator is the image file name.
			basename = image_name + image_name_length;
			while ((basename > image_name) && (basename[-1] != L'\\') && (basename[-1] != L':'))
			{
				basename--;
			}
			
			// case insensitive compare against voidImageViewer.exe.
			wanted = L"voidImageViewer.exe";
			
			while ((*basename) && (*wanted))
			{
				wchar_t c1;
				wchar_t c2;
				
				c1 = *basename;
				c2 = *wanted;
				
				if ((c1 >= L'A') && (c1 <= L'Z'))
				{
					c1 += (L'a' - L'A');
				}
				
				if ((c2 >= L'A') && (c2 <= L'Z'))
				{
					c2 += (L'a' - L'A');
				}
				
				if (c1 != c2)
				{
					break;
				}
				
				basename++;
				wanted++;
			}
			
			if ((*basename) || (*wanted))
			{
					// the image is not voidImageViewer.exe: not our window.
					ret = 0;
				}
		}
		
		CloseHandle(process_handle);
	}
	
	return ret;
}
static void _viv_close_existing_process(void)
{
	int attempts;
	
	// close at most 16 instances; a window that survives a close attempt
	// stops the loop early, so the cap only guards a pathological
	// instance spawning loop, not a normal machine.
	for(attempts = 0;attempts < 16;attempts++)
	{
		HWND hwnd;
		DWORD process_id;
		HANDLE process_handle;
		
		hwnd = FindWindowA("VOIDIMAGEVIEWER",0);
		
		if (!hwnd)
		{
			// no window open
			break;
		}
		
		// ask the instance to close. SMTO_ABORTIFHUNG gives up on an
		// unresponsive window instead of blocking us forever.
		SendMessageTimeoutA(hwnd,WM_CLOSE,0,0,SMTO_ABORTIFHUNG,5000,0);
		
		process_id = 0;
		GetWindowThreadProcessId(hwnd,&process_id);
		
		// the window class is a weak identity: any program can register
		// the same class name and the uninstaller would then close a
		// window it does not own. verify the process image name first.
		if ((process_id) && (!_viv_is_voidimageviewer_process(process_id)))
		{
			// the window belongs to another program: leave it alone and
			// stop looking (findwindow would return the same window).
			break;
		}
		
		process_handle = 0;
		
		if (process_id)
		{
			// PROCESS_TERMINATE is for the last resort kill below. opening
			// may fail (NULL) for an elevated instance from a non elevated
			// uninstaller: we then skip the wait and the window check below
			// stops the loop instead of spinning.
			process_handle = OpenProcess(SYNCHRONIZE|PROCESS_TERMINATE,FALSE,process_id);
		}
		
		if (process_handle)
		{
			if (WaitForSingleObject(process_handle,5000) == WAIT_TIMEOUT)
			{
				// the instance did not exit in time: force it.
				TerminateProcess(process_handle,1);
				
				WaitForSingleObject(process_handle,5000);
			}
			
			CloseHandle(process_handle);
		}
		
		// if the window survived the close (and the possible kill),
		// repeating the same failing attempt can not succeed: stop
		// instead of stalling setup.
		if (hwnd == FindWindowA("VOIDIMAGEVIEWER",0))
		{
			break;
		}
	}
}
static void _viv_uninstall_delete_file(const wchar_t *path,const utf8_t *filename)
{
	wchar_t full_path_and_filename[STRING_SIZE];

	string_path_combine_utf8(full_path_and_filename,path,filename);

	if (!DeleteFile(full_path_and_filename))
	{
		// a blocked file is why a removedirectory fails silently -
		// the log owes the uninstaller the name.
		debug_printf("DeleteFile %S failed %u\n",full_path_and_filename,GetLastError());
	}
}

// the failed-save temps (voidImageViewer.ini.<pid>.tmp, and the
// fixed .ini.tmp the pre-1.1.16 writers could leave): a crashed
// save leaves one behind, no live path ever cleans another
// process's name, and the leftovers kept removedirectory from
// taking the folder with them. the uninstall sweep owns them all
// - garbage, never settings.
static void _viv_uninstall_sweep_config_temps(const wchar_t *path)
{
	wchar_t pattern[STRING_SIZE];
	wchar_t full_path[STRING_SIZE];
	WIN32_FIND_DATAW find_data;
	HANDLE find_handle;

	string_path_combine_utf8(pattern,path,(const utf8_t *)"voidImageViewer.ini*.tmp");

	find_handle = FindFirstFileW(pattern,&find_data);

	if (find_handle != INVALID_HANDLE_VALUE)
	{
		do
		{
			// only the file shape - a directory named like a temp is
			// not ours to take.
			if (!(find_data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
			{
				string_path_combine(full_path,path,find_data.cFileName);

				if (!DeleteFile(full_path))
				{
					debug_printf("DeleteFile temp %S failed %u\n",full_path,GetLastError());
				}
			}
		}
		while(FindNextFileW(find_handle,&find_data));

		FindClose(find_handle);
	}
}
int _viv_is_start_menu_shortcuts(void)
{
	wchar_t special_folder_path_wbuf[STRING_SIZE];
	wchar_t path_wbuf[STRING_SIZE];
	int ret;
	
	ret = 0;

	// delete old english shortcuts
	// delete shortcuts
	if (os_get_special_folder_path(special_folder_path_wbuf,CSIDL_COMMON_PROGRAMS))
	{
		string_path_combine_utf8(path_wbuf,special_folder_path_wbuf,(const utf8_t *)"void Image Viewer");

		// make sure this directory exists!		
		if (GetFileAttributesW(path_wbuf) != INVALID_FILE_ATTRIBUTES)
		{
			// at least one item exists, ret = 2
			ret = 1;
		}
	}

	return ret;
}
static void _viv_install_start_menu_shortcuts(void)
{
	wchar_t special_folder_path_wbuf[STRING_SIZE];
	
	// always uninstall and reinstall
	// this allows us to switch between english and another language.
	_viv_uninstall_start_menu_shortcuts();

	// create shortcuts. the all-users programs folder answers the
	// admin token; a standard user's shortcuts ride the per-user
	// folder - the same seat the per-user install itself lives in
	// (the uninstall sweeps both, so the seat follows the token that
	// wrote it).
	if (os_get_special_folder_path(special_folder_path_wbuf,os_is_admin() ? CSIDL_COMMON_PROGRAMS : CSIDL_PROGRAMS))
	{
		wchar_t path_wbuf[STRING_SIZE];
		wchar_t exe_filename_wbuf[STRING_SIZE];
		wchar_t exe_path_wbuf[STRING_SIZE];
		wchar_t uninstall_filename_wbuf[STRING_SIZE];
		wchar_t lnk_wbuf[STRING_SIZE];
		
		string_path_combine_utf8(path_wbuf,special_folder_path_wbuf,(const utf8_t *)"void Image Viewer");

		// make sure this directory exists!		
		os_make_sure_path_exists(path_wbuf);

		// void image viewer.lnk
		_viv_get_exe_filename(exe_filename_wbuf);
		string_path_combine_utf8(lnk_wbuf,path_wbuf,"void Image Viewer.lnk");
		os_create_shell_link(exe_filename_wbuf,lnk_wbuf);
	
		// uninstall
		string_get_path_part(exe_path_wbuf,exe_filename_wbuf);
		string_path_combine_utf8(uninstall_filename_wbuf,exe_path_wbuf,(const utf8_t *)"Uninstall.exe");

		string_path_combine_utf8(lnk_wbuf,path_wbuf,"Uninstall.lnk");
		os_create_shell_link(uninstall_filename_wbuf,lnk_wbuf);
	}
}
static void _viv_uninstall_start_menu_shortcuts(void)
{
	wchar_t special_folder_path_wbuf[STRING_SIZE];
	wchar_t path_wbuf[STRING_SIZE];
	int folderi;

	// delete old english shortcuts
	// delete shortcuts. both seats: the shortcut's home followed
	// the token that wrote it (the all-users folder for an elevated
	// apply, the per-user one otherwise), and an uninstall can run
	// under either.
	for(folderi=0;folderi<2;folderi++)
	{
		if (os_get_special_folder_path(special_folder_path_wbuf,folderi ? CSIDL_PROGRAMS : CSIDL_COMMON_PROGRAMS))
		{
			wchar_t lnk_wbuf[STRING_SIZE];

			string_path_combine_utf8(path_wbuf,special_folder_path_wbuf,(const utf8_t *)"void Image Viewer");
			
			// localized.
			string_path_combine_utf8(lnk_wbuf,path_wbuf,"void Image Viewer.lnk");
			DeleteFile(lnk_wbuf);
			
			string_path_combine_utf8(lnk_wbuf,path_wbuf,"Uninstall.lnk");
			DeleteFile(lnk_wbuf);
			
			RemoveDirectory(path_wbuf);
		}
	}
}
void _viv_append_admin_param(wchar_t *wbuf,const utf8_t *param)
{
	string_cat_utf8(wbuf,(const utf8_t *)" /");
	string_cat_utf8(wbuf,param);
}
// rc.6: does this token own the directory? the probe writes and
// removes a temporary file - the same dance the nsis side runs on
// the default directory (a refusal there moves the install to the
// per-user programs folder; here it decides whether the relay is
// needed at all).
static int _viv_install_path_user_writable(const wchar_t *install_path)
{
	wchar_t probe_path[STRING_SIZE];
	HANDLE file;
	int writable;

	os_make_sure_path_exists(install_path);

	string_copy(probe_path,install_path);
	string_cat_utf8(probe_path,(const utf8_t *)"\\._viw_probe");

	writable = 0;

	// CREATE_NEW: the probe must create, not truncate - anything
	// already sitting at the predictable name (a stale probe, a
	// planted reparse point) must not redirect the write. a refused
	// create that still deletes the blocker proves the directory
	// writable, so the retry keeps the answer honest.
	file = CreateFileW(probe_path,GENERIC_WRITE,0,0,CREATE_NEW,FILE_ATTRIBUTE_TEMPORARY,0);

	if (file == INVALID_HANDLE_VALUE)
	{
		if (DeleteFileW(probe_path))
		{
			file = CreateFileW(probe_path,GENERIC_WRITE,0,0,CREATE_NEW,FILE_ATTRIBUTE_TEMPORARY,0);
		}
	}

	if (file != INVALID_HANDLE_VALUE)
	{
		writable = 1;

		CloseHandle(file);

		DeleteFileW(probe_path);
	}

	return writable;
}
// rc.6: the hklm add/remove entry is the admin install's footprint.
// its absence is the witness that an uninstall only touches
// user-owned seats (the per-user entry, the per-user shortcuts, the
// files) - the relay would demand a password for work the token can
// already do.
static int _viv_install_hklm_arp_present(void)
{
	HKEY hkey;
	int present;

	present = 0;

	if (RegOpenKeyExW(HKEY_LOCAL_MACHINE,L"Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\voidImageViewer",0,KEY_QUERY_VALUE|KEY_WOW64_64KEY,&hkey) == ERROR_SUCCESS)
	{
		present = 1;

		RegCloseKey(hkey);
	}

	return present;
}
// rc.6: the installer-path read-back (win10/11 keep the default
// behind the UserChoice hash). one box for the whole run - the
// settings page asks per extension, one box per format mid-install would
// be a wall.
static void _viv_install_association_locked_box(DWORD install_flags)
{
	wchar_t caption_wbuf[STRING_SIZE];
	wchar_t message_wbuf[STRING_SIZE];
	wchar_t ext_list_wbuf[STRING_SIZE];
	int exti;

	ext_list_wbuf[0] = 0;

	for(exti=0;exti<_VIV_ASSOCIATION_COUNT;exti++)
	{
		if ((install_flags & (1 << exti)) && (_viv_is_association(_viv_association_extensions[exti])) && (_viv_default_app_locked_elsewhere(_viv_association_extensions[exti])))
		{
			if (ext_list_wbuf[0])
			{
				string_cat_utf8(ext_list_wbuf,(const utf8_t *)", ");
			}

			string_cat_utf8(ext_list_wbuf,(const utf8_t *)".");
			string_cat_utf8(ext_list_wbuf,(const utf8_t *)_viv_association_extensions[exti]);
		}
	}

	if (ext_list_wbuf[0])
	{
		string_copy_utf8_string(caption_wbuf,localization_get_string(LOCALIZATION_ID_INSTALLER_ASSOCIATION_LOCKED_CAPTION));

		string_printf(message_wbuf,(const char *)localization_get_string(LOCALIZATION_ID_INSTALLER_ASSOCIATION_LOCKED_MESSAGE),ext_list_wbuf);

		if (viv_msgbox(0,caption_wbuf,message_wbuf,MB_YESNO|MB_ICONINFORMATION) == IDYES)
		{
			// the one path windows 10/11 leave open. the registeredAppUser
			// parameter lands the page on our own row (win11 21h2+; older
			// builds ignore it and still open the page).
			ShellExecuteW(0,NULL,L"ms-settings:defaultapps?registeredAppUser=void%20Image%20Viewer",NULL,NULL,SW_SHOWNORMAL);
		}
	}
}
// the honest size for the apps list: the payload this install
// actually laid down, measured - not a constant (the exe and the
// notices grow on their own schedules). the unit is kilobytes
// (the arpsize contract); the payload is megabytes, so the high
// dword of each size is noise by construction.
static DWORD _viv_install_payload_kb(const wchar_t *install_path)
{
	static const utf8_t *payload_files[4] = {(const utf8_t *)"voidImageViewer.exe",(const utf8_t *)"Uninstall.exe",(const utf8_t *)"LICENSE",(const utf8_t *)"THIRD_PARTY_NOTICES.md"};
	DWORD total;
	int i;

	total = 0;

	for(i=0;i<4;i++)
	{
		wchar_t file_path[STRING_SIZE];
		WIN32_FILE_ATTRIBUTE_DATA fad;

		string_path_combine_utf8(file_path,install_path,payload_files[i]);

		if ((GetFileAttributesExW(file_path,GetFileExInfoStandard,&fad)) && (!(fad.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)))
		{
			total += fad.nFileSizeLow;
		}
	}

	return (total + 1023) / 1024;
}

// register voidImageViewer in add/remove programs (programs and
// features). the setup runs the exe with /install so the exe owns this
// key: hklm for admin installs (what the setup's .onInit reads back),
// hkcu otherwise. the uninstall string is quoted so windows can run it
// with spaces in the path.
static void _viv_install_add_remove_programs(const wchar_t *install_path)
{
	HKEY hkey;
	HKEY root;

	root = os_is_admin() ? HKEY_LOCAL_MACHINE : HKEY_CURRENT_USER;
	
	// write the 64-bit view explicitly: a 32-bit build used to land in
	// wow6432node, which the nsis uninstall probe (setregview 64) never
	// reads. the flag is ignored by 64-bit builds and on 32-bit windows.
	if (RegCreateKeyExW(root,L"Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\voidImageViewer",0,0,0,KEY_QUERY_VALUE|KEY_SET_VALUE|KEY_WOW64_64KEY,0,&hkey,0) == ERROR_SUCCESS)
	{
		wchar_t uninstall_wbuf[STRING_SIZE];
		wchar_t icon_wbuf[STRING_SIZE];
		wchar_t version_wbuf[STRING_SIZE];
		DWORD no_modify_repair;
		
		uninstall_wbuf[0] = L'"';
		// string_cat budgets against the whole STRING_SIZE and ignores
		// the quote we already placed: reserve the suffix so a long
		// install path can never truncate it mid-way.
		string_copy_with_bufsize(uninstall_wbuf + 1,STRING_SIZE - 1 - 16,install_path);
		string_cat_utf8(uninstall_wbuf,(const utf8_t *)"\\Uninstall.exe\"");
		
		string_copy(icon_wbuf,install_path);
		string_cat_utf8(icon_wbuf,(const utf8_t *)"\\voidImageViewer.exe,0");
		
		// the release identity straight from version.h - must match the tag
		string_printf(version_wbuf,"%s%s",VERSION_STRING,VERSION_TYPE);
		
		no_modify_repair = 1;
		
		RegSetValueExW(hkey,L"DisplayName",0,REG_SZ,(BYTE *)L"void Image Viewer",sizeof(L"void Image Viewer"));
		RegSetValueExW(hkey,L"DisplayVersion",0,REG_SZ,(BYTE *)version_wbuf,(string_get_length(version_wbuf) + 1) * sizeof(wchar_t));
		RegSetValueExW(hkey,L"Publisher",0,REG_SZ,(BYTE *)L"voidImageViewer_PLUS (voidtools fork)",sizeof(L"voidImageViewer_PLUS (voidtools fork)"));
		RegSetValueExW(hkey,L"InstallLocation",0,REG_SZ,(BYTE *)install_path,(string_get_length(install_path) + 1) * sizeof(wchar_t));
		RegSetValueExW(hkey,L"DisplayIcon",0,REG_SZ,(BYTE *)icon_wbuf,(string_get_length(icon_wbuf) + 1) * sizeof(wchar_t));
		RegSetValueExW(hkey,L"UninstallString",0,REG_SZ,(BYTE *)uninstall_wbuf,(string_get_length(uninstall_wbuf) + 1) * sizeof(wchar_t));
		RegSetValueExW(hkey,L"NoModify",0,REG_DWORD,(BYTE *)&no_modify_repair,sizeof(DWORD));
		RegSetValueExW(hkey,L"NoRepair",0,REG_DWORD,(BYTE *)&no_modify_repair,sizeof(DWORD));

		// the measured footprint in kilobytes - without it the apps
		// list answers with whatever its own scan feels like (or
		// nothing at all).
		{
			DWORD estimated_size;

			estimated_size = _viv_install_payload_kb(install_path);

			RegSetValueExW(hkey,L"EstimatedSize",0,REG_DWORD,(BYTE *)&estimated_size,sizeof(DWORD));
		}

		RegCloseKey(hkey);
	}
}
// remove the add/remove programs entry from both hives: the admin
// install writes hklm, a standard user install writes hkcu.
static void _viv_uninstall_add_remove_programs(void)
{
	// regdeletekeyw cannot reach the alternate registry view (msdn), so
	// the 64-bit view the install writes needs regdeletekeyexw - resolved
	// lazily inside os_reg_delete_key_ex because this path runs before
	// os_init. the plain regdeletekeyw sweep after it removes the
	// wow6432node copy older 32-bit builds left behind (idempotent, and a
	// no-op on 64-bit builds where both calls hit the same view).
	os_reg_delete_key_ex(HKEY_LOCAL_MACHINE,L"Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\voidImageViewer",KEY_WOW64_64KEY);
	os_reg_delete_key_ex(HKEY_CURRENT_USER,L"Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\voidImageViewer",KEY_WOW64_64KEY);
	RegDeleteKeyW(HKEY_LOCAL_MACHINE,L"Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\voidImageViewer");
	RegDeleteKeyW(HKEY_CURRENT_USER,L"Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\voidImageViewer");
}
static void _viv_install_copy_file(const wchar_t *install_path,const wchar_t *temp_path,const utf8_t *filename,int critical)
{
	wchar_t dst_wbuf[STRING_SIZE];
	wchar_t src_wbuf[STRING_SIZE];
	
	string_path_combine_utf8(dst_wbuf,install_path,filename);
	string_path_combine_utf8(src_wbuf,temp_path,filename);
	
	if (!CopyFile(src_wbuf,dst_wbuf,FALSE))
	{
		if (critical)
		{
			debug_fatal("Error %d: unable to copy %S to %S",GetLastError(),src_wbuf,dst_wbuf);
		}
	}	
}
void _viv_get_exe_filename(wchar_t filename[STRING_SIZE])
{
	filename[0] = 0;
	
	GetModuleFileName(0,filename,STRING_SIZE);
	
	// make sure the drive letter is uppercase.
	// launching from .png == d:
	// launching from .exe == D:
	if ((*filename >= 'a') && (*filename <= 'z'))
	{
		if (filename[1] == ':')
		{
			*filename = *filename - 'a' + 'A';
		}
	}
}
