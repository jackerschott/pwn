#include <stdio.h>
#include <stdlib.h>

#include <X11/Xlib.h>
#include <cairo/cairo-xlib.h>

#include "draw.h"
#include "game.h"

Display *dpy;
Window winmain;
cairo_surface_t *surface;

#define FIGURE(p) ((p) / 2 - 1)
#define PALETTE(c) (c)
#define XTOI(x) ((int)(((x) - board_domain.xorig) / (board_domain.size / NF)))
#define YTOJ(y) (NF - (int)(((y) - board_domain.yorig) / (board_domain.size / NF)) - 1)
#define ITOX(i) (board_domain.xorig + (i) * board_domain.fieldsize)
#define JTOY(j) (board_domain.yorig + (NF - (j) - 1) * board_domain.fieldsize)

const int player_color = COLOR_WHITE;

struct {
	double xorig, yorig;
	double size;
	double fieldsize;
} board_domain;

int itouched = -1;
int jtouched = -1;

static void select_field(int i, int j)
{
	draw_record();

	double fx = ITOX(i);
	double fy = JTOY(j);
	draw_field(fx, fy, board_domain.fieldsize, (i + j) % 2, 1);

	int piece = game_get_piece(i, j);
	if (piece) {
		int color = game_get_color(i, j);
		draw_piece(fx, fy, board_domain.fieldsize, FIGURE(piece), PALETTE(color));
	}

	draw_commit();

	itouched = i;
	jtouched = j;
}
static void unselect_field(int i, int j)
{
	draw_record();

	double fx = ITOX(i);
	double fy = JTOY(j);
	draw_field(fx, fy, board_domain.fieldsize, (i + j) % 2, 0);

	int piece = game_get_piece(i, j);
	if (piece) {
		int color = game_get_color(i, j);
		draw_piece(fx, fy, board_domain.fieldsize, FIGURE(piece), PALETTE(color));
	}

	draw_commit();

	itouched = -1;
	jtouched = -1;
}
static void show_move(int ifrom, int jfrom, int ito, int jto, int updates[][2])
{
	double fx, fy;

	draw_record();
	for (int k = 0; k < NUM_UPDATES_MAX && updates[k][0] != -1; ++k) {
		int i = updates[k][0];
		int j = updates[k][1];

		double fx = ITOX(i);
		double fy = JTOY(j);
		draw_field(fx, fy, board_domain.fieldsize, (i + j) % 2, 0);

		int piece = game_get_piece(i, j);
		if (piece & PIECEMASK) {
			int color = game_get_color(i, j);
			draw_piece(fx, fy, board_domain.fieldsize, FIGURE(piece), PALETTE(color));
		}
	}
	draw_commit();
}

static void move(int ifrom, int jfrom, int ito, int jto)
{
	if (game_move(ifrom, jfrom, ito, jto))
		return;

	int updates[NUM_UPDATES_MAX][2];
	game_get_updates(updates);
	show_move(ifrom, jfrom, ito, jto, updates);

	if (game_is_checkmate()) {
		printf("checkmate!\n");
	}
	if (game_is_stalemate()) {
		printf("stalemate!\n");
	}
}

void on_configure(XConfigureEvent *e)
{
	cairo_xlib_surface_set_size(surface, e->width, e->height);

	int d = (e->width - e->height) / 2;
	if (d > 0) {
		board_domain.xorig = d;
		board_domain.yorig = 0;
		board_domain.size = e->height;
		board_domain.fieldsize = e->height / NF;
	} else {
		board_domain.xorig = d;
		board_domain.yorig = 0;
		board_domain.size = e->height;
		board_domain.fieldsize = e->height / NF;
	}

	draw_record();
	for (int j = 0; j < NF; ++j) {
		for (int i = 0; i < NF; ++i) {
			double fx = ITOX(i);
			double fy = JTOY(j);
			draw_field(fx, fy, board_domain.fieldsize, (i + j) % 2, 0);

			int piece = game_get_piece(i, j);
			if (piece & PIECEMASK) {
				int color = game_get_color(i, j);
				draw_piece(fx, fy, board_domain.fieldsize,
						FIGURE(piece), PALETTE(color));
			}
		}
	}
	draw_commit();
}

void on_button_press(XButtonEvent *e)
{
	int i = XTOI(e->x);
	int j = YTOJ(e->y);
	if (itouched != -1 && jtouched != -1) {
		move(itouched, jtouched, i, j);
		unselect_field(i, j);
	} else if (game_is_movable_piece(i, j)) {
		select_field(i, j);
	}
}

void on_button_release(XButtonEvent *e)
{
	if (itouched == -1 && jtouched == -1)
		return;

	int i = XTOI(e->x);
	int j = YTOJ(e->y);
	if (itouched != i || jtouched != j) {
		move(itouched, jtouched, i, j);
		unselect_field(itouched, jtouched);
	}
}

void setup(void)
{
	int scr = DefaultScreen(dpy);
	Window root = RootWindow(dpy, scr);
	Visual *vis = DefaultVisual(dpy, DefaultScreen(dpy));

	unsigned int winwidth = 800;
	unsigned int winheight = 800;
	winmain = XCreateSimpleWindow(dpy, root, 0, 0, winwidth, winheight, 0, 0, 0);

	long mask = ExposureMask | KeyPressMask | PointerMotionMask
		| ButtonPressMask | ButtonReleaseMask | StructureNotifyMask;
	XSelectInput(dpy, winmain, mask);
	XMapWindow(dpy, winmain);

	surface = cairo_xlib_surface_create(dpy, winmain, vis, winwidth, winheight);
	draw_init_context(surface);

	board_domain.xorig = 0;
	board_domain.yorig = 0;
	board_domain.size = winwidth;

	game_init_board(player_color);
}

void cleanup(void)
{
	draw_destroy_context();
	cairo_surface_destroy(surface);
	XUnmapWindow(dpy, winmain);
	XDestroyWindow(dpy, winmain);
	XCloseDisplay(dpy);
}

void run(void)
{
	int quit = 0;
	XEvent ev;
	while (!quit) {
		XNextEvent(dpy, &ev);
		if (ev.type == ConfigureNotify) {
			on_configure(&ev.xconfigure);
		} else if (ev.type == ButtonPress) {
			on_button_press(&ev.xbutton);
		} else if (ev.type == ButtonRelease) {
			on_button_release(&ev.xbutton);
		}
	}
}

int main(int argc, char *argv[])
{
	dpy = XOpenDisplay(NULL);
	if (!dpy) {
		fprintf(stderr, "could not open X display");
		exit(EXIT_FAILURE);
	}

	setup();
	run();
	cleanup();
	return 0;
}
