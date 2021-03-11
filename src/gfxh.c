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
#include <time.h>
#include <unistd.h>

#include <arpa/inet.h>
#include <netdb.h>

#include <cairo/cairo-xlib.h>
#include <pthread.h>
#include <X11/Xlib.h>

#include "audioh.h"
#include "config.h"
#include "draw.h"
#include "game.h"
#include "notation.h"
#include "util.h"

#include "gfxh.h"

#define GFXH_EVENT_RESPONSE_TIME 10000

int fopp;

static struct handler_context_t *hctx;
static int fevent;
static int fconfirm;
static int *state;
static struct pollfd pfds[2];

/* graphics handling */
#define XTOI(x, xorig, squaresize, c) \
	((1 - (c)) * (int)(((x) - xorig) / squaresize) \
	+ (c) * (NF - (int)(((x) - xorig) / squaresize) - 1))
#define YTOJ(y, yorig, squaresize, c) \
	((1 - c) * ((NF - 1) - (int)(((y) - yorig) / squaresize)) \
	+ (c) * (int)(((y) - yorig) / squaresize))
#define ITOX(i, xorig, squaresize, c) \
	(xorig + ((1 - (c)) * (i) + (c) * (NF - (i) - 1)) * squaresize)
#define JTOY(j, yorig, squaresize, c) \
	(yorig + ((1 - (c)) * (NF - (j) - 1) + (c) * (j)) * squaresize)

#define MOVEUPDATES_SIZE (UPDATES_NUM_MAX + 2)

static sqid selsquare[2];
static struct {
	cairo_surface_t *surface;
	double xorig;
	double yorig;
	double size;
	double squaresize;
} board;
static sqid moveupdates[MOVEUPDATES_SIZE][2];

static Display *dpy;
static Window winmain;
static Visual *vis;
static Atom atoms[ATOM_COUNT];

/* network communication */
#define INITMSG_PREFIX "init"
#define MOVEMSG_PREFIX "move"
#define STATMSG_PREFIX "status"
#define DRAWMSG "drawoffer"
#define TAKEBACKMSG "takeback"

#define INITCOLOR_MAXLEN (MAX(STRLEN("white"), STRLEN("black")))
#define INITMSG_MAXLEN (STRLEN("init ") 		\
		+ INITCOLOR_MAXLEN + STRLEN(" ") 	\
		+ TINTERVAL_COARSE_MAXLEN + STRLEN(" ") \
		+ TSTAMP_MAXLEN)
#define MOVEMSG_MAXLEN (STRLEN("move ") 		\
		+ MOVE_MAXLEN + STRLEN(" ") 		\
		+ TINTERVAL_MAXLEN + STRLEN(" ") 	\
		+ TSTAMP_MAXLEN)
#define STATMSG_MAXLEN (STRLEN("status ") 		\
		+ STATMSG_NAME_MAXLEN)
#define MSG_MAXLEN MAX(MAX(INITMSG_MAXLEN, MOVEMSG_MAXLEN), STATMSG_MAXLEN)

#define TITLE_MAXLEN (TINTERVAL_COARSE_MAXLEN + STRLEN(" - ") 	\
		+ TINTERVAL_COARSE_MAXLEN 			\
		+ STATMSG_TEXTS_MAXLEN)

#define STATMSG_NAME_MAXLEN 14
static const char *statmsg_names[] = {
	"movingwhite",
	"movingblack",
	"checkmatewhite",
	"checkmateblack",
	"drawmaterial",
	"drawstalemate",
	"drawrepitition",
	"drawfiftymove",
	"timeoutwhite",
	"timeoutblack",
	"drawmaterialvstimeout",
	"surrenderwhite",
	"surrenderblack",
};
#define STATMSG_TEXTS_MAXLEN 40
static const char *statmsg_texts[] = {
	"It is White to move",
	"It is Black to move",
	"Black won by checkmate",
	"White won by checkmate",
	"Draw by insufficient material",
	"Draw by stalemate",
	"Draw by threefold repetition",
	"Draw by fifty move rule",
	"Black won by timeout",
	"White won by timeout",
	"Draw by timeout vs insufficient material",
	"Black won by surrender",
	"White won by surrender",
};

struct msg_init {
	int type;
	color_t color;
	long gametime;
	long tstamp;
};
struct msg_playmove {
	int type;
	piece_t piece;
	sqid from[2];
	sqid to[2];
	piece_t prompiece;
	long tmove;
	long tstamp;
};
struct msg_statuschange {
	int type;
	status_t status;
};
union msg_t {
	int type;
	struct msg_init init;
	struct msg_playmove playmove;
	struct msg_statuschange statuschange;
};

/* time and game status management */
#define TIME_STATUS_UPDATE_INTERVAL SECOND
#define TIMEOUTDIFF_MAX (SECOND / 20) /* maximal difference between gametime
					 for one color measured by both parties */
#define TRANSDIFF_MAX SECOND /* maximal difference between measured
				game starts by server and client */

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
long time_status_updates_num;

static void gfxh_cleanup(void);

static void measure_game_start(long *treal, long *tmono)
{
	struct timespec tsr, tsm;
	clock_gettime(CLOCK_REALTIME, &tsr);
	clock_gettime(CLOCK_MONOTONIC, &tsm);

	*treal = tsr.tv_sec * SECOND + tsr.tv_nsec;
	*tmono = tsm.tv_sec * SECOND + tsm.tv_nsec;
}
static int get_game_start_mono(long treal, long *tmono)
{
	struct timespec tsr, tsm;
	clock_gettime(CLOCK_REALTIME, &tsr);
	clock_gettime(CLOCK_MONOTONIC, &tsm);

	long tm = tsm.tv_sec * SECOND + tsm.tv_nsec;
	long tr = tsr.tv_sec * SECOND + tsr.tv_nsec;

	long transdiff = tr - treal;
	if (transdiff > TRANSDIFF_MAX)
		return 1;

	*tmono = tm - transdiff;
	return 0;
}
static long measure_move_time(long tmovestart)
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	long t = ts.tv_sec * SECOND + ts.tv_nsec;

	return t - tmovestart;
}
static long measure_timestamp()
{
	struct timespec ts;
	clock_gettime(CLOCK_REALTIME, &ts);
	return ts.tv_sec * SECOND + ts.tv_nsec;
}

static void format_initmsg(struct msg_init *e, char *str)
{
	char *c = str;
	strncpy(c, INITMSG_PREFIX " ", STRLEN(INITMSG_PREFIX " "));
	c += STRLEN(INITMSG_PREFIX " ");
	
	if (e->color == COLOR_WHITE) {
		strncpy(c, "black", STRLEN("black"));
		c += STRLEN("black");
	} else {
		strncpy(c, "white", STRLEN("white"));
		c += STRLEN("white");

	}
	*c = ' ';
	++c;

	size_t len;
	len = format_timeinterval(e->gametime, c, 1);
	c += len;
	*c = ' ';
	++c;

	len = format_timestamp(e->tstamp, c, 0);
	c += len;

	*c = '\0';
}
static void format_movemsg(struct msg_playmove *e, char *str)
{
	size_t l;
	char *c = str;
	strncpy(c, MOVEMSG_PREFIX " ", STRLEN(MOVEMSG_PREFIX " "));
	c += STRLEN(MOVEMSG_PREFIX " ");

	l = format_move(e->piece, e->from, e->to, e->prompiece, c);
	c += l;

	*c = ' ';
	++c;

	l = format_timeinterval(e->tmove, c, 0);
	c += l;

	*c = ' ';
	++c;

	l = format_timestamp(e->tstamp, c, 0);
	c += l;

	*c = '\0';
}
static void format_statusmsg(struct msg_statuschange *e, char *str)
{
	char *c = str;
	strncpy(c, STATMSG_PREFIX " ", STRLEN(STATMSG_PREFIX " "));
	c += STRLEN(STATMSG_PREFIX " ");

	strcpy(c, statmsg_names[(int)e->status]);
}
static int parse_initmsg(const char *str, struct msg_init *e)
{
	e->type = GFXH_EVENT_INIT;

	const char *c = str;
	assert(strncmp(str, INITMSG_PREFIX " ",  STRLEN(INITMSG_PREFIX " ")) == 0);
	c += STRLEN(INITMSG_PREFIX " ");

	if (strncmp(c, "white", STRLEN("white")) == 0) {
		e->color = COLOR_WHITE;
		c += STRLEN("white");
	} else if (strncmp(c, "black", STRLEN("black")) == 0) {
		e->color = COLOR_BLACK;
		c += STRLEN("black");
	} else {
		return 1;
	}
	if (*c != ' ')
		return 1;
	++c;

	if (!(c = parse_timeinterval(c, &e->gametime, 0)))
		return 1;
	if (*c != ' ')
		return 1;
	++c;

	if (!(c = parse_timestamp(c, &e->tstamp)))
		return 1;
	if (*c != '\0')
		return 1;

	return 0;
}
static int parse_movemsg(const char *str, struct msg_playmove *e)
{
	e->type = GFXH_EVENT_PLAYMOVE;

	const char *c = str;
	assert(strncmp(c, MOVEMSG_PREFIX " ", STRLEN(MOVEMSG_PREFIX " ")) == 0);
	c += STRLEN(MOVEMSG_PREFIX " ");

	if (!(c = parse_move(c, &e->piece, e->from, e->to, &e->prompiece)))
		return 1;
	if (*c != ' ') {
		e->tmove = measure_move_time(ginfo.tiself.movestart);
		return 0;
	}

	if (!(c = parse_timeinterval(c, &e->tmove, 0)))
		return 1;
	if (*c == '\0')
		return 0;
	if (*c != ' ')
		return 1;

	if (!(c = parse_timestamp(c, &e->tstamp)))
		return 1;
	if (*c != '\0')
		return 1;

	return 0;
}
static int parse_statusmsg(const char *str, struct msg_statuschange *e)
{
	e->type = GFXH_EVENT_STATUSCHANGE;

	const char *c = str;
	assert(strncmp(c, STATMSG_PREFIX " ", STRLEN(STATMSG_PREFIX " ")) == 0);
	c += STRLEN(STATMSG_PREFIX " ");

	int s = -1;
	size_t l;
	for (int i = 0; i < ARRNUM(statmsg_names); ++i) {
		l = strlen(statmsg_names[i]);
		if (strncmp(c, statmsg_names[i], l) == 0) {
			s = i;
			c += l;
			break;
		}
	}
	if (s == -1 || *c != '\0')
		return 1;
	e->status = (status_t)s;

	return 0;
}
static void format_msg(union msg_t *e, char *str)
{
	switch (e->type) {
	case GFXH_EVENT_INIT:
		format_initmsg(&e->init, str);
		break;
	case GFXH_EVENT_PLAYMOVE:
		format_movemsg(&e->playmove, str);
		break;
	case GFXH_EVENT_STATUSCHANGE:
		format_statusmsg(&e->statuschange, str);
		break;
	default:
		assert(0);
	}
}
static int parse_msg(const char *str, union msg_t *e)
{
	if (strncmp(str, INITMSG_PREFIX, STRLEN(INITMSG_PREFIX)) == 0) {
		return parse_initmsg(str, &e->init);
	} else if (strncmp(str, MOVEMSG_PREFIX, STRLEN(MOVEMSG_PREFIX)) == 0) {
		return parse_movemsg(str, &e->playmove);
	} else if (strncmp(str, STATMSG_PREFIX, STRLEN(STATMSG_PREFIX)) == 0) {
		return parse_statusmsg(str, &e->statuschange);
	}
	assert(0);
}
static int send_msg(union msg_t *e)
{
	char buf[MSG_MAXLEN + 1];
	format_msg(e, buf);

	int err = hsend(fopp, buf);
	if (err != 0)
		return err;

	return 0;
}
static int recv_msg(union msg_t *e)
{
	char buf[MSG_MAXLEN + 1];
	int err = hrecv(fopp, buf, sizeof(buf));
	if (err != 0)
		return err;

	if (parse_msg(buf, e))
		return 2;

	return 0;
}

static int send_status(status_t status)
{
	union msg_t m;
	memset(&m, 0, sizeof(m));
	m.statuschange.type = GFXH_EVENT_STATUSCHANGE;
	m.statuschange.status = status;

	char buf[STATMSG_MAXLEN + 1];
	format_statusmsg(&m.statuschange, buf);

	int err = hsend(fopp, buf);
	if (err != 0)
		return err;

	return 0;
}
static int recv_status(struct msg_statuschange *e)
{
	const size_t bufsize = STATMSG_MAXLEN + 1;
	char buf[bufsize];
	int err = hrecv(fopp, buf, bufsize);
	if (err != 0)
		return err;

	if (parse_statusmsg(buf, e))
		return 2;

	return 0;
}

static void show_status(struct gameinfo_t ginfo)
{
	char title[TITLE_MAXLEN + 1];
	char *c = title;

	size_t len;
	if (ginfo.time) {
		len = format_timeinterval(ginfo.tiself.total, c, 1);
		c += len;

		strncpy(c, " - ", STRLEN(" - "));
		c += STRLEN(" - ");

		len = format_timeinterval(ginfo.tiopp.total, c, 1);
		c += len;

		*c = ' ';
		++c;
	}

	strcpy(c, statmsg_texts[ginfo.status]);

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
static int apply_move(sqid from[2], sqid to[2], piece_t *piece, piece_t *prompiece,
		long tmove, int oppmove)
{
	/* let time run only after the first move by white */
	long deduction = tmove;
	pthread_mutex_lock(&hctx->gamelock);
	int nmove = game_get_move_number();
	pthread_mutex_unlock(&hctx->gamelock);
	if (nmove == 0 && ginfo.status == STATUS_MOVING_WHITE) {
		deduction = 0;
	}

	/* get piece */
	if (piece) {
		pthread_mutex_lock(&hctx->gamelock);
		piece_t p = game_get_piece(from[0], from[1]);
		pthread_mutex_unlock(&hctx->gamelock);
		*piece = p;
	}

	/* apply move */
	pthread_mutex_lock(&hctx->gamelock);
	int ret = game_exec_ply(from[0], from[1], to[0], to[1], *prompiece);
	pthread_mutex_unlock(&hctx->gamelock);
	if (ret == 1) {
		return 1;
	} else if (*prompiece == PIECE_NONE && ret == 2) {
		if (*prompiece != PIECE_NONE)
			return 1;

		ret = prompt_promotion_piece(prompiece);
		if (ret == -1) {
			SYSERR();
			gfxh_cleanup();
			pthread_exit(NULL);
		} else if (ret == 1) {
			return 1;
		}

		pthread_mutex_lock(&hctx->gamelock);
		ret = game_exec_ply(from[0], from[1], to[0], to[1], *prompiece);
		pthread_mutex_unlock(&hctx->gamelock);
		assert(ret == 0);
	}

	/* get updates */
	pthread_mutex_lock(&hctx->gamelock);
	size_t nlastply = game_get_ply_number() - 1;
	size_t nupdates = game_get_updates(nlastply, moveupdates, 1);
	if (nlastply > 0)
		game_get_updates(nlastply - 1, moveupdates + nupdates, 0);
	pthread_mutex_unlock(&hctx->gamelock);

	/* update status */
	status_t status = ginfo.status;
	pthread_mutex_lock(&hctx->gamelock);
	game_get_status(&status);
	pthread_mutex_unlock(&hctx->gamelock);

	/* play sound */
	pthread_mutex_lock(&hctx->gamelock);
	int capture = game_last_ply_was_capture();
	pthread_mutex_unlock(&hctx->gamelock);
	char *fname = capture ? SOUND_CAPTURE_FNAME : SOUND_MOVE_FNAME;

	union audioh_event_t esound;
	memset(&esound, 0, sizeof(esound));
	esound.playsound.type = AUDIOH_EVENT_PLAYSOUND;
	esound.playsound.fname = fname;
	int n = hwrite(hctx->audioh.pevent[1], &esound, sizeof(esound));
	if (n == -1) {
		SYSERR();
		gfxh_cleanup();
		pthread_exit(NULL);
	}

	/* show status */
	if (oppmove) {
		ginfo.tiopp.total = ginfo.tiopp.subtotal - deduction;
		ginfo.tiopp.subtotal = ginfo.tiopp.total;
		ginfo.tiself.movestart = ginfo.tiopp.movestart + tmove;
	} else {
		ginfo.tiself.total = ginfo.tiself.subtotal - deduction;
		ginfo.tiself.subtotal = ginfo.tiself.total;
		ginfo.tiopp.movestart = ginfo.tiself.movestart + tmove;
	}
	ginfo.status = status;
	show_status(ginfo);

	time_status_updates_num = 0;
	return 0;
}

static void selectf(sqid f[2])
{
	pthread_mutex_lock(&hctx->xlock);
	draw_record();
	pthread_mutex_unlock(&hctx->xlock);

	double fx = ITOX(f[0], board.xorig, board.squaresize, ginfo.selfcolor);
	double fy = JTOY(f[1], board.yorig, board.squaresize, ginfo.selfcolor);

	shade_t shade = (shade_t)(1 - (f[0] + f[1]) % 2);
	pthread_mutex_lock(&hctx->xlock);
	draw_square(fx, fy, board.squaresize, shade, SQUARE_HIGHLIGHT_SELECTED);
	pthread_mutex_unlock(&hctx->xlock);

	pthread_mutex_lock(&hctx->gamelock);
	piece_t piece = game_get_piece(f[0], f[1]);
	pthread_mutex_unlock(&hctx->gamelock);
	if (piece) {
		pthread_mutex_lock(&hctx->gamelock);
		color_t color = game_get_color(f[0], f[1]);
		pthread_mutex_unlock(&hctx->gamelock);

		pthread_mutex_lock(&hctx->xlock);
		draw_piece(fx, fy, board.squaresize, piece, (shade_t)color);
		pthread_mutex_unlock(&hctx->xlock);
	}

	pthread_mutex_lock(&hctx->xlock);
	draw_commit();
	pthread_mutex_unlock(&hctx->xlock);

	memcpy(selsquare, f, 2 * sizeof(sqid));
}
static void unselectf(void)
{
	pthread_mutex_lock(&hctx->xlock);
	draw_record();
	pthread_mutex_unlock(&hctx->xlock);

	double fx = ITOX(selsquare[0], board.xorig, board.squaresize, ginfo.selfcolor);
	double fy = JTOY(selsquare[1], board.yorig, board.squaresize, ginfo.selfcolor);
	
	shade_t shade = (shade_t)(1 - (selsquare[0] + selsquare[1]) % 2);
	pthread_mutex_lock(&hctx->xlock);
	draw_square(fx, fy, board.squaresize, shade, SQUARE_HIGHLIGHT_UNSELECTED);
	pthread_mutex_unlock(&hctx->xlock);

	pthread_mutex_lock(&hctx->gamelock);
	piece_t piece = game_get_piece(selsquare[0], selsquare[1]);
	pthread_mutex_unlock(&hctx->gamelock);
	if (piece) {
		pthread_mutex_lock(&hctx->gamelock);
		color_t color = game_get_color(selsquare[0], selsquare[1]);
		pthread_mutex_unlock(&hctx->gamelock);

		pthread_mutex_lock(&hctx->xlock);
		draw_piece(fx, fy, board.squaresize, piece, (shade_t)color);
		pthread_mutex_unlock(&hctx->xlock);
	}

	pthread_mutex_lock(&hctx->xlock);
	draw_commit();
	pthread_mutex_unlock(&hctx->xlock);

	memset(selsquare, 0xff, 2 * sizeof(sqid));
}
static void show()
{
	pthread_mutex_lock(&hctx->xlock);
	draw_record();
	pthread_mutex_unlock(&hctx->xlock);
	for (int k = MOVEUPDATES_SIZE - 1; k >= 0; --k) {
		if (moveupdates[k][0] == -1)
			continue;

		int i = moveupdates[k][0];
		int j = moveupdates[k][1];

		double fx = ITOX(i, board.xorig, board.squaresize, ginfo.selfcolor);
		double fy = JTOY(j, board.yorig, board.squaresize, ginfo.selfcolor);

		shade_t shade = (shade_t)(1 - (i + j) % 2);
		square_highlight_t hl = k > 1 ?
			SQUARE_HIGHLIGHT_UNSELECTED : SQUARE_HIGHLIGHT_MOVE_INVOLVED;
		pthread_mutex_lock(&hctx->xlock);
		draw_square(fx, fy, board.squaresize, shade, hl);
		pthread_mutex_unlock(&hctx->xlock);

		pthread_mutex_lock(&hctx->gamelock);
		piece_t piece = game_get_piece(i, j);
		pthread_mutex_unlock(&hctx->gamelock);
		if (piece) {
			pthread_mutex_lock(&hctx->gamelock);
			int color = game_get_color(i, j);
			pthread_mutex_unlock(&hctx->gamelock);

			pthread_mutex_lock(&hctx->xlock);
			draw_piece(fx, fy, board.squaresize, piece, (shade_t)color);
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
		board.squaresize = ((double)height) / NF;
	} else {
		board.xorig = 0;
		board.yorig = -d;
		board.size = width;
		board.squaresize = ((double)width) / NF;
	}

	pthread_mutex_lock(&hctx->xlock);
	draw_record();
	draw_clear();
	pthread_mutex_unlock(&hctx->xlock);

	for (int j = 0; j < NF; ++j) {
		for (int i = 0; i < NF; ++i) {
			double fx = ITOX(i, board.xorig, board.squaresize, ginfo.selfcolor);
			double fy = JTOY(j, board.yorig, board.squaresize, ginfo.selfcolor);

			shade_t shade = (shade_t)(1 - (i + j) % 2);
			square_highlight_t hl = SQUARE_HIGHLIGHT_UNSELECTED;
			if (i == selsquare[0] && j == selsquare[1]) {
				hl = SQUARE_HIGHLIGHT_SELECTED;
			} else if ((i == moveupdates[0][0] && j == moveupdates[0][1])
					|| (i == moveupdates[1][0] && j == moveupdates[1][1])) {
				hl = SQUARE_HIGHLIGHT_MOVE_INVOLVED;
			}
			pthread_mutex_lock(&hctx->xlock);
			draw_square(fx, fy, board.squaresize, shade, hl);
			pthread_mutex_unlock(&hctx->xlock);

			pthread_mutex_lock(&hctx->gamelock);
			piece_t piece = game_get_piece(i, j);
			pthread_mutex_unlock(&hctx->gamelock);
			if (piece) {
				pthread_mutex_lock(&hctx->gamelock);
				int color = game_get_color(i, j);
				pthread_mutex_unlock(&hctx->gamelock);

				pthread_mutex_lock(&hctx->xlock);
				draw_piece(fx, fy, board.squaresize, piece, (shade_t)color);
				pthread_mutex_unlock(&hctx->xlock);
			}
		}
	}

	pthread_mutex_lock(&hctx->xlock);
	draw_commit();
	pthread_mutex_unlock(&hctx->xlock);
}

static void handle_clientmessage(struct gfxh_event_clientmessage *e)
{
	if (e->data.l[0] == atoms[ATOM_DELETE_WINDOW]) {
		gfxh_cleanup();
		pthread_exit(NULL);
	}
}
static void handle_redraw(struct gfxh_event_redraw *e)
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
static void handle_touch(struct gfxh_event_touch *e)
{
	if (ginfo.status != STATUS_MOVING_WHITE && ginfo.status != STATUS_MOVING_BLACK)
		return;

	pthread_mutex_lock(&hctx->gamelock);
	int isplaying = game_get_active_color() == ginfo.selfcolor;
	pthread_mutex_unlock(&hctx->gamelock);
	if (!isplaying)
		return;

	sqid f[2] = {
		XTOI(e->x, board.xorig, board.squaresize, ginfo.selfcolor),
		YTOJ(e->y, board.yorig, board.squaresize, ginfo.selfcolor)
	};
	if (f[0] < 0 || f[0] >= NF || f[1] < 0 || f[1] >= NF)
		return;

	if (e->flags & TOUCH_RELEASE && (selsquare[0] == -1 || memcmp(f, selsquare, sizeof(f)) == 0))
		return;

	pthread_mutex_lock(&hctx->gamelock);
	int targetspiece = game_is_movable_piece_at(f[0], f[1]);
	pthread_mutex_unlock(&hctx->gamelock);
	if (targetspiece) {
		if (memcmp(f, selsquare, 2 * sizeof(sqid)) == 0) { /* unselect piece */
			unselectf();
		} else if (selsquare[0] == -1) { /* select piece */
			selectf(f);
		} else { /* switch selection over to piece */
			unselectf();
			if (!(e->flags & TOUCH_RELEASE))
				selectf(f);
		}
	} else if (selsquare[0] != -1) {
		long tmove = measure_move_time(ginfo.tiself.movestart);
		long tstamp = measure_timestamp();

		piece_t piece;
		piece_t prompiece = PIECE_NONE;
		int err = apply_move(selsquare, f, &piece, &prompiece, tmove, 0);
		if (err == 1) {
			unselectf();
			return;
		}

		union msg_t mmove;
		memset(&mmove, 0, sizeof(mmove));
		mmove.playmove.type = GFXH_EVENT_PLAYMOVE;
		mmove.playmove.piece = piece;
		memcpy(mmove.playmove.from, selsquare, sizeof(mmove.playmove.from));
		memcpy(mmove.playmove.to, f, sizeof(mmove.playmove.to));
		mmove.playmove.prompiece = prompiece;
		mmove.playmove.tmove = tmove;
		mmove.playmove.tstamp = tstamp;
		err = send_msg(&mmove);
		if (err == -1) {
			SYSERR();
			gfxh_cleanup();
			pthread_exit(NULL);
		}

		/* communicate status */
		union msg_t mstat;
		memset(&mstat, 0, sizeof(mstat));
		mstat.statuschange.type = GFXH_EVENT_STATUSCHANGE;
		mstat.statuschange.status = ginfo.status;
		err = send_msg(&mstat);
		if (err == -1) {
			SYSERR();
			gfxh_cleanup();
			pthread_exit(NULL);
		}

		unselectf();
		show();
	}
}
static void handle_playmove(struct msg_playmove *e)
{
	if (ginfo.status != STATUS_MOVING_WHITE && ginfo.status != STATUS_MOVING_BLACK) {
		fprintf(stderr, "%s: received move in state %s", __func__, statmsg_names[(int)ginfo.status]);
		gfxh_cleanup();
		pthread_exit(NULL);
	}
	
	long tstamp = measure_timestamp();
	if (tstamp - e->tstamp > TRANSDIFF_MAX) {
		fprintf(stderr, "warning: move transmission time larger than %li\n", TRANSDIFF_MAX);
	}

	int err = apply_move(e->from, e->to, NULL, &e->prompiece, e->tmove, 1);
	if (err == 1) {
		fprintf(stderr, "%s: received illegal move", __func__);
		gfxh_cleanup();
		pthread_exit(NULL);
	}

	/* communicate status */
	union msg_t m;
	memset(&m, 0, sizeof(m));
	m.statuschange.type = GFXH_EVENT_STATUSCHANGE;
	m.statuschange.status = ginfo.status;
	err = send_msg(&m);
	if (err == -1) {
		SYSERR();
		gfxh_cleanup();
		pthread_exit(NULL);
	}

	show();
}
static void handle_statuschange(struct msg_statuschange *e)
{
	char *soundfname = NULL;

	pthread_mutex_lock(&hctx->gamelock);
	color_t activecolor = game_get_active_color();
	pthread_mutex_unlock(&hctx->gamelock);

	struct timeinfo_t tiwhite = ginfo.selfcolor == COLOR_WHITE ? ginfo.tiself : ginfo.tiopp;
	struct timeinfo_t tiblack = ginfo.selfcolor == COLOR_BLACK ?  ginfo.tiself : ginfo.tiopp;

	switch (e->status) {
	case STATUS_MOVING_WHITE:
	case STATUS_MOVING_BLACK:
		break;
	case STATUS_CHECKMATE_WHITE:
	case STATUS_CHECKMATE_BLACK:
	case STATUS_DRAW_MATERIAL:
	case STATUS_DRAW_STALEMATE:
	case STATUS_DRAW_REPETITION:
	case STATUS_DRAW_FIFTY_MOVES:
		soundfname = SOUND_GAME_DECIDED_FNAME;
		break;
	case STATUS_TIMEOUT_WHITE:
		if (activecolor != COLOR_WHITE)
			goto err_false_claim;

		if (ginfo.status == STATUS_MOVING_WHITE && labs(tiwhite.total) <= TIMEOUTDIFF_MAX)
			ginfo.status = e->status;

		soundfname = SOUND_GAME_DECIDED_FNAME;
		break;
	case STATUS_TIMEOUT_BLACK:
		if (activecolor != COLOR_BLACK)
			goto err_false_claim;

		if (ginfo.status == STATUS_MOVING_BLACK && labs(tiblack.total) <= TIMEOUTDIFF_MAX)
			ginfo.status = e->status;

		soundfname = SOUND_GAME_DECIDED_FNAME;
		break;
	case STATUS_DRAW_MATERIAL_VS_TIMEOUT:
		if (ginfo.status == STATUS_MOVING_WHITE
				&& labs(tiwhite.total) <= TIMEOUTDIFF_MAX) {
			ginfo.status = e->status;
		} else if (ginfo.status == STATUS_MOVING_BLACK
				&& labs(tiblack.total) <= TIMEOUTDIFF_MAX) {
			ginfo.status = e->status;
		}

		soundfname = SOUND_GAME_DECIDED_FNAME;
		break;
	case STATUS_SURRENDER_WHITE:
	case STATUS_SURRENDER_BLACK:
		ginfo.status = e->status;
		soundfname = SOUND_GAME_DECIDED_FNAME;
		break;
	default:
		assert(0);
	}
	if (e->status != ginfo.status)
		goto err_false_claim;

	show_status(ginfo);

	if (soundfname) {
		union audioh_event_t esound;
		memset(&esound, 0, sizeof(esound));
		esound.playsound.type = AUDIOH_EVENT_PLAYSOUND;
		esound.playsound.fname = soundfname;
		int n = hwrite(hctx->audioh.pevent[1], &esound, sizeof(esound));
		if (n == -1) {
			SYSERR();
			gfxh_cleanup();
			pthread_exit(NULL);
		}
	}

	return;

err_false_claim:
	fprintf(stderr, "%s: opponent claims %s\n", __func__,
			statmsg_names[(int)e->status]);
	fprintf(stderr, "%s: game will not be continued with unreasonable opponent\n",
			__func__);
	gfxh_cleanup();
	pthread_exit(NULL);
}
static void handle_updatetime(void)
{
	if (ginfo.status != STATUS_MOVING_BLACK
			&& ginfo.status != STATUS_MOVING_WHITE)
		return;
	if (ginfo.tiself.movestart == ginfo.tstart
			&& ginfo.tiopp.movestart == ginfo.tstart)
		return;

	pthread_mutex_lock(&hctx->gamelock);
	int isplaying = game_get_active_color() == ginfo.selfcolor;
	pthread_mutex_unlock(&hctx->gamelock);
	struct timeinfo_t *tiplayer = isplaying ? &ginfo.tiself : &ginfo.tiopp;

	long movetime = measure_move_time(tiplayer->movestart);
	tiplayer->total = tiplayer->subtotal - movetime;
	if (tiplayer->total < 0) {
		tiplayer->total = 0;
		
		if (selsquare[0] != -1)
			unselectf();

		/* update status */
		status_t status;
		if (isplaying) {
			status = ginfo.selfcolor ? STATUS_TIMEOUT_BLACK : STATUS_TIMEOUT_WHITE;
		} else {
			status = ginfo.selfcolor ? STATUS_TIMEOUT_WHITE : STATUS_TIMEOUT_BLACK;
		}
		pthread_mutex_lock(&hctx->gamelock);
		game_get_status(&status);
		pthread_mutex_unlock(&hctx->gamelock);

		/* communicate status */
		union msg_t m;
		memset(&m, 0, sizeof(m));
		m.statuschange.type = GFXH_EVENT_STATUSCHANGE;
		m.statuschange.status = status;
		int err = send_msg(&m);
		if (err == -1) {
			SYSERR();
			gfxh_cleanup();
			pthread_exit(NULL);
		}

		ginfo.status = status;
		show_status(ginfo);
	} else if (movetime >= time_status_updates_num * TIME_STATUS_UPDATE_INTERVAL) {
		show_status(ginfo);
		++time_status_updates_num;
	}
}

static void gfxh_setup(void)
{
	int err;
	if (fcntl(fevent, F_SETFL, O_NONBLOCK) == -1) {
		SYSERR();
		goto cleanup_err;
	}
	if (fcntl(fopp, F_SETFL, O_NONBLOCK) == -1) {
		SYSERR();
		goto cleanup_err;
	}

	pfds[0].fd = fevent;
	pfds[0].events = POLLIN;
	pfds[1].fd = fopp;
	pfds[1].events = POLLIN;

	selsquare[0] = -1;
	selsquare[1] = -1;
	memset(moveupdates, 0xff, sizeof(moveupdates));

	pthread_mutex_lock(&hctx->xlock);
	XWindowAttributes wa;
	XGetWindowAttributes(dpy, winmain, &wa);

	board.surface = cairo_xlib_surface_create(dpy, winmain, vis, wa.width, wa.height);
	draw_init_context(board.surface);
	pthread_mutex_unlock(&hctx->xlock);

	board.xorig = 0;
	board.yorig = 0;
	board.size = wa.width;
	board.squaresize = wa.height / NF;

	pthread_mutex_lock(&hctx->gamelock);
	err = game_init(STARTPOS_FEN);
	pthread_mutex_unlock(&hctx->gamelock);
	if (err == -1) {
		SYSERR();
		goto cleanup_err;
	}

	ginfo.tiself.subtotal = ginfo.time;
	ginfo.tiself.movestart = ginfo.tstart;
	ginfo.tiself.total = ginfo.tiself.subtotal;
	ginfo.tiopp.subtotal = ginfo.time;
	ginfo.tiopp.movestart = ginfo.tstart;
	ginfo.tiopp.total = ginfo.tiopp.subtotal;

	ginfo.status = STATUS_MOVING_WHITE;
	show_status(ginfo);
	time_status_updates_num = -1;
	return;

cleanup_err:
	pthread_mutex_lock(&hctx->mainlock);
	hctx->terminate = 1;
	pthread_mutex_unlock(&hctx->mainlock);
	pthread_exit(NULL);
}
static void gfxh_run(void)
{
	union gfxh_event_t e;
	memset(&e, 0, sizeof(e));

	union msg_t m;
	memset(&m, 0, sizeof(m));
	while (1) {
		int n = poll(pfds, ARRNUM(pfds), 0);
		if (n == -1) {
			SYSERR();
			goto cleanup_err;
		}

		if (pfds[0].revents) {
			n = hread(fevent, &e, sizeof(e));
			if (n == -1) {
				SYSERR();
				goto cleanup_err;
			} else if (n == 1) {
				break;
			}

			switch (e.type) {
			case GFXH_EVENT_CLIENTMESSAGE:
				handle_clientmessage(&e.clientmessage);
				break;
			case GFXH_EVENT_REDRAW:
				handle_redraw(&e.redraw);
				break;
			case GFXH_EVENT_TOUCH:
				handle_touch(&e.touch);
				break;
			default:
				fprintf(stderr, "%s: received unexpected event\n", __func__);
				goto cleanup_err;
			}
		} else if (pfds[1].revents) {
			n = recv_msg(&m);
			if (n == -1) {
				SYSERR();
				goto cleanup_err;
			} else if (n == -2 || n == 2) {
				fprintf(stderr, "%s: received invalid message\n", __func__);
				goto cleanup_err;
			} else if (n == -3) {
				fprintf(stderr, "%s: received too large message\n", __func__);
				goto cleanup_err;
			} else if (n == 1) { /* shutdown of opponent */
				break;
			}

			switch (m.type) {
			case GFXH_EVENT_PLAYMOVE:
				handle_playmove(&m.playmove);
				break;
			case GFXH_EVENT_STATUSCHANGE:
				handle_statuschange(&m.statuschange);
				break;
			default:
				fprintf(stderr, "%s: received unexpected message\n", __func__);
				goto cleanup_err;
			}
		} else {
			if (ginfo.time)
				handle_updatetime();
			usleep(GFXH_EVENT_RESPONSE_TIME);
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
		fprintf(stderr, "%s: could not resolve address and port information, %s\n",
				__func__, gai_strerror(err));
		close(fsock);
		exit(-1);
	}

	int val = 1;
	setsockopt(fsock, SOL_SOCKET, SO_REUSEADDR, &val, sizeof(val));
	if (bind(fsock, res->ai_addr, res->ai_addrlen) == -1) {
		fprintf(stderr, "%s: could not bind to specified address\n", __func__);
		SYSERR();
		close(fsock);
		exit(-1);
	}
	freeaddrinfo(res);

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
	long tstartreal, tstart;
	measure_game_start(&tstartreal, &tstart);

	union msg_t m;
	memset(&m, 0, sizeof(m));
	m.init.type = GFXH_EVENT_INIT;
	m.init.color = color;
	m.init.gametime = gametime;
	m.init.tstamp = tstartreal;
	err = send_msg(&m);
	if (err == -1) {
		SYSERR();
		close(fopp);
		exit(-1);
	}

	ginfo.selfcolor = color;
	ginfo.time = gametime;
	ginfo.tstart = tstart;
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

	union msg_t m;
	err = recv_msg(&m);
	if (err == -1) {
		SYSERR();
		close(fopp);
		exit(-1);
	} else if (err == -2 || err == 2) {
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
	if (m.type != GFXH_EVENT_INIT) {
		fprintf(stderr, "%s: expected init message", __func__);
		close(fopp);
		exit(-1);
	}

	long tstart;
	err = get_game_start_mono(m.init.tstamp, &tstart);
	if (err == 1) {
		fprintf(stderr, "%s: server and client don't agree on start time\n", __func__);
		close(fopp);
		exit(-1);
	}

	ginfo.selfcolor = m.init.color;
	ginfo.time = m.init.gametime;
	ginfo.tstart = tstart;
}
