/*  pwn - simple multiplayer chess game
 *
 *  Copyright (C) 2020 Jona Ackerschott
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#ifndef GFXH_H
#define GFXH_H

#include "pwn.h"
#include "game.h"

#define GFXH_STACKSIZE (4096 * 64)

enum {
	TOUCH_PRESS = 0,
	TOUCH_RELEASE = 1,
};

enum {
	GFXH_EVENT_CLIENTMESSAGE,
	GFXH_EVENT_REDRAW,
	GFXH_EVENT_TOUCH,
	GFXH_EVENT_KEYTOUCH,
	GFXH_EVENT_INIT,
	GFXH_EVENT_PLAYMOVE,
	GFXH_EVENT_STATUSCHANGE,
};
struct gfxh_event_clientmessage {
	int type;
	union {
		char b[20];
		short s[10];
		long l[5];
	} data;
};
struct gfxh_event_redraw {
	int type;
	int width, height;
};
struct gfxh_event_touch {
	int type;
	int x;
	int y;
	int flags;
};
union gfxh_event_t {
	int type;
	struct gfxh_event_clientmessage clientmessage;
	struct gfxh_event_redraw redraw;
	struct gfxh_event_touch touch;
};

enum {
	ATOM_DELETE_WINDOW,
	ATOM_NAME,
	ATOM_ICON_NAME,
	ATOM_COUNT
};

enum {
	GFXH_IS_DRAWING = 1,
};
struct gfxh_args_t {
	struct handler_context_t *hctx;

	Display *dpy;
	Window winmain;
	Visual *vis;
	Atom atoms[ATOM_COUNT];
};

void init_communication_server(const char* node, const char *port, color_t color, long gametime);
void init_communication_client(const char *node, const char *port);

void *gfxh_main(void *args);

#endif /* GFHX_H */
