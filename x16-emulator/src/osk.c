// On-screen keyboard.
//
// Why this exists: a phone or a touchscreen has no physical keyboard, and
// relying only on the host OS's own IME (see video.c's SDL_StartTextInput()
// call) only covers plain typing - letters, digits, backspace, enter. A lot
// of real X16 software also needs arrow keys, function keys, RUN/STOP, and
// RESTORE, none of which any IME produces. This draws an actual keyboard
// as part of the emulator's own rendering, identically on Linux, Windows,
// and Android, with a small toggle icon to show/hide it.
//
// Design: a toggle icon is always drawn in a corner. Tapping it shows a
// full key layout across the bottom of the screen. Tapping a regular key
// sends a real handle_keyboard() keydown when the finger/mouse goes down
// and the matching keyup when it lifts, exactly like a physical key press
// held for that long (so KERNAL key-repeat works normally too). Shift and
// Ctrl are latch/toggle keys instead, since chording two on-screen taps at
// once is awkward - tap once to hold it down, tap again to release.
// RESTORE maps to the same NMI real hardware wires it to, not a scancode.

#include <string.h>

#include "osk.h"
#include "rendertext.h"
#include "keyboard.h"
#include "glue.h"

typedef enum {
	OSK_KEY,       // ordinary key: press on finger-down, release on finger-up
	OSK_MODIFIER,  // Shift/Ctrl: toggles held on/off each tap
	OSK_RESTORE,   // NMI, edge-triggered, no up/down state
} OskActionType;

typedef struct {
	const char *label;
	OskActionType action;
	SDL_Keycode sym;
	SDL_Scancode scancode;
	float width; // in units of BASE_KEY_SIZE
} OskKey;

#define W(x) (x)

static const OskKey ROW_FN[] = {
	{ "RS", OSK_KEY, SDLK_ESCAPE, SDL_SCANCODE_ESCAPE, W(1.25f) }, // RUN/STOP
	{ "F1", OSK_KEY, SDLK_F1, SDL_SCANCODE_F1, W(1) },
	{ "F2", OSK_KEY, SDLK_F2, SDL_SCANCODE_F2, W(1) },
	{ "F3", OSK_KEY, SDLK_F3, SDL_SCANCODE_F3, W(1) },
	{ "F4", OSK_KEY, SDLK_F4, SDL_SCANCODE_F4, W(1) },
	{ "F5", OSK_KEY, SDLK_F5, SDL_SCANCODE_F5, W(1) },
	{ "F6", OSK_KEY, SDLK_F6, SDL_SCANCODE_F6, W(1) },
	{ "F7", OSK_KEY, SDLK_F7, SDL_SCANCODE_F7, W(1) },
	{ "F8", OSK_KEY, SDLK_F8, SDL_SCANCODE_F8, W(1) },
	{ "RESTORE", OSK_RESTORE, 0, 0, W(2) },
};

static const OskKey ROW_DIGITS[] = {
	{ "`", OSK_KEY, SDLK_BACKQUOTE, SDL_SCANCODE_GRAVE, W(1) },
	{ "1", OSK_KEY, SDLK_1, SDL_SCANCODE_1, W(1) },
	{ "2", OSK_KEY, SDLK_2, SDL_SCANCODE_2, W(1) },
	{ "3", OSK_KEY, SDLK_3, SDL_SCANCODE_3, W(1) },
	{ "4", OSK_KEY, SDLK_4, SDL_SCANCODE_4, W(1) },
	{ "5", OSK_KEY, SDLK_5, SDL_SCANCODE_5, W(1) },
	{ "6", OSK_KEY, SDLK_6, SDL_SCANCODE_6, W(1) },
	{ "7", OSK_KEY, SDLK_7, SDL_SCANCODE_7, W(1) },
	{ "8", OSK_KEY, SDLK_8, SDL_SCANCODE_8, W(1) },
	{ "9", OSK_KEY, SDLK_9, SDL_SCANCODE_9, W(1) },
	{ "0", OSK_KEY, SDLK_0, SDL_SCANCODE_0, W(1) },
	{ "-", OSK_KEY, SDLK_MINUS, SDL_SCANCODE_MINUS, W(1) },
	{ "=", OSK_KEY, SDLK_EQUALS, SDL_SCANCODE_EQUALS, W(1) },
	{ "DEL", OSK_KEY, SDLK_BACKSPACE, SDL_SCANCODE_BACKSPACE, W(1.75f) },
};

static const OskKey ROW_QWERTY[] = {
	{ "TAB", OSK_KEY, SDLK_TAB, SDL_SCANCODE_TAB, W(1.5f) },
	{ "Q", OSK_KEY, SDLK_q, SDL_SCANCODE_Q, W(1) },
	{ "W", OSK_KEY, SDLK_w, SDL_SCANCODE_W, W(1) },
	{ "E", OSK_KEY, SDLK_e, SDL_SCANCODE_E, W(1) },
	{ "R", OSK_KEY, SDLK_r, SDL_SCANCODE_R, W(1) },
	{ "T", OSK_KEY, SDLK_t, SDL_SCANCODE_T, W(1) },
	{ "Y", OSK_KEY, SDLK_y, SDL_SCANCODE_Y, W(1) },
	{ "U", OSK_KEY, SDLK_u, SDL_SCANCODE_U, W(1) },
	{ "I", OSK_KEY, SDLK_i, SDL_SCANCODE_I, W(1) },
	{ "O", OSK_KEY, SDLK_o, SDL_SCANCODE_O, W(1) },
	{ "P", OSK_KEY, SDLK_p, SDL_SCANCODE_P, W(1) },
	{ "[", OSK_KEY, SDLK_LEFTBRACKET, SDL_SCANCODE_LEFTBRACKET, W(1) },
	{ "]", OSK_KEY, SDLK_RIGHTBRACKET, SDL_SCANCODE_RIGHTBRACKET, W(1) },
};

static const OskKey ROW_HOME[] = {
	{ "CTRL", OSK_MODIFIER, SDLK_LCTRL, SDL_SCANCODE_LCTRL, W(1.75f) },
	{ "A", OSK_KEY, SDLK_a, SDL_SCANCODE_A, W(1) },
	{ "S", OSK_KEY, SDLK_s, SDL_SCANCODE_S, W(1) },
	{ "D", OSK_KEY, SDLK_d, SDL_SCANCODE_D, W(1) },
	{ "F", OSK_KEY, SDLK_f, SDL_SCANCODE_F, W(1) },
	{ "G", OSK_KEY, SDLK_g, SDL_SCANCODE_G, W(1) },
	{ "H", OSK_KEY, SDLK_h, SDL_SCANCODE_H, W(1) },
	{ "J", OSK_KEY, SDLK_j, SDL_SCANCODE_J, W(1) },
	{ "K", OSK_KEY, SDLK_k, SDL_SCANCODE_K, W(1) },
	{ "L", OSK_KEY, SDLK_l, SDL_SCANCODE_L, W(1) },
	{ ";", OSK_KEY, SDLK_SEMICOLON, SDL_SCANCODE_SEMICOLON, W(1) },
	{ "'", OSK_KEY, SDLK_QUOTE, SDL_SCANCODE_APOSTROPHE, W(1) },
	{ "RETURN", OSK_KEY, SDLK_RETURN, SDL_SCANCODE_RETURN, W(1.75f) },
};

static const OskKey ROW_SHIFT[] = {
	{ "SHIFT", OSK_MODIFIER, SDLK_LSHIFT, SDL_SCANCODE_LSHIFT, W(2) },
	{ "Z", OSK_KEY, SDLK_z, SDL_SCANCODE_Z, W(1) },
	{ "X", OSK_KEY, SDLK_x, SDL_SCANCODE_X, W(1) },
	{ "C", OSK_KEY, SDLK_c, SDL_SCANCODE_C, W(1) },
	{ "V", OSK_KEY, SDLK_v, SDL_SCANCODE_V, W(1) },
	{ "B", OSK_KEY, SDLK_b, SDL_SCANCODE_B, W(1) },
	{ "N", OSK_KEY, SDLK_n, SDL_SCANCODE_N, W(1) },
	{ "M", OSK_KEY, SDLK_m, SDL_SCANCODE_M, W(1) },
	{ ",", OSK_KEY, SDLK_COMMA, SDL_SCANCODE_COMMA, W(1) },
	{ ".", OSK_KEY, SDLK_PERIOD, SDL_SCANCODE_PERIOD, W(1) },
	{ "/", OSK_KEY, SDLK_SLASH, SDL_SCANCODE_SLASH, W(1) },
	{ "SHIFT", OSK_MODIFIER, SDLK_LSHIFT, SDL_SCANCODE_LSHIFT, W(2) },
};

static const OskKey ROW_BOTTOM[] = {
	{ "SPACE", OSK_KEY, SDLK_SPACE, SDL_SCANCODE_SPACE, W(6) },
	{ "\x7f", OSK_KEY, SDLK_LEFT, SDL_SCANCODE_LEFT, W(1) },
	{ "^", OSK_KEY, SDLK_UP, SDL_SCANCODE_UP, W(1) },
	{ "v", OSK_KEY, SDLK_DOWN, SDL_SCANCODE_DOWN, W(1) },
	{ "\x7e", OSK_KEY, SDLK_RIGHT, SDL_SCANCODE_RIGHT, W(1) },
};

#undef W

typedef struct {
	const OskKey *keys;
	int count;
} OskRow;

static const OskRow ROWS[] = {
	{ ROW_FN, sizeof(ROW_FN) / sizeof(ROW_FN[0]) },
	{ ROW_DIGITS, sizeof(ROW_DIGITS) / sizeof(ROW_DIGITS[0]) },
	{ ROW_QWERTY, sizeof(ROW_QWERTY) / sizeof(ROW_QWERTY[0]) },
	{ ROW_HOME, sizeof(ROW_HOME) / sizeof(ROW_HOME[0]) },
	{ ROW_SHIFT, sizeof(ROW_SHIFT) / sizeof(ROW_SHIFT[0]) },
	{ ROW_BOTTOM, sizeof(ROW_BOTTOM) / sizeof(ROW_BOTTOM[0]) },
};
#define NUM_ROWS ((int)(sizeof(ROWS) / sizeof(ROWS[0])))
#define MAX_COLS 14

#define KEY_UNIT 34
#define KEY_GAP 3
#define TOGGLE_SIZE 30
#define TOGGLE_MARGIN 6

static bool g_open = false;
static bool g_shift_on = false;
static bool g_ctrl_on = false;

// One slot per concurrent press (finger or mouse) that's currently holding
// an OSK_KEY down, so the matching release goes to the same key even if
// several are held at once.
#define MAX_ACTIVE_PRESSES 8
typedef struct {
	bool used;
	bool is_mouse;
	SDL_FingerID finger_id;
	SDL_Keycode sym;
	SDL_Scancode scancode;
} ActivePress;
static ActivePress g_active[MAX_ACTIVE_PRESSES];

static SDL_Rect
toggle_rect(int logical_w, int logical_h)
{
	(void)logical_h;
	SDL_Rect r = { logical_w - TOGGLE_SIZE - TOGGLE_MARGIN, TOGGLE_MARGIN, TOGGLE_SIZE, TOGGLE_SIZE };
	return r;
}

static int
panel_height(void)
{
	return NUM_ROWS * (KEY_UNIT + KEY_GAP) + KEY_GAP;
}

// Finds the key (if any) at logical coordinates (x, y), while also handing
// back its on-screen rect (for highlighting). Returns NULL if the point is
// in the panel's background but not on any key.
static const OskKey *
key_at(int logical_w, int logical_h, int x, int y, SDL_Rect *out_rect)
{
	int top = logical_h - panel_height();
	if (y < top) {
		return NULL;
	}
	int row_index = (y - top) / (KEY_UNIT + KEY_GAP);
	if (row_index < 0 || row_index >= NUM_ROWS) {
		return NULL;
	}
	const OskRow *row = &ROWS[row_index];

	// Center the row horizontally.
	float total_units = 0;
	for (int i = 0; i < row->count; i++) {
		total_units += row->keys[i].width;
	}
	int row_width = (int)(total_units * KEY_UNIT + (row->count - 1) * KEY_GAP);
	int cursor = (logical_w - row_width) / 2;
	int row_top = top + row_index * (KEY_UNIT + KEY_GAP) + KEY_GAP;

	for (int i = 0; i < row->count; i++) {
		int key_w = (int)(row->keys[i].width * KEY_UNIT);
		if (x >= cursor && x < cursor + key_w) {
			if (out_rect) {
				SDL_Rect r = { cursor, row_top, key_w, KEY_UNIT - KEY_GAP };
				*out_rect = r;
			}
			return &row->keys[i];
		}
		cursor += key_w + KEY_GAP;
	}
	return NULL;
}

static void
event_to_logical(SDL_Renderer *renderer, SDL_Window *window, const SDL_Event *event, bool *is_down, bool *is_up, bool *is_mouse, SDL_FingerID *finger_id, float *lx, float *ly)
{
	*is_down = false;
	*is_up = false;
	*is_mouse = false;
	*finger_id = 0;

	if (event->type == SDL_MOUSEBUTTONDOWN || event->type == SDL_MOUSEBUTTONUP) {
		if (event->button.button != SDL_BUTTON_LEFT) {
			return;
		}
		if (event->button.which == SDL_TOUCH_MOUSEID) {
			// SDL synthesizes a matching mouse event for every real touch
			// event too, for apps that only handle a mouse. Without this
			// check, a single tap here would be processed twice - once as
			// SDL_FINGERDOWN/UP, once as this synthesized mouse event -
			// toggling the keyboard open then immediately closed again
			// every time, which looked like the toggle just didn't work.
			return;
		}
		*is_mouse = true;
		*is_down = (event->type == SDL_MOUSEBUTTONDOWN);
		*is_up = (event->type == SDL_MOUSEBUTTONUP);
		SDL_RenderWindowToLogical(renderer, event->button.x, event->button.y, lx, ly);
	} else if (event->type == SDL_FINGERDOWN || event->type == SDL_FINGERUP) {
		*is_down = (event->type == SDL_FINGERDOWN);
		*is_up = (event->type == SDL_FINGERUP);
		*finger_id = event->tfinger.fingerId;
		int ww = 1, wh = 1;
		SDL_GetWindowSize(window, &ww, &wh);
		SDL_RenderWindowToLogical(renderer, (int)(event->tfinger.x * ww), (int)(event->tfinger.y * wh), lx, ly);
	}
}

static ActivePress *
find_active(bool is_mouse, SDL_FingerID finger_id)
{
	for (int i = 0; i < MAX_ACTIVE_PRESSES; i++) {
		if (g_active[i].used && g_active[i].is_mouse == is_mouse && (is_mouse || g_active[i].finger_id == finger_id)) {
			return &g_active[i];
		}
	}
	return NULL;
}

static ActivePress *
alloc_active(void)
{
	for (int i = 0; i < MAX_ACTIVE_PRESSES; i++) {
		if (!g_active[i].used) {
			return &g_active[i];
		}
	}
	return NULL;
}

void
osk_init(void)
{
	g_open = false;
	g_shift_on = false;
	g_ctrl_on = false;
	memset(g_active, 0, sizeof(g_active));
}

bool
osk_handle_event(SDL_Renderer *renderer, SDL_Window *window, const SDL_Event *event)
{
	bool is_down, is_up, is_mouse;
	SDL_FingerID finger_id;
	float lxf, lyf;
	event_to_logical(renderer, window, event, &is_down, &is_up, &is_mouse, &finger_id, &lxf, &lyf);
	if (!is_down && !is_up) {
		return false;
	}
	int logical_w, logical_h;
	SDL_RenderGetLogicalSize(renderer, &logical_w, &logical_h);
	int x = (int)lxf, y = (int)lyf;

	if (is_down) {
		SDL_Rect toggle = toggle_rect(logical_w, logical_h);
		if (x >= toggle.x && x < toggle.x + toggle.w && y >= toggle.y && y < toggle.y + toggle.h) {
			g_open = !g_open;
			return true;
		}
		if (!g_open) {
			return false;
		}
		if (y < logical_h - panel_height()) {
			return false;
		}
		SDL_Rect key_rect;
		const OskKey *key = key_at(logical_w, logical_h, x, y, &key_rect);
		if (key) {
			if (key->action == OSK_RESTORE) {
				machine_nmi();
			} else if (key->action == OSK_MODIFIER) {
				bool *state = (key->scancode == SDL_SCANCODE_LSHIFT) ? &g_shift_on : &g_ctrl_on;
				*state = !*state;
				handle_keyboard(*state, key->sym, key->scancode);
			} else {
				ActivePress *slot = alloc_active();
				if (slot) {
					slot->used = true;
					slot->is_mouse = is_mouse;
					slot->finger_id = finger_id;
					slot->sym = key->sym;
					slot->scancode = key->scancode;
				}
				handle_keyboard(true, key->sym, key->scancode);
			}
		}
		return true; // consume every tap inside the panel, hit or not
	} else {
		ActivePress *slot = find_active(is_mouse, finger_id);
		if (slot) {
			handle_keyboard(false, slot->sym, slot->scancode);
			slot->used = false;
			return true;
		}
		return g_open && y >= logical_h - panel_height();
	}
}

static void
draw_key(SDL_Renderer *renderer, SDL_Rect rect, const char *label, bool active)
{
	if (active) {
		SDL_SetRenderDrawColor(renderer, 90, 170, 90, 235);
	} else {
		SDL_SetRenderDrawColor(renderer, 55, 55, 60, 220);
	}
	SDL_RenderFillRect(renderer, &rect);
	SDL_SetRenderDrawColor(renderer, 150, 150, 155, 255);
	SDL_RenderDrawRect(renderer, &rect);

	SDL_Color white = { 230, 230, 230, 255 };
	int scale = 2;
	int text_w = TextStringPixelWidth(label, scale);
	int tx = rect.x + (rect.w - text_w) / 2;
	int ty = rect.y + (rect.h - 7 * scale) / 2;
	TextStringPixel(renderer, tx, ty, label, white, scale);
}

static void
draw_toggle_icon(SDL_Renderer *renderer, SDL_Rect r)
{
	SDL_SetRenderDrawColor(renderer, g_open ? 90 : 55, g_open ? 170 : 55, g_open ? 90 : 60, 220);
	SDL_RenderFillRect(renderer, &r);
	SDL_SetRenderDrawColor(renderer, 200, 200, 205, 255);
	SDL_RenderDrawRect(renderer, &r);

	// A small "keyboard" glyph: an outer body plus a grid of key dots.
	SDL_Rect body = { r.x + 4, r.y + 8, r.w - 8, r.h - 14 };
	SDL_RenderDrawRect(renderer, &body);
	for (int row = 0; row < 2; row++) {
		for (int col = 0; col < 5; col++) {
			SDL_Rect dot = { body.x + 2 + col * (body.w - 4) / 5, body.y + 2 + row * (body.h - 4) / 2, (body.w - 4) / 5 - 1, (body.h - 4) / 2 - 1 };
			SDL_RenderFillRect(renderer, &dot);
		}
	}
}

void
osk_render(SDL_Renderer *renderer, int logical_w, int logical_h)
{
	SDL_BlendMode old_blend;
	SDL_GetRenderDrawBlendMode(renderer, &old_blend);
	SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);

	draw_toggle_icon(renderer, toggle_rect(logical_w, logical_h));

	if (g_open) {
		int top = logical_h - panel_height();
		SDL_Rect panel = { 0, top, logical_w, panel_height() };
		SDL_SetRenderDrawColor(renderer, 20, 20, 22, 235);
		SDL_RenderFillRect(renderer, &panel);

		for (int ri = 0; ri < NUM_ROWS; ri++) {
			const OskRow *row = &ROWS[ri];
			float total_units = 0;
			for (int i = 0; i < row->count; i++) {
				total_units += row->keys[i].width;
			}
			int row_width = (int)(total_units * KEY_UNIT + (row->count - 1) * KEY_GAP);
			int cursor = (logical_w - row_width) / 2;
			int row_top = top + ri * (KEY_UNIT + KEY_GAP) + KEY_GAP;

			for (int i = 0; i < row->count; i++) {
				int key_w = (int)(row->keys[i].width * KEY_UNIT);
				SDL_Rect rect = { cursor, row_top, key_w, KEY_UNIT - KEY_GAP };
				bool active = false;
				if (row->keys[i].action == OSK_MODIFIER) {
					active = (row->keys[i].scancode == SDL_SCANCODE_LSHIFT) ? g_shift_on : g_ctrl_on;
				}
				draw_key(renderer, rect, row->keys[i].label, active);
				cursor += key_w + KEY_GAP;
			}
		}
	}

	SDL_SetRenderDrawBlendMode(renderer, old_blend);
}
