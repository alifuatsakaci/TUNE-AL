#pragma once

#include <JuceHeader.h>
#include "PitchDetector.h"
#include "PitchStabiliser.h"
#include "PluckSynth.h"
#include "RingComponent.h"
#include "HeaderComponent.h"
#include "StringChipsComponent.h"
#include "ManualControls.h"
#include "GuideComponent.h"
#include "SidebarComponent.h"

//==============================================================================
/*
    This component lives inside our window, and this is where you should put all
    your controls and content.
*/
class MainComponent  : public juce::AudioAppComponent,
                       private juce::Timer,
                       private juce::DarkModeSettingListener      // tells us when macOS switches light/dark
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

    // ---- manual mode ----
    void setMode (bool manual);                               // switch between Auto and Manual
    void selectTarget (int note, int octave, bool play);      // choose the note to tune to
    float targetFrequency() const;                            // that note in Hz
    static int defaultOctaveFor (int note);                   // octave of the nearest guitar string
    void playNote (float hz);                                 // pluck on the next free voice
    void chooseString (int stringIndex);                      // a string was clicked (row or sidebar)
    void setReference (float hz);                             // change A4 everywhere
    static int stringIndexFor (int note, int octave);         // 0..5, or -1 if no open string has it
    void toggleSidebar();                                     // open / close the sidebar or drawer (animated)
    void setDrawerOpen (bool shouldBeOpen);                   // iPhone: the sidebar slides over the tuner
    void applyTheme();                                        // light/dark changed: update everything
    void drawSignalWave (juce::Graphics& g);                  // the flowing wave at the bottom
    juce::BorderSize<int> safeInsets() const;                 // iPhone: space taken by the notch and home bar
    void darkModeSettingChanged() override { applyTheme(); }  // macOS appearance changed
    bool keyPressed (const juce::KeyPress& key) override;     // Ctrl+Cmd+S toggles the sidebar

private:
    //==============================================================================
    std::atomic<float> inputLevel { 0.0f };   // RMS of the last block, shared between threads
    double currentSampleRate = 48000.0;       // set in prepareToPlay
    float  smoothedLevel     = 0.0f;          // y[k-1], used only on the audio thread

    // ---- pitch detection (audio thread) ----
    PitchDetector detector;
    PitchStabiliser stabiliser;
    static constexpr int hopSize = PitchDetector::frameSize / 2;   // analyse every 1024 new samples
    std::array<float, PitchDetector::frameSize> frame {};          // fixed size: no allocation
    int frameFill = 0;                                             // how many samples are in frame

    // ---- reference tone (audio thread renders it, GUI thread starts/stops it) ----
    PluckSynth synth;
    PluckSynth synth2;          // a second voice: lets two notes ring together (music basics, beats)
    int nextVoice = 0;

    // ---- results, read by the GUI thread ----
    std::atomic<float> detectedFrequency { 0.0f };                 // 0 = no note

    // ---- shared settings ----
    std::atomic<float> referencePitch { 440.0f };                  // A4 in Hz, read by both threads
    std::atomic<bool>  keepListening { true };                     // keep detecting while the tone plays?

    // ---- user interface ----
    SidebarComponent sidebar;   // Mac layout only (wide windows)
    SidebarToggleButton sidebarToggle;
    DrawerScrim scrim;                                             // dims the tuner behind the drawer
    HeaderComponent header;
    RingComponent ring;
    StringChipsComponent strings;
    OctavePicker octavePicker;
    PlayBar playBar;
    GuideComponent guide;       // the music basics slideshow, drawn on top of everything
    bool manualMode = false;
    bool wideLayout = false;                                       // true: sidebar layout (wide window)

    // ---- the signal wave at the bottom (smoothed in timerCallback, drawn in paint) ----
    float waveAmplitude = 0.0f;     // pixels
    float waveCycles    = 1.5f;     // how many periods across the screen (more for higher notes)
    float wavePhase     = 0.0f;     // radians, keeps increasing: that is what makes it flow
    float waveMix       = 0.0f;     // 0 = calm neutral colour ... 1 = state colour
    int   waveTop       = 0;        // the wave stays below this y (under the last control)

    // the soft background is drawn into this small image (1/4 size) and stretched to the window:
    // blurry shapes look the same scaled up, and it is 16 times fewer pixels to paint every frame
    juce::Image backgroundImage;
    void renderBackground();
    bool sidebarOpen = true;                                       // what the user chose
    float sidebarAmount = 1.0f;                                    // 0 = hidden ... 1 = fully open (animated)
    bool drawerOpen = false;                                       // narrow screens: the sidebar as a drawer
    float drawerAmount = 0.0f;                                     // 0 = hidden ... 1 = fully open (animated)
    juce::BorderSize<int> lastInsets;                              // the safe area used by the last layout
    juce::Rectangle<int> content;                                  // the area for ring, text and controls
    int targetNote = 4, targetOctave = 2;                          // manual mode: E2

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainComponent)
};
