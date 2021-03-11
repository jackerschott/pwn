#include <fcntl.h>
#include <poll.h>

#include <pthread.h>

#include <pulse/simple.h>
#include <pulse/error.h>

/* for some reason these are defined in libpulse headers */
#undef MAX
#undef MIN

#define MINIMP3_IMPLEMENTATION
#define MINIMP3_ONLY_MP3
#include "minimp3/minimp3_ex.h"

#include "pwn.h"
#include "util.h"

#include "audioh.h"

#define AUDIOH_EVENT_RESPONSE_TIME 10000

static struct handler_context_t *hctx;
static int fevent;
static int fconfirm;
static int *state;
static struct pollfd pfd;

static pa_simple *pacon;
static const pa_sample_spec pa_soundspec = {
	.format = PA_SAMPLE_S16LE,
	.rate = 44100,
	.channels = 1
};

static void audioh_cleanup(void);

static void handle_playsound(struct audioh_event_playsound *e)
{
	printf("[handle_playsound] %s\n", e->fname);

	mp3dec_t dec;
	mp3dec_file_info_t info;
	if (mp3dec_load(&dec, e->fname, &info, NULL, NULL)) {
		fprintf(stderr, "could not load and decode %s\n", e->fname);
		audioh_cleanup();
		pthread_exit(NULL);
	}

	int err;
	size_t nsamplebytes = info.samples * sizeof(mp3d_sample_t);
	if (pa_simple_write(pacon, (char *)info.buffer, nsamplebytes, &err) < 0) {
		fprintf(stderr, "could not write to pulsaudio: %s\n", pa_strerror(err));
		free(info.buffer);
		audioh_cleanup();
		pthread_exit(NULL);
	}

	free(info.buffer);
}

static void audioh_setup(void)
{
	int err;
	if (fcntl(fevent, F_SETFL, O_NONBLOCK) == -1) {
		SYSERR();
		goto cleanup_err;
	}

	pfd.fd = fevent;
	pfd.events = POLLIN;

	/* init pulseaudio connection */
	pacon = pa_simple_new(NULL, PROGNAME, PA_STREAM_PLAYBACK, NULL, PROGNAME,
			&pa_soundspec, NULL, NULL, &err);
	if (!pacon) {
		fprintf(stderr, "%s: could not connect to pulseaudio server, %s\n",
				__func__, pa_strerror(err));
		goto cleanup_err;
	}
	return;

cleanup_err:
	pthread_mutex_lock(&hctx->mainlock);
	hctx->terminate = 1;
	pthread_mutex_unlock(&hctx->mainlock);
	pthread_exit(NULL);
}
static void audioh_run(void)
{
	union audioh_event_t e;
	memset(&e, 0, sizeof(e));
	while (1) {
		int n = poll(&pfd, 1, 0);
		if (n == -1) {
			SYSERR();
			goto cleanup_err;
		}


		if (pfd.revents) {
			n = hread(fevent, &e, sizeof(e));
			if (n == -1) {
				SYSERR();
			} else if (n == 1) {
				break;
			}

			switch (e.type) {
			case AUDIOH_EVENT_PLAYSOUND:
				handle_playsound(&e.playsound);
				break;
			default:
				fprintf(stderr, "%s: received unexpected event\n", __func__);
				goto cleanup_err;
			}
		} else {
			usleep(AUDIOH_EVENT_RESPONSE_TIME);
		}
	}
	return;

cleanup_err:
	audioh_cleanup();
	pthread_exit(NULL);
}
static void audioh_cleanup(void)
{
	pa_simple_free(pacon);

	if (close(fevent) == -1 || close(fconfirm) == -1)
		fprintf(stderr, "%s: error while closing event pipes\n", __func__);

	pthread_mutex_lock(&hctx->mainlock);
	hctx->terminate = 1;
	pthread_mutex_unlock(&hctx->mainlock);
}
void *audioh_main(void *args)
{
	struct audioh_args_t *a = (struct audioh_args_t *)args;
	hctx = a->hctx;
	free(a);

	fevent = hctx->audioh.pevent[0];
	fconfirm = hctx->audioh.pconfirm[1];
	state = &hctx->audioh.state;

	audioh_setup();
	audioh_run();
	audioh_cleanup();
	return 0;
}
