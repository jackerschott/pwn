#define _GNU_SOURCE

#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <pthread.h>

#include <X11/Xlib.h>
#include <X11/keysym.h>
#include <cairo/cairo-xlib.h>

#include "chess.h"
#include "draw.h"
#include "game.h"

#define SYSERREXIT() do { \
	SYSERR(); \
	cleanup(); \
	exit(-1); \
} while (0);

/* definitions */

/* X11 */
Display *dpy;
static Window winmain;

enum { WMDeleteWindow, WMCount };
static Atom atoms[WMCount];

int terminate = 0;

/* handle moves */
#define XTOI(x, xorig, fieldsize) ((int)(((x) - xorig) / fieldsize))
#define YTOJ(y, yorig, fieldsize) (NF - (int)(((y) - yorig) / fieldsize) - 1)
#define ITOX(i, xorig, fieldsize) (xorig + (i) * fieldsize)
#define JTOY(j, yorig, fieldsize) (yorig + (NF - (j) - 1) * fieldsize)
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
	GFXH_EVENT_REDRAW,
	GFXH_EVENT_TOUCH,
	GFXH_EVENT_TERM,
};
struct eventtouch {
	char type;
	fid f[2];
};
struct eventredraw {
	char type;
	int width, height;
};
struct eventterm {
	char type;
};
union event_t {
	char type;
	struct eventtouch touch;
	struct eventredraw redraw;
	struct eventterm term;
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
	struct timespec tredraw;
	int flags;
} gfxh;

/* game */
fid clickfield[2] = {-1, -1};
const int player_color = COLOR_WHITE;

static void gfxh_cleanup(void);

/* TODO: maybe move thread safety handling to draw.c */
static void selectf(fid f[2])
{
	pthread_mutex_lock(&gfxh.mutex);
	draw_record();
	pthread_mutex_unlock(&gfxh.mutex);

	double fx = ITOX(f[0], board.xorig, board.fieldsize);
	double fy = JTOY(f[1], board.yorig, board.fieldsize);
	pthread_mutex_lock(&gfxh.mutex);
	draw_field(fx, fy, board.fieldsize, (f[0] + f[1]) % 2, 1);
	pthread_mutex_unlock(&gfxh.mutex);

	piece_t piece = game_get_piece(f[0], f[1]);
	if (piece) {
		color_t color = game_get_color(f[0], f[1]);

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
	double fy = JTOY(f[1], board.yorig, board.fieldsize);
	pthread_mutex_lock(&gfxh.mutex);
	draw_field(fx, fy, board.fieldsize, (f[0] + f[1]) % 2, 0);
	pthread_mutex_unlock(&gfxh.mutex);

	piece_t piece = game_get_piece(f[0], f[1]);
	if (piece) {
		color_t color = game_get_color(f[0], f[1]);

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
		double fy = JTOY(j, board.yorig, board.fieldsize);
		pthread_mutex_lock(&gfxh.mutex);
		draw_field(fx, fy, board.fieldsize, (i + j) % 2, 0);
		pthread_mutex_unlock(&gfxh.mutex);

		int piece = game_get_piece(i, j);
		if (piece & PIECEMASK) {
			int color = game_get_color(i, j);

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
	if (game_move(from[0], from[1], to[0], to[1], 0))
		return;

	fid updates[NUM_UPDATES_MAX][2];
	game_get_updates(updates);
	show(updates);

	if (game_is_checkmate()) {
		printf("checkmate!\n");
	}
	if (game_is_stalemate()) {
		printf("stalemate!\n");
	}

	memset(board.selfield, 0xff, 2 * sizeof(fid));
}

static void redraw(int width, int height)
{
	pthread_mutex_lock(&gfxh.mutex);
	cairo_xlib_surface_set_size(board.surface, width, height);
	pthread_mutex_unlock(&gfxh.mutex);

	pthread_mutex_lock(&gfxh.mutex);
	draw_record();
	draw_clear();
	pthread_mutex_unlock(&gfxh.mutex);
	for (int j = 0; j < NF; ++j) {
		for (int i = 0; i < NF; ++i) {
			double fx = ITOX(i, board.xorig, board.fieldsize);
			double fy = JTOY(j, board.yorig, board.fieldsize);
			pthread_mutex_lock(&gfxh.mutex);
			draw_field(fx, fy, board.fieldsize, (i + j) % 2, 0);
			pthread_mutex_unlock(&gfxh.mutex);

			int piece = game_get_piece(i, j);
			if (piece & PIECEMASK) {
				int color = game_get_color(i, j);
				pthread_mutex_lock(&gfxh.mutex);
				draw_piece(fx, fy, board.fieldsize, FIGURE(piece), PALETTE(color));
				pthread_mutex_unlock(&gfxh.mutex);
			}
		}
	}
	pthread_mutex_lock(&gfxh.mutex);
	draw_commit();
	pthread_mutex_unlock(&gfxh.mutex);
}

static void handle_redraw(struct eventredraw *e)
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
static void handle_touch(struct eventtouch *e)
{
	int touchpiece = game_is_movable_piece_at(e->f[0], e->f[1]);
	if (memcmp(e->f, board.selfield, 2 * sizeof(fid)) == 0) {
		unselectf(e->f);
	} else if (touchpiece && board.selfield[0] == -1) {
		selectf(e->f);
	} else if (touchpiece) {
		unselectf(board.selfield);
		selectf(e->f);
	} else if (board.selfield[0] != -1) {
		move(board.selfield, e->f);
	}
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
	pthread_mutex_lock(&gfxh.mutex);
	int term = terminate;
	pthread_mutex_unlock(&gfxh.mutex);
	while (!term) {
		ssize_t n = read(gfxh.pevent[0], &gfxh.event, sizeof(gfxh.event));
		if (n == -1) {
			SYSERR();
			gfxh_cleanup();
			pthread_exit(NULL);
		} else if (n != sizeof(gfxh.event)) {
			BUG();
		}

		if (gfxh.event.type == GFXH_EVENT_REDRAW) {
			handle_redraw(&gfxh.event.redraw);
		} else if (gfxh.event.type == GFXH_EVENT_TOUCH) {
			handle_touch(&gfxh.event.touch);
		}

		pthread_mutex_lock(&gfxh.mutex);
		term = terminate;
		pthread_mutex_unlock(&gfxh.mutex);
	}
}
static void gfxh_cleanup(void)
{
	return;
}
void *gfxh_main(void *args)
{
	gfxh_setup();
	gfxh_run();
	//gfxh_cleanup();
	return 0;
}

static void cleanup(void);

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

	pthread_mutexattr_t mutexattr;
	if ((errno = pthread_mutexattr_init(&mutexattr))) {
		SYSERR();
		goto err_close_pipes;
	}
	if ((errno = pthread_mutex_init(&gfxh.mutex, &mutexattr))) {
		SYSERR();
		if ((errno = pthread_mutexattr_destroy(&mutexattr)))
			fprintf(stderr, "%s: error while destroying movehandler mutexattr\n", __func__);
		goto err_close_pipes;
	}
	if ((errno = pthread_mutexattr_destroy(&mutexattr))) {
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
	event.term.type = GFXH_EVENT_TERM;
	if (write(gfxh.pevent[1], &event, sizeof(event)) < sizeof(event))
		fprintf(stderr, "%s: error while terminating graphicshandler thread", __func__);

	terminate = 1;
	if ((errno = pthread_join(gfxh.thread, NULL)))
		fprintf(stderr, "%s: error while joining graphicshandler thread\n", __func__);

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
	event.redraw.type = GFXH_EVENT_REDRAW;
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
	clickfield[1] = YTOJ(e->y, board.yorig, board.fieldsize);

	union event_t event;
	event.touch.type = GFXH_EVENT_TOUCH;
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
		YTOJ(e->y, board.yorig, board.fieldsize) };
	if (memcmp(f, clickfield, 2 * sizeof(fid)) == 0)
		return;

	union event_t event;
	event.touch.type = GFXH_EVENT_TOUCH;
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

static void setup(void)
{
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

	draw_destroy_context();
	cairo_surface_destroy(board.surface);
	XUnmapWindow(dpy, winmain);
	XDestroyWindow(dpy, winmain);
	XCloseDisplay(dpy);
}
static void run(void)
{
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
	if (argc > 1) {
		if (game_load_board(argv[1])) {
			perror("game_load_board");
			exit(-1);
		}
	} else {
		game_init_board(player_color);
	}

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
