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
    for (int32_t i=0; i<LCW_REVERB_AP_MAX; i++) {
        LCWDelayBuffer *p = block->apBuffers + i;
        p->buffer = &(buffer[LCW_REVERB_AP_SIZE * i]);
        p->size = LCW_REVERB_AP_SIZE;
        p->mask = LCW_REVERB_AP_SIZE - 1;
        p->pointer = 0;
    }
}

#define FIR_TAP_PREPARE (9)
float LCWInputPreBuffer(float in, LCWReverbBlock *block)
{
    // HPF, fc: 0.094, window: kaiser(3.395)
    const float fir[] = {
        0.00821397, 0.04162759, 0.10096903, 0.16154171, -0.80827342, 0.16154171, 0.10096903, 0.04162759, 0.00821397
    };

    LCWDelayBuffer *buf = &(block->preBuffer);
    buf->pointer = LCW_DELAY_BUFFER_DEC(buf);
    buf->buffer[buf->pointer] = in;

    const uint32_t mask = buf->mask;
    const float *p = buf->buffer;
    const int32_t j = buf->pointer;
    const float *param = &(fir[0]);

    float out = 0.f;
    for (int32_t k=0; k<FIR_TAP_PREPARE; k++) {
        out += (p[(j+k) & mask] * param[k]);
    }

    return out;
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

    float tmp[LCW_REVERB_COMB_MAX];
    for (int32_t i=0; i<LCW_REVERB_COMB_MAX; i++) {
        float *p = buf->buffer + (LCW_REVERB_COMB_SIZE * i);
        const int32_t j = pointer + block->combDelaySize[i] - (FIR_TAP_COMB >> 1);
        const float fbGain = block->combFbGain[i];
        const float *param = combFirParams[i];

        float zn = .0f;
        for (int32_t k=0; k<FIR_TAP_COMB; k++) {
            zn += (p[(uint32_t)(j + k) & mask] * param[k]);
        }

        // フィードバックを加算
        p[pointer] = in + (zn * fbGain);

        tmp[i] = zn;
    }

    *out = tmp[0] - tmp[1] + tmp[2] - tmp[3];
}

static fast_inline float inputAllPass(float in, LCWDelayBuffer *buf, int32_t delaySize, float fbGain)
{
    buf->pointer = LCW_DELAY_BUFFER_DEC(buf);
    const float zn = LCW_DELAY_BUFFER_LUT(buf, delaySize);
    const float in2 = in - (zn * fbGain);

    buf->buffer[buf->pointer] = in2;
    return zn + (in2 * fbGain);
}

float LCWInputAllPass2(float in, LCWReverbBlock *block)
{
    const int32_t *delaySize = &(block->apDelaySize[0]);
    const float *fbGain = &(block->apFbGain[0]);

    float out = in;
    for (int32_t i=0; i<LCW_REVERB_AP_MAX; i+=2) {
        LCWDelayBuffer *buf = &(block->apBuffers[i]);
        buf->pointer = LCW_DELAY_BUFFER_DEC(buf);
        float zn = LCW_DELAY_BUFFER_LUT(buf, delaySize[i]);

        // 内側の処理
        zn = inputAllPass(
            zn, buf + 1, delaySize[i+1], fbGain[i+1]);

        // 外側の処理
        {
            const float in = out;
            const float in2 = in - (zn * fbGain[i]);

            buf->buffer[buf->pointer] = in2;
            out = zn + (in2 * fbGain[i]);
        }
    }

    return out;
}
