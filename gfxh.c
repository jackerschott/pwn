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

#include <string.h>

#include <cairo/cairo-xlib.h>
#include <X11/Xlib.h>

#include "draw.h"
#include "game.h"

#include "comh.h"
#include "gfxh.h"

#define XTOI(x, xorig, fieldsize) ((int)(((x) - xorig) / fieldsize))
#define YTOJ(y, yorig, fieldsize, c) \
	((1 - c) * ((NF - 1) - (int)(((y) - yorig) / fieldsize)) \
	+ (c) * (int)(((y) - yorig) / fieldsize))
#define ITOX(i, xorig, fieldsize) (xorig + (i) * fieldsize)
#define JTOY(j, yorig, fieldsize, c) \
	(yorig + ((1 - c) * ((NF - 1) - (j)) + (c) * (j)) * fieldsize)
#define FIGURE(p) ((p) / 2 - 1)
#define PALETTE(c) (c)

static color_t gamecolor;
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

static struct handler_context_t *hctx;
static int fevent;
static int fconfirm;
static int *state;

static void gfxh_cleanup(void);

static void selectf(fid f[2])
{
	pthread_mutex_lock(&hctx->xlock);
	draw_record();
	pthread_mutex_unlock(&hctx->xlock);

	double fx = ITOX(f[0], board.xorig, board.fieldsize);
	double fy = JTOY(f[1], board.yorig, board.fieldsize, gamecolor);
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
static void unselectf(fid f[2])
{
	pthread_mutex_lock(&hctx->xlock);
	draw_record();
	pthread_mutex_unlock(&hctx->xlock);

	double fx = ITOX(f[0], board.xorig, board.fieldsize);
	double fy = JTOY(f[1], board.yorig, board.fieldsize, gamecolor);
	pthread_mutex_lock(&hctx->xlock);
	draw_field(fx, fy, board.fieldsize, (f[0] + f[1]) % 2, 0);
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
		double fy = JTOY(j, board.yorig, board.fieldsize, gamecolor);
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
static void move(fid from[2], fid to[2])
{
	pthread_mutex_lock(&hctx->gamelock);
	fid updates[NUM_UPDATES_MAX][2];
	game_get_updates(updates);
	pthread_mutex_unlock(&hctx->gamelock);
	show(updates);

	pthread_mutex_lock(&hctx->gamelock);
	if (game_is_checkmate()) {
		printf("checkmate!\n");
	}
	if (game_is_stalemate()) {
		printf("stalemate!\n");
	}
	pthread_mutex_unlock(&hctx->gamelock);

	memset(selfield, 0xff, 2 * sizeof(fid));
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
			double fy = JTOY(j, board.yorig, board.fieldsize, gamecolor);
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
	}
	pthread_mutex_lock(&hctx->xlock);
	draw_commit();
	pthread_mutex_unlock(&hctx->xlock);
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
	pthread_mutex_lock(&hctx->gamelock);
	int isplaying = game_get_playing_color() == gamecolor;
	pthread_mutex_unlock(&hctx->gamelock);
	if (!isplaying)
		return;

	fid f[2] = { XTOI(e->x, board.xorig, board.fieldsize),
		YTOJ(e->y, board.yorig, board.fieldsize, gamecolor)};

	pthread_mutex_lock(&hctx->gamelock);
	int targetspiece = game_is_movable_piece_at(f[0], f[1]);
	pthread_mutex_unlock(&hctx->gamelock);
	if (targetspiece) {
		if (memcmp(f, selfield, 2 * sizeof(fid)) == 0) {
			if (!(e->flags & TOUCH_RELEASE))
				unselectf(f);
		} else if (selfield[0] == -1) {
			if (!(e->flags & TOUCH_RELEASE))
				selectf(f);
		} else {
			unselectf(selfield);
			if (!(e->flags & TOUCH_RELEASE))
				selectf(f);
		}
	} else if (selfield[0] != -1) {
		pthread_mutex_lock(&hctx->comhlock);
		int canmove = !(hctx->comh.state & COMH_IS_EXCHANGING);
		pthread_mutex_unlock(&hctx->comhlock);
		if (!canmove) {
			unselectf(selfield);
			return;
		}

		pthread_mutex_lock(&hctx->gamelock);
		int err = game_move(selfield[0], selfield[1], f[0], f[1], 0);
		pthread_mutex_unlock(&hctx->gamelock);
		if (err)
			return;

		union event_t event;
		event.sendmove.type = EVENT_SENDMOVE;
		memcpy(event.sendmove.from, selfield, sizeof(selfield));
		memcpy(event.sendmove.to, f, sizeof(f));
		event.sendmove.tmove = 0;
		if (hwrite(hctx->comh.pevent[1], &event, sizeof(event)) == -1) {
			SYSERR();
			gfxh_cleanup();
			pthread_exit(NULL);
		}

		int ret;
		if (hread(hctx->comh.pconfirm[0], &ret, sizeof(ret)) == -1) {
			SYSERR();
			gfxh_cleanup();
			pthread_exit(NULL);
		}

		move(selfield, f);
	}
}
static void handle_playmove(struct event_passmove *e)
{
	pthread_mutex_lock(&hctx->gamelock);
	game_move(e->from[0], e->from[1], e->to[0], e->to[1], 0);
	pthread_mutex_unlock(&hctx->gamelock);
	move(e->from, e->to);
}

static void gfxh_setup(void)
{
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
}
static void gfxh_run(void)
{
	union event_t event;
	while (1) {
		if (hread(fevent, &event, sizeof(event)) == -1) {
			SYSERR();
			gfxh_cleanup();
			pthread_exit(NULL);
		}

		if (event.type == EVENT_REDRAW) {
			handle_redraw(&event.redraw);
		} else if (event.type == EVENT_TOUCH) {
			handle_touch(&event.touch);
		} else if (event.type == EVENT_PLAYMOVE) {
			handle_playmove(&event.playmove);
		} else if (event.type == EVENT_TERM) {
			break;
		}
	}
}
static void gfxh_cleanup(void)
{
	pthread_mutex_lock(&hctx->xlock);
	draw_destroy_context();
	cairo_surface_destroy(board.surface);
	pthread_mutex_unlock(&hctx->xlock);

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
	gamecolor = a->gamecolor;
	free(a);

	fevent = hctx->gfxh.pevent[0];
	fconfirm = hctx->gfxh.pconfirm[1];
	state = &hctx->gfxh.state;

	gfxh_setup();
	gfxh_run();
	gfxh_cleanup();
	return 0;
}
