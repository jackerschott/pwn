#include <assert.h>

#include "test.h"
#include "game.h"
#include "notation.h"

static void print_fen()
{
	char buf[FEN_BUFSIZE];
	game_get_fen(buf);
	printf("%s\n", buf);
}

static unsigned int get_possible_position_num(int depth, int print)
{
	if (depth == 0)
		return 0;

	unsigned int npositions = 0;
	for (sqid n = 0; n < NF * NF * NF * NF; ++n) {
		int n1 = n / (NF * NF);
		int n2 = n % (NF * NF);

		int i = n1 % NF;
		int j = n1 / NF;
		int k = n2 % NF;
		int l = n2 / NF;

		if (!game_is_movable_piece_at(i, j))
			continue;

		piece_t piece = game_get_piece(i, j);
		int err = game_exec_ply(i, j, k, l, PIECE_NONE);
		if (err == 0) {
			int pr = i == 1 && j == 0 && k == 3 && l == 1;
			unsigned int npos = get_possible_position_num(depth - 1, pr) + 1;

			if (print) {
				sqid from[] = {i, j};
				sqid to[] = {k, l};
				char move[MOVE_MAXLEN];
				size_t len = format_move(piece, from, to, PIECE_NONE, move);
				move[len] = '\0';
				printf("%s: %u\n", move, npos);
			}

			game_undo_last_ply();
			npositions += npos;
			continue;
		} else if (err == 1) {
			continue;
		}

		for (int p = PIECE_IDX(PIECE_QUEEN); p <= PIECE_IDX(PIECE_KNIGHT); ++p) {
			err = game_exec_ply(i, j, k, l, PIECE_BY_IDX(p));
			assert(!err);
			unsigned int npos = get_possible_position_num(depth - 1, 0) + 1;

			if (print) {
				sqid from[] = {i, j};
				sqid to[] = {k, l};
				char move[MOVE_MAXLEN];
				size_t len = format_move(piece, from, to, PIECE_BY_IDX(p), move);
				move[len] = '\0';
				printf("%s: %u\n", move, npos);
			}

			game_undo_last_ply();
			npositions += npos;
		}
	}

	return npositions;
}

static const char *testpos_fen = "rnbq1k1r/pp1Pbppp/2p5/8/2B5/8/PPP1NnPP/RNBQK2R w KQ - 1 8";
static unsigned int testpos_possible_positions_num = 0;
static unsigned int possible_positions_nums[] = {
	44,
	1486,
	62379,
	2103487,
	89941194,
};

int main(void) {
	game_init(testpos_fen);
	//game_exec_ply(1, 0, 3, 1, PIECE_NONE);
	//get_possible_position_num(1, 1);
	//return 0;

	for (int i = 0; i < ARRNUM(possible_positions_nums); ++i) {
		unsigned int npos = get_possible_position_num(i + 1, 1);
		unsigned int nposref = possible_positions_nums[i];
		TEST_EQUAL_U(npos, nposref);
	}
	return 0;
}
