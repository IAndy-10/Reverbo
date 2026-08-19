#include "Chorus.h"
#include <cmath>
#include <algorithm>

void Chorus::prepare(double sampleRate) {
    sr = sampleRate;

    int maxSamples = static_cast<int>(sampleRate * 0.05) + 4; // 50ms max
    delayL.prepare(maxSamples);
    delayR.prepare(maxSamples);

    baseDelaySamples = static_cast<int>(sampleRate * BASE_DELAY_MS * 0.001);
    depthSamples     = static_cast<float>(sampleRate * MAX_DEPTH_MS * 0.001) * amount;

    lfoL.prepare(sampleRate);
    lfoR.prepare(sampleRate);
    lfoL.setFrequency(rate);
    lfoR.setFrequency(rate);

    // Offset R lfo by 90 degrees for width
    for (int i = 0; i < static_cast<int>(sampleRate / 4.0 / rate); ++i)
        lfoR.getNext();
}

void Chorus::setAmount(float a) {
    amount = a;
    depthSamples = static_cast<float>(sr * MAX_DEPTH_MS * 0.001) * amount;
}

void Chorus::setRate(float hz) {
    rate = hz;
    lfoL.setFrequency(hz);
    lfoR.setFrequency(hz);
    // Re-establish 90° offset relative to L's current phase whenever rate changes.
    lfoR.reset(std::fmod(lfoL.getPhase() + 0.25, 1.0));
}

void Chorus::reset() {
    delayL.clear();
    delayR.clear();
    lfoL.reset();
    lfoR.reset();
}

void Chorus::process(juce::AudioBuffer<float>& buffer) {
    if (amount < 0.001f) return;

    int numChannels = juce::jmin(buffer.getNumChannels(), 2);
    int numSamples  = buffer.getNumSamples();

    for (int i = 0; i < numSamples; ++i) {
        float modL = lfoL.getNext() * depthSamples;
        float modR = lfoR.getNext() * depthSamples;

        float readPosL = static_cast<float>(baseDelaySamples) + modL;
        float readPosR = static_cast<float>(baseDelaySamples) + modR;
        readPosL = std::max(1.0f, readPosL);
        readPosR = std::max(1.0f, readPosR);

        if (numChannels >= 1) {
            float inL = buffer.getSample(0, i);
            delayL.write(inL);
            float wet = delayL.readInterpolated(readPosL);
            // Fixed 50/50 dry+wet mix: amount controls modulation depth only (via depthSamples),
            // not the dry/wet ratio. Prevents the comb-filter amplitude sweep that sounds like tremolo.
            buffer.setSample(0, i, (inL + wet) * 0.5f);
        }

        if (numChannels >= 2) {
            float inR = buffer.getSample(1, i);
            delayR.write(inR);
            float wet = delayR.readInterpolated(readPosR);
            buffer.setSample(1, i, (inR + wet) * 0.5f);
        }
    }
}

