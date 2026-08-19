#pragma once
#include <cmath>

class LFO {
public:
    void prepare(double sampleRate) {
        sr = sampleRate;
        updateIncrement();
    }

    void setFrequency(float hz) {
        freq = hz;
        updateIncrement();
    }

    void reset(double initialPhase = 0.0) { phase = initialPhase; }

    double getPhase() const { return phase; }

    // Returns sine output in range [-1, 1]
    float getNext() {
        const float out = static_cast<float>(std::sin(6.283185307179586 * phase));
        phase += phaseInc;
        if (phase >= 1.0) phase -= 1.0;
        return out;
    }

private:
    double sr = 44100.0;
    double phase = 0.0;
    double phaseInc = 0.0;
    float freq = 1.0f;

    void updateIncrement() {
        phaseInc = (sr > 0.0) ? static_cast<double>(freq) / sr : 0.0;
    }
};

