#ifndef NOTATION_H
#define NOTATION_H

#include "pwn.h"

#define SECOND (1000L * 1000L * 1000L)
#define MINUTE (60L * SECOND)
#define HOUR (60L * MINUTE)

#define MOVE_MAXLEN (STRLEN("Sg1-f3"))
#define TINTERVAL_MAXLEN (STRLEN("10:00:00.000000000"))
#define TINTERVAL_COARSE_MAXLEN (STRLEN("10:00:00"))
#define TSTAMP_MAXLEN (STRLEN("1970-01-01 00:00:00.000000000"))
#define TSTAMP_COARSE_MAXLEN (STRLEN("1970-01-01 00:00:00"))

#define FEN_BUFSIZE 1024

size_t format_move(piece_t piece, sqid from[2], sqid to[2], piece_t prompiece, char *str);
size_t format_timeinterval(long t, char *str, int coarse);
size_t format_timestamp(long t, char *s, int coarse);

char *parse_number(const char *s, long *n);
char *parse_move(const char *s, piece_t *piece,
		sqid from[2], sqid to[2], piece_t *prompiece);
char *parse_timeinterval(const char *s, long *t, int onlycoarse);
char *parse_timestamp(const char *s, long *t);

size_t format_fen(squareinfo_t position[NF][NF], color_t active_color, int castlerights[2],
		sqid fep[2], unsigned int ndrawplies, unsigned int nmove, char *s);
char *parse_fen(const char *s, squareinfo_t position[NF][NF], color_t *active_color,
		int castlerights[2], sqid fep[2], unsigned int *ndrawplies, unsigned int *nmove);

#endif /* NOTATION_H */
