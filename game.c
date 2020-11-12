#include <stdlib.h>
// debug
#include <stdio.h>
#include <string.h>

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

#define OPP_COLOR(c) ((!(c)) & COLORMASK)
#define PIECE_IDX(p) ((p) / 2 - 1)

// debug
#define COL_CHAR(i) ('a' + i)
#define ROW_CHAR(j) ('1' + (NF - j - 1))
const char *piece_names[] = {
	"king",
	"queen",
	"rook",
	"bishop",
	"knight",
	"pawn",
};
const char *piece_symbols[] = {
	"K",
	"Q",
	"R",
	"B",
	"N",
	"",
};
static void print_move(const char *fmt, int ifrom, int jfrom, int ito, int jto, int piece) {
	char s[7];
	char field[3] = { 0 };
	field[0] = COL_CHAR(ifrom);
	field[1] = ROW_CHAR(jfrom);
	snprintf(s, 7, "%s%s-", piece_symbols[PIECE_IDX(piece)], field);

	field[0] = COL_CHAR(ito);
	field[1] = ROW_CHAR(jto);
	snprintf(s + strlen(s), 3, "%s", field);
	printf(fmt, s);
}

struct move_info {
	int p;
	int ifrom, jfrom;
	int ito, jto;
	int taken;
	int hints;
	move_info *prev;
	move_info *next;
};

static int is_possible_king_move(int ifrom, int jfrom, int ito, int jto, int *flags);
static int is_possible_queen_move(int ifrom, int jfrom, int ito, int jto, int *flags);
static int is_possible_rook_move(int ifrom, int jfrom, int ito, int jto, int *flags);
static int is_possible_bishop_move(int ifrom, int jfrom, int ito, int jto, int *flags);
static int is_possible_knight_move(int ifrom, int jfrom, int ito, int jto, int *flags);
static int is_possible_pawn_move(int ifrom, int jfrom, int ito, int jto, int *flags);

int (*is_possible_move[NUM_PIECES])(int, int, int, int, int *) = {
	is_possible_king_move,
	is_possible_queen_move,
	is_possible_rook_move,
	is_possible_bishop_move,
	is_possible_knight_move,
	is_possible_pawn_move,
};

int playing_color;
char board[NF][NF];
int updates[NUM_UPDATES_MAX][2];

move_info *movefirst;
move_info *movelast;

static int is_possible_king_move(int ifrom, int jfrom, int ito, int jto, int *hints)
{
	int color = board[ifrom][jfrom] & COLORMASK;
	if ((board[ito][jto] != PIECE_NONE) && (board[ito][jto] & COLORMASK) == color)
		return 0;

	if (hints) {
		*hints = 0;
		if (board[ifrom][jfrom] & ATTR_CAN_CASTLE)
			*hints |= HINT_DEL_ATTR_CAN_CASTLE;
	}

	if (abs(ito - ifrom) <= 1 && abs(jto - jfrom) <= 1) {
		return 1;
	} else if (board[ito][jto] & ATTR_CAN_CASTLE && (ito == 2 || ito == NF - 2)) {
		if (color == COLOR_WHITE && jto != 0)
			return 0;
		if (color == COLOR_BLACK && jto != NF - 1)
			return 0;

		int step = 2 * (ito > ifrom) - 1;
		int irook = (NF - 1) * (ito > ifrom);
		if (!(board[irook][jto] & (color | PIECE_ROOK | ATTR_CAN_CASTLE)))
			return 0;
		for (int k = 1; k < abs(irook - ifrom); ++k) {
			if ((board[ito + k * step][jto] & PIECEMASK) != PIECE_NONE)
				return 0;
		}

		if (hints)
			*hints |= HINT_CASTLE;
		return 1;
	}

	return 0;
}
static int is_possible_queen_move(int ifrom, int jfrom, int ito, int jto, int *hints)
{
	int color = board[ifrom][jfrom] & COLORMASK;
	if ((board[ito][jto] != PIECE_NONE) && (board[ito][jto] & COLORMASK) == color)
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
		for (int i = MIN(jfrom, jto) + 1; i < MAX(jfrom, jto); ++i) {
			if ((board[i][jfrom] & PIECEMASK) != PIECE_NONE)
				return 0;
		}
		return 1;
	}

	return 0;
}
static int is_possible_rook_move(int ifrom, int jfrom, int ito, int jto, int *hints)
{
	int color = board[ifrom][jfrom] & COLORMASK;
	if ((board[ito][jto] != PIECE_NONE) && (board[ito][jto] & COLORMASK) == color)
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
static int is_possible_bishop_move(int ifrom, int jfrom, int ito, int jto, int *hints)
{
	int color = board[ifrom][jfrom] & COLORMASK;
	if ((board[ito][jto] != PIECE_NONE) && (board[ito][jto] & COLORMASK) == color)
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
static int is_possible_knight_move(int ifrom, int jfrom, int ito, int jto, int *hints)
{
	int color = board[ifrom][jfrom] & COLORMASK;
	if ((board[ito][jto] != PIECE_NONE) && (board[ito][jto] & COLORMASK) == color)
		return 0;

	if (hints)
		*hints = 0;

	if ((abs(ito - ifrom) + abs(jto - jfrom)) == 3)
		return 1;

	return 0;
}
static int is_possible_pawn_move(int ifrom, int jfrom, int ito, int jto, int *hints)
{
	int color = board[ifrom][jfrom] & COLORMASK;
	if ((board[ito][jto] != PIECE_NONE) && (board[ito][jto] & COLORMASK) == color)
		return 0;

	if (hints)
		*hints = 0;

	int di = ito - ifrom;
	int dj = jto - jfrom;

	int step = 1 - 2 * color;
	int promrow = (1 - color) * (NF - 1);
	int pawnrow = color * (NF - 2) + (1 - color);
	if (dj == step && di == 0) { /* normal step */
		if ((board[ito][jto] & PIECEMASK) == PIECE_NONE) {
			if (hints && jto == promrow)
				*hints |= HINT_PROMOTION;
			return 1;
		}
	}
	else if (dj == step && abs(di) == 1) { /* diagonal step with take */
		if ((board[ito][jto] & PIECEMASK) != PIECE_NONE
				&& (board[ito][jto] & COLORMASK) != color) {
			return 1;
		} else if ((board[ito][jto - step] & PIECEMASK) == PIECE_PAWN
				&& (board[ito][jto - step] & COLORMASK) != color
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

static void move(int ifrom, int jfrom, int ito, int jto, int hints, move_info *m)
{
	m->p = board[ifrom][jfrom] & PIECEMASK;
	m->ifrom = ifrom;
	m->jfrom = jfrom;
	m->ito = ito;
	m->jto = jto;
	m->taken = board[ito][jto];
	m->hints = hints;
	memset(updates, 0xff, 2 * NUM_UPDATES_MAX * sizeof(int)); /* set to -1 */

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
static void undo_move(move_info *m)
{
	memset(updates, 0xff, 2 * NUM_UPDATES_MAX * sizeof(int)); /* set to -1 */

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

static void get_king(int c, int *i, int *j)
{
	for (int l = 0; l < NF; ++l) {
		for (int k = 0; k < NF; ++k) {
			if ((board[k][l] & PIECEMASK) == PIECE_KING
					&& (board[k][l] & COLORMASK) == c) {
				*i = k;
				*j = l;
				return;
			}
		}
	}
}
static int is_king_in_check(int c, int iking, int jking)
{
	for (int j = 0; j < NF; ++j) {
		for (int i = 0; i < NF; ++i) {
			if ((board[i][j] & PIECEMASK) == PIECE_NONE
					|| (board[i][j] & COLORMASK) == c)
				continue;

			int piece = board[i][j] & PIECEMASK;
			if (is_possible_move[PIECE_IDX(piece)](i, j, iking, jking, NULL))
				return 1;
		}
	}
	return 0;
}

/* possible = Move is allowed even if king is in check after
   legal = Move is allowed, king is not in check after */
static int has_legal_move(int ipiece, int jpiece)
{
	int p = board[ipiece][jpiece] & PIECEMASK;
	int c = board[ipiece][jpiece] & COLORMASK;
	int iking, jking;
	if (p != PIECE_KING)
		get_king(c, &iking, &jking);
	for (int j = 0; j < NF; ++j) {
		for (int i = 0; i < NF; ++i) {
			if (i == ipiece && j == jpiece)
				continue;

			int hints;
			if (!is_possible_move[PIECE_IDX(p)](ipiece, jpiece, i, j, &hints))
				continue;

			move_info m;
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

void game_init_board(int c)
{
	playing_color = c;

	int pawnrows[2];
	int piecerows[2];
	if (c == COLOR_WHITE) {
		piecerows[0] = 0;
		piecerows[1] = NF - 1;
		pawnrows[0] = 1;
		pawnrows[1] = NF - 2;
	} else {
		piecerows[0] = NF - 1;
		piecerows[1] = 0;
		pawnrows[0] = NF - 2;
		pawnrows[1] = 1;
	}

	for (int j = 0; j < NUM_COLORS; ++j) {
		board[0][piecerows[j]] = PIECE_ROOK | j | ATTR_CAN_CASTLE;
		board[1][piecerows[j]] = PIECE_KNIGHT | j | ATTR_CAN_CASTLE;
		board[2][piecerows[j]] = PIECE_BISHOP | j;
		board[3][piecerows[j]] = PIECE_QUEEN | j;
		board[4][piecerows[j]] = PIECE_KING | j;
		board[5][piecerows[j]] = PIECE_BISHOP | j;
		board[6][piecerows[j]] = PIECE_KNIGHT | j;
		board[7][piecerows[j]] = PIECE_ROOK | j | ATTR_CAN_CASTLE;
		for (int i = 0; i < NF; ++i) {
			board[i][pawnrows[j]] = PIECE_PAWN | j;
		}
	}
}
void game_init_test_board(int c)
{
	board[3][0] = PIECE_QUEEN | COLOR_WHITE;
	board[4][0] = PIECE_KING | COLOR_WHITE;

	board[4][NF - 1] = PIECE_KING | COLOR_BLACK;
}
void game_terminate()
{
	for (move_info *m = movelast; m; m = m->prev) {
		free(m);
	}
}

int game_is_movable_piece(int i, int j)
{
	return ((board[i][j] & PIECEMASK) != PIECE_NONE)
		&& (board[i][j] & COLORMASK) == playing_color;
}
int game_move(int ifrom, int jfrom, int ito, int jto)
{
	int piece = board[ifrom][jfrom] & PIECEMASK;

	int hints;
	if (!is_possible_move[PIECE_IDX(piece)](ifrom, jfrom, ito, jto, &hints))
		return 1;

	move_info *m = malloc(sizeof(move_info));
	if (!m)
		return -1;
	move(ifrom, jfrom, ito, jto, hints, m);

	int iking, jking;
	get_king(playing_color, &iking, &jking);
	if (is_king_in_check(playing_color, iking, jking)) {
		undo_move(m);
		free(m);
		return 1;
	}

	playing_color = OPP_COLOR(playing_color);
	return 0;
}
void game_undo_last_move()
{
	undo_move(movelast);
}
int game_is_stalemate()
{
	int iking, jking;
	get_king(playing_color, &iking, &jking);

	if (is_king_in_check(playing_color, iking, jking))
		return 0;

	for (int j = 0; j < NF; ++j) {
		for (int i = 0; i < NF; ++i) {
			if ((board[i][j] & PIECEMASK) == PIECE_NONE
					|| (board[i][j] & COLORMASK) != playing_color)
				continue;
			if (has_legal_move(i, j))
				return 0;
		}
	}

	return 1;
}
int game_is_checkmate()
{
	int iking, jking;
	get_king(playing_color, &iking, &jking);

	if (!is_king_in_check(playing_color, iking, jking))
		return 0;

	for (int j = 0; j < NF; ++j) {
		for (int i = 0; i < NF; ++i) {
			if ((board[i][j] & PIECEMASK) == PIECE_NONE
					|| (board[i][j] & COLORMASK) != playing_color)
				continue;
			if (has_legal_move(i, j))
				return 0;
		}
	}

	return 1;
}

int game_get_piece(int i, int j)
{
	return board[i][j] & PIECEMASK;
}
int game_get_color(int i, int j)
{
	return board[i][j] & COLORMASK;
}
void game_get_updates(int u[][2])
{
	memcpy(u, updates, 2 * NUM_UPDATES_MAX * sizeof(int));
}
