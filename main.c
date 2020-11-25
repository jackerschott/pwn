#define _GNU_SOURCE

#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/random.h>
#include <unistd.h>

#include <arpa/inet.h>
#include <netdb.h>

#include <pthread.h>

#include <X11/Xlib.h>
#include <X11/keysym.h>
#include <cairo/cairo-xlib.h>

#include "chess.h"
#include "config.h"
#include "draw.h"
#include "game.h"

#define SYSERREXIT() do { \
	SYSERR(); \
	cleanup(); \
	exit(-1); \
} while (0);

/* options */
enum {
	OPTION_IS_SERVER = 1,
};
struct {
	color_t color;
	char *node;
	long tvgame;
	int flags;
} options;

/* X11 */
Display *dpy;
static Window winmain;

enum { WMDeleteWindow, WMCount };
static Atom atoms[WMCount];

int terminate = 0;

/* handle moves */
#define XTOI(x, xorig, fieldsize) ((int)(((x) - xorig) / fieldsize))
#define YTOJ(y, yorig, fieldsize, c) \
	((1 - c) * ((NF - 1) - (int)(((y) - yorig) / fieldsize)) \
	+ (c) * (int)(((y) - yorig) / fieldsize))
#define ITOX(i, xorig, fieldsize) (xorig + (i) * fieldsize)
#define JTOY(j, yorig, fieldsize, c) \
	(yorig + ((1 - c) * ((NF - 1) - (j)) + (c) * (j)) * fieldsize)
#define FIGURE(p) ((p) / 2 - 1)
#define PALETTE(c) (c)

static struct {
	cairo_surface_t *surface;
	double xorig;
	double yorig;
	double size;
	double fieldsize;
	fid selfield[2];
} board;

enum {
	EVENT_REDRAW,
	EVENT_TOUCH,
	EVENT_OPPMOVE,
	EVENT_SYNCMOVES,
	EVENT_TERM,
};
struct event_redraw {
	char type;
	int width, height;
};
struct event_touch {
	char type;
	fid f[2];
	long ttouch;
};
struct event_oppmove {
	char type;
	fid from[2];
	fid to[2];
	long tmove;
};
struct event_syncmoves {
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
	struct event_oppmove oppmove;
	struct event_syncmoves syncmoves;
	struct event_term term;
};

enum {
	GFXH_IS_DRAWING = 1,
	GFXH_WAS_DISTURBED = 2,
};
static struct {
	pthread_t thread;
	pthread_mutex_t mutex;
	int pevent[2];
	int pconfirm[2];
	union event_t event;
	int flags;
} gfxh;

enum {
	COMH_IS_EXCHANGING = 1,
};
static struct {
	pthread_t thread;
	pthread_mutex_t mutex;
	int pevent[2];
	int pconfirm[2];
	union event_t event;
	int flags;
	int fopp;
} comh;

static pthread_mutex_t gamelock = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t xlock = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t gfxhlock = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t comhlock = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t mainlock = PTHREAD_MUTEX_INITIALIZER;

/* game */
fid clickfield[2] = {-1, -1};

static void comh_cleanup(void);

static void handle_syncmoves(struct event_syncmoves *e)
{
	pthread_mutex_lock(&comh.mutex);
	comh.flags |= COMH_IS_EXCHANGING;
	pthread_mutex_unlock(&comh.mutex);

	int ret = 0;
	size_t n = write(comh.pconfirm[1], &ret, sizeof(ret));
	if (n == -1) {
		SYSERR();
		comh_cleanup();
		pthread_exit(NULL);
	} else if (n < sizeof(ret)) {
		BUG();
	}

	/* send move */
	union event_t event;
	event.syncmoves.type = EVENT_SYNCMOVES;
	memcpy(event.syncmoves.from, e->from, sizeof(e->from));
	memcpy(event.syncmoves.to, e->to, sizeof(e->to));
	event.syncmoves.tmove = e->tmove;
	n = send(comh.fopp, &event, sizeof(event), 0);
	if (n == -1) {
		SYSERR();
		comh_cleanup();
		pthread_exit(NULL);
	} else if (n < sizeof(ret)) {
		BUG();
	}

	/* recv move */
	n = recv(comh.fopp, &event, sizeof(event), 0);
	if (n == -1) {
		SYSERR();
		comh_cleanup();
		pthread_exit(NULL);
	} else if (n < sizeof(event)) {
		BUG();
	}

	/* send opponent move event */
	event.oppmove.type = EVENT_OPPMOVE;
	n = write(gfxh.pevent[1], &event, sizeof(event));
	if (n == -1) {
		SYSERR();
		comh_cleanup();
		exit(-1);
	} else if (n < sizeof(event)) {
		BUG();
	}

	pthread_mutex_lock(&comh.mutex);
	comh.flags &= ~COMH_IS_EXCHANGING;
	pthread_mutex_unlock(&comh.mutex);
}

static void comh_setup(void)
{
	color_t c = options.color;
	if (c == COLOR_BLACK) {
		union event_t event;
		event.oppmove.type = EVENT_OPPMOVE;
		recvmove(event.oppmove.from, event.oppmove.to, &event.oppmove.tmove);

		size_t n = write(gfxh.pevent[1], &event, sizeof(event));
		if (n == -1) {
			SYSERR();
			comh_cleanup();
			exit(-1);
		} else if (n < sizeof(event)) {
			BUG();
		}
	}

	pthread_mutex_lock(&comh.mutex);
	comh.flags &= ~COMH_IS_EXCHANGING;
	pthread_mutex_unlock(&comh.mutex);
}
static void comh_run(void)
{
	while (1) {
		ssize_t n = read(comh.pevent[0], &comh.event, sizeof(comh.event));
		if (n == -1) {
			SYSERR();
			comh_cleanup();
			pthread_exit(NULL);
		} else if (n < sizeof(comh.event)) {
			BUG();
		}

		if (comh.event.type == EVENT_SYNCMOVES) {
			handle_syncmoves(&comh.event.syncmoves);
		} else if (comh.event.type == EVENT_TERM) {
			break;
		} 
	}
}
static void comh_cleanup(void)
{
	if (close(comh.fopp) == -1)
		fprintf(stderr, "error while closing socket, possible data loss\n");
	pthread_mutex_lock(&comh.mutex);
	terminate = 1;
	pthread_mutex_unlock(&comh.mutex);
}
static void *comh_main(void *args)
{
	comh_setup();
	comh_run();
	comh_cleanup();
	return 0;
}

static void gfxh_cleanup(void);

/* TODO: maybe move thread safety handling to draw.c */
static void selectf(fid f[2])
{
	pthread_mutex_lock(&gfxh.mutex);
	draw_record();
	pthread_mutex_unlock(&gfxh.mutex);

	double fx = ITOX(f[0], board.xorig, board.fieldsize);
	double fy = JTOY(f[1], board.yorig, board.fieldsize, options.color);
	pthread_mutex_lock(&gfxh.mutex);
	draw_field(fx, fy, board.fieldsize, (f[0] + f[1]) % 2, 1);
	pthread_mutex_unlock(&gfxh.mutex);

	pthread_mutex_lock(&gamelock);
	piece_t piece = game_get_piece(f[0], f[1]);
	pthread_mutex_unlock(&gamelock);
	if (piece) {
		pthread_mutex_lock(&gamelock);
		color_t color = game_get_color(f[0], f[1]);
		pthread_mutex_unlock(&gamelock);

		pthread_mutex_lock(&gfxh.mutex);
		draw_piece(fx, fy, board.fieldsize, FIGURE(piece), PALETTE(color));
		pthread_mutex_unlock(&gfxh.mutex);
	}

	pthread_mutex_lock(&gfxh.mutex);
	draw_commit();
	pthread_mutex_unlock(&gfxh.mutex);

	memcpy(board.selfield, f, 2 * sizeof(fid));
}
static void unselectf(fid f[2])
{
	pthread_mutex_lock(&gfxh.mutex);
	draw_record();
	pthread_mutex_unlock(&gfxh.mutex);

	double fx = ITOX(f[0], board.xorig, board.fieldsize);
	double fy = JTOY(f[1], board.yorig, board.fieldsize, options.color);
	pthread_mutex_lock(&gfxh.mutex);
	draw_field(fx, fy, board.fieldsize, (f[0] + f[1]) % 2, 0);
	pthread_mutex_unlock(&gfxh.mutex);

	pthread_mutex_lock(&gamelock);
	piece_t piece = game_get_piece(f[0], f[1]);
	pthread_mutex_unlock(&gamelock);
	if (piece) {
		pthread_mutex_lock(&gamelock);
		color_t color = game_get_color(f[0], f[1]);
		pthread_mutex_unlock(&gamelock);

		pthread_mutex_lock(&gfxh.mutex);
		draw_piece(fx, fy, board.fieldsize, FIGURE(piece), PALETTE(color));
		pthread_mutex_unlock(&gfxh.mutex);
	}

	pthread_mutex_lock(&gfxh.mutex);
	draw_commit();
	pthread_mutex_unlock(&gfxh.mutex);

	memset(board.selfield, 0xff, 2 * sizeof(fid));
}
static void show(fid updates[][2])
{
	double fx, fy;

	pthread_mutex_lock(&gfxh.mutex);
	draw_record();
	pthread_mutex_unlock(&gfxh.mutex);
	for (int k = 0; k < NUM_UPDATES_MAX && updates[k][0] != -1; ++k) {
		int i = updates[k][0];
		int j = updates[k][1];

		double fx = ITOX(i, board.xorig, board.fieldsize);
		double fy = JTOY(j, board.yorig, board.fieldsize, options.color);
		pthread_mutex_lock(&gfxh.mutex);
		draw_field(fx, fy, board.fieldsize, (i + j) % 2, 0);
		pthread_mutex_unlock(&gfxh.mutex);

		pthread_mutex_lock(&gamelock);
		int piece = game_get_piece(i, j);
		pthread_mutex_unlock(&gamelock);
		if (piece & PIECEMASK) {
			pthread_mutex_lock(&gamelock);
			int color = game_get_color(i, j);
			pthread_mutex_unlock(&gamelock);

			pthread_mutex_lock(&gfxh.mutex);
			draw_piece(fx, fy, board.fieldsize, FIGURE(piece), PALETTE(color));
			pthread_mutex_unlock(&gfxh.mutex);
		}
	}
	pthread_mutex_lock(&gfxh.mutex);
	draw_commit();
	pthread_mutex_unlock(&gfxh.mutex);
}
static void move(fid from[2], fid to[2])
{
	pthread_mutex_lock(&gamelock);
	int err = game_move(from[0], from[1], to[0], to[1], 0);
	pthread_mutex_unlock(&gamelock);
	if (err)
		return;

	pthread_mutex_lock(&gamelock);
	fid updates[NUM_UPDATES_MAX][2];
	game_get_updates(updates);
	pthread_mutex_unlock(&gamelock);
	show(updates);

	pthread_mutex_lock(&gamelock);
	if (game_is_checkmate()) {
		printf("checkmate!\n");
	}
	if (game_is_stalemate()) {
		printf("stalemate!\n");
	}
	pthread_mutex_unlock(&gamelock);

	memset(board.selfield, 0xff, 2 * sizeof(fid));
}

static void redraw(int width, int height)
{
	pthread_mutex_lock(&xlock);
	cairo_xlib_surface_set_size(board.surface, width, height);
	pthread_mutex_unlock(&xlock);

	pthread_mutex_lock(&xlock);
	draw_record();
	draw_clear();
	pthread_mutex_unlock(&xlock);
	for (int j = 0; j < NF; ++j) {
		for (int i = 0; i < NF; ++i) {
			pthread_mutex_lock(&xlock);
			double fx = ITOX(i, board.xorig, board.fieldsize);
			double fy = JTOY(j, board.yorig, board.fieldsize, options.color);
			draw_field(fx, fy, board.fieldsize, (i + j) % 2, 0);
			pthread_mutex_unlock(&xlock);

			pthread_mutex_lock(&gamelock);
			int piece = game_get_piece(i, j);
			pthread_mutex_unlock(&gamelock);
			if (piece & PIECEMASK) {
				pthread_mutex_lock(&gamelock);
				int color = game_get_color(i, j);
				pthread_mutex_unlock(&gamelock);

				pthread_mutex_lock(&xlock);
				draw_piece(fx, fy, board.fieldsize, FIGURE(piece), PALETTE(color));
				pthread_mutex_unlock(&xlock);
			}
		}
	}
	pthread_mutex_lock(&xlock);
	draw_commit();
	pthread_mutex_unlock(&xlock);
}

static void handle_redraw(struct event_redraw *e)
{
	pthread_mutex_lock(&gfxh.mutex);
	gfxh.flags |= GFXH_IS_DRAWING;
	pthread_mutex_unlock(&gfxh.mutex);

	int ret = 0;
	if (write(gfxh.pconfirm[1], &ret, sizeof(ret)) < sizeof(ret)) {
		SYSERR();
		gfxh_cleanup();
		pthread_exit(NULL);
	}

	redraw(e->width, e->height);

	pthread_mutex_lock(&gfxh.mutex);
	int disturbed = gfxh.flags & GFXH_WAS_DISTURBED;
	gfxh.flags &= ~GFXH_IS_DRAWING;
	gfxh.flags &= ~GFXH_WAS_DISTURBED;
	pthread_mutex_unlock(&gfxh.mutex);

	int confnote = 0;
	pthread_mutex_lock(&gfxh.mutex);
	if (XPending(dpy)) {
		XEvent ev;
		XPeekEvent(dpy, &ev);
		confnote = ev.type == ConfigureNotify;
	}
	pthread_mutex_unlock(&gfxh.mutex);
	if (!confnote) {
		XWindowAttributes swa;
		pthread_mutex_lock(&gfxh.mutex);
		XGetWindowAttributes(dpy, winmain, &swa);
		pthread_mutex_unlock(&gfxh.mutex);

		redraw(swa.width, swa.height);
	}
}
static void handle_touch(struct event_touch *e)
{
	pthread_mutex_lock(&gamelock);
	int touchpiece = game_is_movable_piece_at(e->f[0], e->f[1]);
	pthread_mutex_unlock(&gamelock);
	if (memcmp(e->f, board.selfield, 2 * sizeof(fid)) == 0) {
		unselectf(e->f);
	} else if (touchpiece && board.selfield[0] == -1) {
		selectf(e->f);
	} else if (touchpiece) {
		unselectf(board.selfield);
		selectf(e->f);
	} else if (board.selfield[0] != -1) {
		pthread_mutex_lock(&comh.mutex);
		int canmove = !(comh.flags & COMH_IS_EXCHANGING);
		pthread_mutex_unlock(&comh.mutex);
		if (!canmove) {
			unselectf(board.selfield);
			return;
		}

		union event_t event;
		event.syncmoves.type = EVENT_SYNCMOVES;
		memcpy(event.syncmoves.from, board.selfield, sizeof(board.selfield));
		memcpy(event.syncmoves.to, e->f, sizeof(e->f));
		event.syncmoves.tmove = 0;

		size_t n = write(comh.pevent[1], &event, sizeof(event));
		if (n == -1) {
			SYSERR();
			gfxh_cleanup();
			pthread_exit(NULL);
		} else if (n < sizeof(event)) {
			BUG();
		}

		int ret;
		n = read(comh.pconfirm[0], &ret, sizeof(ret));
		if (n == -1) {
			SYSERR();
			gfxh_cleanup();
			pthread_exit(NULL);
		} else if (n < sizeof(ret)) {
			BUG();
		}

		move(board.selfield, e->f);
	}
}
static void handle_oppmove(struct event_oppmove *e)
{
	printf("oppmove: { from: {%i, %i}, to: {%i, %i}}\n", e->from[0], e->from[1], e->to[0], e->to[1]);
	move(e->from, e->to);
}

static void gfxh_setup(void)
{
	pthread_mutex_lock(&gfxh.mutex);
	board.selfield[0] = -1;
	board.selfield[1] = -1;
	pthread_mutex_unlock(&gfxh.mutex);
}
static void gfxh_run(void)
{
	while (1) {
		ssize_t n = read(gfxh.pevent[0], &gfxh.event, sizeof(gfxh.event));
		if (n == -1) {
			SYSERR();
			gfxh_cleanup();
			pthread_exit(NULL);
		} else if (n != sizeof(gfxh.event)) {
			BUG();
		}

		if (gfxh.event.type == EVENT_REDRAW) {
			handle_redraw(&gfxh.event.redraw);
		} else if (gfxh.event.type == EVENT_TOUCH) {
			handle_touch(&gfxh.event.touch);
		} else if (gfxh.event.type == EVENT_OPPMOVE) {
			handle_oppmove(&gfxh.event.oppmove);
		} else if (gfxh.event.type == EVENT_TERM) {
			break;
		}
	}
}
static void gfxh_cleanup(void)
{
	pthread_mutex_lock(&gfxh.mutex);
	terminate = 1;
	pthread_mutex_unlock(&gfxh.mutex);
}
void *gfxh_main(void *args)
{
	gfxh_setup();
	gfxh_run();
	gfxh_cleanup();
	return 0;
}

static void cleanup(void);

static void start_communicationhandler(void)
{
	const int stacksize = 4096 * 64;

	int pevent[2];
	int pconfirm[2];
	if (pipe(pevent) == -1) {
		SYSERR();
		cleanup();
		exit(-1);
	}
	if (pipe(pconfirm) == -1) {
		close(pevent[0]);
		close(pevent[1]);
		cleanup();
		exit(-1);
	}
		
	memcpy(comh.pevent, pevent, 2 * sizeof(int));
	memcpy(comh.pconfirm, pconfirm, 2 * sizeof(int));

	if ((errno = pthread_mutex_init(&comh.mutex, NULL))) {
		SYSERR();
		goto err_close_pipes;
	}

	pthread_attr_t attr;
	if ((errno = pthread_attr_init(&attr))) {
		SYSERR();
		goto err_close_pipes;
	}
	if ((errno = pthread_attr_setstacksize(&attr, stacksize))) {
		SYSERR();
		if ((errno = pthread_attr_destroy(&attr)))
			fprintf(stderr, "%s: error while destroying movehandler thread attributes\n", __func__);
		goto err_close_pipes;
	}

	if ((errno = pthread_create(&comh.thread, &attr, comh_main, NULL))) {
		SYSERR();
		if ((errno = pthread_attr_destroy(&attr)))
			fprintf(stderr, "%s: error while destroying movehandler thread attributes\n", __func__);
		goto err_close_pipes;
	}
	if ((errno = pthread_attr_destroy(&attr))) {
		SYSERR();
		cleanup();
		exit(-1);
	}

	comh.flags |= COMH_IS_EXCHANGING;
	return;

err_close_pipes:
	close(comh.pevent[0]);
	close(comh.pevent[1]);
	close(comh.pconfirm[0]);
	close(comh.pconfirm[1]);
	cleanup();
}
static void stop_communicationhandler(void)
{
	union event_t event;
	event.term.type = EVENT_TERM;
	if (write(comh.pevent[1], &event, sizeof(event)) < sizeof(event))
		fprintf(stderr, "%s: error while terminating graphicshandler thread", __func__);

	if ((errno = pthread_join(comh.thread, NULL)))
		fprintf(stderr, "%s: error while joining graphicshandler thread\n", __func__);

	if ((errno = pthread_mutex_destroy(&comh.mutex)))
		fprintf(stderr, "%s: error while destroying mutex\n", __func__);

	if (close(comh.pevent[1]) == -1 || close(comh.pevent[0])
			|| close(comh.pconfirm[0]) || close(comh.pconfirm[1]))
		fprintf(stderr, "%s: error while closing event pipe, potential data loss\n", __func__);
}
static void start_graphicshandler(void)
{
	const int stacksize = 4096 * 64;

	int pevent[2];
	int pconfirm[2];
	if (pipe(pevent) == -1) {
		SYSERR();
		cleanup();
		exit(-1);
	}
	if (pipe(pconfirm) == -1) {
		close(pevent[0]);
		close(pevent[1]);
		cleanup();
		exit(-1);
	}
		
	memcpy(gfxh.pevent, pevent, 2 * sizeof(int));
	memcpy(gfxh.pconfirm, pconfirm, 2 * sizeof(int));

	if ((errno = pthread_mutex_init(&gfxh.mutex, NULL))) {
		SYSERR();
		goto err_close_pipes;
	}

	pthread_attr_t attr;
	if ((errno = pthread_attr_init(&attr))) {
		SYSERR();
		goto err_close_pipes;
	}
	if ((errno = pthread_attr_setstacksize(&attr, stacksize))) {
		SYSERR();
		if ((errno = pthread_attr_destroy(&attr)))
			fprintf(stderr, "%s: error while destroying movehandler thread attributes\n", __func__);
		goto err_close_pipes;
	}

	if ((errno = pthread_create(&gfxh.thread, &attr, gfxh_main, NULL))) {
		SYSERR();
		if ((errno = pthread_attr_destroy(&attr)))
			fprintf(stderr, "%s: error while destroying movehandler thread attributes\n", __func__);
		goto err_close_pipes;
	}
	if ((errno = pthread_attr_destroy(&attr))) {
		SYSERR();
		cleanup();
		exit(-1);
	}
	return;

err_close_pipes:
	close(gfxh.pevent[0]);
	close(gfxh.pevent[1]);
	close(gfxh.pconfirm[0]);
	close(gfxh.pconfirm[1]);
	cleanup();
	exit(-1);
}
static void stop_graphicshandler(void)
{
	union event_t event;
	event.term.type = EVENT_TERM;
	if (write(gfxh.pevent[1], &event, sizeof(event)) < sizeof(event))
		fprintf(stderr, "%s: error while terminating communicationhandler thread", __func__);

	if ((errno = pthread_join(gfxh.thread, NULL)))
		fprintf(stderr, "%s: error while joining communicationhandler thread\n", __func__);

	if ((errno = pthread_mutex_destroy(&gfxh.mutex)))
		fprintf(stderr, "%s: error while destroying mutex\n", __func__);

	if (close(gfxh.pevent[1]) == -1 || close(gfxh.pevent[0])
			|| close(gfxh.pconfirm[0]) || close(gfxh.pconfirm[1]))
		fprintf(stderr, "%s: error while closing event pipe, potential data loss\n", __func__);
}

static void on_configure(XConfigureEvent *e)
{
	pthread_mutex_lock(&gfxh.mutex);
	double d = (e->width - e->height) / 2.0;
	if (d > 0) {
		board.xorig = d;
		board.yorig = 0;
		board.size = e->height;
		board.fieldsize = ((double)e->height) / NF;
	} else {
		board.xorig = 0;
		board.yorig = -d;
		board.size = e->width;
		board.fieldsize = ((double)e->width) / NF;
	}
	pthread_mutex_unlock(&gfxh.mutex);

	pthread_mutex_lock(&gfxh.mutex);
	int sleeping = gfxh.flags & GFXH_IS_DRAWING;
	pthread_mutex_unlock(&gfxh.mutex);
	if (sleeping) {
		pthread_mutex_lock(&gfxh.mutex);
		gfxh.flags |= GFXH_WAS_DISTURBED;
		pthread_mutex_unlock(&gfxh.mutex);
		return;
	}

	union event_t event = {0};
	event.redraw.type = EVENT_REDRAW;
	event.redraw.width = e->width;
	event.redraw.height = e->height;

	if (write(gfxh.pevent[1], &event, sizeof(event)) < sizeof(event)) {
		SYSERR();
		cleanup();
		exit(-1);
	}

	int res;
	if (read(gfxh.pconfirm[0], &res, sizeof(res)) == -1) {
		SYSERR();
		cleanup();
		exit(-1);
	}
}
static void on_client_message(XClientMessageEvent *e)
{
	pthread_mutex_lock(&gfxh.mutex);
	if ((Atom)e->data.l[0] == atoms[WMDeleteWindow])
		terminate = 1;
	pthread_mutex_unlock(&gfxh.mutex);
}
static void on_button_press(XButtonEvent *e)
{
	clickfield[0] = XTOI(e->x, board.xorig, board.fieldsize);
	clickfield[1] = YTOJ(e->y, board.yorig, board.fieldsize, options.color);

	union event_t event;
	event.touch.type = EVENT_TOUCH;
	memcpy(event.touch.f, clickfield, 2 * sizeof(fid));

	if (write(gfxh.pevent[1], &event, sizeof(event)) < sizeof(event)) {
		SYSERR();
		cleanup();
		exit(-1);
	}
}
static void on_button_release(XButtonEvent *e)
{
	fid f[2] = { XTOI(e->x, board.xorig, board.fieldsize),
		YTOJ(e->y, board.yorig, board.fieldsize, options.color) };
	if (memcmp(f, clickfield, 2 * sizeof(fid)) == 0)
		return;

	union event_t event;
	event.touch.type = EVENT_TOUCH;
	memcpy(event.touch.f, f, 2 * sizeof(fid));
	
	if (write(gfxh.pevent[1], &event, sizeof(event)) < sizeof(event)) {
		SYSERR();
		cleanup();
		exit(-1);
	}
}
static void on_keypress(XKeyEvent *e)
{
	pthread_mutex_lock(&gfxh.mutex);
	KeySym ksym = XLookupKeysym(e, 0);
	pthread_mutex_unlock(&gfxh.mutex);

	if (ksym == XK_s) {
		if (game_save_board("board")) {
			perror("game_save_board");
			exit(-1);
		}
		printf("board saved\n");
	} else if (ksym == XK_q) {
		terminate = 1;
	}
}

static int init_communication(int *err)
{
	int fsock = socket(AF_INET, SOCK_STREAM, 0);
	if (fsock == -1)
		return -1;

	int fopp;
	if (options.flags & OPTION_IS_SERVER) {
		struct addrinfo hints = {0};
		hints.ai_family = AF_UNSPEC;
		hints.ai_socktype = SOCK_STREAM;
		hints.ai_flags = AI_PASSIVE;

		struct addrinfo *res;
		*err = getaddrinfo(options.node, PORTSTR, &hints, &res);
		if (*err) {
			close(fsock);
			return -2;
		}

		if (bind(fsock, res->ai_addr, res->ai_addrlen) == -1) {
			close(fsock);
			return -1;
		}

		struct sockaddr addr;
		if (listen(fsock, 1) == -1) {
			close(fsock);
			return -1;
		}

		fopp = accept(fsock, NULL, NULL);
		if (comh.fopp == -1) {
			close(fsock);
			return -1;
		}
		close(fsock);
	}
	else {
		fopp = fsock;

		struct addrinfo hints = {0};
		hints.ai_family = AF_UNSPEC;
		hints.ai_socktype = SOCK_STREAM;

		struct addrinfo *res;
		*err = getaddrinfo(options.node, PORTSTR, &hints, &res);
		if (*err) {
			close(fopp);
			return -2;
		}

		if (connect(fopp, res->ai_addr, res->ai_addrlen) == -1) {
			close(fopp);
			return 1;
		}
	}

	if (options.flags & OPTION_IS_SERVER) {
		printf("send option color\n");
		size_t n = send(fopp, &options.color, sizeof(options.color), 0);
		if (n == -1) {
			close(fopp);
			return -1;
		} else if (n < sizeof(options.color)) {
			BUG();
		}
		printf("options.color = %i\n", options.color);
	} else {
		printf("receive option color\n");
		color_t c;
		size_t n = recv(fopp, &c, sizeof(c), 0);
		if (n == -1) {
			close(fopp);
			return -1;
		} else if (n < sizeof(c)) {
			BUG();
		}
		options.color = OPP_COLOR(c);
		printf("options.color = %i\n", options.color);
	}

	comh.fopp = fopp;
	return 0;
}
static void free_options()
{
	free(options.node);
}
static void set_option(char key, const char *val)
{
	if (key == 'a') {
		if (!val)
			goto err_noarg;

		char *node = malloc(sizeof(val));
		if (!node) {
			SYSERR();
			free_options();
			exit(-1);
		}
		options.node = node;

		strcpy(options.node, val);
	} else if (key == 'c') {
		if (!val)
			goto err_noarg;

		if (strcmp(val, "white") == 0) {
			options.color = COLOR_WHITE;
		} else if (strcmp(val, "black") == 0) {
			options.color = COLOR_BLACK;
		} else {
			goto err_invalidarg;
		}
	} else if (key == 'l') {
		if (!val)
			goto err_noarg;

		char *node = malloc(sizeof(val));
		if (!node) {
			SYSERR();
			free_options();
			exit(-1);
		}
		options.node = node;

		strcpy(options.node, val);
		options.flags |= OPTION_IS_SERVER;
	} else {
		goto err_invalid;
	}
	return;

err_invalid:
	fprintf(stderr, "not a valid option: `-%c'", key);
	free_options();
	exit(1);
err_noarg:
	fprintf(stderr, "you have to specify an argument for `-%c'", key);
	free_options();
	exit(1);
err_sparearg:
	fprintf(stderr, "you cannot specify an argument for `-%c'", key);
	free_options();
	exit(1);
err_invalidarg:
	fprintf(stderr, "not a valid argument for the option `%c': `%s'", key, val);
	free_options();
	exit(1);
}
static void parse_options(int argc, char *argv[])
{
	options.node = NULL;
	options.tvgame = 0;
	options.color = -1;

	char key = 0;
	char *val = NULL;
	for (int i = 1; i < argc; ++i) {
		if (argv[i][0] == '-') {
			if (key) {
				set_option(key, val);
				key = 0;
				val = NULL;
			}

			if (!argv[i][1]) {
				fprintf(stderr, "not a valid argument: `%s'", argv[i]);
				free_options();
				exit(1);
			}

			key = argv[i][1];
			size_t vallen = strlen(argv[i]) - 2;
			if (vallen)
				val = argv[i] + 2;
		} else if (key && !val) {
			size_t vallen = strlen(argv[i]) - 2;
			if (vallen)
				val = argv[i];
		} else {
			fprintf(stderr, "not a valid argument: `%s'", argv[i]);
			free_options();
			exit(1);
		}
	}
	if (key && !val) {
		fprintf(stderr, "you have to specify an argument for `-%c'", key);
			free_options();
		exit(1);
	}

	if (key && val)
		set_option(key, val);

	if ((options.flags & OPTION_IS_SERVER) && options.color == -1) {
		unsigned int r;
		if (getrandom(&r, sizeof(r), GRND_RANDOM) == -1) {
			SYSERR();
			exit(-1);
		}
		options.color = r % 2;
	}

	printf("options: { address: %s, color: %i, is_server: %i }\n",
			options.node, options.color, (options.flags & OPTION_IS_SERVER) != 0);
}
static void setup(void)
{
	int gaierr;
	int err = init_communication(&gaierr);
	if (err == -1) {
		SYSERR();
		exit(-1);
	} else if (err == -2) {
		GAIERR(gaierr);
		exit(-1);
	} else if (err == 1) {
		fprintf(stderr, "could not connect to %s\n", options.node);
		SYSERR();
		exit(-1);
	}

	game_init_board();

	int scr = DefaultScreen(dpy);
	Window root = RootWindow(dpy, scr);
	Visual *vis = DefaultVisual(dpy, scr);
	int depth = DefaultDepth(dpy, scr);

	unsigned int winwidth = 800;
	unsigned int winheight = 800;

	winmain = XCreateWindow(dpy, root, 0, 0, winwidth, winheight, 0,
			depth, InputOutput, vis, 0, NULL);

	long mask = KeyPressMask | PointerMotionMask | ButtonPressMask
		| ButtonReleaseMask | StructureNotifyMask;
	XSelectInput(dpy, winmain, mask);
	XMapWindow(dpy, winmain);

	atoms[WMDeleteWindow] = XInternAtom(dpy, "WM_DELETE_WINDOW", False);

	XSetWMProtocols(dpy, winmain, &atoms[WMDeleteWindow], 1);

	board.surface = cairo_xlib_surface_create(dpy, winmain, vis, winwidth, winheight);
	draw_init_context(board.surface);

	board.xorig = 0;
	board.yorig = 0;
	board.size = winwidth;
}
static void cleanup(void)
{
	if (gfxh.thread)
		stop_graphicshandler();
	if (comh.thread)
		stop_communicationhandler();

	draw_destroy_context();
	cairo_surface_destroy(board.surface);
	XUnmapWindow(dpy, winmain);
	XDestroyWindow(dpy, winmain);
	XCloseDisplay(dpy);
	game_terminate();
}
static void run(void)
{
	start_communicationhandler();
	start_graphicshandler();

	XEvent ev;
	pthread_mutex_lock(&gfxh.mutex);
	int term = terminate;
	pthread_mutex_unlock(&gfxh.mutex);
	while (!term) {
		/* Prevent blocking of XNextEvent inside mutex lock */
		pthread_mutex_lock(&gfxh.mutex);
		int n = XPending(dpy);
		pthread_mutex_unlock(&gfxh.mutex);
		if (n == 0)
			continue;

		pthread_mutex_lock(&gfxh.mutex);
		XNextEvent(dpy, &ev);
		pthread_mutex_unlock(&gfxh.mutex);

		if (ev.type == ConfigureNotify) {
			on_configure(&ev.xconfigure);
		} else if (ev.type == ClientMessage) {
			on_client_message(&ev.xclient);
		} else if (ev.type == ButtonPress) {
			on_button_press(&ev.xbutton);
		} else if (ev.type == ButtonRelease) {
			on_button_release(&ev.xbutton);
		} else if (ev.type == KeyPress) {
			on_keypress(&ev.xkey);
		}

		pthread_mutex_lock(&gfxh.mutex);
		term = terminate;
		pthread_mutex_unlock(&gfxh.mutex);
	}
}
int main(int argc, char *argv[])
{
	parse_options(argc, argv);

	//if (argc > 1) {
	//	if (game_load_board(argv[1])) {
	//		perror("game_load_board");
	//		exit(-1);
	//	}
	//} else {
	//	game_init_board(player_color);
	//}

	//struct sigaction act;
	//struct sigaction oldact;

	//act.sa_handler = handle_sigchild;
	//sigemptyset(&act.sa_mask);
	//act.sa_flags = 0;
	//sigaction(SIGCHLD, &act, NULL);

	if (!XInitThreads()) {
		fprintf(stderr, "could not initialize X threads");
		exit(1);
	}

	dpy = XOpenDisplay(NULL);
	if (!dpy) {
		fprintf(stderr, "could not open X display");
		exit(1);
	}

	setup();
	run();
	cleanup();
	return 0;
}
