#include <assert.h>
#include <ctype.h>
#include <string.h>

#include "notation.h"

#define DIVROUND(a, b) (((a) + ((b) - 1)) / (b))

void format_move(piece_t piece, fid from[2], fid to[2], piece_t prompiece, char *str, size_t *len)
{
	assert(prompiece == PIECE_NONE
			|| piece == PIECE_PAWN
			&& PIECE_IDX(prompiece) >= 1 && PIECE_IDX(prompiece) <= 4);

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

	*len = c - str;
	assert(*len <= MAXLEN_MOVE);
}
void format_timeinterval(long t, char *str, size_t *len, int coarse)
{
	long ns, us, ms, s, m, h;
	str[0] = '\0';
	if (coarse) {
		s = DIVROUND(t, SECOND);
		m = s / 60L;
		h = m / 60L;

		s -= 60L * m;
		m -= 60L * h;

		sprintf(str, "%li:%.2li:%.2li", h, m, s);
		*len = strlen(str);
	} else {
		ns = t;
		s = ns / SECOND;
		m = s / 60L;
		h = m / 60L;

		ns -= SECOND * s;
		s -= 60L * m;
		m -= 60L * h;

		sprintf(str, "%li:%.2li:%.2li.%.9li", h, m, s, ns);
		*len = strlen(str);
	}

	assert(*len <= MAXLEN_TIME);
}

/* TODO: respect len while parsing */
static long parse_number(const char *str, char **end)
{
	if (isspace(*str))
		return -1;

	long l = strtol(str, end, 10);
	if (errno == ERANGE)
		return -1;

	return l;
}
int parse_move(const char *str, piece_t *piece,
		fid from[2], fid to[2], piece_t *prompiece)
{
	/* get piece */
	const char *c = str;
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
		return 1;
	from[0] = ROW_BY_CHAR(*c);
	++c;

	if (*c < '1' || *c > '8')
		return 1;
	from[1] = COL_BY_CHAR(*c);
	++c;

	if (*c != '-')
		return 1;
	++c;

	if (*c < 'a' || *c > 'h')
		return 1;
	to[0] = ROW_BY_CHAR(*c);
	++c;

	if (*c < '1' || *c > '8')
		return 1;
	to[1] = COL_BY_CHAR(*c);
	++c;

	/* get prompiece */
	if (*c != '\0') {
		p = PIECE_NONE;
		for (int i = 1; i < 4; ++i) {
			if (*c == piece_symbols[i]) {
				p = PIECE_BY_IDX(i);
				++c;
				break;
			}
		}
		if (p == PIECE_NONE)
			return 1;
		*prompiece = p;
	} else {
		*prompiece = PIECE_NONE;
	}

	if (*c != '\0')
		return 1;
	return 0;
}
int parse_time(const char *str, const char **end, long *t)
{
	long h, m, s;

	const char *c = str;
	char *e;
	h = parse_number(c, &e);
	if (h == -1)
		return 1;
	c = e;

	if (*c != ':')
		return 1;
	++c;

	m = parse_number(c, &e);
	if (m == -1)
		return 1;
	c = e;

	if (*c != ':')
		return 1;
	++c;

	s = parse_number(c, &e);
	if (s == -1)
		return 1;
	c = e;

	long ns = 0;
	if (*c == '.') {
		++c;

		ns = parse_number(c, (char **)&c);
		if (ns == -1)
			return 1;
	}

	*end = c;
	*t = h * 3600L * SECOND
		+ m * 60L * SECOND
		+ s * SECOND
		+ ns;
	return 0;
}
