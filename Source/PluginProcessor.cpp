#include "PluginProcessor.h"
#include "PluginEditor.h"

ClassicStudioGProcessor::ClassicStudioGProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "STATE", csg::createLayout())
{
    std::array<int, csg::kNumModules> o {};
    for (int i = 0; i < csg::kNumModules; ++i) o[(size_t) i] = i;
    packedOrder.store (packOrder (o));

    for (auto* p : getParameters())
        if (auto* withId = dynamic_cast<juce::AudioProcessorParameterWithID*> (p))
            raw[withId->paramID.toStdString()] = apvts.getRawParameterValue (withId->paramID);
}

float ClassicStudioGProcessor::param (const char* id) const
{
    auto it = raw.find (id);
    return it != raw.end() ? it->second->load() : 0.0f;
}

// ---------------------------------------------------------------- order
uint64_t ClassicStudioGProcessor::packOrder (const std::array<int, csg::kNumModules>& o)
{
    uint64_t v = 0;
    for (int i = 0; i < csg::kNumModules; ++i) v |= (uint64_t) (o[(size_t) i] & 15) << (4 * i);
    return v;
}

std::array<int, csg::kNumModules> ClassicStudioGProcessor::unpackOrder (uint64_t v)
{
    std::array<int, csg::kNumModules> o {};
    for (int i = 0; i < csg::kNumModules; ++i) o[(size_t) i] = (int) ((v >> (4 * i)) & 15);
    return o;
}

int ClassicStudioGProcessor::moduleAtSlot (int slot) const
{
    return (int) ((packedOrder.load() >> (4 * juce::jlimit (0, csg::kNumModules - 1, slot))) & 15);
}

void ClassicStudioGProcessor::moveModule (int fromSlot, int toSlot)
{
    fromSlot = juce::jlimit (0, csg::kNumModules - 1, fromSlot);
    toSlot   = juce::jlimit (0, csg::kNumModules - 1, toSlot);
    if (fromSlot == toSlot) return;
    auto o = unpackOrder (packedOrder.load());
    const int m = o[(size_t) fromSlot];
    std::vector<int> v (o.begin(), o.end());
    v.erase (v.begin() + fromSlot);
    v.insert (v.begin() + toSlot, m);
    for (int i = 0; i < csg::kNumModules; ++i) o[(size_t) i] = v[(size_t) i];
    packedOrder.store (packOrder (o));
}

// ---------------------------------------------------------------- audio
bool ClassicStudioGProcessor::isBusesLayoutSupported (const BusesLayout& l) const
{
    const auto in = l.getMainInputChannelSet(), out = l.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::stereo() && out != juce::AudioChannelSet::mono()) return false;
    if (in  != juce::AudioChannelSet::stereo() && in  != juce::AudioChannelSet::mono()) return false;
    if (out == juce::AudioChannelSet::mono() && in != juce::AudioChannelSet::mono()) return false;
    return true;   // mono to mono, mono to stereo, stereo to stereo
}

void ClassicStudioGProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    sr = sampleRate;
    wl.assign ((size_t) std::max (samplesPerBlock, 1), 0.0f);
    wr.assign ((size_t) std::max (samplesPerBlock, 1), 0.0f);
    inGain = {}; outGain = {};
    inFilter.prepare (sr); deEsser.prepare (sr); fet.prepare (sr); opto.prepare (sr); eq.prepare (sr);
    sat[0].prepare (sr, csg::SaturatorDsp::Tube);
    sat[1].prepare (sr, csg::SaturatorDsp::Tape);
    sat[2].prepare (sr, csg::SaturatorDsp::Transistor);
    rev[0].prepare (sr, csg::ReverbFx::Plate);
    rev[1].prepare (sr, csg::ReverbFx::Hall);
    rev[2].prepare (sr, csg::ReverbFx::Room);
    dly[0].prepare (sr, csg::DelayFx::Slap);
    dly[1].prepare (sr, csg::DelayFx::Tempo);
    width.prepare (sr);
}

void ClassicStudioGProcessor::updateMeter (std::atomic<float>& level, std::atomic<bool>* clip,
                                           const float* L, const float* R, int n)
{
    float pk = 0.0f;
    for (int i = 0; i < n; ++i) pk = std::max (pk, std::max (std::fabs (L[i]), std::fabs (R[i])));
    const float decay = std::pow (0.5f, (float) n / (float) (sr * 0.15));
    level.store (std::max (pk, level.load() * decay));
    if (clip != nullptr && pk >= 1.0f) clip->store (true);
}

void ClassicStudioGProcessor::runModule (int id, float* L, float* R, int n, double bpm)
{
    using namespace csg;

    // Power switch off: the module is skipped. Reverbs and delays are flushed so old tails do not come back later.
    if (param (kOnIds[id]) < 0.5f)
    {
        switch (id)
        {
            case mPlate: rev[0].process (L, R, n, 0, 0, 0, 0); break;
            case mHall:  rev[1].process (L, R, n, 0, 0, 0, 0); break;
            case mRoom:  rev[2].process (L, R, n, 0, 0, 0, 0); break;
            case mSlap:  dly[0].process (L, R, n, 0, 0, 0, 0, 0.11f); break;
            case mDelay: dly[1].process (L, R, n, 0, 0, 0, 0, 0.5f); break;
            case m1176:  grFet.store (0.0f);   break;
            case mLA2A:  grOpto.store (0.0f);  break;
            case mDeEsser: grDeEss.store (0.0f); break;
            default: break;
        }
        return;
    }

    switch (id)
    {
        case mInputFilter:
            inFilter.process (L, R, n, inFilterLowHz (param ("if_low")), inFilterHighHz (param ("if_high")));
            break;
        case mDeEsser:
        {
            updateMeter (dsInLevel, nullptr, L, R, n);
            const float g = deEsser.process (L, R, n, deEssHz (param ("ds_freq")), param ("ds_str"));
            grDeEss.store (g);
            updateMeter (dsOutLevel, nullptr, L, R, n);
            break;
        }
        case m1176:
        {
            updateMeter (fetInLevel, nullptr, L, R, n);
            const float g = fet.process (L, R, n, param ("c_in"), param ("c_out"), param ("c_att"), param ("c_rel"),
                                         (int) param ("c_ratio"));
            grFet.store (g);
            updateMeter (fetOutLevel, nullptr, L, R, n);
            break;
        }
        case mLA2A:
        {
            updateMeter (optoInLevel, nullptr, L, R, n);
            const float g = opto.process (L, R, n, param ("la_gain"), param ("la_peak"), param ("la_mode") > 0.5f);
            grOpto.store (g);
            updateMeter (optoOutLevel, nullptr, L, R, n);
            break;
        }
        case mEq:
        {
            float g[10];
            for (int i = 0; i < 10; ++i) g[i] = param (kEqIds[i]);
            eq.process (L, R, n, g, kEqHz);
            break;
        }
        case mTube:       sat[0].process (L, R, n, param ("tube_amt"), satHpHz (param ("tube_hp")), satLpHz (param ("tube_lp"))); break;
        case mTape:       sat[1].process (L, R, n, param ("tape_amt"), satHpHz (param ("tape_hp")), satLpHz (param ("tape_lp"))); break;
        case mTransistor: sat[2].process (L, R, n, param ("trn_amt"),  satHpHz (param ("trn_hp")),  satLpHz (param ("trn_lp")));  break;
        case mPlate: rev[0].process (L, R, n, param ("plate_lvl"), param ("plate_duck"), fxLowHz (param ("plate_lo")), fxHighHz (param ("plate_hi"))); break;
        case mHall:  rev[1].process (L, R, n, param ("hall_lvl"),  param ("hall_duck"),  fxLowHz (param ("hall_lo")),  fxHighHz (param ("hall_hi")),
                                     preDelayMs (param ("hall_pre")));  break;
        case mRoom:  rev[2].process (L, R, n, param ("room_lvl"),  param ("room_duck"),  fxLowHz (param ("room_lo")),  fxHighHz (param ("room_hi")));  break;
        case mSlap:  dly[0].process (L, R, n, param ("slap_lvl"), param ("slap_duck"), fxLowHz (param ("slap_lo")), fxHighHz (param ("slap_hi")), 0.110f); break;
        case mDelay: dly[1].process (L, R, n, param ("dly_lvl"),  param ("dly_duck"),  fxLowHz (param ("dly_lo")),  fxHighHz (param ("dly_hi")),
                                     (float) (60.0 / std::max (bpm, 20.0)) * kDivBeats[juce::jlimit (0, 10, (int) param ("dly_div"))]); break;   // synced to host tempo
        case mWidth: width.process (L, R, n, param ("w_haas"), param ("w_det"), param ("w_ms")); break;
        default: break;
    }
}

void ClassicStudioGProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();
    const int inCh = getTotalNumInputChannels(), outCh = getTotalNumOutputChannels();
    if (n <= 0 || inCh < 1 || outCh < 1) return;

    if ((int) wl.size() < n) { wl.assign ((size_t) n, 0.0f); wr.assign ((size_t) n, 0.0f); }

    std::copy (buffer.getReadPointer (0), buffer.getReadPointer (0) + n, wl.begin());
    std::copy (buffer.getReadPointer (inCh > 1 ? 1 : 0), buffer.getReadPointer (inCh > 1 ? 1 : 0) + n, wr.begin());

    double bpm = 120.0;
    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
            if (auto b = pos->getBpm())
                bpm = *b;

    // Input level knob sits at the top left of the window, before the chain
    inGain.process (wl.data(), wr.data(), n, csg::dbToGain (csg::trimDb (param ("in_gain"))));
    updateMeter (inLevel, &inClip, wl.data(), wr.data(), n);

    const auto order = unpackOrder (packedOrder.load());
    for (int slot = 0; slot < csg::kNumModules; ++slot)
        runModule (order[(size_t) slot], wl.data(), wr.data(), n, bpm);

    // Output level knob sits at the top right of the window, after the chain
    outGain.process (wl.data(), wr.data(), n, csg::dbToGain (csg::trimDb (param ("out_gain"))));
    updateMeter (outLevel, &outClip, wl.data(), wr.data(), n);

    if (outCh > 1)
    {
        std::copy (wl.begin(), wl.begin() + n, buffer.getWritePointer (0));
        std::copy (wr.begin(), wr.begin() + n, buffer.getWritePointer (1));
    }
    else
    {
        float* o = buffer.getWritePointer (0);
        for (int i = 0; i < n; ++i) o[i] = 0.5f * (wl[(size_t) i] + wr[(size_t) i]);
    }
}

// ---------------------------------------------------------------- state
void ClassicStudioGProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    auto state = apvts.copyState();
    juce::String s;
    const auto o = unpackOrder (packedOrder.load());
    for (int i = 0; i < csg::kNumModules; ++i) s << (i ? "," : "") << o[(size_t) i];
    state.setProperty ("chainOrder", s, nullptr);
    if (auto xml = state.createXml()) copyXmlToBinary (*xml, dest);
}

void ClassicStudioGProcessor::setStateInformation (const void* data, int size)
{
    if (auto xml = getXmlFromBinary (data, size))
    {
        auto tree = juce::ValueTree::fromXml (*xml);
        if (! tree.isValid()) return;

        const auto parts = juce::StringArray::fromTokens (tree.getProperty ("chainOrder").toString(), ",", "");
        std::array<int, csg::kNumModules> o {};
        bool ok = parts.size() == csg::kNumModules;
        std::array<bool, csg::kNumModules> seen {};
        for (int i = 0; ok && i < csg::kNumModules; ++i)
        {
            const int m = parts[i].getIntValue();
            if (m < 0 || m >= csg::kNumModules || seen[(size_t) m]) ok = false; else { seen[(size_t) m] = true; o[(size_t) i] = m; }
        }
        if (ok) packedOrder.store (packOrder (o));

        if (tree.hasType (apvts.state.getType()))
            apvts.replaceState (tree);
    }
}

juce::AudioProcessorEditor* ClassicStudioGProcessor::createEditor() { return new ClassicStudioGEditor (*this); }

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new ClassicStudioGProcessor(); }
