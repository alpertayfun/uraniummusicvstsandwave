#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <vector>

// A Chladni plate covered in sand. Each step shakes every grain by an amount that grows with how
// strongly its spot on the plate moves in the current vibration mode, so the grains drift out of
// the moving areas and pile up on the still nodal lines.
class Plate : public juce::Component
{
public:
    static constexpr int numModes = 14;

    Plate();

    void setCircle (bool circle);      // square or round plate; changing it pours fresh sand
    void setMode (int modeIndex);      // 0 .. numModes - 1, low to high frequency
    void step (float energy, float grain);   // energy 0..1 = how hard the plate vibrates this frame
    void shake();                      // scatter the sand evenly again

    int getModeN() const noexcept { return n; }
    int getModeM() const noexcept { return m; }

    void paint (juce::Graphics&) override;

private:
    void rebuildField();
    float fieldAt (float x, float y) const;
    float random() noexcept;   // 0..1

    static constexpr int numGrains = 12000, fieldRes = 256;

    std::vector<float> gx, gy;      // grain positions, 0..1
    std::vector<float> field;       // |plate displacement| on a grid, 0..1
    bool circle = false;
    int mode = 6, n = 3, m = 5;
    uint32_t seed = 0x2545f491u;
};
