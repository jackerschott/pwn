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

#include <fcntl.h>
#include <poll.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cairo/cairo-xlib.h>
#include <pthread.h>
#include <X11/Xlib.h>

#include "draw.h"
#include "game.h"

#include "gfxh.h"

#define FIGURE(p) ((p) / 2 - 1)
#define PALETTE(c) (c)
#define XTOI(x, xorig, fieldsize) ((int)(((x) - xorig) / fieldsize))
#define YTOJ(y, yorig, fieldsize, c) \
	((1 - c) * ((NF - 1) - (int)(((y) - yorig) / fieldsize)) \
	+ (c) * (int)(((y) - yorig) / fieldsize))
#define ITOX(i, xorig, fieldsize) (xorig + (i) * fieldsize)
#define JTOY(j, yorig, fieldsize, c) \
	(yorig + ((1 - c) * ((NF - 1) - (j)) + (c) * (j)) * fieldsize)

#define SECOND (1000L * 1000L * 1000L)
#define TIMESTRLEN 7
#define TITLE_MAXLEN (TIMESTRLEN + STRLEN(" - ") + TIMESTRLEN + MSG_MAXLEN)

#define RDIV(a, b) (((a) + ((b) - 1)) / (b))

int foppt;

struct timeinfo_t {
	long movestart;
	long subtotal;
	long total;
};
struct gameinfo_t {
	color_t selfcolor;
	status_t status;
	struct timeinfo_t tiself;
	struct timeinfo_t tioppt;
};
struct gameinfo_t gameinfo;
long tlastupdate;

static fid selfield[2];
static struct {
	cairo_surface_t *surface;
	double xorig;
	double yorig;
	double size;
	double fieldsize;
} board;

static Display *dpy;
static Window winmain;
static Visual *vis;
static Atom atoms[ATOM_COUNT];

static struct handler_context_t *hctx;
static int fevent;
static int fconfirm;
static int *state;
struct pollfd pfds[2];

static void gfxh_cleanup(void);

int hread(int fd, void *buf, size_t size)
{
	char *b = (char *)buf;
	while (size > 0) {
		size_t n = read(fd, b, size);
		if (n == -1) {
			if (errno == EINTR)
				continue;
			if (errno == EWOULDBLOCK || errno == EAGAIN)
				return 2;
			return -1;
		} else if (n == 0) {
			return 1;
		}

		b += n;
		size -= n;
	}
	return 0;
}
int hwrite(int fd, void *buf, size_t size)
{
	char *b = (char *)buf;
	while (size > 0) {
		size_t n = write(fd, b, size);
		if (n == -1) {
			if (errno == EINTR)
				continue;
			if (errno == EWOULDBLOCK || errno == EAGAIN)
				continue;
			if (errno == EPIPE)
				return 1;
			return -1;
		}

		b += n;
		size -= n;
	}
	return 0;
}
int hsend(int fd, void *buf, size_t size)
{
	char *b = (char *)buf;
	while (size > 0) {
		size_t n = send(fd, b, size, 0);
		if (n == -1) {
			if (errno == EINTR)
				continue;
			if (errno == EWOULDBLOCK || errno == EAGAIN)
				continue;
			if (errno == EPIPE)
				return 1;
			return -1;
		}

		b += n;
		size -= n;
	}
	return 0;
}
int hrecv(int fd, void *buf, size_t size)
{
	char *b = (char *)buf;
	while (size > 0) {
		size_t n = recv(fd, b, size, 0);
		if (n == -1) {
			if (errno == EINTR)
				continue;
			if (errno == EWOULDBLOCK || errno == EAGAIN)
				return 2;
			return -1;
		} else if (n == 0) {
			return 1;
		}

		b += n;
		size -= n;
	}
	return 0;
}

long gettimepoint(void)
{
	struct timespec tp;
	clock_gettime(CLOCK_MONOTONIC, &tp);
	return tp.tv_sec * SECOND + tp.tv_nsec;
}
void format_gametime(long t, char *str)
{
	int h = t / (SECOND * 3600);
	int m = t / (SECOND * 60) - 60 * h;
	int s = RDIV(t, SECOND) - 60 * m - 3600 * h;

	sprintf(str, "%i:%.2i:%.2i", h, m, s);
}
void show_status(struct gameinfo_t ginfo)
{
	char title[TITLE_MAXLEN + 1];
	format_gametime(ginfo.tiself.total, title);
	strcat(title, " - ");
	format_gametime(ginfo.tioppt.total, title + strlen(title));
	strcat(title, "  ");
	strcat(title, messages[ginfo.status]);

	pthread_mutex_lock(&hctx->xlock);
	XStoreName(dpy, winmain, title);
	pthread_mutex_unlock(&hctx->xlock);
}

static void selectf(fid f[2])
{
	pthread_mutex_lock(&hctx->xlock);
	draw_record();
	pthread_mutex_unlock(&hctx->xlock);

	double fx = ITOX(f[0], board.xorig, board.fieldsize);
	double fy = JTOY(f[1], board.yorig, board.fieldsize, gameinfo.selfcolor);
	pthread_mutex_lock(&hctx->xlock);
	draw_field(fx, fy, board.fieldsize, (f[0] + f[1]) % 2, 1);
	pthread_mutex_unlock(&hctx->xlock);

	pthread_mutex_lock(&hctx->gamelock);
	piece_t piece = game_get_piece(f[0], f[1]);
	pthread_mutex_unlock(&hctx->gamelock);
	if (piece) {
		pthread_mutex_lock(&hctx->gamelock);
		color_t color = game_get_color(f[0], f[1]);
		pthread_mutex_unlock(&hctx->gamelock);

		pthread_mutex_lock(&hctx->xlock);
		draw_piece(fx, fy, board.fieldsize, FIGURE(piece), PALETTE(color));
		pthread_mutex_unlock(&hctx->xlock);
	}

	pthread_mutex_lock(&hctx->xlock);
	draw_commit();
	pthread_mutex_unlock(&hctx->xlock);

	memcpy(selfield, f, 2 * sizeof(fid));
}
static void unselectf(void)
{
	pthread_mutex_lock(&hctx->xlock);
	draw_record();
	pthread_mutex_unlock(&hctx->xlock);

	double fx = ITOX(selfield[0], board.xorig, board.fieldsize);
	double fy = JTOY(selfield[1], board.yorig, board.fieldsize, gameinfo.selfcolor);
	pthread_mutex_lock(&hctx->xlock);
	draw_field(fx, fy, board.fieldsize, (selfield[0] + selfield[1]) % 2, 0);
	pthread_mutex_unlock(&hctx->xlock);

	pthread_mutex_lock(&hctx->gamelock);
	piece_t piece = game_get_piece(selfield[0], selfield[1]);
	pthread_mutex_unlock(&hctx->gamelock);
	if (piece) {
		pthread_mutex_lock(&hctx->gamelock);
		color_t color = game_get_color(selfield[0], selfield[1]);
		pthread_mutex_unlock(&hctx->gamelock);

		pthread_mutex_lock(&hctx->xlock);
		draw_piece(fx, fy, board.fieldsize, FIGURE(piece), PALETTE(color));
		pthread_mutex_unlock(&hctx->xlock);
	}

	pthread_mutex_lock(&hctx->xlock);
	draw_commit();
	pthread_mutex_unlock(&hctx->xlock);

	memset(selfield, 0xff, 2 * sizeof(fid));
}
static void show(fid updates[][2])
{
	double fx, fy;

	pthread_mutex_lock(&hctx->xlock);
	draw_record();
	pthread_mutex_unlock(&hctx->xlock);
	for (int k = 0; k < NUM_UPDATES_MAX && updates[k][0] != -1; ++k) {
		int i = updates[k][0];
		int j = updates[k][1];

		double fx = ITOX(i, board.xorig, board.fieldsize);
		double fy = JTOY(j, board.yorig, board.fieldsize, gameinfo.selfcolor);
		pthread_mutex_lock(&hctx->xlock);
		draw_field(fx, fy, board.fieldsize, (i + j) % 2, 0);
		pthread_mutex_unlock(&hctx->xlock);

		pthread_mutex_lock(&hctx->gamelock);
		int piece = game_get_piece(i, j);
		pthread_mutex_unlock(&hctx->gamelock);
		if (piece & PIECEMASK) {
			pthread_mutex_lock(&hctx->gamelock);
			int color = game_get_color(i, j);
			pthread_mutex_unlock(&hctx->gamelock);

			pthread_mutex_lock(&hctx->xlock);
			draw_piece(fx, fy, board.fieldsize, FIGURE(piece), PALETTE(color));
			pthread_mutex_unlock(&hctx->xlock);
		}
	}
	pthread_mutex_lock(&hctx->xlock);
	draw_commit();
	pthread_mutex_unlock(&hctx->xlock);
}
static void redraw(int width, int height)
{
	pthread_mutex_lock(&hctx->xlock);
	cairo_xlib_surface_set_size(board.surface, width, height);
	pthread_mutex_unlock(&hctx->xlock);

	double d = (width - height) / 2.0;
	if (d > 0) {
		board.xorig = d;
		board.yorig = 0;
		board.size = height;
		board.fieldsize = ((double)height) / NF;
	} else {
		board.xorig = 0;
		board.yorig = -d;
		board.size = width;
		board.fieldsize = ((double)width) / NF;
	}

	pthread_mutex_lock(&hctx->xlock);
	draw_record();
	draw_clear();
	pthread_mutex_unlock(&hctx->xlock);

	for (int j = 0; j < NF; ++j) {
		for (int i = 0; i < NF; ++i) {
			double fx = ITOX(i, board.xorig, board.fieldsize);
			double fy = JTOY(j, board.yorig, board.fieldsize, gameinfo.selfcolor);
			int sel = i == selfield[0] && j == selfield[1];
			pthread_mutex_lock(&hctx->xlock);
			draw_field(fx, fy, board.fieldsize, (i + j) % 2, sel);
			pthread_mutex_unlock(&hctx->xlock);

			pthread_mutex_lock(&hctx->gamelock);
			int piece = game_get_piece(i, j);
			pthread_mutex_unlock(&hctx->gamelock);
			if (piece & PIECEMASK) {
				pthread_mutex_lock(&hctx->gamelock);
				int color = game_get_color(i, j);
				pthread_mutex_unlock(&hctx->gamelock);

				pthread_mutex_lock(&hctx->xlock);
				draw_piece(fx, fy, board.fieldsize, FIGURE(piece), PALETTE(color));
				pthread_mutex_unlock(&hctx->xlock);
			}
		}
	}

	pthread_mutex_lock(&hctx->xlock);
	draw_commit();
	pthread_mutex_unlock(&hctx->xlock);
}

static void handle_clientmessage(struct event_clientmessage *e)
{
	if (e->data.l[0] == atoms[ATOM_DELETE_WINDOW]) {
		gfxh_cleanup();
		pthread_exit(NULL);
	}
}
static void handle_redraw(struct event_redraw *e)
{
	pthread_mutex_lock(&hctx->gfxhlock);
	*state |= GFXH_IS_DRAWING;
	pthread_mutex_unlock(&hctx->gfxhlock);

	int ret = 0;
	if (hwrite(fconfirm, &ret, sizeof(ret)) == -1) {
		SYSERR();
		gfxh_cleanup();
		pthread_exit(NULL);
	}

	redraw(e->width, e->height);

	pthread_mutex_lock(&hctx->gfxhlock);
	*state &= ~GFXH_IS_DRAWING;
	pthread_mutex_unlock(&hctx->gfxhlock);

	int confnote = 0;
	pthread_mutex_lock(&hctx->xlock);
	if (XPending(dpy)) {
		XEvent ev;
		XPeekEvent(dpy, &ev);
		confnote = ev.type == ConfigureNotify;
	}
	pthread_mutex_unlock(&hctx->xlock);
	if (!confnote) {
		XWindowAttributes wa;
		pthread_mutex_lock(&hctx->xlock);
		XGetWindowAttributes(dpy, winmain, &wa);
		pthread_mutex_unlock(&hctx->xlock);

		redraw(wa.width, wa.height);
	}
}
static void handle_touch(struct event_touch *e)
{
	if (gameinfo.status != STATUS_MOVE_WHITE && gameinfo.status != STATUS_MOVE_BLACK)
		return;

	pthread_mutex_lock(&hctx->gamelock);
	int isplaying = game_get_moving_color() == gameinfo.selfcolor;
	pthread_mutex_unlock(&hctx->gamelock);
	if (!isplaying)
		return;

	fid f[2] = { XTOI(e->x, board.xorig, board.fieldsize),
		YTOJ(e->y, board.yorig, board.fieldsize, gameinfo.selfcolor)};

	pthread_mutex_lock(&hctx->gamelock);
	int targetspiece = game_is_movable_piece_at(f[0], f[1]);
	pthread_mutex_unlock(&hctx->gamelock);
	if (targetspiece) {
		if (memcmp(f, selfield, 2 * sizeof(fid)) == 0) {
			if (!(e->flags & TOUCH_RELEASE))
				unselectf();
		} else if (selfield[0] == -1) {
			if (!(e->flags & TOUCH_RELEASE))
				selectf(f);
		} else {
			unselectf();
			if (!(e->flags & TOUCH_RELEASE))
				selectf(f);
		}
	} else if (selfield[0] != -1) {
		pthread_mutex_lock(&hctx->gamelock);
		int err = game_move(selfield[0], selfield[1], f[0], f[1], 0);
		pthread_mutex_unlock(&hctx->gamelock);
		if (err)
			return;

		long tp = gettimepoint();

		union event_t event;
		memset(&event, 0, sizeof(event));
		event.playmove.type = EVENT_PLAYMOVE;
		memcpy(event.playmove.from, selfield, sizeof(selfield));
		memcpy(event.playmove.to, f, sizeof(f));
		event.playmove.tmove = tp;
		int n = hsend(foppt, &event, sizeof(event));
		if (n == -1) {
			SYSERR();
			gfxh_cleanup();
			pthread_exit(NULL);
		} else if (n == 1) {
			return;
		}

		pthread_mutex_lock(&hctx->gamelock);
		fid updates[NUM_UPDATES_MAX][2];
		game_get_updates(updates);
		pthread_mutex_unlock(&hctx->gamelock);

		unselectf();
		show(updates);

		pthread_mutex_lock(&hctx->gamelock);
		gameinfo.status = game_get_status(0);
		pthread_mutex_unlock(&hctx->gamelock);

		if (gameinfo.tiself.movestart != -1) {
			gameinfo.tiself.total = gameinfo.tiself.subtotal
				- (tp - gameinfo.tiself.movestart);
			gameinfo.tiself.subtotal = gameinfo.tiself.total;
		}
		gameinfo.tioppt.movestart = tp;
		show_status(gameinfo);
		tlastupdate = tp;
	}
}
static void handle_playmove(struct event_playmove *e)
{
	pthread_mutex_lock(&hctx->gamelock);
	fid updates[NUM_UPDATES_MAX][2];
	game_move(e->from[0], e->from[1], e->to[0], e->to[1], 0);
	game_get_updates(updates);
	pthread_mutex_unlock(&hctx->gamelock);
	show(updates);

	if (gameinfo.tioppt.movestart != -1) {
		gameinfo.tioppt.total = gameinfo.tioppt.subtotal
			- (e->tmove - gameinfo.tioppt.movestart);
		gameinfo.tioppt.subtotal = gameinfo.tioppt.total;
	}
	gameinfo.tiself.movestart = e->tmove;

	pthread_mutex_lock(&hctx->gamelock);
	gameinfo.status = game_get_status(0);
	pthread_mutex_unlock(&hctx->gamelock);
	show_status(gameinfo);
	tlastupdate = e->tmove;
}
static void handle_updatetime(void)
{
	if ((gameinfo.status != STATUS_MOVE_BLACK && gameinfo.status != STATUS_MOVE_WHITE)
			|| (gameinfo.tiself.movestart == -1 && gameinfo.tioppt.movestart == -1)) {
		return;
	}

	long tp = gettimepoint();

	pthread_mutex_lock(&hctx->gamelock);
	int isplaying = game_get_moving_color() == gameinfo.selfcolor;
	pthread_mutex_unlock(&hctx->gamelock);

	struct timeinfo_t *tiplayer = isplaying ? &gameinfo.tiself : &gameinfo.tioppt;
	long movetime = tp - tiplayer->movestart;
	tiplayer->total = tiplayer->subtotal - movetime;
	if (tiplayer->total < 0) {
		tiplayer->total = 0;
		
		if (selfield[0] != -1)
			unselectf();

		pthread_mutex_lock(&hctx->gamelock);
		gameinfo.status = game_get_status(1);
		pthread_mutex_unlock(&hctx->gamelock);
		show_status(gameinfo);
	} else if (tp - tlastupdate >= SECOND) {
		show_status(gameinfo);
		tlastupdate = tp;
	}
}

static void gfxh_setup(void)
{
	if (fcntl(fevent, F_SETFL, O_NONBLOCK) == -1) {
		SYSERR();
		goto cleanup_err_fcntl;
	}
	if (fcntl(foppt, F_SETFL, O_NONBLOCK) == -1) {
		SYSERR();
		goto cleanup_err_fcntl;
	}

	pfds[0].fd = fevent;
	pfds[0].events = POLLIN;
	pfds[1].fd = foppt;
	pfds[1].events = POLLIN;

	selfield[0] = -1;
	selfield[1] = -1;

	pthread_mutex_lock(&hctx->xlock);
	XWindowAttributes wa;
	XGetWindowAttributes(dpy, winmain, &wa);

	board.surface = cairo_xlib_surface_create(dpy, winmain, vis, wa.width, wa.height);
	draw_init_context(board.surface);
	pthread_mutex_unlock(&hctx->xlock);

	board.xorig = 0;
	board.yorig = 0;
	board.size = wa.width;
	board.fieldsize = wa.height / NF;

	game_init_board();

	gameinfo.tiself.movestart = -1;
	gameinfo.tiself.total = gameinfo.tiself.subtotal;
	gameinfo.tioppt.movestart = -1;
	gameinfo.tioppt.total = gameinfo.tioppt.subtotal;

	gameinfo.status = STATUS_MOVE_WHITE;
	show_status(gameinfo);
	return;

cleanup_err_fcntl:
	pthread_mutex_lock(&hctx->mainlock);
	hctx->terminate = 1;
	pthread_mutex_unlock(&hctx->mainlock);
	pthread_exit(NULL);
}
static void gfxh_run(void)
{
	union event_t event;
	memset(&event, 0, sizeof(event));
	while (1) {
		int n = poll(pfds, ARRNUM(pfds), 0);
		if (n == -1) {
			SYSERR();
			goto cleanup_err_sys;
		}

		if (n == 0) {
			handle_updatetime();
		} else if (pfds[0].revents) {
			n = hread(fevent, &event, sizeof(event));
			if (n == -1) {
				SYSERR();
				goto cleanup_err_sys;
			} else if (n == 1) {
				break;
			} else if (n == 2) {
				continue;
			}

			if (event.type == EVENT_CLIENTMESSAGE) {
				handle_clientmessage(&event.clientmessage);
			} else if (event.type == EVENT_REDRAW) {
				handle_redraw(&event.redraw);
			} else if (event.type == EVENT_TOUCH) {
				handle_touch(&event.touch);
			}
		} else if (pfds[1].revents) {
			n = hrecv(foppt, &event, sizeof(event));
			if (n == -1) {
				SYSERR();
				goto cleanup_err_sys;
			} else if (n == 1) {
				break;
			} else if (n == 2) {
				continue;
			}

			handle_playmove(&event.playmove);
		}
	}
	return;

cleanup_err_sys:
	gfxh_cleanup();
	pthread_exit(NULL);
}
static void gfxh_cleanup(void)
{
	game_terminate();

	pthread_mutex_lock(&hctx->xlock);
	draw_destroy_context();
	cairo_surface_destroy(board.surface);
	pthread_mutex_unlock(&hctx->xlock);

	if (close(foppt) == -1)
		fprintf(stderr, "%s: error while closing communication socket\n", __func__);

	if (close(fevent) == -1 || close(fconfirm) == -1)
		fprintf(stderr, "%s: error while closing event pipes\n", __func__);

	pthread_mutex_lock(&hctx->mainlock);
	hctx->terminate = 1;
	pthread_mutex_unlock(&hctx->mainlock);
}
void *gfxh_main(void *args)
{
	struct gfxh_args_t *a = (struct gfxh_args_t *)args;
	hctx = a->hctx;
	dpy = a->dpy;
	winmain = a->winmain;
	vis = a->vis;
	memcpy(atoms, a->atoms, sizeof(a->atoms));
	foppt = a->fopp;
	gameinfo.selfcolor = a->gamecolor;
	gameinfo.tiself.subtotal = a->gametime;
	gameinfo.tioppt.subtotal = a->gametime;
	free(a);

	fevent = hctx->gfxh.pevent[0];
	fconfirm = hctx->gfxh.pconfirm[1];
	state = &hctx->gfxh.state;

	gfxh_setup();
	gfxh_run();
	gfxh_cleanup();
	return 0;
}
