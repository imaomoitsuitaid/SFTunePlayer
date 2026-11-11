// Offline engine test: loads soundfonts + tunings, renders notes and measures the pitch.
#include "SynthEngine.h"
#include "UpdateChecker.h"
#include <cmath>
#include <cstdio>

static int failures = 0;

static double measureFreq (const juce::AudioBuffer<float>& b, double sr, int from, int to)
{
    const float* x = b.getReadPointer (0);
    double first = -1, last = -1; int crossings = 0;
    for (int i = from + 1; i < to; ++i)
        if (x[i - 1] < 0.0f && x[i] >= 0.0f)
        {
            const double t = (i - 1) + x[i - 1] / (x[i - 1] - x[i]);
            if (first < 0) first = t; else ++crossings;
            last = t;
        }
    return crossings > 0 ? crossings * sr / (last - first) : 0.0;
}

static float rms (const juce::AudioBuffer<float>& b, int from, int to)
{
    double s = 0; const float* x = b.getReadPointer (0);
    for (int i = from; i < to; ++i) s += x[i] * x[i];
    return (float) std::sqrt (s / std::max (1, to - from));
}

static juce::AudioBuffer<float> play (SynthEngine& e, int note, double sr, double holdSec, double totalSec, int vel = 100)
{
    const int total = (int) (sr * totalSec), block = 512;
    juce::AudioBuffer<float> out (2, total), tmp (2, block);
    for (int pos = 0; pos < total; pos += block)
    {
        const int n = std::min (block, total - pos);
        tmp.setSize (2, n, false, false, true);
        juce::MidiBuffer m;
        if (pos == 0) m.addEvent (juce::MidiMessage::noteOn (1, note, (juce::uint8) vel), 0);
        const int offAt = (int) (holdSec * sr);
        if (offAt >= pos && offAt < pos + n) m.addEvent (juce::MidiMessage::noteOff (1, note), offAt - pos);
        e.render (tmp, m);
        for (int c = 0; c < 2; ++c) out.copyFrom (c, pos, tmp, c, 0, n);
    }
    return out;
}

static void check (const char* what, double got, double expect, double tolCents = 1.0)
{
    const double cents = (got > 0 && expect > 0) ? 1200.0 * std::log2 (got / expect) : 9999.0;
    const bool ok = std::abs (cents) <= tolCents;
    if (! ok) ++failures;
    std::printf ("%s %-46s expected %10.4f Hz  got %10.4f Hz  (%+.3f c)\n", ok ? "[OK]  " : "[FAIL]", what, expect, got, cents);
}

static void checkTrue (const char* what, bool cond, const juce::String& detail = {})
{
    if (! cond) ++failures;
    std::printf ("%s %s %s\n", cond ? "[OK]  " : "[FAIL]", what, detail.toRawUTF8());
}

int main (int argc, char** argv)
{
    const juce::File dir (argc > 1 ? juce::String (argv[1]) : juce::File::getCurrentWorkingDirectory().getFullPathName());
    const double sr = 48000.0;

    // ---------------- SF2 ----------------
    juce::String err;
    auto sf2 = SoundBankLoader::load (dir.getChildFile ("sine.sf2"), err);
    checkTrue ("SF2 loads", sf2 != nullptr, err);
    if (sf2 == nullptr) return 1;
    checkTrue ("SF2 has 2 presets", sf2->presets.size() == 2, juce::String ((int) sf2->presets.size()));

    SynthEngine e;
    e.prepare (sr);
    EngineParams p; p.attack = 0.001f; p.decay = 0.01f; p.sustain = 1.0f; p.release = 0.05f;
    e.setParams (p);
    e.setBank (sf2, 0);

    auto measureNote = [&] (int note) { auto b = play (e, note, sr, 0.9, 1.0); return measureFreq (b, sr, (int) (0.2 * sr), (int) (0.8 * sr)); };

    check ("SF2 12-TET A4 (69)", measureNote (69), 440.0);
    check ("SF2 12-TET C4 (60)", measureNote (60), 261.6256);
    check ("SF2 12-TET A5 (81)", measureNote (81), 880.0);
    e.setPreset (1);
    check ("SF2 preset-level coarseTune +12 on A4", measureNote (69), 880.0);
    e.setPreset (0);

    // loop: still sounding after the sample's natural length (1 s of data, render 1.5 s held)
    {
        auto b = play (e, 69, sr, 1.45, 1.5);
        checkTrue ("SF2 loop sustains past sample end", rms (b, (int) (1.2 * sr), (int) (1.4 * sr)) > 0.05f);
    }
    // ADSR: release brings it to silence; sustain level honoured
    {
        EngineParams q = p; q.sustain = 0.25f; q.decay = 0.05f; q.release = 0.1f;
        e.setParams (q);
        auto b = play (e, 69, sr, 0.5, 1.2);
        const float sus = rms (b, (int) (0.3 * sr), (int) (0.45 * sr));
        const float peak = rms (b, 0, (int) (0.003 * sr) + 1);
        auto bFull = (e.setParams (p), play (e, 69, sr, 0.5, 0.6));
        const float full = rms (bFull, (int) (0.3 * sr), (int) (0.45 * sr));
        checkTrue ("ADSR sustain 25% ~ 0.25x amplitude", std::abs (sus / full - 0.25f) < 0.02f, juce::String (sus / full, 3));
        checkTrue ("ADSR release -> silence", rms (b, (int) (1.0 * sr), (int) (1.2 * sr)) < 1e-4f, juce::String (rms (b, (int) (1.0 * sr), (int) (1.2 * sr))));
        (void) peak;
        checkTrue ("voices freed after release", e.activeVoiceCount() == 0, juce::String (e.activeVoiceCount()));
    }

    // ---------------- Tunings ----------------
    {
        ScalaScale s; checkTrue ("19edo.scl parses", s.parse (dir.getChildFile ("19edo.scl").loadFileAsString(), err), err);
        KeyboardMapping k; k.middleNote = k.referenceNote = 60; k.referenceFreq = 261.6255653;
        auto t = TuningLoader::fromScala (s, k);
        e.setTuning (t);
        check ("19-EDO key 60 (base)", measureNote (60), 261.6255653);
        check ("19-EDO key 61 (+1 step = 63.16c)", measureNote (61), 261.6255653 * std::pow (2.0, 1.0 / 19.0));
        check ("19-EDO key 79 (+19 steps = octave)", measureNote (79), 523.2511306);
        check ("19-EDO key 50 (-10 steps)", measureNote (50), 261.6255653 * std::pow (2.0, -10.0 / 19.0));

        k.middleNote = k.referenceNote = 69; k.referenceFreq = 432.0;     // base note / freq change
        e.setTuning (TuningLoader::fromScala (s, k));
        check ("19-EDO base A4=432, key 70", measureNote (70), 432.0 * std::pow (2.0, 1.0 / 19.0));
    }
    {
        ScalaScale s; s.parse (dir.getChildFile ("just.scl").loadFileAsString(), err);
        KeyboardMapping k; checkTrue ("whitekeys.kbm parses", k.parse (dir.getChildFile ("whitekeys.kbm").loadFileAsString(), err), err);
        auto t = TuningLoader::fromScala (s, k);
        e.setTuning (t);
        check ("Just+kbm A4 = reference 440", measureNote (69), 440.0);
        check ("Just+kbm C4 = 440 * 3/5", measureNote (60), 264.0);
        check ("Just+kbm E4 = 264 * 5/4", measureNote (64), 330.0);
        check ("Just+kbm C5 = 528", measureNote (72), 528.0);
        checkTrue ("Just+kbm C#4 unmapped -> silent", ! t.isMapped (61) && rms (play (e, 61, sr, 0.3, 0.4), 0, (int) (0.3 * sr)) == 0.0f);
    }
    {
        TuningTable t; checkTrue ("plus25.tun parses", TuningLoader::loadTun (dir.getChildFile ("plus25.tun").loadFileAsString(), t, err), err);
        e.setTuning (t);
        check ("TUN key 69 = 440 * 2^(25/1200)", measureNote (69), 440.0 * std::pow (2.0, 25.0 / 1200.0));
        check ("TUN [Exact Tuning] key 60 = 6013.6863c", measureNote (60), 8.1757989156 * std::pow (2.0, 6013.6863 / 1200.0));
    }
    e.setTuning (TuningTable::equalTemperament());

    // ---------------- SFZ ----------------
    auto sfz = SoundBankLoader::load (dir.getChildFile ("test.sfz"), err);
    checkTrue ("SFZ loads (default_path, #define, spaces in path, note names)", sfz != nullptr, err);
    if (sfz != nullptr)
    {
        checkTrue ("SFZ has 2 regions", sfz->presets[0].regions.size() == 2, juce::String ((int) sfz->presets[0].regions.size()));
        e.setBank (sfz, 0);
        const double base = 220.5;  // sample's real pitch, mapped to key 57
        check ("SFZ key 57 (keycenter)", measureNote (57), base);
        check ("SFZ key 62 (+5 st)", measureNote (62), base * std::pow (2.0, 5.0 / 12.0));
        check ("SFZ key 69 (region 2, tune +50c)", measureNote (69), base * std::pow (2.0, (12 + 0.5) / 12.0));
        auto b = play (e, 57, sr, 1.4, 1.5);
        checkTrue ("SFZ loop_continuous sustains", rms (b, (int) (1.1 * sr), (int) (1.35 * sr)) > 0.05f);

        // file envelope from SFZ (ampeg_release=0.2)
        EngineParams q = p; q.useFileEnvelope = true; e.setParams (q);
        auto r = play (e, 57, sr, 0.3, 1.0);
        checkTrue ("SFZ file envelope: still audible 50ms after note-off", rms (r, (int) (0.34 * sr), (int) (0.36 * sr)) > 0.01f);
        checkTrue ("SFZ file envelope: silent after release", rms (r, (int) (0.8 * sr), (int) (1.0 * sr)) < 1e-4f);

        // microtonal on SFZ
        ScalaScale s; s.parse (dir.getChildFile ("19edo.scl").loadFileAsString(), err);
        KeyboardMapping k;
        e.setParams (p);
        e.setTuning (TuningLoader::fromScala (s, k));
        check ("SFZ + 19-EDO key 61", measureNote (61), base * std::pow (2.0, (60 - 57) / 12.0) * std::pow (2.0, 1.0 / 19.0));
    }

    // ---------------- Updater ----------------
    checkTrue ("isNewer 1.0.1 > 1.0.0",   UpdateUtils::isNewer ("1.0.1", "1.0.0"));
    checkTrue ("isNewer v1.2.10 > 1.2.9", UpdateUtils::isNewer ("v1.2.10", "1.2.9"));
    checkTrue ("isNewer 2.0 > 1.9.9",     UpdateUtils::isNewer ("2.0", "1.9.9"));
    checkTrue ("not newer: same version", ! UpdateUtils::isNewer ("v1.0.0", "1.0.0"));
    checkTrue ("not newer: older",        ! UpdateUtils::isNewer ("1.0.0", "1.0.1"));
    {
        const char* json = R"({"tag_name":"v1.0.3","html_url":"https://github.com/x/y/releases/tag/v1.0.3","body":"fix",
            "assets":[{"name":"SFTunePlayer-macOS.zip","browser_download_url":"https://e/mac.zip"},
                      {"name":"SFTunePlayer-Setup.exe","browser_download_url":"https://e/setup.exe"}]})";
        ReleaseInfo r;
        const bool ok = UpdateUtils::parseLatestRelease (json, r);
        checkTrue ("parse release JSON (version, installer asset)", ok && r.version == "1.0.3" && r.installerUrl == "https://e/setup.exe",
                   r.version + " " + r.installerUrl);
        checkTrue ("reject non-release JSON", ! UpdateUtils::parseLatestRelease (R"({"message":"Not Found"})", r));
    }

    std::printf ("\n%s (%d failure%s)\n", failures == 0 ? "ALL TESTS PASSED" : "TESTS FAILED", failures, failures == 1 ? "" : "s");
    return failures == 0 ? 0 : 1;
}
