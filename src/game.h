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

#ifndef GAME_H
#define GAME_H

#include "pwn.h"

#define NF 8
#define PIECES_NUM 6
#define COLORS_NUM 2

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
#define CASTLERIGHT_QUEENSIDE 	(1 << 0)
#define CASTLERIGHT_KINGSIDE 	(1 << 1)

enum status_t {
	STATUS_MOVING_WHITE,
	STATUS_MOVING_BLACK,
	STATUS_CHECKMATE_WHITE,
	STATUS_CHECKMATE_BLACK,
	STATUS_DRAW_MATERIAL,
	STATUS_DRAW_STALEMATE,
	STATUS_DRAW_REPETITION,
	STATUS_DRAW_FIFTY_MOVES,
	STATUS_TIMEOUT_WHITE,
	STATUS_TIMEOUT_BLACK,
	STATUS_DRAW_MATERIAL_VS_TIMEOUT,
	STATUS_SURRENDER_WHITE,
	STATUS_SURRENDER_BLACK,
};

typedef enum color_t color_t;
typedef enum piece_t piece_t;
typedef int sqid;
typedef int squareinfo_t;
typedef struct ply_t ply_t;
typedef enum status_t status_t;

#define STARTPOS_FEN "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1"

#define UPDATES_NUM_MAX 4


int game_init(const char *fen);
void game_terminate(void);

int game_exec_ply(sqid ifrom, sqid jfrom, sqid ito, sqid jto, piece_t prompiece);
void game_undo_last_ply(void);

int game_is_movable_piece_at(sqid i, sqid j);
int game_last_ply_was_capture(void);
void game_get_status(status_t *externstatus);

color_t game_get_active_color(void);
piece_t game_get_piece(sqid i, sqid j);
color_t game_get_color(sqid i, sqid j);
squareinfo_t game_get_squareinfo(sqid i, sqid j);
unsigned int game_get_ply_number(void);
size_t game_get_updates(unsigned int nply, sqid squares[][2], int alsoindirect);
int game_get_move_number();

int game_load_fen(const char *s);
void game_get_fen(char *s);

/* debug */
void print_board(void);

#endif /* GAME_H */
