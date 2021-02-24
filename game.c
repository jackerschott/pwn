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

#include <assert.h>
#include <ctype.h>
#include <fcntl.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cairo/cairo.h>

#include "draw.h"
#include "game.h"

/* possible = Move is allowed even if king is in check after
   legal = Move is allowed, king is not in check after */

#define HINT_CASTLE 				(1 << 0)
#define HINT_DEL_CASTLERIGHT_KINGSIDE 		(1 << 1)
#define HINT_DEL_CASTLERIGHT_QUEENSIDE 		(1 << 2)
#define HINT_PROMOTION 				(1 << 3)
#define HINT_EN_PASSANT 			(1 << 4)
#define HINT_SET_EN_PASSANT_FIELD 		(1 << 5)

#define CASTLERIGHT_QUEENSIDE 			(1 << 0)
#define CASTLERIGHT_KINGSIDE 			(1 << 1)

/* for testing */
#define PLIES_BUFSIZE 4
//#define PLIES_BUFSIZE 64

struct moveinfo_t {
	piece_t p;
	fid from[2];
	fid to[2];
	piece_t taken;
	fid fep[2];
	piece_t prompiece;
	int ndrawmoves; /* half move: ply */
	int hints;
};

static fieldinfo_t board[NF][NF];
static color_t moving_color;
static fid fep[2];
static char castlerights[2];
static int num_drawish_moves;
static fid updates[NUM_UPDATES_MAX][2];

static unsigned int pliesnum;
static unsigned int pliessize;
static plyinfo_t *plies;

/* debug */
static void print_hints(int hints);
static void print_move(plyinfo_t *m);

static int is_possible_queen_move(fid ifrom, fid jfrom, fid ito, fid jto, int *hints)
{
	color_t c = board[ifrom][jfrom] & COLORMASK;
	if ((board[ito][jto] != PIECE_NONE) && (board[ito][jto] & COLORMASK) == c)
		return 0;

	if (hints)
		*hints = 0;

	int di = ito - ifrom;
	int dj = jto - jfrom;

	if (abs(di) == abs(dj)) { /* like bishop */
		int vi = SGN(ito - ifrom);
		int vj = SGN(jto - jfrom);
		for (int k = 1; k < abs(ito - ifrom); ++k) {
			int i = ifrom + vi * k;
			int j = jfrom + vj * k;
			if ((board[i][j] & PIECEMASK) != PIECE_NONE)
				return 0;
		}
		return 1;
	} else if (di == 0) { /* like rook, vertically */
		for (int j = MIN(jfrom, jto) + 1; j < MAX(jfrom, jto); ++j) {
			if ((board[ifrom][j] & PIECEMASK) != PIECE_NONE)
				return 0;
		}
		return 1;
	} else if (dj == 0) { /* like rook, horizontally */
		for (int i = MIN(ifrom, ito) + 1; i < MAX(ifrom, ito); ++i) {
			if ((board[i][jfrom] & PIECEMASK) != PIECE_NONE)
				return 0;
		}
		return 1;
	}

	return 0;
}
static int is_possible_rook_move(fid ifrom, fid jfrom, fid ito, fid jto, int *hints)
{
	color_t c = board[ifrom][jfrom] & COLORMASK;
	if ((board[ito][jto] != PIECE_NONE) && (board[ito][jto] & COLORMASK) == c)
		return 0;

	if (hints) {
		*hints = 0;
		if ((castlerights[c] & CASTLERIGHT_QUEENSIDE) && ifrom == 0) {
			*hints |= HINT_DEL_CASTLERIGHT_QUEENSIDE;
		} else if ((castlerights[c] & CASTLERIGHT_KINGSIDE) && ifrom == NF - 1) {
			*hints |= HINT_DEL_CASTLERIGHT_KINGSIDE;
		}
	}

	if (ito - ifrom == 0) { /* vertically */
		for (int j = MIN(jfrom, jto) + 1; j < MAX(jfrom, jto); ++j) {
			if ((board[ifrom][j] & PIECEMASK) != PIECE_NONE)
				return 0;
		}
		return 1;
	} else if (jto - jfrom == 0) { /* horizontally */
		for (int i = MIN(jfrom, jto) + 1; i < MAX(jfrom, jto); ++i) {
			if ((board[i][jfrom] & PIECEMASK) != PIECE_NONE)
				return 0;
		}
		return 1;
	}

	return 0;
}
static int is_possible_bishop_move(fid ifrom, fid jfrom, fid ito, fid jto, int *hints)
{
	color_t c = board[ifrom][jfrom] & COLORMASK;
	if ((board[ito][jto] != PIECE_NONE) && (board[ito][jto] & COLORMASK) == c)
		return 0;

	if (hints)
		*hints = 0;

	if (abs(ito - ifrom) != abs(jto - jfrom))
		return 0;

	int vi = SGN(ito - ifrom);
	int vj = SGN(jto - jfrom);
	for (int k = 1; k < abs(ito - ifrom); ++k) {
		int i = ifrom + vi * k;
		int j = jfrom + vj * k;
		if ((board[i][j] & PIECEMASK) != PIECE_NONE)
			return 0;
	}

	return 1;
}
static int is_possible_knight_move(fid ifrom, fid jfrom, fid ito, fid jto, int *hints)
{
	color_t c = board[ifrom][jfrom] & COLORMASK;
	if ((board[ito][jto] != PIECE_NONE) && (board[ito][jto] & COLORMASK) == c)
		return 0;

	if (hints)
		*hints = 0;

	if ((abs(ito - ifrom) + abs(jto - jfrom)) == 3)
		return 1;

	return 0;
}
static int is_possible_pawn_move(fid ifrom, fid jfrom, fid ito, fid jto, int *hints)
{
	color_t c = board[ifrom][jfrom] & COLORMASK;
	if ((board[ito][jto] != PIECE_NONE) && (board[ito][jto] & COLORMASK) == c)
		return 0;

	if (hints)
		*hints = 0;

	int di = ito - ifrom;
	int dj = jto - jfrom;

	int step = 1 - 2 * c;
	int promrow = (1 - c) * (NF - 1);
	int pawnrow = c * (NF - 2) + (1 - c);
	if (dj == step && di == 0) { /* normal step */
		if ((board[ito][jto] & PIECEMASK) == PIECE_NONE) {
			if (hints && jto == promrow)
				*hints |= HINT_PROMOTION;
			return 1;
		}
	} else if (dj == step && abs(di) == 1) { /* diagonal step with take */
		if ((board[ito][jto] & PIECEMASK) != PIECE_NONE
				&& (board[ito][jto] & COLORMASK) != c) {
			if (hints && jto == promrow)
				*hints |= HINT_PROMOTION;
			return 1;
		} else if (ito == fep[0] && jto == fep[1]) {
			if (hints)
				*hints |= HINT_EN_PASSANT;
			return 1;
		}
	} else if (jfrom == pawnrow && dj == 2 * step && di == 0) { /* 2 field step from pawnrow */
		if ((board[ito][jto - step] & PIECEMASK) == PIECE_NONE
				&& (board[ito][jto] & PIECEMASK) == PIECE_NONE) {
			if (hints)
				*hints |= HINT_SET_EN_PASSANT_FIELD;
			return 1;
		}
	}

	return 0;
}
static int is_possible_non_king_move(piece_t piece, fid ifrom, fid jfrom, fid ito, fid jto, int *flags)
{
	switch (piece) {
	case PIECE_QUEEN:
		return is_possible_queen_move(ifrom, jfrom, ito, jto, flags);
	case PIECE_ROOK:
		return is_possible_rook_move(ifrom, jfrom, ito, jto, flags);
	case PIECE_BISHOP:
		return is_possible_bishop_move(ifrom, jfrom, ito, jto, flags);
	case PIECE_KNIGHT:
		return is_possible_knight_move(ifrom, jfrom, ito, jto, flags);
	case PIECE_PAWN:
		return is_possible_pawn_move(ifrom, jfrom, ito, jto, flags);
	default:
		assert(0);
	}
}

static void get_king(color_t c, fid *i, fid *j)
{
	for (fid l = 0; l < NF; ++l) {
		for (fid k = 0; k < NF; ++k) {
			if ((board[k][l] & PIECEMASK) == PIECE_KING
					&& (board[k][l] & COLORMASK) == c) {
				*i = k;
				*j = l;
				return;
			}
		}
	}
}
static int is_king_in_check(color_t c, fid iking, fid jking)
{
	for (fid j = 0; j < NF; ++j) {
		for (fid i = 0; i < NF; ++i) {
			if ((board[i][j] & PIECEMASK) == PIECE_NONE
					|| (board[i][j] & PIECEMASK) == PIECE_KING
					|| (board[i][j] & COLORMASK) == c)
				continue;

			piece_t p = board[i][j] & PIECEMASK;
			if (is_possible_non_king_move(p, i, j, iking, jking, NULL))
				return 1;
		}
	}
	return 0;
}
static int is_possible_king_move(fid ifrom, fid jfrom, fid ito, fid jto, int *hints)
{
	color_t c = board[ifrom][jfrom] & COLORMASK;
	if ((board[ito][jto] != PIECE_NONE) && (board[ito][jto] & COLORMASK) == c)
		return 0;

	if (hints) {
		*hints = 0;
		if (castlerights[c] & CASTLERIGHT_KINGSIDE)
			*hints |= HINT_DEL_CASTLERIGHT_KINGSIDE;
		if (castlerights[c] & CASTLERIGHT_QUEENSIDE)
			*hints |= HINT_DEL_CASTLERIGHT_QUEENSIDE;
	}

	if (abs(ito - ifrom) <= 1 && abs(jto - jfrom) <= 1) {
		return 1;
	} else if (ito == NF - 2 && (jto == 0 || jto == NF - 1)
			&& (castlerights[c] & CASTLERIGHT_KINGSIDE)) {
		if ((board[NF - 2][jto] & PIECEMASK) != PIECE_NONE
				|| (board[NF - 3][jto] & PIECEMASK) != PIECE_NONE)
			return 0;

		if (is_king_in_check(c, ifrom, jfrom))
			return 0;

		if (hints)
			*hints |= HINT_CASTLE;
		return 1;
	} else if (ito == 2 && (jto == 0 || jto == NF - 1)
			&& (castlerights[c] & CASTLERIGHT_QUEENSIDE)) {
		if ((board[1][jto] & PIECEMASK) != PIECE_NONE
				|| (board[2][jto] & PIECEMASK) != PIECE_NONE
				|| (board[3][jto] & PIECEMASK) != PIECE_NONE)
			return 0;

		if (is_king_in_check(c, ifrom, jfrom))
			return 0;

		if (hints)
			*hints |= HINT_CASTLE;
		return 1;
	}

	return 0;
}
static int is_possible_move(piece_t piece, fid ifrom, fid jfrom, fid ito, fid jto, int *flags)
{
	if (piece == PIECE_KING) {
		return is_possible_king_move(ifrom, jfrom, ito, jto, flags);
	} else {
		return is_possible_non_king_move(piece, ifrom, jfrom, ito, jto, flags);
	}
}

static int exec_ply(fid ifrom, fid jfrom, fid ito, fid jto, int hints, piece_t prompiece)
{
	color_t c = board[ifrom][jfrom] & COLORMASK;

	plyinfo_t ply;
	ply.p = board[ifrom][jfrom] & PIECEMASK;
	ply.from[0] = ifrom;
	ply.from[1] = jfrom;
	ply.to[0] = ito;
	ply.to[1] = jto;
	ply.taken = board[ito][jto];
	ply.prompiece = prompiece;
	ply.ndrawmoves = num_drawish_moves;
	memcpy(ply.fep, fep, sizeof(fep));
	ply.hints = hints;
	memset(updates, 0xff, 2 * NUM_UPDATES_MAX * sizeof(fid));

	/* apply bare move */
	board[ito][jto] = board[ifrom][jfrom];
	memcpy(updates[0], ply.from, sizeof(updates[0]));

	board[ifrom][jfrom] = PIECE_NONE;
	memcpy(updates[1], ply.to, sizeof(updates[0]));

	/* apply hints */
	if (hints & HINT_CASTLE) {
		if (ito > ifrom) {
			board[NF - 3][jfrom] = board[NF - 1][jfrom];
			updates[2][0] = NF - 3;
			updates[2][1] = jfrom;

			board[NF - 1][jfrom] = PIECE_NONE;
			updates[3][0] = NF - 1;
			updates[3][1] = jfrom;
		} else {
			board[3][jfrom] = board[0][jfrom];
			updates[2][0] = 3;
			updates[2][1] = jfrom;

			board[0][jfrom] = PIECE_NONE;
			updates[3][0] = 0;
			updates[3][1] = jfrom;
		}
	} else if (hints & HINT_EN_PASSANT) {
		ply.taken = board[ito][jfrom];

		board[ito][jfrom] = PIECE_NONE;
		updates[2][0] = ito;
		updates[2][1] = jfrom;
	} else if (hints & HINT_SET_EN_PASSANT_FIELD) {
		fep[0] = ifrom;
		fep[1] = (jfrom + jto) / 2;
	} else if (hints & HINT_PROMOTION) {
		board[ito][jto] = (board[ito][jto] & COLORMASK) | prompiece;
	}

	if (hints & HINT_DEL_CASTLERIGHT_QUEENSIDE)
		castlerights[c] &= ~CASTLERIGHT_QUEENSIDE;
	if (hints & HINT_DEL_CASTLERIGHT_KINGSIDE)
		castlerights[c] &= ~CASTLERIGHT_KINGSIDE;

	/* count move for fifty-move rule */
	if ((ply.taken & PIECEMASK) == PIECE_NONE && ply.p != PIECE_PAWN) {
		++num_drawish_moves;
	} else {
		num_drawish_moves = 0;
	}

	/* add move to list */
	if (pliesnum == pliessize) {
		pliessize += PLIES_BUFSIZE;
		plyinfo_t *p = realloc(plies, pliessize * sizeof(*p));
		if (!p)
			return -1;
		plies = p;
	}
	plies[pliesnum] = ply;
	++pliesnum;
	return 0;
}
static void undo_last_ply()
{
	assert(pliesnum > 0);
	plyinfo_t ply = plies[pliesnum - 1];
	fid ifrom = ply.from[0];
	fid jfrom = ply.from[1];
	fid ito = ply.to[0];
	fid jto = ply.to[1];
	color_t c = board[ito][jto] & COLORMASK;
	/* fill updates with -1 */
	memset(updates, 0xff, 2 * NUM_UPDATES_MAX * sizeof(fid));
	/* remove ply from list */
	--pliesnum;

	/* restore fifty-move rule count */
	num_drawish_moves = ply.ndrawmoves;

	/* undo hints */
	if (ply.hints & HINT_DEL_CASTLERIGHT_QUEENSIDE)
		castlerights[c] |= CASTLERIGHT_QUEENSIDE;
	if (ply.hints & HINT_DEL_CASTLERIGHT_KINGSIDE)
		castlerights[c] |= CASTLERIGHT_KINGSIDE;

	if (ply.hints & HINT_CASTLE) {
		if (ito > ifrom) {
			board[NF - 1][jfrom] = board[NF - 3][jfrom];
			updates[3][0] = NF - 1;
			updates[3][1] = jfrom;

			board[NF - 3][jfrom] = PIECE_NONE;
			updates[2][0] = NF - 3;
			updates[2][1] = jfrom;

		} else {
			board[0][jfrom] = board[3][jfrom];
			updates[3][0] = 0;
			updates[3][1] = jfrom;

			board[3][jfrom] = PIECE_NONE;
			updates[2][0] = 3;
			updates[2][1] = jfrom;
		}
	} else if (ply.hints & HINT_EN_PASSANT) {
		board[ito][jfrom] = ply.taken;
		updates[2][0] = jto;
		updates[2][1] = jfrom;
	} else if (ply.hints & HINT_SET_EN_PASSANT_FIELD) {
		memcpy(fep, ply.fep, sizeof(fep));
	} else if (ply.hints & HINT_PROMOTION) {
		board[ito][jto] = (board[ito][jto] & COLORMASK) | PIECE_PAWN;
	}

	/* undo bare move */
	board[ifrom][jfrom] = board[ito][jto];
	memcpy(updates[1], ply.from, sizeof(updates[1]));

	board[ito][jto] = (ply.hints & HINT_EN_PASSANT) ? PIECE_NONE : ply.taken;
	memcpy(updates[1], ply.to, sizeof(updates[0]));
}

static int piece_has_legal_move(fid ipiece, fid jpiece)
{
	piece_t p = board[ipiece][jpiece] & PIECEMASK;
	color_t c = board[ipiece][jpiece] & COLORMASK;
	fid iking, jking;
	if (p != PIECE_KING)
		get_king(c, &iking, &jking);
	for (int j = 0; j < NF; ++j) {
		for (int i = 0; i < NF; ++i) {
			if (i == ipiece && j == jpiece)
				continue;

			int hints;
			if (!is_possible_move(p, ipiece, jpiece, i, j, &hints))
				continue;

			plyinfo_t m;
			exec_ply(ipiece, jpiece, i, j, hints, 0);

			int check;
			if (p == PIECE_KING) {
				check = is_king_in_check(c, i, j);
			} else {
				check = is_king_in_check(c, iking, jking);
			}

			undo_last_ply();
			if (!check)
				return 1;
		}
	}
	return 0;
}
static int has_legal_move(color_t color)
{
	for (fid j = 0; j < NF; ++j) {
		for (fid i = 0; i < NF; ++i) {
			if ((board[i][j] & PIECEMASK) == PIECE_NONE
					|| (board[i][j] & COLORMASK) != color)
				continue;
			if (piece_has_legal_move(i, j))
				return 1;
		}
	}
	return 0;
}

int game_init(void)
{
	moving_color = COLOR_WHITE;
	memset(fep, 0xff, sizeof(fep));
	castlerights[0] = CASTLERIGHT_KINGSIDE | CASTLERIGHT_QUEENSIDE;
	castlerights[1] = CASTLERIGHT_KINGSIDE | CASTLERIGHT_QUEENSIDE;

	fid pawnrows[2];
	fid piecerows[2];
	piecerows[0] = 0;
	piecerows[1] = NF - 1;
	pawnrows[0] = 1;
	pawnrows[1] = NF - 2;

	for (int j = 0; j < NUM_COLORS; ++j) {
		board[0][piecerows[j]] = PIECE_ROOK | j;
		board[1][piecerows[j]] = PIECE_KNIGHT | j;
		board[2][piecerows[j]] = PIECE_BISHOP | j;
		board[3][piecerows[j]] = PIECE_QUEEN | j;
		board[4][piecerows[j]] = PIECE_KING | j;
		board[5][piecerows[j]] = PIECE_BISHOP | j;
		board[6][piecerows[j]] = PIECE_KNIGHT | j;
		board[7][piecerows[j]] = PIECE_ROOK | j;
		for (int i = 0; i < NF; ++i) {
			board[i][pawnrows[j]] = PIECE_PAWN | j;
		}
	}

	pliessize = PLIES_BUFSIZE;
	plies = malloc(pliessize * sizeof(*plies));
	if (!plies)
		return -1;
	return 0;
}
void game_init_test_board(void)
{
	moving_color = COLOR_WHITE;
	memset(fep, 0xff, sizeof(fep));
	castlerights[0] = CASTLERIGHT_KINGSIDE | CASTLERIGHT_QUEENSIDE;
	castlerights[1] = CASTLERIGHT_KINGSIDE | CASTLERIGHT_QUEENSIDE;

	board[4][0] = PIECE_KING | COLOR_WHITE;
	board[4][NF - 1] = PIECE_KING | COLOR_BLACK;
	board[0][6] = PIECE_PAWN;
}
void game_terminate(void)
{
	free(plies);
}

int game_exec_ply(fid ifrom, fid jfrom, fid ito, fid jto, piece_t prompiece)
{
	piece_t piece = board[ifrom][jfrom] & PIECEMASK;

	/* check if move is possible */
	int hints;
	if (!is_possible_move(piece, ifrom, jfrom, ito, jto, &hints))
		return 1;

	/* apply move */
	exec_ply(ifrom, jfrom, ito, jto, hints, prompiece);

	/* check if move is legal */
	fid iking, jking;
	get_king(moving_color, &iking, &jking);
	if (is_king_in_check(moving_color, iking, jking)) {
		undo_last_ply();
		return 1;
	}

	/* indicate that prompiece must be given */
	if ((hints & HINT_PROMOTION) && prompiece == PIECE_NONE) {
		undo_last_ply();
		return 2;
	}

	moving_color = OPP_COLOR(moving_color);
	return 0;
}
void game_undo_last_ply(void)
{
	undo_last_ply();
}

color_t game_get_moving_color()
{
	return moving_color;
}
piece_t game_get_piece(fid i, fid j)
{
	return board[i][j] & PIECEMASK;
}
color_t game_get_color(fid i, fid j)
{
	return board[i][j] & COLORMASK;
}
fieldinfo_t game_get_fieldinfo(fid i, fid j)
{
	return board[i][j];
}
int game_get_move_number()
{
	return pliesnum / 2;
}
int game_is_stalemate(void)
{
	fid iking, jking;
	get_king(moving_color, &iking, &jking);

	if (is_king_in_check(moving_color, iking, jking))
		return 0;

	for (fid j = 0; j < NF; ++j) {
		for (fid i = 0; i < NF; ++i) {
			if ((board[i][j] & PIECEMASK) == PIECE_NONE
					|| (board[i][j] & COLORMASK) != moving_color)
				continue;
			if (piece_has_legal_move(i, j))
				return 0;
		}
	}

	return 1;
}
int game_is_checkmate(void)
{
	fid iking, jking;
	get_king(moving_color, &iking, &jking);

	if (!is_king_in_check(moving_color, iking, jking))
		return 0;

	for (fid j = 0; j < NF; ++j) {
		for (fid i = 0; i < NF; ++i) {
			if ((board[i][j] & PIECEMASK) == PIECE_NONE
					|| (board[i][j] & COLORMASK) != moving_color)
				continue;
			if (piece_has_legal_move(i, j))
				return 0;
		}
	}

	return 1;
}
int game_is_movable_piece_at(fid i, fid j)
{
	return ((board[i][j] & PIECEMASK) != PIECE_NONE)
		&& (board[i][j] & COLORMASK) == moving_color;
}
status_t game_get_status(int timeout)
{
	if (timeout) {
		if (moving_color == COLOR_WHITE) {
			return STATUS_TIMEOUT_WHITE;
		} else {
			return STATUS_TIMEOUT_BLACK;
		}
	}

	if (!has_legal_move(moving_color)) {
		fid iking, jking;
		get_king(moving_color, &iking, &jking);

		if (is_king_in_check(moving_color, iking, jking)) {
			return moving_color ? STATUS_CHECKMATE_BLACK
				: STATUS_CHECKMATE_WHITE;
		} else {
			return STATUS_DRAW_STALEMATE;
		}
	}

	//int n = 0;
	//for (moveinfo_t *m = movelast; m; m = m->prev) {
	//	if (memcmp(m->posid, board, POSIDSIZE) == 0) {
	//		printf("equal\n");
	//		++n;
	//	}
	//}
	//if (n >= 3)
	//	return STATUS_DRAW_REPETITION;

	return moving_color ? STATUS_MOVING_BLACK : STATUS_MOVING_WHITE;
}
void game_get_updates(fid u[][2])
{
	memcpy(u, updates, 2 * NUM_UPDATES_MAX * sizeof(fid));
}

/* debug */
static char piece_chars[] = { 'k', 'q', 'r', 'b', 'n', 'p' };
static const char *hintstrs[] = {
	"HINT_CASTLE",
	"HINT_DEL_ATTR_CAN_CASTLE",
	"HINT_PROMOTION",
	"HINT_EN_PASSANT",
	"HINT_ADD_ATTR_TAKEABLE_EN_PASSANT",
};
static void print_hints(int hints)
{
	for (int i = 0; i < 5; ++i) {
		if (hints & (1 << i)) {
			printf("%s | ", hintstrs[i]);
		}
	}
}
static void print_move(plyinfo_t *m)
{
	char fieldfrom[3] = {0};
	char fieldto[3] = {0};
	fieldfrom[0] = ROW_CHAR(m->from[0]);
	fieldfrom[1] = COL_CHAR(m->from[1]);
	fieldto[0] = ROW_CHAR(m->to[0]);
	fieldto[1] = COL_CHAR(m->to[1]);

	//printf("fields: %s%s-%s\n", piece_symbols[PIECE_IDX(m->p)], fieldfrom, fieldto);
	//printf("fields: %s%i%i-%i%i\n", piece_symbols[PIECE_IDX(m->p)], m->ifrom, m->jfrom, m->ito, m->jto);

	printf("taken: ");
	if ((m->taken & PIECEMASK) != PIECE_NONE)
		printf("%s", piece_names[PIECE_IDX(m->taken & PIECEMASK)]);
	printf("\n");

	printf("hints: ");
	print_hints(m->hints);
	printf("\n\n");
}
void print_board(void)
{
	for (int j = NF - 1; j >= 0; --j) {
		for (int i = 0; i < NF; ++i) {
			piece_t piece = board[i][j] & PIECEMASK;
			if (piece == PIECE_NONE) {
				printf("  ");
				continue;
			}

			int p = PIECE_IDX(piece);
			char c = (board[i][j] & COLORMASK) ?
				piece_chars[p] : toupper(piece_chars[p]);
			printf("%c ", c);
		}
		printf("\n");
	}
	printf("\n");

	char cr[4];
	cr[0] = '\0';
	if (castlerights[COLOR_WHITE] & CASTLERIGHT_KINGSIDE)
		strcat(cr, "K");
	if (castlerights[COLOR_WHITE] & CASTLERIGHT_QUEENSIDE)
		strcat(cr, "Q");
	if (castlerights[COLOR_BLACK] & CASTLERIGHT_KINGSIDE)
		strcat(cr, "k");
	if (castlerights[COLOR_BLACK] & CASTLERIGHT_QUEENSIDE)
		strcat(cr, "q");
	if (strlen(cr) == 0) {
		printf("castlerights: -\n");
	} else {
		printf("castlerights: %s\n", cr);
	}

	if (fep[0] == -1) {
		printf("ep target: -\n");
	} else {
		char f[3];
		f[0] = ROW_CHAR(fep[0]);
		f[1] = COL_CHAR(fep[1]);
		f[2] = '\0';
		printf("ep target: %s\n", f);
	}
};
