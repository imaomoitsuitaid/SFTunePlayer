#pragma once
#include <juce_audio_utils/juce_audio_utils.h>
#include "PluginProcessor.h"
#include "UpdateChecker.h"

class SFTunePlayerEditor : public juce::AudioProcessorEditor,
                           private juce::Timer,
                           private juce::MidiKeyboardState::Listener,
                           private juce::ChangeListener
{
public:
    explicit SFTunePlayerEditor (SFTunePlayerProcessor&);
    ~SFTunePlayerEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    struct Knob
    {
        juce::Slider slider { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::TextBoxBelow };
        juce::Label label;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
    };

    void timerCallback() override;
    void handleNoteOn (juce::MidiKeyboardState*, int, int note, float) override { lastNote = note; }
    void handleNoteOff (juce::MidiKeyboardState*, int, int, float) override {}
    void refreshFromProcessor();
    void changeListenerCallback (juce::ChangeBroadcaster*) override { refreshUpdateUi(); }
    void refreshUpdateUi();
    void setupKnob (Knob& k, const juce::String& paramID, const juce::String& text);
    void chooseFile (bool soundfont);
    void showMessage (const juce::String& m, bool error);

    SFTunePlayerProcessor& proc;

    juce::TextButton loadSoundButton { "Load SF2 / SFZ..." }, loadTuningButton { "Load SCL / KBM / TUN..." }, resetTuningButton { "12-TET" };
    juce::Label soundLabel, tuningLabel, statusLabel, noteInfoLabel;
    juce::ComboBox presetBox;

    Knob attack, decay, sustain, release, gain, bend, baseNote, baseFreq;
    juce::ToggleButton fileEnvToggle { "Use envelope from file (ignore ADSR knobs)" };
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> fileEnvAttachment;

    juce::SharedResourcePointer<UpdateChecker> updater;
    juce::TextButton updateButton;
    juce::Label versionLabel;

    juce::MidiKeyboardComponent keyboard;
    std::unique_ptr<juce::FileChooser> chooser;

    int lastUiCounter = -1;
    std::atomic<int> lastNote { -1 };
    int shownNote = -2;
    juce::String shownTuning;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SFTunePlayerEditor)
};
