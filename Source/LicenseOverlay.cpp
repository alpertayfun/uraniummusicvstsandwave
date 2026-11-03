#include "LicenseOverlay.h"
#include "Theme.h"

using namespace theme;

namespace
{
    const juce::Colour errorCol (0xffff7a6b);
}

LicenseOverlay::LicenseOverlay (LicenseManager& lm) : license (lm)
{
    for (auto* field : { &emailField, &keyField })
    {
        field->setJustification (juce::Justification::centred);
        field->setColour (juce::TextEditor::backgroundColourId, screenCol);
        field->setColour (juce::TextEditor::textColourId, accentHot);
        field->setColour (juce::TextEditor::outlineColourId, accent.withAlpha (0.4f));
        field->setColour (juce::TextEditor::focusedOutlineColourId, accent);
        field->onReturnKey = [this] { submit(); };
        addAndMakeVisible (field);
    }

    emailField.setTextToShowWhenEmpty ("E-mail address", textDim.withAlpha (0.6f));
    emailField.setInputRestrictions (254);
    emailField.setFont (juce::FontOptions (15.0f));

    keyField.setTextToShowWhenEmpty ("URAN-XXXX-XXXX-XXXX-XXXX", textDim.withAlpha (0.6f));
    keyField.setInputRestrictions (29, "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789- ");
    keyField.setFont (juce::FontOptions (17.0f, juce::Font::bold));

    activateButton.setToggleState (true, juce::dontSendNotification);   // drawn in the accent colour
    activateButton.setClickingTogglesState (false);
    activateButton.onClick = [this] { submit(); };
    addAndMakeVisible (activateButton);

    refresh();
    startTimerHz (5);
}

void LicenseOverlay::submit()
{
    if (shown.state == LicenseManager::State::checking)
        return;
    license.activateAsync (keyField.getText(), emailField.getText());
    refresh();
}

void LicenseOverlay::refresh()
{
    const auto s = license.getStatus();
    const bool show = s.state != LicenseManager::State::licensed;

    if (show != isVisible())
    {
        setVisible (show);
        if (show) (emailField.isEmpty() ? emailField : keyField).grabKeyboardFocus();
    }

    const bool busy = s.state == LicenseManager::State::checking;
    activateButton.setEnabled (! busy);
    keyField.setEnabled (! busy);
    emailField.setEnabled (! busy);
    activateButton.setButtonText (busy ? "CHECKING..." : "ACTIVATE");

    if (s.message != shown.message || s.state != shown.state)
    {
        shown = s;
        repaint();
    }
}

void LicenseOverlay::resized()
{
    box = getLocalBounds().withSizeKeepingCentre (460, 346);
    auto r = box.reduced (30, 24);
    r.removeFromTop (110);   // title + text
    emailField.setBounds (r.removeFromTop (36));
    r.removeFromTop (8);
    keyField.setBounds (r.removeFromTop (40));
    r.removeFromTop (12);
    activateButton.setBounds (r.removeFromTop (34).withSizeKeepingCentre (180, 34));
    r.removeFromTop (10);
    messageArea = r;
}

void LicenseOverlay::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colours::black.withAlpha (0.82f));

    g.setColour (panelCol);
    g.fillRoundedRectangle (box.toFloat(), 10.0f);
    g.setColour (accent);
    g.drawRoundedRectangle (box.toFloat().reduced (0.5f), 10.0f, 1.5f);

    auto r = box.reduced (30, 24);
    g.setColour (logoCol);
    g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
    g.drawText ("URANIUM MUSIC", r.removeFromTop (16), juce::Justification::centred);

    g.setColour (textLight);
    g.setFont (juce::FontOptions (24.0f, juce::Font::bold));
    g.drawText ("ACTIVATE SANDWAVE", r.removeFromTop (34), juce::Justification::centred);

    g.setColour (textDim);
    g.setFont (juce::FontOptions (13.0f));
    g.drawFittedText ("Enter the e-mail address and license key you received\nfrom Uranium Music. An internet connection is needed.",
                      r.removeFromTop (44), juce::Justification::centred, 2);

    const bool busy = shown.state == LicenseManager::State::checking;
    const auto text = shown.message.isNotEmpty() ? shown.message : juce::String (busy ? "Checking license..." : "");
    g.setColour (busy ? accentHot : errorCol);
    g.setFont (juce::FontOptions (12.0f));
    g.drawFittedText (text, messageArea, juce::Justification::centredTop, 3);
}
