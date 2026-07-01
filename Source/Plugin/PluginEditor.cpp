#include "PluginEditor.h"
#include "ParameterIDs.h"

AudioPluginAudioProcessorEditor::AudioPluginAudioProcessorEditor(AudioPluginAudioProcessor& p)
    : AudioProcessorEditor(&p), processorRef(p)
{
    webView = std::make_unique<WebViewBridge>();
    addAndMakeVisible(webView.get());

    // Route parameter changes from JS to APVTS
    webView->setParameterCallback([this](const juce::String& paramId, float value) {
        onParameterChangedFromJS(paramId, value);
    });

    setSize(1260, 700);
    setResizable(true, true); 
    setResizeLimits(450, 280, 1800, 1120);

    // Unpack HTML from binary data to a temp dir and load
    auto tempDir = juce::File::getSpecialLocation(juce::File::tempDirectory)
                       .getChildFile("Reverbo_" +
                                     juce::String(juce::Time::currentTimeMillis()));
    tempDir.createDirectory();

    auto htmlFile = tempDir.getChildFile("index.html");
    htmlFile.replaceWithData(BinaryData::index_html, BinaryData::index_htmlSize);

    webView->goToURL("file://" + htmlFile.getFullPathName());

    startTimerHz(30);
    sendAllParamsToJS();
}

AudioPluginAudioProcessorEditor::~AudioPluginAudioProcessorEditor() {
    stopTimer();
}

void AudioPluginAudioProcessorEditor::paint(juce::Graphics& g) {
    g.fillAll(juce::Colour(0xffede6da));
}

void AudioPluginAudioProcessorEditor::resized() {
    if (webView != nullptr)
        webView->setBounds(getLocalBounds());
}

void AudioPluginAudioProcessorEditor::timerCallback() {
    // Poll each parameter and send to JS if changed
    auto& apvts = processorRef.apvts;

    auto sendIfChanged = [&](const juce::String& paramId) {
        auto* param = apvts.getParameter(paramId);
        if (!param) return;
        float normalized = param->getValue(); // 0-1
        auto key = paramId.toStdString();
        auto it = lastParamValues.find(key);
        if (it == lastParamValues.end() || std::abs(it->second - normalized) > 0.001f) {
            lastParamValues[key] = normalized;
            sendParamToJS(paramId, normalized);
        }
    };

    using namespace PluginParamIDs;
    sendIfChanged(loCutEnabled.getParamID()); sendIfChanged(hiCutEnabled.getParamID());
    sendIfChanged(loCutFreq.getParamID());    sendIfChanged(hiCutFreq.getParamID());
    sendIfChanged(erEnabled.getParamID());    sendIfChanged(erAmount.getParamID());
    sendIfChanged(erRate.getParamID());       sendIfChanged(erShape.getParamID());
    sendIfChanged(reverbMode.getParamID());   sendIfChanged(crossoverFreq.getParamID());
    sendIfChanged(diffusion.getParamID());    sendIfChanged(scale.getParamID());
    sendIfChanged(decay.getParamID());        sendIfChanged(damping.getParamID());
    sendIfChanged(feedback.getParamID());
    sendIfChanged(chorusAmount.getParamID()); sendIfChanged(chorusRate.getParamID());
    sendIfChanged(reflectGain.getParamID());  sendIfChanged(diffuseGain.getParamID());
    sendIfChanged(dryWet.getParamID());
    sendIfChanged(predelay.getParamID());     sendIfChanged(smooth.getParamID());
    sendIfChanged(size.getParamID());         sendIfChanged(freeze.getParamID());
    sendIfChanged(flatEnabled.getParamID());  sendIfChanged(cutEnabled.getParamID());
    sendIfChanged(stereo.getParamID());       sendIfChanged(highFilterType.getParamID());
    sendIfChanged(density.getParamID());
}

void AudioPluginAudioProcessorEditor::sendAllParamsToJS() {
    auto& apvts = processorRef.apvts;
    auto sendAll = [&](const juce::String& paramId) {
        auto* param = apvts.getParameter(paramId);
        if (!param) return;
        float normalized = param->getValue();
        lastParamValues[paramId.toStdString()] = normalized;
        sendParamToJS(paramId, normalized);
    };

    using namespace PluginParamIDs;
    sendAll(loCutEnabled.getParamID()); sendAll(hiCutEnabled.getParamID());
    sendAll(loCutFreq.getParamID());    sendAll(hiCutFreq.getParamID());
    sendAll(erEnabled.getParamID());    sendAll(erAmount.getParamID());
    sendAll(erRate.getParamID());       sendAll(erShape.getParamID());
    sendAll(reverbMode.getParamID());   sendAll(crossoverFreq.getParamID());
    sendAll(diffusion.getParamID());    sendAll(scale.getParamID());
    sendAll(decay.getParamID());        sendAll(damping.getParamID());
    sendAll(feedback.getParamID());
    sendAll(chorusAmount.getParamID()); sendAll(chorusRate.getParamID());
    sendAll(reflectGain.getParamID());  sendAll(diffuseGain.getParamID());
    sendAll(dryWet.getParamID());
    sendAll(predelay.getParamID());     sendAll(smooth.getParamID());
    sendAll(size.getParamID());         sendAll(freeze.getParamID());
    sendAll(flatEnabled.getParamID());  sendAll(cutEnabled.getParamID());
    sendAll(stereo.getParamID());       sendAll(highFilterType.getParamID());
    sendAll(density.getParamID());
}

void AudioPluginAudioProcessorEditor::sendParamToJS(const juce::String& paramId,
                                                     float normalizedValue) {
    juce::String js = "if (window.setParameterValue) { window.setParameterValue('"
                      + paramId + "', " + juce::String(normalizedValue) + "); }";
    webView->evaluateJavascript(js);
}

void AudioPluginAudioProcessorEditor::onParameterChangedFromJS(const juce::String& paramId,
                                                                float value) {
    auto* param = processorRef.apvts.getParameter(paramId);
    if (param != nullptr) {
        param->setValueNotifyingHost(value); // value is 0-1 normalized
        lastParamValues[paramId.toStdString()] = value;
    }
}
