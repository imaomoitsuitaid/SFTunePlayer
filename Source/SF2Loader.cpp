// SoundFont 2 (.sf2) loader.
// Reads RIFF 'sfbk', converts the presets' zone hierarchy (preset -> instrument -> sample)
// into flat Regions. Supports 16-bit samples and the optional 24-bit 'sm24' chunk.

#include "SoundBank.h"
#include <cmath>
#include <map>
#include <algorithm>
#include <cctype>
#include <cstring>

namespace
{
    enum Gen
    {
        startAddrsOffset = 0, endAddrsOffset = 1, startloopAddrsOffset = 2, endloopAddrsOffset = 3,
        startAddrsCoarseOffset = 4, endAddrsCoarseOffset = 12, pan = 17,
        delayVolEnv = 33, attackVolEnv = 34, holdVolEnv = 35, decayVolEnv = 36, sustainVolEnv = 37, releaseVolEnv = 38,
        instrument = 41, keyRange = 43, velRange = 44, startloopAddrsCoarseOffset = 45, keynum = 46, velocity = 47,
        initialAttenuation = 48, endloopAddrsCoarseOffset = 50, coarseTune = 51, fineTune = 52, sampleID = 53,
        sampleModes = 54, scaleTuning = 56, exclusiveClass = 57, overridingRootKey = 58, numGens = 61
    };

    struct GenSet
    {
        int16_t value[numGens] {};
        bool    set[numGens]   {};

        void apply (const GenSet& other)
        {
            for (int i = 0; i < numGens; ++i)
                if (other.set[i]) { value[i] = other.value[i]; set[i] = true; }
        }
        int lo (int g) const { return set[g] ? (int) (uint8_t) (value[g] & 0xff) : 0; }
        int hi (int g) const { return set[g] ? (int) (uint8_t) ((value[g] >> 8) & 0xff) : 127; }
    };

    struct Reader
    {
        const uint8_t* p; size_t size;
        uint16_t u16 (size_t o) const { return (uint16_t) (p[o] | (p[o + 1] << 8)); }
        int16_t  s16 (size_t o) const { return (int16_t) u16 (o); }
        uint32_t u32 (size_t o) const { return (uint32_t) (p[o] | (p[o + 1] << 8) | (p[o + 2] << 16) | ((uint32_t) p[o + 3] << 24)); }
        juce::String name20 (size_t o) const
        {
            size_t n = 0; while (n < 20 && p[o + n] != 0) ++n;
            return juce::String::fromUTF8 ((const char*) p + o, (int) n).trim();
        }
        bool tag (size_t o, const char* t) const { return std::memcmp (p + o, t, 4) == 0; }
    };

    struct Chunk { size_t offset = 0, size = 0; bool found = false; };

    float timecentsToSeconds (int tc)
    {
        if (tc <= -12000) return 0.0f;
        return (float) std::pow (2.0, tc / 1200.0);
    }
}

std::shared_ptr<SoundBank> SoundBankLoader::loadSF2 (const juce::File& file, juce::String& error)
{
    juce::MemoryBlock mb;
    if (! file.loadFileAsData (mb)) { error = "Cannot read file"; return nullptr; }

    Reader r { (const uint8_t*) mb.getData(), mb.getSize() };
    if (r.size < 12 || ! r.tag (0, "RIFF") || ! r.tag (8, "sfbk")) { error = "Not a SoundFont 2 file"; return nullptr; }

    std::map<std::string, Chunk> chunks;

    // walk top-level LISTs
    const size_t riffEnd = std::min ((size_t) r.u32 (4) + 8, r.size);
    for (size_t pos = 12; pos + 8 <= riffEnd;)
    {
        const uint32_t sz = r.u32 (pos + 4);
        if (r.tag (pos, "LIST") && pos + 12 <= riffEnd)
        {
            const size_t listEnd = std::min (pos + 8 + sz, riffEnd);
            for (size_t sub = pos + 12; sub + 8 <= listEnd;)
            {
                const uint32_t ssz = r.u32 (sub + 4);
                Chunk c { sub + 8, std::min ((size_t) ssz, listEnd - (sub + 8)), true };
                chunks[std::string ((const char*) r.p + sub, 4)] = c;
                sub += 8 + ssz + (ssz & 1);
            }
        }
        pos += 8 + sz + (sz & 1);
    }

    for (auto* need : { "smpl", "phdr", "pbag", "pgen", "inst", "ibag", "igen", "shdr" })
        if (! chunks[need].found) { error = juce::String ("Missing chunk: ") + need; return nullptr; }

    const Chunk smpl = chunks["smpl"], sm24 = chunks["sm24"];
    const Chunk phdr = chunks["phdr"], pbag = chunks["pbag"], pgen = chunks["pgen"];
    const Chunk inst = chunks["inst"], ibag = chunks["ibag"], igen = chunks["igen"], shdr = chunks["shdr"];

    const size_t totalSamples = smpl.size / 2;
    const bool has24 = sm24.found && sm24.size >= totalSamples;

    auto readGens = [&] (const Chunk& genChunk, size_t first, size_t last, GenSet& out)
    {
        const size_t count = genChunk.size / 4;
        for (size_t g = first; g < last && g < count; ++g)
        {
            const size_t o = genChunk.offset + g * 4;
            const uint16_t oper = r.u16 (o);
            if (oper < numGens) { out.value[oper] = r.s16 (o + 2); out.set[oper] = true; }
        }
    };

    // ---- samples (sliced lazily, cached) ----
    const size_t numShdr = shdr.size / 46;
    std::map<int, std::shared_ptr<SampleData>> sampleCache;

    struct SHdr { uint32_t start, end, loopStart, loopEnd, rate; int origPitch, pitchCorr, type; juce::String name; };
    auto getShdr = [&] (int idx)
    {
        const size_t o = shdr.offset + (size_t) idx * 46;
        return SHdr { r.u32 (o + 20), r.u32 (o + 24), r.u32 (o + 28), r.u32 (o + 32), r.u32 (o + 36),
                      (int) r.p[o + 40], (int) (int8_t) r.p[o + 41], (int) r.u16 (o + 44), r.name20 (o) };
    };

    auto getSample = [&] (int idx) -> std::shared_ptr<SampleData>
    {
        if (auto it = sampleCache.find (idx); it != sampleCache.end()) return it->second;
        auto h = getShdr (idx);
        auto sd = std::make_shared<SampleData>();
        sd->sampleRate = h.rate > 0 ? (double) h.rate : 44100.0;
        sd->name = h.name;
        // Slice the whole sample-pool range around this header with a little slack for offsets
        const size_t s = std::min ((size_t) h.start, totalSamples);
        const size_t e = std::min ((size_t) h.end, totalSamples);
        sd->channels.resize (1);
        auto& ch = sd->channels[0];
        ch.resize (e > s ? e - s : 0);
        for (size_t i = 0; i < ch.size(); ++i)
        {
            int32_t v = (int32_t) r.s16 (smpl.offset + (s + i) * 2) << 8;
            if (has24) v |= r.p[sm24.offset + s + i];
            ch[i] = (float) v / 8388608.0f;
        }
        sampleCache[idx] = sd;
        return sd;
    };

    auto bank = std::make_shared<SoundBank>();
    bank->sourceName = file.getFileName();

    const size_t numPresets = phdr.size / 38;
    const size_t numInst = inst.size / 22;
    const size_t numPbag = pbag.size / 4, numIbag = ibag.size / 4;

    for (size_t pi = 0; pi + 1 < numPresets; ++pi) // last record is EOP terminator
    {
        const size_t po = phdr.offset + pi * 38;
        Preset preset;
        preset.name    = r.name20 (po);
        preset.program = r.u16 (po + 20);
        preset.bank    = r.u16 (po + 22);
        const size_t bagStart = r.u16 (po + 24), bagEnd = r.u16 (po + 38 + 24);

        GenSet presetGlobal;
        for (size_t b = bagStart; b < bagEnd && b + 1 < numPbag; ++b)
        {
            const size_t g0 = r.u16 (pbag.offset + b * 4), g1 = r.u16 (pbag.offset + (b + 1) * 4);
            GenSet pz; readGens (pgen, g0, g1, pz);

            if (! pz.set[instrument])
            {
                if (b == bagStart) presetGlobal = pz;   // global preset zone
                continue;
            }
            GenSet pzone = presetGlobal; pzone.apply (pz);

            const int instIdx = (int) (uint16_t) pzone.value[instrument];
            if (instIdx < 0 || (size_t) instIdx + 1 >= numInst) continue;

            const size_t io = inst.offset + (size_t) instIdx * 22;
            const size_t ibStart = r.u16 (io + 20), ibEnd = r.u16 (io + 22 + 20);

            GenSet instGlobal;
            for (size_t ib = ibStart; ib < ibEnd && ib + 1 < numIbag; ++ib)
            {
                const size_t ig0 = r.u16 (ibag.offset + ib * 4), ig1 = r.u16 (ibag.offset + (ib + 1) * 4);
                GenSet iz; readGens (igen, ig0, ig1, iz);
                if (! iz.set[sampleID])
                {
                    if (ib == ibStart) instGlobal = iz;
                    continue;
                }

                GenSet z;
                // defaults
                z.value[scaleTuning] = 100; z.set[scaleTuning] = true;
                for (int g : { delayVolEnv, attackVolEnv, holdVolEnv, decayVolEnv, releaseVolEnv }) { z.value[g] = -12000; }
                z.value[overridingRootKey] = -1; z.value[keynum] = -1; z.value[velocity] = -1;
                z.apply (instGlobal);
                z.apply (iz);

                const int sIdx = (int) (uint16_t) z.value[sampleID];
                if (sIdx < 0 || (size_t) sIdx + 1 >= numShdr) continue;  // last header is EOS
                const auto h = getShdr (sIdx);
                if (h.type & 0x8000) continue;  // ROM sample, not available

                // preset-level generators are additive (except a few)
                auto P = [&] (int g) -> int { return pzone.set[g] ? pzone.value[g] : 0; };
                auto I = [&] (int g) -> int { return z.value[g]; };

                Region reg;
                reg.sample = getSample (sIdx);

                reg.loKey = std::max (z.lo (keyRange), pzone.lo (keyRange));
                reg.hiKey = std::min (z.hi (keyRange), pzone.hi (keyRange));
                reg.loVel = std::max (z.lo (velRange), pzone.lo (velRange));
                reg.hiVel = std::min (z.hi (velRange), pzone.hi (velRange));
                if (reg.loKey > reg.hiKey || reg.loVel > reg.hiVel) continue;

                // addresses relative to the sliced sample (which starts at h.start)
                const int64_t len = reg.sample->length();
                auto clampA = [len] (int64_t v) { return juce::jlimit<int64_t> (0, len, v); };
                reg.start     = clampA (I (startAddrsOffset)     + 32768LL * I (startAddrsCoarseOffset));
                reg.end       = clampA (len + I (endAddrsOffset) + 32768LL * I (endAddrsCoarseOffset));
                reg.loopStart = clampA ((int64_t) h.loopStart - h.start + I (startloopAddrsOffset) + 32768LL * I (startloopAddrsCoarseOffset));
                reg.loopEnd   = clampA ((int64_t) h.loopEnd   - h.start + I (endloopAddrsOffset)   + 32768LL * I (endloopAddrsCoarseOffset));
                if (reg.end <= reg.start) continue;

                switch (I (sampleModes) & 3)
                {
                    case 1:  reg.loopMode = LoopMode::continuous; break;
                    case 3:  reg.loopMode = LoopMode::sustain; break;
                    default: reg.loopMode = LoopMode::none; break;
                }
                if (reg.loopMode != LoopMode::none && reg.loopEnd - reg.loopStart < 2) reg.loopMode = LoopMode::none;

                reg.rootKey = (float) (I (overridingRootKey) >= 0 ? I (overridingRootKey) : (h.origPitch <= 127 ? h.origPitch : 60));
                if (I (keynum) >= 0) { reg.loKey = reg.hiKey = I (keynum); }

                reg.tuneCents = (float) ((I (coarseTune) + P (coarseTune)) * 100 + I (fineTune) + P (fineTune) + h.pitchCorr);
                reg.keyTrack  = (float) (I (scaleTuning) + P (scaleTuning)) / 100.0f;

                const int atten = juce::jlimit (0, 1440, I (initialAttenuation) + P (initialAttenuation));
                // Most players (EMU/FluidSynth) apply attenuation at 0.4x of the nominal cB value
                reg.gain = (float) std::pow (10.0, -(atten * 0.4) / 200.0);
                reg.pan  = juce::jlimit (-1.0f, 1.0f, (float) (I (pan) + P (pan)) / 500.0f);

                reg.env.delay   = timecentsToSeconds (I (delayVolEnv)   + P (delayVolEnv));
                reg.env.attack  = timecentsToSeconds (I (attackVolEnv)  + P (attackVolEnv));
                reg.env.hold    = timecentsToSeconds (I (holdVolEnv)    + P (holdVolEnv));
                reg.env.decay   = timecentsToSeconds (I (decayVolEnv)   + P (decayVolEnv));
                reg.env.release = timecentsToSeconds (I (releaseVolEnv) + P (releaseVolEnv));
                const int sus = juce::jlimit (0, 1440, I (sustainVolEnv) + P (sustainVolEnv));
                reg.env.sustain = (float) std::pow (10.0, -sus / 200.0);

                reg.exclusiveClass = I (exclusiveClass);
                reg.offBy = reg.exclusiveClass;

                preset.regions.push_back (std::move (reg));
            }
        }

        if (! preset.regions.empty())
            bank->presets.push_back (std::move (preset));
    }

    std::sort (bank->presets.begin(), bank->presets.end(), [] (const Preset& a, const Preset& b)
    {
        return a.bank != b.bank ? a.bank < b.bank : a.program < b.program;
    });

    if (bank->presets.empty()) { error = "No playable presets found"; return nullptr; }
    return bank;
}

std::shared_ptr<SoundBank> SoundBankLoader::load (const juce::File& file, juce::String& error)
{
    const auto ext = file.getFileExtension().toLowerCase();
    if (ext == ".sf2") return loadSF2 (file, error);
    if (ext == ".sfz") return loadSFZ (file, error);
    error = "Unsupported file type (use .sf2 or .sfz)";
    return nullptr;
}
