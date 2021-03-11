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

#define _GNU_SOURCE

#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/random.h>
#include <unistd.h>

#include <pthread.h>

#include <X11/Xlib.h>
#include <X11/keysym.h>

#include "audioh.h"
#include "config.h"
#include "game.h"
#include "gfxh.h"
#include "notation.h"
#include "pwn.h"
#include "util.h"

#define WINEVENT_RESPONSE_TIME 10000

#define GAMETIME_MAX (10L * HOUR)
#define MOVEINC_MAX MINUTE

/* options */
enum {
	OPTION_IS_SERVER = 1,
	OPTION_NO_OPPONENT = 2,
};
struct {
	color_t color;
	char *node;
	char *port;
	long gametime;
	long moveinc;
	int flags;
} options;

/* X11 */
static Display *dpy;
static Window winmain;

static struct handler_context_t *hctx;

static void cleanup(void);

static int start_thread(void *args, void *(*main)(void *), int stacksize, pthread_t *id)
{
	pthread_attr_t attr;
	if ((errno = pthread_attr_init(&attr))) {
		return 1;
	}
	if ((errno = pthread_attr_setstacksize(&attr, stacksize))) {
		pthread_attr_destroy(&attr);
		return 1;
	}
	if ((errno = pthread_create(id, &attr, main, args))) {
		pthread_attr_destroy(&attr);
	}
	pthread_attr_destroy(&attr);
	return 0;
}
static int start_handler(void *args, int state, void *(*main)(void *), struct handler_t *h)
{
	int pevent[2];
	int pconfirm[2];
	if (pipe(pevent) == -1) {
		return 1;
	}
	if (pipe(pconfirm) == -1) {
		close(pevent[0]);
		close(pevent[1]);
		return 1;
	}

	memcpy(h->pevent, pevent, 2 * sizeof(int));
	memcpy(h->pconfirm, pconfirm, 2 * sizeof(int));
	h->state = state;

	return start_thread(args, main, GFXH_STACKSIZE, &h->id);
}
static int stop_handler(struct handler_t *h)
{
	int ret = 0;
	if (close(h->pevent[1]) == -1) {
		fprintf(stderr, "warning: there could be a problem in handler thread termination");
		ret = 1;
	}

	if ((errno = pthread_join(h->id, NULL)))
		ret = 1;

	if (close(h->pconfirm[0]))
		ret = 1;

	return ret;
}

static void on_client_message(XClientMessageEvent *e)
{
	union gfxh_event_t event;
	memset(&event, 0, sizeof(event));
	event.clientmessage.type = GFXH_EVENT_CLIENTMESSAGE;
	memcpy(&event.clientmessage.data, &e->data, sizeof(e->data));
	int n = hwrite(hctx->gfxh.pevent[1], &event, sizeof(event));
	if (n == -1) {
		SYSERR();
		cleanup();
		exit(-1);
	}
}
static void on_configure(XConfigureEvent *e)
{
	pthread_mutex_lock(&hctx->gfxhlock);
	int isdrawing = hctx->gfxh.state & GFXH_IS_DRAWING;
	pthread_mutex_unlock(&hctx->gfxhlock);
	if (isdrawing) {
		return;
	}

	union gfxh_event_t event;
	memset(&event, 0, sizeof(event));
	event.redraw.type = GFXH_EVENT_REDRAW;
	event.redraw.width = e->width;
	event.redraw.height = e->height;
	int n = hwrite(hctx->gfxh.pevent[1], &event, sizeof(event));
	if (n == -1) {
		SYSERR();
		cleanup();
		exit(-1);
	}

	int res;
	n = hread(hctx->gfxh.pconfirm[0], &res, sizeof(res));
	if (n == -1) {
		SYSERR();
		cleanup();
		exit(-1);
	} else if (n == 1) {
		return;
	}
}
static void on_button_press(XButtonEvent *e)
{
	union gfxh_event_t event;
	memset(&event, 0, sizeof(event));
	event.touch.type = GFXH_EVENT_TOUCH;
	event.touch.x = e->x;
	event.touch.y = e->y;
	event.touch.flags = TOUCH_PRESS;
	int n = hwrite(hctx->gfxh.pevent[1], &event, sizeof(event));
	if (n == -1) {
		SYSERR();
		cleanup();
		exit(-1);
	}
}
static void on_button_release(XButtonEvent *e)
{
	union gfxh_event_t event;
	memset(&event, 0, sizeof(event));
	event.touch.type = GFXH_EVENT_TOUCH;
	event.touch.x = e->x;
	event.touch.y = e->y;
	event.touch.flags = TOUCH_RELEASE;
	int n = hwrite(hctx->gfxh.pevent[1], &event, sizeof(event));
	if (n == -1) {
		SYSERR();
		cleanup();
		exit(-1);
	}
}
static void on_keypress(XKeyEvent *e)
{
	pthread_mutex_lock(&hctx->xlock);
	KeySym ksym = XLookupKeysym(e, 0);
	pthread_mutex_unlock(&hctx->xlock);

	if (ksym == XK_q) {
		pthread_mutex_lock(&hctx->mainlock);
		hctx->terminate = 1;
		pthread_mutex_unlock(&hctx->mainlock);
	}
}

static int parse_gametime(const char *str, long *t, long *i)
{
	const char *c = str;
	if (!(c = parse_timeinterval(c, t, 1)))
		return 1;
	if (*c == '\0') {
		*i = 0;
		return 0;
	}

	if (*c != '+')
		return 1;

	long n;
	if (!(c = parse_number(c, &n)))
		return 1;
	if (n < 0)
		return 1;

	if (*c != '\0')
		return 1;

	*i = n * SECOND;
	return 0;
}
static void parse_options(int argc, char *argv[])
{
	int color = -1;
	char *port = NULL;
	char *node = NULL;
	long gametime = 0;
	long moveinc = 0;
	int flags = 0;

	/* check for option combination */
	char optstr[sizeof(":l:p:s:t:")] = "\0";
	for (int i = 1; i < argc; ++i) {
		if (strstr(argv[i], "-n") != NULL) {
			strcpy(optstr, ":ns:");
		} else if (strstr(argv[i], "-c") != NULL) {
			strcpy(optstr, ":c:p:");
		} else if (strstr(argv[i], "-l") != NULL) {
			strcpy(optstr, ":l:p:s:t:");
		} else {
			continue;
		}
		break;
	}
	if (optstr[0] == 0) {
		strcpy(optstr, ":s:");
		flags = OPTION_NO_OPPONENT;
	}

	/* get options */
	int c = getopt(argc, argv, optstr);
	for (; c != -1; c = getopt(argc, argv, optstr)) {
		switch (c) {
		case 'n':
			flags |= OPTION_NO_OPPONENT;
			break;
		case 'l':
			flags |= OPTION_IS_SERVER;
		case 'c':
			node = malloc(strlen(optarg) + 1);
			if (!node)
				goto err_malloc;

			strcpy(node, optarg);
			break;
		case 'p':
			port = malloc(strlen(optarg) + 1);
			if (!port)
				goto err_malloc;

			strcpy(port, optarg);
			break;
		case 's':
			if (strcmp(optarg, "white") == 0) {
				color = COLOR_WHITE;
			} else if (strcmp(optarg, "black") == 0) {
				color = COLOR_BLACK;
			} else {
				goto err_invalid_arg;
			}
			break;
		case 't': {
			long t, i;
			int err = parse_gametime(optarg, &t, &i);
			if (err)
				goto err_invalid_arg;
			if (t > GAMETIME_MAX)
				goto err_invalid_arg;
			if (i > MINUTE)
				goto err_invalid_arg;

			gametime = t;
			moveinc = i;
			break;
		}
		case '?':
			goto err_invalid_opt;
		case ':':
			goto err_missing_arg;
		}
	} 

	/* check options */
	if (!port) {
		fprintf(stderr, "missing option '-p'\n");
		free(node);
		free(port);
		exit(1);
	}

	/* set options */
	if (color != -1) {
		options.color = color;
	} else if (flags & OPTION_NO_OPPONENT) {
		options.color = COLOR_WHITE;
	} else {
		unsigned int r;
		if (getrandom(&r, sizeof(r), GRND_RANDOM) == -1) {
			SYSERR();
			exit(-1);
		}
		options.color = r % 2;
	}
	options.node = node;
	options.port = port;
	options.gametime = gametime;
	options.moveinc = moveinc;
	options.flags = flags;
	return;

err_malloc:
	SYSERR();
	free(node);
	free(port);
	exit(-1);
err_invalid_opt:
	fprintf(stderr, "invalid option '-%c'\n", optopt);
	free(node);
	free(port);
	exit(1);
err_missing_arg:
	fprintf(stderr, "missing argument for option '-%c'\n", optopt);
	free(node);
	free(port);
	exit(1);
err_invalid_arg:
	fprintf(stderr, "invalid argument '%s' for option '-%c'\n", optarg, c);
	free(node);
	free(port);
	exit(1);
}
static void free_options()
{
	free(options.node);
	free(options.port);
}

static void setup(void)
{
	int ret;

	if (options.flags & OPTION_IS_SERVER) {
		printf("server\n");
		init_communication_server(options.node, options.port,
				options.color, options.gametime);
	} else {
		printf("client\n");
		init_communication_client(options.node, options.port);
	}

	/* setup X */
	dpy = XOpenDisplay(NULL);
	if (!dpy) {
		fprintf(stderr, "could not open X display\n");
		ret = 1;
		goto cleanup_err_x;
	}

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

	Atom atoms[ATOM_COUNT];
	atoms[ATOM_DELETE_WINDOW] = XInternAtom(dpy, "WM_DELETE_WINDOW", False);
	atoms[ATOM_NAME] = XInternAtom(dpy, "_NET_WM_NAME", False);

	XSetWMProtocols(dpy, winmain, &atoms[ATOM_DELETE_WINDOW], 1);

	/* setup handlers */
	hctx = malloc(sizeof(*hctx));
	if (!hctx) {
		SYSERR();
		ret = -1;
		goto cleanup_err_threads;
	}
	memset(hctx, 0, sizeof(*hctx));

	pthread_mutex_init(&hctx->gamelock, NULL);
	pthread_mutex_init(&hctx->xlock, NULL);
	pthread_mutex_init(&hctx->gfxhlock, NULL);
	pthread_mutex_init(&hctx->mainlock, NULL);

	struct gfxh_args_t *gfxhargs = malloc(sizeof(*gfxhargs));
	if (!gfxhargs) {
		SYSERR();
		free(hctx);
		goto cleanup_err_threads;
	}
	gfxhargs->hctx = hctx;
	gfxhargs->dpy = dpy;
	gfxhargs->winmain = winmain;
	gfxhargs->vis = vis;
	memcpy(gfxhargs->atoms, atoms, sizeof(atoms));
	if (start_handler(gfxhargs, 0, gfxh_main, &hctx->gfxh)) {
		fprintf(stderr, "error while starting graphics handler thread");
		free(gfxhargs);
		free(hctx);
		goto cleanup_err_threads;
	}

	struct audioh_args_t *audiohargs = malloc(sizeof(*audiohargs));
	if (!audiohargs) {
		SYSERR();
		if (stop_handler(&hctx->gfxh))
			fprintf(stderr, "error while terminating graphics handler thread\n");
		free(hctx);
		goto cleanup_err_threads;
	}
	audiohargs->hctx = hctx;
	if (start_handler(audiohargs, 0, audioh_main, &hctx->audioh)) {
		fprintf(stderr, "error while starting audio handler thread");
		if (stop_handler(&hctx->gfxh))
			fprintf(stderr, "error while terminating graphics handler thread\n");
		free(audiohargs);
		free(hctx);
		goto cleanup_err_threads;
	}

	return;

cleanup_err_threads:
	XUnmapWindow(dpy, winmain);
	XDestroyWindow(dpy, winmain);
	XCloseDisplay(dpy);
cleanup_err_x:
	game_terminate();
	exit(ret);
}
static void cleanup(void)
{
	if (stop_handler(&hctx->gfxh))
		fprintf(stderr, "error while terminating graphics handler thread\n");
	if (stop_handler(&hctx->audioh))
		fprintf(stderr, "error while terminating audio handler thread\n");

	pthread_mutex_destroy(&hctx->mainlock);
	pthread_mutex_destroy(&hctx->gfxhlock);
	pthread_mutex_destroy(&hctx->xlock);
	pthread_mutex_destroy(&hctx->gamelock);
	free(hctx);

	XUnmapWindow(dpy, winmain);
	XDestroyWindow(dpy, winmain);
	XCloseDisplay(dpy);

	free_options();
}
static void run(void)
{
	XEvent e;
	int term = 0;
	while (!term) {
		pthread_mutex_lock(&hctx->mainlock);
		term = hctx->terminate;
		pthread_mutex_unlock(&hctx->mainlock);


		/* Prevent blocking of XNextEvent inside mutex lock */
		pthread_mutex_lock(&hctx->xlock);
		int n = XPending(dpy);
		pthread_mutex_unlock(&hctx->xlock);
		if (n == 0) {
			usleep(WINEVENT_RESPONSE_TIME);
			continue;
		}

		pthread_mutex_lock(&hctx->xlock);
		XNextEvent(dpy, &e);
		pthread_mutex_unlock(&hctx->xlock);

		switch (e.type) {
		case ClientMessage:
			on_client_message(&e.xclient);
			break;
		case ConfigureNotify:
			on_configure(&e.xconfigure);
			break;
		case ButtonPress:
			on_button_press(&e.xbutton);
			break;
		case ButtonRelease:
			on_button_release(&e.xbutton);
			break;
		case KeyPress:
			on_keypress(&e.xkey);
			break;
		}
	}
}
int main(int argc, char *argv[])
{
	parse_options(argc, argv);

	struct sigaction sa;
	sa.sa_handler = SIG_IGN;
	sigemptyset(&sa.sa_mask);
	sa.sa_flags = 0;
	sigaction(SIGPIPE, &sa, NULL);

	setup();
	run();
	cleanup();
	return 0;
}
