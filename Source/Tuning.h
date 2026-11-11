#pragma once
#include <juce_core/juce_core.h>
#include <array>

// ============================================================================
//  Tuning table: frequency (Hz) for each of the 128 MIDI keys.
//  Sources: 12-TET (default), Scala .scl (+ optional .kbm), AnaMark .tun
//  A key with frequency <= 0 is unmapped and will not sound.
// ============================================================================

struct TuningTable
{
    std::array<double, 128> freq {};
    juce::String name = "12-TET";
    juce::String description;

    static TuningTable equalTemperament (double a4 = 440.0);

    // Pitch of a key expressed in "12-TET MIDI note" units (60.0 = middle C @ A=440)
    double semitones (int key) const;
    bool isMapped (int key) const { return key >= 0 && key < 128 && freq[(size_t) key] > 0.0; }
};

struct ScalaScale
{
    juce::String description;
    std::vector<double> cents;   // degrees 1..N, last entry is the period (e.g. 1200 for octave)
    bool parse (const juce::String& text, juce::String& error);
};

struct KeyboardMapping   // Scala .kbm
{
    int mapSize = 0;              // 0 = linear mapping
    int firstNote = 0, lastNote = 127;
    int middleNote = 60;          // key where scale degree 0 is mapped
    int referenceNote = 60;
    double referenceFreq = 261.6255653005986;
    int octaveDegree = 0;         // 0 = use scale size
    std::vector<int> mapping;     // -1 = unmapped
    bool parse (const juce::String& text, juce::String& error);
};

namespace TuningLoader
{
    TuningTable fromScala (const ScalaScale& scl, const KeyboardMapping& kbm);
    bool loadTun (const juce::String& text, TuningTable& out, juce::String& error);

    // Loads .scl / .tun / .kbm-less scl. For .scl the given base note/frequency is used
    // (scale degree 0 sits on baseNote, which sounds at baseFreq).
    bool loadFile (const juce::File& file, int baseNote, double baseFreq, TuningTable& out, juce::String& error);
}
