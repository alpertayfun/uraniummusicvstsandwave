#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace theme
{
    // brown and tan, like an old acid box
    inline const juce::Colour bgCol      (0xff3b2618);
    inline const juce::Colour panelCol   (0xff4c3220);
    inline const juce::Colour edgeCol    (0xff241509);
    inline const juce::Colour headCol    (0xffc9a477);
    inline const juce::Colour headText   (0xff2e1c10);
    inline const juce::Colour plateCol   (0xff120b06);
    inline const juce::Colour plateEdge  (0xff5a3b24);
    inline const juce::Colour sandCol    (0xfff3e4c8);
    inline const juce::Colour knobBody   (0xff1c120b);
    inline const juce::Colour knobRing   (0xff8a6240);
    inline const juce::Colour knobMark   (0xfff2c48a);
    inline const juce::Colour buttonCol  (0xff2a1a0f);
    inline const juce::Colour lcdBg      (0xff1a2410);
    inline const juce::Colour lcdText    (0xffc7e08a);
    inline const juce::Colour accent     (0xffe07a2a);
    inline const juce::Colour accentHot  (0xffffb066);
    inline const juce::Colour textLight  (0xfff0dfc4);
    inline const juce::Colour textDim    (0xffd9b98f);

    // names the license overlay draws with
    inline const juce::Colour screenCol = buttonCol;
    inline const juce::Colour logoCol = headCol;

    inline juce::Font labelFont (float size, bool bold = false)
    {
        return juce::Font (juce::FontOptions (size, bold ? juce::Font::bold : juce::Font::plain).withKerningFactor (0.12f));
    }
}
