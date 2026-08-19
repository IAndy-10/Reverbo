#include "PluginProcessor.h"
#include "../Parameters/ParameterLayout.h"
#include "../Parameters/ParameterIDs.h"
#include "PluginEditor.h"

using namespace PluginParamIDs;

AudioPluginAudioProcessor::AudioPluginAudioProcessor()
    : AudioProcessor(
          BusesProperties()
#if !JucePlugin_IsMidiEffect
#if !JucePlugin_IsSynth
              .withInput("Input",  juce::AudioChannelSet::stereo(), true)
#endif
              .withOutput("Output", juce::AudioChannelSet::stereo(), true)
#endif
      ),
      apvts(*this, nullptr, "Parameters", createParameterLayout())
{}

AudioPluginAudioProcessor::~AudioPluginAudioProcessor() {}

const juce::String AudioPluginAudioProcessor::getName() const { return JucePlugin_Name; }
bool AudioPluginAudioProcessor::acceptsMidi()  const { return true;  }
bool AudioPluginAudioProcessor::producesMidi() const { return false; }
bool AudioPluginAudioProcessor::isMidiEffect() const { return false; }
double AudioPluginAudioProcessor::getTailLengthSeconds() const { return 60.0; }
int AudioPluginAudioProcessor::getNumPrograms() { return 1; }
int AudioPluginAudioProcessor::getCurrentProgram() { return 0; }
void AudioPluginAudioProcessor::setCurrentProgram(int) {}
const juce::String AudioPluginAudioProcessor::getProgramName(int) { return {}; }
void AudioPluginAudioProcessor::changeProgramName(int, const juce::String&) {}

bool AudioPluginAudioProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const {
#if JucePlugin_IsMidiEffect
    juce::ignoreUnused(layouts);
    return true;
#else
    if (layouts.getMainOutputChannelSet() != juce::AudioChannelSet::mono() &&
        layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo())
        return false;
#if !JucePlugin_IsSynth
    if (layouts.getMainOutputChannelSet() != layouts.getMainInputChannelSet())
        return false;
#endif
    return true;
#endif
}

void AudioPluginAudioProcessor::prepareToPlay(double sampleRate, int samplesPerBlock) {
    // Prepare DSP modules
    inputFilter.prepare(sampleRate);
    predelayModule.prepare(sampleRate);
    earlyReflections.prepare(sampleRate);
    fdnReverb.prepare(sampleRate);
    chorusModule.prepare(sampleRate);
    stereoWidener.setWidth(1.0f);
    dryWetMixer.prepare(getTotalNumOutputChannels(), samplesPerBlock);  // B3 fix
    erOutputBuffer.setSize(getTotalNumOutputChannels(), samplesPerBlock);
    lastSmoothMode = -1; // force smoother reset on first block

    // Prepare smoothed values (10ms smoothing time)
    auto initSmooth = [&](juce::SmoothedValue<float>& sv, float initial) {
        sv.reset(sampleRate, 0.01);
        sv.setCurrentAndTargetValue(initial);
    };

    // ===== PANEL 1: INPUT =====
    // FilterGraph: lo/hi cut frequencies
    initSmooth(smoothLoCutFreq, apvts.getRawParameterValue(loCutFreq.getParamID())->load());
    initSmooth(smoothHiCutFreq, apvts.getRawParameterValue(hiCutFreq.getParamID())->load());
    // Predelay subsection
    initSmooth(smoothPredelay, apvts.getRawParameterValue(PluginParamIDs::predelay.getParamID())->load());

    // ===== PANEL 2: EARLY REFLECTIONS =====
    initSmooth(smoothErAmount, apvts.getRawParameterValue(erAmount.getParamID())->load());
    initSmooth(smoothErRate,   apvts.getRawParameterValue(erRate.getParamID())->load());
    // Shape subsection: Reflect gain
    initSmooth(smoothReflectGain, apvts.getRawParameterValue(reflectGain.getParamID())->load());

    // ===== PANEL 3: DIFFUSION NETWORK =====
    initSmooth(smoothCrossoverFreq, apvts.getRawParameterValue(crossoverFreq.getParamID())->load());
    initSmooth(smoothDiffusion,     apvts.getRawParameterValue(diffusion.getParamID())->load());
    initSmooth(smoothDamping,       apvts.getRawParameterValue(damping.getParamID())->load());
    initSmooth(smoothFeedback,      apvts.getRawParameterValue(feedback.getParamID())->load());
    initSmooth(smoothDiffuseGain,   apvts.getRawParameterValue(diffuseGain.getParamID())->load());
    // Chorus subsection
    initSmooth(smoothChorusAmount, apvts.getRawParameterValue(chorusAmount.getParamID())->load());
    initSmooth(smoothChorusRate,   apvts.getRawParameterValue(chorusRate.getParamID())->load());
    // Size subsection
    initSmooth(smoothSize, apvts.getRawParameterValue(size.getParamID())->load());

    // ===== PANEL 4: DECAY =====
    initSmooth(smoothDecay, apvts.getRawParameterValue(decay.getParamID())->load());

    // ===== PANEL 5: OUTPUT =====
    initSmooth(smoothStereo, apvts.getRawParameterValue(stereo.getParamID())->load());
    initSmooth(smoothDryWet, apvts.getRawParameterValue(dryWet.getParamID())->load());


    updateDSPParameters();
}

void AudioPluginAudioProcessor::releaseResources() {
    inputFilter.reset();
    predelayModule.reset();
    earlyReflections.reset();
    fdnReverb.reset();
    chorusModule.reset();
}

void AudioPluginAudioProcessor::updateDSPParameters() {
    // Parameters → DSP mapping (grouped by WebUI panels in `WebUI/src/App.svelte`)

    // ===== PANEL 1: INPUT =====
    inputFilter.setLoCutEnabled(apvts.getRawParameterValue(loCutEnabled.getParamID())->load() > 0.5f);
    inputFilter.setHiCutEnabled(apvts.getRawParameterValue(hiCutEnabled.getParamID())->load() > 0.5f);
    inputFilter.setLoCutFreq(apvts.getRawParameterValue(loCutFreq.getParamID())->load());
    inputFilter.setHiCutFreq(apvts.getRawParameterValue(hiCutFreq.getParamID())->load());
    inputFilter.setHiCutQ(apvts.getRawParameterValue(hiCutQ.getParamID())->load());
    predelayModule.setDelayMs(apvts.getRawParameterValue(PluginParamIDs::predelay.getParamID())->load());

    // ===== PANEL 2: EARLY REFLECTIONS =====
    // erAmount APVTS stores 2–55 (display units). Normalize to 0–1 for DSP.
    earlyReflections.setEnabled(apvts.getRawParameterValue(erEnabled.getParamID())->load() > 0.5f);
    earlyReflections.setAmount((apvts.getRawParameterValue(erAmount.getParamID())->load() - 2.0f) / 53.0f);
    earlyReflections.setRate(apvts.getRawParameterValue(erRate.getParamID())->load());  // already in Hz
    earlyReflections.setShape(apvts.getRawParameterValue(erShape.getParamID())->load());

    // ===== PANEL 3: DIFFUSION NETWORK =====
    fdnReverb.setDecayMs(apvts.getRawParameterValue(decay.getParamID())->load());
    fdnReverb.setDiffusion(apvts.getRawParameterValue(diffusion.getParamID())->load());
    fdnReverb.setSize(apvts.getRawParameterValue(PluginParamIDs::size.getParamID())->load());
    fdnReverb.setDamping(apvts.getRawParameterValue(damping.getParamID())->load());
    fdnReverb.setFeedback(apvts.getRawParameterValue(feedback.getParamID())->load());
    fdnReverb.setCrossoverFreq(apvts.getRawParameterValue(crossoverFreq.getParamID())->load());
    fdnReverb.setReverbMode(static_cast<int>(apvts.getRawParameterValue(reverbMode.getParamID())->load()));
    fdnReverb.setFrozen(apvts.getRawParameterValue(PluginParamIDs::freeze.getParamID())->load() > 0.5f);
    fdnReverb.setHighFilterType(apvts.getRawParameterValue(highFilterType.getParamID())->load() > 0.5f);
    fdnReverb.setInputScale(apvts.getRawParameterValue(PluginParamIDs::scale.getParamID())->load());
    fdnReverb.setFlatEnabled(apvts.getRawParameterValue(PluginParamIDs::flatEnabled.getParamID())->load() > 0.5f);
    fdnReverb.setCutEnabled(apvts.getRawParameterValue(PluginParamIDs::cutEnabled.getParamID())->load() > 0.5f);
    fdnReverb.setDensity(static_cast<int>(apvts.getRawParameterValue(density.getParamID())->load()));

    // Chorus subsection (Diffusion Network)
    chorusModule.setAmount(apvts.getRawParameterValue(chorusAmount.getParamID())->load());
    chorusModule.setRate(apvts.getRawParameterValue(chorusRate.getParamID())->load());

    // Early Reflections (initial state; real-time updates happen in processBlock)
    earlyReflections.setAmount((apvts.getRawParameterValue(erAmount.getParamID())->load() - 2.0f) / 53.0f);
    earlyReflections.setRate(apvts.getRawParameterValue(erRate.getParamID())->load());
    earlyReflections.setShape(apvts.getRawParameterValue(erShape.getParamID())->load());

    // ===== PANEL 5: OUTPUT =====
    stereoWidener.setWidth(apvts.getRawParameterValue(stereo.getParamID())->load() / 120.0f);
    dryWetMixer.setMix(apvts.getRawParameterValue(dryWet.getParamID())->load() / 100.0f);
}

void AudioPluginAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer,
                                              juce::MidiBuffer& midiMessages) {
    juce::ScopedNoDenormals noDenormals;
    juce::ignoreUnused(midiMessages);

    auto totalNumOutputChannels = getTotalNumOutputChannels();
    for (auto i = getTotalNumInputChannels(); i < totalNumOutputChannels; ++i)
        buffer.clear(i, 0, buffer.getNumSamples());

    const int numSamples  = buffer.getNumSamples();
    const int numChannels = buffer.getNumChannels();

    // ── Smooth mode: update ramp times when selector changes (B4 / smooth wiring) ──
    int smoothMode = static_cast<int>(apvts.getRawParameterValue(PluginParamIDs::smooth.getParamID())->load());
    if (smoothMode != lastSmoothMode) {
        static constexpr float kSmoothTimes[] = { 0.001f, 0.02f, 0.08f, 0.20f }; // Off/Low/Med/High
        float t = kSmoothTimes[juce::jlimit(0, 3, smoothMode)];
        smoothDecay.reset(getSampleRate(), t);
        smoothReflectGain.reset(getSampleRate(), t);
        smoothDiffuseGain.reset(getSampleRate(), t);
        lastSmoothMode = smoothMode;
    }
    smoothDecay.setTargetValue(apvts.getRawParameterValue(decay.getParamID())->load());
    smoothReflectGain.setTargetValue(apvts.getRawParameterValue(reflectGain.getParamID())->load());
    smoothDiffuseGain.setTargetValue(apvts.getRawParameterValue(diffuseGain.getParamID())->load());

    // Save dry signal before any processing
    dryWetMixer.saveDry(buffer);

    // ===== PANEL 1: INPUT =====
    inputFilter.setLoCutEnabled(apvts.getRawParameterValue(loCutEnabled.getParamID())->load() > 0.5f);
    inputFilter.setHiCutEnabled(apvts.getRawParameterValue(hiCutEnabled.getParamID())->load() > 0.5f);
    inputFilter.setLoCutFreq(apvts.getRawParameterValue(loCutFreq.getParamID())->load());
    inputFilter.setHiCutFreq(apvts.getRawParameterValue(hiCutFreq.getParamID())->load());
    inputFilter.setHiCutQ(apvts.getRawParameterValue(hiCutQ.getParamID())->load());
    inputFilter.process(buffer);

    predelayModule.setDelayMs(apvts.getRawParameterValue(PluginParamIDs::predelay.getParamID())->load());
    predelayModule.process(buffer);
    // buffer = filtered + predelayed dry signal (FDN input)

    // ===== PANEL 2: EARLY REFLECTIONS (parallel path — B1 fix) =====
    // Runs on the predelayed dry signal; ER-only output written to erOutputBuffer.
    // This keeps reflectGain and diffuseGain independently applicable.
    earlyReflections.setEnabled(apvts.getRawParameterValue(erEnabled.getParamID())->load() > 0.5f);
    earlyReflections.setAmount((apvts.getRawParameterValue(erAmount.getParamID())->load() - 2.0f) / 53.0f);
    earlyReflections.setRate(apvts.getRawParameterValue(erRate.getParamID())->load());
    earlyReflections.setShape(apvts.getRawParameterValue(erShape.getParamID())->load());
    erOutputBuffer.setSize(numChannels, numSamples, false, false, true);
    earlyReflections.processOut(buffer, erOutputBuffer);
    // erOutputBuffer = ER contribution only; buffer still = predelayed dry

    // ===== PANEL 3: DIFFUSION NETWORK + DECAY =====
    bool isFrozen = apvts.getRawParameterValue(PluginParamIDs::freeze.getParamID())->load() > 0.5f;
    fdnReverb.setFrozen(isFrozen);
    fdnReverb.setDecayMs(smoothDecay.skip(numSamples));   // B4: smoothed decay
    fdnReverb.setDiffusion(apvts.getRawParameterValue(diffusion.getParamID())->load());
    fdnReverb.setSize(apvts.getRawParameterValue(PluginParamIDs::size.getParamID())->load());
    fdnReverb.setDamping(apvts.getRawParameterValue(damping.getParamID())->load());
    fdnReverb.setFeedback(apvts.getRawParameterValue(feedback.getParamID())->load());
    fdnReverb.setCrossoverFreq(apvts.getRawParameterValue(crossoverFreq.getParamID())->load());
    fdnReverb.setReverbMode(static_cast<int>(apvts.getRawParameterValue(reverbMode.getParamID())->load()));
    fdnReverb.setHighFilterType(apvts.getRawParameterValue(highFilterType.getParamID())->load() > 0.5f);
    fdnReverb.setInputScale(apvts.getRawParameterValue(PluginParamIDs::scale.getParamID())->load());
    fdnReverb.setFlatEnabled(apvts.getRawParameterValue(PluginParamIDs::flatEnabled.getParamID())->load() > 0.5f);
    fdnReverb.setCutEnabled(apvts.getRawParameterValue(PluginParamIDs::cutEnabled.getParamID())->load() > 0.5f);
    fdnReverb.setDensity(static_cast<int>(apvts.getRawParameterValue(density.getParamID())->load()));
    fdnReverb.process(buffer);
    // buffer = FDN reverb output

    // Apply diffuseGain to FDN tail; add reflectGain-scaled ER separately (B1 fix)
    float dGain = juce::Decibels::decibelsToGain(smoothDiffuseGain.skip(numSamples));
    float rGain = juce::Decibels::decibelsToGain(smoothReflectGain.skip(numSamples));
    buffer.applyGain(dGain);
    for (int ch = 0; ch < numChannels; ++ch)
        buffer.addFrom(ch, 0, erOutputBuffer, ch, 0, numSamples, rGain);

    // Chorus subsection
    bool chorusOn = apvts.getRawParameterValue(chorusEnabled.getParamID())->load() > 0.5f;
    chorusModule.setAmount(apvts.getRawParameterValue(chorusAmount.getParamID())->load());
    chorusModule.setRate(apvts.getRawParameterValue(chorusRate.getParamID())->load());
    if (chorusOn) chorusModule.process(buffer);

    // ===== PANEL 5: OUTPUT =====
    stereoWidener.setWidth(apvts.getRawParameterValue(stereo.getParamID())->load() / 120.0f);
    stereoWidener.process(buffer);

    dryWetMixer.setMix(apvts.getRawParameterValue(dryWet.getParamID())->load() / 100.0f);
    dryWetMixer.mixToOutput(buffer);
}

bool AudioPluginAudioProcessor::hasEditor() const { return true; }

juce::AudioProcessorEditor* AudioPluginAudioProcessor::createEditor() {
    return new AudioPluginAudioProcessorEditor(*this);
}

void AudioPluginAudioProcessor::getStateInformation(juce::MemoryBlock& destData) {
    auto state = apvts.copyState();
    std::unique_ptr<juce::XmlElement> xml(state.createXml());
    copyXmlToBinary(*xml, destData);
}

void AudioPluginAudioProcessor::setStateInformation(const void* data, int sizeInBytes) {
    std::unique_ptr<juce::XmlElement> xmlState(getXmlFromBinary(data, sizeInBytes));
    if (xmlState && xmlState->hasTagName(apvts.state.getType()))
        apvts.replaceState(juce::ValueTree::fromXml(*xmlState));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() {
    return new AudioPluginAudioProcessor();
}
