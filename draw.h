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

#define PALETTE_LIGHT 	0
#define PALETTE_DARK 	1

#define FIGURE_KING 	0
#define FIGURE_QUEEN 	1
#define FIGURE_ROOK 	2
#define FIGURE_BISHOP 	3
#define FIGURE_KNIGHT 	4
#define FIGURE_PAWN 	5

#include <cairo/cairo-xlib.h>

void draw_init_context(cairo_surface_t *surface);
void draw_destroy_context();

void draw_record();
void draw_commit();

void draw_clear();
void draw_field(double x, double y, double size, int palette, int selected);
void draw_piece(double x, double y, double size, int figure, int palette);

#endif /* DRAW_H */
