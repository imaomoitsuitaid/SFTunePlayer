#pragma once
#include "SoundBank.h"
#include "Tuning.h"
#include <juce_audio_basics/juce_audio_basics.h>

struct EngineParams
{
    bool  useFileEnvelope = false;  // true = envelope from SF2/SFZ, false = manual ADSR below
    float attack = 0.005f, decay = 0.3f, sustain = 0.8f, release = 0.3f; // seconds / 0..1
    float gainDb = 0.0f;
    float bendRangeSemis = 2.0f;
};

class SynthEngine
{
public:
    static constexpr int maxVoices = 96;

    void prepare (double sampleRate);
    void setBank (std::shared_ptr<SoundBank> newBank, int presetIndex);
    void setPreset (int index);
    void setTuning (const TuningTable& t);
    void setParams (const EngineParams& p) { params = p; }

    int  getPresetIndex() const { return presetIndex; }
    bool consumePresetChangedFlag() { auto f = presetChangedByMidi; presetChangedByMidi = false; return f; }
    const TuningTable& getTuning() const { return tuning; }

    void allNotesOff (bool immediate);
    void render (juce::AudioBuffer<float>& out, const juce::MidiBuffer& midi);
    int  activeVoiceCount() const;

private:
    struct Voice
    {
        bool active = false;
        const Region* region = nullptr;
        int note = 0, velocity = 0, channel = 1;
        bool keyDown = false, sustainedByPedal = false, isReleaseTrigger = false;
        double pos = 0.0;
        double baseSemis = 0.0;   // pitch offset excluding pitch-bend
        float gainL = 0.0f, gainR = 0.0f;
        uint64_t startOrder = 0;

        // envelope
        enum Stage { delay, attack, hold, decay, sustain, release, done } stage = done;
        float level = 0.0f, stageTime = 0.0f;
        EnvelopeSpec env;
        float releaseCoef = 0.0f, decayCoef = 0.0f;
        bool  fastRelease = false;
    };

    void handleMidi (const juce::MidiMessage& m);
    void noteOn (int ch, int note, int vel);
    void noteOff (int ch, int note);
    void startVoice (const Region& r, int ch, int note, int vel, bool releaseTrig);
    void enterRelease (Voice& v, bool fast = false);
    void renderVoice (Voice& v, float* L, float* R, int n);
    double pitchIncrement (const Voice& v) const;
    float  readSample (const Voice& v, int ch, int64_t i) const;

    std::shared_ptr<SoundBank> bank;
    int presetIndex = 0;
    TuningTable tuning = TuningTable::equalTemperament();
    EngineParams params;

    double sampleRate = 44100.0;
    std::array<Voice, maxVoices> voices;
    uint64_t voiceCounter = 0;

    bool sustainPedal = false;
    float pitchBend = 0.0f;              // -1..+1
    float cc7 = 1.0f, cc11 = 1.0f;
    int bankMsb = 0, bankLsb = 0;
    bool presetChangedByMidi = false;
    std::array<int, 128> lastVelocity {};
};
