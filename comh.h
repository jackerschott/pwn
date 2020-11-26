#ifndef COMH_H
#define COMH_H

#include "chess.h"
#include "handler.h"

enum {
	COMH_IS_EXCHANGING = 1,
};
struct comh_args_t {
	struct handler_context_t *hctx;

	int fopp;
	color_t gamecolor;
};

void *comh_main(void *args);

#endif /* COMH_H */
