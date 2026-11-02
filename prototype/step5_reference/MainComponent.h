#pragma once

#include <JuceHeader.h>
#include "PitchDetector.h"

//==============================================================================
/*
    This component lives inside our window, and this is where you should put all
    your controls and content.
*/
class MainComponent  : public juce::AudioAppComponent, private juce::Timer

{
public:
    //==============================================================================
    MainComponent();
    ~MainComponent() override;

    //==============================================================================
    void prepareToPlay (int samplesPerBlockExpected, double sampleRate) override;
    void getNextAudioBlock (const juce::AudioSourceChannelInfo& bufferToFill) override;
    void releaseResources() override;

    //==============================================================================
    void paint (juce::Graphics& g) override;
    void resized() override;
    void timerCallback() override;
    void pushSample (float sample);   // collects samples into frames for the detector

private:
    //==============================================================================
    // Your private member variables go here...
    std::atomic<float> inputLevel { 0.0f };   // RMS of the last block, shared between threads
    double currentSampleRate = 48000.0;   // set in prepareToPlay
    float  smoothedLevel     = 0.0f;      // y[k-1], used only on the audio thread

    // ---- pitch detection (audio thread) ----
    PitchDetector detector;
    static constexpr int hopSize = PitchDetector::frameSize / 2;   // analyse every 1024 new samples
    std::array<float, PitchDetector::frameSize> frame {};          // fixed size: no allocation
    int frameFill = 0;                                             // how many samples are in frame

    // ---- results, read by the GUI thread ----
    std::atomic<float> detectedFrequency { 0.0f };                 // 0 = no note

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainComponent)
};
