#ifndef GAME_H
#define GAME_H

#include "chess.h"

#define NUM_UPDATES_MAX 4

typedef struct moveinfo_t moveinfo_t;

void game_init_board(void);
void game_init_test_board(void);
void game_terminate(void);

int game_move(fid ifrom, fid jfrom, fid ito, fid jto, piece_t prompiece);
void game_undo_move(moveinfo_t *m);

int game_is_movable_piece_at(fid i, fid j);
int game_is_stalemate(void);
int game_is_checkmate(void);

color_t game_get_playing_color(void);
piece_t game_get_piece(fid i, fid j);
color_t game_get_color(fid i, fid j);
fieldinfo_t game_get_fieldinfo(fid i, fid j);
void game_get_updates(fid u[][2]);

int game_save_board(const char *fname);
int game_load_board(const char *fname);

#endif /* GAME_H */
