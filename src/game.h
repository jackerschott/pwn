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

#define UPDATES_NUM_MAX 4

#define STARTPOS_FEN "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1"

typedef struct ply_t ply_t;

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
