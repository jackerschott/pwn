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

#ifndef DRAW_H
#define DRAW_H

#include "pwn.h"
#include "game.h"

enum shade_t {
	SHADE_LIGHT = 0,
	SHADE_DARK = 1,
};

enum square_highlight_t {
	SQUARE_HIGHLIGHT_UNSELECTED = 0,
	SQUARE_HIGHLIGHT_SELECTED = 1,
	SQUARE_HIGHLIGHT_MOVE_INVOLVED = 2,
};

typedef enum shade_t shade_t;
typedef enum square_highlight_t square_highlight_t;

#include <cairo/cairo-xlib.h>

void draw_init_context(cairo_surface_t *surface);
void draw_destroy_context();

void draw_record();
void draw_commit();

void draw_clear();
void draw_square(double x, double y, double size, shade_t shade, square_highlight_t highlight);
void draw_piece(double x, double y, double size, piece_t piece, shade_t shade);

#endif /* DRAW_H */
