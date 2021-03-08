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

#include "notation.h"

#include "game.h"

/* pseudolegal = Move is allowed even if king is in check after
   legal = Move is allowed, king is not in check after */

#define HINT_CASTLE 				(1 << 0)
#define HINT_DEL_CASTLERIGHT_KINGSIDE 		(1 << 1)
#define HINT_DEL_CASTLERIGHT_QUEENSIDE 		(1 << 2)
#define HINT_PROMOTION 				(1 << 3)
#define HINT_EN_PASSANT 			(1 << 4)
#define HINT_SET_EN_PASSANT_FIELD 		(1 << 5)

/* for testing */
//#define PLIES_BUFSIZE 4
#define PLIES_BUFSIZE 64

#define DRAWISH_MOVES_MAX 50

struct plyinfo_t {
	piece_t p;
	sqid from[2];
	sqid to[2];
	piece_t taken;
	piece_t prompiece;
	sqid fep[2];
	int castlerights[2];
	squareinfo_t position[NF][NF];
	int ndrawplies;
	int hints;
};

static squareinfo_t position[NF][NF];
static color_t active_color;
static int castlerights[2];
static sqid fep[2];
static unsigned int drawish_plies_num;
static unsigned int nmove;
static sqid updates[NUM_UPDATES_MAX][2];

static unsigned int pliesnum;
static unsigned int pliessize;
static plyinfo_t *plies;

/* debug */
static void print_hints(int hints);
static void print_ply(plyinfo_t *m);

static int is_pseudolegal_queen_ply(sqid ifrom, sqid jfrom, sqid ito, sqid jto, int *hints)
{
	color_t c = position[ifrom][jfrom] & COLORMASK;
	if ((position[ito][jto] != PIECE_NONE) && (position[ito][jto] & COLORMASK) == c)
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
			if ((position[i][j] & PIECEMASK) != PIECE_NONE)
				return 0;
		}
		return 1;
	} else if (di == 0) { /* like rook, vertically */
		for (int j = MIN(jfrom, jto) + 1; j < MAX(jfrom, jto); ++j) {
			if ((position[ifrom][j] & PIECEMASK) != PIECE_NONE)
				return 0;
		}
		return 1;
	} else if (dj == 0) { /* like rook, horizontally */
		for (int i = MIN(ifrom, ito) + 1; i < MAX(ifrom, ito); ++i) {
			if ((position[i][jfrom] & PIECEMASK) != PIECE_NONE)
				return 0;
		}
		return 1;
	}

	return 0;
}
static int is_pseudolegal_rook_ply(sqid ifrom, sqid jfrom, sqid ito, sqid jto, int *hints)
{
	color_t c = position[ifrom][jfrom] & COLORMASK;
	if ((position[ito][jto] != PIECE_NONE) && (position[ito][jto] & COLORMASK) == c)
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
			if ((position[ifrom][j] & PIECEMASK) != PIECE_NONE)
				return 0;
		}
		return 1;
	} else if (jto - jfrom == 0) { /* horizontally */
		for (int i = MIN(ifrom, ito) + 1; i < MAX(ifrom, ito); ++i) {
			if ((position[i][jfrom] & PIECEMASK) != PIECE_NONE)
				return 0;
		}
		return 1;
	}

	return 0;
}
static int is_pseudolegal_bishop_ply(sqid ifrom, sqid jfrom, sqid ito, sqid jto, int *hints)
{
	color_t c = position[ifrom][jfrom] & COLORMASK;
	if ((position[ito][jto] != PIECE_NONE) && (position[ito][jto] & COLORMASK) == c)
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
		if ((position[i][j] & PIECEMASK) != PIECE_NONE)
			return 0;
	}

	return 1;
}
static int is_pseudolegal_knight_ply(sqid ifrom, sqid jfrom, sqid ito, sqid jto, int *hints)
{
	color_t c = position[ifrom][jfrom] & COLORMASK;
	if ((position[ito][jto] != PIECE_NONE) && (position[ito][jto] & COLORMASK) == c)
		return 0;

	if (hints)
		*hints = 0;

	if ((abs(ito - ifrom) + abs(jto - jfrom)) == 3
			&& ito != ifrom && jto != jfrom)
		return 1;

	return 0;
}
static int is_pseudolegal_pawn_ply(sqid ifrom, sqid jfrom, sqid ito, sqid jto, int *hints)
{
	color_t c = position[ifrom][jfrom] & COLORMASK;
	if ((position[ito][jto] != PIECE_NONE) && (position[ito][jto] & COLORMASK) == c)
		return 0;

	if (hints)
		*hints = 0;

	int di = ito - ifrom;
	int dj = jto - jfrom;

	int step = 1 - 2 * c;
	int promrow = (1 - c) * (NF - 1);
	int pawnrow = c * (NF - 2) + (1 - c);
	if (dj == step && di == 0) { /* normal step */
		if ((position[ito][jto] & PIECEMASK) == PIECE_NONE) {
			if (hints && jto == promrow)
				*hints |= HINT_PROMOTION;
			return 1;
		}
	} else if (dj == step && abs(di) == 1) { /* diagonal step with take */
		if ((position[ito][jto] & PIECEMASK) != PIECE_NONE
				&& (position[ito][jto] & COLORMASK) != c) {
			if (hints && jto == promrow)
				*hints |= HINT_PROMOTION;
			return 1;
		} else if (ito == fep[0] && jto == fep[1]) {
			if (hints)
				*hints |= HINT_EN_PASSANT;
			return 1;
		}
	} else if (jfrom == pawnrow && dj == 2 * step && di == 0) { /* 2 square step from pawnrow */
		if ((position[ito][jto - step] & PIECEMASK) == PIECE_NONE
				&& (position[ito][jto] & PIECEMASK) == PIECE_NONE) {
			if (hints)
				*hints |= HINT_SET_EN_PASSANT_FIELD;
			return 1;
		}
	}

	return 0;
}
static int is_pseudolegal_non_king_ply(piece_t piece, sqid ifrom, sqid jfrom,
		sqid ito, sqid jto, int *flags)
{
	switch (piece) {
	case PIECE_QUEEN:
		return is_pseudolegal_queen_ply(ifrom, jfrom, ito, jto, flags);
	case PIECE_ROOK:
		return is_pseudolegal_rook_ply(ifrom, jfrom, ito, jto, flags);
	case PIECE_BISHOP:
		return is_pseudolegal_bishop_ply(ifrom, jfrom, ito, jto, flags);
	case PIECE_KNIGHT:
		return is_pseudolegal_knight_ply(ifrom, jfrom, ito, jto, flags);
	case PIECE_PAWN:
		return is_pseudolegal_pawn_ply(ifrom, jfrom, ito, jto, flags);
	default:
		assert(0);
	}
}

static void get_king(color_t c, sqid *i, sqid *j)
{
	for (sqid l = 0; l < NF; ++l) {
		for (sqid k = 0; k < NF; ++k) {
			if ((position[k][l] & PIECEMASK) == PIECE_KING
					&& (position[k][l] & COLORMASK) == c) {
				*i = k;
				*j = l;
				return;
			}
		}
	}
}
static int is_king_in_check(color_t c, sqid iking, sqid jking)
{
	for (sqid j = 0; j < NF; ++j) {
		for (sqid i = 0; i < NF; ++i) {
			if ((position[i][j] & PIECEMASK) == PIECE_NONE
					|| (position[i][j] & PIECEMASK) == PIECE_KING
					|| (position[i][j] & COLORMASK) == c)
				continue;

			piece_t p = position[i][j] & PIECEMASK;
			if (is_pseudolegal_non_king_ply(p, i, j, iking, jking, NULL))
				return 1;
		}
	}
	return 0;
}
static int is_pseudolegal_king_ply(sqid ifrom, sqid jfrom, sqid ito, sqid jto, int *hints)
{
	color_t c = position[ifrom][jfrom] & COLORMASK;
	if ((position[ito][jto] != PIECE_NONE) && (position[ito][jto] & COLORMASK) == c)
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
		if ((position[NF - 2][jto] & PIECEMASK) != PIECE_NONE
				|| (position[NF - 3][jto] & PIECEMASK) != PIECE_NONE)
			return 0;

		if (is_king_in_check(c, ifrom, jfrom))
			return 0;

		if (hints)
			*hints |= HINT_CASTLE;
		return 1;
	} else if (ito == 2 && (jto == 0 || jto == NF - 1)
			&& (castlerights[c] & CASTLERIGHT_QUEENSIDE)) {
		if ((position[1][jto] & PIECEMASK) != PIECE_NONE
				|| (position[2][jto] & PIECEMASK) != PIECE_NONE
				|| (position[3][jto] & PIECEMASK) != PIECE_NONE)
			return 0;

		if (is_king_in_check(c, ifrom, jfrom))
			return 0;

		if (hints)
			*hints |= HINT_CASTLE;
		return 1;
	}

	return 0;
}
static int is_pseudolegal_ply(piece_t piece, sqid ifrom, sqid jfrom, sqid ito, sqid jto, int *flags)
{
	if (piece == PIECE_KING) {
		return is_pseudolegal_king_ply(ifrom, jfrom, ito, jto, flags);
	} else {
		return is_pseudolegal_non_king_ply(piece, ifrom, jfrom, ito, jto, flags);
	}
}

static int exec_ply(sqid ifrom, sqid jfrom, sqid ito, sqid jto, int hints, piece_t prompiece)
{
	color_t c = position[ifrom][jfrom] & COLORMASK;

	plyinfo_t ply;
	memset(&ply, 0, sizeof(ply));
	ply.p = position[ifrom][jfrom] & PIECEMASK;
	ply.from[0] = ifrom;
	ply.from[1] = jfrom;
	ply.to[0] = ito;
	ply.to[1] = jto;
	ply.taken = position[ito][jto] & PIECEMASK;
	ply.prompiece = prompiece;
	memcpy(ply.fep, fep, sizeof(fep));
	memcpy(ply.castlerights, castlerights, sizeof(ply.castlerights));
	memcpy(ply.position, position, sizeof(ply.position));
	ply.ndrawplies = drawish_plies_num;
	ply.hints = hints;

	memset(updates, 0xff, sizeof(updates));
	fep[0] = -1;
	fep[1] = -1;

	/* apply bare ply */
	position[ito][jto] = position[ifrom][jfrom];
	memcpy(updates[0], ply.from, sizeof(updates[0]));

	position[ifrom][jfrom] = PIECE_NONE;
	memcpy(updates[1], ply.to, sizeof(updates[0]));

	/* apply hints */
	if (hints & HINT_CASTLE) {
		if (ito > ifrom) {
			position[NF - 3][jfrom] = position[NF - 1][jfrom];
			updates[2][0] = NF - 3;
			updates[2][1] = jfrom;

			position[NF - 1][jfrom] = PIECE_NONE;
			updates[3][0] = NF - 1;
			updates[3][1] = jfrom;
		} else {
			position[3][jfrom] = position[0][jfrom];
			updates[2][0] = 3;
			updates[2][1] = jfrom;

			position[0][jfrom] = PIECE_NONE;
			updates[3][0] = 0;
			updates[3][1] = jfrom;
		}
	} else if (hints & HINT_EN_PASSANT) {
		ply.taken = position[ito][jfrom];

		position[ito][jfrom] = PIECE_NONE;
		updates[2][0] = ito;
		updates[2][1] = jfrom;
	} else if (hints & HINT_SET_EN_PASSANT_FIELD) {
		fep[0] = ifrom;
		fep[1] = (jfrom + jto) / 2;
	} else if (hints & HINT_PROMOTION) {
		position[ito][jto] = (position[ito][jto] & COLORMASK) | prompiece;
	}

	if (hints & HINT_DEL_CASTLERIGHT_QUEENSIDE)
		castlerights[c] &= ~CASTLERIGHT_QUEENSIDE;
	if (hints & HINT_DEL_CASTLERIGHT_KINGSIDE)
		castlerights[c] &= ~CASTLERIGHT_KINGSIDE;

	/* count drawish plies for fifty-move rule */
	if ((ply.taken & PIECEMASK) == PIECE_NONE && ply.p != PIECE_PAWN) {
		++drawish_plies_num;
	} else {
		drawish_plies_num = 0;
	}

	/* update fullmove number */
	if (active_color == COLOR_BLACK)
		++nmove;

	/* update active color */
	active_color = OPP_COLOR(active_color);

	/* add ply to list */
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
	sqid ifrom = ply.from[0];
	sqid jfrom = ply.from[1];
	sqid ito = ply.to[0];
	sqid jto = ply.to[1];
	color_t c = position[ito][jto] & COLORMASK;

	memset(updates, 0xff, sizeof(updates));
	memcpy(fep, ply.fep, sizeof(fep));

	/* remove ply from list */
	--pliesnum;

	/* restore fifty-move rule count */
	drawish_plies_num = ply.ndrawplies;

	/* undo hints */
	if (ply.hints & HINT_DEL_CASTLERIGHT_QUEENSIDE)
		castlerights[c] |= CASTLERIGHT_QUEENSIDE;
	if (ply.hints & HINT_DEL_CASTLERIGHT_KINGSIDE)
		castlerights[c] |= CASTLERIGHT_KINGSIDE;

	if (ply.hints & HINT_CASTLE) {
		if (ito > ifrom) {
			position[NF - 1][jfrom] = position[NF - 3][jfrom];
			updates[3][0] = NF - 1;
			updates[3][1] = jfrom;

			position[NF - 3][jfrom] = PIECE_NONE;
			updates[2][0] = NF - 3;
			updates[2][1] = jfrom;

		} else {
			position[0][jfrom] = position[3][jfrom];
			updates[3][0] = 0;
			updates[3][1] = jfrom;

			position[3][jfrom] = PIECE_NONE;
			updates[2][0] = 3;
			updates[2][1] = jfrom;
		}
	} else if (ply.hints & HINT_EN_PASSANT) {
		position[ito][jfrom] = ply.taken;
		updates[2][0] = jto;
		updates[2][1] = jfrom;
	} else if (ply.hints & HINT_PROMOTION) {
		position[ito][jto] = (position[ito][jto] & COLORMASK) | PIECE_PAWN;
	}

	/* update fullmove number */
	if (active_color == COLOR_WHITE)
		--nmove;

	/* update active color */
	active_color = OPP_COLOR(active_color);

	/* undo bare ply */
	position[ifrom][jfrom] = position[ito][jto];
	memcpy(updates[1], ply.from, sizeof(updates[1]));

	position[ito][jto] = (ply.hints & HINT_EN_PASSANT) ? PIECE_NONE : ply.taken;
	memcpy(updates[1], ply.to, sizeof(updates[0]));
}

static int piece_has_legal_ply(sqid ipiece, sqid jpiece)
{
	piece_t p = position[ipiece][jpiece] & PIECEMASK;
	color_t c = position[ipiece][jpiece] & COLORMASK;
	sqid iking, jking;
	if (p != PIECE_KING)
		get_king(c, &iking, &jking);
	for (int j = 0; j < NF; ++j) {
		for (int i = 0; i < NF; ++i) {
			if (i == ipiece && j == jpiece)
				continue;

			int hints;
			if (!is_pseudolegal_ply(p, ipiece, jpiece, i, j, &hints))
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
static int has_legal_ply(color_t color)
{
	for (sqid j = 0; j < NF; ++j) {
		for (sqid i = 0; i < NF; ++i) {
			if ((position[i][j] & PIECEMASK) == PIECE_NONE
					|| (position[i][j] & COLORMASK) != color)
				continue;
			if (piece_has_legal_ply(i, j))
				return 1;
		}
	}
	return 0;
}

int game_init(const char *fen)
{
	int err = game_load_fen(fen);
	assert(!err);

	//active_color = COLOR_WHITE;
	//memset(fep, 0xff, sizeof(fep));
	//castlerights[0] = CASTLERIGHT_KINGSIDE | CASTLERIGHT_QUEENSIDE;
	//castlerights[1] = CASTLERIGHT_KINGSIDE | CASTLERIGHT_QUEENSIDE;

	//sqid pawnrows[2];
	//sqid piecerows[2];
	//piecerows[0] = 0;
	//piecerows[1] = NF - 1;
	//pawnrows[0] = 1;
	//pawnrows[1] = NF - 2;

	//for (int j = 0; j < NUM_COLORS; ++j) {
	//	position[0][piecerows[j]] = PIECE_ROOK | j;
	//	position[1][piecerows[j]] = PIECE_KNIGHT | j;
	//	position[2][piecerows[j]] = PIECE_BISHOP | j;
	//	position[3][piecerows[j]] = PIECE_QUEEN | j;
	//	position[4][piecerows[j]] = PIECE_KING | j;
	//	position[5][piecerows[j]] = PIECE_BISHOP | j;
	//	position[6][piecerows[j]] = PIECE_KNIGHT | j;
	//	position[7][piecerows[j]] = PIECE_ROOK | j;
	//	for (int i = 0; i < NF; ++i) {
	//		position[i][pawnrows[j]] = PIECE_PAWN | j;
	//	}
	//}

	pliessize = PLIES_BUFSIZE;
	plies = malloc(pliessize * sizeof(*plies));
	if (!plies)
		return -1;
	return 0;
}
void game_terminate(void)
{
	free(plies);
}

int game_exec_ply(sqid ifrom, sqid jfrom, sqid ito, sqid jto, piece_t prompiece)
{
	piece_t piece = position[ifrom][jfrom] & PIECEMASK;

	/* check if ply is pseudolegal */
	int hints;
	if (!is_pseudolegal_ply(piece, ifrom, jfrom, ito, jto, &hints))
		return 1;

	/* exec ply */
	exec_ply(ifrom, jfrom, ito, jto, hints, prompiece);

	/* check if ply is legal */
	sqid iking, jking;
	get_king(active_color, &iking, &jking);
	if (is_king_in_check(active_color, iking, jking)) {
		undo_last_ply();
		return 1;
	}

	/* indicate that prompiece must be given */
	if ((hints & HINT_PROMOTION) && prompiece == PIECE_NONE) {
		undo_last_ply();
		return 2;
	}

	return 0;
}
void game_undo_last_ply(void)
{
	undo_last_ply();
}

color_t game_get_active_color()
{
	return active_color;
}
piece_t game_get_piece(sqid i, sqid j)
{
	return position[i][j] & PIECEMASK;
}
color_t game_get_color(sqid i, sqid j)
{
	return position[i][j] & COLORMASK;
}
squareinfo_t game_get_squareinfo(sqid i, sqid j)
{
	return position[i][j];
}
int game_get_move_number(void)
{
	return pliesnum / 2;
}
int game_is_stalemate(void)
{
	sqid iking, jking;
	get_king(active_color, &iking, &jking);

	if (is_king_in_check(active_color, iking, jking))
		return 0;

	for (sqid j = 0; j < NF; ++j) {
		for (sqid i = 0; i < NF; ++i) {
			if ((position[i][j] & PIECEMASK) == PIECE_NONE
					|| (position[i][j] & COLORMASK) != active_color)
				continue;
			if (piece_has_legal_ply(i, j))
				return 0;
		}
	}

	return 1;
}
int game_is_checkmate(void)
{
	sqid iking, jking;
	get_king(active_color, &iking, &jking);

	if (!is_king_in_check(active_color, iking, jking))
		return 0;

	for (sqid j = 0; j < NF; ++j) {
		for (sqid i = 0; i < NF; ++i) {
			if ((position[i][j] & PIECEMASK) == PIECE_NONE
					|| (position[i][j] & COLORMASK) != active_color)
				continue;
			if (piece_has_legal_ply(i, j))
				return 0;
		}
	}

	return 1;
}
int game_is_movable_piece_at(sqid i, sqid j)
{
	return ((position[i][j] & PIECEMASK) != PIECE_NONE)
		&& (position[i][j] & COLORMASK) == active_color;
}
int game_last_ply_was_capture(void)
{
	return plies[pliesnum - 1].taken != PIECE_NONE;
}

int game_has_sufficient_mating_material(color_t color)
{
	int hasking = 0;
	int nbishops = 0;
	int nknights = 0;
	for (int j = 0; j < NF; ++j) {
		for (int i = 0; i < NF; ++i) {
			switch (position[i][j]) {
			case PIECE_KING:
				hasking = 1;
				break;
			case PIECE_QUEEN:
				return 1;
			case PIECE_ROOK:
				return 1;
			case PIECE_BISHOP:
				++nbishops;
			case PIECE_KNIGHT:
				++nknights;
			case PIECE_PAWN:
				return 1;
			}
		}
	}
	assert(hasking);

	if (nbishops + nknights > 1)
		return 1;
	return 0;
}
void game_get_status(status_t *externstatus)
{
	/* surrender? */
	if (*externstatus == STATUS_SURRENDER_WHITE || *externstatus == STATUS_SURRENDER_BLACK)
		return;

	/* timeout? */
	if (*externstatus == STATUS_TIMEOUT_WHITE) {
		*externstatus = game_has_sufficient_mating_material(COLOR_BLACK) ?
			*externstatus : STATUS_DRAW_MATERIAL_VS_TIMEOUT;
		return;
	} else if (*externstatus == STATUS_TIMEOUT_BLACK) {
		*externstatus = game_has_sufficient_mating_material(COLOR_WHITE) ?
			*externstatus : STATUS_DRAW_MATERIAL_VS_TIMEOUT;
		return;
	}

	assert(*externstatus == STATUS_MOVING_WHITE || *externstatus == STATUS_MOVING_BLACK);

	/* check- or stalemate? */
	if (!has_legal_ply(active_color)) {
		sqid iking, jking;
		get_king(active_color, &iking, &jking);

		if (is_king_in_check(active_color, iking, jking)) {
			*externstatus = active_color ?
				STATUS_CHECKMATE_BLACK : STATUS_CHECKMATE_WHITE;
		} else {
			*externstatus = STATUS_DRAW_STALEMATE;
		}
		return;
	}

	/* draw by insufficient material? */
	if (!game_has_sufficient_mating_material(COLOR_WHITE)
			&& !game_has_sufficient_mating_material(COLOR_BLACK)) {
		*externstatus = STATUS_DRAW_MATERIAL;
		return;
	}

	/* draw by repetition? */
	/* TODO: Adapt to official rules */
	/* TODO: maybe optimize: https://www.chessprogramming.org/Repetitions */
	for (int i = 0; i < pliesnum; ++i) {
		int n = 0;
		for (int j = i + 1; j < pliesnum; ++j) {
			if (memcmp(plies[i].position, plies[j].position,
						sizeof(plies[i].position)) == 0
					&& memcmp(plies[i].fep, plies[j].fep,
						sizeof(plies[i].fep)) == 0
					&& memcmp(plies[i].castlerights, plies[j].castlerights,
						sizeof(plies[i].castlerights)) == 0)
				++n;
		}
		if (n >= 2) {
			*externstatus = STATUS_DRAW_REPETITION;
			return;
		}
	}

	/* draw by fifty move rule? */
	if (drawish_plies_num >= DRAWISH_MOVES_MAX * 2) {
		*externstatus = STATUS_DRAW_FIFTY_MOVES;
		return;
	}

	/* just moving */
	*externstatus = active_color ? STATUS_MOVING_BLACK : STATUS_MOVING_WHITE;
}
void game_get_updates(sqid u[][2])
{
	memcpy(u, updates, 2 * NUM_UPDATES_MAX * sizeof(sqid));
}

int game_load_fen(const char *s)
{
	if (!parse_fen(s, position, &active_color, castlerights, fep, &drawish_plies_num, &nmove))
		return 1;
	return 0;
}
void game_get_fen(char *s)
{
	format_fen(position, active_color, castlerights, fep, drawish_plies_num, nmove, s);
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
static void print_ply(plyinfo_t *m)
{
	char squarefrom[3] = {0};
	char squareto[3] = {0};
	squarefrom[0] = ROW_CHAR(m->from[0]);
	squarefrom[1] = COL_CHAR(m->from[1]);
	squareto[0] = ROW_CHAR(m->to[0]);
	squareto[1] = COL_CHAR(m->to[1]);

	//printf("squares: %s%s-%s\n", piece_symbols[PIECE_IDX(m->p)], squarefrom, squareto);
	//printf("squares: %s%i%i-%i%i\n", piece_symbols[PIECE_IDX(m->p)], m->ifrom, m->jfrom, m->ito, m->jto);

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
			piece_t piece = position[i][j] & PIECEMASK;
			if (piece == PIECE_NONE) {
				printf("  ");
				continue;
			}

			int p = PIECE_IDX(piece);
			char c = (position[i][j] & COLORMASK) ?
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
