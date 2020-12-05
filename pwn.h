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
#define MAX(x, y) (((x) > (y)) ? (x) : (y))
#define MIN(x, y) (((x) < (y)) ? (x) : (y))
#define SGN(i) ((i > 0) - (i < 0))

#define ARRNUM(x) (sizeof(x) / sizeof((x)[0]))
#define STRLEN(x) (sizeof(x) - 1)

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

#define OPP_COLOR(c) ((c) ^ COLORMASK)
#define PIECE_IDX(p) ((p) / 2 - 1)
#define PIECE_BY_IDX(i) (2 * (i) + 2)

#define COLORMASK 0b0001
enum color_t {
	COLOR_WHITE = 0,
	COLOR_BLACK = 1,
};
#define PIECEMASK 0b1110
enum piece_t {
	PIECE_NONE = 0,
	PIECE_KING = 2,
	PIECE_QUEEN = 4,
	PIECE_ROOK = 6,
	PIECE_BISHOP = 8,
	PIECE_KNIGHT = 10,
	PIECE_PAWN = 12,
};

enum status_t {
	STATUS_MOVE_WHITE,
	STATUS_MOVE_BLACK,
	STATUS_WHITE_WON_CHECKMATE,
	STATUS_WHITE_WON_SURRENDER,
	STATUS_WHITE_WON_TIMEOUT,
	STATUS_BLACK_WON_CHECKMATE,
	STATUS_BLACK_WON_SURRENDER,
	STATUS_BLACK_WON_TIMEOUT,
	STATUS_DRAW_MATERIAL,
	STATUS_DRAW_STALEMATE,
	STATUS_DRAW_REPETITION,
	STATUS_DRAW_AGREEMENT,
	STATUS_DRAW_TIMEOUT,
	STATUS_DRAW_FIFTY_MOVES,
};

#define MSG_MAXLEN 49
const static char *messages[] = {
	"It is White to move",
	"It is Black to move",
	"White won by checkmate",
	"Black surrendered",
	"White won by timeout",
	"Black won by checkmate",
	"White surrendered",
	"Black won by timeout",
	"Draw by insufficient material",
	"Draw by stalemate",
	"Draw by threefold repetition",
	"The players agreed to a draw",
	"Draw by timeout with insufficient mating material",
	"Draw by fifty consecutive moves wihout capture or pawn move",
};

#define PIECENAME_BUF_SIZE 16
static const char *piece_names[] = {
	"king",
	"queen",
	"rook",
	"bishop",
	"knight",
	"pawn",
};

typedef enum color_t color_t;
typedef enum piece_t piece_t;
typedef int fid;
typedef int fieldinfo_t;
typedef enum status_t status_t;

#endif /* CHESS_H */
