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

#define _XOPEN_SOURCE 700
#define _DEFAULT_SOURCE

#include <assert.h>
#include <ctype.h>
#include <string.h>
#include <time.h>

#include "notation.h"

#define DIVROUND(a, b) (((a) + ((b) - 1)) / (b))

size_t format_move(piece_t piece, sqid from[2], sqid to[2], piece_t prompiece, char *str)
{
	assert(memcmp(from, to, 2 * sizeof(sqid)) != 0);
	assert(prompiece == PIECE_NONE || piece == PIECE_PAWN
			&& PIECE_IDX(prompiece) >= PIECE_IDX(PIECE_QUEEN)
			&& PIECE_IDX(prompiece) <= PIECE_IDX(PIECE_KNIGHT));

	size_t len;
	char *c = str;
	if (piece != PIECE_PAWN) {
		*c = piece_symbols[PIECE_IDX(piece)];
		++c;
	}

	c[0] = ROW_CHAR(from[0]);
	c[1] = COL_CHAR(from[1]);
	c[2] = '-';
	c[3] = ROW_CHAR(to[0]);
	c[4] = COL_CHAR(to[1]);
	c += 5;

	if (prompiece != PIECE_NONE) {
		*c = piece_symbols[PIECE_IDX(prompiece)];
		++c;
	}

	len = c - str;
	assert(len <= MOVE_MAXLEN);
	return len;
}
size_t format_timeinterval(long t, char *str, int coarse)
{
	size_t len;
	long ns, us, ms, s, m, h;
	str[0] = '\0';
	if (coarse) {
		s = DIVROUND(t, SECOND);
		m = s / 60L;
		h = m / 60L;

		s -= 60L * m;
		m -= 60L * h;

		sprintf(str, "%li:%.2li:%.2li", h, m, s);
		len = strlen(str);
		assert(len <= TINTERVAL_COARSE_MAXLEN);
	} else {
		ns = t;
		s = ns / SECOND;
		m = s / 60L;
		h = m / 60L;

		ns -= SECOND * s;
		s -= 60L * m;
		m -= 60L * h;

		sprintf(str, "%li:%.2li:%.2li.%.9li", h, m, s, ns);
		len = strlen(str);
		assert(len <= TINTERVAL_MAXLEN);
	}

	return len;
}
size_t format_timestamp(long t, char *s, int coarse)
{
	size_t len;
	char *c = s;

	long tcoarse = t / SECOND;
	long tfine = t % SECOND;

	struct tm bdt;
	gmtime_r(&tcoarse, &bdt);

	len = strftime(s, TSTAMP_COARSE_MAXLEN + 1, "%Y-%m-%d %H:%M:%S", &bdt);
	c += len;

	if (!coarse) {
		sprintf(c, ".%.9li", tfine);
		len = strlen(s);
	}

	assert(len <= TSTAMP_MAXLEN);
	return len;
}

char *parse_number(const char *s, long *n)
{
	if (isspace(*s))
		return NULL;

	char *e;
	*n = strtol(s, &e, 10);
	if (errno == ERANGE)
		return NULL;

	return e;
}
char *parse_move(const char *s, piece_t *piece,
		sqid from[2], sqid to[2], piece_t *prompiece)
{
	/* get piece */
	const char *c = s;
	piece_t p = PIECE_NONE;
	for (int i = 0; i < ARRNUM(piece_symbols); ++i) {
		if (*c == piece_symbols[i]) {
			p = PIECE_BY_IDX(i);
			++c;
			break;
		}
	}
	if (p == PIECE_NONE)
		p = PIECE_PAWN;
	*piece = p;

	/* get start and destination field */
	if (*c < 'a' || *c > 'h')
		return NULL;
	from[0] = ROW_BY_CHAR(*c);
	++c;

	if (*c < '1' || *c > '8')
		return NULL;
	from[1] = COL_BY_CHAR(*c);
	++c;

	if (*c != '-')
		return NULL;
	++c;

	if (*c < 'a' || *c > 'h')
		return NULL;
	to[0] = ROW_BY_CHAR(*c);
	++c;

	if (*c < '1' || *c > '8')
		return NULL;
	to[1] = COL_BY_CHAR(*c);
	++c;

	/* get prompiece */
	p = PIECE_NONE;
	for (int i = PIECE_IDX(PIECE_QUEEN); i <= PIECE_IDX(PIECE_KNIGHT); ++i) {
		if (*c == piece_symbols[i]) {
			p = PIECE_BY_IDX(i);
			++c;
			break;
		}
	}
	*prompiece = p;

	return (char *)c;
}
char *parse_timeinterval(const char *s, long *t, int onlycoarse)
{
	const char *c = s;

	struct tm bdt;
	if (!(c = strptime(c, "%H", &bdt)))
		return NULL;
	*t = bdt.tm_hour * 3600L * SECOND;

	if (!(c = strptime(c, ":%M", &bdt)))
		return (char *)c;
	*t += bdt.tm_min * 60L * SECOND;

	if (!(c = strptime(c, ":%S", &bdt)))
		return (char *)c;
	*t += bdt.tm_sec * SECOND;

	if (onlycoarse || *c != '.') {
		return (char *)c;
	}
	++c;

	long ns;
	if (!(c = parse_number(c, &ns))) {
		return (char *)c;
	}
	*t += ns;

	return (char *)c;
}
char *parse_timestamp(const char *s, long *t)
{
	const char *c = s;

	struct tm bdt;
	c = strptime(c, "%Y-%m-%d %H:%M:%S", &bdt);
	*t = timegm(&bdt) * SECOND;

	if (*c != '.') {
		return (char *)c;
	}
	++c;

	long ns;
	c = parse_number(c, &ns);
	if (!c) {
		return (char *)c;
	}
	*t += ns;

	return (char *)c;
}
