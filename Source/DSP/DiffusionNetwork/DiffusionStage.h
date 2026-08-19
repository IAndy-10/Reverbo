#pragma once
#include "DiffusionNetwork/DelayLine.h"

// Single stereo allpass diffusion stage.
// Uses Schroeder allpass: out = -g*in + delay + g*delay_out
// Real-time safe, no allocations after prepare().
class DiffusionStage {
public:
    void prepare(double /*sampleRate*/, int delaySamplesL, int delaySamplesR) {
        delayL.prepare(delaySamplesL + 4);
        delayR.prepare(delaySamplesR + 4);
        delayLen[0] = delaySamplesL;
        delayLen[1] = delaySamplesR;
    }

    void setCoefficient(float g) { coeff = g; }

    void processStereo(float& sampleL, float& sampleR) {
        sampleL = processChannel(sampleL, delayL, delayLen[0], z[0]);
        sampleR = processChannel(sampleR, delayR, delayLen[1], z[1]);
    }

    void reset() {
        delayL.clear();
        delayR.clear();
        z[0] = z[1] = 0.0f;
    }

private:
    float processChannel(float in, DelayLine& dl, int len, float& /*feedbackState*/) {
        float delayed = dl.read(len);
        float feedback = in + coeff * delayed;
        dl.write(feedback);
        float out = delayed - coeff * feedback;
        return out;
    }

    DelayLine delayL, delayR;
    int delayLen[2] = { 151, 167 };
    float coeff = 0.6f;
    float z[2] = { 0.0f, 0.0f };
};

