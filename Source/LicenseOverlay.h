#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "License.h"

// Covers the whole editor while the plugin is not licensed and asks for a key.
// It cannot be dismissed; it hides itself once the key is verified.
class LicenseOverlay : public juce::Component, private juce::Timer
{
public:
    explicit LicenseOverlay (LicenseManager&);

    void paint (juce::Graphics&) override;
    void resized() override;

    void refresh();   // pick up the manager's current state

private:
    void timerCallback() override { refresh(); }
    void submit();

    LicenseManager& license;
    juce::TextEditor emailField, keyField;
    juce::TextButton activateButton { "ACTIVATE" };
    juce::Rectangle<int> box, messageArea;
    LicenseManager::Status shown;
};
