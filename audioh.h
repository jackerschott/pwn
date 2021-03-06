#ifndef AUDIO_H
#define AUDIO_H

#define SOUND_MOVE_FNAME 		(DATADIR "sounds/move.mp3")
#define SOUND_CAPTURE_FNAME 		(DATADIR "sounds/capture.mp3")
#define SOUND_GAME_DECIDED_FNAME 	(DATADIR "sounds/gamedecided.mp3")
#define SOUND_LOW_TIME_FNAME 		(DATADIR "sounds/lowtime.mp3")

enum {
	AUDIOH_EVENT_PLAYSOUND,
};
struct audioh_event_playsound {
	int type;
	const char *fname;
};

union audioh_event_t {
	int type;
	struct audioh_event_playsound playsound;
};

struct audioh_args_t {
	struct handler_context_t *hctx;
};

void *audioh_main(void *args);

#endif /* AUDIO_H */
