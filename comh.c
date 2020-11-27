#include <fcntl.h>
#include <poll.h>
#include <string.h>
#include <unistd.h>

#include <sys/socket.h>

#include "comh.h"

static int fopp;
static color_t gamecolor;

static struct handler_context_t *hctx;
static int fevent;
static int fconfirm;
static int *state;

static int npoll;
struct pollfd pfds[2];

static void comh_cleanup(void);

static int hsend(int fd, void *buf, size_t size)
{
	char *b = (char *)buf;
	while (size > 0) {
		size_t n = send(fd, b, size, 0);
		if (n == -1) {
			if (errno == EINTR)
				continue;
			if (errno == EWOULDBLOCK || errno == EAGAIN)
				continue;
			return -1;
		}

		b += n;
		size -= n;
	}
	return 0;
}
static int hrecv(int fd, void *buf, size_t size)
{
	char *b = (char *)buf;
	while (size > 0) {
		size_t n = recv(fd, b, size, 0);
		if (n == -1) {
			if (errno == EINTR)
				continue;
			if (errno == EWOULDBLOCK || errno == EAGAIN)
				return 1;
			return -1;
		}

		b += n;
		size -= n;
	}
	return 0;
}

static void handle_sendmove(struct event_passmove *e)
{
	pthread_mutex_lock(&hctx->comhlock);
	*state |= COMH_IS_EXCHANGING;
	pthread_mutex_unlock(&hctx->comhlock);

	int ret = 0;
	if (hwrite(fconfirm, &ret, sizeof(ret)) == -1) {
		SYSERR();
		comh_cleanup();
		pthread_exit(NULL);
	}

	union event_t event;
	event.sendmove.type = EVENT_SENDMOVE;
	memcpy(event.sendmove.from, e->from, sizeof(e->from));
	memcpy(event.sendmove.to, e->to, sizeof(e->to));
	event.sendmove.tmove = e->tmove;
	int n = hsend(fopp, &event, sizeof(event));
	if (n == -1) {
		SYSERR();
		comh_cleanup();
		pthread_exit(NULL);
	}

	npoll = ARRSIZE(pfds);
}
static void handle_recvmove(struct event_passmove *e)
{
	union event_t event;
	memcpy(&event.playmove, e, sizeof(event.playmove));
	event.playmove.type = EVENT_PLAYMOVE;
	if (hwrite(hctx->gfxh.pevent[1], &event, sizeof(event)) == -1) {
		SYSERR();
		comh_cleanup();
		exit(-1);
	}

	pthread_mutex_lock(&hctx->comhlock);
	*state &= ~COMH_IS_EXCHANGING;
	pthread_mutex_unlock(&hctx->comhlock);
	npoll = ARRSIZE(pfds) - 1;
}

static void comh_setup(void)
{
	if (fcntl(fevent, F_SETFL, O_NONBLOCK) == -1)
		goto syserr_cleanup;
	if (fcntl(fopp, F_SETFL, O_NONBLOCK) == -1)
		goto syserr_cleanup;

	pfds[0].fd = fevent;
	pfds[0].events = POLLIN;
	pfds[1].fd = fopp;
	pfds[1].events = POLLIN;

	if (gamecolor == COLOR_WHITE) {
		npoll = ARRSIZE(pfds) - 1;

		pthread_mutex_lock(&hctx->comhlock);
		*state &= ~COMH_IS_EXCHANGING;
		pthread_mutex_unlock(&hctx->comhlock);
	} else if (gamecolor == COLOR_BLACK) {
		npoll = ARRSIZE(pfds);
	}
	return;

syserr_cleanup:
	SYSERR();
	comh_cleanup();
	pthread_exit(NULL);
}
static void comh_run(void)
{
	union event_t event;
	while (1) {
		int n = poll(pfds, npoll, 0);
		if (n == -1) {
			goto syserr_cleanup;
		} else if (n == 0) {
			continue;
		}

		if (pfds[0].revents) {
			n = hread(fevent, &event, sizeof(event));
			if (n == -1) {
				goto syserr_cleanup;
			} else if (n == 1) {
				continue;
			}

			if (event.type == EVENT_SENDMOVE) {
				handle_sendmove(&event.sendmove);
			} else if (event.type == EVENT_TERM) {
				break;
			} 
		} else if (npoll > 1 && pfds[1].revents) {
			n = hrecv(fopp, &event, sizeof(event));
			if (n == -1) {
				goto syserr_cleanup;
			} else if (n == 1) {
				continue;
			}

			handle_recvmove(&event.recvmove);
		}
	}
	return;

syserr_cleanup:
	SYSERR();
	comh_cleanup();
	pthread_exit(NULL);
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
