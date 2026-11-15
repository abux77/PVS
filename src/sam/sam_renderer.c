/* Bounded adapter around the existing s-macke/SAM C renderer.
 * The synthesis algorithm lives in third_party/sam/render.c. */
#include "sam_engine.h"
#include "render.h"
#include <stdio.h>
#include <string.h>

/* The upstream renderer shares these 6502-style registers with its caller.
 * They have one owner here; no second reciter definition or -fcommon is needed. */
unsigned char A, X, Y;
unsigned char mem44, mem47, mem49, mem39, mem50, mem51, mem53, mem56;
unsigned char speed, pitch;
unsigned char phonemeIndexOutput[60], stressOutput[60], phonemeLengthOutput[60];
int singmode, debug, bufferpos;
void ResetRenderOutput(void);

static struct {
    sam_pcm_sink_t sink;
    void *context;
    unsigned position;
    uint8_t pending[5], block[128];
    size_t used;
    bool okay;
} output;

int PVSOutputOkay(void) { return output.okay; }

void PVSOutputSamples(unsigned position, const unsigned char samples[5]) {
    /* Upstream writes five samples ahead and overwrites some on the next
     * timetable event. Only publish samples which can no longer be changed. */
    while (output.okay && output.position < position) {
        output.block[output.used++] = output.pending[0];
        memmove(output.pending, output.pending + 1, 4);
        output.pending[4] = 0;
        ++output.position;
        if (output.used == sizeof output.block) {
            output.okay = output.sink(output.block, output.used, output.context);
            output.used = 0;
        }
    }
    memcpy(output.pending, samples, 5);
}

void PrintOutput(unsigned char *flags, unsigned char *f1, unsigned char *f2,
                 unsigned char *f3, unsigned char *a1, unsigned char *a2,
                 unsigned char *a3, unsigned char *pitches) {
    for (unsigned i = 0; i < 256; ++i)
        printf("SAM frame%u flag%u f:%u/%u/%u a:%u/%u/%u pitch%u\n",
               i, flags[i], f1[i], f2[i], f3[i], a1[i], a2[i], a3[i], pitches[i]);
}

bool sam_render(const sam_phonemes_t *p, uint8_t voice_speed, uint8_t voice_pitch,
                uint8_t mouth, uint8_t throat, sam_pcm_sink_t sink, void *context) {
    if (!p || !sink || !voice_speed || p->count >= SAM_PHONEME_CAPACITY) return false;
    unsigned total = 0;
    for (size_t i = 0; i < p->count; ++i) {
        if (p->index[i] >= 80 || p->stress[i] > 9) return false;
        total += p->length[i];
        if (total > SAM_FRAME_CAPACITY) return false;
    }
    speed = voice_speed;
    pitch = voice_pitch;
    SetMouthThroat(mouth, throat);
    memset(&output, 0, sizeof output);
    output.sink = sink;
    output.context = context;
    output.okay = true;
    ResetRenderOutput();
    size_t next = 0;
    while (next < p->count && output.okay) {
        unsigned count = 0, frames = 0;
        memset(phonemeIndexOutput, 255, sizeof phonemeIndexOutput);
        memset(phonemeLengthOutput, 0, sizeof phonemeLengthOutput);
        memset(stressOutput, 0, sizeof stressOutput);
        while (next < p->count && count < 59) {
            unsigned length = p->length[next];
            if (frames + length > 220 && count) break;
            if (length > 220) return false;
            if (length && p->index[next]) {
                phonemeIndexOutput[count] = p->index[next];
                phonemeLengthOutput[count] = (unsigned char)length;
                stressOutput[count++] = p->stress[next];
                frames += length;
            }
            ++next;
        }
        /* No arbitrary text splits: each renderer batch ends at a complete
         * internal SAM phoneme and stays inside its 256-frame workspace. */
        if (count) Render();
    }
    /* The upstream output length excludes its speculative five-sample tail. */
    if (output.okay && output.used)
        output.okay = sink(output.block, output.used, context);
    return output.okay;
}

uint8_t sam_quantize(uint8_t sample, uint8_t bits) {
    if (bits >= 8) return sample;
    if (bits < 1) bits = 1;
    int value = (int)sample - 128;
    int step = 1 << (8 - bits), half = step >> 1;
    value = value >= 0 ? ((value + half) / step) * step : -(((-value + half) / step) * step);
    if (value > 127) value = 127;
    if (value < -128) value = -128;
    return (uint8_t)(value + 128);
}
