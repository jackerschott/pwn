#ifndef GFXH_H
#define GFXH_H

#include "chess.h"
#include "handler.h"

enum {
	GFXH_IS_DRAWING = 1,
};
struct gfxh_args_t {
	struct handler_context_t *hctx;

	Display *dpy;
	Window winmain;
	Visual *vis;
	color_t gamecolor;
};

void *gfxh_main(void *args);

#endif /* GFHX_H */
