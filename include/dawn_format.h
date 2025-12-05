#ifndef DAWN_FORMAT_H
#define DAWN_FORMAT_H

#include <stdbool.h>
#include "sequencer.h" /* provides NoteEvent, Instrument, note_name_to_freq */

#define DAWN_MAX_CHANNELS 8
#define DAWN_MAX_PATTERNS 64
#define DAWN_MAX_PATTERN_ROWS 256
#define DAWN_MAX_ORDER 256
#define DAWN_MAX_TITLE_LEN 128

typedef struct {
    int channel;         /* 0-based */
    int row_count;
    NoteEvent rows[DAWN_MAX_PATTERN_ROWS];
} DawnPatternChannel;

typedef struct {
    int id;
    int channel_count; /* number of channels defined in this pattern (<= DAWN_MAX_CHANNELS) */
    DawnPatternChannel channels[DAWN_MAX_CHANNELS];
} DawnPattern;

typedef struct {
    char title[DAWN_MAX_TITLE_LEN];
    int bpm;
    int ticks_per_beat;
    int channel_count; /* how many channels in this song */

    Instrument channel_instruments[DAWN_MAX_CHANNELS];

    int order_length;
    int order[DAWN_MAX_ORDER]; /* pattern ids */

    int pattern_count;
    DawnPattern patterns[DAWN_MAX_PATTERNS];
} DawnSong;

/* Parse a .dawn file and fill DawnSong. Returns true on success. */
bool dawn_parse_file(const char *filename, DawnSong *out_song);

#endif
