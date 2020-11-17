#ifndef GAME_H
#define GAME_H

#define NF 8
#define NUM_PIECES 6
#define NUM_COLORS 2
#define NUM_UPDATES_MAX 4

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

typedef struct move_info move_info;

void game_init_board(int c);
void game_init_test_board(int c);

int game_move(int ifrom, int jfrom, int ito, int jto, int prompiece);
void game_undo_move(move_info *m);

int game_is_movable_piece(int i, int j);
int game_is_stalemate(void);
int game_is_checkmate(void);

int game_get_piece(int i, int j);
int game_get_color(int i, int j);
void game_get_updates(int u[][2]);

int game_save_board(const char *fname);
int game_load_board(const char *fname);

#endif /* GAME_H */
