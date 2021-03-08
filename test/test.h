#ifndef TEST_H
#define TEST_H

#include <stdio.h>
#include <stdlib.h>

#define TEST_EQUAL_I(x1, x2) 						\
	do { 								\
		printf("test %i == %i\n", x1, x2); 			\
		if ((x1) != (x2)) { 					\
			fprintf(stderr, "test failed at %s():%s/%i\n", 	\
					__func__, __FILE__, __LINE__); 	\
			exit(1); 					\
		} 							\
	} while (0); 
#define TEST_EQUAL_U(x1, x2) 						\
	do { 								\
		printf("test %u == %u\n", x1, x2); 			\
		if ((x1) != (x2)) { 					\
			fprintf(stderr, "test failed at %s():%s/%i\n", 	\
					__func__, __FILE__, __LINE__); 	\
			exit(1); 					\
		} 							\
	} while (0); 
#define TEST_EQUAL_LI(x1, x2) 						\
	do { 								\
		printf("test %li == %li\n", x1, x2); 			\
		if ((x1) != (x2)) { 					\
			fprintf(stderr, "test failed at %s():%s/%i\n", 	\
					__func__, __FILE__, __LINE__); 	\
			exit(1); 					\
		} 							\
	} while (0); 

#endif /* TEST_H */
