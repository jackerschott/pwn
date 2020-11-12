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

void draw_field(double x, double y, double size, int palette, int selected);
void draw_piece(double x, double y, double size, int figure, int palette);

#endif /* DRAW_H */
