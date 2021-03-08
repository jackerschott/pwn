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
		*c = toupper(piece_symbols[PIECE_IDX(piece)]);
		++c;
	}

	c[0] = FILE_CHAR(from[0]);
	c[1] = RANK_CHAR(from[1]);
	c[2] = '-';
	c[3] = FILE_CHAR(to[0]);
	c[4] = RANK_CHAR(to[1]);
	c += 5;

	if (prompiece != PIECE_NONE) {
		*c = toupper(piece_symbols[PIECE_IDX(prompiece)]);
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
	for (int i = 0; i < STRLEN(piece_symbols); ++i) {
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
	from[0] = FILE_BY_CHAR(*c);
	++c;

	if (*c < '1' || *c > '8')
		return NULL;
	from[1] = RANK_BY_CHAR(*c);
	++c;

	if (*c != '-')
		return NULL;
	++c;

	if (*c < 'a' || *c > 'h')
		return NULL;
	to[0] = FILE_BY_CHAR(*c);
	++c;

	if (*c < '1' || *c > '8')
		return NULL;
	to[1] = RANK_BY_CHAR(*c);
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

size_t format_fen(squareinfo_t position[NF][NF], color_t active_color,
		int castlerights[2], sqid fep[2], unsigned int ndrawplies, unsigned int nmove, char *s)
{
	char *c = s;

	/* position */
	int n = 0;
	for (sqid j = NF - 1; j >= 0; --j) {
		for (sqid i = 0; i < NF; ++i) {
			piece_t piece = position[i][j] & PIECEMASK;
			color_t color = position[i][j] & COLORMASK;
			if (piece == PIECE_NONE) {
				++n;
				continue;
			}

			if (n > 0) {
				assert(n <= NF);
				*c = '0' + n;
				++c;

				n = 0;
			}

			int p = PIECE_IDX(piece);
			*c = color ? piece_symbols[p] : toupper(piece_symbols[p]);
			++c;
		}
		if (n > 0) {
			assert(n <= NF);
			*c = '0' + n;
			++c;

			n = 0;
		}

		*c = j > 0 ? '/' : ' ';
		++c;
	}

	/* active color */
	c[0] = active_color ? 'b' : 'w';
	c[1] = ' ';
	c += 2;

	/* castlerights */
	if (castlerights[COLOR_WHITE] == 0 && castlerights[COLOR_BLACK] == 0) {
		*c = '-';
	} else {
		if (castlerights[COLOR_WHITE] & CASTLERIGHT_KINGSIDE) {
			*c = 'K';
			++c;
		}
		if (castlerights[COLOR_WHITE] & CASTLERIGHT_QUEENSIDE) {
			*c = 'Q';
			++c;
		}
		if (castlerights[COLOR_BLACK] & CASTLERIGHT_KINGSIDE) {
			*c = 'k';
			++c;
		}
		if (castlerights[COLOR_BLACK] & CASTLERIGHT_QUEENSIDE) {
			*c = 'q';
			++c;
		}
	}
	*c = ' ';
	++c;

	/* en passant target square */
	if (fep[0] == -1 && fep[1] == -1) {
		*c = '-';
		++c;
	} else {
		c[0] = FILE_CHAR(fep[0]);
		c[1] = RANK_CHAR(fep[1]);
		c += 2;
	}
	*c = ' ';
	++c;

	/* number of drawish plies */
	sprintf(c, "%u %u", ndrawplies, nmove);

	size_t l = strlen(s);
	assert(l < FEN_BUFSIZE);
	return l;
}
char *parse_fen(const char *s, squareinfo_t position[NF][NF], color_t *active_color,
		int castlerights[2], sqid fep[2], unsigned int *ndrawplies, unsigned int *nmove)
{
	const char *c = s;

	/* position */
	memset(position, 0, NF * NF * sizeof(position[0][0]));

	int f = 0;
	while (*c != ' ') {
		if (*c == '/') {
			++c;
			continue;
		} else if (*c >= '1' && *c <= '8') {
			f += *c - '0';
			++c;
			continue;
		}

		int p = 0;
		color_t color;
		for (; p < STRLEN(piece_symbols); ++p) {
			if (*c == toupper(piece_symbols[p])) {
				color = COLOR_WHITE;
				break;
			} else if (*c == piece_symbols[p]) {
				color = COLOR_BLACK;
				break;
			}
		}
		if (p == STRLEN(piece_symbols))
			return NULL;

		if (f >= NF * NF)
			return NULL;
		position[f % NF][NF - (f / NF) - 1] = PIECE_BY_IDX(p) | color;
		++f;

		++c;
	}
	++c;

	/* active color */
	if (c[0] == 'w') {
		*active_color = COLOR_WHITE;
	} else if (c[0] == 'b') {
		*active_color = COLOR_BLACK;
	} else {
		return NULL;
	}
	if (c[1] != ' ')
		return NULL;
	c += 2;

	/* castlerights */
	memset(castlerights, 0, 2 * sizeof(castlerights[0]));
	if (*c == '-') {
		++c;
	} else {
		int rights = 0;
		const char *syms = "KQkq";

		int i = 0;
		for (; c[i] != ' '; ++i) {
			int j = 0;
			for (; j < STRLEN(syms); ++j) {
				if (c[i] != syms[j])
					continue;

				int right = 1 << j;
				if (right <= rights)
					return NULL;

				rights |= right;
				break;
			}
			if (j == STRLEN(syms))
				return NULL;
		}

		castlerights[COLOR_WHITE] = rights & (CASTLERIGHT_KINGSIDE | CASTLERIGHT_QUEENSIDE);
		castlerights[COLOR_BLACK] = (rights >> 2) & (CASTLERIGHT_KINGSIDE | CASTLERIGHT_QUEENSIDE);

		c += i;
	}
	if (*c != ' ')
		return NULL;
	++c;

	/* en passant target square */
	if (*c == '-') {
		fep[0] = -1;
		fep[1] = -1;
		++c;
	} else {
		int i = FILE_BY_CHAR(c[0]);
		int j = RANK_BY_CHAR(c[1]);
		if (i < 0 || i >= NF || j < 0 || j >= NF)
			return NULL;

		fep[0] = i;
		fep[1] = j;
		c += 2;
	}
	if (*c != ' ')
		return NULL;
	++c;

	/* number of drawish plies */
	long n;
	c = parse_number(c, &n);
	if (c == NULL)
		return NULL;
	*ndrawplies = n;
	if (*c != ' ')
		return NULL;
	++c;

	/* fullmove number */
	c = parse_number(c, &n);
	if (c == NULL)
		return NULL;
	*nmove = n;

	return (char *)c;
}
