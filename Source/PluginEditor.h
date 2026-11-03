#pragma once
#include "PluginProcessor.h"

// Resizable hardware-style window: 16 modules in two rows of 8. Drag a module's
// title area to move it in the chain. Drag the bottom-right corner to resize.
class ClassicStudioGEditor : public juce::AudioProcessorEditor,
                             private juce::Timer
{
public:
    explicit ClassicStudioGEditor (ClassicStudioGProcessor&);
    ~ClassicStudioGEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    // used by the module components
    int  slotAtPoint (juce::Point<int> canvasPoint) const;
    int  slotOfModule (int moduleId) const;
    void moveModule (int fromSlot, int toSlot);

    ClassicStudioGProcessor& proc;

private:
    void timerCallback() override;
    void layoutModules();

    struct Impl;
    std::unique_ptr<Impl> impl;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ClassicStudioGEditor)
};
