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

#define GFXH_STACKSIZE (4096 * 64)

enum {
	EVENT_CLIENTMESSAGE,
	EVENT_REDRAW,
	EVENT_TOUCH,
	EVENT_KEYTOUCH,
	EVENT_PLAYMOVE,
};
enum {
	TOUCH_PRESS = 0,
	TOUCH_RELEASE = 1,
};
struct event_clientmessage {
	int type;
	union {
		char b[20];
		short s[10];
		long l[5];
	} data;
};
struct event_redraw {
	int type;
	int width, height;
};
struct event_touch {
	int type;
	int x;
	int y;
	int flags;
};
struct event_playmove {
	int type;
	piece_t piece;
	fid from[2];
	fid to[2];
	piece_t prompiece;
	long tmove;
	long tstamp;
};
union event_t {
	int type;
	struct event_redraw redraw;
	struct event_touch touch;
	struct event_playmove playmove;
	struct event_clientmessage clientmessage;
};

struct handler_t {
	pthread_t id;
	int pevent[2];
	int pconfirm[2];
	int state;
};
struct handler_context_t {
	struct handler_t gfxh;

	pthread_mutex_t gamelock;
	pthread_mutex_t xlock;
	pthread_mutex_t gfxhlock;
	pthread_mutex_t mainlock;

	int terminate;
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

int hread(int fd, void *buf, size_t size);
int hwrite(int fd, void *buf, size_t size);

void init_communication_server(const char* node, const char *port, color_t color, long gametime);
void init_communication_client(const char *node, const char *port);

void *gfxh_main(void *args);

#endif /* GFHX_H */
