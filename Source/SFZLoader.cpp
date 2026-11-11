// SFZ (.sfz) loader.
// Supports <control> <global> <master> <group> <region> headers, #define / #include,
// and the commonly used opcodes for mapping, tuning, looping, amp envelope and volume.
// Samples are loaded through JUCE (WAV / AIFF / FLAC / Ogg Vorbis).

#include "SoundBank.h"
#include <cmath>
#include <map>
#include <algorithm>
#include <cctype>
#include <cstring>

namespace
{
    using Opcodes = std::map<juce::String, juce::String>;

    int parseNote (const juce::String& raw, int fallback)
    {
        auto s = raw.trim().toLowerCase();
        if (s.isEmpty()) return fallback;
        if (s.containsOnly ("-0123456789")) return s.getIntValue();

        static const int base[] = { 9, 11, 0, 2, 4, 5, 7 }; // a b c d e f g
        const auto c = s[0];
        if (c < 'a' || c > 'g') return fallback;
        int n = base[c - 'a'];
        int i = 1;
        while (i < s.length() && (s[i] == '#' || s[i] == 'b'))
        {
            n += (s[i] == '#') ? 1 : -1;
            ++i;
        }
        const int octave = s.substring (i).getIntValue();
        return juce::jlimit (0, 127, (octave + 1) * 12 + n);   // c4 = 60
    }

    // Read file, strip comments, resolve #include and #define.
    juce::String preprocess (const juce::File& file, const juce::File& rootDir,
                             std::map<juce::String, juce::String>& defines, int depth)
    {
        if (depth > 16 || ! file.existsAsFile()) return {};
        juce::StringArray lines;
        lines.addLines (file.loadFileAsString());

        juce::String out;
        bool inBlockComment = false;
        for (auto line : lines)
        {
            // block comments
            juce::String cleaned;
            for (int i = 0; i < line.length(); ++i)
            {
                if (inBlockComment)
                {
                    if (line[i] == '*' && i + 1 < line.length() && line[i + 1] == '/') { inBlockComment = false; ++i; }
                    continue;
                }
                if (line[i] == '/' && i + 1 < line.length() && line[i + 1] == '*') { inBlockComment = true; ++i; continue; }
                if (line[i] == '/' && i + 1 < line.length() && line[i + 1] == '/') break;
                cleaned << juce::String::charToString (line[i]);
            }
            line = cleaned;

            auto t = line.trim();
            if (t.startsWith ("#define"))
            {
                auto rest = t.substring (7).trim();
                auto name = rest.upToFirstOccurrenceOf (" ", false, false).trim();
                auto value = rest.fromFirstOccurrenceOf (" ", false, false).trim();
                if (name.isNotEmpty()) defines[name] = value;
                continue;
            }
            if (t.startsWith ("#include"))
            {
                auto inc = t.fromFirstOccurrenceOf ("\"", false, false).upToLastOccurrenceOf ("\"", false, false);
                inc = inc.replaceCharacter ('\\', '/');
                out << preprocess (rootDir.getChildFile (inc), rootDir, defines, depth + 1) << "\n";
                continue;
            }
            // substitute $VARS (longest names first to avoid prefix clashes)
            if (line.containsChar ('$'))
            {
                std::vector<std::pair<juce::String, juce::String>> sorted (defines.begin(), defines.end());
                std::sort (sorted.begin(), sorted.end(), [] (auto& a, auto& b) { return a.first.length() > b.first.length(); });
                for (auto& [k, v] : sorted) line = line.replace (k, v);
            }
            out << line << "\n";
        }
        return out;
    }

    struct Token { bool isHeader; juce::String name, value; };

    // Splits text into <header> and opcode=value tokens. Values may contain spaces
    // (e.g. sample paths); a value ends where the next "name=" or "<" begins.
    std::vector<Token> tokenize (const juce::String& text)
    {
        std::vector<Token> tokens;
        const auto* s = text.toRawUTF8();
        const std::string str (s);
        const size_t n = str.size();
        size_t i = 0;

        auto isNameChar = [] (char c) { return std::isalnum ((unsigned char) c) || c == '_'; };

        while (i < n)
        {
            while (i < n && std::isspace ((unsigned char) str[i])) ++i;
            if (i >= n) break;

            if (str[i] == '<')
            {
                const size_t close = str.find ('>', i);
                if (close == std::string::npos) break;
                tokens.push_back ({ true, juce::String (str.substr (i + 1, close - i - 1)).trim().toLowerCase(), {} });
                i = close + 1;
                continue;
            }

            // opcode name
            size_t nameStart = i;
            while (i < n && isNameChar (str[i])) ++i;
            if (i >= n || str[i] != '=' || i == nameStart) { ++i; continue; }
            const auto name = str.substr (nameStart, i - nameStart);
            ++i; // skip '='

            // value: up to newline, or up to the start of the next opcode / header
            size_t valStart = i, end = i;
            while (end < n && str[end] != '\n' && str[end] != '\r' && str[end] != '<')
            {
                if (std::isspace ((unsigned char) str[end]))
                {
                    size_t k = end;
                    while (k < n && (str[k] == ' ' || str[k] == '\t')) ++k;
                    size_t m = k;
                    while (m < n && isNameChar (str[m])) ++m;
                    if (m > k && m < n && str[m] == '=') break;     // next opcode begins at k
                }
                ++end;
            }
            tokens.push_back ({ false, juce::String (name).toLowerCase(), juce::String (str.substr (valStart, end - valStart)).trim() });
            i = end;
        }
        return tokens;
    }

    LoopMode parseLoopMode (const juce::String& v)
    {
        if (v == "one_shot")        return LoopMode::oneShot;
        if (v == "loop_continuous") return LoopMode::continuous;
        if (v == "loop_sustain")    return LoopMode::sustain;
        return LoopMode::none;
    }
}

std::shared_ptr<SoundBank> SoundBankLoader::loadSFZ (const juce::File& file, juce::String& error)
{
    if (! file.existsAsFile()) { error = "File not found"; return nullptr; }

    const auto rootDir = file.getParentDirectory();
    std::map<juce::String, juce::String> defines;
    const auto text = preprocess (file, rootDir, defines, 0);
    const auto tokens = tokenize (text);

    juce::AudioFormatManager formats;
    formats.registerBasicFormats();

    std::map<juce::String, std::shared_ptr<SampleData>> cache;
    struct LoopInfo { bool has = false; int64_t start = 0, end = 0; };
    std::map<juce::String, LoopInfo> loopCache;
    juce::StringArray missing;

    auto loadSample = [&] (const juce::File& f, LoopInfo& loopOut) -> std::shared_ptr<SampleData>
    {
        const auto key = f.getFullPathName();
        if (auto it = cache.find (key); it != cache.end()) { loopOut = loopCache[key]; return it->second; }

        std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (f));
        if (reader == nullptr) { cache[key] = nullptr; missing.addIfNotAlreadyThere (f.getFileName()); return nullptr; }

        auto sd = std::make_shared<SampleData>();
        sd->sampleRate = reader->sampleRate;
        sd->name = f.getFileName();
        const int nch = (int) std::min<unsigned int> (2, reader->numChannels);
        const auto len = (int) reader->lengthInSamples;
        juce::AudioBuffer<float> buf (nch, len);
        reader->read (&buf, 0, len, 0, true, nch > 1);
        sd->channels.resize ((size_t) nch);
        for (int c = 0; c < nch; ++c)
            sd->channels[(size_t) c].assign (buf.getReadPointer (c), buf.getReadPointer (c) + len);

        LoopInfo li;
        const auto& meta = reader->metadataValues;
        if (meta.getValue ("NumSampleLoops", "0").getIntValue() > 0)
        {
            li.has = true;
            li.start = meta.getValue ("Loop0Start", "0").getLargeIntValue();
            li.end   = meta.getValue ("Loop0End", "0").getLargeIntValue() + 1; // WAV loop end is inclusive
        }
        cache[key] = sd;
        loopCache[key] = li;
        loopOut = li;
        return sd;
    };

    Opcodes control, global, master, group, region;
    enum class Level { none, inControl, inGlobal, inMaster, inGroup, inRegion } level = Level::none;

    Preset preset;
    preset.name = file.getFileNameWithoutExtension();

    auto flushRegion = [&]
    {
        Opcodes o;
        for (auto* m : { &global, &master, &group, &region })
            for (auto& [k, v] : *m) o[k] = v;

        auto get = [&] (const char* k, const juce::String& def = {}) { auto it = o.find (k); return it != o.end() ? it->second : def; };
        auto has = [&] (const char* k) { return o.count (k) > 0; };

        auto samplePath = get ("sample");
        if (samplePath.isEmpty() || samplePath.startsWithChar ('*')) return;   // generators like *sine are not supported

        auto defaultPath = control.count ("default_path") ? control["default_path"] : juce::String();
        auto rel = (defaultPath + samplePath).replaceCharacter ('\\', '/');
        auto sampleFile = juce::File::isAbsolutePath (rel) ? juce::File (rel) : rootDir.getChildFile (rel);

        LoopInfo li;
        auto sd = loadSample (sampleFile, li);
        if (sd == nullptr) return;

        const int noteOffset = (control.count ("note_offset") ? control["note_offset"].getIntValue() : 0)
                             + 12 * (control.count ("octave_offset") ? control["octave_offset"].getIntValue() : 0);

        Region reg;
        reg.sample = sd;

        if (has ("key"))
        {
            const int k = parseNote (get ("key"), 60);
            reg.loKey = reg.hiKey = k;
            reg.rootKey = (float) k;
        }
        reg.loKey = parseNote (get ("lokey"), reg.loKey);
        reg.hiKey = parseNote (get ("hikey"), reg.hiKey);
        reg.loKey = juce::jlimit (0, 127, reg.loKey + noteOffset);
        reg.hiKey = juce::jlimit (0, 127, reg.hiKey + noteOffset);
        reg.rootKey = (float) parseNote (get ("pitch_keycenter"), (int) reg.rootKey) + (float) noteOffset;
        reg.loVel = juce::jlimit (0, 127, get ("lovel", "0").getIntValue());
        reg.hiVel = juce::jlimit (0, 127, get ("hivel", "127").getIntValue());

        reg.tuneCents = get ("tune", get ("pitch", "0")).getFloatValue() + 100.0f * get ("transpose", "0").getFloatValue();
        reg.keyTrack  = get ("pitch_keytrack", "100").getFloatValue() / 100.0f;

        const float volDb = get ("volume", "0").getFloatValue();
        const float amp = get ("amplitude", "100").getFloatValue() / 100.0f;
        reg.gain = juce::Decibels::decibelsToGain (volDb, -144.0f) * amp;
        reg.pan  = juce::jlimit (-1.0f, 1.0f, get ("pan", "0").getFloatValue() / 100.0f);

        const int64_t len = sd->length();
        reg.start = juce::jlimit<int64_t> (0, len, get ("offset", "0").getLargeIntValue());
        reg.end   = has ("end") ? juce::jlimit<int64_t> (0, len, get ("end").getLargeIntValue() + 1) : len;
        if (reg.end <= reg.start) return;

        reg.loopStart = li.has ? li.start : 0;
        reg.loopEnd   = li.has ? li.end   : reg.end;
        if (has ("loop_start") || has ("loopstart")) reg.loopStart = get ("loop_start", get ("loopstart")).getLargeIntValue();
        if (has ("loop_end")   || has ("loopend"))   reg.loopEnd   = get ("loop_end",   get ("loopend")).getLargeIntValue() + 1;
        reg.loopStart = juce::jlimit<int64_t> (0, len, reg.loopStart);
        reg.loopEnd   = juce::jlimit<int64_t> (0, len, reg.loopEnd);

        if (has ("loop_mode") || has ("loopmode")) reg.loopMode = parseLoopMode (get ("loop_mode", get ("loopmode")));
        else reg.loopMode = li.has ? LoopMode::continuous : LoopMode::none;
        if ((reg.loopMode == LoopMode::continuous || reg.loopMode == LoopMode::sustain) && reg.loopEnd - reg.loopStart < 2)
            reg.loopMode = LoopMode::none;

        reg.trigger = get ("trigger", "attack") == "release" ? Trigger::release : Trigger::attack;

        reg.env.delay   = get ("ampeg_delay",   "0").getFloatValue();
        reg.env.attack  = get ("ampeg_attack",  "0").getFloatValue();
        reg.env.hold    = get ("ampeg_hold",    "0").getFloatValue();
        reg.env.decay   = get ("ampeg_decay",   "0").getFloatValue();
        reg.env.sustain = juce::jlimit (0.0f, 1.0f, get ("ampeg_sustain", "100").getFloatValue() / 100.0f);
        reg.env.release = get ("ampeg_release", "0.001").getFloatValue();

        reg.exclusiveClass = get ("group", "0").getIntValue();
        reg.offBy          = get ("off_by", "0").getIntValue();

        preset.regions.push_back (std::move (reg));
    };

    for (const auto& t : tokens)
    {
        if (t.isHeader)
        {
            if (level == Level::inRegion) flushRegion();
            if      (t.name == "control") { level = Level::inControl; control.clear(); }
            else if (t.name == "global")  { level = Level::inGlobal;  global.clear(); master.clear(); group.clear(); }
            else if (t.name == "master")  { level = Level::inMaster;  master.clear(); group.clear(); }
            else if (t.name == "group")   { level = Level::inGroup;   group.clear(); }
            else if (t.name == "region")  { level = Level::inRegion;  region.clear(); }
            else                            level = Level::none;   // <curve>, <effect>, <midi>... ignored
            continue;
        }
        switch (level)
        {
            case Level::inControl: control[t.name] = t.value; break;
            case Level::inGlobal:  global[t.name]  = t.value; break;
            case Level::inMaster:  master[t.name]  = t.value; break;
            case Level::inGroup:   group[t.name]   = t.value; break;
            case Level::inRegion:  region[t.name]  = t.value; break;
            case Level::none: break;
        }
    }
    if (level == Level::inRegion) flushRegion();

    if (preset.regions.empty())
    {
        error = "No playable regions";
        if (! missing.isEmpty()) error << " (could not load: " << missing.joinIntoString (", ") << ")";
        return nullptr;
    }
    if (! missing.isEmpty())
        error = "Some samples could not be loaded: " + missing.joinIntoString (", ");

    auto bank = std::make_shared<SoundBank>();
    bank->sourceName = file.getFileName();
    bank->presets.push_back (std::move (preset));
    return bank;
}
