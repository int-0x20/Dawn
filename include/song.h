#ifndef SONG_H
#define SONG_H

#define MAX_INSTR 32
#define MAX_PATTERNS 128
#define MAX_ROWS 64
#define MAX_ORDER 256

typedef enum {
    WAVE_SINE,
    WAVE_SQUARE,
    WAVE_SAW,
    WAVE_NOISE,
    WAVE_UNKNOWN
} WaveType;

typedef struct {
    int id;
    WaveType wave;
    // future: ADSR, filters, etc.
} Instrument;

typedef struct {
    int row;            // 0–63
    char note[4];       // "C-4", "D#3", or "---"
    char length[8];     // "1/4", "1/8", or "---"
    int instrument;     // instrument ID or -1
} PatternRow;

typedef struct {
    int id;
    int row_count;
    PatternRow rows[MAX_ROWS];
} Pattern;

typedef struct {
    int bpm;
    int instrument_count;
    Instrument instruments[MAX_INSTR];

    int pattern_count;
    Pattern patterns[MAX_PATTERNS];

    int order_length;
    int order[MAX_ORDER];
} Song;

#endif
