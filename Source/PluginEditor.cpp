#include "PluginEditor.h"
#include <cmath>

namespace Colours
{
    const juce::Colour bg     { 0xff1b1d22 };
    const juce::Colour panel  { 0xff262a31 };
    const juce::Colour accent { 0xff5fb3a1 };
    const juce::Colour text   { 0xffe4e6ea };
    const juce::Colour dim    { 0xff8d939e };
}

SFTunePlayerEditor::SFTunePlayerEditor (SFTunePlayerProcessor& p)
    : AudioProcessorEditor (&p), proc (p),
      keyboard (p.keyboardState, juce::MidiKeyboardComponent::horizontalKeyboard)
{
    auto& lf = getLookAndFeel();
    lf.setColour (juce::Slider::rotarySliderFillColourId, Colours::accent);
    lf.setColour (juce::Slider::thumbColourId, Colours::accent);
    lf.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    lf.setColour (juce::ComboBox::backgroundColourId, Colours::panel);
    lf.setColour (juce::TextButton::buttonColourId, Colours::panel);

    for (auto* b : { &loadSoundButton, &loadTuningButton, &resetTuningButton }) addAndMakeVisible (*b);
    loadSoundButton.onClick  = [this] { chooseFile (true); };
    loadTuningButton.onClick = [this] { chooseFile (false); };
    resetTuningButton.onClick = [this] { proc.resetTuning(); showMessage ("Tuning reset to 12-TET", false); };

    for (auto* l : { &soundLabel, &tuningLabel, &statusLabel, &noteInfoLabel })
    {
        addAndMakeVisible (*l);
        l->setColour (juce::Label::textColourId, Colours::text);
        l->setMinimumHorizontalScale (0.6f);
    }
    statusLabel.setColour (juce::Label::textColourId, Colours::dim);
    noteInfoLabel.setJustificationType (juce::Justification::centredRight);

    addAndMakeVisible (presetBox);
    presetBox.setTextWhenNothingSelected ("(no soundfont loaded)");
    presetBox.onChange = [this]
    {
        const int idx = presetBox.getSelectedItemIndex();
        if (idx >= 0 && idx != proc.getPresetIndex()) proc.selectPreset (idx);
    };

    setupKnob (attack,  "attack",  "Attack");
    setupKnob (decay,   "decay",   "Decay");
    setupKnob (sustain, "sustain", "Sustain");
    setupKnob (release, "release", "Release");
    setupKnob (gain,    "gain",    "Gain");
    setupKnob (bend,    "bendRange", "Bend Range");
    setupKnob (baseNote, "baseNote", "Scale Base Key");
    setupKnob (baseFreq, "baseFreq", "Base Freq (Hz)");

    addAndMakeVisible (fileEnvToggle);
    fileEnvToggle.setColour (juce::ToggleButton::textColourId, Colours::text);
    fileEnvToggle.setColour (juce::ToggleButton::tickColourId, Colours::accent);
    fileEnvAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (proc.apvts, "useFileEnv", fileEnvToggle);
    fileEnvToggle.onStateChange = [this]
    {
        const bool manual = ! fileEnvToggle.getToggleState();
        for (auto* k : { &attack, &decay, &sustain, &release }) k->slider.setAlpha (manual ? 1.0f : 0.4f);
    };

    addAndMakeVisible (keyboard);
    keyboard.setAvailableRange (0, 127);
    keyboard.setLowestVisibleKey (36);
    keyboard.setKeyWidth (16.0f);
    proc.keyboardState.addListener (this);

    addAndMakeVisible (updateButton);
    addAndMakeVisible (versionLabel);
    versionLabel.setJustificationType (juce::Justification::centredRight);
    versionLabel.setColour (juce::Label::textColourId, Colours::dim);
    versionLabel.setMinimumHorizontalScale (0.5f);
    updateButton.onClick = [this]
    {
        if (updater->getState() == UpdateChecker::State::updateAvailable) updater->downloadAndInstall();
        else updater->checkNow();
    };
    updater->broadcaster.addChangeListener (this);
    if (updater->getState() == UpdateChecker::State::idle) updater->checkNow();   // once per DAW session
    refreshUpdateUi();

    setResizable (true, true);
    setResizeLimits (720, 440, 1600, 900);
    setSize (820, 470);

    showMessage ("Load an SF2 / SFZ instrument, then optionally a SCL / KBM / TUN tuning.", false);
    refreshFromProcessor();
    startTimerHz (15);
}

SFTunePlayerEditor::~SFTunePlayerEditor()
{
    updater->broadcaster.removeChangeListener (this);
    proc.keyboardState.removeListener (this);
}

void SFTunePlayerEditor::setupKnob (Knob& k, const juce::String& paramID, const juce::String& text)
{
    addAndMakeVisible (k.slider);
    addAndMakeVisible (k.label);
    k.slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 90, 18);
    k.slider.setColour (juce::Slider::textBoxTextColourId, Colours::text);
    k.label.setText (text, juce::dontSendNotification);
    k.label.setJustificationType (juce::Justification::centred);
    k.label.setColour (juce::Label::textColourId, Colours::dim);
    k.attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (proc.apvts, paramID, k.slider);
}

void SFTunePlayerEditor::chooseFile (bool soundfont)
{
    chooser = std::make_unique<juce::FileChooser> (soundfont ? "Load SoundFont" : "Load Tuning",
                                                   juce::File::getSpecialLocation (juce::File::userHomeDirectory),
                                                   soundfont ? "*.sf2;*.sfz" : "*.scl;*.kbm;*.tun");
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                          [this, soundfont] (const juce::FileChooser& fc)
    {
        const auto f = fc.getResult();
        if (f == juce::File()) return;
        juce::String msg;
        const bool ok = soundfont ? proc.loadSoundFile (f, msg) : proc.loadTuningFile (f, msg);
        showMessage (msg, ! ok);
        refreshFromProcessor();
    });
}

void SFTunePlayerEditor::showMessage (const juce::String& m, bool error)
{
    statusLabel.setText (m, juce::dontSendNotification);
    statusLabel.setColour (juce::Label::textColourId, error ? juce::Colour (0xffe07a6a) : Colours::dim);
}

void SFTunePlayerEditor::refreshFromProcessor()
{
    lastUiCounter = proc.uiChangeCounter.load();

    const auto names = proc.getPresetNames();
    presetBox.clear (juce::dontSendNotification);
    for (int i = 0; i < names.size(); ++i) presetBox.addItem (names[i], i + 1);
    if (! names.isEmpty()) presetBox.setSelectedItemIndex (proc.getPresetIndex(), juce::dontSendNotification);

    const auto sf = proc.getSoundFileName();
    soundLabel.setText (sf.isEmpty() ? "SoundFont: -" : "SoundFont: " + sf, juce::dontSendNotification);
    tuningLabel.setText ("Tuning: " + proc.getTuningName(), juce::dontSendNotification);

    const bool scl = proc.tuningUsesBaseNote();
    baseNote.slider.setEnabled (scl);
    baseFreq.slider.setEnabled (scl);
    baseNote.slider.setAlpha (scl ? 1.0f : 0.4f);
    baseFreq.slider.setAlpha (scl ? 1.0f : 0.4f);
    shownNote = -2;
    fileEnvToggle.onStateChange();
}

void SFTunePlayerEditor::refreshUpdateUi()
{
    using S = UpdateChecker::State;
    const auto st = updater->getState();
    versionLabel.setText (updater->getStatusText().isNotEmpty() ? updater->getStatusText() : "v" + UpdateChecker::currentVersion(),
                          juce::dontSendNotification);
    const bool available = st == S::updateAvailable;
    updateButton.setButtonText (available ? "Update now" : "Check updates");
    updateButton.setColour (juce::TextButton::buttonColourId, available ? Colours::accent.darker (0.3f) : Colours::panel);
    updateButton.setEnabled (st != S::checking && st != S::downloading && st != S::disabled);
    updateButton.setVisible (st != S::disabled);
    versionLabel.setColour (juce::Label::textColourId, available ? Colours::accent : Colours::dim);
}

void SFTunePlayerEditor::timerCallback()
{
    if (proc.uiChangeCounter.load() != lastUiCounter) refreshFromProcessor();

    const int n = lastNote.load();
    if (n != shownNote)
    {
        shownNote = n;
        if (n >= 0)
        {
            const double f = proc.getKeyFrequency (n);
            juce::String s = juce::MidiMessage::getMidiNoteName (n, true, true, 4) + " (" + juce::String (n) + ")  ";
            if (f > 0.0)
            {
                const double cents = 1200.0 * std::log2 (f / (440.0 * std::pow (2.0, (n - 69) / 12.0)));
                s << juce::String (f, 3) << " Hz  (" << (cents >= 0 ? "+" : "") << juce::String (cents, 1) << " c vs 12-TET)";
            }
            else s << "unmapped";
            noteInfoLabel.setText (s, juce::dontSendNotification);
        }
    }
}

void SFTunePlayerEditor::paint (juce::Graphics& g)
{
    g.fillAll (Colours::bg);
    auto area = getLocalBounds().reduced (10);

    g.setColour (Colours::accent);
    g.setFont (juce::FontOptions (20.0f, juce::Font::bold));
    g.drawText ("SF Tune Player", area.removeFromTop (28), juce::Justification::centredLeft);

    // panels behind knob groups
    g.setColour (Colours::panel);
    auto knobs = getLocalBounds().reduced (10).withTrimmedTop (28 + 8 + 28 + 6 + 28 + 6).withHeight (150);
    auto left = knobs.removeFromLeft (knobs.getWidth() * 4 / 7).reduced (2);
    g.fillRoundedRectangle (left.toFloat(), 6.0f);
    g.fillRoundedRectangle (knobs.reduced (2).toFloat(), 6.0f);

    g.setColour (Colours::dim);
    g.setFont (juce::FontOptions (12.0f));
    g.drawText ("AMP ENVELOPE", left.reduced (8, 4).removeFromTop (14), juce::Justification::left);
    g.drawText ("OUTPUT / TUNING", knobs.reduced (10, 6).removeFromTop (14), juce::Justification::left);
}

void SFTunePlayerEditor::resized()
{
    auto area = getLocalBounds().reduced (10);
    auto titleRow = area.removeFromTop (28);
    titleRow.removeFromLeft (150);                       // title text
    updateButton.setBounds (titleRow.removeFromRight (110).reduced (0, 2));
    titleRow.removeFromRight (6);
    versionLabel.setBounds (titleRow.removeFromRight (260));
    noteInfoLabel.setBounds (titleRow);
    area.removeFromTop (8);

    auto row1 = area.removeFromTop (28);
    loadSoundButton.setBounds (row1.removeFromLeft (150));
    row1.removeFromLeft (8);
    presetBox.setBounds (row1.removeFromRight (row1.getWidth() / 2));
    soundLabel.setBounds (row1.withTrimmedRight (8));
    area.removeFromTop (6);

    auto row2 = area.removeFromTop (28);
    loadTuningButton.setBounds (row2.removeFromLeft (170));
    row2.removeFromLeft (6);
    resetTuningButton.setBounds (row2.removeFromLeft (64));
    row2.removeFromLeft (8);
    tuningLabel.setBounds (row2);
    area.removeFromTop (6);

    auto knobs = area.removeFromTop (150);
    auto left = knobs.removeFromLeft (knobs.getWidth() * 4 / 7).reduced (8, 6);
    auto right = knobs.reduced (8, 6);

    left.removeFromTop (14);
    fileEnvToggle.setBounds (left.removeFromBottom (22));
    right.removeFromTop (14);

    auto place = [] (juce::Rectangle<int> r, std::initializer_list<Knob*> ks)
    {
        const int w = r.getWidth() / (int) ks.size();
        for (auto* k : ks)
        {
            auto cell = r.removeFromLeft (w);
            k->label.setBounds (cell.removeFromTop (16));
            k->slider.setBounds (cell);
        }
    };
    place (left, { &attack, &decay, &sustain, &release });
    place (right, { &gain, &bend, &baseNote, &baseFreq });

    area.removeFromTop (6);
    statusLabel.setBounds (area.removeFromTop (22));
    area.removeFromTop (4);
    keyboard.setBounds (area);
}
