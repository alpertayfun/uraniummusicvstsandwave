#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace
{
    constexpr float minHz = 40.0f, maxHz = 5000.0f;   // search range for the dominant frequency
}

SandwaveAudioProcessor::SandwaveAudioProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true))
{
    license->checkAsync();   // verify the license every time the plugin is loaded
}

juce::AudioProcessorValueTreeState::ParameterLayout SandwaveAudioProcessor::createLayout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout layout;
    auto knob = [&] (const char* id, const char* name, float def)
    {
        layout.add (std::make_unique<AudioParameterFloat> (ParameterID { id, 1 }, name, NormalisableRange<float> (0.0f, 100.0f, 1.0f), def));
    };
    knob ("gain", "Gain", 70.0f);     // input gain, -30..+30 dB
    knob ("sens", "Sens", 60.0f);     // how quiet a sound still moves the sand
    knob ("grain", "Grain", 50.0f);   // how far the grains jump
    knob ("decay", "Decay", 40.0f);   // how long the plate rings after the sound stops
    knob ("range", "Range", 50.0f);   // frequency span mapped across the plate modes
    knob ("freq", "Freq", 45.0f);     // manual mode when MIC IN is off
    layout.add (std::make_unique<AudioParameterBool> (ParameterID { "follow", 1 }, "Mic In", true));
    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { "plate", 1 }, "Plate", StringArray { "Square", "Circle" }, 0));
    return layout;
}

void SandwaveAudioProcessor::prepareToPlay (double sr, int)
{
    sampleRate = sr;
    ring.fill (0.0f);
    ringPos = sinceHop = 0;
    hopSumSquares = 0.0;
}

bool SandwaveAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo())
        return false;
    return layouts.getMainInputChannelSet() == out;
}

void SandwaveAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    for (int c = getTotalNumInputChannels(); c < getTotalNumOutputChannels(); ++c)
        buffer.clear (c, 0, buffer.getNumSamples());

    // the sound always passes through; only the plate is locked until the license is verified
    if (! license->isLicensed())
        return;

    const int numChannels = juce::jmax (1, getTotalNumInputChannels());
    const float scale = 1.0f / (float) numChannels;
    for (int i = 0; i < buffer.getNumSamples(); ++i)
    {
        float s = 0.0f;
        for (int c = 0; c < numChannels && c < buffer.getNumChannels(); ++c)
            s += buffer.getSample (c, i);
        s *= scale;
        analyse (&s, 1);
    }
}

void SandwaveAudioProcessor::analyse (const float* samples, int numSamples)
{
    for (int i = 0; i < numSamples; ++i)
    {
        const float s = samples[i];
        ring[(size_t) ringPos] = s;
        ringPos = (ringPos + 1) & (fftSize - 1);
        hopSumSquares += (double) s * s;
        if (++sinceHop >= hopSize)
            analyseHop();
    }
}

void SandwaveAudioProcessor::analyseHop()
{
    const auto rms = std::sqrt (hopSumSquares / sinceHop);
    sinceHop = 0;
    hopSumSquares = 0.0;
    levelDb = juce::Decibels::gainToDecibels ((float) rms, -100.0f);

    // oldest sample first, windowed
    for (int i = 0; i < fftSize; ++i)
        fftData[(size_t) i] = ring[(size_t) ((ringPos + i) & (fftSize - 1))];
    std::fill (fftData.begin() + fftSize, fftData.end(), 0.0f);
    window.multiplyWithWindowingTable (fftData.data(), (size_t) fftSize);
    fft.performFrequencyOnlyForwardTransform (fftData.data(), true);

    const double binHz = sampleRate / fftSize;
    const int lo = juce::jmax (2, (int) (minHz / binHz));
    const int hi = juce::jmin (fftSize / 2 - 2, (int) (maxHz / binHz));
    int peak = lo;
    double sum = 0.0;
    for (int k = lo; k <= hi; ++k)
    {
        sum += fftData[(size_t) k];
        if (fftData[(size_t) k] > fftData[(size_t) peak])
            peak = k;
    }

    // a peak that barely rises above the average is noise, not a tone
    const double mean = sum / (hi - lo + 1);
    if (fftData[(size_t) peak] < mean * 6.0 || fftData[(size_t) peak] <= 1.0e-6f)
    {
        dominantHz = 0.0f;
        return;
    }

    // parabolic interpolation on log magnitudes for a finer estimate than one bin
    const double a = std::log (fftData[(size_t) peak - 1] + 1.0e-12), b = std::log (fftData[(size_t) peak] + 1.0e-12),
                 c = std::log (fftData[(size_t) peak + 1] + 1.0e-12);
    const double denom = a - 2.0 * b + c;
    const double delta = std::abs (denom) > 1.0e-12 ? juce::jlimit (-0.5, 0.5, 0.5 * (a - c) / denom) : 0.0;
    dominantHz = (float) ((peak + delta) * binHz);
}

juce::AudioProcessorEditor* SandwaveAudioProcessor::createEditor()
{
    return new SandwaveAudioProcessorEditor (*this);
}

void SandwaveAudioProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary (*xml, dest);
}

void SandwaveAudioProcessor::setStateInformation (const void* data, int size)
{
    if (auto xml = getXmlFromBinary (data, size))
        if (xml->hasTagName (apvts.state.getType()))
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new SandwaveAudioProcessor();
}
