#ifndef SEQUENCER_H
#define SEQUENCER_H

#include <stdint.h>
#include "audio.h"

#define DAWN_CHANNELS 8
#define TICKS_PER_BEAT 96
#define MAX_PATTERN_ROWS 256
#define MAX_PATTERNS 64
#define MAX_ORDER 256

/* Note event for a single row in a pattern */
typedef struct {
    float frequency;       /* 0.0 == rest */
    int length_ticks;      /* how many ticks this note lasts */
    Instrument instr;      /* which instrument to play */
} NoteEvent;

/* Pattern = sequence of rows (NoteEvent) */
typedef struct {
    int id;
    int row_count;
    NoteEvent rows[MAX_PATTERN_ROWS];
} Pattern;

/* Sequencer / song container */
typedef struct {
    int bpm;
    int ticks_per_beat;
    double seconds_per_tick;

    Pattern patterns[MAX_PATTERNS];
    int pattern_count;

    int order[MAX_ORDER];     /* pattern ids in order */
    int order_length;

    /* per-channel playback pointers/state */
    int channel_pos[DAWN_CHANNELS];      /* current row index in pattern */
    int channel_remaining_ticks[DAWN_CHANNELS];

    /* song playback pointers */
    int order_index;    /* which order entry is playing */
    int current_pattern_id;

    int is_playing;
} Sequencer;

/* API */
void sequencer_init(Sequencer *s, int bpm);
void sequencer_add_pattern(Sequencer *s, Pattern *p);
void sequencer_set_order(Sequencer *s, int *order, int order_len);
void sequencer_start(Sequencer *s);
void sequencer_stop(Sequencer *s);

/* Blocking runner: runs until the order is finished (or stopped) */
void sequencer_run_blocking(Sequencer *s);

/* helper: convert note name to frequency (C-4, C4, D#3, A4, etc.) */
float note_name_to_freq(const char *name);

/* helpers for building dummy patterns in user code */
Pattern make_empty_pattern(int id);
NoteEvent make_note(const char *note_name, const char *len_token, Instrument inst);
NoteEvent make_rest(const char *len_token);

#endif
