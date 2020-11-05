#include <stdio.h>
#include <stdlib.h>

#include <X11/Xlib.h>
#include <cairo/cairo-xlib.h>

#include "draw.h"
#include "game.h"

Display *dpy;
Window winmain;
cairo_surface_t *surface;

int board[NUM_ROW_FIELDS][NUM_ROW_FIELDS];

void on_expose(XExposeEvent *e) {
	cairo_xlib_surface_set_size(surface, e->width, e->height);

	draw_record();
	double d = (e->width - e->height) / 2;
	if (d > 0) {
		draw_game(board, d, 0, e->height);
	} else {
		draw_game(board, 0, -d, e->width);
	}
	draw_commit();
}

void on_motion(XMotionEvent *e)
{

}

void on_button_press(XButtonEvent *e)
{

}

void on_button_release(XButtonEvent *e)
{

}

void setup(void)
{
	int scr = DefaultScreen(dpy);
	Window root = RootWindow(dpy, scr);
	Visual *vis = DefaultVisual(dpy, DefaultScreen(dpy));

	unsigned int winwidth = 800;
	unsigned int winheight = 800;
	winmain = XCreateSimpleWindow(dpy, root, 0, 0, winwidth, winheight, 0, 0, 0);
	XSelectInput(dpy, winmain, ExposureMask | KeyPressMask);
	XMapWindow(dpy, winmain);

	surface = cairo_xlib_surface_create(dpy, winmain, vis, winwidth, winheight);
	draw_init_context(surface);

	game_init_board(board, COLOR_WHITE);
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

		if (ev.type == Expose) {
			on_expose(&ev.xexpose);
		} else if (ev.type == MotionNotify) {
			on_motion(&ev.xmotion);
		} else if (ev.type == ButtonPress) {
			on_button_press(&ev.xbutton);
		} else if (ev.type == ButtonRelease) {

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
