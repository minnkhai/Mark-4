#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <atomic>
#include <string>
#include <unordered_map>
#include "Params.h"
#include "Dsp.h"

class ClassicStudioGProcessor : public juce::AudioProcessor
{
public:
    ClassicStudioGProcessor();
    ~ClassicStudioGProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 4.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    // Chain order: slot 0..15 (row 1 left to right, then row 2) holds a module id.
    int  moduleAtSlot (int slot) const;
    void moveModule (int fromSlot, int toSlot);

    float param (const char* id) const;

    juce::AudioProcessorValueTreeState apvts;

    // Meter feeds read by the editor (linear level, decays over about 150 ms)
    std::atomic<float> inLevel { 0.0f }, outLevel { 0.0f };
    std::atomic<float> fetInLevel { 0.0f }, fetOutLevel { 0.0f }, optoInLevel { 0.0f }, optoOutLevel { 0.0f }, dsInLevel { 0.0f }, dsOutLevel { 0.0f };
    std::atomic<float> grFet { 0.0f }, grOpto { 0.0f }, grDeEss { 0.0f };    // gain reduction in dB
    std::atomic<bool>  inClip { false }, outClip { false };

private:
    static uint64_t packOrder (const std::array<int, csg::kNumModules>&);
    static std::array<int, csg::kNumModules> unpackOrder (uint64_t);
    void runModule (int id, float* L, float* R, int n, double bpm);
    void updateMeter (std::atomic<float>& level, std::atomic<bool>* clip, const float* L, const float* R, int n);

    std::atomic<uint64_t> packedOrder { 0 };
    std::unordered_map<std::string, std::atomic<float>*> raw;

    double sr = 44100.0;
    std::vector<float> wl, wr;

    csg::GainStage inGain, outGain;
    csg::InputFilterDsp inFilter;
    csg::DeEsserDsp deEsser;
    csg::C1176Dsp fet;
    csg::LA2ADsp opto;
    csg::EqDsp eq;
    csg::SaturatorDsp sat[3];
    csg::ReverbFx rev[3];
    csg::DelayFx dly[2];
    csg::WidthDsp width;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ClassicStudioGProcessor)
};
