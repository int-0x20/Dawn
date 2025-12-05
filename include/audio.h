#ifndef AUDIO_H
#define AUDIO_H

#include <SDL2/SDL.h>
#include <stdint.h>

typedef enum {
    INST_SINE = 1,
    INST_SQUARE,
    INST_TRIANGLE,
    INST_SAW,
    INST_NOISE
} Instrument;

typedef struct {
    int active;
    float frequency;
    Instrument instrument;
    float phase;
} Channel;

void audio_init(void);
void audio_shutdown(void);
void audio_set_channel(int id, float freq, Instrument inst);
void audio_stop_channel(int id);

#endif
