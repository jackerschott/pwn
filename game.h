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

#define NUM_UPDATES_MAX 4

typedef struct moveinfo_t plyinfo_t;

int game_init(void);
void game_init_test_board(void);
void game_terminate(void);

int game_exec_ply(fid ifrom, fid jfrom, fid ito, fid jto, piece_t prompiece);
void game_undo_move(plyinfo_t *m);

int game_is_movable_piece_at(fid i, fid j);
status_t game_get_status(int timeout);

color_t game_get_moving_color(void);
piece_t game_get_piece(fid i, fid j);
color_t game_get_color(fid i, fid j);
fieldinfo_t game_get_fieldinfo(fid i, fid j);
void game_get_updates(fid u[][2]);
int game_get_move_number();

int game_save_board(const char *fname);
int game_load_board(const char *fname);

/* debug */
void print_board(void);

#endif /* GAME_H */
