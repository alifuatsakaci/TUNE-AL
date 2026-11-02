#include "MainComponent.h"

//==============================================================================
MainComponent::MainComponent()
{
    // Make sure you set the size of the component after
    // you add any child components.
    setSize (800, 600);
    startTimerHz (60);   // call timerCallback() 30 times per second

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
    stabiliser.prepare (sampleRate, hopSize);
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
        // level of THIS frame, not smoothed: the onset detector needs to see fast jumps
        float sum = 0.0f;
        for (float s : frame)
            sum += s * s;
        const float frameDb = juce::Decibels::gainToDecibels (std::sqrt (sum / (float) frame.size()), -100.0f);

        float frequency = 0.0f, confidence = 0.0f;
        if (frameDb >= -45.0f)                          // don't waste CPU on silence
        {
            frequency  = detector.detect (frame.data());
            confidence = detector.getConfidence();
        }

        const float pitch = stabiliser.process (frequency, confidence, frameDb);

        if (pitch == PitchStabiliser::noPitch)
            detectedFrequency.store (0.0f);
        else
            detectedFrequency.store (440.0f * std::pow (2.0f, pitch / 12.0f));   // back to Hz

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
    // ---- colours (same as the design draft) ----
    const juce::Colour background (0xff2c2c2a), light (0xfff1efe8), grey (0xff888780),
                       green (0xff97c459), darkGreen (0xff3b6d11), amber (0xffef9f27);

    g.fillAll (background);

    // ---- read the shared values once ----
    const float f        = detectedFrequency.load();
    const bool  hasPitch = f > 0.0f;

    juce::String noteName = "-";
    float cents = 0.0f;

    if (hasPitch)
    {
        const float n       = 12.0f * std::log2 (f / 440.0f);   // semitones from A4
        const int   nearest = (int) std::round (n);
        const int   midi    = 69 + nearest;
        cents = 100.0f * (n - (float) nearest);

        static const char* noteNames[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
        noteName = juce::String (noteNames[midi % 12]) + juce::String (midi / 12 - 1);
    }

    const bool inTune = hasPitch && std::abs (cents) <= 3.0f;

    auto area = getLocalBounds().reduced (30);

    // ---- level meter (thin strip at the bottom) ----
    const float db         = juce::Decibels::gainToDecibels (inputLevel.load(), -60.0f);
    const float proportion = juce::jmap (db, -60.0f, 0.0f, 0.0f, 1.0f);
    auto bar = area.removeFromBottom (6);
    g.setColour (juce::Colour (0xff444441));
    g.fillRect (bar);
    g.setColour (darkGreen);
    g.fillRect (bar.withWidth ((int) (bar.getWidth() * proportion)));

    // ---- the gauge: an arc from -50 to +50 cents ----
    auto gaugeArea = area.removeFromTop (area.getHeight() / 2).toFloat();
    const float radius   = juce::jmin (gaugeArea.getWidth() / 2.0f, gaugeArea.getHeight()) * 0.9f;
    const juce::Point<float> centre (gaugeArea.getCentreX(), gaugeArea.getBottom());
    const float maxAngle = juce::MathConstants<float>::pi / 3.0f;     // +-50 cents = +-60 degrees

    // small helper functions ("lambdas"): cents -> angle, angle -> point on a circle
    auto angleFor = [maxAngle] (float c) { return c / 50.0f * maxAngle; };
    auto pointAt  = [centre] (float angle, float r)
    {
        // JUCE angles: 0 = straight up, positive = clockwise
        return centre + juce::Point<float> (r * std::sin (angle), -r * std::cos (angle));
    };

    juce::Path arc;
    arc.addCentredArc (centre.x, centre.y, radius, radius, 0.0f, -maxAngle, maxAngle, true);
    g.setColour (grey);
    g.strokePath (arc, juce::PathStrokeType (3.0f));

    juce::Path zone;                                                   // the +-3 cent "in tune" zone
    zone.addCentredArc (centre.x, centre.y, radius, radius, 0.0f, angleFor (-3.0f), angleFor (3.0f), true);
    g.setColour (darkGreen);
    g.strokePath (zone, juce::PathStrokeType (8.0f));

    g.setColour (grey);
    for (int c = -50; c <= 50; c += 10)                                // tick marks every 10 cents
    {
        const float a = angleFor ((float) c);
        g.drawLine (juce::Line<float> (pointAt (a, radius - 12.0f), pointAt (a, radius)), c == 0 ? 3.0f : 1.5f);
    }

    g.setColour (grey);
    g.setFont (14.0f);
    g.drawText ("-50", juce::Rectangle<float> (40.0f, 20.0f).withCentre (pointAt (-maxAngle, radius + 18.0f)),
                juce::Justification::centred);
    g.drawText ("+50", juce::Rectangle<float> (40.0f, 20.0f).withCentre (pointAt (maxAngle, radius + 18.0f)),
                juce::Justification::centred);

    if (hasPitch)                                                      // the needle
    {
        g.setColour (inTune ? green : light);
        g.drawLine (juce::Line<float> (centre, pointAt (angleFor (cents), radius - 8.0f)), 4.0f);
    }
    g.setColour (light);
    g.fillEllipse (juce::Rectangle<float> (14.0f, 14.0f).withCentre (centre));

    // ---- note name, cents and hint below the gauge ----
    area.removeFromTop (20);
    g.setColour (inTune ? green : light);
    g.setFont (110.0f);
    g.drawText (hasPitch ? noteName : "-", area.removeFromTop (120), juce::Justification::centred);

    if (! hasPitch)
    {
        g.setColour (grey);
        g.setFont (22.0f);
        g.drawText ("play a string", area.removeFromTop (40), juce::Justification::centred);
        return;
    }

    g.setColour (inTune ? green : amber);
    g.setFont (30.0f);
    g.drawText ((cents >= 0 ? "+" : "") + juce::String (cents, 1) + " cents", area.removeFromTop (40),
                juce::Justification::centred);

    g.setColour (grey);
    g.setFont (18.0f);
    const juce::String hint = inTune ? "in tune" : (cents < 0 ? "too low - tighten" : "too high - loosen");
    g.drawText (hint + "   |   " + juce::String (f, 2) + " Hz", area.removeFromTop (30), juce::Justification::centred);
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
