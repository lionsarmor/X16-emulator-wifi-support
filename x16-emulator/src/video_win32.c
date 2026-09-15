// All rights reserved. License: 2-clause BSD

#include <SDL.h>
#include <SDL_syswm.h>

#include <windows.h>
#include <dwmapi.h>

// The mingw-w64 dwmapi.h shipped by most Linux distros predates the Windows
// 11 SDK and doesn't declare these yet. Only used for cross-compiling with
// mingw-w64; a real Windows SDK (MSVC, or a newer mingw-w64) already has
// them, so these guards are no-ops there.
#ifndef DWMWA_WINDOW_CORNER_PREFERENCE
#define DWMWA_WINDOW_CORNER_PREFERENCE 33
typedef enum {
	DWMWCP_DEFAULT = 0,
	DWMWCP_DONOTROUND = 1,
	DWMWCP_ROUND = 2,
	DWMWCP_ROUNDSMALL = 3
} DWM_WINDOW_CORNER_PREFERENCE;
#endif

void video_win32_set_rounded_corners(SDL_Window *window)
{
	SDL_SysWMinfo wmInfo;
	SDL_VERSION(&wmInfo.version);
	SDL_GetWindowWMInfo(window, &wmInfo);

	HWND hwnd = wmInfo.info.win.window;
	DWM_WINDOW_CORNER_PREFERENCE preference = DWMWCP_ROUNDSMALL;
	DwmSetWindowAttribute(hwnd, DWMWA_WINDOW_CORNER_PREFERENCE, &preference, sizeof(preference));
}
