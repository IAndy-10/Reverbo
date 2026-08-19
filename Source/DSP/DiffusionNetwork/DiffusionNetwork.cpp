#include "DiffusionNetwork.h"

void DiffusionNetwork::prepare(double sampleRate) {
    double ratio = sampleRate / 44100.0;
    for (size_t s = 0; s < static_cast<size_t>(NUM_STAGES); ++s) {
        int dL = static_cast<int>(DELAYS_L[s] * ratio) + 1;
        int dR = static_cast<int>(DELAYS_R[s] * ratio) + 1;
        stages[s].prepare(sampleRate, dL, dR);
        stages[s].setCoefficient(diffusion);
    }
}

void DiffusionNetwork::setDiffusion(float d) {
    diffusion = d;
    for (auto& stage : stages)
        stage.setCoefficient(diffusion);
}

void DiffusionNetwork::setNumStages(int n) {
    numActive = juce::jlimit(0, NUM_STAGES, n);
}

void DiffusionNetwork::processStereo(float& sampleL, float& sampleR) {
    for (size_t s = 0; s < static_cast<size_t>(numActive); ++s)
        stages[s].processStereo(sampleL, sampleR);
}

void DiffusionNetwork::reset() {
    for (auto& stage : stages)
        stage.reset();
}

