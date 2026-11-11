#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "SynthEngine.h"

class SFTunePlayerProcessor : public juce::AudioProcessor, private juce::Timer
{
public:
    SFTunePlayerProcessor();
    ~SFTunePlayerProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 2.0; }

    int getNumPrograms() override;
    int getCurrentProgram() override;
    void setCurrentProgram (int index) override;
    const juce::String getProgramName (int index) override;
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    // ---- called from the editor (message thread) ----
    bool loadSoundFile (const juce::File& f, juce::String& message);
    bool loadTuningFile (const juce::File& f, juce::String& message);
    void resetTuning();
    void selectPreset (int index);

    juce::StringArray getPresetNames() const;
    int getPresetIndex() const { return presetIndex.load(); }
    juce::String getSoundFileName() const { return soundFile.getFileName(); }
    juce::String getTuningName() const;
    bool tuningUsesBaseNote() const { return tuningKind == TuningKind::scala; }
    double getKeyFrequency (int key) const;

    juce::AudioProcessorValueTreeState apvts;
    juce::MidiKeyboardState keyboardState;

    std::atomic<int> uiChangeCounter { 0 };   // bumps when bank/preset/tuning changes

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    void timerCallback() override;
    void rebuildScalaTuning();
    void pushTuning (const TuningTable& t);
    EngineParams readParams() const;

    juce::CriticalSection engineLock;   // audio thread only ever try-locks
    SynthEngine engine;

    std::shared_ptr<SoundBank> bank;    // also owned by engine
    juce::File soundFile, tuningFile, kbmFile;  // kbmFile: explicitly loaded .kbm
    std::atomic<int> presetIndex { 0 };

    enum class TuningKind { equal, scala, tun } tuningKind = TuningKind::equal;
    ScalaScale scala;
    KeyboardMapping scalaKbm;
    bool scalaHasKbm = false;
    TuningTable tuning = TuningTable::equalTemperament();
    int lastBaseNote = -1; double lastBaseFreq = -1.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SFTunePlayerProcessor)
};
