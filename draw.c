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

#include <math.h>
#include "pwn.h"
#include "draw.h"

#define COLOR(r, g, b, a) ((color){ (r) / ((double)0xff), (g) / ((double)0xff), (b) / ((double)0xff), (a) / ((double)0xff) })

#define PIECE_COLOR_FILL 	0
#define PIECE_COLOR_STROKE 	1
#define PIECE_COLOR_OUTLINE 	2

#define FIELD_COLOR_DEFAULT 	0
#define FIELD_COLOR_SELECTED 	1

typedef struct {
	double r, g, b, a;
} color;

color palette_light_piece[] = {
	COLOR(0xff, 0xff, 0xff, 0xff),
	COLOR(0x00, 0x00, 0x00, 0xff),
	COLOR(0x00, 0x00, 0x00, 0xff),
};
color palette_dark_piece[] = {
	COLOR(0x00, 0x00, 0x00, 0xff),
	COLOR(0xff, 0xff, 0xff, 0xff),
	COLOR(0x00, 0x00, 0x00, 0xff),
};
color palette_light_field[] = {
	COLOR(0xff, 0xce, 0x9e, 0xff),
	COLOR(0x80, 0xff, 0x00, 0x80),
};
color palette_dark_field[] = {
	COLOR(0xd1, 0x8b, 0x47, 0xff),
	COLOR(0x80, 0xff, 0x00, 0x80),
};

cairo_t *cr;

static void draw_king(double x, double y, double size, int palette)
{
	color f, s, o;
	if (palette == PALETTE_LIGHT) {
		f = palette_light_piece[PIECE_COLOR_FILL];
		s = palette_light_piece[PIECE_COLOR_STROKE];
		o = palette_light_piece[PIECE_COLOR_OUTLINE];
	} else {
		f = palette_dark_piece[PIECE_COLOR_FILL];
		s = palette_dark_piece[PIECE_COLOR_STROKE];
		o = palette_dark_piece[PIECE_COLOR_OUTLINE];
	}
	cairo_pattern_t *fill = cairo_pattern_create_rgba(f.r, f.g, f.b, f.a);
	cairo_pattern_t *stroke = cairo_pattern_create_rgba(s.r, s.g, s.b, s.a);
	cairo_pattern_t *outline = cairo_pattern_create_rgba(o.r, o.g, o.b, o.a);

	cairo_translate(cr, x, y);
	cairo_scale(cr, size, size);
	cairo_scale(cr, 1.0 / 45.0, 1.0 / 45.0);

	cairo_set_line_width(cr, 1.5);
	cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);

	cairo_move_to(cr, 22.5, 11.63);
	cairo_line_to(cr, 22.5, 6);
	cairo_set_source(cr, outline);
	cairo_stroke(cr);

	cairo_move_to(cr, 20, 8);
	cairo_line_to(cr, 25, 8);
	cairo_set_source(cr, outline);
	cairo_stroke(cr);

	cairo_move_to(cr, 22.5, 25);
	cairo_curve_to(cr, 22.5, 25, 27, 17.5, 25.5, 14.5);
	cairo_curve_to(cr, 25.5, 14.5, 24.5, 12, 22.5, 12);
	cairo_curve_to(cr, 20.5, 12, 19.5, 14.5, 19.5, 14.5);
	cairo_curve_to(cr, 18, 17.5, 22.5, 25, 22.5, 25);
	cairo_set_source(cr, fill);
	cairo_fill_preserve(cr);
	cairo_set_source(cr, outline);
	cairo_stroke(cr);

	cairo_move_to(cr, 11.5, 37);
	cairo_curve_to(cr, 17, 40.5, 27, 40.5, 32.5, 37);
	cairo_line_to(cr, 32.5, 30);
	cairo_curve_to(cr, 32.5, 30, 41.5, 25.5, 38.5, 19.5);
	cairo_curve_to(cr, 34.5, 13, 25, 16, 22.5, 23.5);
	cairo_line_to(cr, 22.5, 27);
	cairo_line_to(cr, 22.5, 23.5);
	cairo_curve_to(cr, 19, 16, 9.5, 13, 6.5, 19.5);
	cairo_curve_to(cr, 3.5, 25.5, 11.5, 29.5, 11.5, 29.5);
	cairo_line_to(cr, 11.5, 37);
	cairo_close_path(cr);
	cairo_set_source(cr, fill);
	cairo_fill_preserve(cr);
	cairo_set_source(cr, outline);
	cairo_stroke(cr);

	cairo_move_to(cr, 11.5, 30);
	cairo_curve_to(cr, 17, 27, 27, 27, 32.5, 30);
	cairo_set_source(cr, stroke);
	cairo_stroke(cr);

	cairo_move_to(cr, 11.5, 33.5);
	cairo_curve_to(cr, 17, 30.5, 27, 30.5, 32.5, 33.5);
	cairo_set_source(cr, stroke);
	cairo_stroke(cr);

	cairo_move_to(cr, 11.5, 37);
	cairo_curve_to(cr, 17, 34, 27, 34, 32.5, 37);
	cairo_set_source(cr, stroke);
	cairo_stroke(cr);

	if (palette == PALETTE_DARK) {
		cairo_move_to(cr, 32, 29.5);
		cairo_curve_to(cr, 32, 29.5, 40.5, 25.5, 38.03, 19.85);
		cairo_curve_to(cr, 34.15, 14, 25, 18, 22.5, 24.5);
		cairo_line_to(cr, 22.51, 26.6);
		cairo_line_to(cr, 22.5, 24.5);
		cairo_curve_to(cr, 20, 18, 9.906, 14, 6.997, 19.85);
		cairo_curve_to(cr, 4.5, 25.5, 11.85, 28.85, 11.85, 28.85);
		cairo_set_source(cr, stroke);
		cairo_stroke(cr);
	}

	cairo_set_line_join(cr, CAIRO_LINE_JOIN_MITER);
	cairo_set_line_cap(cr, CAIRO_LINE_CAP_BUTT);
	cairo_identity_matrix(cr);

	cairo_pattern_destroy(outline);
	cairo_pattern_destroy(stroke);
	cairo_pattern_destroy(fill);
}
static void draw_queen(double x, double y, double size, int palette)
{
	color f, s, o;
	if (palette == PALETTE_LIGHT) {
		f = palette_light_piece[PIECE_COLOR_FILL];
		s = palette_light_piece[PIECE_COLOR_STROKE];
		o = palette_light_piece[PIECE_COLOR_OUTLINE];
	} else {
		f = palette_dark_piece[PIECE_COLOR_FILL];
		s = palette_dark_piece[PIECE_COLOR_STROKE];
		o = palette_dark_piece[PIECE_COLOR_OUTLINE];
	}
	cairo_pattern_t *fill = cairo_pattern_create_rgba(f.r, f.g, f.b, f.a);
	cairo_pattern_t *stroke = cairo_pattern_create_rgba(s.r, s.g, s.b, s.a);
	cairo_pattern_t *outline = cairo_pattern_create_rgba(o.r, o.g, o.b, o.a);

	cairo_translate(cr, x, y);
	cairo_scale(cr, size, size);
	cairo_scale(cr, 1.0 / 45.0, 1.0 / 45.0);

	cairo_set_line_width(cr, 1.5);
	cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);

	cairo_arc(cr, 6.0, 12.0, 2.0, 0.0, 2.0 * M_PI);
	cairo_set_source(cr, fill);
	cairo_fill_preserve(cr);
	cairo_set_source(cr, outline);
	cairo_stroke(cr);

	cairo_arc(cr, 22.5, 7.5, 2.0, 0.0, 2.0 * M_PI);
	cairo_set_source(cr, fill);
	cairo_fill_preserve(cr);
	cairo_set_source(cr, outline);
	cairo_stroke(cr);

	cairo_arc(cr, 39.0, 12.0, 2.0, 0.0, 2.0 * M_PI);
	cairo_set_source(cr, fill);
	cairo_fill_preserve(cr);
	cairo_set_source(cr, outline);
	cairo_stroke(cr);

	cairo_arc(cr, 14.0, 8.5, 2.0, 0.0, 2.0 * M_PI);
	cairo_set_source(cr, fill);
	cairo_fill_preserve(cr);
	cairo_set_source(cr, outline);
	cairo_stroke(cr);

	cairo_arc(cr, 31.0, 8.5, 2.0, 0.0, 2.0 * M_PI);
	cairo_set_source(cr, fill);
	cairo_fill_preserve(cr);
	cairo_set_source(cr, outline);
	cairo_stroke(cr);

	cairo_set_line_join(cr, CAIRO_LINE_JOIN_ROUND);
	cairo_move_to(cr, 9, 26);
	cairo_curve_to(cr, 17.5, 24.5, 30, 24.5, 36, 26);
	cairo_line_to(cr, 38, 14);
	cairo_line_to(cr, 31, 25);
	cairo_line_to(cr, 31, 11);
	cairo_line_to(cr, 25.5, 24.5);
	cairo_line_to(cr, 22.5, 9.5);
	cairo_line_to(cr, 19.5, 24.5);
	cairo_line_to(cr, 14, 10.5);
	cairo_line_to(cr, 14, 25);
	cairo_line_to(cr, 7, 14);
	cairo_line_to(cr, 9, 26);
	cairo_close_path(cr);
	cairo_set_source(cr, fill);
	cairo_fill_preserve(cr);
	cairo_set_source(cr, outline);
	cairo_stroke(cr);

	cairo_move_to(cr, 9, 26);
	cairo_curve_to(cr, 9, 28, 10.5, 28, 11.5, 30);
	cairo_curve_to(cr, 12.5, 31.5, 12.5, 31, 12, 33.5);
	cairo_curve_to(cr, 10.5, 34.5, 10.5, 36, 10.5, 36);
	cairo_curve_to(cr, 9, 37.5, 11, 38.5, 11, 38.5);
	if (palette == PALETTE_LIGHT) {
		cairo_curve_to(cr, 17.5, 39.5, 27.5, 39.5, 34, 38.5);
	} else {
		cairo_curve_to(cr, 17.5, 41.0, 27.5, 41.0, 34, 38.5);
	}
	cairo_curve_to(cr, 34, 38.5, 35.5, 37.5, 34, 36);
	cairo_curve_to(cr, 34, 36, 34.5, 34.5, 33, 33.5);
	cairo_curve_to(cr, 32.5, 31, 32.5, 31.5, 33.5, 30);
	cairo_curve_to(cr, 34.5, 28, 36, 28, 36, 26);
	cairo_curve_to(cr, 27.5, 24.5, 17.5, 24.5, 9, 26);
	cairo_close_path(cr);
	cairo_set_source(cr, fill);
	cairo_fill_preserve(cr);
	cairo_set_source(cr, outline);
	cairo_stroke(cr);

	if (palette == PALETTE_LIGHT) {
		cairo_move_to(cr, 11.5, 30);
		cairo_curve_to(cr, 15, 29, 30, 29, 33.5, 30);
		cairo_set_source(cr, stroke);
		cairo_stroke(cr);

		cairo_move_to(cr, 12, 33.5);
		cairo_curve_to(cr, 18, 32.5, 27, 32.5, 33, 33.5);
		cairo_set_source(cr, stroke);
		cairo_stroke(cr);
	} else {
		cairo_move_to(cr, 11, 29);
		cairo_curve_to(cr, 18.4, 26.4, 26.4, 26.4, 34, 29);
		cairo_set_source(cr, stroke);
		cairo_stroke(cr);

		cairo_move_to(cr, 12.5, 31.5);
		cairo_line_to(cr, 32.5, 31.5);
		cairo_set_source(cr, stroke);
		cairo_stroke(cr);

		cairo_move_to(cr, 11.5, 34.5);
		cairo_curve_to(cr, 18.6, 36.9, 26.4, 36.9, 33.5, 34.5);
		cairo_set_source(cr, stroke);
		cairo_stroke(cr);

		cairo_move_to(cr, 10.5, 37.5);
		cairo_curve_to(cr, 18.3, 40.3, 26.8, 40.3, 34.5, 37.5);
		cairo_set_source(cr, stroke);
		cairo_stroke(cr);
	}

	cairo_set_line_join(cr, CAIRO_LINE_JOIN_MITER);
	cairo_set_line_cap(cr, CAIRO_LINE_CAP_BUTT);
	cairo_identity_matrix(cr);

	cairo_pattern_destroy(outline);
	cairo_pattern_destroy(stroke);
	cairo_pattern_destroy(fill);
}
static void draw_rook(double x, double y, double size, int palette)
{
	color f, s, o;
	if (palette == PALETTE_LIGHT) {
		f = palette_light_piece[PIECE_COLOR_FILL];
		s = palette_light_piece[PIECE_COLOR_STROKE];
		o = palette_light_piece[PIECE_COLOR_OUTLINE];
	} else {
		f = palette_dark_piece[PIECE_COLOR_FILL];
		s = palette_dark_piece[PIECE_COLOR_STROKE];
		o = palette_dark_piece[PIECE_COLOR_OUTLINE];
	}
	cairo_pattern_t *fill = cairo_pattern_create_rgba(f.r, f.g, f.b, f.a);
	cairo_pattern_t *stroke = cairo_pattern_create_rgba(s.r, s.g, s.b, s.a);
	cairo_pattern_t *outline = cairo_pattern_create_rgba(o.r, o.g, o.b, o.a);

	cairo_translate(cr, x, y);
	cairo_scale(cr, size, size);
	cairo_scale(cr, 1.0 / 45.0, 1.0 / 45.0);

	cairo_set_line_width(cr, 1.5);
	cairo_set_line_cap(cr, CAIRO_LINE_CAP_SQUARE);

	cairo_move_to(cr, 9.0, 39.0);
	cairo_line_to(cr, 36.0, 39.0);
	cairo_line_to(cr, 36.0, 36.0);
	cairo_line_to(cr, 9.0, 36.0);
	cairo_line_to(cr, 9.0, 39.0);
	cairo_set_source(cr, fill);
	cairo_fill_preserve(cr);
	cairo_set_source(cr, outline);
	cairo_stroke(cr);

	cairo_move_to(cr, 12.0, 36.0);
	cairo_line_to(cr, 12.0, 32.0);
	cairo_line_to(cr, 33.0, 32.0);
	cairo_line_to(cr, 33.0, 36.0);
	cairo_line_to(cr, 12.0, 36.0);
	cairo_set_source(cr, fill);
	cairo_fill_preserve(cr);
	cairo_set_source(cr, outline);
	cairo_stroke(cr);

	cairo_line_to(cr, 11.0, 14.0);
	cairo_line_to(cr, 11.0, 9.0);
	cairo_line_to(cr, 15.0, 9.0);
	cairo_line_to(cr, 15.0, 11.0);
	cairo_line_to(cr, 20.0, 11.0);
	cairo_line_to(cr, 20.0, 9.0);
	cairo_line_to(cr, 25.0, 9.0);
	cairo_line_to(cr, 25.0, 11.0);
	cairo_line_to(cr, 30.0, 11.0);
	cairo_line_to(cr, 30.0, 9.0);
	cairo_line_to(cr, 34.0, 9.0);
	cairo_line_to(cr, 34.0, 14.0);
	cairo_line_to(cr, 31.0, 17.0);
	cairo_line_to(cr, 14.0, 17.0);
	cairo_close_path(cr);
	cairo_set_source(cr, fill);
	cairo_fill_preserve(cr);
	cairo_set_source(cr, outline);
	cairo_stroke(cr);

	cairo_move_to(cr, 31.0, 17.0);
	cairo_line_to(cr, 31.0, 29.5);
	cairo_line_to(cr, 14.0, 29.5);
	cairo_line_to(cr, 14.0, 17.0);
	cairo_set_source(cr, fill);
	cairo_fill_preserve(cr);
	cairo_set_source(cr, outline);
	cairo_stroke(cr);

	cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
	cairo_set_line_join(cr, CAIRO_LINE_JOIN_ROUND);
	cairo_move_to(cr, 31.0, 29.5);
	cairo_line_to(cr, 32.5, 32.0);
	cairo_line_to(cr, 12.5, 32.0);
	cairo_line_to(cr, 14.0, 29.5);
	cairo_set_source(cr, fill);
	cairo_fill_preserve(cr);
	cairo_set_source(cr, outline);
	cairo_stroke(cr);

	cairo_move_to(cr, 11.0, 14.0);
	cairo_line_to(cr, 34.0, 14.0);
	cairo_set_source(cr, outline);
	cairo_stroke(cr);

	if (palette == PALETTE_DARK) {
		cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);

		cairo_move_to(cr, 12.0, 35.5);
		cairo_line_to(cr, 33.0, 35.5);
		cairo_line_to(cr, 33.0, 35.5);
		cairo_set_source(cr, stroke);
		cairo_stroke(cr);

		cairo_move_to(cr, 13.0, 31.5);
		cairo_line_to(cr, 32.0, 31.5);
		cairo_set_source(cr, stroke);
		cairo_stroke(cr);

		cairo_move_to(cr, 14.0, 29.5);
		cairo_line_to(cr, 31.0, 29.5);
		cairo_set_source(cr, stroke);
		cairo_stroke(cr);

		cairo_move_to(cr, 14.0, 16.5);
		cairo_line_to(cr, 31.0, 16.5);
		cairo_set_source(cr, stroke);
		cairo_stroke(cr);

		cairo_move_to(cr, 11.0, 14.0);
		cairo_line_to(cr, 34.0, 14.0);
		cairo_set_source(cr, stroke);
		cairo_stroke(cr);
	}

	cairo_set_line_join(cr, CAIRO_LINE_JOIN_MITER);
	cairo_set_line_cap(cr, CAIRO_LINE_CAP_BUTT);
	cairo_identity_matrix(cr);

	cairo_pattern_destroy(outline);
	cairo_pattern_destroy(stroke);
	cairo_pattern_destroy(fill);
}
static void draw_bishop(double x, double y, double size, int palette)
{
	color f, s, o;
	if (palette == PALETTE_LIGHT) {
		f = palette_light_piece[PIECE_COLOR_FILL];
		s = palette_light_piece[PIECE_COLOR_STROKE];
		o = palette_light_piece[PIECE_COLOR_OUTLINE];
	} else {
		f = palette_dark_piece[PIECE_COLOR_FILL];
		s = palette_dark_piece[PIECE_COLOR_STROKE];
		o = palette_dark_piece[PIECE_COLOR_OUTLINE];
	}
	cairo_pattern_t *fill = cairo_pattern_create_rgba(f.r, f.g, f.b, f.a);
	cairo_pattern_t *stroke = cairo_pattern_create_rgba(s.r, s.g, s.b, s.a);
	cairo_pattern_t *outline = cairo_pattern_create_rgba(o.r, o.g, o.b, o.a);

	cairo_translate(cr, x, y);
	cairo_scale(cr, size, size);
	cairo_scale(cr, 1.0 / 45.0, 1.0 / 45.0);

	cairo_set_line_width(cr, 1.5);
	cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);

	cairo_move_to(cr, 9.0, 36.0);
	cairo_curve_to(cr, 12.39, 35.03, 19.11, 36.43, 22.5, 34.0);
	cairo_curve_to(cr, 25.89, 36.43, 32.61, 35.03, 36.0, 36.0);
	cairo_curve_to(cr, 36.0, 36.0, 37.65, 36.54, 39.0, 38.0);
	cairo_curve_to(cr, 38.32, 38.97, 37.35, 38.99, 36.0, 38.5);
	cairo_curve_to(cr, 32.61, 37.53, 25.89, 38.96, 22.5, 37.5);
	cairo_curve_to(cr, 19.11, 38.96, 12.39, 37.53, 9.0, 38.5);
	cairo_curve_to(cr, 7.65, 38.99, 6.68, 38.97, 6.0, 38.0);
	cairo_curve_to(cr, 7.35, 36.06, 9.0, 36.0, 9.0, 36.0);

	cairo_set_source(cr, fill);
	cairo_fill_preserve(cr);

	cairo_set_source(cr, outline);
	cairo_stroke(cr);

	cairo_move_to(cr, 15.0, 32.0);
	cairo_curve_to(cr, 17.5, 34.5, 27.5, 34.5, 30.0, 32.0);
	cairo_curve_to(cr, 30.5, 30.5, 30.0, 30.0, 30.0, 30.0);
	cairo_curve_to(cr, 30.0, 27.5, 27.5, 26.0, 27.5, 26.0);
	cairo_curve_to(cr, 33.0, 24.5, 33.5, 14.5, 22.5, 10.5);
	cairo_curve_to(cr, 11.5, 14.5, 12.0, 24.5, 17.5, 26.0);
	cairo_curve_to(cr, 17.5, 26.0, 15.0, 27.5, 15.0, 30.0);
	cairo_curve_to(cr, 15.0, 30.0, 14.5, 30.5, 15.0, 32.0);

	cairo_set_source(cr, fill);
	cairo_fill_preserve(cr);

	cairo_set_source(cr, outline);
	cairo_stroke(cr);

	cairo_arc(cr, 22.5, 8.0, 2.5, 0.0, 2.0 * M_PI);

	cairo_set_source(cr, fill);
	cairo_fill_preserve(cr);

	cairo_set_source(cr, outline);
	cairo_stroke(cr);

	cairo_move_to(cr, 17.5, 26.0);
	cairo_line_to(cr, 27.5, 26.0);
	cairo_move_to(cr, 15.0, 30.0);
	cairo_line_to(cr, 30.0, 30.0);
	cairo_move_to(cr, 22.5, 15.5);
	cairo_line_to(cr, 22.5, 20.5);
	cairo_move_to(cr, 20.0, 18.0);
	cairo_line_to(cr, 25.0, 18.0);

	cairo_set_source(cr, stroke);
	cairo_stroke(cr);

	cairo_set_line_cap(cr, CAIRO_LINE_CAP_BUTT);
	cairo_identity_matrix(cr);

	cairo_pattern_destroy(outline);
	cairo_pattern_destroy(stroke);
	cairo_pattern_destroy(fill);
}
static void draw_knight(double x, double y, double size, int palette)
{
	color f, s, o;
	if (palette == PALETTE_LIGHT) {
		f = palette_light_piece[PIECE_COLOR_FILL];
		s = palette_light_piece[PIECE_COLOR_STROKE];
		o = palette_light_piece[PIECE_COLOR_OUTLINE];
	} else {
		f = palette_dark_piece[PIECE_COLOR_FILL];
		s = palette_dark_piece[PIECE_COLOR_STROKE];
		o = palette_dark_piece[PIECE_COLOR_OUTLINE];
	}
	cairo_pattern_t *fill = cairo_pattern_create_rgba(f.r, f.g, f.b, f.a);
	cairo_pattern_t *stroke = cairo_pattern_create_rgba(s.r, s.g, s.b, s.a);
	cairo_pattern_t *outline = cairo_pattern_create_rgba(o.r, o.g, o.b, o.a);

	cairo_translate(cr, x, y);
	cairo_scale(cr, size, size);
	cairo_scale(cr, 1.0 / 45.0, 1.0 / 45.0);

	cairo_set_line_width(cr, 1.5);
	cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);

	cairo_move_to(cr, 22.0, 10.0);
	cairo_curve_to(cr, 32.5, 11.0, 38.5, 18.0, 38.0, 39.0);
	cairo_line_to(cr, 15.0, 39.0);
	cairo_curve_to(cr, 15.0, 30.0, 25.0, 32.5, 23.0, 18.0);
	cairo_set_source(cr, fill);
	cairo_fill_preserve(cr);
	cairo_set_source(cr, outline);
	cairo_stroke(cr);

	cairo_move_to(cr, 24.0, 18.0);
	cairo_curve_to(cr, 24.38, 20.91, 18.45, 25.37, 16.0, 27.0);
	cairo_curve_to(cr, 13.0, 29.0, 13.18, 31.34, 11.0, 31.0);
	cairo_curve_to(cr, 9.96, 30.06, 12.41, 27.96, 11.0, 28.0);
	cairo_curve_to(cr, 10.0, 28.0, 11.19, 29.23, 10.0, 30.0);
	cairo_curve_to(cr, 9.0, 30.0, 6.0, 31.0, 6.0, 26.0);
	cairo_curve_to(cr, 6.0, 24.0, 12.0, 14.0, 12.0, 14.0);
	cairo_curve_to(cr, 12.0, 14.0, 13.89, 12.1, 14.0, 10.5);
	cairo_curve_to(cr, 13.27, 9.51, 13.5, 8.5, 13.5, 7.5);
	cairo_curve_to(cr, 14.5, 6.5, 16.5, 10.0, 16.5, 10.0);
	cairo_line_to(cr, 18.5, 10.0);
	cairo_curve_to(cr, 18.5, 10.0, 19.28, 8.01, 21.0, 7.0);
	cairo_curve_to(cr, 22.0, 7.0, 22.0, 10.0, 22.0, 10.0);
	cairo_set_source(cr, fill);
	cairo_fill_preserve(cr);
	cairo_set_source(cr, outline);
	cairo_stroke(cr);

	cairo_arc(cr, 9.0, 25.5, 0.5, 0, 2 * M_PI);
	cairo_set_source(cr, stroke);
	cairo_fill_preserve(cr);
	cairo_set_source(cr, stroke);
	cairo_stroke(cr);

	if (palette == PALETTE_DARK) {
		cairo_move_to(cr, 24.55, 10.4);
		cairo_line_to(cr, 24.1, 11.85);
		cairo_line_to(cr, 24.6, 12.0);
		cairo_curve_to(cr, 27.75, 13.0, 30.25, 14.49, 32.5, 18.75);
		cairo_curve_to(cr, 34.75, 23.01, 35.75, 29.06, 35.25, 39.0);
		cairo_line_to(cr, 35.2, 39.5);
		cairo_line_to(cr, 37.45, 39.5);
		cairo_line_to(cr, 37.5, 39.0);
		cairo_curve_to(cr, 38.0, 28.94, 36.62, 22.15, 34.25, 17.66);
		cairo_curve_to(cr, 31.88, 13.17, 28.46, 11.02, 25.06, 10.5);
		cairo_line_to(cr, 24.55, 10.4);

		cairo_set_source(cr, stroke);
		cairo_fill(cr);
	}

	cairo_translate(cr, 14.5, 15.5);
	cairo_rotate(cr, M_PI / 6.0);
	cairo_scale(cr, 1.0, 2.0);
	cairo_arc(cr, 0.0, 0.0, 0.5, 0.0, 2 * M_PI);
	cairo_set_source(cr, stroke);
	cairo_stroke(cr);

	cairo_set_line_cap(cr, CAIRO_LINE_CAP_SQUARE);
	cairo_identity_matrix(cr);

	cairo_pattern_destroy(outline);
	cairo_pattern_destroy(stroke);
	cairo_pattern_destroy(fill);
}
static void draw_pawn(double x, double y, double size, int palette)
{
	color f, o;
	if (palette == PALETTE_LIGHT) {
		f = palette_light_piece[PIECE_COLOR_FILL];
		o = palette_light_piece[PIECE_COLOR_OUTLINE];
	} else {
		f = palette_dark_piece[PIECE_COLOR_FILL];
		o = palette_dark_piece[PIECE_COLOR_OUTLINE];
	}
	cairo_pattern_t *fill = cairo_pattern_create_rgba(f.r, f.g, f.b, f.a);
	cairo_pattern_t *outline = cairo_pattern_create_rgba(o.r, o.g, o.b, o.a);

	cairo_translate(cr, x, y);
	cairo_scale(cr, size, size);
	cairo_scale(cr, 1.0 / 45.0, 1.0 / 45.0);

	cairo_set_line_width(cr, 1.5);

	cairo_move_to(cr, 22.0, 9.0);
	cairo_curve_to(cr, 19.79, 9.0, 18.0, 10.79, 18.0, 13.0);
	cairo_curve_to(cr, 18.0, 13.89, 18.29, 14.71, 18.78, 15.38);
	cairo_curve_to(cr, 16.83, 16.5, 15.5, 18.59, 15.5, 21.0);
	cairo_curve_to(cr, 15.5, 23.03, 16.44, 24.84, 17.91, 26.03);
	cairo_curve_to(cr, 14.91, 27.09, 10.5, 31.58, 10.5, 39.5);
	cairo_line_to(cr, 33.5, 39.5);
	cairo_curve_to(cr, 33.5, 31.58, 29.09, 27.09, 26.09, 26.03);
	cairo_curve_to(cr, 27.56, 24.84, 28.5, 23.03, 28.5, 21.0);
	cairo_curve_to(cr, 28.5, 18.59, 27.17, 16.5, 25.22, 15.38);
	cairo_curve_to(cr, 25.71, 14.71, 26.0, 13.89, 26.0, 13.0);
	cairo_curve_to(cr, 26.0, 10.79, 24.21, 9.0, 22.0, 9.0);
	cairo_close_path(cr);
	cairo_set_source(cr, fill);
	cairo_fill_preserve(cr);
	cairo_set_source(cr, outline);
	cairo_stroke(cr);

	cairo_identity_matrix(cr);

	cairo_pattern_destroy(outline);
	cairo_pattern_destroy(fill);
}

void draw_init_context(cairo_surface_t *surface)
{
	cr = cairo_create(surface);
}
void draw_destroy_context()
{
	cairo_destroy(cr);
}

void draw_record()
{
	cairo_push_group(cr);
}
void draw_commit()
{
	cairo_pop_group_to_source(cr);
	cairo_paint(cr);
}

void draw_clear()
{
	cairo_set_source_rgb(cr, 0.0, 0.0, 0.0);
	cairo_paint(cr);
}

void draw_field(double x, double y, double size, int palette, int selected)
{
	color d, s;
	if (palette == PALETTE_LIGHT) {
		d = palette_light_field[FIELD_COLOR_DEFAULT];
		s = palette_light_field[FIELD_COLOR_SELECTED];
	} else {
		d = palette_dark_field[FIELD_COLOR_DEFAULT];
		s = palette_dark_field[FIELD_COLOR_SELECTED];
	}
	cairo_pattern_t *def = cairo_pattern_create_rgba(d.r, d.g, d.b, d.a);
	cairo_pattern_t *sel = cairo_pattern_create_rgba(s.r, s.g, s.b, s.a);

	cairo_set_antialias(cr, CAIRO_ANTIALIAS_NONE);

	cairo_set_source(cr, def);
	cairo_rectangle(cr, x, y, size, size);
	cairo_fill(cr);

	if (selected) {
		cairo_set_source(cr, sel);
		cairo_rectangle(cr, x, y, size, size);
		cairo_fill(cr);
	}

	cairo_set_antialias(cr, CAIRO_ANTIALIAS_DEFAULT);

	cairo_pattern_destroy(def);
	cairo_pattern_destroy(sel);
}
void draw_piece(double x, double y, double size, int figure, int palette)
{
	switch (figure) {
		case FIGURE_KING:
			draw_king(x, y, size, palette);
			break;
		case FIGURE_QUEEN:
			draw_queen(x, y, size, palette);
			break;
		case FIGURE_ROOK:
			draw_rook(x, y, size, palette);
			break;
		case FIGURE_BISHOP:
			draw_bishop(x, y, size, palette);
			break;
		case FIGURE_KNIGHT:
			draw_knight(x, y, size, palette);
			break;
		case FIGURE_PAWN:
			draw_pawn(x, y, size, palette);
			break;
		default:
			BUG();
	}
}
