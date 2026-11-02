#include "MainComponent.h"

//==============================================================================
MainComponent::MainComponent()
{
    Theme::loadSettings();                                   // dark by default, or what the user chose last time
    Theme::readAccessibilitySettings();                      // Reduce Motion / Reduce Transparency
    Theme::update();                                         // light or dark, before anything is drawn
    sidebar.themeChanged();      // the sidebar was created before Theme::update(): give its text box the right colours
    juce::Desktop::getInstance().addDarkModeSettingListener (this);

    addChildComponent (sidebar);        // shown only when the window is wide enough
    addChildComponent (sidebarToggle);
    addChildComponent (scrim);
    sidebarToggle.onClick = [this] { toggleSidebar(); };
    scrim.onTap = [this] { setDrawerOpen (false); };
    setWantsKeyboardFocus (true);       // so Ctrl+Cmd+S reaches keyPressed()
    sidebarSpring.snapTo (1.0f);        // the docked sidebar starts open, the drawer closed
    addMouseListener (&dragListener, true);   // "true": also hear presses on child components (the sidebar's rows)

    // components that change rarely keep a ready-made picture of themselves ("buffered to image"):
    // the window redraws 60 times per second for the background and the wave, but these are only
    // redrawn when they call repaint() themselves
    for (auto* c : { (juce::Component*) &sidebar, (juce::Component*) &sidebarToggle, (juce::Component*) &header,
                     (juce::Component*) &strings, (juce::Component*) &octavePicker, (juce::Component*) &playBar,
                     (juce::Component*) &ring })
        c->setBufferedToImage (true);
    addAndMakeVisible (header);
    addAndMakeVisible (ring);   // the ring is a child component: it draws itself
    addAndMakeVisible (strings);
    addChildComponent (octavePicker);   // addChildComponent = added, but hidden until manual mode
    addChildComponent (playBar);

    // what happens when the header buttons are clicked
    header.onModeChanged = [this] (bool manual) { setMode (manual); };

    // the music basics slideshow: hidden until "?" is clicked, and it can play notes
    addChildComponent (guide);
    guide.playNote = [this] (float hz) { playNote (hz); };
    guide.playPair = [this] (float a, float b) { synth.pluck (a); synth2.pluck (b); };
    guide.getReference = [this] { return referencePitch.load(); };

    // the launch intro and, on the very first launch, the welcome card with the tour offer
    addChildComponent (intro);
    intro.onTour       = [this] { guide.open(); };
    intro.onFinished   = [this] { startListening(); };
    intro.onForkTapped = [this] { playNote (referencePitch.load()); };   // the fork plays A4

    // manual mode: three ways to choose the note, and the Play button
    ring.onNoteClicked = [this] (int note) { selectTarget (note, defaultOctaveFor (note), true); };
    octavePicker.onOctaveChanged = [this] (int octave) { selectTarget (targetNote, octave, true); };
    strings.onStringClicked = [this] (int i) { chooseString (i); };

    // the sidebar: docked on the Mac, a slide-over drawer on the iPhone
    // (on the iPhone, choosing a string or opening the guide also closes the drawer)
    sidebar.onStringClicked    = [this] (int i) { chooseString (i); setDrawerOpen (false); };
    sidebar.onReferenceChanged = [this] (float hz) { setReference (hz); };
    sidebar.onHelpClicked      = [this] { setDrawerOpen (false); guide.open(); };
    sidebar.onAppearanceClicked = [this]
    {
        // Dark -> Light -> System -> Dark ...
        auto& m = Theme::mode();
        m = m == Theme::Mode::dark ? Theme::Mode::light : (m == Theme::Mode::light ? Theme::Mode::system : Theme::Mode::dark);
        Theme::saveSettings();                               // remembered for the next launch
        applyTheme();
    };
    playBar.onPlayClicked = [this]
    {
        if (synth.isPlaying()) synth.stop();
        else                   synth.pluck (targetFrequency());
    };
    playBar.onKeepListeningChanged = [this] (bool listen) { keepListening.store (listen); };

    selectTarget (4, 2, false);   // start with E2 selected, silently

    // A4 as the user left it last time (415...466 Hz, 440 if never changed)
    const auto savedReference = (float) Theme::settings().getDoubleValue ("referencePitch", 440.0);
    if (savedReference != 440.0f)
        setReference (juce::jlimit (415.0f, 466.0f, savedReference));

    // set the size after adding the child components, so resized() can place them
    setSize (1100, 780);
    startTimerHz (60);   // call timerCallback() 60 times per second

    // Sound only for now: the microphone is opened when the intro is over (startListening), so the
    // "allow microphone?" question doesn't pop up over the welcome card
    setAudioChannels (0, 2);
    intro.start (! Theme::settings().getBoolValue ("welcomeSeen", false));
    resized();
}

void MainComponent::startListening()
{
    // Some platforms require permissions to open input channels so request that here
    if (juce::RuntimePermissions::isRequired (juce::RuntimePermissions::recordAudio)
        && ! juce::RuntimePermissions::isGranted (juce::RuntimePermissions::recordAudio))
    {
        juce::RuntimePermissions::request (juce::RuntimePermissions::recordAudio,
                                           [this] (bool granted) { setAudioChannels (granted ? 2 : 0, 2); });
    }
    else
    {
        // Specify the number of input and output channels that we want to open
        setAudioChannels (2, 2);
    }
}

MainComponent::~MainComponent()
{
    juce::Desktop::getInstance().removeDarkModeSettingListener (this);
    removeMouseListener (&dragListener);
    // This shuts down the audio device and clears the audio source.
    shutdownAudio();
}

//==============================================================================
void MainComponent::prepareToPlay (int samplesPerBlockExpected, double sampleRate)
{
    // Called when the audio device starts, or when its settings change.
    // Careful: this runs on the audio thread, not the GUI thread.
    juce::ignoreUnused (samplesPerBlockExpected);

    currentSampleRate = sampleRate;
    smoothedLevel = 0.0f;

    decimation = juce::jmax (1, (int) std::round (sampleRate / 48000.0));   // 1 at 44.1/48 kHz, 2 at 88.2/96, 4 at 192
    decimationCount = 0;
    decimationSum = 0.0f;
    const double analysisRate = sampleRate / decimation;

    detector.prepare (analysisRate);   // allocates the detector's arrays - before audio starts
    stabiliser.prepare (analysisRate, hopSize);
    synth.prepare (sampleRate);
    synth2.prepare (sampleRate);
    frameFill = 0;
}

void MainComponent::getNextAudioBlock (const juce::AudioSourceChannelInfo& bufferToFill)
{
    auto* buffer = bufferToFill.buffer;
    const int n  = bufferToFill.numSamples;

    // while the reference tone plays, the mic hears it too: only analyse if the user wants that
    const bool analyse = keepListening.load() || ! (synth.isPlaying() || synth2.isPlaying());

    if (buffer->getNumChannels() > 0 && n > 0)
    {
        // pointer to the microphone samples (channel 0) — exactly like a float array in C
        const float* x = buffer->getReadPointer (0, bufferToFill.startSample);

        float sum = 0.0f;
        for (int i = 0; i < n; ++i)
        {
            sum += x[i] * x[i];
            if (analyse)
            {
                decimationSum += x[i];
                if (++decimationCount == decimation)
                {
                    pushSample (decimationSum / (float) decimation);
                    decimationSum = 0.0f;
                    decimationCount = 0;
                }
            }
        }

        if (! analyse)
            detectedFrequency.store (0.0f);

        const float rms = std::sqrt (sum / (float) n);

        // one-pole low-pass: fast attack (10 ms), slow release (300 ms)
        const float blockTime = (float) n / (float) currentSampleRate;   // T_block in seconds
        const float tau       = (rms > smoothedLevel) ? 0.010f : 0.300f;
        const float a         = std::exp (-blockTime / tau);

        smoothedLevel = a * smoothedLevel + (1.0f - a) * rms;
        inputLevel.store (smoothedLevel);
    }

    // IMPORTANT: clear the output first. The buffer still holds the mic input, which
    // would otherwise play straight back out of the speakers and cause feedback.
    bufferToFill.clearActiveBufferRegion();

    // then add the reference tone (silent unless a note was plucked)
    synth.render (*buffer, bufferToFill.startSample, n);
    synth2.render (*buffer, bufferToFill.startSample, n);
}

void MainComponent::releaseResources()
{
    // Called when the audio device stops, or restarts because a setting changed.
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
        if (frameDb >= -62.0f)                          // don't waste CPU on silence (same as the gate)
        {
            frequency  = detector.detect (frame.data());
            confidence = detector.getConfidence();
        }

        const float reference = referencePitch.load();
        const float pitch = stabiliser.process (frequency, confidence, frameDb, reference);

        if (pitch == PitchStabiliser::noPitch)
            detectedFrequency.store (0.0f);
        else
            detectedFrequency.store (reference * std::pow (2.0f, pitch / 12.0f));   // back to Hz

        // keep the newest half, so the next frame overlaps this one by 50 %
        std::copy (frame.begin() + hopSize, frame.end(), frame.begin());
        frameFill = PitchDetector::frameSize - hopSize;
    }
}

//==============================================================================
void MainComponent::setMode (bool manual)
{
    manualMode = manual;
    header.setManual (manual);
    ring.setManual (manual);
    octavePicker.setVisible (manual);
    playBar.setVisible (manual);

    if (! manual)
    {
        synth.stop();
        synth2.stop();
    }

    resized();      // the free space for the wave depends on the mode
    repaint();
}

void MainComponent::selectTarget (int note, int octave, bool play)
{
    static const char* names[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };

    targetNote = note;
    targetOctave = octave;
    const float hz = targetFrequency();

    ring.setTarget (note, octave, hz);
    octavePicker.setOctave (octave);
    playBar.setTarget (juce::String (names[note]) + juce::String (octave), hz);

    if (play)
        synth.pluck (hz);
}

void MainComponent::playNote (float hz)
{
    // alternate between the two voices, so a new note doesn't cut off the previous one
    (nextVoice == 0 ? synth : synth2).pluck (hz);
    nextVoice = 1 - nextVoice;
}

void MainComponent::chooseString (int stringIndex)
{
    if (! manualMode)
        setMode (true);                                     // clicking a string switches to manual
    const auto& s = StringChipsComponent::standardTuning[stringIndex];
    selectTarget (s.note, s.octave, true);
}

void MainComponent::setReference (float hz)
{
    referencePitch.store (hz);
    sidebar.setReference (hz);
    selectTarget (targetNote, targetOctave, false);         // the target's frequency changed

    Theme::settings().setValue ("referencePitch", (double) hz);   // remembered for the next launch
    Theme::settings().saveIfNeeded();
}

void MainComponent::applyTheme()
{
    Theme::update();
    sidebar.themeChanged();

    // buffered components keep their old picture until THEY repaint, so tell every one of them
    std::function<void (juce::Component&)> repaintAll = [&repaintAll] (juce::Component& c)
    {
        c.repaint();
        for (auto* child : c.getChildren())
            repaintAll (*child);
    };
    repaintAll (*this);
}

void MainComponent::toggleSidebar()
{
    if (! wideLayout)                 // narrow window or iPhone: the sidebar is a drawer
    {
        setDrawerOpen (! drawerOpen);
        return;
    }

    sidebarOpen = ! sidebarOpen;
    fadeTransition = Theme::reduceMotion();
    sidebarSpring.setParameters (1.0f, Theme::reduceMotion() ? 0.25f : 0.35f);   // a click has no momentum: no bounce
    sidebarSpring.target = sidebarOpen ? 1.0f : 0.0f;
    resized();        // the string row changes at once; the sidebar itself slides in timerCallback
}

void MainComponent::setDrawerOpen (bool shouldBeOpen)
{
    drawerOpen = shouldBeOpen && ! wideLayout;    // the drawer only exists on narrow screens
    fadeTransition = Theme::reduceMotion();
    drawerSpring.setParameters (1.0f, Theme::reduceMotion() ? 0.25f : 0.3f);
    drawerSpring.target = drawerOpen ? 1.0f : 0.0f;
}

//==============================================================================
// Dragging the drawer (iPhone, or a narrow Mac window). Swipe in from the left edge to open it;
// swipe it (or the dark veil) to the left to close it. While the finger is down the drawer
// follows it exactly. When the finger lifts, the drawer keeps the finger's speed and a spring
// takes it the rest of the way, open or closed, depending on where the flick was heading.

float MainComponent::drawerTravel() const
{
    const auto safeArea = safeInsets().subtractedFrom (getLocalBounds());
    return (float) juce::jmin (300, safeArea.getWidth() - 50) + 20.0f;     // from fully hidden to fully open
}

void MainComponent::drawerMouseDown (const juce::MouseEvent& e)
{
    drag = {};
    if (wideLayout || guide.isVisible() || intro.isVisible())
        return;

    // the search box keeps its own drags (they select text)
    for (auto* c = e.originalComponent; c != nullptr; c = c->getParentComponent())
        if (dynamic_cast<juce::TextEditor*> (c) != nullptr)
            return;

    const auto p = e.getEventRelativeTo (this).position;
    const bool drawerShowing = drawerSpring.value > 0.01f;                      // press on the drawer or on the veil
    const bool fromLeftEdge  = p.x < (float) safeInsets().getLeft() + 20.0f;   // or at the very left edge
    if (! drawerShowing && ! fromLeftEdge)
        return;

    drag.possible = true;
    drag.start = p;
    drag.startAmount = drawerSpring.value;       // grab it where it is NOW, even halfway through an animation
    drag.history.push_back ({ juce::Time::getMillisecondCounterHiRes() * 0.001, p.x });
}

void MainComponent::drawerMouseDrag (const juce::MouseEvent& e)
{
    if (! drag.possible)
        return;

    const auto p = e.getEventRelativeTo (this).position;
    const auto moved = p - drag.start;

    if (! drag.active)
    {
        // wait for 10 px of movement, so a tap stays a tap; mostly vertical means it is not for us
        if (std::abs (moved.y) > 10.0f && std::abs (moved.y) > std::abs (moved.x))  { drag.possible = false; return; }
        if (std::abs (moved.x) < 10.0f)
            return;

        drag.active = true;
        fadeTransition = false;

        // the row or button under the finger must not click when the finger lifts:
        // disable it until the drag is over (a disabled button never clicks)
        for (auto* c = e.originalComponent; c != nullptr && c != this; c = c->getParentComponent())
            if (dynamic_cast<juce::Button*> (c) != nullptr)
            {
                drag.blocked = c;
                c->setEnabled (false);
                break;
            }
    }

    // follow the finger 1:1; past fully open, resist like a rubber band
    const float travel = drawerTravel();
    float amount = drag.startAmount + moved.x / travel;
    if (amount > 1.0f)
        amount = 1.0f + rubberBand ((amount - 1.0f) * travel, travel) / travel;
    drawerSpring.snapTo (juce::jmax (0.0f, amount));

    // remember the last 0.1 s of positions to know how fast the finger is moving
    const double now = juce::Time::getMillisecondCounterHiRes() * 0.001;
    drag.history.push_back ({ now, p.x });
    while (drag.history.size() > 2 && now - drag.history.front().time > 0.1)
        drag.history.erase (drag.history.begin());

    resized();
}

void MainComponent::drawerMouseUp (const juce::MouseEvent&)
{
    if (drag.active)
    {
        // finger speed in pixels per second (zero if the finger stopped before lifting)
        float velocity = 0.0f;
        const double now = juce::Time::getMillisecondCounterHiRes() * 0.001;
        const auto& first = drag.history.front();
        const auto& last  = drag.history.back();
        if (now - last.time < 0.05 && last.time - first.time > 0.005)
            velocity = (float) ((last.x - first.x) / (last.time - first.time));

        // open or closed? A clear flick decides by its direction; a slow release by where the drawer
        // would coast to (Apple's "momentum projection")
        const float travel = drawerTravel();
        const bool open = std::abs (velocity) > 300.0f ? velocity > 0.0f
                                                       : drawerSpring.value + projectMomentum (velocity) / travel > 0.5f;

        drawerOpen = open;
        drawerSpring.setParameters (Theme::reduceMotion() ? 1.0f : 0.8f, 0.3f);   // Apple's drawer spring: a hint of bounce
        drawerSpring.target   = open ? 1.0f : 0.0f;
        drawerSpring.velocity = velocity / travel;                                // the spring starts at the finger's speed

        if (drag.blocked != nullptr)
            drag.blocked->setEnabled (true);
    }

    drag = {};
}

bool MainComponent::keyPressed (const juce::KeyPress& key)
{
    const auto mods = key.getModifiers();
    if ((key.getKeyCode() == 's' || key.getKeyCode() == 'S') && mods.isCommandDown() && mods.isCtrlDown())
    {
        toggleSidebar();
        return true;
    }
    return false;
}

int MainComponent::stringIndexFor (int note, int octave)
{
    for (int i = 0; i < 6; ++i)
        if (StringChipsComponent::standardTuning[i].note == note && StringChipsComponent::standardTuning[i].octave == octave)
            return i;
    return -1;
}

float MainComponent::targetFrequency() const
{
    // MIDI note number: C4 = 60, A4 = 69. Each semitone is a factor 2^(1/12).
    const int midi = 12 * (targetOctave + 1) + targetNote;
    return referencePitch.load() * std::pow (2.0f, (float) (midi - 69) / 12.0f);
}

int MainComponent::defaultOctaveFor (int note)
{
    // the octave of the closest open guitar string: E2 A2 D3 G3 B3 E4
    //                          C  C# D  D# E  F  F# G  G# A  A# B
    static const int octaves[] = { 3, 3, 3, 3, 2, 2, 2, 3, 3, 2, 2, 3 };
    return octaves[note];
}

//==============================================================================
void MainComponent::paint (juce::Graphics& g)
{
    // ---- a living background: glass only looks like glass if there is colour behind it ----
    renderBackground();
    g.setImageResamplingQuality (juce::Graphics::mediumResamplingQuality);   // smooth when stretched
    g.drawImage (backgroundImage, getLocalBounds().toFloat());

    drawSignalWave (g);

    const auto glowColour = ring.getStateColour();

    // ---- hint text below the ring (automatic mode only: manual mode shows the octave picker there) ----
    // (it fits in the 80 px between the ring and the string row)
    auto below = content.withTop (ring.getBottom() + 16).reduced (20, 0);
    const float f = detectedFrequency.load();
    const juce::String dot = juce::String::fromUTF8 ("  \xc2\xb7  ");     // a middle dot, as UTF-8

    if (f > 0.0f && ! manualMode)
    {
        const float n     = 12.0f * std::log2 (f / referencePitch.load());
        const float cents = 100.0f * (n - std::round (n));
        const int   c     = juce::roundToInt (std::abs (cents));
        const bool  inTune = std::abs (cents) <= 3.0f;

        g.setColour (Theme::readable (glowColour));
        g.setFont (juce::FontOptions (26.0f, juce::Font::bold));
        g.drawText (inTune ? "In tune" : (cents < 0 ? "Tighten" : "Loosen"), below.removeFromTop (34), juce::Justification::centred);

        g.setColour (Theme::ink().withAlpha (0.62f));
        g.setFont (juce::FontOptions (16.0f));
        const juce::String detail = inTune ? juce::String (f, 2) + " Hz"
                                           : juce::String (c) + (cents < 0 ? " cents flat" : " cents sharp") + dot + juce::String (f, 2) + " Hz";
        g.drawText (detail, below.removeFromTop (24), juce::Justification::centred);
    }

    // ---- manual mode: one line under the play bar comparing your string with the target ----
    if (manualMode)
    {
        juce::String line = "Click a note on the ring, or a string";
        auto lineColour = Theme::ink().withAlpha (0.55f);

        if (f > 0.0f)
        {
            const float c = 1200.0f * std::log2 (f / targetFrequency());   // cents from the target
            if (std::abs (c) <= 3.0f)       { line = "In tune with the reference";           lineColour = RingComponent::green; }
            else if (std::abs (c) <= 100.0f) { line = juce::String (juce::roundToInt (std::abs (c))) + (c < 0 ? " cents flat: tighten" : " cents sharp: loosen");
                                               lineColour = RingComponent::orange; }
            else                               line = "Your string is more than a semitone away";
        }
        else if (synth.isPlaying())
        {
            line = "Listen, then play your string" + juce::String (keepListening.load() ? dot + "beats slow down as you get close" : "");
        }

        g.setColour (Theme::readable (lineColour));
        g.setFont (juce::FontOptions (15.0f));
        g.drawText (line, playBar.getBounds().translated (0, playBar.getHeight() + 8).withHeight (22).withX (content.getX()).withWidth (content.getWidth()),
                    juce::Justification::centred);
    }

}

void MainComponent::renderBackground()
{
    // Three very soft colour "blobs" drift slowly (sin/cos of the time): the state colour behind
    // the ring, plus indigo and teal accents. The glass panels let them shine through.
    const int scale = 4;
    const int w = juce::jmax (1, getWidth() / scale), h = juce::jmax (1, getHeight() / scale);
    if (backgroundImage.getWidth() != w || backgroundImage.getHeight() != h)
        backgroundImage = juce::Image (juce::Image::RGB, w, h, false);

    juce::Graphics g (backgroundImage);
    g.addTransform (juce::AffineTransform::scale (1.0f / (float) scale));   // draw in window coordinates
    g.fillAll (Theme::paper());

    const auto b = getLocalBounds().toFloat();
    const float t = Theme::reduceMotion() ? 0.0f : (float) (juce::Time::getMillisecondCounterHiRes() * 0.001);   // Reduce Motion: they stand still
    const float strength = Theme::dark() ? 1.0f : 0.75f;

    auto blob = [&g] (juce::Colour c, juce::Point<float> centre, float radius)
    {
        g.setGradientFill (juce::ColourGradient (c, centre, c.withAlpha (0.0f), centre.translated (radius, 0.0f), true));
        g.fillEllipse (juce::Rectangle<float> (radius * 2.0f, radius * 2.0f).withCentre (centre));
    };

    if (! Theme::reduceTransparency())        // Reduce Transparency: a plain background, only the state glow
    {
        blob (juce::Colour (0xff5e5ce6).withAlpha (0.32f * strength),                                   // indigo, top left
              { b.getWidth() * (0.12f + 0.05f * std::sin (t * 0.13f)), b.getHeight() * (0.22f + 0.06f * std::cos (t * 0.11f)) },
              b.getWidth() * 0.42f);
        blob (juce::Colour (0xff40c8e0).withAlpha (0.24f * strength),                                   // teal, bottom right
              { b.getWidth() * (0.88f + 0.04f * std::cos (t * 0.09f)), b.getHeight() * (0.85f + 0.05f * std::sin (t * 0.12f)) },
              b.getWidth() * 0.38f);
    }

    const auto ringCentre = ring.getBounds().getCentre().toFloat();
    const auto glowColour = ring.getStateColour();
    const bool silent = detectedFrequency.load() <= 0.0f && ! manualMode;
    blob ((silent ? juce::Colour (0xff5e5ce6) : glowColour).withAlpha (Theme::glowAlpha() * (silent ? 0.9f : 1.5f)),   // indigo when silent                                         // the state colour
          ringCentre.translated (12.0f * std::sin (t * 0.3f), 10.0f * std::cos (t * 0.25f)),
          (float) ring.getWidth() * 0.85f);


    // the sidebar's soft shadow is blurry too, so it can live in the small image as well
    if (sidebar.isVisible() && sidebar.getAlpha() > 0.5f)
        Theme::softShadow (g, sidebar.getBounds().toFloat(), 20.0f);
}

// the band below the last control (but at most 120 px tall); waves never grow past its edges
juce::Rectangle<float> MainComponent::waveBand() const
{
    auto band = content.toFloat().removeFromBottom (120.0f).reduced (10.0f, 0.0f);
    band.setTop (juce::jmax (band.getY(), (float) waveTop));
    return band;
}

void MainComponent::drawSignalWave (juce::Graphics& g)
{
    // Three sine waves across the bottom of the content area, like an oscilloscope in slow motion.
    // Their size, speed and colour come from waveAmplitude / waveCycles / wavePhase / waveMix,
    // which timerCallback() moves smoothly towards what the microphone hears.
    const auto band = waveBand();
    if (band.getHeight() < 30.0f)
        return;
    const float midY = band.getCentreY();
    const float maxAmplitude = band.getHeight() * 0.45f;
    const auto colour = Theme::ink().withAlpha (0.30f).interpolatedWith (ring.getStateColour(), waveMix);

    struct Layer { float amplitude, cyclesFactor, phaseOffset, alpha; };
    const Layer layers[] = { { 1.0f, 1.0f, 0.0f, 0.9f }, { 0.6f, 1.5f, 2.1f, 0.5f }, { 0.35f, 0.7f, 4.2f, 0.35f } };

    for (const auto& layer : layers)
    {
        juce::Path wave;
        const int points = 160;
        for (int i = 0; i <= points; ++i)
        {
            const float u = (float) i / (float) points;                       // 0 ... 1 across
            const float envelope = std::sin (juce::MathConstants<float>::pi * u);   // calm at the edges
            const float y = midY - juce::jmin (waveAmplitude, maxAmplitude) * layer.amplitude * envelope
                                   * std::sin (juce::MathConstants<float>::twoPi * waveCycles * layer.cyclesFactor * u
                                               - wavePhase * layer.cyclesFactor + layer.phaseOffset);
            const float x = band.getX() + u * band.getWidth();
            if (i == 0) wave.startNewSubPath (x, y); else wave.lineTo (x, y);
        }

        // fade both ends out: a horizontal gradient from transparent to the colour and back
        auto stroke = [&] (float alpha, float thickness)
        {
            juce::ColourGradient fade (colour.withAlpha (0.0f), band.getX(), midY, colour.withAlpha (0.0f), band.getRight(), midY, false);
            fade.addColour (0.2, colour.withMultipliedAlpha (alpha));
            fade.addColour (0.8, colour.withMultipliedAlpha (alpha));
            g.setGradientFill (fade);
            g.strokePath (wave, juce::PathStrokeType (thickness, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        };
        stroke (layer.alpha * 0.25f, 7.0f);   // soft glow underneath
        stroke (layer.alpha, 1.8f);           // the line itself
    }
}

void MainComponent::resized()
{
    // On iPhone, the Dynamic Island (top) and the home bar (bottom) cover part of the screen.
    // The background still fills everything, but controls are placed inside the "safe area".
    const auto insets = safeInsets();
    const auto safeArea = insets.subtractedFrom (getLocalBounds());

    guide.setBounds (getLocalBounds());       // the slideshow covers everything...
    guide.setInsets (insets);                 // ...but keeps its sheet inside the safe area
    intro.setBounds (getLocalBounds());
    intro.setInsets (insets);

    lastInsets = insets;

    // Two layouts, chosen by the window width:
    //  - wide (Mac, iPad):  the glass sidebar is DOCKED on the left; the tuner moves to the right of it
    //  - narrow (iPhone):   the sidebar is a DRAWER that slides OVER the tuner, with a dark veil behind it
    // Either way, the sidebar button at the top left opens and closes it.
    wideLayout = getWidth() >= 820;
    if (wideLayout && (drawerOpen || drawerSpring.value != 0.0f))
    {
        drawerOpen = false;
        drawerSpring.snapTo (0.0f);
    }

    const bool useSidebar = wideLayout && sidebarOpen;              // strings live in the docked sidebar
    const auto& spring = wideLayout ? sidebarSpring : drawerSpring;
    const float amount = spring.value;                              // animated 0 ... 1 (a drawer may overshoot 1 a little)

    // Reduce Motion: the sidebar fades in and out in its open place instead of sliding,
    // and everything else jumps straight to where it ends up
    const bool  fade         = fadeTransition && ! drag.active;
    const float slide        = fade ? 1.0f : amount;                // where the sidebar sits
    const float layoutAmount = fade ? spring.target : amount;       // what the tuner and the button follow

    sidebar.setVisible (amount > 0.01f);
    sidebar.setAlpha (fade ? juce::jlimit (0.0f, 1.0f, amount) : 1.0f);
    sidebar.setSolid (! wideLayout || Theme::reduceTransparency()); // frosted when it covers the tuner
    sidebarToggle.setVisible (true);
    strings.setVisible (! useSidebar);

    auto area = safeArea;
    if (wideLayout)
    {
        // docked: the sidebar keeps its size and slides out to the left; the content follows its edge
        const int visibleWidth = juce::roundToInt (300.0f * layoutAmount);
        sidebar.setBounds (juce::Rectangle<int> (280, safeArea.getHeight() - 20)
                               .withPosition (safeArea.getX() + 10 - (300 - juce::roundToInt (300.0f * slide)), safeArea.getY() + 10));
        area.removeFromLeft (visibleWidth);
        scrim.setAmount (0.0f);
    }
    else
    {
        // drawer: the same sidebar, sliding in over the tuner, which stays where it is
        // past fully open (a bounce, or the rubber band) it stays pinned at the left and stretches,
        // so no gap opens up at the edge of the screen
        const int w = juce::jmin (300, safeArea.getWidth() - 50);
        const int stretch = juce::roundToInt ((float) (w + 20) * juce::jmax (0.0f, slide - 1.0f));
        sidebar.setBounds (juce::Rectangle<int> (w + stretch, safeArea.getHeight() - 20)
                               .withPosition (safeArea.getX() + 10 - juce::roundToInt ((float) (w + 20) * (1.0f - juce::jmin (1.0f, slide))),
                                              safeArea.getY() + 10));
        scrim.setBounds (getLocalBounds());
        scrim.setAmount (juce::jlimit (0.0f, 1.0f, amount));
    }
    content = area;

    // stacking order, back to front: tuner ... veil, sidebar, sidebar button, slideshow
    scrim.toFront (false);
    sidebar.toFront (false);
    sidebarToggle.toFront (false);
    if (guide.isVisible())
        guide.toFront (false);
    if (intro.isVisible())
        intro.toFront (false);

    // the sidebar button: inside the sidebar's top-right corner when open, at the top left when closed,
    // and in between while the sidebar slides (a "lerp": linear interpolation by the animation amount)
    const float openX = (float) sidebar.getRight() - 14.0f - 36.0f, openY = (float) sidebar.getY() + 9.0f;
    const float closedX = (float) content.getX() + 16.0f, closedY = (float) safeArea.getY() + 22.0f;
    const float t = juce::jlimit (0.0f, 1.0f, layoutAmount);     // (a bounce past 1 must not push it off screen)
    const float size = 40.0f - 4.0f * t;
    sidebarToggle.setBounds (juce::Rectangle<float> (closedX + (openX - closedX) * t,
                                                     closedY + (openY - closedY) * t, size, size).toNearestInt());

    // header: 44 px tall, centred at the top: just the Auto | Manual switch
    auto top = content.reduced (20).removeFromTop (44);
    header.setBounds (top.withSizeKeepingCentre (juce::jmin (180, top.getWidth()), 44));

    // the ring is a square: as big as fits, centred in the content area, a little above the middle.
    // On short screens (iPhone SE, a small Mac window) it shrinks so that everything below it still
    // fits: octave picker, string row, play bar and the hint line (manual mode needs the most room)
    const int below = useSidebar ? 200 : 252;                    // space needed under the ring
    const int ringTop = content.getY() + 74;                     // just under the header
    int side = juce::jmin (content.getWidth() - 40, (int) (content.getHeight() * (useSidebar ? 0.58f : 0.52f)));
    side = juce::jmax (120, juce::jmin (side, content.getBottom() - below - ringTop));
    int centreY = content.getY() + (int) (content.getHeight() * (useSidebar ? 0.43f : 0.40f));
    centreY = juce::jlimit (ringTop + side / 2, juce::jmax (ringTop + side / 2, content.getBottom() - below - side / 2), centreY);
    ring.setBounds (juce::Rectangle<int> (side, side).withCentre ({ content.getCentreX(), centreY }));

    // under the ring: hint text (auto) or octave picker (manual), then the strings (narrow only)
    const int y = ring.getBottom() + 16;
    octavePicker.setBounds (content.getX(), y, content.getWidth(), 80);
    strings.setBounds (content.getX(), y + 80, content.getWidth(), 52);

    // manual mode: play button + keep-listening switch (right under the octaves when the strings are in the sidebar)
    const int playY = useSidebar ? y + 80 + 14 : strings.getBottom() + 14;
    playBar.setBounds (juce::Rectangle<int> (juce::jmin (520, content.getWidth() - 40), 48)
                           .withCentre ({ content.getCentreX(), playY + 24 }));

    // the signal wave lives in the free space under the last control
    waveTop = manualMode ? playBar.getBottom() + 34
                         : (strings.isVisible() ? strings.getBottom() + 4 : ring.getBottom() + 90);
}

juce::BorderSize<int> MainComponent::safeInsets() const
{
   #if JUCE_IOS
    if (auto* display = juce::Desktop::getInstance().getDisplays().getDisplayForRect (getScreenBounds()))
        return display->safeAreaInsets;
   #endif
    return {};      // on the Mac the window never sits under a notch
}

void MainComponent::timerCallback()
{
    // iPhone: the safe area (Dynamic Island, home bar) is not known yet when the app first lays
    // itself out, and it changes if the phone rotates; whenever it differs, lay everything out again
    if (safeInsets() != lastInsets)
        resized();

    // the real time since the last frame (the timer is not exactly 60 Hz)
    const double now = juce::Time::getMillisecondCounterHiRes() * 0.001;
    const float dt = lastFrameTime > 0.0 ? (float) juce::jlimit (0.0, 1.0 / 30.0, now - lastFrameTime) : 1.0f / 60.0f;
    lastFrameTime = now;

    // about twice a second: did the user switch Reduce Motion / Reduce Transparency?
    if (++frameCounter % 30 == 0 && Theme::readAccessibilitySettings())
    {
        applyTheme();
        resized();
    }

    // sidebar and drawer springs (the drawer stands still while a finger holds it)
    bool moved = sidebarSpring.step (dt);
    if (! drag.active)
        moved = drawerSpring.step (dt) || moved;
    if (moved)
        resized();
    if (sidebarSpring.isSettled() && drawerSpring.isSettled())
        fadeTransition = false;

    // signal wave: louder sound -> bigger waves; higher note -> more cycles; always flowing
    {
        const float f    = detectedFrequency.load();
        const float db   = juce::Decibels::gainToDecibels (inputLevel.load(), -70.0f);
        const float loud = juce::jlimit (0.0f, 1.0f, juce::jmap (db, -60.0f, -15.0f, 0.0f, 1.0f));
        const bool  hearing = f > 0.0f || (manualMode && (synth.isPlaying() || synth2.isPlaying()));

        const float targetAmplitude = 5.0f + (hearing ? 10.0f + 38.0f * loud : 6.0f * loud);   // calm: ~5 px
        const float targetCycles    = f > 0.0f ? 1.5f + 1.6f * std::log2 (f / 82.4f) : 1.5f;  // E2 -> 1.5, E4 -> 4.7
        const float targetMix       = hearing ? 1.0f : 0.0f;

        waveAmplitude += (targetAmplitude - waveAmplitude) * 0.12f;      // same "ease" as the sidebar
        waveCycles    += (targetCycles - waveCycles) * 0.05f;
        waveMix       += (targetMix - waveMix) * 0.08f;
        // faster flow while it hears something; with Reduce Motion it rests while silent
        wavePhase     += Theme::reduceMotion() ? (hearing ? 0.05f : 0.0f) : (hearing ? 0.09f : 0.035f);
    }

    const float f = detectedFrequency.load();

    if (f > 0.0f)
    {
        const float n       = 12.0f * std::log2 (f / referencePitch.load());   // semitones from A4
        const int   nearest = (int) std::round (n);
        const int   midi    = 69 + nearest;
        ring.setReading (true, midi % 12, midi / 12 - 1, 100.0f * (n - (float) nearest));

        if (! manualMode)
        {
            const int cents = juce::roundToInt (100.0f * (n - (float) nearest));
            const juce::String status = std::abs (cents) <= 3 ? juce::String::fromUTF8 ("\xe2\x9c\x93")      // a tick
                                                              : (cents < 0 ? juce::String::fromUTF8 ("\xe2\x88\x92") : juce::String ("+"))
                                                                    + juce::String (std::abs (cents)) + juce::String::fromUTF8 ("\xc2\xa2");
            strings.setActive (midi % 12, midi / 12 - 1, ring.getStateColour());
            sidebar.setActive (stringIndexFor (midi % 12, midi / 12 - 1), ring.getStateColour(), status);
        }
    }
    else
    {
        ring.setReading (false, 0, 0, 0.0f);
        if (! manualMode)
        {
            strings.setActive (-1, 0, {});
            sidebar.setActive (-1, {}, {});
        }
    }

    if (manualMode)
    {
        // how far is your string from the chosen note? (for the fine bar in the ring)
        const float c = f > 0.0f ? 1200.0f * std::log2 (f / targetFrequency()) : 0.0f;
        ring.setTargetCents (f > 0.0f && std::abs (c) <= 50.0f, c);
        strings.setActive (targetNote, targetOctave, RingComponent::blue);
        sidebar.setActive (stringIndexFor (targetNote, targetOctave), RingComponent::blue, "target");
        playBar.setPlaying (synth.isPlaying());
    }

    // What to redraw. Repainting the whole window 60 times a second means blending millions of
    // Retina pixels every frame: a full CPU core. Most of it hardly changes, so:
    //  - the wave strip at the bottom: every frame (it is the only thing that really flows)
    //  - everything else (the slowly drifting background, the hint text): 30 times a second while
    //    a note sounds, 10 when it is quiet, every frame while the sidebar/drawer moves (its shadow
    //    is part of the background)
    //  - nothing while the intro covers the window
    if (isShowing() && ! (intro.isVisible() && intro.isOpaque()))     // (isShowing: false when minimised)
    {
        const bool panelMoving = ! sidebarSpring.isSettled() || ! drawerSpring.isSettled() || drag.active;
        const bool sounding    = f > 0.0f || synth.isPlaying() || synth2.isPlaying();
        const int  everyNth    = panelMoving ? 1 : (sounding ? 2 : 6);

        if (frameCounter % everyNth == 0)
            repaint();
        else
            repaint (waveBand().expanded (0.0f, 12.0f).toNearestInt());   // + room for the glow
    }
}
