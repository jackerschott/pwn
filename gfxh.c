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

#define _XOPEN_SOURCE 700
#define _DEFAULT_SOURCE

#include <assert.h>
#include <fcntl.h>
#include <poll.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#include <arpa/inet.h>
#include <netdb.h>

#include <cairo/cairo-xlib.h>
#include <pthread.h>
#include <X11/Xlib.h>

#include "config.h"
#include "draw.h"
#include "game.h"
#include "notation.h"

#include "gfxh.h"

#define FIGURE(p) ((p) / 2 - 1)
#define PALETTE(c) (c)
#define XTOI(x, xorig, fieldsize, c) \
	((1 - (c)) * (int)(((x) - xorig) / fieldsize) \
	+ (c) * (NF - (int)(((x) - xorig) / fieldsize) - 1))
#define YTOJ(y, yorig, fieldsize, c) \
	((1 - c) * ((NF - 1) - (int)(((y) - yorig) / fieldsize)) \
	+ (c) * (int)(((y) - yorig) / fieldsize))
#define ITOX(i, xorig, fieldsize, c) \
	(xorig + ((1 - (c)) * (i) + (c) * (NF - (i) - 1)) * fieldsize)
#define JTOY(j, yorig, fieldsize, c) \
	(yorig + ((1 - (c)) * (NF - (j) - 1) + (c) * (j)) * fieldsize)

#define MAXLEN_INITTIMESTAMP (STRLEN("00:00:00 01/01/1970"))
#define MAXLEN_INITCOLOR (MAX(STRLEN("white"), STRLEN("black")))
#define MAXLEN_INITSTR (MAXLEN_INITCOLOR + STRLEN(" ") + MAXLEN_TIME_COARSE + STRLEN(" ") + MAXLEN_INITTIMESTAMP)
#define MAXLEN_PLAYMOVE (MAXLEN_MOVE + MAXLEN_TIME + 1)
#define BUFSIZE_PLAYMOVE 128
#define TITLE_MAXLEN (MAXLEN_TIME_COARSE + STRLEN(" - ") + MAXLEN_TIME_COARSE + MSG_MAXLEN)

/* maximal difference between measured game starts by server and client */
#define TRANSDIFF_MAX (2 * SECOND)

int fopp;

struct timeinfo_t {
	long movestart;
	long subtotal;
	long total;
};
struct gameinfo_t {
	color_t selfcolor;
	status_t status;
	long time;
	long tstart;
	struct timeinfo_t tiself;
	struct timeinfo_t tiopp;
};
struct gameinfo_t ginfo;
long nupdates;

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
			assert(errno != EWOULDBLOCK && errno != EAGAIN);
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
			assert(errno != EPIPE && errno != EWOULDBLOCK && errno != EAGAIN);
			return -1;
		}

		b += n;
		size -= n;
	}
	return 0;
}
int hrecv(int fd, char *buf, size_t size)
{
	size_t sz = size;
	char *b = buf;
	while (sz > 0) {
		size_t n = recv(fd, b, sz, 0);
		if (n == -1) {
			if (errno == EINTR)
				continue;
			if (errno == EWOULDBLOCK || errno == EAGAIN)
				return -2;
			return -1;
		} else if (n == 0) {
			return 1;
		}

		if (b[n - 1] == '\n') {
			b[n - 1] = '\0';
			return 0;
		}

		b += n;
		sz -= n;

		//if (sz == 0) {
		//	size_t newsz = sz + size;
		//	void *newbuf = realloc(buf, newsz);
		//	if (!newbuf)
		//		return -1;
		//	buf = newbuf;
		//	b = buf + sz;
		//}
	}
	return -3;
}
int hsend(int fd, char *buf)
{
	size_t size = strlen(buf) + 1;
	buf[size - 1] = '\n';

	char *b = buf;
	while (size > 0) {
		size_t n = send(fd, b, size, 0);
		if (n == -1) {
			if (errno == EINTR)
				continue;
			assert(errno != EPIPE && errno != EWOULDBLOCK && errno != EAGAIN);
			return -1;
		}

		b += n;
		size -= n;
	}
	return 0;
}

static void measure_game_start(struct tm *treal, long *tmono)
{
	/* measure game start in real and monotonic time */
	struct timespec tsr, tsm;
	clock_gettime(CLOCK_REALTIME, &tsr);
	clock_gettime(CLOCK_MONOTONIC, &tsm);

	gmtime_r(&tsr.tv_sec, treal);

	/* correct for delay between tsr and tsm measurement */
	*tmono = tsm.tv_sec * SECOND + tsm.tv_nsec - tsr.tv_nsec;
}
static long get_game_start_mono(struct tm *t)
{
	struct timespec tsreal, tsmono;
	clock_gettime(CLOCK_REALTIME, &tsreal);
	clock_gettime(CLOCK_MONOTONIC, &tsmono);

	long tgamestart = timegm(t) * SECOND;
	long tmono = tsmono.tv_sec * SECOND + tsmono.tv_nsec;
	long treal = tsreal.tv_sec * SECOND + tsreal.tv_nsec;

	long transdiff = treal - tgamestart;
	if (transdiff > TRANSDIFF_MAX)
		return 1;

	return tmono - transdiff;
}
static long measure_move_time(long tstart)
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	long t = ts.tv_sec * SECOND + ts.tv_nsec;

	return t - tstart;
}

static long gettimepoint(void)
{
	struct timespec tp;
	clock_gettime(CLOCK_MONOTONIC, &tp);
	return tp.tv_sec * SECOND + tp.tv_nsec;
}

void format_playmove(struct event_playmove *e, char *str)
{
	size_t l;
	char *c = str;
	format_move(e->piece, e->from, e->to, e->prompiece, str, &l);
	c += l;

	*c = ' ';
	++c;

	format_timeinterval(e->tmove, c, &l, 0);
	c += l;

	*c = '\0';
}
int parse_playmove(char *str, struct event_playmove *e)
{
	e->type = EVENT_PLAYMOVE;

	char *c = strtok(str, " ");
	if (c == NULL)
		return 1;

	if (parse_move(c, &e->piece, e->from, e->to, &e->prompiece))
		return 1;

	c = strtok(NULL, " ");
	if (c == NULL) {
		e->tmove = measure_move_time(ginfo.tiself.movestart);
		return 0;
	}

	const char *end;
	if (parse_time(c, &end, &e->tmove))
		return 1;
	if (*end != '\0')
		return 1;

	return 0;
}
int send_playmove(piece_t piece, fid from[2], fid to[2], piece_t prompiece, long tmove)
{
	union event_t e;
	memset(&e, 0, sizeof(e));
	e.playmove.type = EVENT_PLAYMOVE;
	e.playmove.piece = piece;
	memcpy(e.playmove.from, from, sizeof(e.playmove.from));
	memcpy(e.playmove.to, to, sizeof(e.playmove.to));
	e.playmove.prompiece = prompiece;
	e.playmove.tmove = tmove;

	char buf[MAXLEN_PLAYMOVE + 1];
	format_playmove(&e.playmove, buf);

	int err = hsend(fopp, buf);
	if (err != 0)
		return err;

	return 0;
}
int recv_playmove(struct event_playmove *e)
{
	char buf[BUFSIZE_PLAYMOVE];
	int err = hrecv(fopp, buf, BUFSIZE_PLAYMOVE);
	if (err != 0)
		return err;

	if (parse_playmove(buf, e))
		return 2;

	return 0;
}

static void show_status(struct gameinfo_t ginfo)
{
	char title[TITLE_MAXLEN + 1];
	char *c = title;

	size_t len;
	if (ginfo.time) {
		format_timeinterval(ginfo.tiself.total, c, &len, 1);
		c += len;

		strncpy(c, " - ", STRLEN(" - "));
		c += STRLEN(" - ");

		format_timeinterval(ginfo.tiopp.total, c, &len, 1);
		c += len;

		*c = ' ';
		++c;
	}

	len = strlen(messages[ginfo.status]);
	strncpy(c, messages[ginfo.status], len);
	c += len;
	*c = '\0';

	pthread_mutex_lock(&hctx->xlock);
	XStoreName(dpy, winmain, title);
	pthread_mutex_unlock(&hctx->xlock);
}

static int prompt_promotion_piece(piece_t *p)
{
	char name[PIECENAME_BUF_SIZE];

	int fopts[2];
	int fans[2];
	if (pipe(fopts))
		return -1;

	if (pipe(fans)) {
		SYSERR();
		close(fopts[0]);
		close(fopts[1]);
		return -1;
	}

	pid_t pid = fork();
	if (pid == -1) {
		SYSERR();
		close(fans[0]);
		close(fans[1]);
		close(fopts[0]);
		close(fopts[1]);
		return -1;
	}
	
	if (pid == 0) {
		close(fopts[1]);
		close(fans[0]);

		if (dup2(fopts[0], STDIN_FILENO) == -1) {
			SYSERR();
			close(fopts[0]);
			close(fans[1]);
			exit(-1);
		}
		if (dup2(fans[1], STDOUT_FILENO) == -1) {
			SYSERR();
			close(fopts[0]);
			close(fans[1]);
			exit(-1);
		}

		execvp(promotion_prompt_cmd[0], (char *const *)promotion_prompt_cmd);
		SYSERR();
		exit(-1);
	} else {
		close(fopts[0]);
		close(fans[1]);

		if (write(fopts[1], piece_names[1], strlen(piece_names[1])) == -1) {
			SYSERR();
			close(fopts[1]);
			close(fans[0]);
			return -1;
		}
		for (int i = 2; i < ARRNUM(piece_names) - 1; ++i) {
			if (write(fopts[1], "\n", 1) == -1) {
				SYSERR();
				close(fopts[1]);
				close(fans[0]);
				return -1;
			}
			if (write(fopts[1], piece_names[i], strlen(piece_names[i])) == -1) {
				SYSERR();
				close(fopts[1]);
				close(fans[0]);
				return -1;
			}
		}
		close(fopts[1]);

		int n;
		if ((n = read(fans[0], name, PIECENAME_BUF_SIZE - 1)) == -1) {
			SYSERR();
			close(fans[0]);
			return -1;
		}
		close(fans[0]);
	}

	int i = 1;
	for (; i < ARRNUM(piece_names) - 1; ++i) {
		if (strncmp(name, piece_names[i], strlen(piece_names[i])) == 0)
			break;
	}
	if (i == ARRNUM(piece_names) - 1)
		return 1;

	*p = PIECE_BY_IDX(i);
	return 0;
}
static int move(fid from[2], fid to[2], piece_t *piece, piece_t *prompiece)
{
	*prompiece = PIECE_NONE;

	pthread_mutex_lock(&hctx->gamelock);
	piece_t p = game_get_piece(from[0], from[1]);
	pthread_mutex_unlock(&hctx->gamelock);
	*piece = p;

	pthread_mutex_lock(&hctx->gamelock);
	int ret = game_move(from[0], from[1], to[0], to[1], 0);
	pthread_mutex_unlock(&hctx->gamelock);
	if (ret == -1 || ret == 1) {
		return ret;
	} else if (ret == 2) {
		ret = prompt_promotion_piece(prompiece);
		if (ret == -1)
			return -1;
		if (ret == 1)
			return 2;

		pthread_mutex_lock(&hctx->gamelock);
		ret = game_move(from[0], from[1], to[0], to[1], *prompiece);
		pthread_mutex_unlock(&hctx->gamelock);
		if (ret == -1)
			return -1;
	}

	return 0;
}
static void selectf(fid f[2])
{
	pthread_mutex_lock(&hctx->xlock);
	draw_record();
	pthread_mutex_unlock(&hctx->xlock);

	double fx = ITOX(f[0], board.xorig, board.fieldsize, ginfo.selfcolor);
	double fy = JTOY(f[1], board.yorig, board.fieldsize, ginfo.selfcolor);
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

	double fx = ITOX(selfield[0], board.xorig, board.fieldsize, ginfo.selfcolor);
	double fy = JTOY(selfield[1], board.yorig, board.fieldsize, ginfo.selfcolor);
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

		double fx = ITOX(i, board.xorig, board.fieldsize, ginfo.selfcolor);
		double fy = JTOY(j, board.yorig, board.fieldsize, ginfo.selfcolor);
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
			double fx = ITOX(i, board.xorig, board.fieldsize, ginfo.selfcolor);
			double fy = JTOY(j, board.yorig, board.fieldsize, ginfo.selfcolor);
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
	if (ginfo.status != STATUS_MOVE_WHITE && ginfo.status != STATUS_MOVE_BLACK)
		return;

	pthread_mutex_lock(&hctx->gamelock);
	int isplaying = game_get_moving_color() == ginfo.selfcolor;
	pthread_mutex_unlock(&hctx->gamelock);
	if (!isplaying)
		return;

	fid f[2] = {
		XTOI(e->x, board.xorig, board.fieldsize, ginfo.selfcolor),
		YTOJ(e->y, board.yorig, board.fieldsize, ginfo.selfcolor)
	};
	if (e->flags & TOUCH_RELEASE && (selfield[0] == -1 || memcmp(f, selfield, sizeof(f)) == 0))
		return;

	pthread_mutex_lock(&hctx->gamelock);
	int targetspiece = game_is_movable_piece_at(f[0], f[1]);
	pthread_mutex_unlock(&hctx->gamelock);
	if (targetspiece) {
		if (memcmp(f, selfield, 2 * sizeof(fid)) == 0) { /* unselect piece */
			unselectf();
		} else if (selfield[0] == -1) { /* select piece */
			selectf(f);
		} else { /* switch selection over to piece */
			unselectf();
			if (!(e->flags & TOUCH_RELEASE))
				selectf(f);
		}
	} else if (selfield[0] != -1) {
		piece_t piece, prompiece;
		int err = move(selfield, f, &piece, &prompiece);
		if (err == -1) {
			SYSERR();
			gfxh_cleanup();
			pthread_exit(NULL);
		} else if (err == 1 || err == 2) {
			unselectf();
			return;
		}

		long tmove = measure_move_time(ginfo.tiself.movestart);


		err = send_playmove(piece, selfield, f, prompiece, tmove);
		if (err == -1) {
			SYSERR();
			gfxh_cleanup();
			pthread_exit(NULL);
		}

		/* show move */
		pthread_mutex_lock(&hctx->gamelock);
		fid updates[NUM_UPDATES_MAX][2];
		game_get_updates(updates);
		pthread_mutex_unlock(&hctx->gamelock);
		unselectf();
		show(updates);

		/* let time run only after the first move by white */
		long deduction = tmove;
		pthread_mutex_lock(&hctx->gamelock);
		int nmove = game_get_move_number();
		pthread_mutex_unlock(&hctx->gamelock);
		if (nmove == 0 && ginfo.status == STATUS_MOVE_WHITE) {
			deduction = 0;
		}

		/* update status */
		pthread_mutex_lock(&hctx->gamelock);
		ginfo.status = game_get_status(0);
		pthread_mutex_unlock(&hctx->gamelock);
		ginfo.tiself.total = ginfo.tiself.subtotal - deduction;
		ginfo.tiself.subtotal = ginfo.tiself.total;
		ginfo.tiopp.movestart = ginfo.tiself.movestart + tmove;
		show_status(ginfo);

		nupdates = 0;
	}
}
static void handle_playmove(struct event_playmove *e)
{
	pthread_mutex_lock(&hctx->gamelock);
	int err = game_move(e->from[0], e->from[1], e->to[0], e->to[1], e->prompiece);
	pthread_mutex_unlock(&hctx->gamelock);
	if (err == -1) {
		SYSERR();
		gfxh_cleanup();
		pthread_exit(NULL);
	}

	pthread_mutex_lock(&hctx->gamelock);
	fid updates[NUM_UPDATES_MAX][2];
	game_get_updates(updates);
	pthread_mutex_unlock(&hctx->gamelock);
	show(updates);

	/* let time run only after the first move by white */
	long deduction = e->tmove;
	pthread_mutex_lock(&hctx->gamelock);
	int nmove = game_get_move_number();
	pthread_mutex_unlock(&hctx->gamelock);
	if (nmove == 0 && ginfo.status == STATUS_MOVE_WHITE) {
		deduction = 0;
	}

	ginfo.tiopp.total = ginfo.tiopp.subtotal - deduction;
	ginfo.tiopp.subtotal = ginfo.tiopp.total;
	ginfo.tiself.movestart = ginfo.tiopp.movestart + e->tmove;

	pthread_mutex_lock(&hctx->gamelock);
	ginfo.status = game_get_status(0);
	pthread_mutex_unlock(&hctx->gamelock);
	show_status(ginfo);

	nupdates = 0;
}
static void handle_updatetime(void)
{
	if (ginfo.status != STATUS_MOVE_BLACK
			&& ginfo.status != STATUS_MOVE_WHITE)
		return;
	if (ginfo.tiself.movestart == ginfo.tstart
			&& ginfo.tiopp.movestart == ginfo.tstart)
		return;

	pthread_mutex_lock(&hctx->gamelock);
	int isplaying = game_get_moving_color() == ginfo.selfcolor;
	pthread_mutex_unlock(&hctx->gamelock);
	struct timeinfo_t *tiplayer = isplaying ? &ginfo.tiself : &ginfo.tiopp;

	long movetime = measure_move_time(tiplayer->movestart);
	tiplayer->total = tiplayer->subtotal - movetime;
	if (tiplayer->total < 0) {
		tiplayer->total = 0;
		
		if (selfield[0] != -1)
			unselectf();

		pthread_mutex_lock(&hctx->gamelock);
		ginfo.status = game_get_status(1);
		pthread_mutex_unlock(&hctx->gamelock);
		show_status(ginfo);
	} else if (movetime >= nupdates * SECOND) {
		show_status(ginfo);
		++nupdates;
	}
}

static void gfxh_setup(void)
{
	if (fcntl(fevent, F_SETFL, O_NONBLOCK) == -1) {
		SYSERR();
		goto cleanup_err_fcntl;
	}
	if (fcntl(fopp, F_SETFL, O_NONBLOCK) == -1) {
		SYSERR();
		goto cleanup_err_fcntl;
	}

	pfds[0].fd = fevent;
	pfds[0].events = POLLIN;
	pfds[1].fd = fopp;
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

	ginfo.tiself.subtotal = ginfo.time;
	ginfo.tiself.movestart = ginfo.tstart;
	ginfo.tiself.total = ginfo.tiself.subtotal;
	ginfo.tiopp.subtotal = ginfo.time;
	ginfo.tiopp.movestart = ginfo.tstart;
	ginfo.tiopp.total = ginfo.tiopp.subtotal;

	ginfo.status = STATUS_MOVE_WHITE;
	show_status(ginfo);
	nupdates = -1;
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
			goto cleanup_err;
		}

		if (ginfo.time && n == 0) {
			handle_updatetime();
		} else if (pfds[0].revents) {
			n = hread(fevent, &event, sizeof(event));
			if (n == -1) {
				SYSERR();
				goto cleanup_err;
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
			int err = recv_playmove(&event.playmove);
			if (err == -1) {
				SYSERR();
				goto cleanup_err;
			} else if (err == -2 || err == 2) {
				fprintf(stderr, "%s: received move message is invalid\n", __func__);
				goto cleanup_err;
			} else if (err == -3) {
				fprintf(stderr, "%s: received move message is too large\n",
						__func__);
				goto cleanup_err;
			} else if (err == 1) {
				break;
			}

			handle_playmove(&event.playmove);
		}
	}
	return;

cleanup_err:
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

	if (close(fopp) == -1)
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
	free(a);

	fevent = hctx->gfxh.pevent[0];
	fconfirm = hctx->gfxh.pconfirm[1];
	state = &hctx->gfxh.state;

	gfxh_setup();
	gfxh_run();
	gfxh_cleanup();
	return 0;
}

int parse_init_string(const char *str, color_t *color, long *gametime, struct tm *tp)
{
	const char *c = str;
	if (strncmp(str, "white", STRLEN("white")) == 0) {
		*color = COLOR_WHITE;
		c += STRLEN("white");
	} else if (strncmp(str, "black", STRLEN("black")) == 0) {
		*color = COLOR_BLACK;
		c += STRLEN("black");
	} else {
		return 1;
	}
	if (*c != ' ')
		return 1;
	++c;

	const char *e;
	parse_time(c, &e, gametime);
	if (*e != ' ')
		return 1;
	c = e + 1;

	e = strptime(c, "%H:%M:%S %d/%m/%Y", tp);
	if (!e)
		return 1;
	if (*e != '\0')
		return 1;

	return 0;
}
void format_init_string(color_t color, long gametime, struct tm *tp, char *str)
{
	char *c = str;
	if (color == COLOR_WHITE) {
		strncpy(c, "black", STRLEN("black"));
		c += STRLEN("black");
	} else {
		strncpy(c, "white", STRLEN("white"));
		c += STRLEN("white");

	}
	*c = ' ';
	++c;

	size_t len;
	format_timeinterval(gametime, c, &len, 1);
	c += len;
	*c = ' ';
	++c;

	size_t n = strftime(c, MAXLEN_INITTIMESTAMP + 1, "%H:%M:%S %d/%m/%Y", tp);
	assert(n != 0);
	c += strlen(c);

	*c = '\0';
}
void init_communication_server(const char* node, const char *port, color_t color, long gametime)
{
	int fsock = socket(AF_INET, SOCK_STREAM, 0);
	if (fsock == -1) {
		SYSERR();
		exit(-1);
	}

	struct addrinfo hints = {0};
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_flags = AI_PASSIVE;

	struct addrinfo *res;
	int err = getaddrinfo(node, port, &hints, &res);
	if (err) {
		fprintf(stderr, "could not resolve address and port information\n");
		fprintf(stderr, "%s", gai_strerror(err));
		close(fsock);
		exit(-1);
	}

	int val = 1;
	setsockopt(fsock, SOL_SOCKET, SO_REUSEADDR, &val, sizeof(val));
	if (bind(fsock, res->ai_addr, res->ai_addrlen) == -1) {
		fprintf(stderr, "could not bind to specified address\n");
		SYSERR();
		close(fsock);
		exit(-1);
	}
	freeaddrinfo(res);

	struct sockaddr addr;
	if (listen(fsock, 1) == -1) {
		close(fsock);
		exit(-1);
	}

	fopp = accept(fsock, NULL, NULL);
	if (fopp == -1) {
		close(fsock);
		exit(-1);
	}
	close(fsock);

	/* internally -> monotonic time, send to other client -> time both agree on: real time */
	struct tm tstartreal;
	measure_game_start(&tstartreal, &ginfo.tstart);

	char buf[MAXLEN_INITSTR];
	format_init_string(color, gametime, &tstartreal, buf);
	int n = hsend(fopp, buf);
	if (n == -1) {
		SYSERR();
		close(fopp);
		exit(-1);
	}

	ginfo.selfcolor = color;
	ginfo.time = gametime;
}
void init_communication_client(const char *node, const char *port)
{
	fopp = socket(AF_INET, SOCK_STREAM, 0);
	if (fopp == -1) {
		SYSERR();
		exit(-1);
	}

	struct addrinfo hints = {0};
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;

	struct addrinfo *res;
	int err = getaddrinfo(node, port, &hints, &res);
	if (err) {
		fprintf(stderr, "%s: could not resolve address and port information\n", __func__);
		fprintf(stderr, "%s", gai_strerror(err));
		close(fopp);
		exit(-1);
	}

	if (connect(fopp, res->ai_addr, res->ai_addrlen) == -1) {
		fprintf(stderr, "%s: could not connect to specified address\n", __func__);
		SYSERR();
		close(fopp);
		exit(-1);
	}

	size_t bufsize = MAXLEN_INITSTR + 1;
	char buf[bufsize];
	err = hrecv(fopp, buf, bufsize);
	if (err == -1) {
		SYSERR();
		close(fopp);
		exit(-1);
	} else if (err == -2) {
		fprintf(stderr, "%s: received initialization message is invalid\n", __func__);
		close(fopp);
		exit(-1);
	} else if (err == -3) {
		fprintf(stderr, "%s: received initialization message is too large\n", __func__);
		close(fopp);
		exit(-1);
	} else if (err == 1) {
		fprintf(stderr, "%s: unexpected server shutdown\n", __func__);
		close(fopp);
		exit(-1);
	}

	struct tm tstartreal;
	err = parse_init_string(buf, &ginfo.selfcolor, &ginfo.time, &tstartreal);
	if (err) {
		fprintf(stderr, "%s: received initialization message is invalid\n", __func__);
		close(fopp);
		exit(-1);
	}
	long tstart = get_game_start_mono(&tstartreal);
	if (tstart == -1) {
		fprintf(stderr, "%s: server and client don't agree on start time\n", __func__);
		close(fopp);
		exit(-1);
	}
	ginfo.tstart = tstart;
}
