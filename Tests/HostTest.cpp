// Loads the built VST3 like a DAW would, restores a state that references files, plays a note.
#include <juce_audio_processors/juce_audio_processors.h>
#include <cmath>
#include <cstdio>

int main (int argc, char** argv)
{
    if (argc < 3) { std::printf ("usage: HostTest plugin.vst3 assetsDir\n"); return 2; }
    juce::ScopedJuceInitialiser_GUI init;
    juce::AudioPluginFormatManager fm;
    fm.addFormat (new juce::VST3PluginFormat());

    juce::OwnedArray<juce::PluginDescription> descs;
    juce::VST3PluginFormat vst3;
    vst3.findAllTypesForFile (descs, argv[1]);
    if (descs.isEmpty()) { std::printf ("[FAIL] plugin not found\n"); return 1; }
    std::printf ("[OK]   found: %s  (instrument=%d)\n", descs[0]->name.toRawUTF8(), (int) descs[0]->isInstrument);

    juce::String err;
    const double sr = 48000.0; const int block = 256;
    auto plugin = fm.createPluginInstance (*descs[0], sr, block, err);
    if (plugin == nullptr) { std::printf ("[FAIL] instantiate: %s\n", err.toRawUTF8()); return 1; }
    std::printf ("[OK]   instantiated, %d parameters\n", plugin->getParameters().size());
    for (auto* p : plugin->getParameters()) if (p->getParameterIndex() < 9) std::printf ("         - %s = %s\n", p->getName (40).toRawUTF8(), p->getCurrentValueAsText().toRawUTF8());

    const juce::File dir (argv[2]);
    juce::XmlElement xml ("PARAMS");
    xml.setAttribute ("soundFile", dir.getChildFile ("sine.sf2").getFullPathName());
    xml.setAttribute ("preset", 0);
    xml.setAttribute ("tuningFile", dir.getChildFile ("19edo.scl").getFullPathName());
    for (auto [id, v] : { std::pair<const char*, double> { "attack", 0.001 }, { "sustain", 1.0 }, { "baseNote", 60 }, { "baseFreq", 261.6255653 } })
    {
        auto* c = xml.createNewChildElement ("PARAM"); c->setAttribute ("id", id); c->setAttribute ("value", v);
    }
    juce::MemoryBlock state;
    juce::AudioProcessor::copyXmlToBinary (xml, state);
    // The JUCE VST3 host wraps the plugin's component state in <VST3PluginState><IComponent>base64</IComponent>
    juce::XmlElement wrapper ("VST3PluginState");
    wrapper.createNewChildElement ("IComponent")->addTextElement (state.toBase64Encoding());
    juce::MemoryBlock hostState;
    juce::AudioProcessor::copyXmlToBinary (wrapper, hostState);
    plugin->setStateInformation (hostState.getData(), (int) hostState.getSize());

    plugin->setPlayConfigDetails (0, 2, sr, block);
    plugin->prepareToPlay (sr, block);

    const int total = (int) sr;
    std::vector<float> out ((size_t) total);
    juce::AudioBuffer<float> buf (2, block);
    for (int pos = 0; pos < total; pos += block)
    {
        juce::MidiBuffer m;
        if (pos == 0) m.addEvent (juce::MidiMessage::noteOn (1, 61, (juce::uint8) 100), 0);
        buf.clear();
        plugin->processBlock (buf, m);
        for (int i = 0; i < block && pos + i < total; ++i) out[(size_t) (pos + i)] = buf.getSample (0, i);
    }
    double first = -1, last = -1; int n = 0;
    for (int i = (int) (0.2 * sr); i < (int) (0.9 * sr); ++i)
        if (out[(size_t) i - 1] < 0 && out[(size_t) i] >= 0)
        { double t = i - 1 + out[(size_t) i - 1] / (out[(size_t) i - 1] - out[(size_t) i]); if (first < 0) first = t; else ++n; last = t; }
    const double f = n > 0 ? n * sr / (last - first) : 0, expect = 261.6255653 * std::pow (2.0, 1.0 / 19.0);
    const double cents = f > 0 ? 1200 * std::log2 (f / expect) : 9999;
    std::printf ("%s key 61 through VST3 with 19edo.scl: %.4f Hz (expected %.4f, %+.3f c)\n", std::abs (cents) < 1 ? "[OK]  " : "[FAIL]", f, expect, cents);

    juce::MemoryBlock saved;
    plugin->getStateInformation (saved);
    std::unique_ptr<juce::XmlElement> back;
    if (auto w = juce::AudioProcessor::getXmlFromBinary (saved.getData(), (int) saved.getSize()))
        if (auto* comp = w->getChildByName ("IComponent"))
        {
            juce::MemoryBlock mb; mb.fromBase64Encoding (comp->getAllSubText());
            back = juce::AudioProcessor::getXmlFromBinary (mb.getData(), (int) mb.getSize());
        }
    const bool ok = back != nullptr && back->getStringAttribute ("tuningFile").endsWith ("19edo.scl") && back->getStringAttribute ("soundFile").endsWith ("sine.sf2");
    std::printf ("%s state round-trip keeps soundFile + tuningFile\n", ok ? "[OK]  " : "[FAIL]");

    if (argc > 3)
        if (auto* ed = plugin->createEditorIfNeeded())
        {
            auto img = ed->createComponentSnapshot (ed->getLocalBounds(), true, 1.0f);
            juce::File (argv[3]).deleteFile();
            juce::FileOutputStream os { juce::File (argv[3]) };
            juce::PNGImageFormat().writeImageToStream (img, os);
            std::printf ("[OK]   editor snapshot %dx%d -> %s\n", img.getWidth(), img.getHeight(), argv[3]);
            delete ed;
        }
    plugin->releaseResources();
    plugin.reset();
    return (std::abs (cents) < 1 && ok) ? 0 : 1;
}
