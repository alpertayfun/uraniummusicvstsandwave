#include "Plate.h"
#include "Theme.h"

namespace
{
    // (n, m) pairs in rising order of pitch; n != m, otherwise the square plate pattern vanishes.
    // Even entries use the symmetric shape (closed rings), odd ones the antisymmetric one (crossing lines).
    const int modes[Plate::numModes][2] = {
        { 1, 2 }, { 1, 3 }, { 2, 3 }, { 1, 4 }, { 3, 4 }, { 2, 5 }, { 3, 5 },
        { 1, 6 }, { 4, 5 }, { 3, 7 }, { 5, 6 }, { 4, 7 }, { 5, 8 }, { 6, 7 }
    };
}

Plate::Plate()
{
    gx.resize (numGrains);
    gy.resize (numGrains);
    field.resize ((size_t) (fieldRes * fieldRes));
    setOpaque (true);
    shake();
    rebuildField();
}

float Plate::random() noexcept
{
    seed ^= seed << 13;
    seed ^= seed >> 17;
    seed ^= seed << 5;
    return (float) (seed >> 8) * (1.0f / 16777216.0f);
}

void Plate::shake()
{
    for (int i = 0; i < numGrains; ++i)
    {
        float x, y;
        do { x = random(); y = random(); }
        while (circle && juce::square (x - 0.5f) + juce::square (y - 0.5f) > 0.25f);
        gx[(size_t) i] = x;
        gy[(size_t) i] = y;
    }
}

void Plate::setCircle (bool c)
{
    if (c == circle)
        return;
    circle = c;
    rebuildField();
    shake();
}

void Plate::setMode (int index)
{
    index = juce::jlimit (0, numModes - 1, index);
    if (index == mode)
        return;
    mode = index;
    n = modes[index][0];
    m = modes[index][1];
    rebuildField();
}

void Plate::rebuildField()
{
    using juce::MathConstants;
    for (int j = 0; j < fieldRes; ++j)
        for (int i = 0; i < fieldRes; ++i)
        {
            const float x = ((float) i + 0.5f) / (float) fieldRes, y = ((float) j + 0.5f) / (float) fieldRes;
            float v;
            if (circle)
            {
                const float dx = x - 0.5f, dy = y - 0.5f;
                const float r = std::sqrt (dx * dx + dy * dy) * 2.0f, a = std::atan2 (dy, dx);
                v = std::cos ((float) n * MathConstants<float>::pi * r) * std::cos ((float) m * a);
            }
            else
            {
                const float X = x * MathConstants<float>::pi, Y = y * MathConstants<float>::pi;
                const float sign = mode % 2 == 0 ? 1.0f : -1.0f;
                v = 0.5f * (std::cos ((float) n * X) * std::cos ((float) m * Y) + sign * std::cos ((float) m * X) * std::cos ((float) n * Y));
            }
            field[(size_t) (j * fieldRes + i)] = std::abs (v);
        }
}

float Plate::fieldAt (float x, float y) const
{
    const int i = juce::jlimit (0, fieldRes - 1, (int) (x * (float) fieldRes));
    const int j = juce::jlimit (0, fieldRes - 1, (int) (y * (float) fieldRes));
    return field[(size_t) (j * fieldRes + i)];
}

void Plate::step (float energy, float grain)
{
    if (energy <= 0.0f)
        return;   // a still plate leaves the sand where it is

    const float amp = energy * (0.004f + 0.022f * grain);
    for (int i = 0; i < numGrains; ++i)
    {
        float x = gx[(size_t) i], y = gy[(size_t) i];
        const float s = amp * (0.06f + fieldAt (x, y));   // a little jitter everywhere gives the lines some width
        // grains also slide downhill towards the still lines, which keeps the lines sharp
        const float d = 1.0f / (float) fieldRes;
        const float slopeX = fieldAt (x + d, y) - fieldAt (x - d, y), slopeY = fieldAt (x, y + d) - fieldAt (x, y - d);
        x = juce::jlimit (0.0f, 0.9999f, x + (random() - 0.5f) * 2.0f * s - amp * 1.5f * slopeX);
        y = juce::jlimit (0.0f, 0.9999f, y + (random() - 0.5f) * 2.0f * s - amp * 1.5f * slopeY);
        if (circle)
        {
            const float dx = x - 0.5f, dy = y - 0.5f, dist = std::sqrt (dx * dx + dy * dy);
            if (dist > 0.499f)
            {
                x = 0.5f + dx * 0.499f / dist;
                y = 0.5f + dy * 0.499f / dist;
            }
        }
        gx[(size_t) i] = x;
        gy[(size_t) i] = y;
    }
}

void Plate::paint (juce::Graphics& g)
{
    using namespace theme;
    const int w = getWidth(), h = getHeight();
    if (w <= 0 || h <= 0)
        return;

    g.fillAll (plateCol);
    if (circle)
    {
        g.setColour (plateEdge.withAlpha (0.7f));
        g.drawEllipse (getLocalBounds().toFloat().reduced (1.0f), 1.0f);
    }

    juce::Image image (juce::Image::ARGB, w, h, true);
    {
        juce::Image::BitmapData data (image, juce::Image::BitmapData::writeOnly);
        const auto sand = sandCol;
        for (int i = 0; i < numGrains; ++i)
        {
            const int px = (int) (gx[(size_t) i] * (float) w), py = (int) (gy[(size_t) i] * (float) h);
            for (int dy = 0; dy < 2; ++dy)
                for (int dx = 0; dx < 2; ++dx)
                    if (px + dx < w && py + dy < h)
                        data.setPixelColour (px + dx, py + dy, sand);
        }
    }
    g.drawImageAt (image, 0, 0);

    // the bolt the plate is mounted on
    const auto c = getLocalBounds().toFloat().getCentre();
    g.setColour (juce::Colour (0xffb8b0a4));
    g.fillEllipse (c.x - 6.0f, c.y - 6.0f, 12.0f, 12.0f);
    g.setColour (juce::Colour (0xff6e665c));
    g.drawEllipse (c.x - 6.0f, c.y - 6.0f, 12.0f, 12.0f, 1.0f);
}
