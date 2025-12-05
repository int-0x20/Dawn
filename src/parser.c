#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <stdlib.h>
#include "parser.h"

static WaveType parse_wave(const char *s) {
    if (strcmp(s, "SINE") == 0) return WAVE_SINE;
    if (strcmp(s, "SQUARE") == 0) return WAVE_SQUARE;
    if (strcmp(s, "SAW") == 0) return WAVE_SAW;
    if (strcmp(s, "NOISE") == 0) return WAVE_NOISE;
    return WAVE_UNKNOWN;
}

int load_song(const char *filename, Song *song) {
    FILE *f = fopen(filename, "r");
    if (!f) return 0;

    memset(song, 0, sizeof(Song));
    song->bpm = 120; // default

    char line[256];
    Pattern *current_pattern = NULL;

    while (fgets(line, sizeof(line), f)) {

        // Remove newline
        line[strcspn(line, "\n")] = 0;

        // Skip empty lines
        if (strlen(line) == 0) continue;

        // Skip comments (# ...)
        if (line[0] == '#') continue;

        // Parse BPM
        if (strncmp(line, "BPM", 3) == 0) {
            int bpm;
            sscanf(line, "BPM %d", &bpm);
            song->bpm = bpm;
            continue;
        }

        // Parse INSTR
        if (strncmp(line, "INSTR", 5) == 0) {
            int id;
            char wave_str[32];
            sscanf(line, "INSTR %d %s", &id, wave_str);

            Instrument *inst = &song->instruments[song->instrument_count++];
            inst->id = id;
            inst->wave = parse_wave(wave_str);
            continue;
        }

        // PATTERN start
        if (strncmp(line, "PATTERN", 7) == 0) {
            int id;
            sscanf(line, "PATTERN %d", &id);

            current_pattern = &song->patterns[song->pattern_count++];
            current_pattern->id = id;
            current_pattern->row_count = 0;
            continue;
        }

        // END pattern
        if (strcmp(line, "END") == 0) {
            current_pattern = NULL;
            continue;
        }

        // ORDER list
        if (strncmp(line, "ORDER", 5) == 0) {
            // Format: ORDER 0,1,2,1,0
            char *p = strchr(line, ' ');
            if (p) {
                p++;
                char *tok = strtok(p, ",");
                while (tok) {
                    song->order[song->order_length++] = atoi(tok);
                    tok = strtok(NULL, ",");
                }
            }
            continue;
        }

        // Pattern rows (only allowed inside a PATTERN block)
        if (current_pattern) {
            PatternRow *r = &current_pattern->rows[current_pattern->row_count++];
            char inst_str[8];

            // Format: 00 C-4 1/4 0
            sscanf(line, "%d %3s %7s %s",
                   &r->row, r->note, r->length, inst_str);

            r->instrument = (strcmp(inst_str, "--") == 0) ? -1 : atoi(inst_str);

            continue;
        }
    }

    fclose(f);
    return 1;
}
