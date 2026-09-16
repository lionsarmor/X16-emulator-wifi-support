// On-screen keyboard: a full X16 key layout plus a toggle drawn by the
// emulator itself, so it looks and works the same on Linux, Windows, and
// Android instead of relying on whatever (or nothing) the host OS provides.
// See osk.c for the design notes.
#ifndef _OSK_H_
#define _OSK_H_

#include <SDL.h>
#include <stdbool.h>

// One-time setup. Safe to call before or after the window/renderer exist;
// actual SDL resource creation is lazy (mirrors DEBUGInitChars's pattern).
void osk_init(void);

// Call once per frame, after the emulated screen has been drawn to
// `renderer` but before SDL_RenderPresent, so the toolbar/keyboard overlay
// draws on top. logical_w/logical_h are the renderer's current logical
// size (SDL_RenderGetLogicalSize), i.e. the same coordinate space the rest
// of the frame was just drawn in.
void osk_render(SDL_Renderer *renderer, int logical_w, int logical_h);

// Call for every SDL_Event before any other handling. Returns true if the
// toolbar or keyboard overlay consumed it (a tap on the toggle, or on a
// key), in which case the caller should not also treat it as a normal
// mouse/touch/keyboard event for the emulated machine.
bool osk_handle_event(SDL_Renderer *renderer, SDL_Window *window, const SDL_Event *event);

#endif
