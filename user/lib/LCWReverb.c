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
    LCWDelayBuffer *p = &(block->combBuffers);
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
    const float *param = &(fir[0]);

    LCWDelayBuffer *buf = &(block->preBuffer);
    buf->pointer = LCW_DELAY_BUFFER_DEC(buf);
    buf->buffer[buf->pointer] = in;

    const uint32_t mask = buf->mask;
    const int32_t j = buf->pointer;
    const float *p = buf->buffer;

    float out = 0.f;
    for (int32_t k=0; k<FIR_TAP_PREPARE; k++) {
        out += (p[(j+k) & mask] * param[k]);
    }

    return out;
}

#define FIR_TAP_COMB (9)
void LCWInputCombLines(float *outL, float in, LCWReverbBlock *block)
{
    // LPF, fc: 0.091, window: kaiser(7.857)
    const float fir[] = {
        0.00027488, 0.01564522, 0.09351320, 0.23431705, 0.31249930, 0.23431705, 0.09351320, 0.01564522, 0.00027488
    };
    const float *param = &(fir[0]);

    LCWDelayBuffer *buf = &(block->combBuffers);
    buf->pointer = LCW_DELAY_BUFFER_DEC(buf);

    float out[LCW_REVERB_COMB_MAX];
    const uint32_t mask = buf->mask;
    for (int32_t i=0; i<LCW_REVERB_COMB_MAX; i++) {
        float *p = buf->buffer + (LCW_REVERB_COMB_SIZE * i);
        const int32_t j = buf->pointer + block->combDelaySize[i] - (FIR_TAP_COMB >> 1);

        out[i] = .0f;
        for (int32_t k=0; k<FIR_TAP_COMB; k++) {
            out[i] += (p[(uint32_t)(j + k) & mask] * param[k]);
        }

        // フィードバックを加算
        p[buf->pointer] = in + (out[i] * block->combFbGain[i]);
    }

    // memo:
    // ここで符号を変えて計算したものを2つ用意すればステレオ対応できる（？）
    *outL = out[0] - out[1] + out[2] - out[3];
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
            zn, &(block->apBuffers[i+1]), delaySize[i+1], fbGain[i+1]);

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
