#pragma once

#include <array>

namespace theythem
{
// Factory presets describe the voice. Input/output trim and global bypass are
// session controls and intentionally remain unchanged when browsing sounds.
struct VoicePreset
{
    const char* name;
    const char* description;
    float pitch, formant, character, mix;
    bool highPass = true, compressor = true, transform = true;
};

inline constexpr std::array<VoicePreset, 8> factoryPresets {{
    { "Octave Bloom", "An octave up, with the original vocal envelope.", 12.0f, 0.0f, 0.65f, 1.0f },
    { "Clean Slate", "Zero pitch and formant shift. A starting point for your voice.", 0.0f, 0.0f, 0.0f, 1.0f },
    { "Soft Lift", "A small pitch lift with a gently brighter envelope.", 3.0f, 1.0f, 0.2f, 1.0f },
    { "Airlight", "A higher register and an open, bright character.", 5.0f, 2.0f, 0.35f, 1.0f },
    { "Low Tide", "A lower register with a slightly darker envelope.", -4.0f, -2.0f, 0.0f, 1.0f },
    { "Deep Space", "An octave down for a deliberately oversized sound.", -12.0f, -4.0f, 0.0f, 1.0f },
    { "Small Hours", "A playful high voice with a smaller vocal envelope.", 7.0f, 5.0f, 0.4f, 1.0f },
    { "Parallel Glow", "Blend an upper octave underneath the delayed dry voice.", 12.0f, 2.0f, 0.35f, 0.35f }
}};
} // namespace theythem
