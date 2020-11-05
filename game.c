#include <cairo/cairo.h>

#include "draw.h"
#include "types.h"

#include "game.h"


field touched;

double xorig;
double yorig;
double boardsize;
double fieldsize;

void game_init_board(int board[NUM_ROW_FIELDS][NUM_ROW_FIELDS], color c)
{
	int pawnrows[2];
	int piecerows[2];
	if (c == COLOR_WHITE) {
		piecerows[COLOR_WHITE] = NUM_ROW_FIELDS - 1;
		piecerows[COLOR_BLACK] = 0;
		pawnrows[COLOR_WHITE] = NUM_ROW_FIELDS - 2;
		pawnrows[COLOR_BLACK] = 1;
	} else {
		piecerows[COLOR_WHITE] = 0;
		piecerows[COLOR_BLACK] = NUM_ROW_FIELDS - 1;
		pawnrows[COLOR_WHITE] = 1;
		pawnrows[COLOR_BLACK] = NUM_ROW_FIELDS - 2;
	}

	for (int j = 0; j < 2; ++j) {
		board[0][piecerows[j]] = PIECE_WHITE_ROOK + j;
		board[1][piecerows[j]] = PIECE_WHITE_KNIGHT + j;
		board[2][piecerows[j]] = PIECE_WHITE_BISHOP + j;
		board[3][piecerows[j]] = PIECE_WHITE_QUEEN + j;
		board[4][piecerows[j]] = PIECE_WHITE_KING + j;
		board[5][piecerows[j]] = PIECE_WHITE_BISHOP + j;
		board[6][piecerows[j]] = PIECE_WHITE_KNIGHT + j;
		board[7][piecerows[j]] = PIECE_WHITE_ROOK + j;
		for (int i = 0; i < NUM_ROW_FIELDS; ++i) {
			board[i][pawnrows[j]] = PIECE_WHITE_PAWN + j;
		}
	}
}

void game_touch(field f)
{
	touched = f;
}
