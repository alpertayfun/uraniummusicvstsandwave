#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>
#include <atomic>
#include "License.h"

// Audio passes through untouched. The input is analysed for its loudness and dominant frequency,
// which the editor turns into a vibrating sand plate.
class SandwaveAudioProcessor : public juce::AudioProcessor
{
public:
    SandwaveAudioProcessor();
    ~SandwaveAudioProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

    LicenseManager& getLicense() { return *license; }

    // latest analysis, written by the audio thread
    float getLevelDb() const noexcept { return levelDb.load(); }       // RMS of the last hop, dBFS
    float getDominantHz() const noexcept { return dominantHz.load(); } // 0 when nothing stands out
    // test hook: same as processBlock on a mono signal
    void analyse (const float* samples, int numSamples);

    juce::AudioProcessorValueTreeState apvts { *this, nullptr, "PARAMS", createLayout() };

private:
    void analyseHop();

    static constexpr int fftOrder = 12, fftSize = 1 << fftOrder, hopSize = 1024;

    juce::dsp::FFT fft { fftOrder };
    juce::dsp::WindowingFunction<float> window { (size_t) fftSize, juce::dsp::WindowingFunction<float>::hann };
    std::array<float, fftSize> ring {};
    std::array<float, fftSize * 2> fftData {};
    int ringPos = 0, sinceHop = 0;
    double hopSumSquares = 0.0;
    double sampleRate = 48000.0;

    std::atomic<float> levelDb { -100.0f }, dominantHz { 0.0f };

    juce::SharedResourcePointer<LicenseManager> license;   // shared by all instances in the host

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SandwaveAudioProcessor)
};
