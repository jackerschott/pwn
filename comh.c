#include <string.h>
#include <unistd.h>

#include "comh.h"

static int fopp;
static color_t gamecolor;

static struct handler_context_t *hctx;
static int fevent;
static int fconfirm;
static int *state;

static void comh_cleanup(void);

static void handle_syncmoves(struct event_syncmoves *e)
{
	pthread_mutex_lock(&hctx->comhlock);
	*state |= COMH_IS_EXCHANGING;
	pthread_mutex_unlock(&hctx->comhlock);

	int ret = 0;
	if (writebuf(fconfirm, &ret, sizeof(ret)) == -1) {
		SYSERR();
		comh_cleanup();
		pthread_exit(NULL);
	}

	/* send move */
	union event_t event;
	event.syncmoves.type = EVENT_SYNCMOVES;
	memcpy(event.syncmoves.from, e->from, sizeof(e->from));
	memcpy(event.syncmoves.to, e->to, sizeof(e->to));
	event.syncmoves.tmove = e->tmove;
	if (sendbuf(fopp, &event, sizeof(event)) == -1) {
		SYSERR();
		comh_cleanup();
		pthread_exit(NULL);
	}

	/* recv move */
	if (recvbuf(fopp, &event, sizeof(event)) == -1) {
		SYSERR();
		comh_cleanup();
		pthread_exit(NULL);
	}

	/* send opponent move event */
	event.oppmove.type = EVENT_OPPMOVE;
	if (writebuf(hctx->gfxh.pevent[1], &event, sizeof(event)) == -1) {
		SYSERR();
		comh_cleanup();
		exit(-1);
	}

	pthread_mutex_lock(&hctx->comhlock);
	*state &= ~COMH_IS_EXCHANGING;
	pthread_mutex_unlock(&hctx->comhlock);
}

static void comh_setup(void)
{
	if (gamecolor == COLOR_BLACK) {
		union event_t event;
		if (recvbuf(fopp, &event, sizeof(event)) == -1) {
			SYSERR();
			comh_cleanup();
			exit(-1);
		}

		event.oppmove.type = EVENT_OPPMOVE;
		if (writebuf(hctx->gfxh.pevent[1], &event, sizeof(event)) == -1) {
			SYSERR();
			comh_cleanup();
			exit(-1);
		}
	}

	pthread_mutex_lock(&hctx->comhlock);
	*state &= ~COMH_IS_EXCHANGING;
	pthread_mutex_unlock(&hctx->comhlock);
}
static void comh_run(void)
{
	union event_t event;
	while (1) {
		printf("fevent = %i\n", fevent);
		if (readbuf(fevent, &event, sizeof(event)) == -1) {
			SYSERR();
			comh_cleanup();
			pthread_exit(NULL);
		}
		printf("event.type = %i\n", event.type);

		if (event.type == EVENT_SYNCMOVES) {
			handle_syncmoves(&event.syncmoves);
		} else if (event.type == EVENT_TERM) {
			break;
		} 
	}
}
static void comh_cleanup(void)
{
	if (close(fopp) == -1)
		fprintf(stderr, "error while closing socket, possible data loss\n");
	pthread_mutex_lock(&hctx->mainlock);
	hctx->terminate = 1;
	pthread_mutex_unlock(&hctx->mainlock);
}
void *comh_main(void *args)
{
	struct comh_args_t *a = (struct comh_args_t *)args;
	hctx = a->hctx;
	fopp = a->fopp;
	gamecolor = a->gamecolor;
	free(a);

	fevent = hctx->comh.pevent[0];
	fconfirm = hctx->comh.pconfirm[1];
	state = &hctx->comh.state;

	comh_setup();
	comh_run();
	comh_cleanup();
	return 0;
}
