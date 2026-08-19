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

        // Initialise delay-length smoother at current length; ramp 50 ms on size changes.
        smoothDelayLens[i].reset(sampleRate, 0.05);
        smoothDelayLens[i].setCurrentAndTargetValue(static_cast<float>(delayLens[i]));
    }

    diffusion.prepare(sampleRate);
    diffusion.setDiffusion(diffAmount);

    // Freeze crossfade: 0 = normal RT60 decay, 1 = frozen (0.9999f), 50 ms transition.
    freezeBlend.reset(sampleRate, 0.05);
    freezeBlend.setCurrentAndTargetValue(frozen ? 1.0f : 0.0f);

    // Flat crossfade: 0 = filtered, 1 = bypass damping filter, 30 ms transition.
    flatBlend.reset(sampleRate, 0.03);
    flatBlend.setCurrentAndTargetValue(flatEnabledFlag ? 1.0f : 0.0f);

    // Input gate: 1 = open (normal), 0 = closed (frozen), 100 ms transition.
    // Prevents new audio from accumulating in the FDN when freeze is engaged.
    inputGate.reset(sampleRate, 0.1);
    inputGate.setCurrentAndTargetValue(frozen ? 0.0f : 1.0f);

    normalDecayGain.fill(0.0f);
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
        int newLen = static_cast<int>(BASE_DELAYS[i] * ratio * (0.5 + s));
        newLen = std::min(newLen, delayLines[i].getMaxDelaySamples() - 4);
        delayLens[i] = newLen;
        // Smooth to the new length — avoids the click from an abrupt jump.
        smoothDelayLens[i].setTargetValue(static_cast<float>(newLen));
    }
    updateDecayGains();
}

void FDNReverb::setCrossoverFreq(float hz) {
    crossFreq = hz;
    for (auto& f : dampFilters)
        f.setCutoffFreq(hz);
}

void FDNReverb::setFeedback(float fb) {
    if (std::abs(fb - feedback) < 1e-6f) return;
    feedback = fb;
    updateDecayGains();
}

void FDNReverb::setFrozen(bool isFrozen) {
    frozen = isFrozen;
    // Crossfade to/from frozen state and simultaneously gate the input.
    freezeBlend.setTargetValue(isFrozen ? 1.0f : 0.0f);
    inputGate.setTargetValue(isFrozen ? 0.0f : 1.0f);
    updateDecayGains();
}

void FDNReverb::setReverbMode(int m) {
    mode = m;
    // Mode 0 (High) = brighter (1.5x crossover), Mode 1 (Low) = darker (0.6x crossover).
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

void FDNReverb::setFlatEnabled(bool f) {
    flatEnabledFlag = f;
    flatBlend.setTargetValue(f ? 1.0f : 0.0f);
}

void FDNReverb::setCutEnabled(bool c) { cutEnabledFlag = c; }

void FDNReverb::updateDecayGains() {
    // Always compute RT60-based gain — frozen blend is applied per-sample in process().
    float rt60 = decayMs * 0.001f;
    for (size_t i = 0; i < static_cast<size_t>(N); ++i) {
        float delaySec = static_cast<float>(delayLens[i]) / static_cast<float>(sr);
        if (rt60 > 0.0f && delaySec > 0.0f) {
            // g = 10^(-3 * delaySec / RT60) → -60 dB after RT60 seconds
            float g = std::pow(10.0f, -3.0f * delaySec / rt60);
            normalDecayGain[i] = g * feedback;
        } else {
            normalDecayGain[i] = 0.0f;
        }
        // Leave headroom from unity to avoid instability at high feedback settings.
        normalDecayGain[i] = std::min(normalDecayGain[i], 0.9998f);
    }
}

void FDNReverb::process(juce::AudioBuffer<float>& buffer) {
    int numChannels = juce::jmin(buffer.getNumChannels(), 2);
    int numSamples  = buffer.getNumSamples();

    if (numChannels < 1) return;

    for (int sampleIdx = 0; sampleIdx < numSamples; ++sampleIdx) {
        // Freeze blend: 0 = normal RT60 decay, 1 = fully frozen (0.9999f per line).
        // Ramps smoothly (50 ms) so toggling freeze does not produce a click.
        const float fb    = freezeBlend.getNextValue();
        // Flat blend: 0 = use damping filter, 1 = bypass.
        // Filter is ALWAYS computed to keep its IIR state valid — we only crossfade
        // the output, so re-engaging the filter never causes a state-mismatch click.
        const float flatB = flatBlend.getNextValue();
        // Input gate: 1 = full input, 0 = no input (frozen).
        // Ramps to 0 over 100 ms when freeze engages, preventing level accumulation.
        const float gate  = inputGate.getNextValue();

        float inL = buffer.getSample(0, sampleIdx);
        float inR = (numChannels >= 2) ? buffer.getSample(1, sampleIdx) : inL;

        // --- Diffuse input, blend with raw input (scale param), then gate ---
        float diffL = inL, diffR = inR;
        diffusion.processStereo(diffL, diffR);
        diffL = (inL * (1.0f - inputScale) + diffL * inputScale) * gate;
        diffR = (inR * (1.0f - inputScale) + diffR * inputScale) * gate;

        // --- Read delay line outputs and apply damping ---
        std::array<float, N> y;
        for (size_t i = 0; i < static_cast<size_t>(N); ++i) {
            // Per-line modulation (subtle vibrato to reduce metallic ringing)
            float mod = modLFOs[i].getNext() * modDepth[i];

            // Smoothed delay length: ramps toward target when setSize() is called,
            // preventing the click that a sudden delay-length change would produce.
            float readPos = smoothDelayLens[i].getNextValue() + mod;
            readPos = std::max(1.0f, readPos);

            const float y_raw  = delayLines[i].readInterpolated(readPos);
            const float y_filt = dampFilters[i].processSample(y_raw); // always run to keep IIR fresh

            // Flat: crossfade between filtered and raw output.
            // effectiveFlat is 0 when not frozen, so filter is always used outside freeze.
            const float effectiveFlat = flatB * fb;
            y[i] = y_filt * (1.0f - effectiveFlat) + y_raw * effectiveFlat;

            // Blend between normal RT60 decay and frozen sustain (0.9999f).
            const float actualGain = normalDecayGain[i] * (1.0f - fb) + 0.9999f * fb;
            y[i] *= actualGain;
        }

        // --- Apply Hadamard feedback matrix ---
        FeedbackMatrix::apply(y);

        // --- Write back into delay lines (feedback + gated input) ---
        // cutEnabled provides an immediate hard-cut on top of the smooth inputGate fade.
        for (size_t i = 0; i < static_cast<size_t>(N); ++i) {
            float newInput = (i % 2 == 0) ? diffL : diffR; // already gated
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
        smoothDelayLens[i].setCurrentAndTargetValue(static_cast<float>(delayLens[i]));
    }
    diffusion.reset();
    freezeBlend.setCurrentAndTargetValue(frozen ? 1.0f : 0.0f);
    flatBlend.setCurrentAndTargetValue(flatEnabledFlag ? 1.0f : 0.0f);
    inputGate.setCurrentAndTargetValue(frozen ? 0.0f : 1.0f);
}
