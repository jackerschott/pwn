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

#include <arpa/inet.h>
#include <netdb.h>

#include <pthread.h>

#include <X11/Xlib.h>
#include <X11/keysym.h>

#include "pwn.h"
#include "config.h"
#include "game.h"
#include "gfxh.h"

/* options */
enum {
	OPTION_IS_SERVER = 1,
};
struct {
	color_t color;
	char *node;
	long gametime;
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

err_pthread:
	close(h->pevent[0]);
	close(h->pevent[1]);
	close(h->pconfirm[0]);
	close(h->pconfirm[1]);
	return 1;
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
	union event_t event;
	memset(&event, 0, sizeof(event));
	event.clientmessage.type = EVENT_CLIENTMESSAGE;
	memcpy(&event.clientmessage.data, &e->data, sizeof(e->data));
	int n = hwrite(hctx->gfxh.pevent[1], &event, sizeof(event));
	if (n == -1) {
		SYSERR();
		cleanup();
		exit(-1);
	} else if (n == 1) {
		return;
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

	union event_t event;
	memset(&event, 0, sizeof(event));
	event.redraw.type = EVENT_REDRAW;
	event.redraw.width = e->width;
	event.redraw.height = e->height;
	int n = hwrite(hctx->gfxh.pevent[1], &event, sizeof(event));
	if (n == -1) {
		SYSERR();
		cleanup();
		exit(-1);
	} else if (n == 1) {
		return;
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
	union event_t event;
	memset(&event, 0, sizeof(event));
	event.touch.type = EVENT_TOUCH;
	event.touch.x = e->x;
	event.touch.y = e->y;
	event.touch.flags = TOUCH_PRESS;
	int n = hwrite(hctx->gfxh.pevent[1], &event, sizeof(event));
	if (n == -1) {
		SYSERR();
		cleanup();
		exit(-1);
	} else if (n == 1) {
		return;
	}
}
static void on_button_release(XButtonEvent *e)
{
	union event_t event;
	memset(&event, 0, sizeof(event));
	event.touch.type = EVENT_TOUCH;
	event.touch.x = e->x;
	event.touch.y = e->y;
	event.touch.flags = TOUCH_RELEASE;
	int n = hwrite(hctx->gfxh.pevent[1], &event, sizeof(event));
	if (n == -1) {
		SYSERR();
		cleanup();
		exit(-1);
	} else if (n == 1) {
		return;
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
	} else if (ksym == XK_s) {
		pthread_mutex_lock(&hctx->gamelock);
		game_save_board("position");
		pthread_mutex_unlock(&hctx->gamelock);
	} else if (ksym == XK_b) {
		pthread_mutex_lock(&hctx->gamelock);
		print_board();
		pthread_mutex_unlock(&hctx->gamelock);
	}
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

		char *node = malloc(strlen(val) + 1);
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

		char *node = malloc(strlen(val) + 1);
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
	options.gametime = 0;
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
}
static int init_communication(int *fd, int *err)
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

		int val = 1;
		setsockopt(fsock, SOL_SOCKET, SO_REUSEADDR, &val, sizeof(val));
		if (bind(fsock, res->ai_addr, res->ai_addrlen) == -1) {
			close(fsock);
			return -1;
		}
		freeaddrinfo(res);

		struct sockaddr addr;
		if (listen(fsock, 1) == -1) {
			close(fsock);
			return -1;
		}

		fopp = accept(fsock, NULL, NULL);
		if (fopp == -1) {
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
		size_t n = send(fopp, &options.color, sizeof(options.color), 0);
		if (n == -1) {
			close(fopp);
			return -1;
		} else if (n < sizeof(options.color)) {
			BUG();
		}
	} else {
		color_t c;
		size_t n = recv(fopp, &c, sizeof(c), 0);
		if (n == -1) {
			close(fopp);
			return -1;
		} else if (n < sizeof(c)) {
			BUG();
		}
		options.color = OPP_COLOR(c);
	}

	*fd = fopp;
	return 0;
}

static void setup(void)
{
	int ret;

	int gaierr;
	int fopp;
	int err = init_communication(&fopp, &gaierr);
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
	gfxhargs->fopp = fopp;
	gfxhargs->gamecolor = options.color;
	gfxhargs->gametime = options.gametime;
	if (start_handler(gfxhargs, 0, gfxh_main, &hctx->gfxh)) {
		fprintf(stderr, "error while starting graphics handler thread");
		free(gfxhargs);
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

	pthread_mutex_destroy(&hctx->mainlock);
	pthread_mutex_destroy(&hctx->gfxhlock);
	pthread_mutex_destroy(&hctx->xlock);
	pthread_mutex_destroy(&hctx->gamelock);
	free(hctx);

	XUnmapWindow(dpy, winmain);
	XDestroyWindow(dpy, winmain);
	XCloseDisplay(dpy);
}
static void run(void)
{
	XEvent ev;
	int term = 0;
	while (!term) {
		pthread_mutex_lock(&hctx->mainlock);
		term = hctx->terminate;
		pthread_mutex_unlock(&hctx->mainlock);

		/* Prevent blocking of XNextEvent inside mutex lock */
		pthread_mutex_lock(&hctx->xlock);
		int n = XPending(dpy);
		pthread_mutex_unlock(&hctx->xlock);
		if (n == 0)
			continue;

		pthread_mutex_lock(&hctx->xlock);
		XNextEvent(dpy, &ev);
		pthread_mutex_unlock(&hctx->xlock);

		if (ev.type == ClientMessage) {
			on_client_message(&ev.xclient);
		} else if (ev.type == ConfigureNotify) {
			on_configure(&ev.xconfigure);
		} else if (ev.type == ButtonPress) {
			on_button_press(&ev.xbutton);
		} else if (ev.type == ButtonRelease) {
			on_button_release(&ev.xbutton);
		} else if (ev.type == KeyPress) {
			on_keypress(&ev.xkey);
		}
	}
}
int main(int argc, char *argv[])
{
	parse_options(argc, argv);
	options.gametime = 15L * 60L * 1000L * 1000L * 1000L;

	struct sigaction sa;
	sa.sa_handler = SIG_IGN;
	sigemptyset(&sa.sa_mask);
	sa.sa_flags = 0;
	sigaction(SIGPIPE, &sa, NULL);

	//if (argc > 1) {
	//	if (game_load_board(argv[1])) {
	//		perror("game_load_board");
	//		exit(-1);
	//	}
	//} else {
	//	game_init_board(player_color);
	//}

	setup();
	run();
	cleanup();
	return 0;
}
