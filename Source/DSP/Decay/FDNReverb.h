#pragma once
#include <juce_audio_basics/juce_audio_basics.h>
#include <array>
#include "DiffusionNetwork/DelayLine.h"
#include "DiffusionNetwork/FeedbackMatrix.h"
#include "DiffusionNetwork/CrossoverFilter.h"
#include "DiffusionNetwork/DiffusionNetwork.h"
#include "DiffusionNetwork/LFO.h"

// 8-line Feedback Delay Network reverb with Hadamard feedback matrix,
// frequency-dependent decay, diffusion, and per-line modulation.
// Real-time safe: no allocations in process().
class FDNReverb {
public:
    static constexpr int N = 8; // Must match FeedbackMatrix::N

    void prepare(double sampleRate);

    void setDecayMs(float ms);
    void setDamping(float d);           // 0-1
    void setDiffusion(float d);         // 0-1
    void setSize(float s);              // 0-1: scales delay lengths
    void setCrossoverFreq(float hz);
    void setFeedback(float fb);         // 0-1 (additional feedback scaling)
    void setFrozen(bool frozen);
    void setReverbMode(int mode);          // 0=High, 1=Low
    void setHighFilterType(bool shelving); // false=LP (default), true=high shelf
    void setInputScale(float s);           // 0=raw input, 1=fully diffused input (scale param)
    void setDensity(int d);                // 0-3 -> 0-4 active DiffusionNetwork stages
    void setFlatEnabled(bool f);           // when frozen: bypass CrossoverFilters
    void setCutEnabled(bool c);            // when frozen: hard-block new input immediately

    void process(juce::AudioBuffer<float>& buffer);
    void reset();

private:
    // Mutually prime base delay lengths (samples at 44100 Hz, 12-23 ms)
    static constexpr int BASE_DELAYS[N] = { 557, 617, 683, 743, 809, 877, 947, 1019 };

    void updateDecayGains();

    double sr = 44100.0;

    std::array<DelayLine, N>       delayLines;
    std::array<CrossoverFilter, N> dampFilters;
    DiffusionNetwork               diffusion;
    std::array<LFO, N>             modLFOs;

    // RT60-derived gain per line, always computed from decay time.
    // Blended with 0.9999f via freezeBlend in process() for click-free freeze.
    std::array<float, N> normalDecayGain {};
    std::array<int, N>   delayLens       {};
    std::array<float, N> modDepth        {};

    // Per-line delay length smoother: ramps over 50 ms when setSize() is called,
    // eliminating the click from an abrupt delay-length change.
    std::array<juce::SmoothedValue<float>, N> smoothDelayLens;

    // Crossfades from normal RT60 decay to frozen (0.9999f) over ~50 ms.
    juce::SmoothedValue<float> freezeBlend { 0.0f };
    // Crossfades damping filter in/out when flat mode is toggled (prevents IIR-state click).
    juce::SmoothedValue<float> flatBlend   { 0.0f };
    // Fades new input to zero when freeze is engaged, preventing level accumulation.
    juce::SmoothedValue<float> inputGate   { 1.0f };

    float decayMs       = 1500.0f;
    float dampAmount    = 0.5f;
    float diffAmount    = 0.6f;
    float sizeScale     = 0.5f;
    float crossFreq     = 3000.0f;
    float feedback      = 0.8f;
    float inputScale    = 0.5f;
    bool  frozen        = false;
    bool  flatEnabledFlag = false;
    bool  cutEnabledFlag  = false;
    int   mode          = 0;

    static constexpr float MOD_DEPTH_BASE = 2.0f;
    static constexpr float MOD_FREQ_BASE  = 0.31f;
};
