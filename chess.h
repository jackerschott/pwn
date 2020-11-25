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
