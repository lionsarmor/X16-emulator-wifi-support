
#ifndef _RENDERTEXT_H
#define _RENDERTEXT_H

#include <SDL.h>

#define CHAR_SCALE 		(1)										// character pixel size.

extern int xPos;
extern int yPos;

void DEBUGInitChars(SDL_Renderer *renderer);
void DEBUGWrite(SDL_Renderer *renderer, int x, int y, int ch, SDL_Color colour);
void DEBUGString(SDL_Renderer *renderer, int x, int y, char *s, SDL_Color colour);
char *ltrim(char *s);

// Pixel-precise variants (not grid-cell based like DEBUGWrite/DEBUGString
// above): x/y/scale are all real pixels in the current render target, for
// UI that isn't laid out on the debugger's fixed character grid - e.g. the
// on-screen keyboard (see osk.c). Returns the pixel width the string was
// drawn at, so callers can center labels.
void TextWritePixel(SDL_Renderer *renderer, int x, int y, int ch, SDL_Color colour, int scale);
int TextStringPixel(SDL_Renderer *renderer, int x, int y, const char *s, SDL_Color colour, int scale);
int TextStringPixelWidth(const char *s, int scale);

#endif
