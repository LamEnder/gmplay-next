#pragma once
#include "common.h"

// Audio playback through the Kindle's own mixer (audiomgrd), which is what
// drives Bluetooth headphones. There's no ALSA device for it, so PCM is piped
// into `gst-launch-0.10 fdsrc ! mixersink`, the streaming client the firmware
// ships. The audio track is demuxed/decoded on its own thread from a second
// open of the file, so pausing/seeking audio never disturbs video decoding.
typedef struct Audio Audio;

// NULL if the file has no playable audio, no output device is connected,
// or audio is disabled (-m).
Audio *audio_open(const char *path);
void audio_close(Audio *a);

void audio_play(Audio *a, double pos);   // (re)start from pos (secs)
void audio_halt(Audio *a);               // stop immediately

// Position (secs) of what's being heard right now, or < 0 if not playing.
double audio_clock(Audio *a);
