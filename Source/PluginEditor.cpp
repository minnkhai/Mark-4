#include "PluginEditor.h"

using namespace juce;

namespace
{
// ============================================================== look & feel
const Colour kGreen  (0xff3de26b);
const Colour kYellow (0xffffd23a);
const Colour kRed    (0xffff4040);
const Colour kAmber  (0xffffb347);

Font plainFont (float h)                { return Font (FontOptions (h)); }
Font boldFont  (float h)                { return Font (FontOptions (h, Font::bold)); }
Font panelFont (float h, bool bold)     { return Font (FontOptions ("Avenir Next Condensed", h, bold ? Font::bold : Font::plain)); }

void drawScrew (Graphics& g, float cx, float cy, float r)
{
    g.setGradientFill (ColourGradient (Colour (0xffe6e6ea), cx - r * 0.3f, cy - r * 0.4f, Colour (0xff6d6e73), cx + r, cy + r, true));
    g.fillEllipse (cx - r, cy - r, r * 2, r * 2);
    g.setColour (Colour (0xff2a2a2c));
    g.drawEllipse (cx - r, cy - r, r * 2, r * 2, 1.0f);
    g.drawLine (cx - r * 0.6f, cy, cx + r * 0.6f, cy, 0.8f);
}

// ============================================================== knobs
enum Kind { Num, Pct, Bi, Ms, Sel, FInLow, FInHigh, FDeEss, FSatHp, FSatLp, FFxLow, FFxHigh };

struct KnobSpec
{
    const char* id; const char* label; Kind kind = Num;
    bool ccw = false;   // turns counterclockwise (Input Filter high cut)
    bool rev = false;   // printed scale runs 10 down to 0 (1176 attack and release)
    float def = 0.0f;   // value restored on double-click
};

KnobSpec K (const char* id, const char* label, Kind kind = Num, bool ccw = false, bool rev = false, float def = 0.0f)
{
    return { id, label, kind, ccw, rev, def };
}

bool isFreq (Kind k) { return k >= FInLow; }

float kindHz (Kind k, float v)
{
    switch (k)
    {
        case FInLow:  return csg::inFilterLowHz (v);
        case FInHigh: return csg::inFilterHighHz (v);
        case FDeEss:  return csg::deEssHz (v);
        case FSatHp:  return csg::satHpHz (v);
        case FSatLp:  return csg::satLpHz (v);
        case FFxLow:  return csg::fxLowHz (v);
        case FFxHigh: return csg::fxHighHz (v);
        default:      return 0.0f;
    }
}

String hzShort (float hz)
{
    if (hz <= 0.0f) return "OFF";
    if (hz >= 1000.0f) { const float k = hz / 1000.0f; return String (k, k < 10.0f ? 1 : 0) + "k"; }
    return String (roundToInt (hz));
}

String hzLong (float hz)
{
    if (hz <= 0.0f) return "OFF";
    if (hz >= 1000.0f) return hzShort (hz).dropLastCharacters (1) + " kHz";
    return hzShort (hz) + " Hz";
}

struct Lab { float pos; String text; };

std::vector<Lab> tickLabels (const KnobSpec& s)
{
    std::vector<Lab> r;
    switch (s.kind)
    {
        case Sel: for (int i = 0; i < 11; ++i) r.push_back ({ (float) i / 10.0f, csg::kDivNames[i] }); break;
        case Bi:  r = { { 0.0f, "-12" }, { 0.25f, "-6" }, { 0.5f, "0" }, { 0.75f, "+6" }, { 1.0f, "+12" } }; break;
        case Pct: r = { { 0.0f, "0" }, { 0.25f, "25" }, { 0.5f, "50" }, { 0.75f, "75" }, { 1.0f, "100" } }; break;
        case Ms:  r = { { 0.0f, "0" }, { 0.5f, "100" }, { 1.0f, "200" } }; break;
        default:
            if (isFreq (s.kind)) r = { { 0.0f, hzShort (kindHz (s.kind, 0.0f)) }, { 0.5f, hzShort (kindHz (s.kind, 5.0f)) }, { 1.0f, hzShort (kindHz (s.kind, 10.0f)) } };
            else for (int i = 0; i <= 10; ++i) r.push_back ({ (float) i / 10.0f, String (s.rev ? 10 - i : i) });
    }
    return r;
}

String readoutText (const KnobSpec& s, float v)
{
    switch (s.kind)
    {
        case Bi:  return String (v > 0.0f ? "+" : "") + String (v, 1) + " dB";
        case Ms:  return String (roundToInt (csg::preDelayMs (v))) + " ms";
        case Sel: return csg::kDivNames[jlimit (0, 10, roundToInt (v))];
        default:  return isFreq (s.kind) ? hzLong (kindHz (s.kind, v)) : String();
    }
}

// Large hardware-style knob: printed scale around it, on/off light, bordered label plate, value readout.
class Knob : public Slider
{
public:
    static constexpr int kW = 136, kH = 170;

    Knob (AudioProcessorValueTreeState& apvts, const KnobSpec& s, Colour capColour, Colour textColour)
        : spec (s), cap (capColour), text (textColour), labels (tickLabels (s))
    {
        setSliderStyle (Slider::RotaryVerticalDrag);
        setTextBoxStyle (Slider::NoTextBox, false, 0, 0);
        setMouseDragSensitivity (170);
        attachment = std::make_unique<AudioProcessorValueTreeState::SliderAttachment> (apvts, s.id, *this);
        setDoubleClickReturnValue (true, s.def);
    }

    void paint (Graphics& g) override
    {
        const float prop = (float) valueToProportionOfLength (getValue());
        const float sg = spec.ccw ? -1.0f : 1.0f;
        auto angle = [&] (float q) { return sg * degreesToRadians (-135.0f + 270.0f * q); };
        const bool sel = (spec.kind == Sel);

        {
            Graphics::ScopedSaveState ss (g);
            g.addTransform (AffineTransform::scale ((float) kW / 72.0f).translated ((float) (getWidth() - kW) * 0.5f, 0.0f));
            const float c = 36.0f;

            g.setColour (text);
            if (! sel)
                for (int i = 0; i <= 10; ++i)
                {
                    const float a = angle ((float) i / 10.0f), s = std::sin (a), co = std::cos (a);
                    g.drawLine (c + 28.5f * s, c - 28.5f * co, c + 33.0f * s, c - 33.0f * co, 1.0f);
                }
            const float R = sel ? 27.5f : 23.5f, fs = sel ? 3.7f : (spec.kind == Num ? 5.4f : 5.8f);
            g.setFont (plainFont (fs));
            for (auto& l : labels)
            {
                const float a = angle (l.pos);
                g.drawText (l.text, Rectangle<float> (c + R * std::sin (a) - 7.0f, c - R * std::cos (a) - 3.0f, 14.0f, 6.0f), Justification::centred, false);
            }

            g.setColour (cap);
            g.fillEllipse (c - 17, c - 17, 34, 34);
            g.setColour (Colours::black);
            g.drawEllipse (c - 17, c - 17, 34, 34, 1.5f);
            g.setColour (Colours::white.withAlpha (0.25f));
            g.drawEllipse (c - 13, c - 13, 26, 26, 1.0f);

            const float a = angle (prop);
            Path p;
            p.startNewSubPath (c, c);
            p.lineTo (c + 15.5f * std::sin (a), c - 15.5f * std::cos (a));
            g.setColour (Colours::white);
            g.strokePath (p, PathStrokeType (2.6f, PathStrokeType::curved, PathStrokeType::rounded));

            if (! sel)   // on/off light: dark at zero, lit at constant brightness otherwise
            {
                const bool on = spec.kind == Bi ? std::fabs (getValue()) > 0.001 : getValue() > 0.001;
                if (on) { g.setColour (kAmber.withAlpha (0.30f)); g.fillEllipse (66 - 5.5f, 6 - 5.5f, 11, 11); }
                g.setColour (on ? kAmber : Colour (0xff2c2d30));
                g.fillEllipse (66 - 2.8f, 6 - 2.8f, 5.6f, 5.6f);
                g.setColour (Colours::black);
                g.drawEllipse (66 - 2.8f, 6 - 2.8f, 5.6f, 5.6f, 0.6f);
            }
        }

        // bordered label plate and readout
        const Rectangle<float> plate (((float) getWidth() - 88.0f) * 0.5f, 126.0f, 88.0f, 19.0f);
        g.setColour (Colours::black.withAlpha (0.2f));
        g.fillRoundedRectangle (plate, 3.0f);
        g.setColour (text);
        g.drawRoundedRectangle (plate, 3.0f, 1.0f);
        g.setFont (panelFont (12.0f, false));
        g.drawText (spec.label, plate, Justification::centred, false);

        g.setColour (kAmber);
        g.setFont (plainFont (10.5f));
        g.drawText (readoutText (spec, (float) getValue()), Rectangle<int> (0, 148, getWidth(), 14), Justification::centred, false);
    }

    // Counterclockwise knobs (Input Filter high cut): dragging down turns the knob counterclockwise.
    void mouseDown (const MouseEvent& e) override { Slider::mouseDown (spec.ccw ? flipped (e) : e); }
    void mouseDrag (const MouseEvent& e) override { Slider::mouseDrag (spec.ccw ? flipped (e) : e); }
    void mouseUp   (const MouseEvent& e) override { Slider::mouseUp   (spec.ccw ? flipped (e) : e); }

private:
    static MouseEvent flipped (const MouseEvent& e) { return e.withNewPosition (Point<float> (e.position.x, -e.position.y)); }
    KnobSpec spec; Colour cap, text; std::vector<Lab> labels;
    std::unique_ptr<AudioProcessorValueTreeState::SliderAttachment> attachment;
};

// ============================================================== buttons and switches
class ChoiceButtons : public Component
{
public:
    ChoiceButtons (AudioProcessorValueTreeState& apvts, const char* paramId, StringArray names, int columns)
        : labels (names), cols (columns)
    {
        auto* p = apvts.getParameter (paramId);
        jassert (p != nullptr);
        attachment = std::make_unique<ParameterAttachment> (*p, [this] (float v) { selected = (int) std::lround (v); repaint(); });
        attachment->sendInitialUpdate();
    }

    int heightFor() const { const int rows = (labels.size() + cols - 1) / cols; return rows * 26 + (rows - 1) * 5; }

    void paint (Graphics& g) override
    {
        for (int i = 0; i < labels.size(); ++i)
        {
            auto r = cell (i).toFloat();
            const bool on = (i == selected);
            g.setColour (Colour (0xff222222));
            if (! on) g.fillRoundedRectangle (r.translated (0, 2), 3.0f);
            if (on) r = r.translated (0, 1);
            g.setGradientFill (on ? ColourGradient (Colour (0xff8e8f93), 0, r.getY(), Colour (0xffc7c9cc), 0, r.getBottom(), false)
                                  : ColourGradient (Colour (0xffcfd1d4), 0, r.getY(), Colour (0xff8d8f94), 0, r.getBottom(), false));
            g.fillRoundedRectangle (r, 3.0f);
            g.setColour (Colour (0xff222222));
            g.drawRoundedRectangle (r, 3.0f, 1.0f);
            if (on) { g.setColour (Colours::black.withAlpha (0.25f)); g.fillRect (r.getX() + 1, r.getY() + 1, r.getWidth() - 2, 3.0f); }
            g.setColour (Colour (0xff111111));
            g.setFont (panelFont (12.0f, true));
            g.drawText (labels[i], r, Justification::centred, false);
        }
    }

    void mouseDown (const MouseEvent& e) override
    {
        for (int i = 0; i < labels.size(); ++i)
            if (cell (i).contains (e.getPosition())) { attachment->setValueAsCompleteGesture ((float) i); return; }
    }

private:
    Rectangle<int> cell (int i) const
    {
        const int gap = 5, w = (getWidth() - (cols - 1) * gap) / cols;
        return { (i % cols) * (w + gap), (i / cols) * 31, w, 26 };
    }
    StringArray labels; int cols; int selected = 0;
    std::unique_ptr<ParameterAttachment> attachment;
};

class ChoiceSwitch : public Component
{
public:
    ChoiceSwitch (AudioProcessorValueTreeState& apvts, const char* paramId, String top, String bottom, Colour textColour)
        : a (top), b (bottom), text (textColour)
    {
        auto* p = apvts.getParameter (paramId);
        jassert (p != nullptr);
        attachment = std::make_unique<ParameterAttachment> (*p, [this] (float v) { selected = (int) std::lround (v); repaint(); });
        attachment->sendInitialUpdate();
    }

    void paint (Graphics& g) override
    {
        const float cx = getWidth() * 0.5f;
        g.setFont (panelFont (10.0f, selected == 0));
        g.setColour (text.withAlpha (selected == 0 ? 1.0f : 0.55f));
        g.drawText (a, Rectangle<int> (0, 0, getWidth(), 12), Justification::centred, false);
        g.setFont (panelFont (10.0f, selected == 1));
        g.setColour (text.withAlpha (selected == 1 ? 1.0f : 0.55f));
        g.drawText (b, Rectangle<int> (0, getHeight() - 12, getWidth(), 12), Justification::centred, false);

        const Rectangle<float> slot (cx - 5, 16, 10, 34);
        g.setColour (Colour (0xff17171a));
        g.fillRoundedRectangle (slot, 5.0f);
        const float ty = selected == 0 ? slot.getY() + 1 : slot.getBottom() - 17;
        Rectangle<float> thumb (cx - 8, ty, 16, 16);
        g.setGradientFill (ColourGradient (Colour (0xff999999), thumb.getX(), 0, Colour (0xff777777), thumb.getRight(), 0, false));
        g.fillRoundedRectangle (thumb, 6.0f);
        g.setColour (Colours::white.withAlpha (0.55f));
        g.fillRoundedRectangle (thumb.reduced (5, 1).withHeight (14), 3.0f);
        g.setColour (Colour (0xff222222));
        g.drawRoundedRectangle (thumb, 6.0f, 1.0f);
    }

    void mouseDown (const MouseEvent&) override { attachment->setValueAsCompleteGesture (selected == 0 ? 1.0f : 0.0f); }

private:
    String a, b; Colour text; int selected = 0;
    std::unique_ptr<ParameterAttachment> attachment;
};

// Power switch with a red light right under the module name: lit when the module is on.
class PowerSwitch : public Component
{
public:
    PowerSwitch (AudioProcessorValueTreeState& apvts, const char* paramId)
    {
        auto* p = apvts.getParameter (paramId);
        jassert (p != nullptr);
        attachment = std::make_unique<ParameterAttachment> (*p, [this] (float v) { on = v > 0.5f; repaint(); });
        attachment->sendInitialUpdate();
        setMouseCursor (MouseCursor::PointingHandCursor);
    }

    void paint (Graphics& g) override
    {
        const float ly = (float) getHeight() * 0.5f, lx = 10.0f;
        if (on)
        {
            g.setColour (Colour (0xffff2a2a).withAlpha (0.30f));
            g.fillEllipse (lx - 10, ly - 10, 20, 20);
        }
        g.setGradientFill (on ? ColourGradient (Colour (0xffffb0b0), lx - 2, ly - 2, Colour (0xffa50d0d), lx + 6, ly + 6, true)
                              : ColourGradient (Colour (0xff4a1010), lx - 2, ly - 2, Colour (0xff2a0808), lx + 6, ly + 6, true));
        g.fillEllipse (lx - 6, ly - 6, 12, 12);
        g.setColour (Colours::black);
        g.drawEllipse (lx - 6, ly - 6, 12, 12, 1.0f);

        const Rectangle<float> track (26.0f, ly - 9.0f, 36.0f, 18.0f);
        g.setColour (Colour (0xff0d0e10));
        g.fillRoundedRectangle (track, 9.0f);
        g.setColour (Colours::black);
        g.drawRoundedRectangle (track, 9.0f, 1.0f);
        const float tx = on ? track.getRight() - 15.0f : track.getX() + 3.0f;
        g.setGradientFill (ColourGradient (Colour (0xffe6e7ea), 0, ly - 6, Colour (0xff8d8f94), 0, ly + 6, false));
        g.fillEllipse (tx, ly - 6.0f, 12.0f, 12.0f);
    }

    void mouseDown (const MouseEvent&) override { attachment->setValueAsCompleteGesture (on ? 0.0f : 1.0f); }

private:
    bool on = true;
    std::unique_ptr<ParameterAttachment> attachment;
};

// ============================================================== meters
// Gain meter: IN and OUT ladders in dBFS, and a GR ladder that hangs from the top in dB of reduction.
class GainMeter : public Component
{
public:
    static constexpr int kW = 200, kH = 150;
    explicit GainMeter (float maxGr) : grMax (maxGr) {}

    void update (bool isActive, float inDb, float grDb, float outDb)
    {
        auto follow = [] (float& cur, float target, float rel)
        {
            if (target > cur) cur = target; else cur += rel * (target - cur);
        };
        active = isActive;
        if (! active) { inS = outS = -99.0f; grS = 0.0f; repaint(); return; }
        const float a = inS, b = grS, c = outS;
        follow (inS, inDb, 0.25f); follow (outS, outDb, 0.25f);
        if (grDb > grS) grS += 0.6f * (grDb - grS); else grS += 0.15f * (grDb - grS);
        if (std::fabs (a - inS) > 0.05f || std::fabs (b - grS) > 0.02f || std::fabs (c - outS) > 0.05f) repaint();
    }

    void paint (Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();
        g.setColour (Colour (0xff0d0e10));
        g.fillRoundedRectangle (r, 4.0f);
        g.setColour (Colours::black);
        g.drawRoundedRectangle (r.reduced (0.5f), 4.0f, 1.0f);

        const float xs[3] = { r.getWidth() * 0.19f, r.getWidth() * 0.5f, r.getWidth() * 0.81f };
        const char* titles[3] = { "IN", "GR dB", "OUT" };
        const float top = 22.0f;
        for (int c = 0; c < 3; ++c)
        {
            g.setColour (Colour (0xffcfd2d6));
            g.setFont (panelFont (10.5f, true));
            g.drawText (titles[c], Rectangle<float> (xs[c] - 30, 3, 60, 13), Justification::centred, false);

            // tick labels left of the ladder
            g.setColour (Colour (0xff8f939a));
            g.setFont (plainFont (8.0f));
            const String t0 = "0", t1 = c == 1 ? "-" + String (roundToInt (grMax * 0.5f)) : "-18", t2 = c == 1 ? "-" + String (roundToInt (grMax)) : "-36";
            g.drawText (t0, Rectangle<float> (xs[c] - 28, top - 2, 17, 9), Justification::centredRight, false);
            g.drawText (t1, Rectangle<float> (xs[c] - 28, top + 56, 17, 9), Justification::centredRight, false);
            g.drawText (t2, Rectangle<float> (xs[c] - 28, top + 112, 17, 9), Justification::centredRight, false);

            for (int s = 0; s < 12; ++s)
            {
                bool lit = false; Colour col = kGreen;
                float y;
                if (c == 1)
                {
                    y = top + (float) s * 10.0f;
                    lit = active && grS >= ((float) s + 0.5f) * grMax / 12.0f;
                    col = s < 6 ? kAmber : kRed;
                }
                else
                {
                    const int i = 11 - s;
                    y = top + (float) s * 10.0f;
                    const float db = c == 0 ? inS : outS;
                    lit = active && db >= -36.0f + 3.0f * (float) i;
                    col = i < 8 ? kGreen : (i < 10 ? kYellow : kRed);
                }
                const Rectangle<float> seg (xs[c] - 9.0f, y, 20.0f, 8.0f);
                if (lit) { g.setColour (col.withAlpha (0.30f)); g.fillRoundedRectangle (seg.expanded (2), 2.0f); }
                g.setColour (lit ? col : Colour (0xff1d1e21));
                g.fillRoundedRectangle (seg, 1.0f);
                g.setColour (Colours::black);
                g.drawRoundedRectangle (seg, 1.0f, 0.8f);
            }

            g.setColour (kAmber);
            g.setFont (plainFont (11.0f));
            String v = "-";
            if (active) v = c == 1 ? "-" + String (grS, 1) : String (jmax (-99.0f, c == 0 ? inS : outS), 1);
            g.drawText (v, Rectangle<float> (xs[c] - 28, top + 124, 56, 14), Justification::centred, false);
        }
    }

private:
    float grMax; bool active = true; float inS = -99.0f, outS = -99.0f, grS = 0.0f;
};

// Round LED bulbs like API hardware, laid out in a row: green, then yellow, then red, with peak hold and a clip lamp.
class LedMeter : public Component
{
public:
    static constexpr int kBulbs = 11, kStep = 24, kW = kBulbs * kStep + 76, kH = 74;
    explicit LedMeter (String t) : title (t) {}

    void update (float linearLevel, bool clipped)
    {
        const float db = Decibels::gainToDecibels (linearLevel, -100.0f);
        int lit = -1;
        for (int i = 0; i < kBulbs; ++i) if (db >= thresholds[i]) lit = i;
        if (lit >= holdIdx) { holdIdx = lit; holdTicks = 36; }
        else if (--holdTicks <= 0) holdIdx = lit;
        if (clipped) clipLatched = true;
        if (lit != litIdx || holdIdx != lastHold || clipLatched != lastClip) { litIdx = lit; lastHold = holdIdx; lastClip = clipLatched; repaint(); }
    }

    void mouseDown (const MouseEvent&) override { clipLatched = false; repaint(); }

    void paint (Graphics& g) override
    {
        auto lamp = [&] (float x, float y, float d, Colour c, bool on)
        {
            if (on) { g.setColour (c.withAlpha (0.28f)); g.fillEllipse (x - d * 0.4f, y - d * 0.4f, d * 1.8f, d * 1.8f); }
            g.setGradientFill (ColourGradient (on ? c.brighter (0.5f) : Colour (0xff3a3b3f), x + d * 0.3f, y + d * 0.2f,
                                               on ? c.darker (0.2f) : Colour (0xff17181a), x + d, y + d, true));
            g.fillEllipse (x, y, d, d);
            g.setColour (Colours::black);
            g.drawEllipse (x, y, d, d, 1.0f);
        };
        g.setColour (Colour (0xffcfd2d6));
        g.setFont (panelFont (14.0f, true));
        g.drawText (title, Rectangle<int> (0, 0, kBulbs * kStep, 18), Justification::centred, false);
        for (int i = 0; i < kBulbs; ++i)
        {
            const Colour c = i < 6 ? kGreen : (i < 9 ? kYellow : kRed);
            lamp ((float) (4 + i * kStep), 28.0f, 15.0f, c, i <= litIdx || i == holdIdx);
        }
        const float cx = (float) (kBulbs * kStep + 18);
        lamp (cx, 28.0f, 15.0f, kRed, clipLatched);
        g.setColour (Colour (0xffcfd2d6)); g.setFont (plainFont (8.5f));
        g.drawText ("CLIP", Rectangle<float> (cx - 12, 46, 39, 10), Justification::centred, false);
    }

private:
    String title;
    const float thresholds[kBulbs] = { -48, -40, -32, -26, -20, -15, -11, -8, -5, -3, -1.5f };
    int litIdx = -1, holdIdx = -1, lastHold = -2, holdTicks = 0;
    bool clipLatched = false, lastClip = false;
};

// Input (top left) or Output (top right) level: a centred -12 to +12 knob and a bulb meter. No power switch.
class IoPanel : public Component
{
public:
    IoPanel (ClassicStudioGProcessor& p, bool isInput)
        : proc (p), input (isInput),
          knob (p.apvts, K (isInput ? "in_gain" : "out_gain", isInput ? "In" : "Out", Bi), Colour (0xffc9ccd0), Colour (0xffc9ccd0)),
          meter (isInput ? "INPUT" : "OUTPUT")
    {
        addAndMakeVisible (knob);
        addAndMakeVisible (meter);
    }

    static int widthNeeded() { return Knob::kW + 28 + LedMeter::kW; }

    void resized() override
    {
        const int my = (getHeight() - LedMeter::kH) / 2;
        if (input) { knob.setBounds (0, 0, Knob::kW, Knob::kH); meter.setBounds (Knob::kW + 28, my, LedMeter::kW, LedMeter::kH); }
        else       { meter.setBounds (0, my, LedMeter::kW, LedMeter::kH); knob.setBounds (getWidth() - Knob::kW, 0, Knob::kW, Knob::kH); }
    }

    void tick()
    {
        if (input) meter.update (proc.inLevel.load(),  proc.inClip.exchange (false));
        else       meter.update (proc.outLevel.load(), proc.outClip.exchange (false));
    }

private:
    ClassicStudioGProcessor& proc; bool input;
    Knob knob; LedMeter meter;
};

// ============================================================== logos and glowing labels
enum Logo { LTube, LTape, LTrans, LPlate, LSlap, LWidth };

Colour stateColour (char st)
{
    switch (st) { case 'g': return kGreen; case 'y': return kYellow; case 'r': return kRed; default: return Colour (0xff4d4f55); }
}

void strokeD (Graphics& g, const char* d, float sw, float alpha, Colour c, float scale, Point<float> origin)
{
    Path p = Drawable::parseSVGPath (d);
    p.applyTransform (AffineTransform::scale (scale).translated (origin.x, origin.y));
    g.setColour (c.withMultipliedAlpha (alpha));
    g.strokePath (p, PathStrokeType (sw * scale, PathStrokeType::curved, PathStrokeType::rounded));
}

void drawLogo (Graphics& g, int logo, Rectangle<float> area, Colour c)
{
    const float sc = area.getWidth() / 32.0f;
    const Point<float> o = area.getPosition();
    auto circle = [&] (float x, float y, float r)
    {
        Path p; p.addEllipse (x - r, y - r, r * 2, r * 2);
        p.applyTransform (AffineTransform::scale (sc).translated (o.x, o.y));
        g.setColour (c); g.strokePath (p, PathStrokeType (1.8f * sc));
    };
    auto rect = [&] (float x, float y, float w, float h, float rad)
    {
        Path p; p.addRoundedRectangle (x, y, w, h, rad);
        p.applyTransform (AffineTransform::scale (sc).translated (o.x, o.y));
        g.setColour (c); g.strokePath (p, PathStrokeType (1.8f * sc));
    };
    switch (logo)
    {
        case LTube:
            strokeD (g, "M11 28h10M12 28v-3M20 28v-3M10 25V13a6 6 0 0 1 12 0v12z", 1.8f, 1, c, sc, o);
            strokeD (g, "M13 22v-6M16 22v-8M19 22v-6", 1.8f, 1, c, sc, o);
            break;
        case LTape:
            rect (4, 9, 24, 15, 2); circle (11, 16, 3.5f); circle (21, 16, 3.5f);
            strokeD (g, "M9 24l2-3h10l2 3", 1.8f, 1, c, sc, o);
            break;
        case LTrans:
            circle (16, 16, 11);
            strokeD (g, "M12 9v14M12 14l8-5M12 18l8 5M17 21l3 2-1-3", 1.8f, 1, c, sc, o);
            break;
        case LPlate:
            rect (8, 9, 16, 14, 1);
            strokeD (g, "M4 6l4 3M28 6l-4 3M4 26l4-3M28 26l-4-3", 1.8f, 1, c, sc, o);
            break;
        case LSlap:
            strokeD (g, "M6 7v18",  4.0f, 1.0f,  c, sc, o);
            strokeD (g, "M14 10v12", 3.0f, 0.7f,  c, sc, o);
            strokeD (g, "M21 13v6", 2.4f, 0.45f, c, sc, o);
            strokeD (g, "M26 15v2", 2.0f, 0.25f, c, sc, o);
            break;
        default:
            strokeD (g, "M16 5v22", 1.8f, 1, c, sc, o);
            strokeD (g, "M13 16H4M7 12l-4 4 4 4M19 16h9M25 12l4 4-4 4", 1.8f, 1, c, sc, o);
            break;
    }
}

class LogoComp : public Component
{
public:
    explicit LogoComp (int l) : logo (l) {}
    void setState (char s) { if (s != state) { state = s; repaint(); } }
    void paint (Graphics& g) override
    {
        const Colour c = stateColour (state);
        auto r = getLocalBounds().toFloat().reduced (2);
        if (state != 'n') { g.setColour (c.withAlpha (0.16f)); g.fillEllipse (r.expanded (1)); }
        drawLogo (g, logo, r, c);
    }
private:
    int logo; char state = 'n';
};

class DispLabel : public Component
{
public:
    explicit DispLabel (String t) : text (t) {}
    void setState (char s) { if (s != state) { state = s; repaint(); } }
    void setText (const String& t) { if (t != text) { text = t; repaint(); } }
    void paint (Graphics& g) override
    {
        const bool on = state != 'n';
        const Colour c = on ? stateColour (state) : kAmber.withAlpha (0.4f);
        auto r = getLocalBounds().toFloat().reduced (1);
        g.setColour (on ? Colour (0xff0a0c0a) : Colour (0xff120a02));
        g.fillRoundedRectangle (r, 2.0f);
        if (on) { g.setColour (c.withAlpha (0.22f)); g.fillRoundedRectangle (r.expanded (2), 4.0f); g.setColour (Colour (0xff0a0c0a)); g.fillRoundedRectangle (r, 2.0f); }
        g.setColour (c);
        g.setFont (panelFont (16.0f, false));
        g.drawText (text, r, Justification::centred, false);
    }
private:
    String text; char state = 'n';
};

// ============================================================== API 560 graphic EQ
class EqRow : public Component
{
public:
    EqRow (AudioProcessorValueTreeState& apvts, const char* paramId, String freqLabel)
        : label (freqLabel)
    {
        auto* p = apvts.getParameter (paramId);
        jassert (p != nullptr);
        attachment = std::make_unique<ParameterAttachment> (*p, [this] (float v) { value = v; repaint(); });
        attachment->sendInitialUpdate();
    }

    static constexpr float kLeft = 42.0f, kRight = 16.0f;
    float x0() const { return kLeft; }
    float x1() const { return (float) getWidth() - kRight; }

    void paint (Graphics& g) override
    {
        const float cy = (float) getHeight() * 0.5f, xm = 0.5f * (x0() + x1());
        g.setColour (Colour (0xffcfd2d6));
        g.setFont (plainFont (10.0f));
        g.drawText (label, Rectangle<float> (0, cy - 10, 32, 20), Justification::centredRight, false);

        g.setColour (Colour (0xff0a0b0d));
        g.fillRoundedRectangle (x0(), cy - 1.5f, x1() - x0(), 3.0f, 1.5f);
        g.setColour (Colour (0xffcfd2d6));
        g.fillRect (xm - 0.5f, cy - 6.0f, 1.0f, 12.0f);
        g.setFont (plainFont (7.0f));
        g.drawText ("0", Rectangle<float> (xm - 6, cy + 8, 12, 8), Justification::centred, false);

        const float x = x0() + (value + 12.0f) / 24.0f * (x1() - x0());
        Rectangle<float> cap (x - 6, cy - 10.5f, 12, 21);
        g.setColour (Colours::black.withAlpha (0.6f));
        g.fillRoundedRectangle (cap.translated (0, 2), 2.0f);
        g.setGradientFill (ColourGradient (Colour (0xffa9acb2), cap.getX(), 0, Colour (0xff8e9197), cap.getRight(), 0, false));
        g.fillRoundedRectangle (cap, 2.0f);
        g.setColour (Colour (0xfff1f2f4).withAlpha (0.8f));
        g.fillRect (cap.getCentreX() - 1.5f, cap.getY() + 1, 3.0f, cap.getHeight() - 2);
        g.setColour (Colour (0xff1b1c1e));
        g.drawRoundedRectangle (cap, 2.0f, 1.0f);
    }

    void mouseDown (const MouseEvent& e) override { attachment->beginGesture(); setFromX ((float) e.x); }
    void mouseDrag (const MouseEvent& e) override { setFromX ((float) e.x); }
    void mouseUp (const MouseEvent&) override { attachment->endGesture(); }
    void mouseDoubleClick (const MouseEvent&) override { attachment->setValueAsCompleteGesture (0.0f); }

private:
    void setFromX (float x)
    {
        float v = jlimit (-12.0f, 12.0f, (x - x0()) / (x1() - x0()) * 24.0f - 12.0f);
        v = std::round (v * 5.0f) / 5.0f;
        if (std::fabs (v) < 0.4f) v = 0.0f;
        attachment->setValueAsPartOfGesture (v);
    }
    String label; float value = 0;
    std::unique_ptr<ParameterAttachment> attachment;
};

// Highest frequency at the top, lowest at the bottom. Rows are spread to fill the whole module.
class EqPanel : public Component
{
public:
    explicit EqPanel (AudioProcessorValueTreeState& apvts)
    {
        static const char* names[10] = { "16k", "8k", "4k", "2k", "1k", "500", "250", "125", "63", "31" };
        for (int i = 0; i < 10; ++i)
        {
            rows.add (new EqRow (apvts, csg::kEqIds[9 - i], names[i]));
            addAndMakeVisible (rows.getLast());
        }
    }
    static int heightNeeded() { return 14 + 10 * 36 + 14 + 8 + 22; }

    void resized() override
    {
        const int area = getHeight() - 14 - 14 - 8 - 22;
        const int rh = area / 10;
        for (int i = 0; i < 10; ++i) rows[i]->setBounds (0, 14 + i * rh, getWidth(), rh);
    }

    void paint (Graphics& g) override
    {
        const int area = getHeight() - 14 - 14 - 8 - 22, rh = area / 10;
        const float x0 = EqRow::kLeft, x1 = (float) getWidth() - EqRow::kRight;
        auto scale = [&] (int y)
        {
            g.setColour (Colour (0xffcfd2d6)); g.setFont (plainFont (10.0f));
            const float xs[3] = { x0, 0.5f * (x0 + x1), x1 };
            const char* t[3] = { "\xe2\x88\x92" "12", "0", "+12" };
            for (int i = 0; i < 3; ++i)
                g.drawText (String::fromUTF8 (t[i]), Rectangle<float> (xs[i] - 16, (float) y, 32, 12), Justification::centred, false);
        };
        scale (0);
        scale (14 + 10 * rh + 2);
        const float cy = (float) (getHeight() - 11);   // small blue and red caps
        g.setColour (Colour (0xff2d6bd4)); g.fillEllipse (getWidth() * 0.5f - 22, cy - 7, 14, 14);
        g.setColour (Colour (0xffd43a2d)); g.fillEllipse (getWidth() * 0.5f + 8,  cy - 7, 14, 14);
        g.setColour (Colours::black);
        g.drawEllipse (getWidth() * 0.5f - 22, cy - 7, 14, 14, 1.0f);
        g.drawEllipse (getWidth() * 0.5f + 8,  cy - 7, 14, 14, 1.0f);
    }
private:
    OwnedArray<EqRow> rows;
};

// ============================================================== module definitions
struct Def
{
    const char* name; const char* sub; Colour bg, text, cap;
    std::vector<KnobSpec> knobs;
    std::vector<int> mains;          // knobs that set the logo colour (green / yellow / red)
    int logo = -1; const char* disp = nullptr;
    int gm = 0;                      // gain meter: 1 = 1176, 2 = LA-2A, 3 = De-esser
    bool led = false, eq = false;
};

Def mk (const char* n, const char* s, uint32 bg, uint32 cap, uint32 txt = 0xffeeeeee)
{
    Def d; d.name = n; d.sub = s; d.bg = Colour (bg); d.cap = Colour (cap); d.text = Colour (txt); return d;
}

std::vector<Def> makeDefs()
{
    using namespace csg;
    std::vector<Def> d ((size_t) kNumModules);
    d[mInputFilter] = mk ("Input Filter", "Neve 1073 style", 0xff53697e, 0xff2a2a2d);
    d[mInputFilter].knobs = { K ("if_low", "Low Cut", FInLow), K ("if_high", "High Cut", FInHigh, true) };

    d[mDeEsser] = mk ("De-esser", "Drawmer DS201 style", 0xff121213, 0xff2e2e31);
    d[mDeEsser].gm = 3; d[mDeEsser].knobs = { K ("ds_freq", "Freq", FDeEss), K ("ds_str", "Strength") };

    d[m1176] = mk ("1176", "FET compressor", 0xff7e7e83, 0xff1a1a1c);
    d[m1176].gm = 1;
    d[m1176].knobs = { K ("c_in", "Input"), K ("c_out", "Output"), K ("c_att", "Attack", Num, false, true, 10.0f), K ("c_rel", "Release", Num, false, true, 10.0f) };

    d[mLA2A] = mk ("LA-2A", "optical compressor", 0xffc9ccd0, 0xff2a2a2d, 0xff16171a);
    d[mLA2A].gm = 2; d[mLA2A].knobs = { K ("la_gain", "Gain"), K ("la_peak", "Peak Red.") };

    d[mEq] = mk ("API 560", "graphic EQ", 0xff2f3b4e, 0xff2a2a2d);
    d[mEq].eq = true;

    auto sat = [&] (int id, const char* n, const char* s, uint32 bg, uint32 cap, int logo, const char* a, const char* hp, const char* lp)
    {
        d[(size_t) id] = mk (n, s, bg, cap);
        d[(size_t) id].logo = logo; d[(size_t) id].mains = { 0 };
        d[(size_t) id].knobs = { K (a, "Amount"), K (hp, "High-pass", FSatHp), K (lp, "Low-pass", FSatLp) };
    };
    sat (mTube,       "Tube",       "Fairchild style", 0xff656a70, 0xff1c1d20, LTube,  "tube_amt", "tube_hp", "tube_lp");
    sat (mTape,       "Tape",       "Studer style",    0xff4b504c, 0xff17181a, LTape,  "tape_amt", "tape_hp", "tape_lp");
    sat (mTransistor, "Transistor", "Neve style",      0xff4b5a69, 0xff1c1d20, LTrans, "trn_amt",  "trn_hp",  "trn_lp");

    auto fx = [&] (int id, const char* n, const char* s, uint32 bg, uint32 cap, int logo, const char* disp,
                   const char* lv, const char* du, const char* lo, const char* hi)
    {
        d[(size_t) id] = mk (n, s, bg, cap);
        d[(size_t) id].logo = logo; d[(size_t) id].disp = disp; d[(size_t) id].mains = { 0 };
        d[(size_t) id].knobs = { K (lv, "Level"), K (du, "Duck"), K (lo, "Low Cut", FFxLow), K (hi, "High Cut", FFxHigh) };
    };
    fx (mPlate, "Plate", "EMT 140 style",         0xff67725f, 0xff1b1e19, LPlate, nullptr, "plate_lvl", "plate_duck", "plate_lo", "plate_hi");
    fx (mHall,  "Hall",  "Lexicon 480L style",    0xff1c1d1f, 0xff7a7d82, -1,     "HALL",  "hall_lvl",  "hall_duck",  "hall_lo",  "hall_hi");
    fx (mRoom,  "Room",  "Eventide SP2016 style", 0xff141518, 0xff2c2e33, -1,     "ROOM",  "room_lvl",  "room_duck",  "room_lo",  "room_hi");
    fx (mSlap,  "Slap",  "Space Echo style",      0xff3b3733, 0xff8a6a45, LSlap,  nullptr, "slap_lvl",  "slap_duck",  "slap_lo",  "slap_hi");
    fx (mDelay, "Delay", "Lexicon PCM 42 style",  0xff25272b, 0xff4a4d53, -1,     "1/4",   "dly_lvl",   "dly_duck",   "dly_lo",   "dly_hi");
    d[mHall].knobs.push_back (K ("hall_pre", "Pre-delay", Ms));
    d[mDelay].knobs.push_back (K ("dly_div", "Time", Sel, false, false, 2.0f));

    d[mWidth] = mk ("Width", "Bricasti style", 0xffd1d4d8, 0xff2a2a2d, 0xff16171a);
    d[mWidth].logo = LWidth; d[mWidth].mains = { 0, 1, 2 };
    d[mWidth].knobs = { K ("w_haas", "Haas", Pct), K ("w_det", "Detune", Pct), K ("w_ms", "Mid-Side", Pct) };
    return d;
}

} // namespace

// ============================================================== layout metrics
namespace
{
constexpr int kModW = 292, kGap = 6, kRowPadX = 14, kRowPadY = 8, kChassisX = 18, kChassisTop = 14,
              kPlateH = 206, kPlateGap = 12, kRowGap = 10, kBottom = 20, kHeaderH = 84, kItemGap = 8, kSide = 8;
}

// ============================================================== one module column
class ModuleComponent : public Component
{
public:
    ModuleComponent (ClassicStudioGEditor& e, int moduleId, const Def& d)
        : ed (e), id (moduleId), def (d)
    {
        auto& apvts = ed.proc.apvts;

        power = new PowerSwitch (apvts, csg::kOnIds[id]);
        owned.add (power);
        addAndMakeVisible (power);

        if (def.logo >= 0) logo = new LogoComp (def.logo);
        if (def.disp != nullptr) disp = new DispLabel (def.disp);
        if (def.gm != 0) gmeter = new GainMeter (def.gm == 3 ? 12.0f : 20.0f);

        // knobs
        struct Cell { Component* c; int w, h; };
        std::vector<Cell> cells;
        for (auto& k : def.knobs)
        {
            auto* kn = new Knob (apvts, k, def.cap, def.text);
            owned.add (kn); addAndMakeVisible (kn);
            cells.push_back ({ kn, Knob::kW, Knob::kH });
        }

        // an odd knob count leaves a free cell: the logo, glowing label or LED meter goes there
        Component* extra = nullptr; int ew = 0, eh = 0;
        if (cells.size() % 2 == 1 && logo) { extra = logo; ew = 56; eh = 56; }
        else if (cells.size() % 2 == 1 && disp) { extra = disp; ew = 92; eh = 30; }

        if (logo && extra != logo) add ({ { logo, 44, 44 } });
        if (disp && extra != disp) add ({ { disp, 92, 30 } });
        if (gmeter) add ({ { gmeter, GainMeter::kW, GainMeter::kH } });
        if (extra) { cells.push_back ({ extra, ew, eh }); owned.add (extra); addAndMakeVisible (extra); }
        for (auto* c : { (Component*) logo, (Component*) disp, (Component*) gmeter })
            if (c != nullptr && ! owned.contains (c)) { owned.add (c); addAndMakeVisible (c); }

        for (size_t i = 0; i < cells.size(); i += 2)
        {
            if (i + 1 < cells.size()) add ({ { cells[i].c, cells[i].w, cells[i].h }, { cells[i + 1].c, cells[i + 1].w, cells[i + 1].h } });
            else                      add ({ { cells[i].c, cells[i].w, cells[i].h } });
        }

        if (id == csg::m1176)
        {
            auto* ratio = new ChoiceButtons (apvts, "c_ratio", { "4", "8", "12", "20", "All" }, 5);
            add ({ { ratio, kModW - 2 * kSide - 12, ratio->heightFor() } }); owned.add (ratio); addAndMakeVisible (ratio);
            auto* meter = new ChoiceButtons (apvts, "c_meter", { "GR", "+8", "Off" }, 3);
            add ({ { meter, kModW - 2 * kSide - 12, meter->heightFor() } }); owned.add (meter); addAndMakeVisible (meter);
        }
        if (id == csg::mLA2A)
        {
            auto* a = new ChoiceSwitch (apvts, "la_mode",  "Compress", "Limit", def.text);
            auto* b = new ChoiceSwitch (apvts, "la_meter", "Meter GR", "Meter Out", def.text);
            add ({ { a, 84, 62 }, { b, 84, 62 } });
            for (auto* c : { (Component*) a, (Component*) b }) { owned.add (c); addAndMakeVisible (c); }
        }
        if (def.eq)
        {
            auto* e = new EqPanel (apvts);
            owned.add (e); addAndMakeVisible (e);
            add ({ { e, kModW - 2 * kSide, EqPanel::heightNeeded() } });
            items.back().fill = true;
        }

        setMouseCursor (MouseCursor::DraggingHandCursor);
        updateState();
    }

    int requiredHeight() const
    {
        int h = kHeaderH;
        for (auto& it : items) h += it.height() + kItemGap;
        return h + 4;
    }

    void resized() override
    {
        power->setBounds ((getWidth() - 66) / 2, 40, 66, 22);
        int y = kHeaderH;
        const int cellW = (getWidth() - 2 * kSide) / 2;
        for (auto& it : items)
        {
            if (it.fill)
            {
                it.e[0].c->setBounds (kSide, y, getWidth() - 2 * kSide, getHeight() - y - 12);
                continue;
            }
            const int rh = it.height();
            if (it.e.size() == 1)
                it.e[0].c->setBounds ((getWidth() - it.e[0].w) / 2, y + (rh - it.e[0].h) / 2, it.e[0].w, it.e[0].h);
            else
                for (size_t i = 0; i < 2; ++i)
                    it.e[i].c->setBounds (kSide + (int) i * cellW + (cellW - it.e[i].w) / 2, y + (rh - it.e[i].h) / 2, it.e[i].w, it.e[i].h);
            y += rh + kItemGap;
        }
    }

    void paint (Graphics& g) override
    {
        auto b = getLocalBounds().toFloat();
        g.setColour (def.bg);
        g.fillRoundedRectangle (b, 3.0f);
        for (float x = 0; x < b.getWidth(); x += 3.0f)       // brushed vertical grain
        {
            g.setColour (Colours::white.withAlpha (0.045f));
            g.fillRect (x, 0.0f, 1.0f, b.getHeight());
        }
        g.setColour (Colours::white.withAlpha (0.22f));  g.fillRect (1.0f, 1.0f, b.getWidth() - 2, 1.0f);
        g.setColour (Colours::black.withAlpha (0.30f));  g.fillRect (1.0f, b.getHeight() - 3, b.getWidth() - 2, 2.0f);
        g.setColour (Colours::black.withAlpha (0.55f));  g.drawRoundedRectangle (b.reduced (0.5f), 3.0f, 1.0f);
        drawScrew (g, 8.5f, 8.5f, 3.5f);
        drawScrew (g, b.getWidth() - 8.5f, 8.5f, 3.5f);

        g.setColour (def.text);
        g.setFont (panelFont (21.0f, true));
        g.drawText (def.name, Rectangle<int> (6, 12, getWidth() - 12, 26), Justification::centred, false);
        g.setColour (def.text.withAlpha (0.75f));
        g.setFont (panelFont (11.0f, false));
        g.drawFittedText (def.sub, Rectangle<int> (4, 64, getWidth() - 8, 14), Justification::centred, 1);

        if (dragging) { g.setColour (Colours::white.withAlpha (0.12f)); g.fillRoundedRectangle (b, 3.0f); }
    }

    void tick()
    {
        updateState();
        auto& p = ed.proc;
        const bool isOn = p.param (csg::kOnIds[id]) > 0.5f;

        if (gmeter != nullptr)
        {
            auto db = [] (float lin) { return Decibels::gainToDecibels (lin, -100.0f); };
            if (def.gm == 1)
                gmeter->update (isOn && (int) p.param ("c_meter") != 2, db (p.fetInLevel.load()), p.grFet.load(), db (p.fetOutLevel.load()));
            else if (def.gm == 2)
                gmeter->update (isOn, db (p.optoInLevel.load()), p.grOpto.load(), db (p.optoOutLevel.load()));
            else
                gmeter->update (isOn, db (p.dsInLevel.load()), p.grDeEss.load(), db (p.dsOutLevel.load()));
        }
    }

    void mouseDown (const MouseEvent& e) override
    {
        dragging = true; dragOffset = e.getPosition();
        toFront (false);
        repaint();
    }
    void mouseDrag (const MouseEvent& e) override
    {
        if (! dragging) return;
        const auto pe = e.getEventRelativeTo (getParentComponent());
        setTopLeftPosition (pe.getPosition() - dragOffset);
    }
    void mouseUp (const MouseEvent&) override
    {
        if (! dragging) return;
        dragging = false;
        const int from = ed.slotOfModule (id);
        const int to = ed.slotAtPoint (getBounds().getCentre());
        ed.moveModule (from, to);     // also snaps every module back into its slot
        repaint();
    }

private:
    struct Entry { Component* c; int w, h; };
    struct Item
    {
        std::vector<Entry> e; bool fill = false;
        int height() const { int h = 0; for (auto& x : e) h = std::max (h, x.h); return h; }
    };
    void add (std::vector<Entry> e) { Item it; it.e = std::move (e); items.push_back (std::move (it)); }

    void updateState()
    {
        const bool isOn = ed.proc.param (csg::kOnIds[id]) > 0.5f;
        if (isOn != lastOn)
        {
            lastOn = isOn;
            for (auto* c : owned) if (c != power) c->setAlpha (isOn ? 1.0f : 0.42f);
        }
        if (logo == nullptr && disp == nullptr) return;
        bool any = false; float mx = 0;
        for (auto& k : def.knobs)
            if (k.kind != Sel && k.kind != Bi && ed.proc.param (k.id) > 0.001f) any = true;
        for (int i : def.mains)
        {
            const auto& k = def.knobs[(size_t) i];
            const float v = ed.proc.param (k.id) * (k.kind == Pct ? 0.1f : 1.0f);
            mx = std::max (mx, v);
        }
        const char st = (! isOn || ! any) ? 'n' : mx <= 4.0f ? 'g' : mx <= 7.0f ? 'y' : 'r';
        if (logo) logo->setState (st);
        if (disp)
        {
            disp->setState (st);
            if (id == csg::mDelay) disp->setText (csg::kDivNames[jlimit (0, 10, (int) ed.proc.param ("dly_div"))]);
        }
    }

    ClassicStudioGEditor& ed;
    int id; Def def;
    OwnedArray<Component> owned;
    std::vector<Item> items;
    PowerSwitch* power = nullptr;
    LogoComp* logo = nullptr; DispLabel* disp = nullptr; GainMeter* gmeter = nullptr;
    bool dragging = false, lastOn = true; Point<int> dragOffset;
};

// ============================================================== editor
struct ClassicStudioGEditor::Impl
{
    struct Canvas : public Component
    {
        explicit Canvas (Impl& i) : impl (i) {}
        void paint (Graphics& g) override
        {
            const auto b = getLocalBounds().toFloat();
            // metal chassis
            g.setColour (Colour (0xff404145)); g.fillRect (b);
            for (float x = 0; x < b.getWidth(); x += 7.0f)
            {
                g.setColour (Colour (0xff3d3e42)); g.fillRect (x, 0.0f, 2.0f, b.getHeight());
                g.setColour (Colour (0xff47484c)); g.fillRect (x + 2.0f, 0.0f, 2.0f, b.getHeight());
            }
            g.setColour (Colour (0xff6a6b70)); g.fillRect (0.0f, 0.0f, b.getWidth(), 2.0f);
            g.setColour (Colour (0xff202124)); g.drawRect (b, 2.0f);

            // title plate with cursive lettering
            const Rectangle<float> plate ((float) kChassisX, (float) kChassisTop, b.getWidth() - 2.0f * kChassisX, (float) kPlateH);
            g.setGradientFill (ColourGradient (Colour (0xff26272a), 0, plate.getY(), Colour (0xff16171a), 0, plate.getBottom(), false));
            g.fillRoundedRectangle (plate, 6.0f);
            g.setColour (Colour (0xff0b0b0c)); g.drawRoundedRectangle (plate, 6.0f, 1.0f);
            g.setColour (Colour (0xff55565b)); g.fillRect (plate.getX() + 6, plate.getY() + 1, plate.getWidth() - 12, 1.0f);
            const Font script (FontOptions ("Snell Roundhand", 56.0f, Font::plain));
            g.setFont (script);
            g.setColour (Colour (0xff8a7a45));
            g.drawText ("Classic Studio G", plate.translated (0, 1.5f), Justification::centred, false);
            g.setColour (Colour (0xffe9dcb3));
            g.drawText ("Classic Studio G", plate, Justification::centred, false);

            // the two 500-series rows
            for (int r = 0; r < 2; ++r)
            {
                const Rectangle<float> row ((float) kChassisX, (float) (kChassisTop + kPlateH + kPlateGap + r * (impl.rowH + kRowGap)),
                                            b.getWidth() - 2.0f * kChassisX, (float) impl.rowH);
                g.setColour (Colour (0xff1a1b1d)); g.fillRoundedRectangle (row, 5.0f);
                g.setColour (Colours::black.withAlpha (0.8f)); g.fillRect (row.getX() + 4, row.getY(), row.getWidth() - 8, 3.0f);
                drawScrew (g, row.getX() + 6.0f, row.getCentreY(), 5.5f);
                drawScrew (g, row.getRight() - 6.0f, row.getCentreY(), 5.5f);
            }
        }
        Impl& impl;
    };

    explicit Impl (ClassicStudioGEditor& e) : ed (e), canvas (*this)
    {
        const auto defs = makeDefs();
        int tallest = 0;
        for (int i = 0; i < csg::kNumModules; ++i)
        {
            mods.add (new ModuleComponent (ed, i, defs[(size_t) i]));
            tallest = std::max (tallest, mods.getLast()->requiredHeight());
            canvas.addAndMakeVisible (mods.getLast());
        }
        ioIn  = std::make_unique<IoPanel> (ed.proc, true);
        ioOut = std::make_unique<IoPanel> (ed.proc, false);
        canvas.addAndMakeVisible (*ioIn);
        canvas.addAndMakeVisible (*ioOut);
        moduleH = tallest;
        rowH = moduleH + 2 * kRowPadY;
        baseW = 2 * kChassisX + 2 * kRowPadX + 7 * kModW + 6 * kGap;
        baseH = kChassisTop + kPlateH + kPlateGap + 2 * rowH + kRowGap + kBottom;
        for (auto* m : mods) m->setSize (kModW, moduleH);

        const int ioY = kChassisTop + (kPlateH - Knob::kH) / 2, ioW = IoPanel::widthNeeded();
        ioIn->setBounds  (kChassisX + 34, ioY, ioW, Knob::kH);
        ioOut->setBounds (baseW - kChassisX - 34 - ioW, ioY, ioW, Knob::kH);
    }

    Rectangle<int> slotRect (int slot) const
    {
        const int row = slot / 7, col = slot % 7;
        return { kChassisX + kRowPadX + col * (kModW + kGap),
                 kChassisTop + kPlateH + kPlateGap + row * (rowH + kRowGap) + kRowPadY,
                 kModW, moduleH };
    }

    ClassicStudioGEditor& ed;
    Canvas canvas;
    OwnedArray<ModuleComponent> mods;
    std::unique_ptr<IoPanel> ioIn, ioOut;
    int moduleH = 0, rowH = 0, baseW = 0, baseH = 0;
};

ClassicStudioGEditor::ClassicStudioGEditor (ClassicStudioGProcessor& p)
    : AudioProcessorEditor (&p), proc (p)
{
    impl = std::make_unique<Impl> (*this);
    addAndMakeVisible (impl->canvas);
    layoutModules();

    setResizable (true, true);
    setResizeLimits (roundToInt (impl->baseW * 0.25f), roundToInt (impl->baseH * 0.25f),
                     roundToInt (impl->baseW * 1.2f), roundToInt (impl->baseH * 1.2f));
    getConstrainer()->setFixedAspectRatio ((double) impl->baseW / (double) impl->baseH);
    setSize (roundToInt (impl->baseW * 0.5f), roundToInt (impl->baseH * 0.5f));
    startTimerHz (30);
}

ClassicStudioGEditor::~ClassicStudioGEditor() { stopTimer(); }

void ClassicStudioGEditor::paint (Graphics& g) { g.fillAll (Colour (0xff202124)); }

void ClassicStudioGEditor::resized()
{
    if (impl == nullptr) return;
    const float scale = (float) getWidth() / (float) impl->baseW;
    impl->canvas.setTransform (AffineTransform::scale (scale));
    impl->canvas.setBounds (0, 0, impl->baseW, impl->baseH);
}

void ClassicStudioGEditor::layoutModules()
{
    for (int slot = 0; slot < csg::kNumModules; ++slot)
        impl->mods[proc.moduleAtSlot (slot)]->setBounds (impl->slotRect (slot));
    impl->canvas.repaint();
}

int ClassicStudioGEditor::slotAtPoint (Point<int> pt) const
{
    int best = 0; float bestD = 1.0e12f;
    for (int s = 0; s < csg::kNumModules; ++s)
    {
        const float d = (float) impl->slotRect (s).getCentre().getDistanceSquaredFrom (pt);
        if (d < bestD) { bestD = d; best = s; }
    }
    return best;
}

int ClassicStudioGEditor::slotOfModule (int moduleId) const
{
    for (int s = 0; s < csg::kNumModules; ++s)
        if (proc.moduleAtSlot (s) == moduleId) return s;
    return 0;
}

void ClassicStudioGEditor::moveModule (int fromSlot, int toSlot)
{
    proc.moveModule (fromSlot, toSlot);
    layoutModules();
}

void ClassicStudioGEditor::timerCallback()
{
    for (auto* m : impl->mods) m->tick();
    impl->ioIn->tick();
    impl->ioOut->tick();
}
