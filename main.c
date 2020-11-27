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

#include "chess.h"
#include "config.h"
#include "game.h"
#include "comh.h"
#include "gfxh.h"

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
static Display *dpy;
static Window winmain;
enum { WMDeleteWindow, WMCount };
static Atom atoms[WMCount];

static struct handler_context_t *hctx;

static void cleanup(void);

static void start_communicationhandler(int fopp)
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

	memcpy(hctx->comh.pevent, pevent, 2 * sizeof(int));
	memcpy(hctx->comh.pconfirm, pconfirm, 2 * sizeof(int));
	hctx->comh.state = COMH_IS_EXCHANGING;

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

	struct comh_args_t *args = malloc(sizeof(*args));
	if (!args) {
		SYSERR();
		if ((errno = pthread_attr_destroy(&attr)))
			fprintf(stderr, "%s: error while destroying movehandler thread attributes\n", __func__);
		goto err_close_pipes;
	}
	args->hctx = hctx;
	args->fopp = fopp;
	args->gamecolor = options.color;
	if ((errno = pthread_create(&hctx->comh.id, &attr, comh_main, args))) {
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
	close(hctx->comh.pevent[0]);
	close(hctx->comh.pevent[1]);
	close(hctx->comh.pconfirm[0]);
	close(hctx->comh.pconfirm[1]);
	cleanup();
}
static void stop_communicationhandler(void)
{
	printf("stop communicationhandler\n");
	union event_t event;
	event.term.type = EVENT_TERM;
	if (hwrite(hctx->comh.pevent[1], &event, sizeof(event)) == -1)
		fprintf(stderr, "%s: error while terminating communicationhandler thread", __func__);

	if ((errno = pthread_join(hctx->comh.id, NULL)))
		fprintf(stderr, "%s: error while joining communicationhandler thread\n", __func__);

	if (close(hctx->comh.pevent[0]) == -1 || close(hctx->comh.pevent[1])
			|| close(hctx->comh.pconfirm[0]) || close(hctx->comh.pconfirm[1]))
		fprintf(stderr, "%s: error while closing event pipe, potential data loss\n", __func__);
}
static void start_graphicshandler(Display *d, Window win, Visual *vis)
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
		
	memcpy(hctx->gfxh.pevent, pevent, 2 * sizeof(int));
	memcpy(hctx->gfxh.pconfirm, pconfirm, 2 * sizeof(int));

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

	struct gfxh_args_t *args = malloc(sizeof(*args));
	if (!args) {
		SYSERR();
		if ((errno = pthread_attr_destroy(&attr)))
			fprintf(stderr, "%s: error while destroying movehandler thread attributes\n", __func__);
		goto err_close_pipes;
	}
	args->hctx = hctx;
	args->dpy = d;
	args->winmain = win;
	args->vis = vis;
	args->gamecolor = options.color;
	if ((errno = pthread_create(&hctx->gfxh.id, &attr, gfxh_main, args))) {
		SYSERR();
		free(args);
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
	close(hctx->gfxh.pevent[0]);
	close(hctx->gfxh.pevent[1]);
	close(hctx->gfxh.pconfirm[0]);
	close(hctx->gfxh.pconfirm[1]);
	cleanup();
	exit(-1);
}
static void stop_graphicshandler(void)
{
	printf("stop graphicshandler\n");
	union event_t event;
	event.term.type = EVENT_TERM;
	if (hwrite(hctx->gfxh.pevent[1], &event, sizeof(event)) == -1)
		fprintf(stderr, "%s: error while terminating communicationhandler thread", __func__);

	if ((errno = pthread_join(hctx->gfxh.id, NULL)))
		fprintf(stderr, "%s: error while joining communicationhandler thread\n", __func__);

	if (close(hctx->gfxh.pevent[0]) == -1 || close(hctx->gfxh.pevent[1])
			|| close(hctx->gfxh.pconfirm[0]) || close(hctx->gfxh.pconfirm[1]))
		fprintf(stderr, "%s: error while closing event pipe, potential data loss\n", __func__);
}

static void on_configure(XConfigureEvent *e)
{
	pthread_mutex_lock(&hctx->gfxhlock);
	int isdrawing = hctx->gfxh.state & GFXH_IS_DRAWING;
	pthread_mutex_unlock(&hctx->gfxhlock);
	if (isdrawing)
		return;

	union event_t event = {0};
	event.redraw.type = EVENT_REDRAW;
	event.redraw.width = e->width;
	event.redraw.height = e->height;
	if (hwrite(hctx->gfxh.pevent[1], &event, sizeof(event)) == -1) {
		SYSERR();
		cleanup();
		exit(-1);
	}

	int res;
	if (hread(hctx->gfxh.pconfirm[0], &res, sizeof(res)) == -1) {
		SYSERR();
		cleanup();
		exit(-1);
	}
}
static void on_client_message(XClientMessageEvent *e)
{
	pthread_mutex_lock(&hctx->mainlock);
	if ((Atom)e->data.l[0] == atoms[WMDeleteWindow])
		hctx->terminate = 1;
	pthread_mutex_unlock(&hctx->mainlock);
}
static void on_button_press(XButtonEvent *e)
{
	union event_t event;
	event.touch.type = EVENT_TOUCH;
	event.touch.x = e->x;
	event.touch.y = e->y;
	if (hwrite(hctx->gfxh.pevent[1], &event, sizeof(event)) == -1) {
		SYSERR();
		cleanup();
		exit(-1);
	}
}
static void on_button_release(XButtonEvent *e)
{
	union event_t event;
	event.touch.type = EVENT_TOUCH;
	event.touch.x = e->x;
	event.touch.y = e->y;
	event.touch.flags = TOUCH_RELEASE;
	if (hwrite(hctx->gfxh.pevent[1], &event, sizeof(event)) == -1) {
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

	hctx = malloc(sizeof(*hctx));
	if (!hctx) {
		SYSERR();
		close(fopp);
		exit(-1);
	}
	memset(hctx, 0, sizeof(*hctx));

	pthread_mutex_init(&hctx->gamelock, NULL);
	pthread_mutex_init(&hctx->xlock, NULL);
	pthread_mutex_init(&hctx->gfxhlock, NULL);
	pthread_mutex_init(&hctx->comhlock, NULL);
	pthread_mutex_init(&hctx->mainlock, NULL);

	start_communicationhandler(fopp);
	start_graphicshandler(dpy, winmain, vis);
}
static void cleanup(void)
{
	if (hctx->gfxh.id)
		stop_graphicshandler();
	if (hctx->comh.id)
		stop_communicationhandler();

	pthread_mutex_destroy(&hctx->mainlock);
	pthread_mutex_destroy(&hctx->comhlock);
	pthread_mutex_destroy(&hctx->gfxhlock);
	pthread_mutex_destroy(&hctx->xlock);
	pthread_mutex_destroy(&hctx->gamelock);
	free(hctx);

	XUnmapWindow(dpy, winmain);
	XDestroyWindow(dpy, winmain);
	XCloseDisplay(dpy);
	game_terminate();

}
static void run(void)
{
	XEvent ev;
	int term = 0;
	while (!term) {
		/* Prevent blocking of XNextEvent inside mutex lock */
		pthread_mutex_lock(&hctx->xlock);
		int n = XPending(dpy);
		pthread_mutex_unlock(&hctx->xlock);
		if (n == 0)
			continue;

		pthread_mutex_lock(&hctx->xlock);
		XNextEvent(dpy, &ev);
		pthread_mutex_unlock(&hctx->xlock);

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

		pthread_mutex_lock(&hctx->mainlock);
		term = hctx->terminate;
		pthread_mutex_unlock(&hctx->mainlock);
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
