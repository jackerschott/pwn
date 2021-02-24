#ifndef NOTATION_H
#define NOTATION_H

#include "pwn.h"

#define SECOND (1000L * 1000L * 1000L)

#define MAXLEN_MOVE (STRLEN("Sg1-f3"))
#define MAXLEN_TIME (STRLEN("10:00:00.000000000"))
#define MAXLEN_TIME_COARSE (STRLEN("10:00:00"))

void format_move(piece_t piece, fid from[2], fid to[2], piece_t prompiece, char *str, size_t *len);
void format_timeinterval(long t, char *str, size_t *len, int coarse);

int parse_move(const char *str, piece_t *piece, fid from[2], fid to[2], piece_t *prompiece);
int parse_time(const char *str, const char **end, long *t);

#endif /* NOTATION_H */
