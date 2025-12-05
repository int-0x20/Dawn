#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "audio.h"
#include "dawn_format.h"

/* precise sleep */
#define _POSIX_C_SOURCE 199309L   // MUST be before any #include

#include <stdio.h>
#include <stdlib.h>
#include <time.h>      // defines struct timespec + nanosleep
#include <unistd.h>

void precise_sleep(double seconds) {
    if (seconds <= 0) return;

    struct timespec req;
    req.tv_sec  = (time_t)seconds;
    req.tv_nsec = (long)((seconds - req.tv_sec) * 1e9);

    nanosleep(&req, NULL);
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s song.dawn\n", argv[0]);
        return 1;
    }

    DawnSong song;
    if (!dawn_parse_file(argv[1], &song)) {
        fprintf(stderr, "Failed to parse %s\n", argv[1]);
        return 1;
    }

    printf("Loaded '%s' BPM=%d TPB=%d channels=%d patterns=%d order=%d\n",
        song.title, song.bpm, song.ticks_per_beat, song.channel_count, song.pattern_count, song.order_length);

    /* initialize audio */
    audio_init();

    /* playback state for each channel */
    int channel_pos[DAWN_MAX_CHANNELS];
    int channel_remaining_ticks[DAWN_MAX_CHANNELS];
    for (int c = 0; c < song.channel_count; c++) { channel_pos[c] = 0; channel_remaining_ticks[c] = 0; }

    double seconds_per_beat = 60.0 / (double)song.bpm;
    /* interpret ticks per beat in file as beats per quarter note; we choose TICKS_PER_BEAT = song.ticks_per_beat */
    int ticks_per_beat = song.ticks_per_beat > 0 ? song.ticks_per_beat : 4;
    double seconds_per_tick = seconds_per_beat / (double)ticks_per_beat;

    int order_idx = 0;

    while (order_idx < song.order_length) {
        /* find pattern by id */
        int pid = song.order[order_idx];
        DawnPattern *pat = NULL;
        for (int i = 0; i < song.pattern_count; i++) {
            if (song.patterns[i].id == pid) { pat = &song.patterns[i]; break; }
        }
        if (!pat) {
            fprintf(stderr, "Pattern %d not found in song\n", pid);
            order_idx++;
            continue;
        }

        /* reset per-channel positions for this pattern */
        for (int c = 0; c < song.channel_count; c++) {
            channel_pos[c] = 0;
            channel_remaining_ticks[c] = 0;
        }

        /* compute max rows among channels */
        int max_rows = 0;
        for (int c = 0; c < song.channel_count; c++) {
            int rc = pat->channels[c].row_count;
            if (rc > max_rows) max_rows = rc;
        }
        /* Play this pattern until all channels consumed */
        int pattern_done = 0;
        while (!pattern_done) {
            /* For each channel, if remaining ticks == 0, trigger next row (if any) */
            for (int c = 0; c < song.channel_count; c++) {
                if (channel_remaining_ticks[c] <= 0) {
                    int pos = channel_pos[c];
                    if (pos < pat->channels[c].row_count) {
                        NoteEvent ev = pat->channels[c].rows[pos];
                        /* if ev.frequency > 0 -> note; if ev.instr == INST_NOISE or ev.frequency==0 && token was x -> play noise */
                        if (ev.instr == INST_NOISE) {
                            /* play noise by setting channel with some frequency (we ignore freq for noise) */
                            audio_set_channel(c, 440.0f, INST_NOISE);
                        } else if (ev.frequency > 0.0f) {
                            audio_set_channel(c, ev.frequency, ev.instr);
                        } else {
                            /* rest */
                            audio_stop_channel(c);
                        }
                        /* if length_ticks not set in parsing (0), we default to 1 tick */
                        int ticks = ev.length_ticks > 0 ? ev.length_ticks : 1;
                        channel_remaining_ticks[c] = ticks;
                        channel_pos[c]++;
                    } else {
                        /* no more rows for this channel: stop channel */
                        audio_stop_channel(c);
                        channel_remaining_ticks[c] = 0;
                    }
                }
            }

            /* sleep one tick */
            precise_sleep(seconds_per_tick);

            /* decrement ticks */
            for (int c = 0; c < song.channel_count; c++) {
                if (channel_remaining_ticks[c] > 0) channel_remaining_ticks[c]--;
            }

            /* check if all channels are done */
            pattern_done = 1;
            for (int c = 0; c < song.channel_count; c++) {
                if (channel_pos[c] < pat->channels[c].row_count) { pattern_done = 0; break; }
                if (channel_remaining_ticks[c] > 0) { pattern_done = 0; break; }
            }
        }

        order_idx++;
    }

    /* ensure channels are silenced */
    for (int c = 0; c < song.channel_count; c++) audio_stop_channel(c);

    audio_shutdown();
    printf("Playback finished.\n");
    return 0;
}
