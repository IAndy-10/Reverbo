#include "FDNReverb.h"
#include <cmath>
#include <algorithm>


void FDNReverb::prepare(double sampleRate) {
    sr = sampleRate;
    double ratio = sampleRate / 44100.0;

    for (size_t i = 0; i < static_cast<size_t>(N); ++i) {
        // Allocate at maximum size (sizeScale=1.0 → factor=1.5) so setSize(1.0) never clamps.
        int maxLen = static_cast<int>(BASE_DELAYS[i] * ratio * 1.5) + 32;
        delayLines[i].prepare(maxLen);
        delayLens[i] = static_cast<int>(BASE_DELAYS[i] * ratio * (0.5 + sizeScale));

        dampFilters[i].prepare(sampleRate);
        dampFilters[i].setCutoffFreq(crossFreq);
        dampFilters[i].setDamping(dampAmount);

        // Stagger modulation LFO frequencies slightly for each line
        float lfoFreq = MOD_FREQ_BASE * (1.0f + static_cast<float>(i) * 0.08f);
        modLFOs[i].prepare(sampleRate);
        modLFOs[i].setFrequency(lfoFreq);

        modDepth[i] = MOD_DEPTH_BASE;
    }

    diffusion.prepare(sampleRate);
    diffusion.setDiffusion(diffAmount);

    updateDecayGains();
}

void FDNReverb::setDecayMs(float ms) {
    decayMs = ms;
    updateDecayGains();
}

void FDNReverb::setDamping(float d) {
    dampAmount = d;
    for (auto& f : dampFilters)
        f.setDamping(dampAmount);
}

void FDNReverb::setDiffusion(float d) {
    diffAmount = d;
    diffusion.setDiffusion(d);
}

void FDNReverb::setSize(float s) {
    sizeScale = s;
    if (sr <= 0.0) return;
    double ratio = sr / 44100.0;
    for (size_t i = 0; i < static_cast<size_t>(N); ++i) {
        // Note: changing delay lengths at runtime can cause clicks.
        // We clamp to current buffer capacity.
        int newLen = static_cast<int>(BASE_DELAYS[i] * ratio * (0.5 + s));
        delayLens[i] = std::min(newLen, delayLines[i].getMaxDelaySamples() - 4);
    }
    updateDecayGains();
}

void FDNReverb::setCrossoverFreq(float hz) {
    crossFreq = hz;
    for (auto& f : dampFilters)
        f.setCutoffFreq(hz);
}

void FDNReverb::setFeedback(float fb) {
    feedback = fb;
    updateDecayGains();
}

void FDNReverb::setFrozen(bool isFrozen) {
    frozen = isFrozen;
    updateDecayGains();
}

void FDNReverb::setReverbMode(int m) {
    mode = m;
    // Mode 0 (High) = brighter (1.5× crossover), Mode 1 (Low) = darker (0.6× crossover).
    // Resulting cutoffs at extremes: 200×0.6=120Hz (safe) to 8000×1.5=12000Hz (safe, coeff≈0.82 < 1).
    float freqScale = (mode == 0) ? 1.5f : 0.6f;
    for (auto& f : dampFilters)
        f.setCutoffFreq(crossFreq * freqScale);
}

void FDNReverb::setHighFilterType(bool shelving) {
    for (auto& f : dampFilters)
        f.setFilterType(shelving);
}

void FDNReverb::setInputScale(float s) {
    inputScale = s;
}

void FDNReverb::setDensity(int d) {
    // d: 0=Sparse→0 stages, 1=Low→1 stage, 2=Mid→2 stages, 3=High→4 stages
    static constexpr int stageMap[] = { 0, 1, 2, 4 };
    diffusion.setNumStages(stageMap[juce::jlimit(0, 3, d)]);
}

void FDNReverb::setFlatEnabled(bool f) { flatEnabledFlag = f; }
void FDNReverb::setCutEnabled(bool c)  { cutEnabledFlag  = c; }

void FDNReverb::updateDecayGains() {
    if (frozen) {
        // Freeze: maintain signal with maximum feedback
        for (size_t i = 0; i < static_cast<size_t>(N); ++i)
            decayGain[i] = 0.9999f;
        return;
    }

    float rt60 = decayMs * 0.001f;
    for (size_t i = 0; i < static_cast<size_t>(N); ++i) {
        float delaySec = static_cast<float>(delayLens[i]) / static_cast<float>(sr);
        if (rt60 > 0.0f && delaySec > 0.0f) {
            // g = 10^(-3 * T60 / RT60) → gives -60 dB after RT60 seconds
            float g = std::pow(10.0f, -3.0f * delaySec / rt60);
            decayGain[i] = g * feedback;
        } else {
            decayGain[i] = 0.0f;
        }
        // Stability clamp
        decayGain[i] = std::min(decayGain[i], 0.9999f);
    }
}

void FDNReverb::process(juce::AudioBuffer<float>& buffer) {
    int numChannels = juce::jmin(buffer.getNumChannels(), 2);
    int numSamples  = buffer.getNumSamples();

    if (numChannels < 1) return;

    for (int sampleIdx = 0; sampleIdx < numSamples; ++sampleIdx) {
        float inL = buffer.getSample(0, sampleIdx);
        float inR = (numChannels >= 2) ? buffer.getSample(1, sampleIdx) : inL;

        // --- Diffuse input, then blend with raw input (scale param) ---
        float diffL = inL, diffR = inR;
        diffusion.processStereo(diffL, diffR);
        diffL = inL * (1.0f - inputScale) + diffL * inputScale;
        diffR = inR * (1.0f - inputScale) + diffR * inputScale;

        // --- Read delay line outputs and apply damping ---
        std::array<float, N> y;
        for (size_t i = 0; i < static_cast<size_t>(N); ++i) {
            // Per-line modulation (subtle vibrato to prevent metallic ringing)
            float mod = modLFOs[i].getNext() * modDepth[i];
            float readPos = static_cast<float>(delayLens[i]) + mod;
            readPos = std::max(1.0f, readPos);

            y[i] = delayLines[i].readInterpolated(readPos);

            // Frequency-dependent decay — bypassed when frozen with flat mode active
            if (!(frozen && flatEnabledFlag))
                y[i] = dampFilters[i].processSample(y[i]);

            // Apply decay gain
            y[i] *= decayGain[i];
        }

        // --- Apply Hadamard feedback matrix ---
        FeedbackMatrix::apply(y);

        // --- Write back into delay lines (feedback + new input) ---
        // cut mode: when frozen with cut active, block new input to preserve frozen tail
        for (size_t i = 0; i < static_cast<size_t>(N); ++i) {
            float newInput = (i % 2 == 0) ? diffL : diffR;
            float writeVal = (frozen && cutEnabledFlag) ? y[i] : (newInput + y[i]);
            delayLines[i].write(writeVal);
        }

        // --- Mix outputs (even → L, odd → R) ---
        float outL = 0.0f, outR = 0.0f;
        static constexpr float OUT_SCALE = 0.35355339059f; // 1/sqrt(8)
        for (size_t i = 0; i < static_cast<size_t>(N); ++i) {
            if (i % 2 == 0) outL += y[i];
            else             outR += y[i];
        }
        outL *= OUT_SCALE;
        outR *= OUT_SCALE;

        buffer.setSample(0, sampleIdx, outL);
        if (numChannels >= 2) buffer.setSample(1, sampleIdx, outR);
    }
}

void FDNReverb::reset() {
    for (size_t i = 0; i < static_cast<size_t>(N); ++i) {
        delayLines[i].clear();
        dampFilters[i].reset();
        modLFOs[i].reset();
    }
    diffusion.reset();
}

