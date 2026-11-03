#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "PluginProcessor.h"
#include "Plate.h"
#include "LicenseOverlay.h"

class SandwaveLook : public juce::LookAndFeel_V4
{
public:
    void drawRotarySlider (juce::Graphics&, int x, int y, int w, int h, float pos,
                           float startAngle, float endAngle, juce::Slider&) override;
    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour&, bool over, bool down) override;
    void drawButtonText (juce::Graphics&, juce::TextButton&, bool over, bool down) override;
};

class SandwaveAudioProcessorEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit SandwaveAudioProcessorEditor (SandwaveAudioProcessor&);
    ~SandwaveAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;

    void tick();                  // one animation frame (60 per second)
    void showInfo (bool show);

    static constexpr int numKnobs = 6;

private:
    void timerCallback() override { tick(); }

    struct InfoPanel : public juce::Component
    {
        explicit InfoPanel (LicenseManager& lm) : license (lm) {}
        void paint (juce::Graphics&) override;
        void mouseDown (const juce::MouseEvent&) override { setVisible (false); }
        LicenseManager& license;
    };

    SandwaveAudioProcessor& audioProcessor;
    SandwaveLook look;

    Plate plate;
    juce::Slider knobs[numKnobs];
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> knobAttachments[numKnobs];
    juce::TextButton micButton { "MIC IN" }, squareButton { "SQUARE" }, circleButton { "CIRCLE" }, shakeButton { "SHAKE" };
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> micAttachment;

    InfoPanel infoPanel { audioProcessor.getLicense() };
    LicenseOverlay licenseOverlay { audioProcessor.getLicense() };

    juce::Rectangle<int> header, plateFrame, lcdSection, lcdBox, meterBox, knobSection, buttonSection, ledArea;

    // animation state
    float envelope = 0.0f, meterLevel = 0.0f;
    float logHz = std::log2 (440.0f);
    int shownMode = 6, candidateMode = 6, candidateFrames = 0;
    juce::String shownHz;
    int shownSegments = -1;
    bool ledOn = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SandwaveAudioProcessorEditor)
};
