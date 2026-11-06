#include "audio.h"

#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/opt.h>
#include <libswresample/swresample.h>

#define RATE 44100
#define CHANNELS 2
#define BYTES_PER_SEC (RATE * CHANNELS * 2)
// Measured on a PW4 (FW 5.18): the pipe + gst + mixer queues keep the writer
// a steady ~1.0s ahead of what's actually playing.
#define PIPELINE_AHEAD 1.0

struct Audio {
    AVFormatContext *fmt;
    AVCodecContext *dec;
    SwrContext *swr;
    AVPacket *pkt;
    AVFrame *frm;
    int idx;
    double tb;
    int64_t start_ts;

    pthread_t th;
    bool th_alive;
    volatile bool run;
    pid_t pid;
    int fd;

    pthread_mutex_t mu;      // guards the clock fields below
    double start_pos;        // media time playback (re)started at
    double t_first;          // wall time of the first write, 0 = nothing written yet
    uint64_t written;        // bytes written since (re)start
    bool eof;                // all audio handed to gst
    bool failed;             // gst died / pipe broke
};

static bool output_connected(void) {
    FILE *p = popen("lipc-get-prop com.lab126.audiomgrd audioOutputConnected 2>/dev/null", "r");
    int v = 0;
    if (p) {
        if (fscanf(p, "%d", &v) != 1) v = 0;
        pclose(p);
    }
    return v == 1;
}

Audio *audio_open(const char *path) {
    if (opt.mute) return NULL;
    if (!output_connected()) {
        if (opt.verbose) fprintf(stderr, "gmplay: no audio output connected, playing video only\n");
        return NULL;
    }
    Audio *a = calloc(1, sizeof *a);
    if (!a) return NULL;
    a->fd = -1;
    pthread_mutex_init(&a->mu, NULL);
    const AVCodec *codec = NULL;
    if (avformat_open_input(&a->fmt, path, NULL, NULL) < 0 || avformat_find_stream_info(a->fmt, NULL) < 0 ||
        (a->idx = av_find_best_stream(a->fmt, AVMEDIA_TYPE_AUDIO, -1, -1, &codec, 0)) < 0) {
        if (opt.verbose) fprintf(stderr, "gmplay: no playable audio stream\n");
        audio_close(a);
        return NULL;
    }
    AVStream *st = a->fmt->streams[a->idx];
    for (unsigned i = 0; i < a->fmt->nb_streams; i++)
        if ((int)i != a->idx) a->fmt->streams[i]->discard = AVDISCARD_ALL;
    a->dec = avcodec_alloc_context3(codec);
    avcodec_parameters_to_context(a->dec, st->codecpar);
    a->dec->thread_count = 1;
    AVChannelLayout out = AV_CHANNEL_LAYOUT_STEREO;
    if (avcodec_open2(a->dec, codec, NULL) < 0 ||
        swr_alloc_set_opts2(&a->swr, &out, AV_SAMPLE_FMT_S16, RATE, &a->dec->ch_layout,
                            a->dec->sample_fmt, a->dec->sample_rate, 0, NULL) < 0 ||
        swr_init(a->swr) < 0) {
        fprintf(stderr, "gmplay: can't set up audio decoder\n");
        audio_close(a);
        return NULL;
    }
    a->pkt = av_packet_alloc();
    a->frm = av_frame_alloc();
    a->tb = av_q2d(st->time_base);
    a->start_ts = st->start_time != AV_NOPTS_VALUE ? st->start_time : 0;
    if (opt.verbose)
        fprintf(stderr, "gmplay: audio %s %d Hz %d ch -> mixer %d Hz stereo, A/V delay %.0f ms\n", codec->name,
                a->dec->sample_rate, a->dec->ch_layout.nb_channels, RATE, opt.av_delay * 1000);
    return a;
}

// Start `gst-launch-0.10 fdsrc fd=0 ! caps ! mixersink` reading from a pipe.
static int spawn_sink(Audio *a) {
    int p[2];
    if (pipe(p) < 0) return -1;
    pid_t pid = fork();
    if (pid < 0) { close(p[0]); close(p[1]); return -1; }
    if (pid == 0) {
        dup2(p[0], 0);
        int null = open("/dev/null", O_WRONLY);
        if (null >= 0) { dup2(null, 1); dup2(null, 2); }
        for (int fd = 3; fd < 256; fd++) close(fd);
        execlp("gst-launch-0.10", "gst-launch-0.10", "-q", "fdsrc", "fd=0", "!",
               "audio/x-raw-int,rate=44100,channels=2,width=16,depth=16,signed=true,endianness=1234",
               "!", "mixersink", (char *)NULL);
        _exit(127);
    }
    close(p[0]);
    fcntl(p[1], F_SETFD, FD_CLOEXEC);
    a->pid = pid;
    a->fd = p[1];
    return 0;
}

// Write all of buf to gst; false if we were stopped or the pipe broke.
static bool write_all(Audio *a, const uint8_t *buf, size_t n) {
    while (n && a->run) {
        ssize_t w = write(a->fd, buf, n);
        if (w < 0) {
            if (errno == EINTR) continue;
            if (a->run) {   // not us stopping it: the sink died
                pthread_mutex_lock(&a->mu);
                a->failed = true;
                pthread_mutex_unlock(&a->mu);
                fprintf(stderr, "gmplay: audio output failed, continuing without sound\n");
            }
            return false;
        }
        pthread_mutex_lock(&a->mu);
        if (!a->t_first) a->t_first = now_sec();
        a->written += (uint64_t)w;
        pthread_mutex_unlock(&a->mu);
        buf += w;
        n -= (size_t)w;
    }
    return a->run;
}

// Convert one decoded frame and send it, trimming anything before start_pos.
static bool send_frame(Audio *a, AVFrame *f) {
    uint8_t buf[16384 * CHANNELS * 2];
    int max_out = (int)(sizeof buf / (CHANNELS * 2));
    uint8_t *out[1] = { buf };
    int n = swr_convert(a->swr, out, max_out, f ? (const uint8_t **)f->extended_data : NULL, f ? f->nb_samples : 0);
    if (n <= 0) return a->run;
    size_t skip = 0;
    if (f && f->best_effort_timestamp != AV_NOPTS_VALUE && !a->written) {
        double pts = (double)(f->best_effort_timestamp - a->start_ts) * a->tb;
        if (pts < a->start_pos) {   // seeked to a point inside this frame (or before it)
            double cut = (a->start_pos - pts) * RATE;
            skip = cut >= n ? (size_t)n : (size_t)cut;
        }
    }
    return write_all(a, buf + skip * CHANNELS * 2, (size_t)(n - (int)skip) * CHANNELS * 2);
}

static void *audio_thread(void *arg) {
    Audio *a = arg;
    while (a->run) {
        int e = av_read_frame(a->fmt, a->pkt);
        if (e < 0) {   // end of file: drain decoder + resampler, then let gst play it out
            avcodec_send_packet(a->dec, NULL);
            while (a->run && avcodec_receive_frame(a->dec, a->frm) >= 0) {
                send_frame(a, a->frm);
                av_frame_unref(a->frm);
            }
            if (a->run) send_frame(a, NULL);
            pthread_mutex_lock(&a->mu);
            a->eof = true;
            pthread_mutex_unlock(&a->mu);
            break;
        }
        if (a->pkt->stream_index == a->idx && avcodec_send_packet(a->dec, a->pkt) >= 0) {
            while (a->run && avcodec_receive_frame(a->dec, a->frm) >= 0) {
                bool ok = send_frame(a, a->frm);
                av_frame_unref(a->frm);
                if (!ok) break;
            }
        }
        av_packet_unref(a->pkt);
    }
    return NULL;
}

void audio_halt(Audio *a) {
    if (!a || !a->th_alive) return;
    a->run = false;
    if (a->pid > 0) kill(a->pid, SIGTERM);   // stops sound now; unblocks our write()
    pthread_join(a->th, NULL);
    a->th_alive = false;
    if (a->fd >= 0) close(a->fd);
    a->fd = -1;
    if (a->pid > 0) waitpid(a->pid, NULL, 0);
    a->pid = 0;
}

void audio_play(Audio *a, double pos) {
    if (!a) return;
    audio_halt(a);
    int64_t ts = a->start_ts + (int64_t)(pos / a->tb);
    av_seek_frame(a->fmt, a->idx, ts, AVSEEK_FLAG_BACKWARD);
    avcodec_flush_buffers(a->dec);
    swr_close(a->swr);
    swr_init(a->swr);
    pthread_mutex_lock(&a->mu);
    a->start_pos = pos;
    a->t_first = 0;
    a->written = 0;
    a->eof = false;
    a->failed = false;
    pthread_mutex_unlock(&a->mu);
    if (spawn_sink(a) < 0) {
        fprintf(stderr, "gmplay: can't start gst-launch-0.10\n");
        return;
    }
    a->run = true;
    a->th_alive = pthread_create(&a->th, NULL, audio_thread, a) == 0;
}

double audio_clock(Audio *a) {
    if (!a || !a->th_alive) return -1;
    pthread_mutex_lock(&a->mu);
    double c = a->failed ? -1 : a->start_pos;   // holds at start_pos until sound starts
    if (a->t_first && !a->failed) {
        // Elapsed time since sound started, but never more than what's been
        // written minus what's still queued (catches underruns). After EOF the
        // writes stop, so only elapsed time counts.
        double elapsed = now_sec() - a->t_first;
        double queued_out = (double)a->written / BYTES_PER_SEC - PIPELINE_AHEAD;
        double played = a->eof || elapsed < queued_out ? elapsed : queued_out;
        if (played < 0) played = 0;
        c = a->start_pos + played - opt.av_delay;
        if (c < a->start_pos) c = a->start_pos;
    }
    pthread_mutex_unlock(&a->mu);
    return c;
}

void audio_close(Audio *a) {
    if (!a) return;
    audio_halt(a);
    swr_free(&a->swr);
    av_frame_free(&a->frm);
    av_packet_free(&a->pkt);
    avcodec_free_context(&a->dec);
    avformat_close_input(&a->fmt);
    pthread_mutex_destroy(&a->mu);
    free(a);
}
