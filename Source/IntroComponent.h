/*
  ==============================================================================

    IntroComponent.h
    Created: 2 Nov 2026
    Author:  Ali Fuat Sakacı

  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>
#include "Theme.h"
#include "Spring.h"
#include "HeaderComponent.h"     // for GlassButton

//==============================================================================
/*
    What you see when the app opens.

    1) The intro (every launch, about 2 seconds): the logo is there from the first frame
       (on the iPhone it is already on the system launch screen, Resources/LaunchScreen.storyboard)
       and pops; the fork is struck and rings for a second (its tines blur, sound ripples
       spread out), then everything dissolves into the tuner.

    Smoothness: the animation is driven by the screen itself (VBlankAttachment: called once per
    display refresh, like Apple's CADisplayLink), springs use the real time between frames, and
    the artwork is pre-rendered at the screen's exact pixel size, so each frame only copies a
    few ready-made pictures.
    2) The welcome (first launch only): the logo moves up and a glass card asks whether
       you want the quick tour. Its choice is remembered.

    It covers the whole window and is drawn on top of everything. Click or tap to skip the intro.
*/
class IntroComponent  : public juce::Component
{
public:
    std::function<void()> onTour;           // "Take the quick tour"
    std::function<void()> onFinished;       // the intro/welcome is gone: start listening
    std::function<void()> onForkTapped;     // the logo was tapped on the welcome card

    IntroComponent()
    {
        tourButton.onClick  = [this] { leave (true); };
        startButton.onClick = [this] { leave (false); };
        startButton.fontSize = 17.0f;
        addChildComponent (tourButton);
        addChildComponent (startButton);
        setInterceptsMouseClicks (true, true);
    }

    // showWelcome: true on the first launch
    void start (bool showWelcome)
    {
        welcome = showWelcome;
        phase = Phase::intro;
        startTime = now();
        strikeTime = Theme::reduceMotion() ? -100.0f : strikeAt;     // when the fork gets struck

        // the "pop": the logo's size is a spring with a little overshoot
        handSpring.setParameters (0.7f, 0.45f);
       #if JUCE_IOS
        handSpring.snapTo (1.0f);                                   // the launch screen already shows it at full size:
        handSpring.velocity = Theme::reduceMotion() ? 0.0f : 1.1f;  // a gentle kick, it swells and settles
       #else
        handSpring.snapTo (Theme::reduceMotion() ? 1.0f : 0.86f);   // Mac: grows from a bit smaller as it fades in
        handSpring.target = 1.0f;
       #endif
        cardSpring.setParameters (0.9f, 0.45f);
        cardSpring.snapTo (0.0f);
        setAlpha (1.0f);
        setOpaque (true);                                           // covers the tuner: JUCE can skip painting it
        setVisible (true);
        toFront (false);

        lastFrame = 0.0;
        vblank = std::make_unique<juce::VBlankAttachment> (this, [this] (double timestamp) { tick (timestamp); });
    }

    void setInsets (juce::BorderSize<int> newInsets)
    {
        insets = newInsets;
        resized();
    }

    bool isRunning() const      { return isVisible(); }

    //==============================================================================
    void paint (juce::Graphics& g) override
    {
        const float t = elapsed();

        // the background: the app's colour, and on the welcome a veil so the tuner shows through
        const float veil = 1.0f - 0.45f * cardSpring.value;
        g.fillAll (Theme::paper().withAlpha (veil));

        // a soft indigo glow behind the logo, like the background of the tuner
        const auto logo = logoArea();
        const auto glowCentre = logo.getCentre();
        const auto indigo = juce::Colour (0xff5e5ce6).withAlpha ((Theme::dark() ? 0.35f : 0.22f) * smooth (t / 0.4f)
                                                                 * (1.0f + 0.5f * ringEnvelope (t - strikeTime)));
        g.setGradientFill (juce::ColourGradient (indigo, glowCentre, indigo.withAlpha (0.0f),
                                                 glowCentre.translated (logo.getWidth() * 0.9f, 0.0f), true));
        g.fillEllipse (logo.expanded (logo.getWidth() * 0.4f));

        // the welcome card
        if (cardSpring.value > 0.01f)
        {
            juce::Graphics::ScopedSaveState s (g);
            g.setOpacity (juce::jlimit (0.0f, 1.0f, cardSpring.value));
            Theme::drawGlass (g, card, 34.0f, 0.0f, Theme::sheet().withAlpha (0.94f), true);

            // the words appear in the second half, once the logo has moved out of their way
            g.setOpacity (juce::jlimit (0.0f, 1.0f, (cardSpring.value - 0.55f) / 0.45f));
            const auto ink = Theme::ink();
            auto r = textArea;
            // narrow card (iPhone): the title goes on two lines instead of being cut off
            const bool twoLines = r.getWidth() < 330.0f;
            g.setColour (ink);
            g.setFont (juce::FontOptions (28.0f, juce::Font::bold));
            g.drawFittedText (twoLines ? "Welcome to\nGuitar Tuner" : "Welcome to Guitar Tuner",
                              r.removeFromTop (twoLines ? 70.0f : 38.0f).toNearestInt(), juce::Justification::centred, 2, 1.0f);
            r.removeFromTop (10.0f);
            g.setColour (ink.withAlpha (0.75f));
            g.setFont (juce::FontOptions (16.5f));
            g.drawFittedText ("Play a string and the ring shows its note and how far off it is. "
                              "Turn the peg until the ring glows green.\n\n"
                              "New to tuning? The quick tour explains strings, notes and octaves in two minutes, with sounds.",
                              r.toNearestInt(), juce::Justification::centredTop, 8, 1.0f);

            g.setColour (ink.withAlpha (0.45f));
            g.setFont (juce::FontOptions (13.0f));
            g.drawFittedText ("You can open the tour any time from Music basics in the sidebar.",
                              footnote.toNearestInt(), juce::Justification::centred, 2, 1.0f);
        }

        drawLogo (g, logo, t);
    }

    void resized() override
    {
        const auto safe = insets.subtractedFrom (getLocalBounds()).toFloat();
        const float w = juce::jmin (440.0f, safe.getWidth() - 40.0f);
        const float h = juce::jmin (w < 380.0f ? 620.0f : 560.0f, safe.getHeight() - 40.0f);
        card = juce::Rectangle<float> (w, h).withCentre (safe.getCentre());

        auto inner = card.reduced (28.0f);
        welcomeLogo = inner.removeFromTop (juce::jmin (170.0f, h * 0.3f));
        welcomeLogo = welcomeLogo.withSizeKeepingCentre (welcomeLogo.getHeight() * artAspect(), welcomeLogo.getHeight());
        footnote = inner.removeFromBottom (w < 380.0f ? 36.0f : 20.0f);
        inner.removeFromBottom (10.0f);
        auto buttons = inner.removeFromBottom (50.0f + 12.0f + 50.0f);
        tourButton.setBounds (buttons.removeFromTop (50.0f).toNearestInt());
        buttons.removeFromTop (12.0f);
        startButton.setBounds (buttons.toNearestInt());
        inner.removeFromBottom (16.0f);
        textArea = inner.withTrimmedTop (12.0f);

        const float side = juce::jmin (300.0f, safe.getWidth() * 0.62f);
        introLogo = juce::Rectangle<float> (side, side / artAspect()).withCentre (safe.getCentre());
        renderLayers();
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        if (phase == Phase::intro)                       // tap to skip the intro
        {
            if (welcome) showWelcomeCard();
            else         leave (false);
        }
        else if (phase == Phase::welcome && logoArea().contains (e.position))
        {
            strikeTime = elapsed();                      // ring the fork again...
            if (onForkTapped) onForkTapped();            // ...and let it sound
        }
    }

private:
    enum class Phase { intro, welcome, leaving };

    //==============================================================================
    // once per screen refresh
    void tick (double timestamp)
    {
        const float t = elapsed();
        const float dt = lastFrame > 0.0 ? (float) juce::jlimit (0.0, 1.0 / 30.0, timestamp - lastFrame) : 1.0f / 60.0f;
        lastFrame = timestamp;

        handSpring.step (dt);
        cardSpring.step (dt);
        if (phase == Phase::welcome)
            for (auto* b : { (juce::Component*) &tourButton, (juce::Component*) &startButton })
                b->setAlpha (juce::jlimit (0.0f, 1.0f, (cardSpring.value - 0.55f) / 0.45f));

        if (phase == Phase::intro && t > introEnd)
        {
            if (welcome) showWelcomeCard();
            else         leave (false);
        }

        if (phase == Phase::leaving)
        {
            // dissolve, the logo grows a little as it goes ("materialize" in reverse)
            const float k = leaveProgress();
            setAlpha (1.0f - k);
            if (k >= 1.0f)
            {
                vblank.reset();
                setVisible (false);
                if (onFinished) onFinished();
                if (takeTour && onTour) onTour();
                return;
            }
        }

        // only redraw while something moves: a settled welcome card costs nothing
        const float since = t - strikeTime;
        const bool moving = phase != Phase::welcome || ! handSpring.isSettled() || ! cardSpring.isSettled()
                             || (since >= 0.0f && since < ringLength + 0.5f);       // ringing, or ripples still spreading
        if (moving)
            repaint();
    }

    void showWelcomeCard()
    {
        phase = Phase::welcome;
        setOpaque (false);                                          // the tuner shows through the veil
        handSpring.snapTo (1.0f);
        cardSpring.target = 1.0f;
        tourButton.setVisible (true);
        startButton.setVisible (true);
        tourButton.setAlpha (0.0f);
        startButton.setAlpha (0.0f);
    }

    void leave (bool tour)
    {
        if (phase == Phase::leaving)
            return;
        takeTour = tour;
        phase = Phase::leaving;
        setOpaque (false);
        leaveTime = elapsed();
        if (welcome)
        {
            Theme::settings().setValue ("welcomeSeen", true);      // only once
            Theme::settings().saveIfNeeded();
        }
    }

    // where the logo is right now: in the middle during the intro, on top of the card in the welcome
    juce::Rectangle<float> logoArea() const
    {
        const float m = juce::jlimit (0.0f, 1.2f, cardSpring.value);
        auto r = introLogo;
        if (m > 0.0f)
        {
            auto lerp = [m] (float a, float b) { return a + (b - a) * m; };
            r = { lerp (introLogo.getX(), welcomeLogo.getX()), lerp (introLogo.getY(), welcomeLogo.getY()),
                  lerp (introLogo.getWidth(), welcomeLogo.getWidth()), lerp (introLogo.getHeight(), welcomeLogo.getHeight()) };
        }
        if (phase == Phase::leaving)
        {
            const float grow = 1.0f + 0.06f * leaveProgress();
            r = r.withSizeKeepingCentre (r.getWidth() * grow, r.getHeight() * grow);
        }
        return r;
    }

    //==============================================================================
    // The logo: sound ripples behind it, the hand, the fork's shadow and the fork.
    //
    // A real tuning fork vibrates 440 times a second, far too fast to follow with the eye:
    // what you see is a soft BLUR around the tines, widest at the tips, shrinking as the
    // sound dies away. So under the crisp fork we draw faint "ghost" forks at the two ends
    // of the swing: a wide pair that fades quickly and a narrow pair that lasts longer.
    //
    // The tines bend more towards their tips (like a ruler clamped to a table: the bend
    // grows with the square of the distance from the clamp), so the ghosts are drawn in thin
    // horizontal strips, each stretched sideways a little more than the one below. That is
    // slow, so it is done ONCE (renderLayers) and every frame just copies the pictures.

    // how strongly the fork rings, 0...1, "since" seconds after it was struck: one second in all
    static float ringEnvelope (float since)
    {
        if (since < 0.0f || since >= ringLength)
            return 0.0f;
        const float attack = smooth (since / 0.06f);                         // the strike
        return attack * std::exp (-since / 0.4f) * smooth ((ringLength - since) / 0.35f);
    }

    // 0 -> 1 with a gentle start and end ("ease in-out"); x outside 0...1 is clamped
    static float smooth (float x)
    {
        x = juce::jlimit (0.0f, 1.0f, x);
        return x * x * (3.0f - 2.0f * x);
    }

    float leaveProgress() const     { return smooth ((elapsed() - leaveTime) / leaveLength); }

    // pre-render the logo's layers at the exact number of screen pixels they will cover
    void renderLayers()
    {
        float scale = 1.0f;
        if (auto* display = juce::Desktop::getInstance().getDisplays().getPrimaryDisplay())
            scale = (float) display->scale;
        const int w = juce::roundToInt (introLogo.getWidth() * scale), h = juce::roundToInt (introLogo.getHeight() * scale);
        if (w <= 0 || h <= 0 || (w == layers.hand.getWidth() && h == layers.hand.getHeight()))
            return;

        const juce::Rectangle<float> area (0.0f, 0.0f, (float) w, (float) h);
        auto render = [&] (auto&& draw)
        {
            juce::Image img (juce::Image::ARGB, w, h, true);
            juce::Graphics g (img);
            g.setImageResamplingQuality (juce::Graphics::highResamplingQuality);
            draw (g);
            return img;
        };
        layers.hand   = render ([&] (juce::Graphics& g) { g.drawImage (hand, area); });
        layers.shadow = render ([&] (juce::Graphics& g) { g.drawImage (shadow, area.translated (area.getWidth() * 0.007f, area.getHeight() * 0.018f)); });
        layers.fork   = render ([&] (juce::Graphics& g) { g.drawImage (fork, area); });
        layers.wide   = render ([&] (juce::Graphics& g) { for (float side : { -1.0f, 1.0f }) drawForkLayer (g, fork, area, side * 0.20f); });
        layers.narrow = render ([&] (juce::Graphics& g) { for (float side : { -1.0f, 1.0f }) drawForkLayer (g, fork, area, side * 0.08f); });
    }
    void drawLogo (juce::Graphics& g, juce::Rectangle<float> area, float t) const
    {
        const float appear = handSpring.value;
        if (appear <= 0.001f)
            return;

        juce::Graphics::ScopedSaveState s (g);
        g.setImageResamplingQuality (juce::Graphics::mediumResamplingQuality);

        const auto art = area.withSizeKeepingCentre (area.getWidth() * appear, area.getHeight() * appear);
       #if JUCE_IOS
        const float opacity = 1.0f;                            // already on the launch screen
       #else
        const float opacity = smooth (t / 0.22f);              // fades in while it grows
       #endif

        const float since = t - strikeTime;
        const float ring  = ringEnvelope (since);

        // sound: thin circles spread out from the fork BEHIND the hand, fading as they grow
        const juce::Point<float> source (art.getX() + art.getWidth() * forkCentreX,
                                         art.getY() + art.getHeight() * (forkTop + tineRoot) * 0.5f);
        const auto rippleColour = Theme::ink().interpolatedWith (juce::Colour (0xff64d2ff), 0.35f);
        for (int k = 0; k < 3; ++k)                       // three ripples, 0.22 s apart, each lives 0.8 s
        {
            juce::Graphics::ScopedSaveState rippleState (g);   // (images use the current colour's opacity: keep it separate)
            const float age = (since - 0.22f * (float) k) / 0.8f;
            if (age < 0.0f || age > 1.0f || Theme::reduceMotion())
                continue;
            const float spread = 1.0f - (1.0f - age) * (1.0f - age);      // fast at first, then slows down
            const float radius = art.getWidth() * (0.30f + 0.55f * spread);
            const float fade   = std::pow (1.0f - age, 1.6f);
            g.setColour (rippleColour.withAlpha (0.28f * fade));
            g.drawEllipse (juce::Rectangle<float> (radius * 2.0f, radius * 2.0f).withCentre (source), 1.0f + 1.5f * fade);
        }

        // (each drawImage uses the opacity set just before it)
        auto layer = [&] (const juce::Image& img, float alpha)
        {
            if (alpha > 0.003f)
            {
                g.setOpacity (alpha * opacity);
                g.drawImage (img, art);
            }
        };
        layer (layers.hand, 1.0f);
        layer (layers.shadow, 1.0f);
        layer (layers.wide,   0.30f * ring * ring);         // the wide blur fades first...
        layer (layers.narrow, 0.30f * ring);                // ...the narrow one lingers: the blur shrinks
        layer (layers.fork, 1.0f);
    }

    // the fork with its tines bent apart (spread > 0) or together (spread < 0)
    void drawForkLayer (juce::Graphics& g, const juce::Image& img, juce::Rectangle<float> art, float spread) const
    {
        const auto toArt = juce::AffineTransform::scale (art.getWidth() / (float) img.getWidth(), art.getHeight() / (float) img.getHeight())
                               .translated (art.getX(), art.getY());
        const float cx    = art.getX() + art.getWidth() * forkCentreX;
        const float root  = art.getY() + art.getHeight() * tineRoot;
        const float top   = art.getY() + art.getHeight() * (forkTop - 0.03f);
        const int   seam  = (int) std::round (root);    // one pixel row where the still part meets the moving part

        // the stem and the bottom of the U: they hold still
        {
            juce::Graphics::ScopedSaveState s (g);
            g.reduceClipRegion ((int) (art.getX() - art.getWidth()), seam, (int) (art.getWidth() * 3.0f), (int) (art.getHeight() * 2.0f));
            g.drawImageTransformed (img, toArt);
        }

        // the tines, strip by strip
        if (std::abs (spread) < 0.0005f)
        {
            juce::Graphics::ScopedSaveState s (g);
            const int y0 = (int) std::floor (top - art.getHeight());
            g.reduceClipRegion ((int) (art.getX() - art.getWidth()), y0, (int) (art.getWidth() * 3.0f), seam - y0);
            g.drawImageTransformed (img, toArt);
            return;
        }

        const int y0 = (int) std::floor (top - art.getHeight() * 0.05f), y1 = seam;
        const int stripHeight = 2;
        for (int y = y0; y < y1; y += stripHeight)
        {
            const float h = juce::jlimit (0.0f, 1.0f, (root - ((float) y + 1.0f)) / (root - top));   // 0 at the root, 1 at the tips
            const float stretch = 1.0f + spread * h * h;
            juce::Graphics::ScopedSaveState s (g);
            g.reduceClipRegion ((int) (art.getX() - art.getWidth()), y, (int) (art.getWidth() * 3.0f), juce::jmin (stripHeight, y1 - y));
            g.drawImageTransformed (img, toArt.followedBy (juce::AffineTransform::scale (stretch, 1.0f, cx, 0.0f)));
        }
    }

    float artAspect() const     { return (float) hand.getWidth() / (float) juce::jmax (1, hand.getHeight()); }
    static double now()         { return juce::Time::getMillisecondCounterHiRes() * 0.001; }
    float elapsed() const       { return (float) (now() - startTime); }

    //==============================================================================
    struct BlueButton  : public juce::Button
    {
        BlueButton() : juce::Button ("Take the quick tour") {}
        void paintButton (juce::Graphics& g, bool over, bool down) override
        {
            auto r = getLocalBounds().toFloat().reduced (0.5f);
            Theme::drawGlass (g, r, r.getHeight() * 0.5f, 0.0f,
                              juce::Colour (0xff0a84ff).withAlpha (down ? 1.0f : (over ? 0.95f : 0.88f)));
            g.setColour (juce::Colours::white);
            g.setFont (juce::FontOptions (17.0f, juce::Font::bold));
            g.drawText (getButtonText(), getLocalBounds(), juce::Justification::centred);
        }
    };

    // the artwork, built into the app (see Resources/ and the .jucer file)
    const juce::Image hand   = juce::ImageCache::getFromMemory (BinaryData::hand_png, BinaryData::hand_pngSize);
    const juce::Image fork   = juce::ImageCache::getFromMemory (BinaryData::fork_png, BinaryData::fork_pngSize);
    const juce::Image shadow = juce::ImageCache::getFromMemory (BinaryData::fork_shadow_png, BinaryData::fork_shadow_pngSize);

    // where the fork is in those images (fractions of the width/height, measured by Resources/IconArt/split.py)
    static constexpr float forkCentreX = 0.5318f, forkTop = 0.2115f, tineRoot = 0.6070f;

    // the timing, in seconds
    static constexpr float strikeAt    = 0.35f;     // the fork is struck as the pop settles
    static constexpr float ringLength  = 1.0f;      // it rings for one second
    static constexpr float introEnd    = 1.5f;      // then the tuner (or the welcome card) takes over
    static constexpr float leaveLength = 0.5f;      // the dissolve

    struct Layers { juce::Image hand, shadow, fork, wide, narrow; } layers;
    std::unique_ptr<juce::VBlankAttachment> vblank;
    double lastFrame = 0.0;

    BlueButton tourButton;
    GlassButton startButton { "Start tuning" };

    Phase phase = Phase::intro;
    bool welcome = false, takeTour = false;
    double startTime = 0.0;
    float strikeTime = strikeAt, leaveTime = 0.0f;
    Spring handSpring, cardSpring;
    juce::BorderSize<int> insets;
    juce::Rectangle<float> introLogo, welcomeLogo, card, textArea, footnote;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (IntroComponent)
};
