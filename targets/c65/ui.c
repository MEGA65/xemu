/* Part of the Xemu project, please visit: https://github.com/lgblgblgb/xemu
   Copyright (C)2016-2020 LGB (Gábor Lénárt) <lgblgblgb@gmail.com>

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA */


#include "xemu/emutools.h"
#include "ui.h"
#include "xemu/emutools_gui.h"
#include "xemu/emutools_files.h"
#include "xemu/d81access.h"
#include "xemu/emutools_hid.h"
#include "commodore_65.h"
#include "inject.h"
#include "vic3.h"


//#if defined(CONFIG_DROPFILE_CALLBACK) || defined(XEMU_GUI)

#if 0
static int attach_d81 ( const char *fn )
{
	if (fd_mounted) {
		if (mount_external_d81(fn, 0)) {
			ERROR_WINDOW("Mount failed for some reason.");
			return 1;
		} else {
			DEBUGPRINT("UI: file seems to be mounted successfully as D81: %s" NL, fn);
			return 0;
		}
	} else {
		ERROR_WINDOW("Cannot mount external D81, since Mega65 was not instructed to mount any FD access yet.");
		return 1;
	}
}
#endif


static int attach_d81 ( const char *fn )
{
	if (fn && *fn)
		return d81access_attach_fsobj(fn, D81ACCESS_IMG | D81ACCESS_PRG | D81ACCESS_DIR | D81ACCESS_AUTOCLOSE);
	return -1;
}


// end of #if defined(CONFIG_DROPFILE_CALLBACK) || defined(XEMU_GUI_C)
//#endif


#ifdef CONFIG_DROPFILE_CALLBACK
void emu_dropfile_callback ( const char *fn )
{
	DEBUGGUI("UI: drop event, file: %s" NL, fn);
	if (ARE_YOU_SURE("Shall I try to mount the dropped file as D81 for you?"))
		attach_d81(fn);
}
#endif


#if 1
static void ui_attach_d81_by_browsing ( void )
{
	char fnbuf[PATH_MAX + 1];
	static char dir[PATH_MAX + 1] = "";
	if (!xemugui_file_selector(
		XEMUGUI_FSEL_OPEN | XEMUGUI_FSEL_FLAG_STORE_DIR,
		"Select D81 to attach",
		dir,
		fnbuf,
		sizeof fnbuf
	))
		attach_d81(fnbuf);
	else
		DEBUGPRINT("UI: file selection for D81 mount was cancelled." NL);
}
#endif


static void ui_run_prg_by_browsing ( void )
{
	char fnbuf[PATH_MAX + 1];
	static char dir[PATH_MAX + 1] = "";
	if (!xemugui_file_selector(
		XEMUGUI_FSEL_OPEN | XEMUGUI_FSEL_FLAG_STORE_DIR,
		"Select PRG to directly load&run",
		dir,
		fnbuf,
		sizeof fnbuf
	)) {
		c65_reset();
		inject_register_prg(fnbuf, 0);
	} else
		DEBUGPRINT("UI: file selection for PRG injection was cancelled." NL);
}

static void ui_dump_memory ( void )
{
	char fnbuf[PATH_MAX + 1];
	static char dir[PATH_MAX + 1] = "";
	if (!xemugui_file_selector(
		XEMUGUI_FSEL_SAVE | XEMUGUI_FSEL_FLAG_STORE_DIR,
		"Dump memory content into file",
		dir,
		fnbuf,
		sizeof fnbuf
	)) {
		dump_memory(fnbuf);
	}
}

static void reset_into_c64_mode ( void )
{
	if (c65_reset_asked()) {
		// we need this, because autoboot disk image would bypass the "go to C64 mode" on 'Commodore key' feature
		// this call will deny disk access, and re-enable on the READY. state.
		inject_register_allow_disk_access();
		hid_set_autoreleased_key(0x75);
		KBD_PRESS_KEY(0x75);	// C= key is pressed for C64 mode
	}
}

static void reset_into_c65_mode ( void )
{
	if (c65_reset_asked()) {
		KBD_RELEASE_KEY(0x75);
	}
}

static void reset_into_c65_mode_noboot ( void )
{
	if (c65_reset_asked()) {
		inject_register_allow_disk_access();
		KBD_RELEASE_KEY(0x75);
	}
}

#if 0
static void ui_set_scale_filtering ( const struct menu_st *m, int *query )
{
	static char enabled[2] = "0";
	XEMUGUI_RETURN_CHECKED_ON_QUERY(query, (enabled[0] & 1));
	enabled[0] ^= 1;
	SDL_SetHint("SDL_HINT_RENDER_SCALE_QUALITY", enabled);
}
#endif

static void ui_cb_show_drive_led ( const struct menu_st *m, int *query )
{
	XEMUGUI_RETURN_CHECKED_ON_QUERY(query, show_drive_led);
	show_drive_led = !show_drive_led;
}



/**** MENU SYSTEM ****/


static const struct menu_st menu_display[] = {
	{ "Fullscreen",    		XEMUGUI_MENUID_CALLABLE,	xemugui_cb_windowsize, (void*)0 },
	{ "Window - 100%", 		XEMUGUI_MENUID_CALLABLE,	xemugui_cb_windowsize, (void*)1 },
	{ "Window - 200%", 		XEMUGUI_MENUID_CALLABLE |
					XEMUGUI_MENUFLAG_SEPARATOR,	xemugui_cb_windowsize, (void*)2 },
	{ "Enable mouse grab + emu",	XEMUGUI_MENUID_CALLABLE |
					XEMUGUI_MENUFLAG_QUERYBACK,	xemugui_cb_set_mouse_grab, NULL },
//	{ "Enable scale filtering",	XEMUGUI_MENUID_CALLABLE |
//					XEMUGUI_MENUFLAG_QUERYBACK,	ui_set_scale_filtering, NULL },
	{ "Show drive LED",		XEMUGUI_MENUID_CALLABLE |
					XEMUGUI_MENUFLAG_QUERYBACK,	ui_cb_show_drive_led, NULL },
	{ NULL }
};
static const struct menu_st menu_debug[] = {
	{ "OSD key debugger",		XEMUGUI_MENUID_CALLABLE |
					XEMUGUI_MENUFLAG_QUERYBACK,	xemugui_cb_osd_key_debugger, NULL },
	{ "Dump memory info file",	XEMUGUI_MENUID_CALLABLE,	xemugui_cb_call_user_data, ui_dump_memory },
	{ "Browse system folder",	XEMUGUI_MENUID_CALLABLE,	xemugui_cb_native_os_prefdir_browser, NULL },
	{ NULL }
};
static const struct menu_st menu_reset[] = {
	{ "Reset C65",  		XEMUGUI_MENUID_CALLABLE,	xemugui_cb_call_user_data, reset_into_c65_mode },
	{ "Reset C65 without autoboot",	XEMUGUI_MENUID_CALLABLE,	xemugui_cb_call_user_data, reset_into_c65_mode_noboot },
	{ "Reset into C64 mode",	XEMUGUI_MENUID_CALLABLE,	xemugui_cb_call_user_data, reset_into_c64_mode },
	{ NULL }
};
static const struct menu_st menu_main[] = {
	{ "Display",			XEMUGUI_MENUID_SUBMENU,		NULL, menu_display },
	{ "Reset", 	 		XEMUGUI_MENUID_SUBMENU,		NULL, menu_reset   },
	{ "Debug",			XEMUGUI_MENUID_SUBMENU,		NULL, menu_debug   },
	{ "Attach D81",			XEMUGUI_MENUID_CALLABLE,	xemugui_cb_call_user_data, ui_attach_d81_by_browsing },
	{ "Run PRG directly",		XEMUGUI_MENUID_CALLABLE,	xemugui_cb_call_user_data, ui_run_prg_by_browsing },
#ifdef XEMU_FILES_SCREENSHOT_SUPPORT
	{ "Screenshot",			XEMUGUI_MENUID_CALLABLE,	xemugui_cb_set_integer_to_one, &register_screenshot_request },
#endif
#ifdef XEMU_ARCH_WIN
	{ "System console",		XEMUGUI_MENUID_CALLABLE |
					XEMUGUI_MENUFLAG_QUERYBACK,	xemugui_cb_sysconsole, NULL },
#endif
	{ "About",			XEMUGUI_MENUID_CALLABLE,	xemugui_cb_about_window, NULL },
#ifdef HAVE_XEMU_EXEC_API
	{ "Help (on-line)",		XEMUGUI_MENUID_CALLABLE,	xemugui_cb_web_help_main, NULL },
#endif
	{ "Quit",			XEMUGUI_MENUID_CALLABLE,	xemugui_cb_call_quit_if_sure, NULL },
	{ NULL }
};


void ui_enter ( void )
{
	DEBUGGUI("UI: handler has been called." NL);
	if (xemugui_popup(menu_main)) {
		DEBUGPRINT("UI: oops, POPUP does not worked :(" NL);
	}
}
