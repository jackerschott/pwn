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

#define ATTR_CAN_CASTLE 		16
#define ATTR_TAKEABLE_EN_PASSANT 	32

#define HINT_CASTLE 				(1 << 0)
#define HINT_DEL_ATTR_CAN_CASTLE 		(1 << 1)
#define HINT_PROMOTION 				(1 << 2)
#define HINT_EN_PASSANT 			(1 << 3)
#define HINT_ADD_ATTR_TAKEABLE_EN_PASSANT 	(1 << 4)

#define MAX(x, y) (((x) > (y)) ? (x) : (y))
#define MIN(x, y) (((x) < (y)) ? (x) : (y))
#define SGN(i) ((i > 0) - (i < 0))

#define PIECE_IDX(p) ((p) / 2 - 1)
#define PIECE_BY_IDX(i) (2 * (i) + 2)
#define COL_CHAR(i) ('a' + i)
#define ROW_CHAR(j) ('1' + j)

#define PIECENAME_BUF_SIZE 16

struct moveinfo_t {
	piece_t p;
	fid ifrom, jfrom;
	fid ito, jto;
	fieldinfo_t taken;
	int hints;
	moveinfo_t *prev;
	moveinfo_t *next;
};

static const char *piece_names[] = {
	"king",
	"queen",
	"rook",
	"bishop",
	"knight",
	"pawn",
};
static const char *piece_symbols[] = {
	"K",
	"Q",
	"R",
	"B",
	"N",
	"",
};
static const char *hintstrs[] = {
	"HINT_CASTLE",
	"HINT_DEL_ATTR_CAN_CASTLE",
	"HINT_PROMOTION",
	"HINT_EN_PASSANT",
	"HINT_ADD_ATTR_TAKEABLE_EN_PASSANT",
};

static const char *promotion_prompt_cmd[] = { "dmenu", "-p", "promote to:" };

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

	printf("fields: %s%s-%s\n", piece_symbols[PIECE_IDX(m->p)], fieldfrom, fieldto);
	//printf("fields: %s%i%i-%i%i\n", piece_symbols[PIECE_IDX(m->p)], m->ifrom, m->jfrom, m->ito, m->jto);

	printf("taken: ");
	if ((m->taken & PIECEMASK) != PIECE_NONE)
		printf("%s", piece_names[PIECE_IDX(m->taken & PIECEMASK)]);
	printf("\n");

	printf("hints: ");
	print_hints(m->hints);
	printf("\n\n");
}

static int is_possible_king_move(fid ifrom, fid jfrom, fid ito, fid jto, int *flags);
static int is_possible_queen_move(fid ifrom, fid jfrom, fid ito, fid jto, int *flags);
static int is_possible_rook_move(fid ifrom, fid jfrom, fid ito, fid jto, int *flags);
static int is_possible_bishop_move(fid ifrom, fid jfrom, fid ito, fid jto, int *flags);
static int is_possible_knight_move(fid ifrom, fid jfrom, fid ito, fid jto, int *flags);
static int is_possible_pawn_move(fid ifrom, fid jfrom, fid ito, fid jto, int *flags);

static int is_king_in_check(color_t c, fid iking, fid jking);

static int (*is_possible_move[NUM_PIECES])(fid, fid, fid, fid, int *) = {
	is_possible_king_move,
	is_possible_queen_move,
	is_possible_rook_move,
	is_possible_bishop_move,
	is_possible_knight_move,
	is_possible_pawn_move,
};

static color_t playing_color;
static fieldinfo_t board[NF][NF];
static fid updates[NUM_UPDATES_MAX][2];

static moveinfo_t *movefirst;
static moveinfo_t *movelast;

static int is_possible_king_move(fid ifrom, fid jfrom, fid ito, fid jto, int *hints)
{
	color_t c = board[ifrom][jfrom] & COLORMASK;
	if ((board[ito][jto] != PIECE_NONE) && (board[ito][jto] & COLORMASK) == c)
		return 0;

	if (hints) {
		*hints = 0;
		if (board[ifrom][jfrom] & ATTR_CAN_CASTLE)
			*hints |= HINT_DEL_ATTR_CAN_CASTLE;
	}

	if (abs(ito - ifrom) <= 1 && abs(jto - jfrom) <= 1) {
		return 1;
	} else if (board[ifrom][jfrom] & ATTR_CAN_CASTLE && (ito == 2 || ito == NF - 2)) {
		if (c == COLOR_WHITE && jto != 0)
			return 0;
		if (c == COLOR_BLACK && jto != NF - 1)
			return 0;

		int step = 2 * (ito > ifrom) - 1;
		int irook = (NF - 1) * (ito > ifrom);
		if (!(board[irook][jto] & (c | PIECE_ROOK | ATTR_CAN_CASTLE)))
			return 0;
		for (int k = 1; k < abs(irook - ifrom); ++k) {
			if ((board[ifrom + k * step][jto] & PIECEMASK) != PIECE_NONE)
				return 0;
		}

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
		if (board[ifrom][jfrom] & ATTR_CAN_CASTLE)
			*hints |= HINT_DEL_ATTR_CAN_CASTLE;
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
	}
	else if (dj == step && abs(di) == 1) { /* diagonal step with take */
		if ((board[ito][jto] & PIECEMASK) != PIECE_NONE
				&& (board[ito][jto] & COLORMASK) != c) {
			return 1;
		} else if ((board[ito][jto - step] & PIECEMASK) == PIECE_PAWN
				&& (board[ito][jto - step] & COLORMASK) != c
				&& board[ito][jto - step] & ATTR_TAKEABLE_EN_PASSANT) {
			if (hints)
				*hints |= HINT_EN_PASSANT;
			return 1;
		}
	} else if (jfrom == pawnrow && dj == 2 * step && di == 0) { /* 2 field step from pawnrow */
		if ((board[ito][jto - step] & PIECEMASK) == PIECE_NONE
				&& (board[ito][jto] & PIECEMASK) == PIECE_NONE) {
			if (hints)
				*hints |= HINT_ADD_ATTR_TAKEABLE_EN_PASSANT;
			return 1;
		}
	}

	return 0;
}

static void move(fid ifrom, fid jfrom, fid ito, fid jto, int hints, moveinfo_t *m)
{
	m->p = board[ifrom][jfrom] & PIECEMASK;
	m->ifrom = ifrom;
	m->jfrom = jfrom;
	m->ito = ito;
	m->jto = jto;
	m->taken = board[ito][jto];
	m->hints = hints;
	memset(updates, 0xff, 2 * NUM_UPDATES_MAX * sizeof(fid)); /* set to -1 */

	board[ito][jto] = board[ifrom][jfrom];
	updates[0][0] = ifrom;
	updates[0][1] = jfrom;

	board[ifrom][jfrom] = PIECE_NONE;
	updates[1][0] = ito;
	updates[1][1] = jto;

	if (hints & HINT_CASTLE) {
		if (ito > ifrom) {
			board[NF - 3][jfrom] = board[NF - 1][jfrom] & ~ATTR_CAN_CASTLE;
			updates[2][0] = NF - 3;
			updates[2][1] = jfrom;

			board[NF - 1][jfrom] = PIECE_NONE;
			updates[3][0] = NF - 1;
			updates[3][1] = jfrom;
		} else {
			board[3][jfrom] = board[0][jfrom] & ~ATTR_CAN_CASTLE;
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
	} else if (hints & HINT_ADD_ATTR_TAKEABLE_EN_PASSANT) {
		board[ito][jto] |= ATTR_TAKEABLE_EN_PASSANT;
	}

	if (hints & HINT_DEL_ATTR_CAN_CASTLE) {
		board[ito][jto] &= ~ATTR_CAN_CASTLE;
	}

	if (movelast && movelast->hints & HINT_ADD_ATTR_TAKEABLE_EN_PASSANT) {
		board[movelast->ito][movelast->jto] &= ~ATTR_TAKEABLE_EN_PASSANT;
	}

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
}
static void undo_move(moveinfo_t *m)
{
	memset(updates, 0xff, 2 * NUM_UPDATES_MAX * sizeof(fid)); /* set to -1 */

	if (m->prev) {
		movelast = m->prev;
		movelast->next = NULL;
	} else {
		movefirst = NULL;
		movelast = NULL;
	}

	if (movelast && movelast->hints & HINT_ADD_ATTR_TAKEABLE_EN_PASSANT) {
		board[movelast->ito][movelast->jto] |= ATTR_TAKEABLE_EN_PASSANT;
	}

	if (m->hints & HINT_DEL_ATTR_CAN_CASTLE) {
		board[m->ifrom][m->jfrom] |= ATTR_CAN_CASTLE;
	}

	if (m->hints & HINT_CASTLE) {
		if (m->ito > m->ifrom) {
			board[NF - 1][m->jfrom] = board[NF - 3][m->jfrom] | ATTR_CAN_CASTLE;
			updates[3][0] = NF - 1;
			updates[3][1] = m->jfrom;

			board[NF - 3][m->jfrom] = PIECE_NONE;
			updates[2][0] = NF - 3;
			updates[2][1] = m->jfrom;

		} else {
			board[0][m->jfrom] = board[3][m->jfrom] | ATTR_CAN_CASTLE;
			updates[3][0] = 0;
			updates[3][1] = m->jfrom;

			board[3][m->jfrom] = PIECE_NONE;
			updates[2][0] = 3;
			updates[2][1] = m->jfrom;
		}
	}
	else if (m->hints & HINT_EN_PASSANT) {
		board[m->ito][m->jfrom] = m->taken;
		updates[2][0] = m->jto;
		updates[2][1] = m->jfrom;

		board[m->ito][m->jto] = PIECE_NONE;
		updates[0][0] = m->ito;
		updates[0][1] = m->jto;
	}
	else if (m->hints & HINT_ADD_ATTR_TAKEABLE_EN_PASSANT) {
		board[m->ifrom][m->jfrom] &= ~ATTR_TAKEABLE_EN_PASSANT;
	}

	board[m->ifrom][m->jfrom] = board[m->ito][m->jto];
	updates[1][0] = m->ifrom;
	updates[1][1] = m->jfrom;

	if (!(m->hints & HINT_EN_PASSANT)) {
		board[m->ito][m->jto] = m->taken | OPP_COLOR(playing_color);
		updates[0][0] = m->ito;
		updates[0][1] = m->jto;
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
					|| (board[i][j] & COLORMASK) == c
					|| (board[i][j] & PIECEMASK) == PIECE_KING)
				continue;

			piece_t p = board[i][j] & PIECEMASK;
			if (is_possible_move[PIECE_IDX(p)](i, j, iking, jking, NULL))
				return 1;
		}
	}
	return 0;
}

/* possible = Move is allowed even if king is in check after
   legal = Move is allowed, king is not in check after */
static int has_legal_move(fid ipiece, fid jpiece)
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
			move(ipiece, jpiece, i, j, hints, &m);

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

int prompt_promotion_piece(void)
{
	char name[PIECENAME_BUF_SIZE];

	int fopts[2];
	int fans[2];
	if (pipe(fopts))
		return -1;

	if (pipe(fans)) {
		SYSERR();
		close(fopts[0]);
		close(fopts[1]);
		return -1;
	}

	pid_t pid = fork();
	if (pid == -1) {
		SYSERR();
		close(fans[0]);
		close(fans[1]);
		close(fopts[0]);
		close(fopts[1]);
		return -1;
	}
	
	if (pid == 0) {
		close(fopts[1]);
		close(fans[0]);

		if (dup2(fopts[0], STDIN_FILENO) == -1) {
			SYSERR();
			close(fopts[0]);
			close(fans[1]);
			exit(-1);
		}
		if (dup2(fans[1], STDOUT_FILENO) == -1) {
			SYSERR();
			close(fopts[0]);
			close(fans[1]);
			exit(-1);
		}

		execvp(promotion_prompt_cmd[0], (char *const *)promotion_prompt_cmd);
		SYSERR();
		exit(-1);
	} else {
		close(fopts[0]);
		close(fans[1]);

		if (write(fopts[1], piece_names[1], strlen(piece_names[1])) == -1) {
			SYSERR();
			close(fopts[1]);
			close(fans[0]);
			return -1;
		}
		for (int i = 2; i < ARRSIZE(piece_names) - 1; ++i) {
			if (write(fopts[1], "\n", 1) == -1) {
				SYSERR();
				close(fopts[1]);
				close(fans[0]);
				return -1;
			}
			if (write(fopts[1], piece_names[i], strlen(piece_names[i])) == -1) {
				SYSERR();
				close(fopts[1]);
				close(fans[0]);
				return -1;
			}
		}
		close(fopts[1]);

		int n;
		if ((n = read(fans[0], name, PIECENAME_BUF_SIZE - 1)) == -1) {
			SYSERR();
			close(fans[0]);
			return -1;
		}
		close(fans[0]);
	}

	int i = -1;
	for (i = 1; i < ARRSIZE(piece_names) - 1; ++i) {
		if (strncmp(name, piece_names[i], strlen(piece_names[i])) == 0)
			break;
	}
	if (i == -1)
		return 0;

	return PIECE_BY_IDX(i);
}

void game_init_board(void)
{
	playing_color = COLOR_WHITE;

	fid pawnrows[2];
	fid piecerows[2];
	piecerows[0] = 0;
	piecerows[1] = NF - 1;
	pawnrows[0] = 1;
	pawnrows[1] = NF - 2;

	for (int j = 0; j < NUM_COLORS; ++j) {
		board[0][piecerows[j]] = PIECE_ROOK | j | ATTR_CAN_CASTLE;
		board[1][piecerows[j]] = PIECE_KNIGHT | j;
		board[2][piecerows[j]] = PIECE_BISHOP | j;
		board[3][piecerows[j]] = PIECE_QUEEN | j;
		board[4][piecerows[j]] = PIECE_KING | j | ATTR_CAN_CASTLE;
		board[5][piecerows[j]] = PIECE_BISHOP | j;
		board[6][piecerows[j]] = PIECE_KNIGHT | j;
		board[7][piecerows[j]] = PIECE_ROOK | j | ATTR_CAN_CASTLE;
		for (int i = 0; i < NF; ++i) {
			board[i][pawnrows[j]] = PIECE_PAWN | j;
		}
	}
}
void game_init_test_board(void)
{
	board[3][0] = PIECE_QUEEN | COLOR_WHITE;
	board[4][0] = PIECE_KING | COLOR_WHITE;

	board[4][NF - 1] = PIECE_KING | COLOR_BLACK;
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
		&& (board[i][j] & COLORMASK) == playing_color;
}
int game_move(fid ifrom, fid jfrom, fid ito, fid jto, piece_t prompiece)
{
	piece_t piece = board[ifrom][jfrom] & PIECEMASK;

	int hints;
	if (!is_possible_move[PIECE_IDX(piece)](ifrom, jfrom, ito, jto, &hints))
		return 1;

	moveinfo_t *m = malloc(sizeof(moveinfo_t));
	if (!m) {
		SYSERR();
		return -1;
	}
	move(ifrom, jfrom, ito, jto, hints, m);

	fid iking, jking;
	get_king(playing_color, &iking, &jking);
	if (is_king_in_check(playing_color, iking, jking)) {
		undo_move(m);
		free(m);
		return 1;
	}

	if (m->hints & HINT_PROMOTION) {
		if (prompiece == 0) {
			while ((prompiece = prompt_promotion_piece()) == 0) {};
			if (prompiece == -1) {
				undo_move(m);
				free(m);
				return -1;
			}
		}

		board[m->ito][m->jto] = playing_color | prompiece;
	}

	playing_color = OPP_COLOR(playing_color);
	return 0;
}
void game_undo_last_move(void)
{
	undo_move(movelast);
}
int game_is_stalemate(void)
{
	fid iking, jking;
	get_king(playing_color, &iking, &jking);

	if (is_king_in_check(playing_color, iking, jking))
		return 0;

	for (fid j = 0; j < NF; ++j) {
		for (fid i = 0; i < NF; ++i) {
			if ((board[i][j] & PIECEMASK) == PIECE_NONE
					|| (board[i][j] & COLORMASK) != playing_color)
				continue;
			if (has_legal_move(i, j))
				return 0;
		}
	}

	return 1;
}
int game_is_checkmate(void)
{
	fid iking, jking;
	get_king(playing_color, &iking, &jking);

	if (!is_king_in_check(playing_color, iking, jking))
		return 0;

	for (fid j = 0; j < NF; ++j) {
		for (fid i = 0; i < NF; ++i) {
			if ((board[i][j] & PIECEMASK) == PIECE_NONE
					|| (board[i][j] & COLORMASK) != playing_color)
				continue;
			if (has_legal_move(i, j))
				return 0;
		}
	}

	return 1;
}

color_t game_get_playing_color()
{
	return playing_color;
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

int game_save_board(const char *fname)
{
	int fboard = open(fname, O_WRONLY | O_CREAT, 0644);
	if (fboard == -1)
		return -1;

	write(fboard, board, NF * NF * sizeof(fieldinfo_t));
	write(fboard, &playing_color, sizeof(color_t));
	close(fboard);
	return 0;
}
int game_load_board(const char *fname)
{
	int fboard = open(fname, O_RDONLY);
	if (fboard == -1)
		return -1;

	read(fboard, board, NF * NF * sizeof(fieldinfo_t));
	read(fboard, &playing_color, sizeof(color_t));
	close(fboard);

	for (moveinfo_t *m = movelast; m; m = m->prev) {
		free(m);
	}
	movelast = NULL;
	movefirst = NULL;

	return 0;
}
