#include <string.h>
#include <time.h>
#include <unistd.h>

#include "../notation.h"
#include "test.h"

#define DIVROUND(a, b) (((a) + ((b) - 1L)) / (b))

void test_timestamp(long t) {
	long tres;
	char s[TSTAMP_MAXLEN + 1];
	format_timestamp(t, s, 1);
	parse_timestamp(s, &tres);
	printf("info: s = %s\n", s);
	TEST_EQUAL_LI(tres, (t / SECOND) * SECOND);

	format_timestamp(t, s, 0);
	parse_timestamp(s, &tres);
	printf("info: s = %s\n", s);
	TEST_EQUAL_LI(tres, t);
}

void test_timeinterval(long dt) {
	long dtres;
	char s[TINTERVAL_MAXLEN + 1];
	format_timeinterval(dt, s, 1);
	parse_timeinterval(s, &dtres);
	printf("info: s = %s\n", s);
	TEST_EQUAL_LI(dtres, DIVROUND(dt, SECOND) * SECOND);

	format_timeinterval(dt, s, 0);
	parse_timeinterval(s, &dtres);
	printf("info: s = %s\n", s);
	TEST_EQUAL_LI(dtres, dt);
}

void test_move() {
	for (int i = 0; i < NUM_PIECES * NF * NF * NF * NF * (NUM_PIECES - 2); ++i) {
		fid from[2], to[2];
		piece_t p = 2 * (i % NUM_PIECES) + 2;

		int j = (i / NUM_PIECES);
		int k = (j / NF / NF);
		from[0] = j % NF;
		from[1] = (j / NF) % NF;
		to[0] = k % NF;
		to[1] = (k / NF) % NF;
		if (memcmp(from, to, 2 * sizeof(fid)) == 0)
			continue;

		int l = (k / NF / NF);
		piece_t prompiece = 2 * l + 4;
		if (p != PIECE_PAWN) {
			prompiece = PIECE_NONE;
		}

		char s[MOVE_MAXLEN + 1];
		size_t len = format_move(p, from, to, prompiece, s);

		piece_t pres, prompieceres;
		fid fromres[2], tores[2];
		parse_move(s, &pres, fromres, tores, &prompieceres);
		s[len] = '\0';
		printf("info: s = %s\n", s); 

		TEST_EQUAL_I(pres, p);
		TEST_EQUAL_I(memcmp(from, fromres, 2 * sizeof(fid)), 0);
		TEST_EQUAL_I(memcmp(to, tores, 2 * sizeof(fid)), 0);
		TEST_EQUAL_I(prompieceres, prompiece);
	}
}

int main(void) {
	struct timespec ts;
	clock_gettime(CLOCK_REALTIME, &ts);
	long t1 = ts.tv_sec * SECOND + ts.tv_nsec;
	test_timestamp(t1);

	usleep(10000);

	clock_gettime(CLOCK_REALTIME, &ts);
	long t2 = ts.tv_sec * SECOND + ts.tv_nsec;
	long dt = t2 - t1;
	test_timeinterval(dt);

	test_move();
}
