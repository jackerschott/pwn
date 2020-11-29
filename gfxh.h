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

#ifndef GFXH_H
#define GFXH_H

#include "pwn.h"
#include "handler.h"

enum {
	GFXH_IS_DRAWING = 1,
};
struct gfxh_args_t {
	struct handler_context_t *hctx;

	Display *dpy;
	Window winmain;
	Visual *vis;
	int fopp;
	color_t gamecolor;
};

void *gfxh_main(void *args);

#endif /* GFHX_H */
