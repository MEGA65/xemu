/* Xemu - Somewhat lame emulation (running on Linux/Unix/Windows/OSX, utilizing
   SDL2) of some 8 bit machines, including the Commodore LCD and Commodore 65
   and some Mega-65 features as well.
   Copyright (C)2016,2017 LGB (Gábor Lénárt) <lgblgblgb@gmail.com>

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

#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/time.h>
#include <time.h>
#include <limits.h>
#include <errno.h>

#include "xemu/osd_font_16x16.c"


SDL_Window   *sdl_win = NULL;
SDL_Renderer *sdl_ren = NULL;
SDL_Texture  *sdl_tex = NULL;
SDL_PixelFormat *sdl_pix_fmt;
static const char default_window_title[] = "XEMU";
char *sdl_window_title = (char*)default_window_title;
char *window_title_custom_addon = NULL;
char *window_title_info_addon = NULL;
Uint32 *sdl_pixel_buffer = NULL;
int texture_x_size_in_bytes;
int emu_is_fullscreen = 0;
static int win_xsize, win_ysize;
char *sdl_pref_dir, *sdl_base_dir;
Uint32 sdl_winid;
static Uint32 black_colour;
static void (*shutdown_user_function)(void);
int seconds_timer_trigger;
SDL_version sdlver_compiled, sdlver_linked;
static char *window_title_buffer, *window_title_buffer_end;
static time_t unix_time;
static Uint64 et_old;
static int td_balancer, td_em_ALL, td_pc_ALL;
FILE *debug_fp = NULL;


static int osd_enabled = 0, osd_status = 0, osd_available = 0, osd_xsize, osd_ysize, osd_fade_dec, osd_fade_end, osd_alpha_last;
static Uint32 osd_colours[16], *osd_pixels = NULL, osd_colour_fg, osd_colour_bg;
static SDL_Texture *sdl_osdtex = NULL;
static SDL_bool grabbed_mouse = SDL_FALSE, grabbed_mouse_saved = SDL_FALSE;


#if !SDL_VERSION_ATLEAST(2, 0, 4)
#error "At least SDL version 2.0.4 is needed!"
#endif


void set_mouse_grab ( SDL_bool state )
{
	if (state != grabbed_mouse) {
		grabbed_mouse = state;
		SDL_SetRelativeMouseMode(state);
		SDL_SetWindowGrab(sdl_win, state);
	}
}


SDL_bool is_mouse_grab ( void )
{
	return grabbed_mouse;
}


void save_mouse_grab ( void )
{
	grabbed_mouse_saved = grabbed_mouse;
	set_mouse_grab(SDL_FALSE);
}


void restore_mouse_grab ( void )
{
	set_mouse_grab(grabbed_mouse_saved);
}


static inline int get_elapsed_time ( Uint64 t_old, Uint64 *t_new, time_t *store_unix_time )
{
#ifdef XEMU_OLD_TIMING
#define __TIMING_METHOD_DESC "gettimeofday"
	struct timeval tv;
	gettimeofday(&tv, NULL);
	if (store_unix_time)
		*store_unix_time = tv.tv_sec;
	*t_new = tv.tv_sec * 1000000UL + tv.tv_usec;
	return *t_new - t_old;
#else
#define __TIMING_METHOD_DESC "SDL_GetPerformanceCounter"
	if (store_unix_time)
		*store_unix_time = time(NULL);
	*t_new = SDL_GetPerformanceCounter();
	return 1000000UL * (*t_new - t_old) / SDL_GetPerformanceFrequency();
#endif
}



struct tm *emu_get_localtime ( void )
{
	return localtime(&unix_time);
}


time_t emu_get_unixtime ( void )
{
	return unix_time;
}


void *emu_malloc ( size_t size )
{
	void *p = malloc(size);
	if (!p)
		FATAL("Cannot allocate %d bytes of memory.", (int)size);
	return p;
}


void *emu_realloc ( void *p, size_t size )
{
	p = realloc(p, size);
	if (!p)
		FATAL("Cannot re-allocate %d bytes of memory.", (int)size);
	return p;
}


#ifdef HAVE_MM_MALLOC
#ifdef _WIN32
extern void *_mm_malloc ( size_t size, size_t alignment );	// it seems mingw/win has issue not to define this properly ... FIXME? Ugly windows, always the problems ...
#endif
void *emu_malloc_ALIGNED ( size_t size )
{
	// it seems _mm_malloc() is quite standard at least on gcc, mingw, clang ... so let's try to use it
	void *p = _mm_malloc(size, __BIGGEST_ALIGNMENT__);
	DEBUG("ALIGNED-ALLOC: base_pointer=%p size=%d alignment=%d" NL, p, (int)size, __BIGGEST_ALIGNMENT__);
	return p;
}
#else
#warning "No _mm_malloc() for this architecture ..."
#endif


char *emu_strdup ( const char *s )
{
	char *p = strdup(s);
	if (!p)
		FATAL("Cannot allocate memory for strdup()");
	return p;
}


// Just drop queued SDL events ...
void emu_drop_events ( void )
{
	SDL_PumpEvents();
	SDL_FlushEvent(SDL_KEYDOWN);
	SDL_FlushEvent(SDL_KEYUP);
	SDL_FlushEvent(SDL_MOUSEMOTION);
	SDL_FlushEvent(SDL_MOUSEWHEEL);
	SDL_FlushEvent(SDL_MOUSEBUTTONDOWN);
	SDL_FlushEvent(SDL_MOUSEBUTTONUP);
}



int emu_load_file ( const char *fn, void *buffer, int maxsize )
{
	char fnbuf[PATH_MAX + 1];
	char *search_paths[] = {
#ifdef __EMSCRIPTEN__
		EMSCRIPTEN_SDL_BASE_DIR,
#else
		".",
		"." DIRSEP_STR "rom",
		sdl_pref_dir,
		sdl_base_dir,
#ifndef _WIN32
		DATADIR,
#endif
#endif
		NULL
	};
	int a = 0, fd = -1, read_size = 0;
	if (fn[0] == '!') {
		fn++;
		search_paths[0] = "";
		search_paths[1] = NULL;
	} else if (
		fn[0] == DIRSEP_CHR ||
		(fn[0] == '.' && fn[1] == DIRSEP_CHR)
#ifdef _WIN32
		|| fn[1] == ':'
#endif
	) {
		search_paths[0] = "";
		search_paths[1] = NULL;
	}
	while (search_paths[a]) {
		if (search_paths[a][0] == 0)
			strcpy(fnbuf,  fn);
		else
			snprintf(fnbuf, sizeof fnbuf, "%s%c%s", search_paths[a], DIRSEP_CHR, fn);
		printf("Trying to open file \"%s\" as \"%s\" ..." NL, fn, fnbuf);
		fd = open(fnbuf, O_RDONLY | O_BINARY);	// O_BINARY is Windows stuff, but we define it as zero in case of non-Win32 system, so it won't hurt
		if (fd > -1)
			break;
		a++;
	}
	if (fd < 0) {
		fprintf(stderr, "Cannot open file %s" NL, fn);
		return -1;
	}
	printf("OK, file is open (fd = %d)" NL, fd);
	if (maxsize < 0) {
		if (buffer)
			strcpy(buffer, fnbuf);
		return fd;	// special mode to get a file descriptor, instead of loading anything ...
	}
	while (read_size < maxsize) {
		a = read(fd, buffer, maxsize - read_size);
		if (a < 0) {
			fprintf(stderr, "Reading error!" NL);
			return -1;
		}
		if (a == 0)
			break;
		buffer += a;
		read_size += a;
	}
	close(fd);
	return read_size;
}

/* Meaning of "setting":
	-1 (or any negative integer): toggle, switch between fullscreen / windowed mode automatically depending on the previous state
	 0: set windowed mode (if it's not that already, then nothing will happen)
	 1 (or any positive integer): set full screen mode (if it's not that already, then nothing will happen)
*/
void emu_set_full_screen ( int setting )
{
	if (setting > 1)
		setting = 1;
	if (setting < 0)
		setting = !emu_is_fullscreen;
	if (setting == emu_is_fullscreen)
		return; // do nothing, already that!
	if (setting) {
		// entering into full screen mode ....
		SDL_GetWindowSize(sdl_win, &win_xsize, &win_ysize); // save window size, it seems there are some problems with leaving fullscreen then
		if (SDL_SetWindowFullscreen(sdl_win, SDL_WINDOW_FULLSCREEN_DESKTOP)) {
			fprintf(stderr, "Cannot enter full screen mode: %s" NL, SDL_GetError());
		} else
			emu_is_fullscreen = 1;
	} else {
		// leaving full screen mode ...
		if (SDL_SetWindowFullscreen(sdl_win, 0)) {
			fprintf(stderr, "Cannot leave full screen mode: %s" NL, SDL_GetError());
		} else {
			emu_is_fullscreen = 0;
			SDL_SetWindowSize(sdl_win, win_xsize, win_ysize); // restore window size saved on leaving fullscreen, there can be some bugs ...
		}
	}
	SDL_RaiseWindow(sdl_win); // I have some problems with EP128 emulator that window went to the background. Let's handle that with raising it anyway :)
}



static inline void do_sleep ( int td )
{
#ifdef __EMSCRIPTEN__
#define __SLEEP_METHOD_DESC "emscripten_set_main_loop_timing"
	// Note: even if td is zero (or negative ...) give at least a little time for the browser
	// do not detect the our JS script as a run-away one, suggesting to kill ...
	// Note: this is not an actual sleep, we can't do that in JS. Instead of just "throttle"
	// the "frequency" our main loop is called. This also means, that do_sleep should be
	// called as last in case of emscripten target, since this does not sleep at all for real,
	// unlike the other sleep methods for non-js targets.
	emscripten_set_main_loop_timing(EM_TIMING_SETTIMEOUT, td > 999 ? td / 1000 : 1);
#elif XEMU_SLEEP_IS_SDL_DELAY
#define __SLEEP_METHOD_DESC "SDL_Delay"
	if (td > 0)
		SDL_Delay(td / 1000);
#elif defined(XEMU_SLEEP_IS_USLEEP)
#define __SLEEP_METHOD_DESC "usleep"
	if (td > 0)
		usleep(td);
#elif defined(XEMU_SLEEP_IS_NANOSLEEP)
#define __SLEEP_METHOD_DESC "nanosleep"
	struct timespec req, rem;
	if (td <= 0)
		return;
	td *= 1000;
	req.tv_sec  = td / 1000000000UL;
	req.tv_nsec = td % 1000000000UL;
	for (;;) {
		if (nanosleep(&req, &rem)) {
			if (errno == EINTR) {
				req.tv_sec = rem.tv_sec;
				req.tv_nsec = rem.tv_nsec;
			} else
				FATAL("nanosleep() returned with unhandable error");
		} else
			return;
	}
#else
#error "No SLEEP method is defined with XEMU_SLEEP_IS_* macros!"
#endif
}



/* Should be called regularly (eg on each screen global update), this function
   tries to keep the emulation speed near to real-time of the emulated machine.
   It's assumed that this function is called at least at every 0.1 sec or even
   more frequently ... Ideally it should be used at 25Hz rate (for PAL full TV
   frame) or 50Hz (for PAL half frame).
   Input: td_em: time in microseconds would be need on the REAL (emulated)
   machine to do the task, since the last call of this function! */
void emu_timekeeping_delay ( int td_em )
{
	int td, td_pc;
	time_t old_unix_time = unix_time;
	Uint64 et_new;
	td_pc = get_elapsed_time(et_old, &et_new, NULL);	// get realtime since last call in microseconds
	if (td_pc < 0) return; // time goes backwards? maybe time was modified on the host computer. Skip this delay cycle
	td = td_em - td_pc; // the time difference (+X = emu is faster (than emulated machine) - real time emulation, -X = emu is slower - real time emulation is not possible)
	td_balancer += td;
	if (td_balancer > 0)
		do_sleep(td_balancer);
	/* Purpose:
	 * get the real time spent sleeping (sleep is not an exact science on a multitask OS)
	 * also this will get the starter time for the next frame
	 */
	// calculate real time slept
	td = get_elapsed_time(et_new, &et_old, &unix_time);
	seconds_timer_trigger = (unix_time != old_unix_time);
	if (seconds_timer_trigger) {
		snprintf(window_title_buffer_end, 32, "  [%d%%] %s %s",
			td_em_ALL ? (td_pc_ALL * 100 / td_em_ALL) : -1,
			window_title_custom_addon ? window_title_custom_addon : "running",
			window_title_info_addon ? window_title_info_addon : ""
		);
		SDL_SetWindowTitle(sdl_win, window_title_buffer);
		td_pc_ALL = td_pc;
		td_em_ALL = td_em;
	} else {
		td_pc_ALL += td_pc;
		td_em_ALL += td_em;
	}
	if (td < 0) return; // invalid, sleep was about for _minus_ time? eh, give me that time machine, dude! :)
	// Balancing real and wanted sleep time on long run
	// Insane big values are forgotten, maybe emulator was stopped, or something like that
	td_balancer -= td;
	if (td_balancer >  1000000)
		td_balancer = 0;
	else if (td_balancer < -1000000) {
		// reaching this means the anomaly above, OR simply the fact, that emulator is too slow to emulate in real time!
		td_balancer = 0;
	}
}



static void shutdown_emulator ( void )
{
	DEBUG("XEMU: Shutdown callback function has been called." NL);
	if (shutdown_user_function)
		shutdown_user_function();
	if (sdl_win)
		SDL_DestroyWindow(sdl_win);
	SDL_Quit();
	if (debug_fp) {
		fclose(debug_fp);
		debug_fp = NULL;
	}
	printf("XEMU: shutdown callback says good by(T)e to you!" NL);
}



int emu_init_debug ( const char *fn )
{
#ifdef DISABLE_DEBUG
	printf("Logging is disabled at compile-time." NL);
#else
	if (debug_fp) {
		ERROR_WINDOW("Debug file %s already used, you can't call emu_init_debug() twice!\nUse it before emu_init_sdl() if you need it!", fn);
		return 1;
	} else if (fn) {
		debug_fp = fopen(fn, "wb");
		if (!debug_fp) {
			ERROR_WINDOW("Cannot open requested debug file: %s", fn);
			return 1;
		}
		DEBUGPRINT("Logging into file: %s (fd=%d)." NL, fn, fileno(debug_fp));
		xemu_dump_version(debug_fp, NULL);
		return 0;
	}
#endif
	return 0;
}



/* The SDL init stuff
   Return value: 0 = ok, otherwise: ERROR, caller must exit, and can't use any other functionality, otherwise crash would happen.*/
int emu_init_sdl (
	const char *window_title,		// title of our window
	const char *app_organization,		// organization produced the application, used with SDL_GetPrefPath()
	const char *app_name,			// name of the application, used with SDL_GetPrefPath()
	int is_resizable,			// allow window resize? [0 = no]
	int texture_x_size, int texture_y_size,	// raw size of texture (in pixels)
	int logical_x_size, int logical_y_size,	// "logical" size in pixels, ie to correct aspect ratio, etc, can be the as texture of course, if it's OK ...
	int win_x_size, int win_y_size,		// default window size, in pixels [note: if logical/texture size combo does not match in ratio with this, black stripes you will see ...]
	Uint32 pixel_format,			// SDL pixel format we want to use (an SDL constant, like SDL_PIXELFORMAT_ARGB8888) Note: it can gave serve impact on performance, ARGB8888 recommended
	int n_colours,				// number of colours emulator wants to use
	const Uint8 *colours,			// RGB components of each colours, we need 3 * n_colours bytes to be passed!
	Uint32 *store_palette,			// this will be filled with generated palette, n_colours Uint32 values will be placed
	int render_scale_quality,		// render scale quality, must be 0, 1 or 2 _ONLY_
	int locked_texture_update,		// use locked texture method [non zero], or malloc'ed stuff [zero]. NOTE: locked access doesn't allow to _READ_ pixels and you must fill ALL pixels!
	void (*shutdown_callback)(void)		// callback function called on exit (can be nULL to not have any emulator specific stuff)
) {
	SDL_RendererInfo ren_info;
	char render_scale_quality_s[2];
	int a;
	if (!debug_fp)
		emu_init_debug(getenv("XEMU_DEBUG_FILE"));
	if (!debug_fp)
		printf("Logging into file: not enabled." NL);
	if (SDL_Init(
#ifdef __EMSCRIPTEN__
		// It seems there is an issue with emscripten SDL2: SDL_Init does not work if TIMER and/or HAPTIC is tried to be intialized or just "EVERYTHING" is used!!
		SDL_INIT_EVERYTHING & ~(SDL_INIT_TIMER | SDL_INIT_HAPTIC)
#else
		SDL_INIT_EVERYTHING
#endif
	) != 0) {
		ERROR_WINDOW("Cannot initialize SDL: %s", SDL_GetError());
		return 1;
	}
	shutdown_user_function = shutdown_callback;
	atexit(shutdown_emulator);
	SDL_VERSION(&sdlver_compiled);
        SDL_GetVersion(&sdlver_linked);
	printf( "SDL version: (%s) compiled with %d.%d.%d, used with %d.%d.%d on platform %s" NL
		"SDL system info: %d bits %s, %d cores, l1_line=%d, RAM=%dMbytes, CPU features: "
		"3DNow=%d AVX=%d AVX2=%d AltiVec=%d MMX=%d RDTSC=%d SSE=%d SSE2=%d SSE3=%d SSE41=%d SSE42=%d" NL
		"SDL drivers: video = %s, audio = %s" NL
		"Timing: sleep = %s, query = %s" NL,
		SDL_GetRevision(),
		sdlver_compiled.major, sdlver_compiled.minor, sdlver_compiled.patch,
		sdlver_linked.major, sdlver_linked.minor, sdlver_linked.patch,
		SDL_GetPlatform(),
		ARCH_BITS, ENDIAN_NAME, SDL_GetCPUCount(), SDL_GetCPUCacheLineSize(), SDL_GetSystemRAM(),
		SDL_Has3DNow(),SDL_HasAVX(),SDL_HasAVX2(),SDL_HasAltiVec(),SDL_HasMMX(),SDL_HasRDTSC(),SDL_HasSSE(),SDL_HasSSE2(),SDL_HasSSE3(),SDL_HasSSE41(),SDL_HasSSE42(),
		SDL_GetCurrentVideoDriver(), SDL_GetCurrentAudioDriver(),
		__SLEEP_METHOD_DESC, __TIMING_METHOD_DESC
	);
#ifdef __EMSCRIPTEN__
	MKDIR(EMSCRIPTEN_SDL_BASE_DIR);
	sdl_base_dir = emu_strdup(EMSCRIPTEN_SDL_BASE_DIR);
	sdl_pref_dir = emu_strdup(EMSCRIPTEN_SDL_BASE_DIR);
#else
	sdl_pref_dir = SDL_GetPrefPath(app_organization, app_name);
	if (!sdl_pref_dir) {
		ERROR_WINDOW("Cannot query SDL preferences directory: %s", SDL_GetError());
		return 1;
	}
	sdl_base_dir = SDL_GetBasePath();
	if (!sdl_base_dir) {
		ERROR_WINDOW("Cannot query SDL base directory: %s", SDL_GetError());
		return 1;
	}
#endif
	printf("SDL preferences directory: %s" NL, sdl_pref_dir);
	sdl_window_title = emu_strdup(window_title);
	sdl_win = SDL_CreateWindow(
		window_title,
		SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
		win_x_size, win_y_size,
		SDL_WINDOW_SHOWN | (is_resizable ? SDL_WINDOW_RESIZABLE : 0)
	);
	if (!sdl_win) {
		ERROR_WINDOW("Cannot create SDL window: %s", SDL_GetError());
		return 1;
	}
	window_title_buffer = emu_malloc(strlen(window_title) + 128);
	strcpy(window_title_buffer, window_title);
	window_title_buffer_end = window_title_buffer + strlen(window_title);
	//SDL_SetWindowMinimumSize(sdl_win, SCREEN_WIDTH, SCREEN_HEIGHT * 2);
	a = SDL_GetNumRenderDrivers();
	while (--a >= 0) {
		if (!SDL_GetRenderDriverInfo(a, &ren_info)) {
			printf("SDL renderer driver #%d: \"%s\"" NL, a, ren_info.name);	
		} else
			printf("SDL renderer driver #%d: FAILURE TO QUERY (%s)" NL, a, SDL_GetError());
	}
	sdl_ren = SDL_CreateRenderer(sdl_win, -1, SDL_RENDERER_ACCELERATED);
	if (!sdl_ren) {
		ERROR_WINDOW("Cannot create accelerated SDL renderer: %s", SDL_GetError());
		sdl_ren = SDL_CreateRenderer(sdl_win, -1, 0);
		if (!sdl_ren) {
			ERROR_WINDOW("... and not even non-accelerated driver could be created, giving up: %s", SDL_GetError());
			return 1;
		} else {
			INFO_WINDOW("Created non-accelerated driver. NOTE: it will severly affect the performance!");
		}
	}
	if (!SDL_GetRendererInfo(sdl_ren, &ren_info)) {
		printf("SDL renderer used: \"%s\" max_tex=%dx%d tex_formats=%d ", ren_info.name, ren_info.max_texture_width, ren_info.max_texture_height, ren_info.num_texture_formats);
		for (a = 0; a < ren_info.num_texture_formats; a++)
			printf("%c%s", a ? ' ' : '(', SDL_GetPixelFormatName(ren_info.texture_formats[a]));
		printf(")" NL);
	}
	SDL_RenderSetLogicalSize(sdl_ren, logical_x_size, logical_y_size);	// this helps SDL to know the "logical ratio" of screen, even in full screen mode when scaling is needed!
	sdl_tex = SDL_CreateTexture(sdl_ren, pixel_format, SDL_TEXTUREACCESS_STREAMING, texture_x_size, texture_y_size);
	if (!sdl_tex) {
		ERROR_WINDOW("Cannot create SDL texture: %s", SDL_GetError());
		return 1;
	}
	texture_x_size_in_bytes = texture_x_size * 4;
	sdl_winid = SDL_GetWindowID(sdl_win);
	/* Intitialize palette from given RGB components */
	sdl_pix_fmt = SDL_AllocFormat(pixel_format);
	black_colour = SDL_MapRGBA(sdl_pix_fmt, 0, 0, 0, 0xFF);	// used to initialize pixel buffer
	while (n_colours--)
		store_palette[n_colours] = SDL_MapRGBA(sdl_pix_fmt, colours[n_colours * 3], colours[n_colours * 3 + 1], colours[n_colours * 3 + 2], 0xFF);
	/* SDL hints */
	snprintf(render_scale_quality_s, 2, "%d", render_scale_quality);
	SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, render_scale_quality_s);		// render scale quality 0, 1, 2
	SDL_SetHint(SDL_HINT_VIDEO_X11_NET_WM_PING, "0");				// disable WM ping, SDL dialog boxes makes WMs things emu is dead (?)
	SDL_SetHint(SDL_HINT_RENDER_VSYNC, "0");					// disable vsync aligned screen rendering
#ifdef _WIN32
	SDL_SetHint(SDL_HINT_WINDOWS_NO_CLOSE_ON_ALT_F4, "1");				// 1 = disable ALT-F4 close on Windows
#endif
	SDL_SetHint(SDL_HINT_VIDEO_MINIMIZE_ON_FOCUS_LOSS, "0");			// 1 = do minimize the SDL_Window if it loses key focus when in fullscreen mode
	SDL_SetHint(SDL_HINT_VIDEO_ALLOW_SCREENSAVER, "1");				// 1 = enable screen saver
	/* texture access / buffer */
	if (!locked_texture_update)
		sdl_pixel_buffer = emu_malloc_ALIGNED(texture_x_size_in_bytes * texture_y_size);
	// play a single frame game, to set a consistent colour (all black ...) for the emulator. Also, it reveals possible errors with rendering
	emu_render_dummy_frame(black_colour, texture_x_size, texture_y_size);
	printf(NL);
	return 0;
}


// this is just for the time keeping stuff, to avoid very insane values (ie, years since the last update for the first call ...)
void emu_timekeeping_start ( void )
{
	(void)get_elapsed_time(0, &et_old, &unix_time);
	td_balancer = 0;
	td_em_ALL = 0;
	td_pc_ALL = 0;
	seconds_timer_trigger = 1;
}



void emu_render_dummy_frame ( Uint32 colour, int texture_x_size, int texture_y_size )
{
	int tail;
	Uint32 *pp = emu_start_pixel_buffer_access(&tail);
	int x, y;
	for (y = 0; y < texture_y_size; y++) {
		for (x = 0; x < texture_x_size; x++)
			*(pp++) = colour;
		pp += tail;
	}
	seconds_timer_trigger = 1;
	emu_update_screen();
}



/* You *MUST* call this _ONCE_ before any access of pixels of the rendering target
   after render is done. Then pixels can be written but especially in locked_texture
   mode, you CAN'T read the previous frame pixels back! Also then you need to update
   *ALL* pixels of the texture before calling emu_update_screen() func at the end!
   tail should have added at the end of each lines of the texture, in theory it should
   be zero (when it's not needed ...) but you CANNOT be sure, if it's really true!
   tail is meant in 4 bytes (ie Uint32 pointer)! */
Uint32 *emu_start_pixel_buffer_access ( int *texture_tail )
{
	if (sdl_pixel_buffer) {
		*texture_tail = 0;		// using non-locked texture access, "tail" is always zero
		return sdl_pixel_buffer;	// using non-locked texture access, return with the malloc'ed buffer
	} else {
		int pitch;
		void *pixels;
		if (SDL_LockTexture(sdl_tex, NULL, &pixels, &pitch))
			FATAL("Cannot lock texture: %s", SDL_GetError());
		if ((pitch & 3))
			FATAL("Not dword aligned texture pitch value got!");
		pitch -= texture_x_size_in_bytes;
		if (pitch < 0)
			FATAL("Negative pitch value got for the texture size!");
		*texture_tail = (pitch >> 2);
		return pixels;
	}
}


/* Call this, to "show" the result given by filled pixel buffer whose pointer is
   got by calling emu_start_pixel_buffer_access(). Please read the notes at
   emu_start_pixel_buffer_access() carefully, especially, if you use the locked
   texture method! */
void emu_update_screen ( void )
{
	if (sdl_pixel_buffer)
		SDL_UpdateTexture(sdl_tex, NULL, sdl_pixel_buffer, texture_x_size_in_bytes);
	else
		SDL_UnlockTexture(sdl_tex);
	if (seconds_timer_trigger)
		SDL_RenderClear(sdl_ren); // Note: it's not needed at any price, however eg with full screen or ratio mismatches, unused screen space will be corrupted without this!
	SDL_RenderCopy(sdl_ren, sdl_tex, NULL, NULL);
	if (osd_status) {
		if (osd_status < OSD_STATIC)
			osd_status -= osd_fade_dec;
		if (osd_status <= osd_fade_end) {
			DEBUG("OSD: end of fade at %d" NL, osd_status);
			osd_status = 0;
			osd_alpha_last = 0;
		} else {
			int alpha = osd_status > 0xFF ? 0xFF : osd_status;
			if (alpha != osd_alpha_last) {
				osd_alpha_last = alpha;
				SDL_SetTextureAlphaMod(sdl_osdtex, alpha);
			}
			SDL_RenderCopy(sdl_ren, sdl_osdtex, NULL, NULL);
		}
	}
	SDL_RenderPresent(sdl_ren);
}


void osd_clear ( void )
{
	if (osd_enabled) {
		DEBUG("OSD: osd_clear() called." NL);
		memset(osd_pixels, 0, osd_xsize * osd_ysize * 4);
	}
}


void osd_update ()
{
	if (osd_enabled) {
		DEBUG("OSD: osd_update() called." NL);
                SDL_UpdateTexture(sdl_osdtex, NULL, osd_pixels, osd_xsize * sizeof (Uint32));
	}
}


int osd_init ( int xsize, int ysize, const Uint8 *palette, int palette_entries, int fade_dec, int fade_end )
{
	int a;
	// start with disabled state, so we can abort our init process without need to disable this
	osd_status = 0;
	osd_enabled = 0;
	if (sdl_osdtex || osd_pixels)
		FATAL("Calling osd_init() multiple times?");
	sdl_osdtex = SDL_CreateTexture(sdl_ren, sdl_pix_fmt->format, SDL_TEXTUREACCESS_STREAMING, xsize, ysize);
	if (!sdl_osdtex) {
		ERROR_WINDOW("Error with SDL_CreateTexture(), OSD won't be available: %s", SDL_GetError());
		return 1;
	}
	if (SDL_SetTextureBlendMode(sdl_osdtex, SDL_BLENDMODE_BLEND)) {
		ERROR_WINDOW("Error with SDL_SetTextureBlendMode(), OSD won't be available: %s", SDL_GetError());
		SDL_DestroyTexture(sdl_osdtex);
		sdl_osdtex = NULL;
		return 1;
	}
	osd_pixels = malloc(xsize * ysize * 4);
	if (!osd_pixels) {
		ERROR_WINDOW("Not enough memory to allocate texture, OSD won't be available");
		SDL_DestroyTexture(sdl_osdtex);
		sdl_osdtex = NULL;
		return 1;
	}
	osd_xsize = xsize;
	osd_ysize = ysize;
	osd_fade_dec = fade_dec;
	osd_fade_end = fade_end;
	for (a = 0; a < palette_entries; a++)
		osd_colours[a] = SDL_MapRGBA(sdl_pix_fmt, palette[a << 2], palette[(a << 2) + 1], palette[(a << 2) + 2], palette[(a << 2) + 3]);
	osd_enabled = 1;	// great, everything is OK, we can set enabled state!
	osd_available = 1;
	osd_clear();
	osd_update();
	osd_set_colours(1, 0);
	DEBUG("OSD: init: %dx%d pixels, %d palette entries, %d fade_dec, %d fade_end" NL, xsize, ysize, palette_entries, fade_dec, fade_end);
	return 0;
}



int osd_init_with_defaults ( void )
{
	const Uint8 palette[] = {
		0, 0, 0, 0x80,		// black with alpha channel 0x80
		0xFF,0xFF,0xFF,0xFF	// white
	};
	return osd_init(
		OSD_TEXTURE_X_SIZE, OSD_TEXTURE_Y_SIZE,
		palette,
		sizeof(palette) >> 2,
		OSD_FADE_DEC_VAL,
		OSD_FADE_END_VAL
	);
}


void osd_on ( int value )
{
	if (osd_enabled) {
		osd_alpha_last = 0;	// force alphamod to set on next screen update
		osd_status = value;
		DEBUG("OSD: osd_on(%d) called." NL, value);
	}
}


void osd_off ( void )
{
	osd_status = 0;
	DEBUG("OSD: osd_off() called." NL);
}


void osd_global_enable ( int status )
{
	osd_enabled = (status && osd_available);
	osd_alpha_last = -1;
	osd_status = 0;
	DEBUG("OSD: osd_global_enable(%d), result of status = %d" NL, status, osd_enabled);
}


void osd_set_colours ( int fg_index, int bg_index )
{
	osd_colour_fg = osd_colours[fg_index];
	osd_colour_bg = osd_colours[bg_index];
	DEBUG("OSD: osd_set_colours(%d,%d) called." NL, fg_index, bg_index);
}


void osd_write_char ( int x, int y, char ch )
{
	int row;
	const Uint16 *s;
	int warn = 1;
	Uint32 *d = osd_pixels + y * osd_xsize + x;
	Uint32 *e = osd_pixels + osd_xsize * osd_ysize;
	if ((signed char)ch < 32)	// also for >127 chars, since they're negative in 2-complements 8 bit type
		ch = '?';
	s = font_16x16 + (((unsigned char)ch - 32) << 4);
	for (row = 0; row < 16; row++) {
		Uint16 mask = 0x8000;
		do {
			if (likely(d >= osd_pixels && d < e))
				*d = *s & mask ? osd_colour_fg : osd_colour_bg;
			else if (warn) {
				warn = 0;
				DEBUG("OSD: ERROR: out of OSD dimensions for char %c at starting point %d:%d" NL, ch, x, y);
			}
			d++;
			mask >>= 1;
		} while (mask);
		s++;
		d += osd_xsize - 16;
	}
}


void osd_write_string ( int x, int y, const char *s )
{
	if (y < 0)	// negative y: standard place for misc. notifications
		y = osd_ysize / 2;
	for (;;) {
		int len = 0, xt;
		if (!*s)
			break;
		while (s[len] && s[len] != '\n')
			len++;
		xt = (x < 0) ? ((osd_xsize - len * 16) / 2) : x;	// request for centered? (if x < 0)
		while (len--) {
			osd_write_char(xt, y, *s);
			s++;
			xt += 16;
		}
		y += 16;
		if (*s == '\n')
			s++;
	}
}


int _sdl_emu_secured_modal_box_ ( const char *items_in, const char *msg )
{
	char items_buf[512], *items = items_buf;
	int buttonid;
	SDL_MessageBoxButtonData buttons[16];
	SDL_MessageBoxData messageboxdata = {
		SDL_MESSAGEBOX_INFORMATION, /* .flags */
		sdl_win, /* .window */
		default_window_title, /* .title */
		msg, /* .message */
		0,      /* number of buttons, will be updated! */
		buttons,
		NULL    // &colorScheme
	};
	strcpy(items_buf, items_in);
	for (;;) {
		char *p = strchr(items, '|');
		switch (*items) {
			case '!':
#ifdef __EMSCRIPTEN__
				printf("Emscripten: faking chooser box answer %d for \"%s\"" NL, messageboxdata.numbuttons, msg);
				return messageboxdata.numbuttons;
#endif
				buttons[messageboxdata.numbuttons].flags = SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT;
				items++;
				break;
			case '?':
				buttons[messageboxdata.numbuttons].flags = SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT;
				items++;
				break;
			default:
				buttons[messageboxdata.numbuttons].flags = 0;
				break;
		}
		buttons[messageboxdata.numbuttons].text = items;
		buttons[messageboxdata.numbuttons].buttonid = messageboxdata.numbuttons;
		messageboxdata.numbuttons++;
		if (!p)
			break;
		*p = 0;
		items = p + 1;
	}
	save_mouse_grab();
	SDL_ShowMessageBox(&messageboxdata, &buttonid);
	clear_emu_events();
	emu_drop_events();
	SDL_RaiseWindow(sdl_win);
	restore_mouse_grab();
	emu_timekeeping_start();
	return buttonid;
}
