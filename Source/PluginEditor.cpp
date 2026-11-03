#include "PluginEditor.h"
#include "Theme.h"

using namespace theme;

namespace
{
    const char* knobIds[] = { "gain", "sens", "grain", "decay", "range", "freq" };
    const char* knobNames[] = { "GAIN", "SENS", "GRAIN", "DECAY", "RANGE", "FREQ" };

    constexpr float frameSeconds = 1.0f / 60.0f;
    constexpr float lowestHz = 60.0f;      // bottom of the mode range
    constexpr int meterSegments = 20;
    constexpr int modeHoldFrames = 4;      // a new mode must hold this long before the plate switches

    float param (SandwaveAudioProcessor& p, const char* id) { return p.apvts.getRawParameterValue (id)->load(); }

    // octaves covered by the 14 plate modes: 3 (RANGE 0) to 8 (RANGE 100)
    float octaveSpan (float range) { return 3.0f + 5.0f * range / 100.0f; }
}

//==============================================================================
void SandwaveLook::drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos,
                                     float startAngle, float endAngle, juce::Slider&)
{
    const auto bounds = juce::Rectangle<int> (x, y, w, h).toFloat().reduced (2.0f);
    const float r = juce::jmin (bounds.getWidth(), bounds.getHeight()) * 0.5f;
    const auto c = bounds.getCentre();

    g.setColour (knobBody);
    g.fillEllipse (c.x - r, c.y - r, r * 2.0f, r * 2.0f);
    g.setColour (knobRing);
    g.drawEllipse (c.x - r + 2.0f, c.y - r + 2.0f, r * 2.0f - 4.0f, r * 2.0f - 4.0f, 4.0f);

    const float angle = startAngle + pos * (endAngle - startAngle);
    const juce::Point<float> dir (std::sin (angle), -std::cos (angle));
    g.setColour (knobMark);
    g.drawLine ({ c + dir * (r * 0.28f), c + dir * (r * 0.78f) }, 4.0f);
}

void SandwaveLook::drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour&, bool over, bool down)
{
    const auto r = b.getLocalBounds().toFloat().reduced (0.5f);
    const bool on = b.getToggleState();
    auto fill = on ? accent : buttonCol;
    if (down) fill = fill.darker (0.2f);
    else if (over) fill = fill.brighter (0.08f);
    g.setColour (fill);
    g.fillRoundedRectangle (r, 4.0f);
    g.setColour (on ? accent : knobRing);
    g.drawRoundedRectangle (r, 4.0f, 1.0f);
}

void SandwaveLook::drawButtonText (juce::Graphics& g, juce::TextButton& b, bool, bool)
{
    g.setColour (b.getToggleState() ? juce::Colour (0xff2a1405) : juce::Colour (0xffe8cfa8));
    g.setFont (labelFont (13.0f, true));
    g.drawText (b.getButtonText(), b.getLocalBounds(), juce::Justification::centred);
}

//==============================================================================
SandwaveAudioProcessorEditor::SandwaveAudioProcessorEditor (SandwaveAudioProcessor& p)
    : AudioProcessorEditor (p), audioProcessor (p)
{
    setLookAndFeel (&look);
    addAndMakeVisible (plate);

    for (int i = 0; i < numKnobs; ++i)
    {
        auto& k = knobs[i];
        k.setSliderStyle (juce::Slider::RotaryVerticalDrag);
        k.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
        k.setRotaryParameters (juce::MathConstants<float>::pi * 1.25f, juce::MathConstants<float>::pi * 2.75f, true);
        k.setMouseDragSensitivity (180);
        k.setDoubleClickReturnValue (true, audioProcessor.apvts.getParameter (knobIds[i])->getDefaultValue() * 100.0f);
        k.onValueChange = [this] { repaint (knobSection); };
        addAndMakeVisible (k);
        knobAttachments[i] = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (audioProcessor.apvts, knobIds[i], k);
    }
    // turning FREQ by hand means "play this mode", so it takes the plate off the input
    knobs[5].onDragStart = [this] { audioProcessor.apvts.getParameterAsValue ("follow") = false; };

    micButton.setClickingTogglesState (true);
    micAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (audioProcessor.apvts, "follow", micButton);

    auto setPlate = [this] (int index)
    {
        auto* plateParam = audioProcessor.apvts.getParameter ("plate");
        plateParam->beginChangeGesture();
        plateParam->setValueNotifyingHost (plateParam->convertTo0to1 ((float) index));
        plateParam->endChangeGesture();
    };
    squareButton.onClick = [setPlate] { setPlate (0); };
    circleButton.onClick = [setPlate] { setPlate (1); };
    shakeButton.onClick = [this] { plate.shake(); plate.repaint(); };

    for (auto* b : { &micButton, &squareButton, &circleButton, &shakeButton })
        addAndMakeVisible (b);

    addChildComponent (infoPanel);
    addChildComponent (licenseOverlay);   // topmost: blocks the UI until the license is verified
    licenseOverlay.refresh();

    setSize (860, 542);
    tick();
    startTimerHz (60);
}

SandwaveAudioProcessorEditor::~SandwaveAudioProcessorEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

void SandwaveAudioProcessorEditor::resized()
{
    header = { 16, 16, getWidth() - 32, 40 };
    plateFrame = { 16, 72, 454, 454 };
    plate.setBounds (plateFrame.reduced (8));
    ledArea = header.withLeft (header.getRight() - 34).withSizeKeepingCentre (14, 14);

    const int rx = plateFrame.getRight() + 16, rw = getWidth() - 16 - rx;
    lcdSection = { rx, 72, rw, 112 };
    knobSection = { rx, lcdSection.getBottom() + 12, rw, 220 };
    buttonSection = { rx, knobSection.getBottom() + 12, rw, plateFrame.getBottom() - knobSection.getBottom() - 12 };

    auto l = lcdSection.reduced (12);
    l.removeFromTop (20);
    lcdBox = l.removeFromTop (44);
    l.removeFromTop (10);
    meterBox = l.removeFromTop (12);

    auto k = knobSection.reduced (8, 10);
    const int cellW = k.getWidth() / 3, cellH = k.getHeight() / 2;
    for (int i = 0; i < numKnobs; ++i)
    {
        const juce::Rectangle<int> cell (k.getX() + (i % 3) * cellW, k.getY() + (i / 3) * cellH, cellW, cellH);
        knobs[i].setBounds (cell.withHeight (60).withSizeKeepingCentre (60, 60));
    }

    auto b = buttonSection.reduced (12).withSizeKeepingCentre (buttonSection.getWidth() - 24, 36);
    const int gap = 8, bw = (b.getWidth() - 3 * gap) / 4;
    for (auto* btn : { &micButton, &squareButton, &circleButton, &shakeButton })
    {
        btn->setBounds (b.removeFromLeft (bw));
        b.removeFromLeft (gap);
    }

    infoPanel.setBounds (getLocalBounds());
    licenseOverlay.setBounds (getLocalBounds());
}

//==============================================================================
void SandwaveAudioProcessorEditor::tick()
{
    auto& p = audioProcessor;
    const bool follow = param (p, "follow") > 0.5f;
    const float gain = param (p, "gain"), sens = param (p, "sens"), grain = param (p, "grain"),
                decay = param (p, "decay"), range = param (p, "range"), freq = param (p, "freq");

    float level, pos;
    if (follow)
    {
        // input loudness after GAIN, scaled 0..1 over a 40 dB window whose floor SENS lowers
        const float db = p.getLevelDb() + (gain / 100.0f - 0.5f) * 60.0f;
        const float floorDb = -30.0f - 40.0f * sens / 100.0f;
        level = juce::jlimit (0.0f, 1.0f, (db - floorDb) / 40.0f);
        if (level < 0.05f)
            level = 0.0f;

        const float hz = p.getDominantHz();
        if (hz > 0.0f && level > 0.0f)
            logHz += (std::log2 (hz) - logHz) * 0.3f;
        pos = (logHz - std::log2 (lowestHz)) / octaveSpan (range);
    }
    else
    {
        // manual: the plate rings at FREQ as if driven by a tone generator, GAIN sets how hard
        level = gain / 100.0f;
        pos = freq / 100.0f;
        logHz = std::log2 (lowestHz) + pos * octaveSpan (range);
    }

    // DECAY: how long the plate keeps ringing once the sound stops (50 ms .. 2 s)
    const float release = 0.05f * std::pow (40.0f, decay / 100.0f);
    envelope = level >= envelope ? level : envelope * std::exp (-frameSeconds / release);
    if (envelope < 0.01f)
        envelope = 0.0f;

    const int mode = juce::jlimit (0, Plate::numModes - 1, (int) std::floor (pos * (float) Plate::numModes));
    if (mode != shownMode)
    {
        candidateFrames = mode == candidateMode ? candidateFrames + 1 : 0;
        candidateMode = mode;
        if (candidateFrames >= modeHoldFrames || ! follow)
            shownMode = mode;
    }

    plate.setCircle (param (p, "plate") > 0.5f);
    plate.setMode (shownMode);
    plate.step (envelope, grain / 100.0f);
    plate.repaint();

    const bool circle = param (p, "plate") > 0.5f;
    squareButton.setToggleState (! circle, juce::dontSendNotification);
    circleButton.setToggleState (circle, juce::dontSendNotification);

    // readouts: only repaint what changed
    meterLevel = juce::jmax (level, meterLevel * 0.85f);
    const auto hzText = juce::String (std::exp2 (logHz), 1) + " Hz";
    const int segments = juce::roundToInt (meterLevel * (float) meterSegments);
    if (hzText != shownHz || segments != shownSegments)
    {
        shownHz = hzText;
        shownSegments = segments;
        repaint (lcdSection);
    }
    const bool led = envelope > 0.5f;
    if (led != ledOn)
    {
        ledOn = led;
        repaint (ledArea);
    }
}

//==============================================================================
void SandwaveAudioProcessorEditor::paint (juce::Graphics& g)
{
    g.fillAll (bgCol);
    g.setColour (edgeCol);
    g.drawRect (getLocalBounds(), 1);

    // header strip
    g.setColour (headCol);
    g.fillRoundedRectangle (header.toFloat(), 6.0f);
    g.setColour (headText);
    g.setFont (labelFont (22.0f, true).withExtraKerningFactor (0.2f));
    g.drawText ("SANDWAVE", header.reduced (16, 0), juce::Justification::centredLeft);
    g.setFont (labelFont (14.0f).withExtraKerningFactor (0.2f));
    g.drawText ("CYMATIC VOICE PLATE", header, juce::Justification::centred);
    g.setColour (ledOn ? juce::Colour (0xffff8a2a) : juce::Colour (0xff5a2a10));
    g.fillEllipse (ledArea.toFloat().reduced (2.0f));

    // plate frame
    g.setColour (plateCol);
    g.fillRoundedRectangle (plateFrame.toFloat(), 6.0f);
    g.setColour (plateEdge);
    g.drawRoundedRectangle (plateFrame.toFloat().reduced (0.5f), 6.0f, 1.0f);

    for (auto r : { lcdSection, knobSection, buttonSection })
    {
        g.setColour (panelCol);
        g.fillRoundedRectangle (r.toFloat(), 6.0f);
    }

    // frequency display
    auto top = lcdSection.reduced (12).removeFromTop (16);
    g.setColour (textDim);
    g.setFont (labelFont (12.0f));
    g.drawText ("DOMINANT FREQ", top, juce::Justification::centredLeft);
    g.drawText ("MODE " + juce::String (plate.getModeN()) + ":" + juce::String (plate.getModeM()), top, juce::Justification::centredRight);

    g.setColour (lcdBg);
    g.fillRoundedRectangle (lcdBox.toFloat(), 4.0f);
    g.setColour (lcdText);
    g.setFont (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), 26.0f, juce::Font::plain));
    g.drawText (shownHz, lcdBox.reduced (12, 0), juce::Justification::centredRight);

    const float segW = ((float) meterBox.getWidth() - (meterSegments - 1) * 2.0f) / (float) meterSegments;
    for (int i = 0; i < meterSegments; ++i)
    {
        juce::Colour c (0xff2a1a0f);
        if (i < shownSegments)
            c = i > 15 ? juce::Colour (0xffe04a1a) : i > 11 ? juce::Colour (0xffe0a02a) : headCol;
        g.setColour (c);
        g.fillRect (juce::Rectangle<float> ((float) meterBox.getX() + (float) i * (segW + 2.0f), (float) meterBox.getY(), segW, (float) meterBox.getHeight()));
    }

    // knob names and values
    for (int i = 0; i < numKnobs; ++i)
    {
        const auto kb = knobs[i].getBounds();
        auto below = juce::Rectangle<int> (kb.getCentreX() - 50, kb.getBottom() + 4, 100, 16);
        g.setColour (textDim);
        g.setFont (labelFont (12.0f));
        g.drawText (knobNames[i], below, juce::Justification::centred);
        g.setColour (textLight);
        g.setFont (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), 13.0f, juce::Font::plain));
        g.drawText (juce::String (juce::roundToInt (knobs[i].getValue())), below.translated (0, 17), juce::Justification::centred);
    }
}

void SandwaveAudioProcessorEditor::mouseDown (const juce::MouseEvent& e)
{
    if (header.contains (e.getPosition()))
        showInfo (true);
}

void SandwaveAudioProcessorEditor::showInfo (bool show)
{
    infoPanel.setVisible (show);
    if (show)
        infoPanel.toFront (false);
    licenseOverlay.toFront (false);
}

//==============================================================================
void SandwaveAudioProcessorEditor::InfoPanel::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colours::black.withAlpha (0.75f));
    auto box = getLocalBounds().withSizeKeepingCentre (400, 300);
    g.setColour (panelCol);
    g.fillRoundedRectangle (box.toFloat(), 8.0f);
    g.setColour (headCol);
    g.drawRoundedRectangle (box.toFloat().reduced (0.5f), 8.0f, 1.5f);

    box.reduce (24, 20);
    g.setColour (headCol);
    g.setFont (labelFont (24.0f, true));
    g.drawText ("SANDWAVE", box.removeFromTop (32), juce::Justification::centred);

    g.setColour (textDim);
    g.setFont (labelFont (13.0f));
    g.drawText ("Cymatic voice plate", box.removeFromTop (20), juce::Justification::centred);
    g.drawText ("Version " JucePlugin_VersionString, box.removeFromTop (20), juce::Justification::centred);
    box.removeFromTop (10);

    g.setColour (accentHot);
    g.setFont (labelFont (15.0f, true));
    g.drawText ("Made by Uranium Music", box.removeFromTop (24), juce::Justification::centred);

    // license section
    g.setColour (headCol.withAlpha (0.4f));
    g.fillRect (box.removeFromTop (1));
    box.removeFromTop (8);
    g.setColour (textLight);
    g.setFont (labelFont (11.0f, true));
    g.drawText ("LICENSE", box.removeFromTop (16), juce::Justification::centred);

    g.setColour (textDim);
    g.setFont (labelFont (12.0f));
   #if SANDWAVE_DEV_UNLOCKED
    g.setColour (accent);
    g.drawText ("DEVELOPMENT BUILD - NO LICENSE CHECK", box.removeFromTop (18), juce::Justification::centred);
   #else
    const auto lic = license.getStatus();
    if (lic.state == LicenseManager::State::licensed)
    {
        g.setColour (accentHot);
        g.drawText (lic.offline ? "Active (offline)" : "Active", box.removeFromTop (18), juce::Justification::centred);
        g.setColour (textDim);
        if (lic.licensee.isNotEmpty())
            g.drawText ("Licensed to " + lic.licensee, box.removeFromTop (18), juce::Justification::centred);
        g.drawText (lic.lifetime ? juce::String ("Lifetime license") : "Valid until " + lic.expires.formatted ("%d %b %Y"),
                    box.removeFromTop (18), juce::Justification::centred);
    }
    else
    {
        g.drawText ("Not activated", box.removeFromTop (18), juce::Justification::centred);
    }
   #endif

    g.setColour (textDim.withAlpha (0.6f));
    g.setFont (labelFont (10.0f));
    g.drawText ("click anywhere to close", box.removeFromBottom (14), juce::Justification::centred);
}
