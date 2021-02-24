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

#define POSIDSIZE 64

struct moveinfo_t {
	piece_t p;
	fid ifrom, jfrom;
	fid ito, jto;
	fieldinfo_t taken;
	fid fep[2];
	piece_t prompiece;
	int nmoves;
	int hints;
	char posid[POSIDSIZE];
	moveinfo_t *prev;
	moveinfo_t *next;
};

static fieldinfo_t board[NF][NF];
static color_t moving_color;
static fid fep[2];
static char castlerights[2];
static int nmoves;
static fid updates[NUM_UPDATES_MAX][2];

/* why linked list? remove? */
static moveinfo_t *movefirst;
static moveinfo_t *movelast;

/* debug */
static void print_hints(int hints);
static void print_move(moveinfo_t *m);

static int is_possible_king_move(fid ifrom, fid jfrom, fid ito, fid jto, int *flags);
static int is_possible_queen_move(fid ifrom, fid jfrom, fid ito, fid jto, int *flags);
static int is_possible_rook_move(fid ifrom, fid jfrom, fid ito, fid jto, int *flags);
static int is_possible_bishop_move(fid ifrom, fid jfrom, fid ito, fid jto, int *flags);
static int is_possible_knight_move(fid ifrom, fid jfrom, fid ito, fid jto, int *flags);
static int is_possible_pawn_move(fid ifrom, fid jfrom, fid ito, fid jto, int *flags);
static int (*is_possible_move[NUM_PIECES])(fid, fid, fid, fid, int *) = {
	is_possible_king_move,
	is_possible_queen_move,
	is_possible_rook_move,
	is_possible_bishop_move,
	is_possible_knight_move,
	is_possible_pawn_move,
};

static int is_king_in_check(color_t c, fid iking, fid jking);

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

static int move(fid ifrom, fid jfrom, fid ito, fid jto,
		int hints, piece_t prompiece, moveinfo_t *m)
{
	color_t c = board[ifrom][jfrom] & COLORMASK;

	m->p = board[ifrom][jfrom] & PIECEMASK;
	m->ifrom = ifrom;
	m->jfrom = jfrom;
	m->ito = ito;
	m->jto = jto;
	m->taken = board[ito][jto];
	m->prompiece = prompiece;
	m->nmoves = nmoves;
	memcpy(m->fep, fep, sizeof(fep));
	m->hints = hints;
	memcpy(m->posid, board, POSIDSIZE);
	memset(updates, 0xff, 2 * NUM_UPDATES_MAX * sizeof(fid));

	/* apply bare move */
	board[ito][jto] = board[ifrom][jfrom];
	updates[0][0] = ifrom;
	updates[0][1] = jfrom;

	board[ifrom][jfrom] = PIECE_NONE;
	updates[1][0] = ito;
	updates[1][1] = jto;

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
		m->taken = board[ito][jfrom];

		board[ito][jfrom] = PIECE_NONE;
		updates[2][0] = ito;
		updates[2][1] = jfrom;
	} else if (hints & HINT_SET_EN_PASSANT_FIELD) {
		fep[0] = ifrom;
		fep[1] = (jfrom + jto) / 2;
	} else if (hints & HINT_PROMOTION) {
		board[m->ito][m->jto] = (board[m->ito][m->jto] & COLORMASK) | prompiece;
	}

	if (hints & HINT_DEL_CASTLERIGHT_QUEENSIDE)
		castlerights[c] &= ~CASTLERIGHT_QUEENSIDE;
	if (hints & HINT_DEL_CASTLERIGHT_KINGSIDE)
		castlerights[c] &= ~CASTLERIGHT_KINGSIDE;

	/* count move for fifty-move rule */
	if ((m->taken & PIECEMASK) == PIECE_NONE && m->p != PIECE_PAWN) {
		++nmoves;
	} else {
		nmoves = 0;
	}

	/* add move to list */
	if (movelast) {
		movelast->next = m;
		m->prev = movelast;
		movelast = m;
		m->next = NULL;
	} else {
		m->prev = NULL;
		m->next = NULL;
		movefirst = m;
		movelast = m;
	}

	return 0;
}
static void undo_move(moveinfo_t *m)
{
	color_t c = board[m->ito][m->jto] & COLORMASK;

	memset(updates, 0xff, 2 * NUM_UPDATES_MAX * sizeof(fid)); /* set to -1 */

	/* remove move from list */
	if (m->prev) {
		movelast = m->prev;
		movelast->next = NULL;
	} else {
		movefirst = NULL;
		movelast = NULL;
	}

	/* restore fift-move rule count */
	nmoves = m->nmoves;

	/* undo hints */
	if (m->hints & HINT_DEL_CASTLERIGHT_QUEENSIDE)
		castlerights[c] |= CASTLERIGHT_QUEENSIDE;
	if (m->hints & HINT_DEL_CASTLERIGHT_KINGSIDE)
		castlerights[c] |= CASTLERIGHT_KINGSIDE;

	if (m->hints & HINT_CASTLE) {
		if (m->ito > m->ifrom) {
			board[NF - 1][m->jfrom] = board[NF - 3][m->jfrom];
			updates[3][0] = NF - 1;
			updates[3][1] = m->jfrom;

			board[NF - 3][m->jfrom] = PIECE_NONE;
			updates[2][0] = NF - 3;
			updates[2][1] = m->jfrom;

		} else {
			board[0][m->jfrom] = board[3][m->jfrom];
			updates[3][0] = 0;
			updates[3][1] = m->jfrom;

			board[3][m->jfrom] = PIECE_NONE;
			updates[2][0] = 3;
			updates[2][1] = m->jfrom;
		}
	} else if (m->hints & HINT_EN_PASSANT) {
		board[m->ito][m->jfrom] = m->taken;
		updates[2][0] = m->jto;
		updates[2][1] = m->jfrom;
	} else if (m->hints & HINT_SET_EN_PASSANT_FIELD) {
		memcpy(fep, m->fep, sizeof(fep));
	} else if (m->hints & HINT_PROMOTION) {
		board[m->ito][m->jto] = (board[m->ito][m->jto] & COLORMASK) | PIECE_PAWN;
	}

	/* undo bare move */
	board[m->ifrom][m->jfrom] = board[m->ito][m->jto];
	updates[1][0] = m->ifrom;
	updates[1][1] = m->jfrom;

	board[m->ito][m->jto] = (m->hints & HINT_EN_PASSANT) ? PIECE_NONE : m->taken;
	updates[0][0] = m->ito;
	updates[0][1] = m->jto;
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
					|| (board[i][j] & COLORMASK) == c)
				continue;

			piece_t p = board[i][j] & PIECEMASK;
			if (is_possible_move[PIECE_IDX(p)](i, j, iking, jking, NULL))
				return 1;
		}
	}
	return 0;
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
			if (!is_possible_move[PIECE_IDX(p)](ipiece, jpiece, i, j, &hints))
				continue;

			moveinfo_t m;
			move(ipiece, jpiece, i, j, hints, 0, &m);

			int check;
			if (p == PIECE_KING) {
				check = is_king_in_check(c, i, j);
			} else {
				check = is_king_in_check(c, iking, jking);
			}

			undo_move(&m);
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

void game_init_board(void)
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
	for (moveinfo_t *m = movelast; m; m = m->prev) {
		free(m);
	}
}

int game_is_movable_piece_at(fid i, fid j)
{
	return ((board[i][j] & PIECEMASK) != PIECE_NONE)
		&& (board[i][j] & COLORMASK) == moving_color;
}
int game_move(fid ifrom, fid jfrom, fid ito, fid jto, piece_t prompiece)
{
	piece_t piece = board[ifrom][jfrom] & PIECEMASK;

	/* check if move is possible */
	int hints;
	if (!is_possible_move[PIECE_IDX(piece)](ifrom, jfrom, ito, jto, &hints))
		return 1;


	/* apply move */
	moveinfo_t *m = malloc(sizeof(moveinfo_t));
	if (!m) {
		SYSERR();
		return -1;
	}
	move(ifrom, jfrom, ito, jto, hints, prompiece, m);

	/* check if move is legal */
	fid iking, jking;
	get_king(moving_color, &iking, &jking);
	if (is_king_in_check(moving_color, iking, jking)) {
		undo_move(m);
		free(m);
		return 1;
	}

	/* indicate that prompiece must be given */
	if ((hints & HINT_PROMOTION) && prompiece == PIECE_NONE) {
		undo_move(m);
		free(m);
		return 2;
	}

	moving_color = OPP_COLOR(moving_color);
	return 0;
}
void game_undo_last_move(void)
{
	undo_move(movelast);
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
status_t game_get_status(int timeout)
{
	if (timeout) {
		if (moving_color == COLOR_WHITE) {
			return STATUS_BLACK_WON_TIMEOUT;
		} else {
			return STATUS_WHITE_WON_TIMEOUT;
		}
	}

	if (!has_legal_move(moving_color)) {
		fid iking, jking;
		get_king(moving_color, &iking, &jking);

		if (is_king_in_check(moving_color, iking, jking)) {
			return moving_color ? STATUS_WHITE_WON_CHECKMATE
				: STATUS_BLACK_WON_CHECKMATE;
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

	return moving_color ? STATUS_MOVE_BLACK : STATUS_MOVE_WHITE;
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
void game_get_updates(fid u[][2])
{
	memcpy(u, updates, 2 * NUM_UPDATES_MAX * sizeof(fid));
}
int game_get_move_number()
{
	int n = 0;
	for (moveinfo_t *m = movelast; m; m = m->prev) {
		++n;
	}
	return n / 2;
}

int game_save_board(const char *fname)
{
	int fboard = open(fname, O_WRONLY | O_CREAT, 0644);
	if (fboard == -1)
		return -1;

	write(fboard, board, NF * NF * sizeof(fieldinfo_t));
	write(fboard, &moving_color, sizeof(color_t));
	close(fboard);
	return 0;
}
int game_load_board(const char *fname)
{
	int fboard = open(fname, O_RDONLY);
	if (fboard == -1)
		return -1;

	read(fboard, board, NF * NF * sizeof(fieldinfo_t));
	read(fboard, &moving_color, sizeof(color_t));
	close(fboard);

	for (moveinfo_t *m = movelast; m; m = m->prev) {
		free(m);
	}
	movelast = NULL;
	movefirst = NULL;

	return 0;
}

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
static void print_move(moveinfo_t *m)
{
	char fieldfrom[3] = {0};
	char fieldto[3] = {0};
	fieldfrom[0] = COL_CHAR(m->ifrom);
	fieldfrom[1] = ROW_CHAR(m->jfrom);
	fieldto[0] = COL_CHAR(m->ito);
	fieldto[1] = ROW_CHAR(m->jto);

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
		f[0] = COL_CHAR(fep[0]);
		f[1] = ROW_CHAR(fep[1]);
		f[2] = '\0';
		printf("ep target: %s\n", f);
	}
};
