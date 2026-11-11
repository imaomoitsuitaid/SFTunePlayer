#include "SynthEngine.h"
#include <cmath>

void SynthEngine::prepare (double sr)
{
    sampleRate = sr > 0 ? sr : 44100.0;
    allNotesOff (true);
}

void SynthEngine::setBank (std::shared_ptr<SoundBank> newBank, int index)
{
    allNotesOff (true);     // voices hold raw Region pointers: kill them before swapping
    bank = std::move (newBank);
    presetIndex = 0;
    setPreset (index);
}

void SynthEngine::setPreset (int index)
{
    if (bank == nullptr || bank->presets.empty()) { presetIndex = 0; return; }
    const int clamped = juce::jlimit (0, (int) bank->presets.size() - 1, index);
    if (clamped != presetIndex) allNotesOff (true);
    presetIndex = clamped;
}

void SynthEngine::setTuning (const TuningTable& t)
{
    tuning = t;
    for (auto& v : voices)
        if (v.active && tuning.isMapped (v.note))
            v.baseSemis = (tuning.semitones (v.note) - v.region->rootKey) * v.region->keyTrack + v.region->tuneCents / 100.0;
}

void SynthEngine::allNotesOff (bool immediate)
{
    for (auto& v : voices)
    {
        if (! v.active) continue;
        if (immediate) { v.active = false; v.region = nullptr; }
        else enterRelease (v);
    }
    sustainPedal = false;
}

int SynthEngine::activeVoiceCount() const
{
    int n = 0;
    for (auto& v : voices) n += v.active ? 1 : 0;
    return n;
}

// ---------------------------------------------------------------------------
void SynthEngine::handleMidi (const juce::MidiMessage& m)
{
    const int ch = m.getChannel();
    if (m.isNoteOn())                 noteOn (ch, m.getNoteNumber(), m.getVelocity());
    else if (m.isNoteOff())           noteOff (ch, m.getNoteNumber());
    else if (m.isPitchWheel())        pitchBend = (float) (m.getPitchWheelValue() - 8192) / 8192.0f;
    else if (m.isAllNotesOff() || m.isAllSoundOff()) allNotesOff (m.isAllSoundOff());
    else if (m.isController())
    {
        const int cc = m.getControllerNumber(), val = m.getControllerValue();
        switch (cc)
        {
            case 0:  bankMsb = val; break;
            case 32: bankLsb = val; break;
            case 7:  cc7  = (float) val / 127.0f; break;
            case 11: cc11 = (float) val / 127.0f; break;
            case 64:
            {
                const bool down = val >= 64;
                if (sustainPedal && ! down)
                    for (auto& v : voices)
                        if (v.active && v.sustainedByPedal) { v.sustainedByPedal = false; enterRelease (v); }
                sustainPedal = down;
                break;
            }
            case 121: pitchBend = 0.0f; cc7 = cc11 = 1.0f; break;   // reset all controllers
            default: break;
        }
    }
    else if (m.isProgramChange() && bank != nullptr)
    {
        const int prog = m.getProgramChangeNumber();
        const int wantBank = bankMsb * 128 + bankLsb;
        int found = -1;
        for (int i = 0; i < (int) bank->presets.size(); ++i)
        {
            auto& p = bank->presets[(size_t) i];
            if (p.program == prog && (p.bank == wantBank || p.bank == bankMsb)) { found = i; break; }
        }
        if (found < 0)
            for (int i = 0; i < (int) bank->presets.size(); ++i)
                if (bank->presets[(size_t) i].program == prog) { found = i; break; }
        if (found >= 0 && found != presetIndex) { setPreset (found); presetChangedByMidi = true; }
    }
}

void SynthEngine::noteOn (int ch, int note, int vel)
{
    if (bank == nullptr || bank->presets.empty() || ! tuning.isMapped (note)) return;
    lastVelocity[(size_t) note] = vel;
    const auto& preset = bank->presets[(size_t) presetIndex];

    // exclusive classes / off_by: silence voices cut by any region about to start
    for (const auto& r : preset.regions)
    {
        if (r.trigger != Trigger::attack || note < r.loKey || note > r.hiKey || vel < r.loVel || vel > r.hiVel) continue;
        if (r.exclusiveClass == 0) continue;
        for (auto& v : voices)
            if (v.active && v.region->offBy == r.exclusiveClass && v.stage != Voice::release)
                enterRelease (v, true);
    }

    // retrigger of the same key: release the old voice so it doesn't pile up
    for (auto& v : voices)
        if (v.active && v.note == note && v.channel == ch && ! v.isReleaseTrigger && v.stage != Voice::release)
            enterRelease (v, true);

    for (const auto& r : preset.regions)
        if (r.trigger == Trigger::attack && note >= r.loKey && note <= r.hiKey && vel >= r.loVel && vel <= r.hiVel)
            startVoice (r, ch, note, vel, false);
}

void SynthEngine::noteOff (int ch, int note)
{
    for (auto& v : voices)
    {
        if (! v.active || v.note != note || v.channel != ch || ! v.keyDown || v.isReleaseTrigger) continue;
        v.keyDown = false;
        if (sustainPedal) v.sustainedByPedal = true;
        else enterRelease (v);
    }

    if (bank == nullptr || bank->presets.empty() || ! tuning.isMapped (note)) return;
    const int vel = lastVelocity[(size_t) note];
    for (const auto& r : bank->presets[(size_t) presetIndex].regions)
        if (r.trigger == Trigger::release && note >= r.loKey && note <= r.hiKey && vel >= r.loVel && vel <= r.hiVel)
            startVoice (r, ch, note, vel, true);
}

void SynthEngine::startVoice (const Region& r, int ch, int note, int vel, bool releaseTrig)
{
    if (r.sample == nullptr || r.sample->length() == 0) return;

    // find a free voice, otherwise steal the quietest releasing / oldest voice
    Voice* slot = nullptr;
    for (auto& v : voices) if (! v.active) { slot = &v; break; }
    if (slot == nullptr)
    {
        for (auto& v : voices)
            if (v.stage == Voice::release && (slot == nullptr || v.level < slot->level)) slot = &v;
        if (slot == nullptr)
            for (auto& v : voices)
                if (slot == nullptr || v.startOrder < slot->startOrder) slot = &v;
    }

    Voice& v = *slot;
    v = Voice();
    v.active = true;
    v.region = &r;
    v.note = note; v.velocity = vel; v.channel = ch;
    v.keyDown = ! releaseTrig;
    v.isReleaseTrigger = releaseTrig;
    v.pos = (double) r.start;
    v.startOrder = ++voiceCounter;
    v.baseSemis = (tuning.semitones (note) - r.rootKey) * r.keyTrack + r.tuneCents / 100.0;

    const float velGain = std::pow ((float) vel / 127.0f, 2.0f);
    const float g = r.gain * velGain;
    const float angle = (r.pan + 1.0f) * juce::MathConstants<float>::pi * 0.25f;   // constant power
    v.gainL = g * std::cos (angle) * juce::MathConstants<float>::sqrt2;
    v.gainR = g * std::sin (angle) * juce::MathConstants<float>::sqrt2;

    if (params.useFileEnvelope)
        v.env = r.env;
    else
    {
        v.env = EnvelopeSpec();
        v.env.attack = params.attack; v.env.decay = params.decay;
        v.env.sustain = params.sustain; v.env.release = params.release;
        v.env.delay = 0.0f; v.env.hold = 0.0f;
    }
    // decay reaches the sustain level with exponential shape (about -80 dB of distance in 'decay' seconds)
    v.decayCoef = v.env.decay > 0.0005f ? (float) std::exp (std::log (1.0e-4) / (v.env.decay * sampleRate)) : 0.0f;
    v.stage = v.env.delay > 0.0f ? Voice::delay : Voice::attack;
    v.level = 0.0f;
    v.stageTime = 0.0f;
}

void SynthEngine::enterRelease (Voice& v, bool fast)
{
    if (v.stage == Voice::release && ! fast) return;
    if (v.region != nullptr && v.region->loopMode == LoopMode::oneShot && ! fast) return;
    const float relTime = fast ? 0.005f : std::max (0.001f, v.env.release);
    v.releaseCoef = (float) std::exp (std::log (1.0e-4) / (relTime * sampleRate));
    v.fastRelease = fast;
    if (v.stage == Voice::delay) { v.stage = Voice::done; return; }
    v.stage = Voice::release;
}

double SynthEngine::pitchIncrement (const Voice& v) const
{
    const double semis = v.baseSemis + pitchBend * params.bendRangeSemis;
    return std::pow (2.0, semis / 12.0) * v.region->sample->sampleRate / sampleRate;
}

float SynthEngine::readSample (const Voice& v, int ch, int64_t i) const
{
    const auto& r = *v.region;
    const auto& data = r.sample->channels[(size_t) ch];
    const bool looping = r.loopMode == LoopMode::continuous || (r.loopMode == LoopMode::sustain && v.stage != Voice::release);
    if (looping && i >= r.loopEnd)
        i = r.loopStart + (i - r.loopStart) % (r.loopEnd - r.loopStart);
    if (i < 0 || i >= r.end) return 0.0f;
    return data[(size_t) i];
}

void SynthEngine::renderVoice (Voice& v, float* L, float* R, int n)
{
    const auto& r = *v.region;
    const int nch = r.sample->numChannels();
    const double inc = pitchIncrement (v);
    const float dt = 1.0f / (float) sampleRate;
    const float master = juce::Decibels::decibelsToGain (params.gainDb) * cc7 * cc11;

    for (int s = 0; s < n; ++s)
    {
        // ---- envelope ----
        switch (v.stage)
        {
            case Voice::delay:
                v.stageTime += dt;
                if (v.stageTime >= v.env.delay) { v.stage = Voice::attack; v.stageTime = 0.0f; }
                break;
            case Voice::attack:
                if (v.env.attack <= 0.0005f) v.level = 1.0f;
                else v.level += dt / v.env.attack;
                if (v.level >= 1.0f) { v.level = 1.0f; v.stage = v.env.hold > 0.0f ? Voice::hold : Voice::decay; v.stageTime = 0.0f; }
                break;
            case Voice::hold:
                v.stageTime += dt;
                if (v.stageTime >= v.env.hold) { v.stage = Voice::decay; v.stageTime = 0.0f; }
                break;
            case Voice::decay:
                v.level = v.env.sustain + (v.level - v.env.sustain) * v.decayCoef;
                if (std::abs (v.level - v.env.sustain) < 1.0e-4f) { v.level = v.env.sustain; v.stage = Voice::sustain; }
                break;
            case Voice::sustain:
                v.level = v.env.sustain;
                if (v.level <= 0.0f && r.loopMode != LoopMode::oneShot) v.stage = Voice::done;
                break;
            case Voice::release:
                v.level *= v.releaseCoef;
                if (v.level < 1.0e-4f) v.stage = Voice::done;
                break;
            case Voice::done: break;
        }
        if (v.stage == Voice::done) { v.active = false; v.region = nullptr; return; }

        // ---- sample playback (4-point Hermite interpolation) ----
        const bool looping = r.loopMode == LoopMode::continuous || (r.loopMode == LoopMode::sustain && v.stage != Voice::release);
        if (looping && v.pos >= (double) r.loopEnd)
            v.pos = r.loopStart + std::fmod (v.pos - r.loopStart, (double) (r.loopEnd - r.loopStart));
        if (! looping && v.pos >= (double) r.end) { v.active = false; v.region = nullptr; return; }

        const auto i = (int64_t) v.pos;
        const float f = (float) (v.pos - (double) i);
        float out[2] = { 0.0f, 0.0f };
        for (int c = 0; c < nch; ++c)
        {
            const float xm1 = readSample (v, c, i - 1), x0 = readSample (v, c, i),
                        x1  = readSample (v, c, i + 1), x2 = readSample (v, c, i + 2);
            const float c1 = 0.5f * (x1 - xm1);
            const float c2 = xm1 - 2.5f * x0 + 2.0f * x1 - 0.5f * x2;
            const float c3 = 0.5f * (x2 - xm1) + 1.5f * (x0 - x1);
            out[c] = ((c3 * f + c2) * f + c1) * f + x0;
        }
        if (nch == 1) out[1] = out[0];

        const float amp = v.level * master;
        L[s] += out[0] * v.gainL * amp;
        R[s] += out[1] * v.gainR * amp;

        v.pos += inc;
    }
}

void SynthEngine::render (juce::AudioBuffer<float>& out, const juce::MidiBuffer& midi)
{
    const int total = out.getNumSamples();
    out.clear();
    if (out.getNumChannels() == 0) return;
    float* L = out.getWritePointer (0);
    float* R = out.getNumChannels() > 1 ? out.getWritePointer (1) : nullptr;
    juce::HeapBlock<float> tmpR;
    if (R == nullptr) { tmpR.calloc ((size_t) total); R = tmpR.get(); }

    int pos = 0;
    auto renderUpTo = [&] (int end)
    {
        if (end <= pos) return;
        for (auto& v : voices)
            if (v.active) renderVoice (v, L + pos, R + pos, end - pos);
        pos = end;
    };

    for (const auto meta : midi)
    {
        renderUpTo (juce::jlimit (0, total, meta.samplePosition));
        handleMidi (meta.getMessage());
    }
    renderUpTo (total);

    if (out.getNumChannels() == 1)
        for (int i = 0; i < total; ++i) L[i] = 0.5f * (L[i] + R[i]);
}
