#include "Tuning.h"
#include <cmath>

static constexpr double kMidiZeroHz = 8.1757989156437073; // MIDI key 0 at A4=440

TuningTable TuningTable::equalTemperament (double a4)
{
    TuningTable t;
    for (int k = 0; k < 128; ++k)
        t.freq[(size_t) k] = a4 * std::pow (2.0, (k - 69) / 12.0);
    t.name = "12-TET";
    return t;
}

double TuningTable::semitones (int key) const
{
    return 12.0 * std::log2 (freq[(size_t) key] / kMidiZeroHz);
}

// ---------------------------------------------------------------------------
static juce::StringArray nonCommentLines (const juce::String& text)
{
    juce::StringArray lines, out;
    lines.addLines (text);
    for (auto& l : lines)
        if (! l.trimStart().startsWithChar ('!'))
            out.add (l);
    return out;
}

bool ScalaScale::parse (const juce::String& text, juce::String& error)
{
    auto lines = nonCommentLines (text);
    if (lines.size() < 2) { error = "Invalid .scl file"; return false; }

    description = lines[0].trim();
    int idx = 1;
    while (idx < lines.size() && lines[idx].trim().isEmpty()) ++idx;
    if (idx >= lines.size()) { error = "Invalid .scl: missing note count"; return false; }
    const int count = lines[idx].trim().getIntValue();
    ++idx;

    cents.clear();
    for (; idx < lines.size() && (int) cents.size() < count; ++idx)
    {
        auto t = lines[idx].trim();
        if (t.isEmpty()) continue;
        t = t.upToFirstOccurrenceOf (" ", false, false).upToFirstOccurrenceOf ("\t", false, false);

        double c;
        if (t.containsChar ('.'))
            c = t.getDoubleValue();
        else if (t.containsChar ('/'))
        {
            const double num = t.upToFirstOccurrenceOf ("/", false, false).getDoubleValue();
            const double den = t.fromFirstOccurrenceOf ("/", false, false).getDoubleValue();
            if (num <= 0 || den <= 0) { error = "Invalid ratio in .scl: " + t; return false; }
            c = 1200.0 * std::log2 (num / den);
        }
        else
        {
            const double v = t.getDoubleValue();
            if (v <= 0) { error = "Invalid value in .scl: " + t; return false; }
            c = 1200.0 * std::log2 (v);
        }
        cents.push_back (c);
    }

    if (count <= 0 || (int) cents.size() != count) { error = "Invalid .scl: expected " + juce::String (count) + " pitches"; return false; }
    return true;
}

bool KeyboardMapping::parse (const juce::String& text, juce::String& error)
{
    juce::StringArray vals;
    for (auto& l : nonCommentLines (text))
        if (l.trim().isNotEmpty()) vals.add (l.trim());

    if (vals.size() < 7) { error = "Invalid .kbm file"; return false; }
    mapSize       = vals[0].getIntValue();
    firstNote     = vals[1].getIntValue();
    lastNote      = vals[2].getIntValue();
    middleNote    = vals[3].getIntValue();
    referenceNote = vals[4].getIntValue();
    referenceFreq = vals[5].getDoubleValue();
    octaveDegree  = vals[6].getIntValue();
    mapping.clear();
    for (int i = 0; i < mapSize; ++i)
    {
        const auto v = (7 + i < vals.size()) ? vals[7 + i] : juce::String ("x");
        mapping.push_back (v.startsWithIgnoreCase ("x") ? -1 : v.getIntValue());
    }
    return true;
}

static int floorDiv (int a, int b) { return (a >= 0) ? a / b : -((-a + b - 1) / b); }

TuningTable TuningLoader::fromScala (const ScalaScale& scl, const KeyboardMapping& kbm)
{
    TuningTable t;
    const int N = (int) scl.cents.size();
    const double period = scl.cents.back();

    auto degreeCents = [&] (int degree)   // any integer degree, relative to degree 0
    {
        const int oct = floorDiv (degree, N);
        const int d = degree - oct * N;
        return oct * period + (d == 0 ? 0.0 : scl.cents[(size_t) d - 1]);
    };

    // cents of each key relative to the middle note (NaN if unmapped)
    std::array<double, 128> rel {};
    for (int k = 0; k < 128; ++k)
    {
        rel[(size_t) k] = std::nan ("");
        if (k < kbm.firstNote || k > kbm.lastNote) continue;

        const int offset = k - kbm.middleNote;
        if (kbm.mapSize <= 0)
        {
            rel[(size_t) k] = degreeCents (offset);
        }
        else
        {
            const int mapOct = floorDiv (offset, kbm.mapSize);
            const int idx = offset - mapOct * kbm.mapSize;
            const int deg = kbm.mapping[(size_t) idx];
            if (deg < 0) continue;
            const int octDeg = kbm.octaveDegree > 0 ? kbm.octaveDegree : N;
            rel[(size_t) k] = degreeCents (deg) + mapOct * degreeCents (octDeg);
        }
    }

    // Choose the scale so that referenceNote sounds at referenceFreq.
    double refRel = 0.0;
    const int rn = juce::jlimit (0, 127, kbm.referenceNote);
    if (! std::isnan (rel[(size_t) rn])) refRel = rel[(size_t) rn];
    else
    {
        // reference key itself unmapped: compute its position linearly from scale degrees
        refRel = degreeCents (rn - kbm.middleNote);
    }

    for (int k = 0; k < 128; ++k)
        t.freq[(size_t) k] = std::isnan (rel[(size_t) k]) ? 0.0
                           : kbm.referenceFreq * std::pow (2.0, (rel[(size_t) k] - refRel) / 1200.0);

    t.description = scl.description;
    return t;
}

// AnaMark TUN: [Tuning] / [Exact Tuning] sections, "note N = cents" relative to BaseFreq (default 8.1758 Hz)
bool TuningLoader::loadTun (const juce::String& text, TuningTable& out, juce::String& error)
{
    juce::StringArray lines;
    lines.addLines (text);

    std::array<double, 128> cents;
    for (int k = 0; k < 128; ++k) cents[(size_t) k] = 100.0 * k;   // default 12-TET
    std::array<bool, 128> exactSet {};
    double baseFreq = kMidiZeroHz;
    juce::String section;
    bool any = false;

    for (auto l : lines)
    {
        l = l.upToFirstOccurrenceOf (";", false, false).trim();
        if (l.isEmpty()) continue;
        if (l.startsWithChar ('['))
        {
            section = l.removeCharacters ("[]").trim().toLowerCase();
            continue;
        }
        if (! l.containsChar ('=')) continue;
        const auto key = l.upToFirstOccurrenceOf ("=", false, false).trim().toLowerCase();
        const auto val = l.fromFirstOccurrenceOf ("=", false, false).trim().unquoted();

        if (section == "tuning" || section == "exact tuning")
        {
            if (key.startsWith ("note"))
            {
                const int n = key.substring (4).trim().getIntValue();
                if (n < 0 || n > 127) continue;
                if (section == "tuning" && exactSet[(size_t) n]) continue;  // exact has priority
                cents[(size_t) n] = val.getDoubleValue();
                if (section == "exact tuning") exactSet[(size_t) n] = true;
                any = true;
            }
            else if (key == "basefreq")
                baseFreq = val.getDoubleValue();
        }
        else if (section == "info" && key == "name")
            out.description = val;
    }

    if (! any) { error = "No [Tuning] / [Exact Tuning] note entries found"; return false; }
    if (baseFreq <= 0) baseFreq = kMidiZeroHz;
    for (int k = 0; k < 128; ++k)
        out.freq[(size_t) k] = baseFreq * std::pow (2.0, cents[(size_t) k] / 1200.0);
    return true;
}

bool TuningLoader::loadFile (const juce::File& file, int baseNote, double baseFreq, TuningTable& out, juce::String& error)
{
    const auto ext = file.getFileExtension().toLowerCase();
    const auto text = file.loadFileAsString();
    if (text.isEmpty()) { error = "Cannot read file"; return false; }

    if (ext == ".tun")
    {
        TuningTable t;
        if (! loadTun (text, t, error)) return false;
        t.name = file.getFileName();
        out = t;
        return true;
    }
    if (ext == ".scl")
    {
        ScalaScale scl;
        if (! scl.parse (text, error)) return false;

        KeyboardMapping kbm;
        kbm.middleNote = kbm.referenceNote = juce::jlimit (0, 127, baseNote);
        kbm.referenceFreq = baseFreq;

        // Use a .kbm with the same base name next to the .scl if one exists
        auto kbmFile = file.withFileExtension ("kbm");
        juce::String kbmUsed;
        if (kbmFile.existsAsFile())
        {
            KeyboardMapping k2; juce::String e2;
            if (k2.parse (kbmFile.loadFileAsString(), e2)) { kbm = k2; kbmUsed = " + " + kbmFile.getFileName(); }
        }

        out = fromScala (scl, kbm);
        out.name = file.getFileName() + kbmUsed;
        return true;
    }
    error = "Unsupported tuning file (use .scl or .tun)";
    return false;
}
