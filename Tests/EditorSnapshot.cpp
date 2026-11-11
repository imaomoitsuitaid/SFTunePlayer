#include <juce_audio_processors/juce_audio_processors.h>
#include <cstdio>
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter();
int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI init;
    std::unique_ptr<juce::AudioProcessor> p (createPluginFilter());
    p->setPlayConfigDetails (0, 2, 48000, 256);
    p->prepareToPlay (48000, 256);
    auto* proc = p.get();
    // load test files via the same entry point the UI uses
    juce::String msg;
    auto load = [&] (const char* method, const juce::File& f)
    {
        (void) method;
        juce::MemoryBlock st; juce::XmlElement x ("PARAMS");
        x.setAttribute ("soundFile", f.getChildFile ("sine.sf2").getFullPathName());
        x.setAttribute ("tuningFile", f.getChildFile ("19edo.scl").getFullPathName());
        juce::AudioProcessor::copyXmlToBinary (x, st);
        proc->setStateInformation (st.getData(), (int) st.getSize());
    };
    load ("state", juce::File (argv[1]));
    std::unique_ptr<juce::AudioProcessorEditor> ed (p->createEditor());
    auto img = ed->createComponentSnapshot (ed->getLocalBounds(), true, 1.0f);
    juce::File out (argv[2]); out.deleteFile();
    juce::FileOutputStream os (out);
    juce::PNGImageFormat().writeImageToStream (img, os);
    std::printf ("snapshot %dx%d\n", img.getWidth(), img.getHeight());
    ed.reset();
    return 0;
}
