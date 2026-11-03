#pragma once
// ---------------------------------------------------------------------------
//  Classic Studio G: audio engine. Plain C++ (no JUCE types) so it can be
//  tested on its own. Every module processes a stereo pair of float buffers
//  in place. All modules are first-pass models of the hardware they are named
//  after; the numbers are meant to be tuned by ear.
// ---------------------------------------------------------------------------
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

namespace csg
{
constexpr float kPi = 3.14159265358979f;

inline float dbToGain (float db)  { return std::pow (10.0f, db * 0.05f); }
inline float gainToDb (float g)   { return 20.0f * std::log10 (std::max (g, 1.0e-9f)); }
inline float clampf (float x, float lo, float hi) { return x < lo ? lo : (x > hi ? hi : x); }

// ---------------------------------------------------------------- filters
struct Biquad
{
    enum Type { LP, HP, BP, Peak, LowShelf, HighShelf };
    float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;

    void reset() { z1 = z2 = 0; }
    inline float process (float x)
    {
        const float y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        return y;
    }

    void set (Type t, double fs, double f, double q, double gDb = 0.0)
    {
        f = std::max (10.0, std::min (f, fs * 0.45));
        const double w0 = 2.0 * 3.14159265358979323846 * f / fs;
        const double cw = std::cos (w0), sw = std::sin (w0);
        const double alpha = sw / (2.0 * q);
        double B0 = 1, B1 = 0, B2 = 0, a0 = 1, A1 = 0, A2 = 0;
        switch (t)
        {
            case LP: B0 = (1 - cw) / 2; B1 = 1 - cw; B2 = (1 - cw) / 2; a0 = 1 + alpha; A1 = -2 * cw; A2 = 1 - alpha; break;
            case HP: B0 = (1 + cw) / 2; B1 = -(1 + cw); B2 = (1 + cw) / 2; a0 = 1 + alpha; A1 = -2 * cw; A2 = 1 - alpha; break;
            case BP: B0 = alpha; B1 = 0; B2 = -alpha; a0 = 1 + alpha; A1 = -2 * cw; A2 = 1 - alpha; break;
            case Peak:
            {
                const double A = std::pow (10.0, gDb / 40.0);
                B0 = 1 + alpha * A; B1 = -2 * cw; B2 = 1 - alpha * A;
                a0 = 1 + alpha / A; A1 = -2 * cw; A2 = 1 - alpha / A;
                break;
            }
            case LowShelf:
            {
                const double A = std::pow (10.0, gDb / 40.0);
                const double tsa = 2.0 * std::sqrt (A) * (sw / 2.0 * std::sqrt (2.0));
                B0 = A * ((A + 1) - (A - 1) * cw + tsa);
                B1 = 2 * A * ((A - 1) - (A + 1) * cw);
                B2 = A * ((A + 1) - (A - 1) * cw - tsa);
                a0 = (A + 1) + (A - 1) * cw + tsa;
                A1 = -2 * ((A - 1) + (A + 1) * cw);
                A2 = (A + 1) + (A - 1) * cw - tsa;
                break;
            }
            case HighShelf:
            {
                const double A = std::pow (10.0, gDb / 40.0);
                const double tsa = 2.0 * std::sqrt (A) * (sw / 2.0 * std::sqrt (2.0));
                B0 = A * ((A + 1) + (A - 1) * cw + tsa);
                B1 = -2 * A * ((A - 1) + (A + 1) * cw);
                B2 = A * ((A + 1) + (A - 1) * cw - tsa);
                a0 = (A + 1) - (A - 1) * cw + tsa;
                A1 = 2 * ((A - 1) - (A + 1) * cw);
                A2 = (A + 1) - (A - 1) * cw - tsa;
                break;
            }
        }
        b0 = (float) (B0 / a0); b1 = (float) (B1 / a0); b2 = (float) (B2 / a0);
        a1 = (float) (A1 / a0); a2 = (float) (A2 / a0);
    }
};

struct OnePoleLP
{
    float y = 0, a = 0;
    void setFreq (double fs, double f) { a = (float) (1.0 - std::exp (-2.0 * 3.14159265358979 * f / fs)); }
    inline float process (float x) { y += a * (x - y); return y; }
};

struct DcBlock
{
    float x1 = 0, y1 = 0;
    inline float process (float x) { const float y = x - x1 + 0.9995f * y1; x1 = x; y1 = y; return y; }
};

// Level-dependent shapers used for the "built-in" hardware colour.
inline float fetShape (float x)   // FET stage: slightly asymmetric, mostly odd with some even
{
    const float s = 1.15f * x;
    return std::tanh (s + 0.12f * s * s) / 1.15f;
}
inline float tubeShape (float x)  // tube-style warmth: stronger even-order content
{
    const float s = 1.3f * x;
    return std::tanh (s + 0.2f * s * s) / 1.3f;
}

// ------------------------------------------------------------ gain / trim
struct GainStage
{
    float cur = 1.0f;
    void process (float* L, float* R, int n, float target)
    {
        const float step = (target - cur) / (float) std::max (n, 1);
        for (int i = 0; i < n; ++i) { cur += step; L[i] *= cur; R[i] *= cur; }
        cur = target;
    }
};

// ----------------------------------------------------------- input filter
struct InputFilterDsp
{
    double fs = 48000;
    Biquad hp[2], lp[2];
    OnePoleLP hp1[2];
    float lowF = 0, highF = 0;

    void prepare (double sr)
    {
        fs = sr; lowF = highF = 0;
        for (auto& b : hp) b.reset();
        for (auto& b : lp) b.reset();
        for (auto& o : hp1) o.y = 0;
    }

    void process (float* L, float* R, int n, float lowHz, float highHz)
    {
        if (lowHz != lowF)
        {
            if (lowHz > 0) { for (auto& b : hp) b.set (Biquad::HP, fs, lowHz, 0.7071); for (auto& o : hp1) o.setFreq (fs, lowHz); }
            else { for (auto& b : hp) b.reset(); for (auto& o : hp1) o.y = 0; }
            lowF = lowHz;
        }
        if (highHz != highF)
        {
            if (highHz > 0) for (auto& b : lp) b.set (Biquad::LP, fs, highHz, 0.7071);
            else for (auto& b : lp) b.reset();
            highF = highHz;
        }
        float* ch[2] = { L, R };
        for (int c = 0; c < 2; ++c)
            for (int i = 0; i < n; ++i)
            {
                float x = ch[c][i];
                if (lowF > 0)  { x = hp[c].process (x); x = x - hp1[c].process (x); }   // 12 dB + 6 dB per octave
                if (highF > 0) x = lp[c].process (x);
                ch[c][i] = x;
            }
    }
};

// -------------------------------------------------------------- de-esser
struct DeEsserDsp
{
    double fs = 48000;
    Biquad bp[2];
    float lastF = -1, bandEnv = 0, fullEnv = 0, gr = 0;
    float aAtk = 0, aRel = 0, aFullRel = 0, aGrAtk = 0, aGrRel = 0;

    void prepare (double sr)
    {
        fs = sr; lastF = -1; bandEnv = fullEnv = gr = 0;
        for (auto& b : bp) b.reset();
        auto c = [&] (double t) { return (float) std::exp (-1.0 / (t * fs)); };
        aAtk = c (0.0005); aRel = c (0.03); aFullRel = c (0.08); aGrAtk = c (0.0004); aGrRel = c (0.03);
    }

    // Split-band: only the sibilant band is turned down, and detection compares
    // that band with the whole signal so it follows the voice, not the level.
    // returns the largest gain reduction (dB) seen in this block
    float process (float* L, float* R, int n, float freqHz, float strength)
    {
        if (strength <= 0.001f) { gr = 0; bandEnv = fullEnv = 0; return 0.0f; }
        if (freqHz != lastF) { for (auto& b : bp) b.set (Biquad::BP, fs, freqHz, 1.2); lastF = freqHz; }
        const float thr = -6.0f - 1.2f * strength;
        float maxGr = 0;
        for (int i = 0; i < n; ++i)
        {
            const float bl = bp[0].process (L[i]), br = bp[1].process (R[i]);
            const float band = 0.5f * (std::fabs (bl) + std::fabs (br));
            const float full = 0.5f * (std::fabs (L[i]) + std::fabs (R[i]));
            bandEnv = band > bandEnv ? aAtk * bandEnv + (1 - aAtk) * band : aRel * bandEnv + (1 - aRel) * band;
            fullEnv = full > fullEnv ? aAtk * fullEnv + (1 - aAtk) * full : aFullRel * fullEnv + (1 - aFullRel) * full;
            float target = 0;
            if (bandEnv > 0.0018f)   // about -55 dBFS gate
                target = clampf ((gainToDb (bandEnv) - gainToDb (fullEnv) - thr) * 0.75f, 0.0f, 12.0f);
            gr = target > gr ? aGrAtk * gr + (1 - aGrAtk) * target : aGrRel * gr + (1 - aGrRel) * target;
            const float g = dbToGain (-gr) - 1.0f;
            L[i] += bl * g; R[i] += br * g;
            maxGr = std::max (maxGr, gr);
        }
        return maxGr;
    }
};

// ------------------------------------------------------------------ 1176
// FET limiting amplifier model: fixed threshold, the Input knob drives into it,
// four ratios plus all-buttons-in, 20 us to 800 us attack, 50 ms to 1.1 s release.
struct C1176Dsp
{
    double fs = 48000;
    float gr = 0;
    DcBlock dc[2];

    void prepare (double sr) { fs = sr; gr = 0; dc[0] = {}; dc[1] = {}; }

    // returns the largest gain reduction (dB) seen in this block
    float process (float* L, float* R, int n, float inV, float outV, float attV, float relV, int ratioIdx)
    {
        // Ratios 4/8/12/20 and the threshold rising with the ratio come from the manual and
        // the Huddersfield study. All-buttons-in sits between 12:1 and 20:1 and the ratio
        // grows after the transient. The dB values for threshold and knee are my choices.
        static const float ratios[4] = { 4.0f, 8.0f, 12.0f, 20.0f };
        static const float thrs[4]   = { -10.0f, -8.0f, -6.0f, -4.0f };
        static const float knees[4]  = { 6.0f, 4.5f, 3.0f, 2.0f };      // harder knee at higher ratios
        const bool all = (ratioIdx == 4);
        const int ri = std::max (0, std::min (ratioIdx, 3));
        const float baseRatio = all ? 12.0f : ratios[ri];
        const float drive = inV * 3.0f;                              // 0 to +30 dB into the detector
        const float gin = dbToGain (drive);
        const float makeup = dbToGain (outV * 1.2f - 0.6f * drive);  // partial auto-compensation + output knob
        const float thr = all ? -6.0f : thrs[ri];
        const float atk = 0.0008f * std::pow (0.00002f / 0.0008f, attV / 10.0f);   // 800 us to 20 us (manual)
        const float rel = 1.1f * std::pow (0.05f / 1.1f, relV / 10.0f);            // 1.1 s to 50 ms (manual)
        const float aA = std::exp (-1.0f / (atk * (float) fs)), aR = std::exp (-1.0f / (rel * (float) fs));
        const float sat = all ? 1.5f : 1.0f;
        const float knee = all ? 2.0f : knees[ri];
        float maxGr = 0;
        for (int i = 0; i < n; ++i)
        {
            const float l = L[i] * gin, r = R[i] * gin;
            const float over = gainToDb (std::max (std::fabs (l), std::fabs (r))) - thr;
            const float ratio = all ? baseRatio + 8.0f * clampf (gr / 20.0f, 0.0f, 1.0f) : baseRatio;   // all-buttons-in: ratio climbs 12 to 20
            const float slope = 1.0f - 1.0f / ratio;
            float target;
            if (over <= -knee * 0.5f) target = 0;
            else if (over >= knee * 0.5f) target = over * slope;
            else { const float t = over + knee * 0.5f; target = slope * t * t / (2.0f * knee); }
            gr = target > gr ? aA * gr + (1 - aA) * target : aR * gr + (1 - aR) * target;
            const float g = dbToGain (-gr);
            L[i] = dc[0].process (fetShape (l * g * sat) / sat) * makeup;
            R[i] = dc[1].process (fetShape (r * g * sat) / sat) * makeup;
            maxGr = std::max (maxGr, gr);
        }
        return maxGr;
    }
};

// ------------------------------------------------------------------ LA-2A
// Optical leveler model: soft knee, ~10 ms attack, two-stage release that
// slows down the longer it has been working, tube-style warmth built in.
struct LA2ADsp
{
    double fs = 48000;
    float env = 0, gr = 0, grF = 0, grS = 0, mem = 0;
    DcBlock dc[2];

    void prepare (double sr) { fs = sr; env = gr = grF = grS = mem = 0; dc[0] = {}; dc[1] = {}; }

    float process (float* L, float* R, int n, float gainV, float peakV, bool limitMode)
    {
        const float thr = -3.6f * peakV;                 // 0 to -36 dB
        const float ratio = limitMode ? 10.0f : 3.0f;
        const float slope = 1.0f - 1.0f / ratio;
        const float knee = 8.0f;
        const float makeup = dbToGain (gainV * 1.8f);
        const float aEnvA = std::exp (-1.0f / (0.005f * (float) fs)), aEnvR = std::exp (-1.0f / (0.05f * (float) fs));
        const float aAtk = std::exp (-1.0f / (0.010f * (float) fs));   // about 10 ms attack (UA)
        const float memUp = 1.0f / (3.0f * (float) fs), memDown = 1.0f / (10.0f * (float) fs);
        float maxGr = 0;
        for (int i = 0; i < n; ++i)
        {
            const float pk = std::max (std::fabs (L[i]), std::fabs (R[i]));
            env = pk > env ? aEnvA * env + (1 - aEnvA) * pk : aEnvR * env + (1 - aEnvR) * pk;
            const float over = gainToDb (env) - thr;
            float target;
            if (peakV <= 0.001f || 2.0f * over < -knee) target = 0;
            else if (2.0f * std::fabs (over) <= knee) { const float t = over + knee * 0.5f; target = slope * t * t / (2.0f * knee); }
            else target = slope * over;

            // Two-stage release (UA): about 60 ms for the first half, then 1 to 15 s for the rest.
            // How long the slow part takes grows with how long the unit has been working (my model of the cell's memory).
            if (gr > 1.0f) mem += (1.0f - mem) * memUp; else mem -= mem * memDown;
            const float tFast = 0.06f / 0.693f;                      // 60 ms half-life
            const float tSlow = 1.0f + 14.0f * mem;                  // 1 s to 15 s
            const float aF = std::exp (-1.0f / (tFast * (float) fs)), aS = std::exp (-1.0f / (tSlow * (float) fs));
            const float half = 0.5f * target;
            grF = half > grF ? aAtk * grF + (1 - aAtk) * half : aF * grF + (1 - aF) * half;
            grS = half > grS ? aAtk * grS + (1 - aAtk) * half : aS * grS + (1 - aS) * half;
            gr = grF + grS;

            const float g = dbToGain (-gr) * makeup;
            L[i] = dc[0].process (tubeShape (L[i] * g));
            R[i] = dc[1].process (tubeShape (R[i] * g));
            maxGr = std::max (maxGr, gr);
        }
        return maxGr;
    }
};

// ------------------------------------------------------- API 560 graphic EQ
struct EqDsp
{
    double fs = 48000;
    Biquad b[10][2];
    float lastG[10];
    bool active[10];

    void prepare (double sr)
    {
        fs = sr;
        for (int k = 0; k < 10; ++k) { lastG[k] = 1.0e9f; active[k] = false; b[k][0].reset(); b[k][1].reset(); }
    }

    void process (float* L, float* R, int n, const float* gainsDb, const float* freqs)
    {
        for (int k = 0; k < 10; ++k)
        {
            const float g = gainsDb[k];
            if (g != lastG[k])
            {
                lastG[k] = g;
                active[k] = std::fabs (g) > 0.01f;
                if (active[k])
                {
                    const double q = 0.9 + 0.9 * std::fabs (g) / 12.0;   // proportional Q (API): wider at small settings, narrower at full (values mine)
                    b[k][0].set (Biquad::Peak, fs, freqs[k], q, g);
                    b[k][1].set (Biquad::Peak, fs, freqs[k], q, g);
                }
                else { b[k][0].reset(); b[k][1].reset(); }
            }
            if (! active[k]) continue;
            for (int i = 0; i < n; ++i) { L[i] = b[k][0].process (L[i]); R[i] = b[k][1].process (R[i]); }
        }
    }
};

// -------------------------------------------------------------- saturators
// Only the band between the high-pass and low-pass is saturated; the rest of the
// signal passes through untouched. Amount sets both drive and how much of the
// saturated band is mixed in.
struct SaturatorDsp
{
    enum Kind { Tube, Tape, Transistor };
    Kind kind = Tube;
    double fs = 48000;
    Biquad hp[2], lp[2], bump[2];
    OnePoleLP tone[2];
    DcBlock dc[2];
    float lastHp = -1, lastLp = -1, lastAmt = -1;

    void prepare (double sr, Kind k)
    {
        fs = sr; kind = k; lastHp = lastLp = lastAmt = -1;
        for (int c = 0; c < 2; ++c) { hp[c].reset(); lp[c].reset(); bump[c].reset(); tone[c].y = 0; dc[c] = {}; }
    }

    void process (float* L, float* R, int n, float amtV, float hpHz, float lpHz)
    {
        if (amtV <= 0.001f) return;
        if (hpHz != lastHp) { if (hpHz > 0) for (auto& b : hp) b.set (Biquad::HP, fs, hpHz, 0.7071); else for (auto& b : hp) b.reset(); lastHp = hpHz; }
        if (lpHz != lastLp) { if (lpHz > 0) for (auto& b : lp) b.set (Biquad::LP, fs, lpHz, 0.7071); else for (auto& b : lp) b.reset(); lastLp = lpHz; }
        if (amtV != lastAmt)
        {
            for (auto& b : bump) b.set (Biquad::LowShelf, fs, 90.0, 0.7, 1.8 * amtV / 10.0);   // tape head bump
            for (auto& t : tone) t.setFreq (fs, 14000.0 - 5000.0 * amtV / 10.0);               // tape high-frequency loss
            lastAmt = amtV;
        }
        const float d = dbToGain (amtV * 1.8f);              // 0 to +18 dB of drive
        const float comp = std::pow (d, 0.7f);
        const float mix = clampf (amtV / 2.5f, 0.0f, 1.0f);
        float* ch[2] = { L, R };
        for (int c = 0; c < 2; ++c)
            for (int i = 0; i < n; ++i)
            {
                const float x = ch[c][i];
                float band = x;
                if (hpHz > 0) band = hp[c].process (band);
                if (lpHz > 0) band = lp[c].process (band);
                const float rest = x - band;
                const float s = band * d;
                float y;
                switch (kind)
                {
                    case Tube:       y = dc[c].process (std::tanh (s + 0.3f * s * s)) / comp; break;
                    case Tape:       y = tone[c].process (bump[c].process (std::tanh (0.9f * s) / 0.9f / comp)); break;
                    default:         { const float a = std::fabs (s); y = s / std::cbrt (1.0f + a * a * a) / comp; } break;
                }
                ch[c][i] = rest + band + mix * (y - band);
            }
    }
};

// ------------------------------------------------------------------ reverb
// 8-line feedback delay network with input diffusion, damping and light
// modulation. Decay, size, damping and pre-delay set the room, hall or plate.
struct FdnReverb
{
    static constexpr int N = 8;
    struct Ap
    {
        std::vector<float> b; int w = 0; float g = 0.6f;
        void init (int len) { b.assign ((size_t) std::max (len, 1), 0.0f); w = 0; }
        inline float process (float x)
        {
            const float d = b[(size_t) w];
            const float v = x - g * d;
            b[(size_t) w] = v;
            if (++w >= (int) b.size()) w = 0;
            return g * v + d;
        }
    };

    std::vector<float> buf[N];
    int wr[N] = {}, len[N] = {};
    float lp[N] = {}, g[N] = {}, ph[N] = {}, inc[N] = {};
    float dampA = 0.5f, modDepth = 0, outScale = 0.5f;
    std::vector<float> pre; int preW = 0; float preCur = 0, preTarget = 0;
    Ap ap[3];

    void setPreDelay (float samples) { preTarget = std::max (0.0f, std::min (samples, (float) pre.size() - 4.0f)); }

    void prepare (double fs, float rt60, float size, float dampHz, float preMs, float mod)
    {
        static const int base[N] = { 1117, 1327, 1559, 1693, 1993, 2213, 2459, 2657 };
        for (int i = 0; i < N; ++i)
        {
            len[i] = std::max (32, (int) std::lround (base[i] * fs / 48000.0 * size));
            buf[i].assign ((size_t) len[i] + 160, 0.0f);
            wr[i] = 0; lp[i] = 0;
            g[i] = std::pow (10.0f, -3.0f * (float) len[i] / ((float) fs * rt60));
            ph[i] = 0.8f * (float) i;
            inc[i] = 2.0f * kPi * (0.2f + 0.07f * (float) i) / (float) fs;
        }
        dampA = (float) (1.0 - std::exp (-2.0 * 3.14159265358979 * dampHz / fs));
        modDepth = mod;
        pre.assign ((size_t) (fs * 0.26) + 8, 0.0f); preW = 0;
        preCur = preTarget = std::max (0.0f, std::min ((float) (preMs * 0.001 * fs), (float) pre.size() - 4.0f));
        static const int apLen[3] = { 142, 107, 379 };
        for (int k = 0; k < 3; ++k) { ap[k].init ((int) std::lround (apLen[k] * fs / 48000.0)); ap[k].g = 0.6f; }
    }

    void clear()
    {
        for (int i = 0; i < N; ++i) { std::fill (buf[i].begin(), buf[i].end(), 0.0f); lp[i] = 0; }
        std::fill (pre.begin(), pre.end(), 0.0f);
        for (auto& a : ap) std::fill (a.b.begin(), a.b.end(), 0.0f);
    }

    inline void process (float in, float& outL, float& outR)
    {
        preCur += 0.0005f * (preTarget - preCur);
        pre[(size_t) preW] = in;
        float x;
        {
            const int ps = (int) pre.size();
            float rp = (float) preW - preCur;
            if (rp < 0) rp += (float) ps;
            if (rp >= (float) ps) rp -= (float) ps;
            const int i0 = (int) rp; const float f = rp - (float) i0;
            const int i1 = (i0 + 1 >= ps) ? 0 : i0 + 1;
            x = pre[(size_t) i0] * (1 - f) + pre[(size_t) i1] * f;
        }
        if (++preW >= (int) pre.size()) preW = 0;
        for (auto& a : ap) x = a.process (x);

        float y[N]; float sum = 0;
        for (int i = 0; i < N; ++i)
        {
            const float d = (float) len[i] + modDepth * std::sin (ph[i]);
            ph[i] += inc[i]; if (ph[i] > 2.0f * kPi) ph[i] -= 2.0f * kPi;
            const int size = (int) buf[i].size();
            float rp = (float) wr[i] - d;
            while (rp < 0) rp += (float) size;
            if (rp >= (float) size) rp -= (float) size;
            const int i0 = (int) rp; const float f = rp - (float) i0;
            const int i1 = (i0 + 1 >= size) ? 0 : i0 + 1;
            float v = buf[i][(size_t) i0] * (1 - f) + buf[i][(size_t) i1] * f;
            lp[i] += dampA * (v - lp[i]);
            v = lp[i] * g[i];
            y[i] = v; sum += v;
        }
        const float k = sum * (2.0f / (float) N);          // Householder feedback matrix
        for (int i = 0; i < N; ++i)
        {
            const float sgn = (i % 3 == 0) ? -1.0f : 1.0f;
            buf[i][(size_t) wr[i]] = y[i] - k + sgn * x * 0.35f;
            if (++wr[i] >= (int) buf[i].size()) wr[i] = 0;
        }
        outL = (y[0] - y[2] + y[4] - y[6]) * outScale;
        outR = (y[1] - y[3] + y[5] - y[7]) * outScale;
    }
};

struct ReverbFx
{
    enum Kind { Plate, Hall, Room };
    FdnReverb fdn;
    Biquad hp[2], lp[2];
    float lastLo = -1, lastHi = -1, env = 0;
    double fs = 48000;
    bool active = false;

    void prepare (double sr, Kind k)
    {
        fs = sr; active = false; lastLo = lastHi = -1; env = 0;
        switch (k)
        {
            case Plate: fdn.prepare (sr, 1.9f, 0.62f, 10000.0f, 0.0f, 1.2f); fdn.outScale = 0.76f; break;   // dense, bright, no pre-delay
            case Hall:  fdn.prepare (sr, 2.4f, 1.50f, 5500.0f, 0.0f, 2.5f); fdn.outScale = 1.10f; break;   // long, pre-delayed, darker
            case Room:  fdn.prepare (sr, 0.5f, 0.45f, 6000.0f, 4.0f, 0.6f); fdn.outScale = 1.40f; break;    // short and tight
        }
        for (int c = 0; c < 2; ++c) { hp[c].reset(); lp[c].reset(); }
    }

    void process (float* L, float* R, int n, float levelV, float duckV, float loHz, float hiHz, float preMs = 0.0f)
    {
        if (levelV <= 0.001f)
        {
            if (active) { fdn.clear(); for (int c = 0; c < 2; ++c) { hp[c].reset(); lp[c].reset(); } active = false; env = 0; }
            return;
        }
        active = true;
        fdn.setPreDelay (preMs * 0.001f * (float) fs);
        if (loHz != lastLo) { if (loHz > 0) for (auto& b : hp) b.set (Biquad::HP, fs, loHz, 0.7071); else for (auto& b : hp) b.reset(); lastLo = loHz; }
        if (hiHz != lastHi) { if (hiHz > 0) for (auto& b : lp) b.set (Biquad::LP, fs, hiHz, 0.7071); else for (auto& b : lp) b.reset(); lastHi = hiHz; }
        const float lvl = 1.4f * std::pow (levelV / 10.0f, 1.6f);
        const float duck = duckV / 10.0f;
        const float aA = std::exp (-1.0f / (0.005f * (float) fs)), aR = std::exp (-1.0f / (0.2f * (float) fs));
        for (int i = 0; i < n; ++i)
        {
            const float in = 0.5f * (L[i] + R[i]);
            const float a = std::fabs (in);
            env = a > env ? aA * env + (1 - aA) * a : aR * env + (1 - aR) * a;
            const float act = clampf ((gainToDb (env) + 45.0f) / 30.0f, 0.0f, 1.0f);
            const float dg = 1.0f - duck * 0.9f * act;
            float wl, wr;
            fdn.process (in, wl, wr);
            if (lastLo > 0) { wl = hp[0].process (wl); wr = hp[1].process (wr); }
            if (lastHi > 0) { wl = lp[0].process (wl); wr = lp[1].process (wr); }
            L[i] += wl * lvl * dg; R[i] += wr * lvl * dg;
        }
    }
};

// ------------------------------------------------------------------ delays
struct DelayFx
{
    enum Kind { Slap, Tempo };
    Kind kind = Slap;
    double fs = 48000;
    std::vector<float> buf[2];
    int size = 1, w = 0;
    float curDelay = 0, ph1 = 0, ph2 = 0, env = 0, lastLo = -1, lastHi = -1;
    Biquad hp[2], lp[2];
    bool active = false;

    void prepare (double sr, Kind k)
    {
        fs = sr; kind = k; size = (int) (sr * 6.2) + 8;
        buf[0].assign ((size_t) size, 0.0f); buf[1].assign ((size_t) size, 0.0f);
        w = 0; curDelay = 0; ph1 = ph2 = env = 0; lastLo = lastHi = -1; active = false;
        for (int c = 0; c < 2; ++c) { hp[c].reset(); lp[c].reset(); }
    }

    void process (float* L, float* R, int n, float levelV, float duckV, float loHz, float hiHz, float delaySec)
    {
        const float target = std::min (delaySec * (float) fs, (float) size - 8.0f);
        if (levelV <= 0.001f)
        {
            if (active)
            {
                std::fill (buf[0].begin(), buf[0].end(), 0.0f); std::fill (buf[1].begin(), buf[1].end(), 0.0f);
                for (int c = 0; c < 2; ++c) { hp[c].reset(); lp[c].reset(); }
                active = false; env = 0;
            }
            curDelay = target;
            return;
        }
        active = true;
        if (curDelay <= 0) curDelay = target;
        if (loHz != lastLo) { if (loHz > 0) for (auto& b : hp) b.set (Biquad::HP, fs, loHz, 0.7071); else for (auto& b : hp) b.reset(); lastLo = loHz; }
        if (hiHz != lastHi) { if (hiHz > 0) for (auto& b : lp) b.set (Biquad::LP, fs, hiHz, 0.7071); else for (auto& b : lp) b.reset(); lastHi = hiHz; }

        const float fb = kind == Slap ? 0.28f : 0.38f;
        const float lvl = std::pow (levelV / 10.0f, 1.5f);
        const float duck = duckV / 10.0f;
        const float sm = 1.0f - std::exp (-1.0f / (0.05f * (float) fs));
        const float aA = std::exp (-1.0f / (0.005f * (float) fs)), aR = std::exp (-1.0f / (0.2f * (float) fs));
        const float lfo1 = 2.0f * kPi * (kind == Slap ? 0.7f : 0.35f) / (float) fs;
        const float lfo2 = 2.0f * kPi * 6.0f / (float) fs;
        float* ch[2] = { L, R };
        float wet[2];

        for (int i = 0; i < n; ++i)
        {
            curDelay += sm * (target - curDelay);
            // Slap: tape-style wow and flutter. Tempo: very light chorus-like movement.
            const float mod = kind == Slap ? (0.0004f * (float) fs) * std::sin (ph1) + (0.00005f * (float) fs) * std::sin (ph2)
                                           : (0.00025f * (float) fs) * std::sin (ph1);
            ph1 += lfo1; if (ph1 > 2 * kPi) ph1 -= 2 * kPi;
            ph2 += lfo2; if (ph2 > 2 * kPi) ph2 -= 2 * kPi;
            const float d = clampf (curDelay + mod, 2.0f, (float) size - 3.0f);

            for (int c = 0; c < 2; ++c)
            {
                float rp = (float) w - d;
                if (rp < 0) rp += (float) size;
                if (rp >= (float) size) rp -= (float) size;
                const int i0 = (int) rp; const float f = rp - (float) i0;
                const int i1 = (i0 + 1 >= size) ? 0 : i0 + 1;
                float v = buf[c][(size_t) i0] * (1 - f) + buf[c][(size_t) i1] * f;
                if (lastLo > 0) v = hp[c].process (v);
                if (lastHi > 0) v = lp[c].process (v);
                if (kind == Slap) v = std::tanh (v * 1.1f) / 1.1f;     // tape-style saturation in the repeats
                buf[c][(size_t) w] = ch[c][i] + fb * v;
                wet[c] = v;
            }
            if (++w >= size) w = 0;

            const float a = std::fabs (0.5f * (L[i] + R[i]));
            env = a > env ? aA * env + (1 - aA) * a : aR * env + (1 - aR) * a;
            const float act = clampf ((gainToDb (env) + 45.0f) / 30.0f, 0.0f, 1.0f);
            const float dg = lvl * (1.0f - duck * 0.9f * act);
            L[i] += wet[0] * dg; R[i] += wet[1] * dg;
        }
    }
};

// ------------------------------------------------------------------- width
struct PitchShifter
{
    std::vector<float> b; int w = 0, size = 1; float phase = 0; float W = 1;
    void prepare (double fs)
    {
        W = (float) (0.03 * fs);
        size = (int) (W * 2.0f) + 16;
        b.assign ((size_t) size, 0.0f); w = 0; phase = 0;
    }
    inline float read (float d) const
    {
        float rp = (float) w - d;
        while (rp < 0) rp += (float) size;
        if (rp >= (float) size) rp -= (float) size;
        const int i0 = (int) rp; const float f = rp - (float) i0;
        const int i1 = (i0 + 1 >= size) ? 0 : i0 + 1;
        return b[(size_t) i0] * (1 - f) + b[(size_t) i1] * f;
    }
    inline float process (float x, float ratio)
    {
        b[(size_t) w] = x;
        phase += (1.0f - ratio) / W;
        phase -= std::floor (phase);
        float p2 = phase + 0.5f; if (p2 >= 1.0f) p2 -= 1.0f;
        const float g1 = std::sin (kPi * phase) * std::sin (kPi * phase), g2 = 1.0f - g1;
        const float out = read (phase * W + 2.0f) * g1 + read (p2 * W + 2.0f) * g2;
        if (++w >= size) w = 0;
        return out;
    }
};

struct WidthDsp
{
    double fs = 48000;
    std::vector<float> hb; int hw = 0, hsize = 1; float hCur = 0;
    PitchShifter ps[2];

    void prepare (double sr)
    {
        fs = sr; hsize = (int) (sr * 0.03) + 8;
        hb.assign ((size_t) hsize, 0.0f); hw = 0; hCur = 0;
        ps[0].prepare (sr); ps[1].prepare (sr);
    }

    // haas, det, ms are 0 to 100
    void process (float* L, float* R, int n, float haas, float det, float ms)
    {
        if (haas > 0.001f)
        {
            const float target = haas / 100.0f * 0.020f * (float) fs;      // up to 20 ms on the right channel
            const float sm = 1.0f - std::exp (-1.0f / (0.03f * (float) fs));
            for (int i = 0; i < n; ++i)
            {
                hCur += sm * (target - hCur);
                hb[(size_t) hw] = R[i];
                float rp = (float) hw - hCur;
                if (rp < 0) rp += (float) hsize;
                if (rp >= (float) hsize) rp -= (float) hsize;
                const int i0 = (int) rp; const float f = rp - (float) i0;
                const int i1 = (i0 + 1 >= hsize) ? 0 : i0 + 1;
                R[i] = hb[(size_t) i0] * (1 - f) + hb[(size_t) i1] * f;
                if (++hw >= hsize) hw = 0;
            }
        }
        else hCur = 0;

        if (det > 0.001f)
        {
            const float cents = 4.0f + 10.0f * det / 100.0f;
            const float up = std::pow (2.0f, cents / 1200.0f), down = 1.0f / up;
            const float m = det / 100.0f * 0.5f;
            for (int i = 0; i < n; ++i)
            {
                L[i] = (1 - m) * L[i] + m * ps[0].process (L[i], up);
                R[i] = (1 - m) * R[i] + m * ps[1].process (R[i], down);
            }
        }

        if (ms > 0.001f)
        {
            const float sideGain = 1.0f + ms / 100.0f;                      // up to +6 dB on the side signal
            for (int i = 0; i < n; ++i)
            {
                const float m = 0.5f * (L[i] + R[i]), s = 0.5f * (L[i] - R[i]) * sideGain;
                L[i] = m + s; R[i] = m - s;
            }
        }
    }
};

} // namespace csg
