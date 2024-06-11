/*
Copyright 2024 Tomoaki Itoh
This software is released under the MIT License, see LICENSE.txt.
//*/

#include "LCWReverb.h"
#include "utils/buffer_ops.h"
#include "fx_api.h"

void LCWInitPreBuffer(LCWReverbBlock *block, float *buffer)
{
    LCWDelayBuffer *p = &(block->preBuffer);
    p->buffer = buffer;
    p->size = LCW_REVERB_PRE_SIZE;
    p->mask = LCW_REVERB_PRE_SIZE - 1;
    p->pointer = 0;
}

void LCWInitCombBuffer(LCWReverbBlock *block, float *buffer)
{
    LCWDelayBuffer *p = &(block->combBuffer);
    p->buffer = buffer;
    p->size = LCW_REVERB_COMB_BUFFER_TOTAL;
    p->mask = LCW_REVERB_COMB_SIZE - 1;
    p->pointer = 0;
}

void LCWInitApBuffer(LCWReverbBlock *block, float *buffer)
{
    LCWDelayBuffer *p = &(block->apBuffer);
    p->buffer = buffer;
    p->size = LCW_REVERB_COMB_BUFFER_TOTAL;
    p->mask = LCW_REVERB_AP_SIZE - 1;
    p->pointer = 0;
}

#define FIR_TAP_PREPARE (9)
void LCWInputPreBuffer(float *out, const float *in, LCWReverbBlock *block)
{
    // HPF, fc: 0.094, window: kaiser(3.395)
    const float fir[] = {
        0.00821397, 0.04162759, 0.10096903, 0.16154171, -0.80827342, 0.16154171, 0.10096903, 0.04162759, 0.00821397
    };

    LCWDelayBuffer *buf = &(block->preBuffer);
    buf->pointer = LCW_DELAY_BUFFER_DEC(buf);
    buf->buffer[buf->pointer] = in[0] + in[1];

    const uint32_t mask = buf->mask;
    const float *p = buf->buffer;
    const int32_t j = buf->pointer;
    const float *param = &(fir[0]);

    float zz = 0.f;
#if FIR_TAP_PREPARE == 9
    {
        const float src[] = {
            p[(j+0) & mask] + p[(j+8) & mask],
            p[(j+1) & mask] + p[(j+7) & mask],
            p[(j+2) & mask] + p[(j+6) & mask],
            p[(j+3) & mask] + p[(j+5) & mask],
            p[(j+4) & mask]
        };

        zz += (src[0] * param[0]);
        zz += (src[1] * param[1]);
        zz += (src[2] * param[2]);
        zz += (src[3] * param[3]);
        zz += (src[4] * param[4]);
    }
#else
    for (int32_t k=0; k<FIR_TAP_PREPARE; k++) {
        zz += (p[(j+k) & mask] * param[k]);
    }
#endif

    *out = zz;
}

#define FIR_TAP_COMB (9)
static const float combFirParams[][FIR_TAP_COMB] = {
    // fc = 0.118, gain = -1.336(dB)
    { 0.00005455, 0.01035617, 0.08402783, 0.23962698, 0.33186896, 0.23962698, 0.08402783, 0.01035617, 0.00005455 },
    // fc = 0.110, gain = -1.388(dB)
    { 0.00011571, 0.01195154, 0.08704718, 0.23802043, 0.32573029, 0.23802043, 0.08704718, 0.01195154, 0.00011571 },
    // fc = 0.102, gain = -1.439(dB)
    { 0.00018071, 0.01352785, 0.08989039, 0.23643697, 0.31992816, 0.23643697, 0.08989039, 0.01352785, 0.00018071 },
    // fc = 0.091, gain = -1.505(dB)
    { 0.00027437, 0.01563421, 0.09349490, 0.23432805, 0.31253694, 0.23432805, 0.09349490, 0.01563421, 0.00027437 }
};

void LCWInputCombLines(float *out, float in, LCWReverbBlock *block)
{
    LCWDelayBuffer *buf = &(block->combBuffer);
    buf->pointer = LCW_DELAY_BUFFER_DEC(buf);

    const uint32_t mask = buf->mask;
    const int32_t pointer = buf->pointer;

    float zn[LCW_REVERB_COMB_MAX];
    for (int32_t i=0; i<LCW_REVERB_COMB_MAX; i++) {
        float *p = buf->buffer + (LCW_REVERB_COMB_SIZE * i);
        const int32_t j = pointer + block->combDelaySize[i] - (FIR_TAP_COMB >> 1);
        const float fbGain = block->combFbGain[i];
        const float *param = combFirParams[i];

        float zz = .0f;
#if FIR_TAP_COMB == 9
        {
            const float src[] = {
                p[(j+0) & mask] + p[(j+8) & mask],
                p[(j+1) & mask] + p[(j+7) & mask],
                p[(j+2) & mask] + p[(j+6) & mask],
                p[(j+3) & mask] + p[(j+5) & mask],
                p[(j+4) & mask]
            };

            zz += (src[0] * param[0]);
            zz += (src[1] * param[1]);
            zz += (src[2] * param[2]);
            zz += (src[3] * param[3]);
            zz += (src[4] * param[4]);
        }
#else
        for (int32_t k=0; k<FIR_TAP_COMB; k++) {
            zz += (p[(uint32_t)(j+k) & mask] * param[k]);
        }
#endif
        // フィードバックを加算
        p[pointer] = in + (zz * fbGain);

        zn[i] = zz;
    }

    *out = zn[0] - zn[1] + zn[2] - zn[3];
}

float LCWInputAllPass1(float in, LCWReverbBlock *block)
{
    LCWDelayBuffer *buf = &(block->apBuffer);
    buf->pointer = LCW_DELAY_BUFFER_DEC(buf);

    const uint32_t mask = buf->mask;
    const int32_t pointer = buf->pointer;

    float out = in;
    for (int32_t i=0; i<LCW_REVERB_AP_MAX; i++) {
        const int32_t delaySize = block->apDelaySize[i];
        const float fbGain = block->apFbGain[i];

        float *p = buf->buffer + (LCW_REVERB_AP_SIZE * i);
        const float zn = p[(uint32_t)(pointer + delaySize) & mask];

        const float in2 = out - (zn * fbGain);
        p[pointer] = in2;

        out = zn + (in2 * fbGain);
    }

    return out;
}
