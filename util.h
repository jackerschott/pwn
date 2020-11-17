#ifndef UTIL_H
#define UTIL_H

#include <stdio.h>
#include <stdlib.h>

#define ARRSIZE(x) (sizeof(x) / sizeof((x)[0]))

#define BUG() do { \
	fprintf(stderr, "BUG: failure at %s:%d/%s()!\n", __FILE__, __LINE__, __func__); \
	exit(EXIT_FAILURE); \
} while (0);

#if 1
#define SYSERR() do { \
	fprintf(stderr, "%s:%d/%s: ", __FILE__, __LINE__, __func__); \
	perror(NULL); \
} while (0);
#else
#define SYSERR() do { \
	fprintf(stderr, "%s: ", __func__); \
	perror(NULL); \
} while (0);
#endif

#endif /* UTIL_H */
