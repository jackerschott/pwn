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

#ifndef CHESS_H
#define CHESS_H

#include <stdio.h>
#include <stdlib.h>
#include <errno.h>

#include <X11/Xdefs.h>
#include <cairo/cairo-xlib.h>

/* generic macros */
#define ARRSIZE(x) (sizeof(x) / sizeof((x)[0]))
#define BUG() do { \
	fprintf(stderr, "BUG: failure at %s:%d/%s()!\n", __FILE__, __LINE__, __func__); \
	exit(EXIT_FAILURE); \
} while (0);
#define SYSERR() do { \
	fprintf(stderr, "%s:%d/%s: ", __FILE__, __LINE__, __func__); \
	fprintf(stderr, "%i ", errno); \
	perror(NULL); \
} while (0);
#define GAIERR(err) do { \
	fprintf(stderr, "%s:%d/%s: ", __FILE__, __LINE__, __func__); \
	fprintf(stderr, "%s", gai_strerror((err))); \
} while (0);
#if 0
#define SYSERR() do { \
	fprintf(stderr, "%s: ", __func__); \
	perror(NULL); \
} while (0);
#endif

/* definitions */
/* game */
#define NF 8
#define NUM_PIECES 6
#define NUM_COLORS 2

#define PIECE_NONE 	0
#define PIECE_KING 	2
#define PIECE_QUEEN 	4
#define PIECE_ROOK 	6
#define PIECE_BISHOP 	8
#define PIECE_KNIGHT 	10
#define PIECE_PAWN 	12

#define COLOR_WHITE 	0
#define COLOR_BLACK 	1

#define PIECEMASK 0b1110
#define COLORMASK 0b0001

#define OPP_COLOR(c) ((c) ^ COLORMASK)

typedef char fid;
typedef char piece_t;
typedef char color_t;
typedef char fieldinfo_t;

#endif /* CHESS_H */
