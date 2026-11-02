#include "MainComponent.h"

//==============================================================================
MainComponent::MainComponent()
{
    // Make sure you set the size of the component after
    // you add any child components.
    setSize (800, 600);
    startTimerHz (30);   // call timerCallback() 30 times per second

    // Some platforms require permissions to open input channels so request that here
    if (juce::RuntimePermissions::isRequired (juce::RuntimePermissions::recordAudio)
        && ! juce::RuntimePermissions::isGranted (juce::RuntimePermissions::recordAudio))
    {
        juce::RuntimePermissions::request (juce::RuntimePermissions::recordAudio,
                                           [&] (bool granted) { setAudioChannels (granted ? 2 : 0, 2); });
    }
    else
    {
        // Specify the number of input and output channels that we want to open
        setAudioChannels (2, 2);
    }
}

MainComponent::~MainComponent()
{
    // This shuts down the audio device and clears the audio source.
    shutdownAudio();
}

//==============================================================================
void MainComponent::prepareToPlay (int samplesPerBlockExpected, double sampleRate)
{
    currentSampleRate = sampleRate;
    smoothedLevel = 0.0f;
    detector.prepare (sampleRate);   // allocates the detector's arrays - before audio starts
    frameFill = 0;
    // This function will be called when the audio device is started, or when
    // its settings (i.e. sample rate, block size, etc) are changed.

    // You can use this function to initialise any resources you might need,
    // but be careful - it will be called on the audio thread, not the GUI thread.

    // For more details, see the help for AudioProcessor::prepareToPlay()
}

void MainComponent::getNextAudioBlock (const juce::AudioSourceChannelInfo& bufferToFill)
{
    auto* buffer = bufferToFill.buffer;
    const int n  = bufferToFill.numSamples;

    if (buffer->getNumChannels() > 0 && n > 0)
    {
        // pointer to the microphone samples (channel 0) — exactly like a float array in C
        const float* x = buffer->getReadPointer (0, bufferToFill.startSample);

        float sum = 0.0f;
        for (int i = 0; i < n; ++i)
        {
            sum += x[i] * x[i];
            pushSample (x[i]);
        }

        const float rms = std::sqrt (sum / (float) n);

        // one-pole low-pass: fast attack (10 ms), slow release (300 ms)
        const float blockTime = (float) n / (float) currentSampleRate;   // T_block in seconds
        const float tau       = (rms > smoothedLevel) ? 0.010f : 0.300f;
        const float a         = std::exp (-blockTime / tau);

        smoothedLevel = a * smoothedLevel + (1.0f - a) * rms;
        inputLevel.store (smoothedLevel);
    }

    // IMPORTANT: keep this last. It silences the output, otherwise the mic would
    // play straight back out of the speakers and cause feedback.
    bufferToFill.clearActiveBufferRegion();
}

void MainComponent::pushSample (float sample)
{
    frame[(size_t) frameFill++] = sample;

    if (frameFill == PitchDetector::frameSize)          // frame full -> analyse it
    {
        const float db = juce::Decibels::gainToDecibels (inputLevel.load(), -100.0f);

        if (db < -45.0f)                                // noise gate: nobody is playing
            detectedFrequency.store (0.0f);
        else
            detectedFrequency.store (detector.detect (frame.data()));

        // keep the newest half, so the next frame overlaps this one by 50 %
        std::copy (frame.begin() + hopSize, frame.end(), frame.begin());
        frameFill = PitchDetector::frameSize - hopSize;
    }
}

void MainComponent::releaseResources()
{
    // This will be called when the audio device stops, or when it is being
    // restarted due to a setting change.

    // For more details, see the help for AudioProcessor::releaseResources()
}

//==============================================================================
void MainComponent::paint (juce::Graphics& g)
{
    // (Our component is opaque, so we must completely fill the background with a solid colour)
    g.fillAll (getLookAndFeel().findColour (juce::ResizableWindow::backgroundColourId));
    // ---- level meter (bottom strip) ----
    const float db         = juce::Decibels::gainToDecibels (inputLevel.load(), -60.0f);
    const float proportion = juce::jmap (db, -60.0f, 0.0f, 0.0f, 1.0f);

    auto bar = getLocalBounds().reduced (40).removeFromBottom (20);
    g.setColour (juce::Colours::darkgrey);
    g.fillRect (bar);
    g.setColour (juce::Colours::limegreen);
    g.fillRect (bar.withWidth ((int) (bar.getWidth() * proportion)));

    // ---- note, frequency and cents ----
    const float f = detectedFrequency.load();
    g.setColour (juce::Colours::white);

    if (f <= 0.0f)
    {
        g.setFont (40.0f);
        g.drawText ("play a string", getLocalBounds(), juce::Justification::centred);
        return;
    }

    // same maths as freq_to_note() in Python
    const float n       = 12.0f * std::log2 (f / 440.0f);   // semitones from A4
    const int   nearest = (int) std::round (n);
    const float cents   = 100.0f * (n - (float) nearest);
    const int   midi    = 69 + nearest;

    static const char* noteNames[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    const juce::String noteName = juce::String (noteNames[midi % 12]) + juce::String (midi / 12 - 1);

    auto area = getLocalBounds().reduced (40);
    g.setFont (120.0f);
    g.drawText (noteName, area.removeFromTop (area.getHeight() / 2), juce::Justification::centredBottom);

    g.setFont (36.0f);
    g.drawText ((cents >= 0 ? "+" : "") + juce::String (cents, 1) + " cents", area.removeFromTop (60),
                juce::Justification::centred);

    g.setFont (20.0f);
    g.setColour (juce::Colours::grey);
    g.drawText (juce::String (f, 2) + " Hz", area.removeFromTop (30), juce::Justification::centred);
}

void MainComponent::resized()
{
    // This is called when the MainContentComponent is resized.
    // If you add any child components, this is where you should
    // update their positions.
}

void MainComponent::timerCallback()
{
    repaint();   // ask JUCE to call paint() again
}
