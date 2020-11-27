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

#ifndef HANDLER_H
#define HANDLER_H

#include <pthread.h>
#include <X11/Xlib.h>

#include "pwn.h"

/* handler definitions */
enum {
	EVENT_REDRAW,
	EVENT_TOUCH,
	EVENT_PLAYMOVE,
	EVENT_SENDMOVE,
	EVENT_RECVMOVE,
	EVENT_TERM,
};
enum {
	TOUCH_RELEASE = 1,
};

struct event_redraw {
	char type;
	int width, height;
};
struct event_touch {
	char type;
	int x;
	int y;
	long ttouch;
	int flags;
};
struct event_passmove {
	char type;
	fid from[2];
	fid to[2];
	long tmove;
};
struct event_term {
	char type;
};
union event_t {
	char type;
	struct event_redraw redraw;
	struct event_touch touch;
	struct event_passmove playmove;
	struct event_passmove sendmove;
	struct event_passmove recvmove;
	struct event_term term;
};

struct eventhandler_t {
	int pevent[2];
	int pconfirm[2];
	int state;
};

struct handler_context_t {
	struct {
		pthread_t id;
		int pevent[2];
		int pconfirm[2];
		int state;
	} gfxh;
	struct {
		pthread_t id;
		int pevent[2];
		int pconfirm[2];
		int state;
	} comh;

	pthread_mutex_t gamelock;
	pthread_mutex_t xlock;
	pthread_mutex_t gfxhlock;
	pthread_mutex_t comhlock;
	pthread_mutex_t mainlock;

	int terminate;
};

int hread(int fd, void *buf, size_t size);
int hwrite(int fd, void *buf, size_t size);

#endif /* HANDLER_H */
