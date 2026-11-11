#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace ID
{
    static const juce::String useFileEnv = "useFileEnv", attack = "attack", decay = "decay", sustain = "sustain",
                              release = "release", gain = "gain", bendRange = "bendRange",
                              baseNote = "baseNote", baseFreq = "baseFreq";
}

juce::AudioProcessorValueTreeState::ParameterLayout SFTunePlayerProcessor::createLayout()
{
    using namespace juce;
    std::vector<std::unique_ptr<RangedAudioParameter>> p;

    auto timeRange = [] (float max) { NormalisableRange<float> r (0.001f, max, 0.0f); r.setSkewForCentre (0.5f); return r; };
    auto secs = AudioParameterFloatAttributes().withLabel ("s")
                    .withStringFromValueFunction ([] (float v, int) { return v < 1.0f ? String (v * 1000.0f, 1) + " ms" : String (v, 2) + " s"; });

    p.push_back (std::make_unique<AudioParameterBool>  (ParameterID { ID::useFileEnv, 1 }, "Use File Envelope", false));
    p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { ID::attack, 1 },  "Attack",  timeRange (10.0f), 0.005f, secs));
    p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { ID::decay, 1 },   "Decay",   timeRange (20.0f), 0.5f,   secs));
    p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { ID::sustain, 1 }, "Sustain", NormalisableRange<float> (0.0f, 1.0f), 0.8f,
                     AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return String (v * 100.0f, 0) + " %"; })));
    p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { ID::release, 1 }, "Release", timeRange (20.0f), 0.3f,   secs));
    p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { ID::gain, 1 },    "Gain",    NormalisableRange<float> (-48.0f, 12.0f, 0.1f), 0.0f,
                     AudioParameterFloatAttributes().withLabel ("dB").withStringFromValueFunction ([] (float v, int) { return String (v, 1) + " dB"; })));
    p.push_back (std::make_unique<AudioParameterInt>   (ParameterID { ID::bendRange, 1 }, "Pitch Bend Range", 0, 48, 2,
                     AudioParameterIntAttributes().withLabel ("st").withStringFromValueFunction ([] (int v, int) { return "+/-" + String (v) + " st"; })));
    p.push_back (std::make_unique<AudioParameterInt>   (ParameterID { ID::baseNote, 1 }, "Scale Base Note", 0, 127, 60,
                     AudioParameterIntAttributes().withStringFromValueFunction ([] (int v, int) { return MidiMessage::getMidiNoteName (v, true, true, 4) + " (" + String (v) + ")"; })));
    p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { ID::baseFreq, 1 }, "Scale Base Frequency",
                     NormalisableRange<float> (8.0f, 2000.0f, 0.001f, 0.3f), 261.6256f, AudioParameterFloatAttributes().withLabel ("Hz")));
    return { p.begin(), p.end() };
}

SFTunePlayerProcessor::SFTunePlayerProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PARAMS", createLayout())
{
    startTimerHz (10);
}

SFTunePlayerProcessor::~SFTunePlayerProcessor() { stopTimer(); }

bool SFTunePlayerProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    return out == juce::AudioChannelSet::stereo() || out == juce::AudioChannelSet::mono();
}

void SFTunePlayerProcessor::prepareToPlay (double sampleRate, int)
{
    const juce::ScopedLock sl (engineLock);
    engine.prepare (sampleRate);
}

EngineParams SFTunePlayerProcessor::readParams() const
{
    EngineParams p;
    p.useFileEnvelope = apvts.getRawParameterValue (ID::useFileEnv)->load() > 0.5f;
    p.attack  = apvts.getRawParameterValue (ID::attack)->load();
    p.decay   = apvts.getRawParameterValue (ID::decay)->load();
    p.sustain = apvts.getRawParameterValue (ID::sustain)->load();
    p.release = apvts.getRawParameterValue (ID::release)->load();
    p.gainDb  = apvts.getRawParameterValue (ID::gain)->load();
    p.bendRangeSemis = apvts.getRawParameterValue (ID::bendRange)->load();
    return p;
}

void SFTunePlayerProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    keyboardState.processNextMidiBuffer (midi, 0, buffer.getNumSamples(), true);

    const juce::ScopedTryLock tl (engineLock);
    if (! tl.isLocked()) { buffer.clear(); return; }   // a file is being swapped in

    engine.setParams (readParams());
    engine.render (buffer, midi);
    if (engine.consumePresetChangedFlag())
    {
        presetIndex = engine.getPresetIndex();
        ++uiChangeCounter;
    }
    midi.clear();
}

// ---------------------------------------------------------------------------
bool SFTunePlayerProcessor::loadSoundFile (const juce::File& f, juce::String& message)
{
    juce::String err;
    auto newBank = SoundBankLoader::load (f, err);
    if (newBank == nullptr) { message = "Load failed: " + err; return false; }

    {
        const juce::ScopedLock sl (engineLock);
        engine.setBank (newBank, 0);
        bank = newBank;
        soundFile = f;
        presetIndex = 0;
    }
    int regions = 0;
    for (auto& p : newBank->presets) regions += (int) p.regions.size();
    message = f.getFileName() + ": " + juce::String ((int) newBank->presets.size()) + " preset(s), "
            + juce::String (regions) + " region(s)" + (err.isNotEmpty() ? "  /  " + err : juce::String());
    ++uiChangeCounter;
    updateHostDisplay (ChangeDetails().withProgramChanged (true));
    return true;
}

void SFTunePlayerProcessor::selectPreset (int index)
{
    const juce::ScopedLock sl (engineLock);
    engine.setPreset (index);
    presetIndex = engine.getPresetIndex();
    ++uiChangeCounter;
}

juce::StringArray SFTunePlayerProcessor::getPresetNames() const
{
    juce::StringArray names;
    if (auto b = bank)
        for (auto& p : b->presets)
            names.add (juce::String (p.bank).paddedLeft ('0', 3) + ":" + juce::String (p.program).paddedLeft ('0', 3) + "  " + p.name);
    return names;
}

int SFTunePlayerProcessor::getNumPrograms() { return bank != nullptr ? std::max (1, (int) bank->presets.size()) : 1; }
int SFTunePlayerProcessor::getCurrentProgram() { return presetIndex.load(); }
void SFTunePlayerProcessor::setCurrentProgram (int index) { if (bank != nullptr) selectPreset (index); }
const juce::String SFTunePlayerProcessor::getProgramName (int index)
{
    if (bank != nullptr && juce::isPositiveAndBelow (index, (int) bank->presets.size())) return bank->presets[(size_t) index].name;
    return "Default";
}

// ---------------------------------------------------------------------------
void SFTunePlayerProcessor::pushTuning (const TuningTable& t)
{
    tuning = t;
    const juce::ScopedLock sl (engineLock);
    engine.setTuning (t);
}

void SFTunePlayerProcessor::rebuildScalaTuning()
{
    KeyboardMapping kbm = scalaKbm;
    if (! scalaHasKbm)
    {
        kbm = KeyboardMapping();
        kbm.middleNote = kbm.referenceNote = (int) apvts.getRawParameterValue (ID::baseNote)->load();
        kbm.referenceFreq = apvts.getRawParameterValue (ID::baseFreq)->load();
    }
    auto t = TuningLoader::fromScala (scala, kbm);
    t.name = tuning.name;
    pushTuning (t);
}

bool SFTunePlayerProcessor::loadTuningFile (const juce::File& f, juce::String& message)
{
    const auto ext = f.getFileExtension().toLowerCase();
    const auto text = f.loadFileAsString();
    juce::String err;

    if (ext == ".scl")
    {
        ScalaScale s;
        if (! s.parse (text, err)) { message = "Tuning load failed: " + err; return false; }
        scala = s;
        tuningKind = TuningKind::scala;
        tuningFile = f;
        scalaHasKbm = false;
        kbmFile = juce::File();
        auto siblingKbm = f.withFileExtension ("kbm");          // same-named .kbm is picked up automatically
        if (siblingKbm.existsAsFile() && scalaKbm.parse (siblingKbm.loadFileAsString(), err)) scalaHasKbm = true;
        tuning.name = f.getFileName() + (scalaHasKbm ? " + " + siblingKbm.getFileName() : juce::String());
        rebuildScalaTuning();
        message = "Scale: " + tuning.name + "  (" + juce::String ((int) scala.cents.size()) + " notes/period, period "
                + juce::String (scala.cents.back(), 2) + " cents)  " + scala.description;
    }
    else if (ext == ".kbm")
    {
        if (tuningKind != TuningKind::scala) { message = "Load a .scl first, then a .kbm to remap it"; return false; }
        KeyboardMapping k;
        if (! k.parse (text, err)) { message = "Mapping load failed: " + err; return false; }
        scalaKbm = k; scalaHasKbm = true; kbmFile = f;
        tuning.name = tuningFile.getFileName() + " + " + f.getFileName();
        rebuildScalaTuning();
        message = "Keyboard mapping: " + f.getFileName();
    }
    else if (ext == ".tun")
    {
        TuningTable t;
        if (! TuningLoader::loadTun (text, t, err)) { message = "Tuning load failed: " + err; return false; }
        t.name = f.getFileName();
        tuningKind = TuningKind::tun;
        tuningFile = f;
        pushTuning (t);
        message = "Tuning: " + t.name + (t.description.isNotEmpty() ? "  " + t.description : juce::String());
    }
    else { message = "Unsupported tuning file (.scl / .kbm / .tun)"; return false; }

    ++uiChangeCounter;
    return true;
}

void SFTunePlayerProcessor::resetTuning()
{
    tuningKind = TuningKind::equal;
    tuningFile = juce::File();
    kbmFile = juce::File();
    scalaHasKbm = false;
    pushTuning (TuningTable::equalTemperament());
    ++uiChangeCounter;
}

juce::String SFTunePlayerProcessor::getTuningName() const { return tuning.name; }

double SFTunePlayerProcessor::getKeyFrequency (int key) const
{
    return juce::isPositiveAndBelow (key, 128) ? tuning.freq[(size_t) key] : 0.0;
}

void SFTunePlayerProcessor::timerCallback()
{
    // Base note / frequency only matter for .scl without .kbm; recompute on the message thread.
    const int bn = (int) apvts.getRawParameterValue (ID::baseNote)->load();
    const double bf = apvts.getRawParameterValue (ID::baseFreq)->load();
    if (bn != lastBaseNote || std::abs (bf - lastBaseFreq) > 1.0e-9)
    {
        lastBaseNote = bn; lastBaseFreq = bf;
        if (tuningKind == TuningKind::scala && ! scalaHasKbm) { rebuildScalaTuning(); ++uiChangeCounter; }
    }
}

// ---------------------------------------------------------------------------
void SFTunePlayerProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    state.setProperty ("soundFile", soundFile.getFullPathName(), nullptr);
    state.setProperty ("preset", presetIndex.load(), nullptr);
    state.setProperty ("tuningFile", tuningFile.getFullPathName(), nullptr);
    state.setProperty ("kbmFile", tuningKind == TuningKind::scala ? kbmFile.getFullPathName() : juce::String(), nullptr);
    if (auto xml = state.createXml()) copyXmlToBinary (*xml, destData);
}

void SFTunePlayerProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    auto xml = getXmlFromBinary (data, sizeInBytes);
    if (xml == nullptr || ! xml->hasTagName (apvts.state.getType())) return;
    auto state = juce::ValueTree::fromXml (*xml);
    apvts.replaceState (state);

    juce::String msg;
    const juce::File sf (state.getProperty ("soundFile").toString());
    if (sf.existsAsFile() && loadSoundFile (sf, msg))
        selectPreset ((int) state.getProperty ("preset", 0));

    const juce::File tf (state.getProperty ("tuningFile").toString());
    if (tf.existsAsFile())
    {
        loadTuningFile (tf, msg);
        const juce::File kf (state.getProperty ("kbmFile").toString());
        if (kf.existsAsFile()) loadTuningFile (kf, msg);
    }
    else resetTuning();
}

juce::AudioProcessorEditor* SFTunePlayerProcessor::createEditor() { return new SFTunePlayerEditor (*this); }

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new SFTunePlayerProcessor(); }
