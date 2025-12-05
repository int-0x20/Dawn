#define _POSIX_C_SOURCE 199309L
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <time.h>
#include "sequencer.h"
#include "audio.h"
#include <stdlib.h>

/* internal helpers */
static int parse_length_token(const char *tok, int ticks_per_beat) {
    /* Supports: w, h, q, e, s (whole, half, quarter, eighth, sixteenth)
       or fraction like 1/4, 1/8, 1/16, 1/1
    */
    if (!tok || tok[0] == '\0') return ticks_per_beat; /* default quarter? but fallback */
    if (strcmp(tok, "w") == 0) return ticks_per_beat * 4;
    if (strcmp(tok, "h") == 0) return ticks_per_beat * 2;
    if (strcmp(tok, "q") == 0) return ticks_per_beat;
    if (strcmp(tok, "e") == 0) return ticks_per_beat / 2;
    if (strcmp(tok, "s") == 0) return ticks_per_beat / 4;

    /* fraction form 1/4, 1/8, 1/16, 1/1 */
    int num = 0, den = 1;
    if (sscanf(tok, "%d/%d", &num, &den) == 2 && den != 0) {
        double frac = (double)num / (double)den;
        /* whole note = 4 beats, so ticks = ticks_per_beat * 4 * frac? */
        /* In our notation 1/4 should be quarter, so we interpret 1/4 = 1/4 of a whole; whole = 4 beats */
        /* ticks_per_beat corresponds to quarter note. So 1/4 (quarter) = ticks_per_beat */
        /* To unify: treat fraction as portion of whole note: ticks = ticks_per_beat * 4 * frac */
        int ticks = (int)round(ticks_per_beat * 4.0 * frac);
        return ticks > 0 ? ticks : ticks_per_beat;
    }

    /* fallback: quarter */
    return ticks_per_beat;
}

/* sleep for given seconds (double) using nanosleep */
static void precise_sleep(double seconds) {
    if (seconds <= 0.0) return;
    struct timespec req;
    req.tv_sec = (time_t)seconds;
    req.tv_nsec = (long)((seconds - (time_t)seconds) * 1e9);
    nanosleep(&req, NULL);
}

/* Sequencer API implementations */

void sequencer_init(Sequencer *s, int bpm) {
    if (!s) return;
    memset(s, 0, sizeof(Sequencer));
    s->bpm = bpm > 0 ? bpm : 120;
    s->ticks_per_beat = TICKS_PER_BEAT;
    double beat_seconds = 60.0 / (double)s->bpm;
    s->seconds_per_tick = beat_seconds / (double)s->ticks_per_beat;

    s->pattern_count = 0;
    s->order_length = 0;
    s->order_index = 0;
    s->is_playing = 0;

    for (int c = 0; c < DAWN_CHANNELS; c++) {
        s->channel_pos[c] = 0;
        s->channel_remaining_ticks[c] = 0;
    }
}

void sequencer_add_pattern(Sequencer *s, Pattern *p) {
    if (!s || !p) return;
    if (s->pattern_count >= MAX_PATTERNS) return;
    s->patterns[s->pattern_count++] = *p;
}

void sequencer_set_order(Sequencer *s, int *order, int order_len) {
    if (!s || !order || order_len <= 0) return;
    int n = order_len > MAX_ORDER ? MAX_ORDER : order_len;
    for (int i = 0; i < n; i++) s->order[i] = order[i];
    s->order_length = n;
}

/* find pattern pointer by id (pattern.id) */
static Pattern * find_pattern(Sequencer *s, int id) {
    for (int i = 0; i < s->pattern_count; i++) {
        if (s->patterns[i].id == id) return &s->patterns[i];
    }
    return NULL;
}

void sequencer_start(Sequencer *s) {
    if (!s) return;
    s->is_playing = 1;
    s->order_index = 0;
    s->current_pattern_id = (s->order_length > 0) ? s->order[0] : -1;
    for (int c = 0; c < DAWN_CHANNELS; c++) {
        s->channel_pos[c] = 0;
        s->channel_remaining_ticks[c] = 0;
    }
}

/* Stop playback and silence channels */
void sequencer_stop(Sequencer *s) {
    if (!s) return;
    s->is_playing = 0;
    for (int c = 0; c < DAWN_CHANNELS; c++) {
        audio_stop_channel(c);
    }
}

/* Advance one tick: check each channel for event boundaries and trigger notes */
static void sequencer_advance_tick(Sequencer *s) {
    if (!s || !s->is_playing) return;
    if (s->order_index >= s->order_length) {
        /* nothing more to play */
        sequencer_stop(s);
        return;
    }

    Pattern *pat = find_pattern(s, s->order[s->order_index]);
    if (!pat) {
        /* advance to next pattern in order */
        s->order_index++;
        if (s->order_index >= s->order_length) { sequencer_stop(s); return; }
        pat = find_pattern(s, s->order[s->order_index]);
        if (!pat) { sequencer_stop(s); return; }
    }

    /* For each channel: if no remaining ticks, try to start next row (if exists) */
    for (int ch = 0; ch < DAWN_CHANNELS; ch++) {
        if (s->channel_remaining_ticks[ch] <= 0) {
            /* compute row index for this channel relative to current pattern */
            int row = s->channel_pos[ch];
            if (row < pat->row_count) {
                NoteEvent *ev = &pat->rows[row];
                if (ev->frequency > 0.0f) {
                    /* trigger channel */
                    audio_set_channel(ch, ev->frequency, ev->instr);
                    s->channel_remaining_ticks[ch] = ev->length_ticks;
                } else {
                    /* rest: stop channel */
                    audio_stop_channel(ch);
                    s->channel_remaining_ticks[ch] = ev->length_ticks;
                }
                s->channel_pos[ch]++; /* advance this channel's pointer */
            } else {
                /* channel finished this pattern: stop channel */
                audio_stop_channel(ch);
                s->channel_remaining_ticks[ch] = 0;
            }
        } else {
            /* This channel is currently sustaining a note; decrement later */
        }

        /* decrement remaining ticks for this channel */
        if (s->channel_remaining_ticks[ch] > 0) s->channel_remaining_ticks[ch]--;
    }

    /* Check if all channels have passed the end of this pattern (i.e., their pos >= row_count)
       If so, advance order_index to next pattern and reset channel positions.
    */
    int all_done = 1;
    for (int ch = 0; ch < DAWN_CHANNELS; ch++) {
        if (s->channel_pos[ch] < pat->row_count) { all_done = 0; break; }
    }
    if (all_done) {
        s->order_index++;
        if (s->order_index < s->order_length) {
            s->current_pattern_id = s->order[s->order_index];
            /* reset channel positions for next pattern */
            for (int ch = 0; ch < DAWN_CHANNELS; ch++) {
                s->channel_pos[ch] = 0;
                s->channel_remaining_ticks[ch] = 0;
            }
        } else {
            /* no more patterns: stop */
            sequencer_stop(s);
        }
    }
}

/* Blocking runner: runs until sequencer stops */
void sequencer_run_blocking(Sequencer *s) {
    if (!s) return;
    if (!s->is_playing) sequencer_start(s);
    double tick_seconds = s->seconds_per_tick;

    /* Use a loop with precise_sleep */
    while (s->is_playing) {
        precise_sleep(tick_seconds);
        sequencer_advance_tick(s);
    }
}

/* Utilities */

Pattern make_empty_pattern(int id) {
    Pattern p;
    memset(&p, 0, sizeof(Pattern));
    p.id = id;
    p.row_count = 0;
    return p;
}

/* Create NoteEvent from note name and length token */
NoteEvent make_note(const char *note_name, const char *len_token, Instrument inst) {
    NoteEvent e;
    e.frequency = note_name_to_freq(note_name);
    e.length_ticks = parse_length_token(len_token, TICKS_PER_BEAT);
    e.instr = inst;
    return e;
}

NoteEvent make_rest(const char *len_token) {
    NoteEvent e;
    e.frequency = 0.0f;
    e.length_ticks = parse_length_token(len_token, TICKS_PER_BEAT);
    e.instr = INST_SINE;
    return e;
}

/* Convert a note name (e.g., C4, C-4, D#3, A4) to frequency in Hz.
   Returns 0 for invalid names.
*/
float note_name_to_freq(const char *name) {
    if (!name || name[0] == '\0') return 0.0f;

    /* parse note letter */
    char note = 0;
    int i = 0;
    while (name[i] == ' ') i++;
    note = name[i++];
    int accidental = 0;
    if (name[i] == '#' || name[i] == '+') { accidental = 1; i++; }
    else if (name[i] == '-') { /* allow C-4 style: dash before octave */ 
        /* some formats use C-4 meaning note C octave 4 with - as delimiter */
        /* we'll handle dash as delimiter and continue to parse octave below */
    }

    /* find octave by scanning for digit */
    int octave = 4; /* default */
    int j = 0;
    while (name[j] && !(name[j] >= '0' && name[j] <= '9')) j++;
    if (name[j]) {
        octave = atoi(&name[j]);
    } else {
        /* try last char */
        int L = strlen(name);
        if (L > 0 && name[L-1] >= '0' && name[L-1] <= '9') {
            octave = name[L-1] - '0';
        }
    }

    int note_base = -100;
    switch (note) {
        case 'C': note_base = 0; break;
        case 'D': note_base = 2; break;
        case 'E': note_base = 4; break;
        case 'F': note_base = 5; break;
        case 'G': note_base = 7; break;
        case 'A': note_base = 9; break;
        case 'B': note_base = 11; break;
        default: return 0.0f;
    }

    int semitone = note_base + accidental;
    /* MIDI number for this note: MIDI = (octave+1)*12 + semitone
       (C4 => MIDI 60) */
    int midi = (octave + 1) * 12 + semitone;
    double freq = 440.0 * pow(2.0, (midi - 69) / 12.0);
    return (float)freq;
}
