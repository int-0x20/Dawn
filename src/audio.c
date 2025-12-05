#define _USE_MATH_DEFINES
#include <math.h>
#include <stdlib.h>
#include "audio.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define SAMPLE_RATE 44100
#define CHANNEL_COUNT 8

static Channel channels[CHANNEL_COUNT];

static float generate_sample(Channel *ch) {
    if (!ch->active) return 0.0f;

    float s = 0.0f;
    float t = ch->phase;

    switch (ch->instrument) {
        case INST_SINE:
            s = sinf(2.0f * M_PI * t);
            break;

        case INST_SQUARE:
            s = (fmodf(t, 1.0f) < 0.5f) ? 1.0f : -1.0f;
            break;

        case INST_TRIANGLE:
            s = fabsf(fmodf(t * 2.0f, 2.0f) - 1.0f) * 2.0f - 1.0f;
            break;

        case INST_SAW:
            s = fmodf(t, 1.0f) * 2.0f - 1.0f;
            break;

        case INST_NOISE:
            s = ((float)rand() / RAND_MAX) * 2.0f - 1.0f;
            break;
    }

    ch->phase += ch->frequency / SAMPLE_RATE;
    if (ch->phase >= 1.0f) ch->phase -= 1.0f;

    return s * 0.2f;     // volume scaling
}

static void audio_callback(void *userdata, Uint8 *stream, int len) {
    (void)userdata;
    float *buffer = (float*)stream;
    int samples = len / sizeof(float);

    for (int i = 0; i < samples; i++) {
        float mix = 0.0f;
        for (int c = 0; c < CHANNEL_COUNT; c++)
            mix += generate_sample(&channels[c]);

        buffer[i] = mix;  // mixed audio
    }
}

void audio_init(void) {
    SDL_Init(SDL_INIT_AUDIO);

    SDL_AudioSpec want;
    SDL_zero(want);

    want.freq = SAMPLE_RATE;
    want.format = AUDIO_F32SYS;
    want.channels = 1;
    want.samples = 4096;
    want.callback = audio_callback;

    if (SDL_OpenAudio(&want, NULL) < 0) {
        fprintf(stderr, "SDL audio failed: %s\n", SDL_GetError());
        exit(1);
    }

    for (int i = 0; i < CHANNEL_COUNT; i++) {
        channels[i].active = 0;
        channels[i].phase = 0;
        channels[i].frequency = 0;
        channels[i].instrument = INST_SINE;
    }

    SDL_PauseAudio(0);
}

void audio_set_channel(int id, float freq, Instrument inst) {
    if (id < 0 || id >= CHANNEL_COUNT) return;
    channels[id].frequency = freq;
    channels[id].instrument = inst;
    channels[id].active = 1;
}

void audio_stop_channel(int id) {
    if (id < 0 || id >= CHANNEL_COUNT) return;
    channels[id].active = 0;
}

void audio_shutdown(void) {
    SDL_CloseAudio();
    SDL_Quit();
}
