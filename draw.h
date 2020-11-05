#ifndef DRAW_H
#define DRAW_H

#define SIDE_WHITE 0
#define SIDE_BLACK 1

#include <cairo/cairo-xlib.h>
#include "types.h"

void draw_init_context(cairo_surface_t *surface);
void draw_destroy_context();

void draw_record();
void draw_commit();

void draw_piece(int piece, double x, double y, double size);
void draw_game(int board[NUM_ROW_FIELDS][NUM_ROW_FIELDS], double x, double y, double size);

#endif /* DRAW_H */
