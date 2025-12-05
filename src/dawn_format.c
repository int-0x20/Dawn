#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "dawn_format.h"

/* Helpers */
static char *trim(char *s) {
    if (!s) return s;
    while (*s && isspace((unsigned char)*s)) s++;
    if (*s == 0) return s;
    char *end = s + strlen(s) - 1;
    while (end > s && isspace((unsigned char)*end)) end--;
    end[1] = '\0';
    return s;
}

/* Parse instrument name */
static Instrument parse_instrument(const char *name) {
    if (!name) return INST_SINE;
    if (strcasecmp(name, "SQUARE") == 0) return INST_SQUARE;
    if (strcasecmp(name, "SINE") == 0) return INST_SINE;
    if (strcasecmp(name, "TRIANGLE") == 0 || strcasecmp(name, "TRI") == 0) return INST_TRIANGLE;
    if (strcasecmp(name, "SAW") == 0) return INST_SAW;
    if (strcasecmp(name, "NOISE") == 0) return INST_NOISE;
    return INST_SINE;
}

/* Parse a single token into a NoteEvent (uses note_name_to_freq from sequencer.h) */
static bool token_to_noteevent(const char *tok, NoteEvent *ev, Instrument default_instr) {
    if (!tok || !ev) return false;
    if (strcmp(tok, "-") == 0) {
        ev->frequency = 0.0f;
        ev->length_ticks = 0; /* will be filled later by caller if needed */
        ev->instr = default_instr;
        return true;
    }
    if (strcmp(tok, "x") == 0 || strcmp(tok, "X") == 0) {
        ev->frequency = 0.0f;   /* noise uses instrument type */
        ev->length_ticks = 0;
        ev->instr = INST_NOISE;
        return true;
    }
    float f = note_name_to_freq(tok);
    if (f <= 0.0f) return false;
    ev->frequency = f;
    ev->length_ticks = 0;
    ev->instr = default_instr;
    return true;
}

/* Parse a token list string (space-separated tokens) into channel rows.
   Supports tokens ending with ',' or ';' to indicate continuation or termination.
   We pass the default_instr to fill each NoteEvent instr if the token is a note.
*/
static bool parse_channel_token_lines(const char *buffer, DawnPatternChannel *chan, Instrument default_instr) {
    if (!buffer || !chan) return false;
    chan->row_count = 0;

    /* we'll tokenize by whitespace but keep track of tokens that may include trailing , or ; */
    const char *p = buffer;
    char token[64];
    while (*p) {
        /* skip whitespace */
        while (*p && isspace((unsigned char)*p)) p++;
        if (!*p) break;

        /* read token up to whitespace */
        int ti = 0;
        while (*p && !isspace((unsigned char)*p) && ti < (int)sizeof(token)-1) {
            token[ti++] = *p++;
        }
        token[ti] = '\0';
        if (ti == 0) continue;

        /* check trailing punctuation */
        int len = strlen(token);
        bool cont = false;
        bool term = false;
        if (len > 0) {
            if (token[len-1] == ',') { cont = true; token[len-1] = '\0'; }
            else if (token[len-1] == ';') { term = true; token[len-1] = '\0'; }
        }

        NoteEvent ev;
        if (!token_to_noteevent(token, &ev, default_instr)) {
            fprintf(stderr, "dawn parser: invalid note token '%s'\n", token);
            return false;
        }

        if (chan->row_count >= DAWN_MAX_PATTERN_ROWS) {
            fprintf(stderr, "dawn parser: pattern channel too long\n");
            return false;
        }
        chan->rows[chan->row_count++] = ev;

        /* if term found, stop parsing (caller already collected lines for this channel) */
        if (term) break;
        /* otherwise continue — cont=true means continue to next line; but since caller concatenated lines
           into buffer until ';', we don't need special handling here. */
    }

    return true;
}

/* Read a channel's token lines from the file: after the "CHn:" line the rest of that line may contain tokens.
   We read subsequent lines until we find a token that ends with ';' for this channel.
   We accumulate the tokens into a temporary buffer and call parse_channel_token_lines() to fill chan.
*/
static bool read_channel_pattern(FILE *fp, char *first_line_after_colon, DawnPatternChannel *chan, Instrument default_instr) {
    if (!fp || !first_line_after_colon || !chan) return false;

    char accum[8192];
    accum[0] = '\0';

    /* copy the first bits */
    strncat(accum, first_line_after_colon, sizeof(accum)-strlen(accum)-1);
    strncat(accum, "\n", sizeof(accum)-strlen(accum)-1);

    bool found_semicolon = false;

    /* If the first line already contains ';' then we're done; otherwise keep reading */
    for (;;) {
        /* check if accum contains ';' that is not inside a quote (we don't support quotes in pattern tokens) */
        if (strchr(accum, ';')) { found_semicolon = true; break; }

        /* peek next line */
        long pos = ftell(fp);
        char line[512];
        if (!fgets(line, sizeof(line), fp)) break;
        char *t = trim(line);
        if (t[0] == '#' || t[0] == 0) continue; /* skip blank/comments */
        /* append */
        strncat(accum, t, sizeof(accum)-strlen(accum)-1);
        strncat(accum, "\n", sizeof(accum)-strlen(accum)-1);
    }

    /* Now parse accumulated tokens */
    if (!parse_channel_token_lines(accum, chan, default_instr)) {
        return false;
    }

    return true;
}

/* Parse a standard key/value or line that appears outside patterns */
static bool parse_global_key(char *line, DawnSong *song) {
    char *p = trim(line);
    if (!p || *p == '\0' || *p == '#') return true;

    if (strncasecmp(p, "TITLE", 5) == 0) {
        char *q = strchr(p, '"');
        if (!q) {
            /* allow TITLE without quotes (take rest) */
            char *rest = p + 5;
            rest = trim(rest);
            strncpy(song->title, rest, DAWN_MAX_TITLE_LEN-1);
            return true;
        }
        q++;
        char *r = strchr(q, '"');
        if (!r) return false;
        int len = (int)(r - q);
        if (len >= DAWN_MAX_TITLE_LEN) len = DAWN_MAX_TITLE_LEN - 1;
        strncpy(song->title, q, len);
        song->title[len] = '\0';
        return true;
    }

    if (strncasecmp(p, "TEMPO", 5) == 0) {
        int v = atoi(p + 5);
        if (v > 0) song->bpm = v;
        return true;
    }

    if (strncasecmp(p, "TPB", 3) == 0) {
        int v = atoi(p + 3);
        if (v > 0) song->ticks_per_beat = v;
        return true;
    }

    if (strncasecmp(p, "CHANNELS", 8) == 0) {
        int v = atoi(p + 8);
        if (v > 0 && v <= DAWN_MAX_CHANNELS) song->channel_count = v;
        return true;
    }

    if (strncasecmp(p, "ORDER", 5) == 0) {
        /* tokens after ORDER are pattern ids */
        char *tok = strtok(p + 5, " \t");
        int idx = 0;
        while (tok && idx < DAWN_MAX_ORDER) {
            int id = atoi(tok);
            song->order[idx++] = id;
            tok = strtok(NULL, " \t");
        }
        song->order_length = idx;
        return true;
    }

    if (strncasecmp(p, "CH", 2) == 0) {
        /* CHn INSTR NAME  -> set instrument for channel n */
        /* find number after CH */
        int chnum = atoi(p + 2) - 1;
        if (chnum < 0 || chnum >= DAWN_MAX_CHANNELS) return false;
        char *instr_pos = strstr(p, "INSTR");
        if (!instr_pos) return false;
        instr_pos += 5;
        instr_pos = trim(instr_pos);
        song->channel_instruments[chnum] = parse_instrument(instr_pos);
        return true;
    }

    /* unknown global key — ignore */
    return true;
}

/* Main parser implementation */
bool dawn_parse_file(const char *filename, DawnSong *out_song) {
    if (!filename || !out_song) return false;
    FILE *fp = fopen(filename, "r");
    if (!fp) {
        fprintf(stderr, "dawn: could not open %s\n", filename);
        return false;
    }

    /* initialize defaults */
    memset(out_song, 0, sizeof(DawnSong));
    strncpy(out_song->title, "", DAWN_MAX_TITLE_LEN);
    out_song->bpm = 120;
    out_song->ticks_per_beat = 4; /* default small TPB, but you can pick larger in file */
    out_song->channel_count = 5;
    out_song->pattern_count = 0;
    out_song->order_length = 0;

    for (int i = 0; i < DAWN_MAX_CHANNELS; i++) out_song->channel_instruments[i] = INST_SINE;

    char rawline[512];
    DawnPattern *current_pattern = NULL;
    bool in_pattern = false;

    while (fgets(rawline, sizeof(rawline), fp)) {
        char *line = trim(rawline);
        if (!line || *line == '\0' || *line == '#') continue;

        if (!in_pattern) {
            if (strncasecmp(line, "PATTERN", 7) == 0) {
                /* Begin a pattern */
                int pid = atoi(line + 7);
                if (pid < 0 || pid >= DAWN_MAX_PATTERNS) {
                    fprintf(stderr, "dawn: invalid pattern id %d\n", pid);
                    fclose(fp);
                    return false;
                }
                current_pattern = &out_song->patterns[out_song->pattern_count++];
                memset(current_pattern, 0, sizeof(DawnPattern));
                current_pattern->id = pid;
                current_pattern->channel_count = out_song->channel_count;
                for (int c = 0; c < DAWN_MAX_CHANNELS; c++) {
                    current_pattern->channels[c].channel = c;
                    current_pattern->channels[c].row_count = 0;
                }
                in_pattern = true;
                continue;
            } else {
                /* global key/value */
                if (!parse_global_key(line, out_song)) {
                    fprintf(stderr, "dawn: malformed global line: %s\n", line);
                    /* not fatal; continue */
                }
                continue;
            }
        } else {
            /* inside a pattern: expect lines starting with CHn: or blank to finish pattern */
            if (strncasecmp(line, "CH", 2) == 0) {
                /* CHn: ... */
                int chnum = atoi(line + 2) - 1;
                if (chnum < 0 || chnum >= DAWN_MAX_CHANNELS) {
                    fprintf(stderr, "dawn: invalid channel in pattern: %s\n", line);
                    fclose(fp);
                    return false;
                }
                char *colon = strchr(line, ':');
                if (!colon) {
                    fprintf(stderr, "dawn: malformed channel line (missing ':'): %s\n", line);
                    fclose(fp);
                    return false;
                }
                colon++;
                char *after = trim(colon);
                /* Build a small buffer: the rest of the current line plus subsequent lines until we find a ';' for this channel. */
                /* We'll read following lines from fp until a semicolon appears in the concatenation. */
                char buf[8192];
                buf[0] = '\0';
                strncat(buf, after, sizeof(buf) - strlen(buf) - 1);
                strncat(buf, "\n", sizeof(buf) - strlen(buf) - 1);

                /* If this chunk already contains ';', we can parse it. Otherwise read more lines. */
                while (!strchr(buf, ';')) {
                    long pos = ftell(fp);
                    if (!fgets(rawline, sizeof(rawline), fp)) break;
                    char *ln = trim(rawline);
                    if (!ln || *ln == '\0' || *ln == '#') continue;
                    /* If the next line is "CH" or "PATTERN" or global keyword, then the missing ';' is an error.
                       But we'll allow patterns to be ended by a different syntax: require ';' per channel as specified.
                    */
                    /* Append */
                    strncat(buf, ln, sizeof(buf) - strlen(buf) - 1);
                    strncat(buf, "\n", sizeof(buf) - strlen(buf) - 1);
                }

                /* Parse the accumulated buffer tokens into the channel */
                if (!parse_channel_token_lines(buf, &current_pattern->channels[chnum], out_song->channel_instruments[chnum])) {
                    fprintf(stderr, "dawn: failed to parse pattern channel %d\n", chnum+1);
                    fclose(fp);
                    return false;
                }
                continue;
            } else {
                /* Not a CHn: line. Could be end of pattern (blank), or next PATTERN start. We'll treat any non-CH/ non-comment as end-of-pattern,
                   but also allow encountering a new PATTERN keyword in which case we step back by fseek to let outer loop handle it.
                */
                if (strncasecmp(line, "PATTERN", 7) == 0) {
                    /* Move file pointer back to start of this line so outer loop sees PATTERN */
                    long cur = ftell(fp);
                    size_t linelen = strlen(rawline);
                    if (cur >= (long)linelen) fseek(fp, cur - (long)linelen, SEEK_SET);
                    in_pattern = false;
                    current_pattern = NULL;
                    continue;
                }
                /* If it's a global key while inside pattern, treat it as end of pattern and handle global on next iteration */
                in_pattern = false;
                current_pattern = NULL;
                /* re-process this line as global: move file pointer back and let outer loop read it again */
                long cur = ftell(fp);
                size_t linelen = strlen(rawline);
                if (cur >= (long)linelen) fseek(fp, cur - (long)linelen, SEEK_SET);
                continue;
            }
        }
    }

    fclose(fp);
    return true;
}
