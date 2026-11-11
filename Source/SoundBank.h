#pragma once
#include <juce_audio_formats/juce_audio_formats.h>
#include <memory>
#include <vector>

// ============================================================================
//  Common in-memory representation for SF2 and SFZ content.
//  Both loaders flatten their file into a list of Regions per preset.
// ============================================================================

struct SampleData
{
    // Interleaved is avoided: one vector per channel (1 or 2 channels).
    std::vector<std::vector<float>> channels;
    double sampleRate = 44100.0;
    juce::String name;

    int numChannels() const { return (int) channels.size(); }
    int64_t length() const { return channels.empty() ? 0 : (int64_t) channels[0].size(); }
};

enum class LoopMode
{
    none,           // play once through to end (still obeys note-off / envelope)
    oneShot,        // ignore note-off; play to the end
    continuous,     // loop forever
    sustain         // loop while key held, then play out to end on release
};

enum class Trigger { attack, release };

struct EnvelopeSpec
{
    // seconds, sustain is linear 0..1
    float delay = 0.0f, attack = 0.001f, hold = 0.0f, decay = 0.0f, sustain = 1.0f, release = 0.01f;
};

struct Region
{
    std::shared_ptr<const SampleData> sample;
    int channelOffset = 0;              // for SF2: always 0 (mono samples)

    int loKey = 0, hiKey = 127;
    int loVel = 0, hiVel = 127;

    int64_t start = 0, end = 0;         // end is exclusive
    int64_t loopStart = 0, loopEnd = 0; // loopEnd exclusive
    LoopMode loopMode = LoopMode::none;
    Trigger trigger = Trigger::attack;

    float rootKey = 60.0f;              // MIDI key at which the sample sounds unchanged
    float tuneCents = 0.0f;             // fine + coarse + sample correction
    float keyTrack = 1.0f;              // 1.0 = 100 cents per key (SF2 scaleTuning / SFZ pitch_keytrack)

    float gain = 1.0f;                  // linear
    float pan = 0.0f;                   // -1..+1

    int exclusiveClass = 0;             // SF2 exclusiveClass / SFZ group+off_by
    int offBy = 0;

    EnvelopeSpec env;                   // envelope stored in the file
};

struct Preset
{
    juce::String name;
    int bank = 0, program = 0;
    std::vector<Region> regions;
};

struct SoundBank
{
    juce::String sourceName;            // file name
    std::vector<Preset> presets;
};

namespace SoundBankLoader
{
    // Returns nullptr and sets error on failure.
    std::shared_ptr<SoundBank> loadSF2 (const juce::File& file, juce::String& error);
    std::shared_ptr<SoundBank> loadSFZ (const juce::File& file, juce::String& error);
    std::shared_ptr<SoundBank> load    (const juce::File& file, juce::String& error);
}
