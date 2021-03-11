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

#ifndef CHESS_H
#define CHESS_H

#include <stdio.h>
#include <stdlib.h>
#include <errno.h>

#include <X11/Xdefs.h>
#include <cairo/cairo-xlib.h>

#define PROGNAME "pwn"
#ifndef DATADIR
#define DATADIR "/home/jona/it/dev/git/pwn/"
#endif

/* generic macros */
#define MAX(x, y) (((x) > (y)) ? (x) : (y))
#define MIN(x, y) (((x) < (y)) ? (x) : (y))
#define SGN(i) ((i > 0) - (i < 0))

#define ARRNUM(x) (sizeof(x) / sizeof((x)[0]))
#define STRLEN(x) (sizeof(x) - 1)

#define BUG() do { \
	fprintf(stderr, "BUG: failure at %s:%d/%s()!\n", __FILE__, __LINE__, __func__); \
	exit(EXIT_FAILURE); \
} while (0);
#define SYSERR() do { \
	fprintf(stderr, "%s:%d/%s: ", __FILE__, __LINE__, __func__); \
	fprintf(stderr, "%i ", errno); \
	perror(NULL); \
} while (0);
#define GAIERR(err) do { \
	fprintf(stderr, "%s:%d/%s: ", __FILE__, __LINE__, __func__); \
	fprintf(stderr, "%s", gai_strerror((err))); \
} while (0);

/* threading */
struct handler_t {
	pthread_t id;
	int pevent[2];
	int pconfirm[2];
	int state;
};
struct handler_context_t {
	struct handler_t gfxh;
	struct handler_t audioh;

	pthread_mutex_t gamelock;
	pthread_mutex_t xlock;
	pthread_mutex_t gfxhlock;
	pthread_mutex_t mainlock;

	int terminate;
};

#endif /* CHESS_H */
