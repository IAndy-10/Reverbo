#include "EarlyReflections.h"
#include <algorithm>

void EarlyReflections::prepare(double sampleRate) {
    sr = sampleRate;
    double ratio = sampleRate / 44100.0;

    for (size_t t = 0; t < static_cast<size_t>(NUM_TAPS); ++t) {
        tapL[t] = static_cast<int>(BASE_TAPS_L[t] * ratio);
        tapR[t] = static_cast<int>(BASE_TAPS_R[t] * ratio);
        delayL[t].prepare(tapL[t] + 4);
        delayR[t].prepare(tapR[t] + 4);
    }

    spin.prepare(sampleRate);
    spin.setRate(1.0f);
}

void EarlyReflections::setEnabled(bool on) { enabled = on; }
void EarlyReflections::setAmount(float a)   { amount = a; spin.setAmount(a * 0.05f); }
void EarlyReflections::setRate(float hz)    { spin.setRate(hz); }
void EarlyReflections::setShape(float s)    { shape = s; }

void EarlyReflections::processOut(const juce::AudioBuffer<float>& input,
                                   juce::AudioBuffer<float>& output) {
    output.clear();
    if (!enabled || amount <= 0.001f) return;

    int numChannels = juce::jmin(input.getNumChannels(), 2);
    int numSamples  = juce::jmin(input.getNumSamples(), output.getNumSamples());

    for (int i = 0; i < numSamples; ++i) {
        float spinL, spinR;
        spin.getNextPair(spinL, spinR);

        float inL = (numChannels >= 1) ? input.getSample(0, i) : 0.0f;
        float inR = (numChannels >= 2) ? input.getSample(1, i) : inL;

        for (size_t t = 0; t < static_cast<size_t>(NUM_TAPS); ++t) {
            delayL[t].write(inL);
            delayR[t].write(inR);
        }

        float erL = 0.0f, erR = 0.0f;
        for (size_t t = 0; t < static_cast<size_t>(NUM_TAPS); ++t) {
            float g = TAP_GAINS_GRADUAL[t] + shape * (TAP_GAINS_RAPID[t] - TAP_GAINS_GRADUAL[t]);
            g *= amount;
            erL += delayL[t].read(tapL[t]) * g;
            erR += delayR[t].read(tapR[t]) * g;
        }

        erL += spinR * 0.15f;
        erR += spinL * 0.15f;

        if (numChannels >= 1) output.setSample(0, i, erL);
        if (numChannels >= 2) output.setSample(1, i, erR);
    }
}

void EarlyReflections::reset() {
    for (size_t t = 0; t < static_cast<size_t>(NUM_TAPS); ++t) {
        delayL[t].clear();
        delayR[t].clear();
    }
    spin.reset();
}

void EarlyReflections::process(juce::AudioBuffer<float>& buffer) {
    if (!enabled || amount <= 0.001f) return;

    int numChannels = juce::jmin(buffer.getNumChannels(), 2);
    int numSamples  = buffer.getNumSamples();

    for (int i = 0; i < numSamples; ++i) {
        float spinL, spinR;
        spin.getNextPair(spinL, spinR);

        float inL = (numChannels >= 1) ? buffer.getSample(0, i) : 0.0f;
        float inR = (numChannels >= 2) ? buffer.getSample(1, i) : inL;

        // Write into each delay line
        for (size_t t = 0; t < static_cast<size_t>(NUM_TAPS); ++t) {
            delayL[t].write(inL);
            delayR[t].write(inR);
        }

        // Accumulate taps — blend between gradual and rapid gain profiles via shape.
        float erL = 0.0f, erR = 0.0f;
        for (size_t t = 0; t < static_cast<size_t>(NUM_TAPS); ++t) {
            float g = TAP_GAINS_GRADUAL[t] + shape * (TAP_GAINS_RAPID[t] - TAP_GAINS_GRADUAL[t]);
            g *= amount;
            erL += delayL[t].read(tapL[t]) * g;
            erR += delayR[t].read(tapR[t]) * g;
        }

        // Add spin cross-mix
        erL += spinR * 0.15f;
        erR += spinL * 0.15f;

        // Blend into buffer (additive)
        if (numChannels >= 1) buffer.addSample(0, i, erL);
        if (numChannels >= 2) buffer.addSample(1, i, erR);
    }
}

