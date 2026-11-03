#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <cmath>

// ---------------------------------------------------------------------------
//  Classic Studio G: parameter list and knob-to-value mappings.
//  Every knob is 0 to 10 (Width knobs 0 to 100). Zero always means off/neutral,
//  so double-click-to-zero always gives a neutral module. The one exception is
//  the 1176 attack and release knobs, which default to 10 (fully clockwise,
//  the fastest setting, as on the real unit).
// ---------------------------------------------------------------------------
namespace csg
{
    enum ModuleId
    {
        mInputFilter = 0, mDeEsser, m1176, mLA2A, mEq, mTube, mTape,
        mTransistor, mPlate, mHall, mRoom, mSlap, mDelay, mWidth,
        kNumModules
    };

    inline float lerpExp (float lo, float hi, float t) { return lo * std::pow (hi / lo, t); }

    // Input / Output trim: knob is centred at 0 dB, -12 dB far left, +12 dB far right
    inline float trimDb (float v)        { return v; }

    // Hall pre-delay: knob 0 to 10 is 0 to 200 ms
    inline float preDelayMs (float v)    { return 20.0f * v; }

    // Delay time divisions, in quarter-note beats. Synced to the host tempo.
    inline const char* const kDivNames[11] = { "1/2", "1/4.", "1/4", "1/4T", "1/8.", "1/8", "1/8T", "1/16.", "1/16", "1/16T", "1/32" };
    inline const float kDivBeats[11] = { 2.0f, 1.5f, 1.0f, 2.0f / 3.0f, 0.75f, 0.5f, 1.0f / 3.0f, 0.375f, 0.25f, 1.0f / 6.0f, 0.125f };

    inline const char* const kModNames[kNumModules] = { "Input Filter", "De-esser", "1176", "LA-2A", "API 560", "Tube", "Tape",
                                                        "Transistor", "Plate", "Hall", "Room", "Slap", "Delay", "Width" };
    inline const char* const kOnIds[kNumModules] = { "on0", "on1", "on2", "on3", "on4", "on5", "on6",
                                                     "on7", "on8", "on9", "on10", "on11", "on12", "on13" };

    // Input Filter (Neve 1073 style). Low cut rises with the knob, high cut falls.
    inline float inFilterLowHz  (float v) { return v <= 0.001f ? 0.0f : lerpExp (50.0f, 300.0f, v / 10.0f); }
    inline float inFilterHighHz (float v) { return v <= 0.001f ? 0.0f : lerpExp (20000.0f, 3000.0f, v / 10.0f); }

    // Saturator band limits: only the band between HP and LP is saturated.
    inline float satHpHz (float v) { return v <= 0.001f ? 0.0f : lerpExp (30.0f, 1000.0f, v / 10.0f); }
    inline float satLpHz (float v) { return v <= 0.001f ? 0.0f : lerpExp (20000.0f, 1500.0f, v / 10.0f); }

    // Reverb / delay return filters
    inline float fxLowHz  (float v) { return v <= 0.001f ? 0.0f : lerpExp (40.0f, 800.0f, v / 10.0f); }
    inline float fxHighHz (float v) { return v <= 0.001f ? 0.0f : lerpExp (16000.0f, 1500.0f, v / 10.0f); }

    // De-esser centre frequency (3 kHz to 12 kHz)
    inline float deEssHz (float v) { return lerpExp (3000.0f, 12000.0f, v / 10.0f); }

    inline const char* const kEqIds[10] = { "eq31", "eq63", "eq125", "eq250", "eq500",
                                            "eq1k", "eq2k", "eq4k", "eq8k", "eq16k" };
    inline const float kEqHz[10] = { 31.5f, 63.0f, 125.0f, 250.0f, 500.0f,
                                     1000.0f, 2000.0f, 4000.0f, 8000.0f, 16000.0f };

    inline juce::AudioProcessorValueTreeState::ParameterLayout createLayout()
    {
        using FP = juce::AudioParameterFloat;
        using CP = juce::AudioParameterChoice;
        juce::AudioProcessorValueTreeState::ParameterLayout l;

        auto knob = [&] (const char* id, const char* name, float def = 0.0f, float hi = 10.0f)
        {
            l.add (std::make_unique<FP> (juce::ParameterID (id, 1), name,
                                         juce::NormalisableRange<float> (0.0f, hi, 0.01f), def));
        };
        auto choice = [&] (const char* id, const char* name, juce::StringArray items, int def = 0)
        {
            l.add (std::make_unique<CP> (juce::ParameterID (id, 1), name, items, def));
        };

        for (int i = 0; i < kNumModules; ++i)
            l.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID (kOnIds[i], 1), juce::String (kModNames[i]) + " On", true));

        l.add (std::make_unique<FP> (juce::ParameterID ("in_gain", 1), "Input",
                                     juce::NormalisableRange<float> (-12.0f, 12.0f, 0.1f), 0.0f));

        knob ("if_low",  "Input Filter Low Cut");
        knob ("if_high", "Input Filter High Cut");

        knob ("ds_freq", "De-esser Frequency");
        knob ("ds_str",  "De-esser Strength");

        knob ("c_in",  "1176 Input");
        knob ("c_out", "1176 Output");
        knob ("c_att", "1176 Attack", 10.0f);
        knob ("c_rel", "1176 Release", 10.0f);
        choice ("c_ratio", "1176 Ratio", { "4", "8", "12", "20", "All" }, 0);
        choice ("c_meter", "1176 Meter", { "GR", "+8", "Off" }, 0);

        knob ("la_gain", "LA-2A Gain");
        knob ("la_peak", "LA-2A Peak Reduction");
        choice ("la_mode",  "LA-2A Mode",  { "Compress", "Limit" }, 0);
        choice ("la_meter", "LA-2A Meter", { "GR", "Out" }, 0);

        for (int i = 0; i < 10; ++i)
            l.add (std::make_unique<FP> (juce::ParameterID (kEqIds[i], 1),
                                         juce::String ("EQ ") + kEqIds[i] + " Hz",
                                         juce::NormalisableRange<float> (-12.0f, 12.0f, 0.1f), 0.0f));

        knob ("tube_amt", "Tube Amount");  knob ("tube_hp", "Tube High-pass");  knob ("tube_lp", "Tube Low-pass");
        knob ("tape_amt", "Tape Amount");  knob ("tape_hp", "Tape High-pass");  knob ("tape_lp", "Tape Low-pass");
        knob ("trn_amt",  "Transistor Amount");  knob ("trn_hp", "Transistor High-pass");  knob ("trn_lp", "Transistor Low-pass");

        knob ("plate_lvl", "Plate Level"); knob ("plate_duck", "Plate Duck"); knob ("plate_lo", "Plate Low Cut"); knob ("plate_hi", "Plate High Cut");
        knob ("hall_lvl",  "Hall Level");  knob ("hall_duck",  "Hall Duck");  knob ("hall_lo",  "Hall Low Cut");  knob ("hall_hi",  "Hall High Cut");  knob ("hall_pre", "Hall Pre-delay");
        knob ("room_lvl",  "Room Level");  knob ("room_duck",  "Room Duck");  knob ("room_lo",  "Room Low Cut");  knob ("room_hi",  "Room High Cut");
        knob ("slap_lvl",  "Slap Level");  knob ("slap_duck",  "Slap Duck");  knob ("slap_lo",  "Slap Low Cut");  knob ("slap_hi",  "Slap High Cut");
        knob ("dly_lvl",   "Delay Level"); knob ("dly_duck",   "Delay Duck"); knob ("dly_lo",   "Delay Low Cut"); knob ("dly_hi",   "Delay High Cut");
        choice ("dly_div", "Delay Time", juce::StringArray (kDivNames, 11), 2);

        knob ("w_haas", "Width Haas",     0.0f, 100.0f);
        knob ("w_det",  "Width Detune",   0.0f, 100.0f);
        knob ("w_ms",   "Width Mid-Side", 0.0f, 100.0f);

        l.add (std::make_unique<FP> (juce::ParameterID ("out_gain", 1), "Output",
                                     juce::NormalisableRange<float> (-12.0f, 12.0f, 0.1f), 0.0f));
        return l;
    }
}
