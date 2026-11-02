/*
  ==============================================================================

    GuideComponent.h
    Created: 26 Oct 2026
    Author:  Ali Fuat Sakacı

  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>
#include "Theme.h"
#include "HeaderComponent.h"     // for GlassButton

//==============================================================================
/*
    "Music basics": an interactive slideshow on a glass sheet over the tuner.

    Every slide has a moving picture, and tapping the picture plays sound through
    the tuner's own Karplus-Strong synth (via the playNote / playPair callbacks).
    The pictures are drawn in paint() and animated by a 60 Hz timer.
*/
class GuideComponent  : public juce::Component, private juce::Timer
{
public:
    std::function<void (float hz)> playNote;                 // pluck one string
    std::function<void (float hz1, float hz2)> playPair;     // pluck two at once (for beats)
    std::function<float()> getReference;                     // A4 in Hz (normally 440)

    GuideComponent()
    {
        slides = {
            { "Meet your six strings",
              "A guitar has six strings. The thickest sounds lowest (E2, 82 Hz), the thinnest sounds "
              "highest (E4, 330 Hz). Tap a string to hear it." },
            { "Eddie Ate Dynamite, Good Bye Eddie",
              "From thickest to thinnest the strings are E A D G B E. This silly sentence helps you "
              "remember them. Tap to hear all six." },
            { "Every note is a vibration",
              "A string swings back and forth many times per second. The low E does it 82 times, the "
              "high E 330 times: faster means higher. Pick a string below." },
            { "The 12 notes",
              "Music uses 12 notes and then repeats them. They are the 12 places on the tuner ring. "
              "Tap one to hear it." },
            { "Sharps and flats",
              "The sharp sign means one step up, the flat sign one step down. So C sharp and D flat are "
              "the same key with two names. Tap the keys." },
            { "One fret = one semitone",
              "Each fret raises the note by one semitone. After 12 frets you are back at E, an octave "
              "higher. Frets get closer because each one multiplies the frequency by the 12th root of 2." },
            { "Octaves",
              "The same note name at double the frequency: A2 = 110 Hz, A3 = 220 Hz, A4 = 440 Hz. "
              "They sound alike, just higher. Tap each one." },
            { "Octave numbers",
              "E2 means the note E in octave 2. The number goes up at C, not at A: "
              "A2, B2, then C3. This surprises almost everyone." },
            { "Cents and beats",
              "Each semitone is split into 100 cents. Two notes a few cents apart \"beat\": the sound "
              "pulses. The closer they are, the slower it pulses. Tap a distance." },
            { "Reference pitch",
              "Every note is calculated from one reference: A4 = 440 Hz, the world standard. "
              "You can change it with the A4 button at the top. Tap to hear it." },
            { "The missing fundamental",
              "Laptop and phone mics barely hear 82 Hz, so the low E arrives mostly as its harmonics. "
              "Your brain still hears 82 Hz, and so does this tuner." },
            { "You're ready",
              "Pluck one string, watch the needle, and turn the peg slowly until the ring turns green. "
              "Tighten to go higher, loosen to go lower." },
        };

        prevButton.onClick = [this] { go (index - 1); };
        nextButton.onClick = [this] { if (index == numSlides() - 1) close(); else go (index + 1); };
        skipButton.onClick = [this] { close(); };
        addAndMakeVisible (prevButton);
        addAndMakeVisible (nextButton);
        addAndMakeVisible (skipButton);

        setWantsKeyboardFocus (true);
    }

    void open()
    {
        go (0);
        setVisible (true);
        toFront (true);
        grabKeyboardFocus();
        startTimerHz (60);              // animation
    }

    // iPhone: the screen corners, Dynamic Island and home bar must stay free
    void setInsets (juce::BorderSize<int> newInsets)
    {
        insets = newInsets;
        resized();
    }

    void close()
    {
        stopTimer();
        setVisible (false);
    }

    //==============================================================================
    void paint (juce::Graphics& g) override
    {
        const auto ink = Theme::ink();

        // dim the tuner behind, then the glass sheet
        g.fillAll (Theme::paper().withAlpha (0.6f));
        Theme::drawGlass (g, sheet, 34.0f, 0.0f, Theme::sheet().withAlpha (0.94f), true);

        // "MUSIC BASICS · 3 OF 12"
        text (g, "MUSIC BASICS" + dot() + juce::String (index + 1) + " OF " + juce::String (numSlides()),
              topRow, 13.0f, true, ink.withAlpha (0.62f), juce::Justification::centredLeft);

        // the picture
        Theme::drawGlass (g, visual, 24.0f, 0.06f, {}, false, 0.6f);
        drawVisual (g, picture());

        // title and text
        auto t = textArea;
        text (g, slides[(size_t) index].title, t.removeFromTop (40.0f), 26.0f, true, ink, juce::Justification::centredLeft);
        g.setColour (ink.withAlpha (0.75f));
        g.setFont (juce::FontOptions (16.5f));
        g.drawMultiLineText (slides[(size_t) index].body, (int) t.getX(), (int) t.getY() + 20, (int) t.getWidth(),
                             juce::Justification::left, 4.0f);

        // page dots: the current one is a wider pill
        const float w = (float) numSlides() * 14.0f + 12.0f;
        float x = dotsRow.getCentreX() - w * 0.5f;
        for (int i = 0; i < numSlides(); ++i)
        {
            const float dw = i == index ? 20.0f : 8.0f;
            g.setColour (ink.withAlpha (i == index ? 1.0f : 0.35f));
            g.fillRoundedRectangle (x, dotsRow.getCentreY() - 4.0f, dw, 8.0f, 4.0f);
            x += dw + 6.0f;
        }
    }

    void resized() override
    {
        auto b = insets.subtractedFrom (getLocalBounds()).toFloat();    // stay clear of the notch / home bar
        sheet = b.withSizeKeepingCentre (juce::jmin (560.0f, b.getWidth() - 40.0f), juce::jmin (720.0f, b.getHeight() - 40.0f));

        auto inner = sheet.reduced (24.0f);
        topRow = inner.removeFromTop (44.0f);
        skipButton.setBounds (topRow.removeFromRight (90.0f).toNearestInt());

        auto buttons = inner.removeFromBottom (52.0f);
        nextButton.setBounds (buttons.removeFromRight ((buttons.getWidth() - 12.0f) * 0.5f).toNearestInt());
        buttons.removeFromRight (12.0f);
        prevButton.setBounds (buttons.toNearestInt());

        dotsRow = inner.removeFromBottom (36.0f);
        inner.removeFromTop (12.0f);
        visual = inner.removeFromTop (inner.getHeight() * 0.64f);
        inner.removeFromTop (16.0f);
        textArea = inner;
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        if (visual.contains (e.position))
            clickVisual (e.position, picture());
    }

    bool keyPressed (const juce::KeyPress& key) override
    {
        if (key == juce::KeyPress::rightKey)  { go (index + 1); return true; }
        if (key == juce::KeyPress::leftKey)   { go (index - 1); return true; }
        if (key == juce::KeyPress::escapeKey) { close();        return true; }
        return false;
    }

private:
    struct Slide { juce::String title, body; };

    //==============================================================================
    // helpers

    int numSlides() const                 { return (int) slides.size(); }
    juce::Rectangle<float> picture() const { return visual.reduced (18.0f); }
    static double now()                   { return juce::Time::getMillisecondCounterHiRes() * 0.001; }
    double slideTime() const              { return now() - slideStart; }   // seconds since the slide opened
    double tapAge() const                 { return now() - tapTime; }      // seconds since the last tap

    static juce::String dot()             { return juce::String::fromUTF8 (" \xc2\xb7 "); }
    static juce::String sharp()           { return juce::String::fromUTF8 ("\xe2\x99\xaf"); }
    static juce::String flat()            { return juce::String::fromUTF8 ("\xe2\x99\xad"); }

    float hz (int midi) const
    {
        const float a4 = getReference ? getReference() : 440.0f;
        return a4 * std::pow (2.0f, (float) (midi - 69) / 12.0f);
    }

    static juce::String noteName (int midi)
    {
        static const char* names[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
        return juce::String (names[midi % 12]).replace ("#", sharp()) + juce::String (midi / 12 - 1);
    }

    void play (int midi)                   { if (playNote) playNote (hz (midi)); }

    void tap (int which)
    {
        selected = which;
        tapTime = now();
    }

    void go (int newIndex)
    {
        index = juce::jlimit (0, numSlides() - 1, newIndex);
        selected = (index == 2 ? 0 : (index == 8 ? 2 : -1));   // some slides start with something chosen
        tapTime = -100.0;
        slideStart = now();

        prevButton.setAlpha (index == 0 ? 0.4f : 1.0f);
        skipButton.setVisible (index < numSlides() - 1);      // on the last slide "Done" does the same
        nextButton.setButtonText (index == numSlides() - 1 ? "Done" : "Next");
        repaint();
    }

    void timerCallback() override { repaint(); }

    static void text (juce::Graphics& g, const juce::String& s, juce::Rectangle<float> r, float size, bool bold,
                      juce::Colour c, juce::Justification j = juce::Justification::centred)
    {
        g.setColour (Theme::readable (c));
        g.setFont (juce::FontOptions (size, bold ? juce::Font::bold : juce::Font::plain));
        g.drawText (s, r, j);
    }

    // a round "chip" with a label, used on several slides
    static void chip (juce::Graphics& g, juce::Rectangle<float> r, const juce::String& label, bool lit,
                      juce::Colour litColour, float fontSize = 17.0f)
    {
        g.setColour (lit ? litColour.withAlpha (0.35f) : Theme::ink().withAlpha (0.08f));
        g.fillEllipse (r);
        g.setColour (lit ? litColour : Theme::ink().withAlpha (0.22f));
        g.drawEllipse (r, lit ? 2.0f : 1.0f);
        text (g, label, r, fontSize, true, Theme::ink());
    }

    // a sine-like wave across r: "cycles" periods, moving to the left over time
    static void wave (juce::Graphics& g, juce::Rectangle<float> r, float cycles, float amplitude, float phase,
                      juce::Colour c, float thickness = 2.0f)
    {
        juce::Path p;
        for (int i = 0; i <= 200; ++i)
        {
            const float u = (float) i / 200.0f;
            const float y = r.getCentreY() - amplitude * std::sin (juce::MathConstants<float>::twoPi * cycles * u + phase);
            if (i == 0) p.startNewSubPath (r.getX(), y);
            else        p.lineTo (r.getX() + u * r.getWidth(), y);
        }
        g.setColour (c);
        g.strokePath (p, juce::PathStrokeType (thickness));
    }

    // the six strings, low E first
    static constexpr int stringMidi[6] = { 40, 45, 50, 55, 59, 64 };   // E2 A2 D3 G3 B3 E4

    //==============================================================================
    // one switch for drawing and one for clicking: like a switch in C, one case per slide

    void drawVisual (juce::Graphics& g, juce::Rectangle<float> r)
    {
        const auto ink = Theme::ink();
        const float t = (float) slideTime();
        const float age = (float) tapAge();

        switch (index)
        {
            case 0:   // ---- the six strings, high e at the top like in a guitar tab ----
            {
                for (int row = 0; row < 6; ++row)
                {
                    const int s = 5 - row;                                    // 0 = low E ... 5 = high e
                    const float y = r.getY() + r.getHeight() * ((float) row + 0.5f) / 6.0f;
                    const float thickness = 1.2f + (float) (5 - s) * 0.7f;    // low E is the thickest
                    const bool wound = s < 3;                                 // the 3 low strings are wound: golden
                    const bool active = s == selected && age < 4.0f;

                    text (g, noteName (stringMidi[s]), { r.getX(), y - 12.0f, 44.0f, 24.0f }, 17.0f, true,
                          active ? RingComponentColours::green : ink);
                    text (g, juce::String (juce::roundToInt (hz (stringMidi[s]))) + " Hz", { r.getX() + 40.0f, y - 10.0f, 56.0f, 20.0f },
                          12.0f, false, ink.withAlpha (0.55f), juce::Justification::centredLeft);

                    // a plucked string vibrates: a half sine shape whose size decays
                    const float amp = active ? 11.0f * std::exp (-age * 1.1f) * std::sin (age * 40.0f + (float) s) : 0.0f;
                    const float x0 = r.getX() + 100.0f, x1 = r.getRight();
                    juce::Path p;
                    for (int i = 0; i <= 60; ++i)
                    {
                        const float u = (float) i / 60.0f;
                        const float yy = y + amp * std::sin (juce::MathConstants<float>::pi * u);
                        if (i == 0) p.startNewSubPath (x0, yy); else p.lineTo (x0 + u * (x1 - x0), yy);
                    }
                    g.setColour (active ? RingComponentColours::green : (wound ? juce::Colour (0xffd8c9a8) : ink.withAlpha (0.85f)));
                    g.strokePath (p, juce::PathStrokeType (thickness));
                }
                break;
            }

            case 1:   // ---- the mnemonic: six words, six strings ----
            {
                static const char* words[] = { "Eddie", "Ate", "Dynamite,", "Good", "Bye", "Eddie" };
                const bool strumming = age < 1.8f;
                const int lit = strumming ? (int) (age / 0.25f) : (int) (t * 1.4f) % 6;
                const float colW = r.getWidth() / 6.0f;

                for (int i = 0; i < 6; ++i)
                {
                    auto col = r.withX (r.getX() + colW * (float) i).withWidth (colW);
                    const bool on = i == lit;
                    chip (g, juce::Rectangle<float> (50.0f, 50.0f).withCentre ({ col.getCentreX(), r.getY() + r.getHeight() * 0.38f }),
                          noteName (stringMidi[i]).dropLastCharacters (1), on, RingComponentColours::green, 20.0f);
                    text (g, words[i], { col.getX(), r.getY() + r.getHeight() * 0.62f, colW, 22.0f }, 14.0f, on,
                          ink.withAlpha (on ? 1.0f : 0.6f));
                    text (g, noteName (stringMidi[i]), { col.getX(), r.getY() + r.getHeight() * 0.62f + 22.0f, colW, 18.0f }, 11.0f, false,
                          ink.withAlpha (0.45f));
                }
                break;
            }

            case 2:   // ---- frequency: a slow-motion wave for the chosen string ----
            {
                const int s = juce::jmax (0, selected);
                const float f = hz (stringMidi[s]);
                auto top = r.removeFromTop (54.0f);
                text (g, juce::String (f, 2) + " Hz", top.removeFromTop (34.0f), 28.0f, true, RingComponentColours::green);
                text (g, "vibrations per second", top, 13.0f, false, ink.withAlpha (0.55f));

                auto chips = r.removeFromBottom (40.0f);
                wave (g, r.reduced (0.0f, 8.0f), f / 27.0f, r.getHeight() * 0.32f, -t * 3.0f, ink.withAlpha (0.85f), 2.5f);

                const float cw = 40.0f, gap = 10.0f, total = 6.0f * cw + 5.0f * gap;
                for (int i = 0; i < 6; ++i)
                    chip (g, { chips.getCentreX() - total * 0.5f + (float) i * (cw + gap), chips.getY(), cw, cw },
                          noteName (stringMidi[i]).dropLastCharacters (1), i == s, RingComponentColours::green, 15.0f);
                break;
            }

            case 3:   // ---- the 12 notes on a small ring ----
            {
                const auto c = r.getCentre();
                const float radius = juce::jmin (r.getWidth(), r.getHeight()) * 0.40f;
                const int lit = (selected >= 0 && age < 1.0f) ? selected : (int) (t * 2.5f) % 12;
                g.setColour (ink.withAlpha (0.10f));
                g.drawEllipse (juce::Rectangle<float> (radius * 2.0f, radius * 2.0f).withCentre (c), 1.0f);

                for (int i = 0; i < 12; ++i)
                {
                    const float a = juce::degreesToRadians ((float) i * 30.0f);
                    const auto p = c + juce::Point<float> (radius * std::sin (a), -radius * std::cos (a));
                    chip (g, juce::Rectangle<float> (40.0f, 40.0f).withCentre (p), noteName (48 + i).dropLastCharacters (1),
                          i == lit, RingComponentColours::green, 14.0f);
                }
                text (g, "12", juce::Rectangle<float> (80.0f, 40.0f).withCentre (c.translated (0.0f, -8.0f)), 32.0f, true, ink);
                text (g, "notes", juce::Rectangle<float> (80.0f, 20.0f).withCentre (c.translated (0.0f, 18.0f)), 13.0f, false, ink.withAlpha (0.6f));
                break;
            }

            case 4:   // ---- a piano octave: the black keys have two names ----
            {
                pianoKeys (r, [&] (juce::Rectangle<float> key, int semitone, bool black)
                {
                    const bool pressed = semitone == selected && age < 0.6f;
                    if (black)
                    {
                        g.setColour (pressed ? RingComponentColours::green : juce::Colour (0xff1c1c1e));
                        g.fillRoundedRectangle (key, 4.0f);
                        g.setColour (juce::Colours::white.withAlpha (0.3f));      // black keys: white details
                        g.drawRoundedRectangle (key, 4.0f, 1.0f);
                        const auto name = noteName (48 + semitone).dropLastCharacters (1);
                        const auto other = noteName (49 + semitone).dropLastCharacters (1) + flat();
                        text (g, name, key.withTrimmedTop (key.getHeight() - 44.0f).removeFromTop (20.0f), 11.0f, true, juce::Colours::white);
                        text (g, other, key.withTrimmedTop (key.getHeight() - 26.0f).removeFromTop (20.0f), 11.0f, true, juce::Colours::white.withAlpha (0.7f));
                    }
                    else
                    {
                        g.setColour (pressed ? RingComponentColours::green.withAlpha (0.8f) : juce::Colours::white.withAlpha (0.92f));
                        g.fillRoundedRectangle (key.reduced (1.5f, 0.0f), 5.0f);
                        text (g, noteName (48 + semitone).dropLastCharacters (1), key.removeFromBottom (28.0f), 14.0f, true,
                              juce::Colours::black.withAlpha (0.7f));
                    }
                });
                break;
            }

            case 5:   // ---- the fretboard of the low E string ----
            {
                const auto board = r.withSizeKeepingCentre (r.getWidth(), juce::jmin (r.getHeight(), 110.0f));
                g.setColour (juce::Colour (0xff4a3426));
                g.fillRoundedRectangle (board, 6.0f);

                // fret n sits at 1 - 2^(-n/12) of the string length (fret 12 = half way)
                auto fretX = [&] (int n) { return board.getX() + board.getWidth() * 2.0f * (1.0f - std::pow (2.0f, -(float) n / 12.0f)); };

                for (int n : { 3, 5, 7, 9 })
                {
                    g.setColour (ink.withAlpha (0.35f));
                    g.fillEllipse (juce::Rectangle<float> (9.0f, 9.0f).withCentre ({ (fretX (n - 1) + fretX (n)) * 0.5f, board.getCentreY() + 26.0f }));
                }
                for (float dy : { -26.0f, 26.0f })
                    g.fillEllipse (juce::Rectangle<float> (9.0f, 9.0f).withCentre ({ (fretX (11) + fretX (12)) * 0.5f, board.getCentreY() + dy }));

                for (int n = 0; n <= 12; ++n)
                {
                    g.setColour (n == 0 ? juce::Colours::white.withAlpha (0.9f) : juce::Colour (0xffc8c8cc));
                    g.fillRect (juce::Rectangle<float> (n == 0 ? 6.0f : 3.0f, board.getHeight()).withCentre ({ fretX (n), board.getCentreY() }));
                }

                g.setColour (juce::Colour (0xffd8c9a8));                               // the string itself
                g.drawLine (board.getX(), board.getCentreY(), board.getRight(), board.getCentreY(), 4.0f);

                if (selected >= 0)
                {
                    const float cx = selected == 0 ? board.getX() + 14.0f : (fretX (selected - 1) + fretX (selected)) * 0.5f;
                    chip (g, juce::Rectangle<float> (44.0f, 44.0f).withCentre ({ cx, board.getCentreY() }),
                          noteName (40 + selected).dropLastCharacters (1), true, RingComponentColours::green, 14.0f);
                    text (g, selected == 12 ? "E3" + dot() + "one octave up!" : "fret " + juce::String (selected) + dot() + noteName (40 + selected),
                          { r.getX(), board.getBottom() + 6.0f, r.getWidth(), 22.0f }, 14.0f, true, RingComponentColours::green);
                }
                else
                {
                    text (g, "tap between the frets", { r.getX(), board.getBottom() + 6.0f, r.getWidth(), 22.0f }, 13.0f, false, ink.withAlpha (0.5f));
                }
                break;
            }

            case 6:   // ---- octaves: 1, 2 and 4 wave periods in the same time ----
            {
                for (int row = 0; row < 3; ++row)
                {
                    auto band = r.withY (r.getY() + r.getHeight() * (float) row / 3.0f).withHeight (r.getHeight() / 3.0f);
                    const bool on = row == selected && age < 2.5f;
                    const int midi = 45 + 12 * row;                                     // A2, A3, A4
                    text (g, noteName (midi), band.removeFromLeft (56.0f).withTrimmedBottom (14.0f), 20.0f, true,
                          on ? RingComponentColours::green : ink);
                    text (g, juce::String (juce::roundToInt (hz (midi))) + " Hz", band.withX (band.getX() - 56.0f).withWidth (56.0f).withTrimmedTop (28.0f),
                          12.0f, false, ink.withAlpha (0.55f));
                    wave (g, band.reduced (6.0f, 10.0f), (float) (1 << row) * 1.5f, band.getHeight() * 0.28f, -t * 2.0f,
                          on ? RingComponentColours::green : ink.withAlpha (0.8f));
                }
                break;
            }

            case 7:   // ---- octave numbers change at C ----
            {
                static const int midis[] = { 45, 47, 48, 50, 52 };   // A2 B2 C3 D3 E3
                const float cw = 62.0f, gap = 14.0f, total = 5.0f * cw + 4.0f * gap + 20.0f;
                const int lit = (selected >= 0 && age < 1.0f) ? selected : (int) (t * 1.5f) % 5;
                float x = r.getCentreX() - total * 0.5f;
                const float y = r.getCentreY() - cw * 0.5f - 10.0f;

                for (int i = 0; i < 5; ++i)
                {
                    if (i == 2)                                                         // the border between B2 and C3
                    {
                        g.setColour (RingComponentColours::orange);
                        g.fillRoundedRectangle (x + 3.0f, y - 14.0f, 4.0f, cw + 28.0f, 2.0f);
                        x += 20.0f;
                    }
                    chip (g, { x, y, cw, cw }, noteName (midis[i]), i == lit, i >= 2 ? RingComponentColours::orange : RingComponentColours::green, 18.0f);
                    x += cw + gap;
                }
                text (g, "the number goes up here", { r.getX(), y + cw + 22.0f, r.getWidth(), 22.0f }, 14.0f, true, RingComponentColours::orange);
                break;
            }

            case 8:   // ---- cents and beats ----
            {
                static const int options[] = { 0, 3, 10, 30 };
                const int sel = juce::jlimit (0, 3, selected);
                const float base = hz (57);                                            // A3 = 220 Hz
                const float beatHz = base * (std::pow (2.0f, (float) options[sel] / 1200.0f) - 1.0f);

                auto pills = r.removeFromBottom (40.0f);
                auto caption = r.removeFromBottom (26.0f);

                // two notes added together: a fast wave whose loudness rises and falls (2 seconds shown)
                juce::Path p;
                const auto w = r.reduced (0.0f, 6.0f);
                for (int i = 0; i <= 600; ++i)
                {
                    const float u = (float) i / 600.0f;
                    const float tau = u * 2.0f + t * 0.5f;                              // seconds, scrolling slowly
                    const float env = std::abs (std::cos (juce::MathConstants<float>::pi * beatHz * tau));
                    const float y = w.getCentreY() - w.getHeight() * 0.45f * env * std::sin (juce::MathConstants<float>::twoPi * 40.0f * u);
                    if (i == 0) p.startNewSubPath (w.getX(), y); else p.lineTo (w.getX() + u * w.getWidth(), y);
                }
                g.setColour (options[sel] <= 3 ? RingComponentColours::green : RingComponentColours::orange);
                g.strokePath (p, juce::PathStrokeType (1.4f));

                text (g, juce::String (options[sel]) + " cents apart" + dot()
                         + (options[sel] == 0 ? juce::String ("no beats: perfectly in tune")
                                              : juce::String (beatHz, 1) + " beats per second"),
                      caption, 14.0f, true, ink.withAlpha (0.85f));

                const float pw = 74.0f, gap = 10.0f, total = 4.0f * pw + 3.0f * gap;
                for (int i = 0; i < 4; ++i)
                {
                    juce::Rectangle<float> pill (pills.getCentreX() - total * 0.5f + (float) i * (pw + gap), pills.getY() + 4.0f, pw, 34.0f);
                    g.setColour (i == sel ? ink.withAlpha (0.24f) : ink.withAlpha (0.08f));
                    g.fillRoundedRectangle (pill, 17.0f);
                    g.setColour (ink.withAlpha (0.22f));
                    g.drawRoundedRectangle (pill, 17.0f, 1.0f);
                    text (g, juce::String (options[i]) + juce::String::fromUTF8 ("\xc2\xa2"), pill, 15.0f, true, ink);
                }
                break;
            }

            case 9:   // ---- reference pitch: a tuning fork sending out sound waves ----
            {
                const auto c = r.getCentre().translated (-r.getWidth() * 0.18f, 10.0f);
                juce::Path fork;                                                       // a U with a handle
                fork.startNewSubPath (c.x - 16.0f, c.y - 70.0f);
                fork.lineTo (c.x - 16.0f, c.y);
                fork.quadraticTo (c.x - 16.0f, c.y + 18.0f, c.x, c.y + 18.0f);
                fork.quadraticTo (c.x + 16.0f, c.y + 18.0f, c.x + 16.0f, c.y);
                fork.lineTo (c.x + 16.0f, c.y - 70.0f);
                fork.startNewSubPath (c.x, c.y + 18.0f);
                fork.lineTo (c.x, c.y + 60.0f);
                g.setColour (juce::Colour (0xffc8c8cc));
                g.strokePath (fork, juce::PathStrokeType (7.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

                for (int k = 0; k < 3; ++k)                                            // rings moving outwards
                {
                    const float phase = std::fmod (t * 0.6f + (float) k / 3.0f, 1.0f);
                    const float rad = 30.0f + phase * 90.0f;
                    g.setColour (RingComponentColours::green.withAlpha (0.6f * (1.0f - phase)));
                    g.drawEllipse (juce::Rectangle<float> (rad * 2.0f, rad * 2.0f).withCentre (c.translated (0.0f, -50.0f)), 2.0f);
                }

                auto right = r.withTrimmedLeft (r.getWidth() * 0.52f);
                text (g, "A4", right.withHeight (60.0f).withCentre ({ right.getCentreX(), r.getCentreY() - 24.0f }), 48.0f, true, ink);
                text (g, juce::String (juce::roundToInt (hz (69))) + " Hz", right.withHeight (30.0f).withCentre ({ right.getCentreX(), r.getCentreY() + 22.0f }),
                      22.0f, true, RingComponentColours::green);
                break;
            }

            case 10:  // ---- the missing fundamental: what the mic actually hears ----
            {
                static const float levels[] = { 0.0f, 1.0f, 0.5f, 0.65f, 0.35f };
                auto bars = r.withTrimmedBottom (26.0f);
                const float bw = 44.0f, gap = 22.0f, total = 5.0f * bw + 4.0f * gap;
                for (int i = 0; i < 5; ++i)
                {
                    const float x = bars.getCentreX() - total * 0.5f + (float) i * (bw + gap);
                    const float breathe = 1.0f + 0.04f * std::sin (t * 2.0f + (float) i);
                    const float h = i == 0 ? 26.0f : (bars.getHeight() - 30.0f) * levels[i] * breathe;
                    juce::Rectangle<float> bar (x, bars.getBottom() - 22.0f - h, bw, h);

                    if (i == 0)
                    {
                        juce::Path outline;
                        outline.addRoundedRectangle (bar, 8.0f);
                        juce::Path dashed;
                        const float dashes[] = { 5.0f, 4.0f };
                        juce::PathStrokeType (1.5f).createDashedStroke (dashed, outline, dashes, 2);
                        g.setColour (ink.withAlpha (0.6f));
                        g.fillPath (dashed);
                    }
                    else
                    {
                        g.setColour (RingComponentColours::orange.withAlpha (0.5f + 0.5f * levels[i]));
                        g.fillRoundedRectangle (bar, 8.0f);
                    }
                    text (g, juce::String (juce::roundToInt (hz (40) * (float) (i + 1))), { x - 10.0f, bars.getBottom() - 20.0f, bw + 20.0f, 18.0f },
                          12.0f, false, ink.withAlpha (0.7f));
                }
                text (g, "Hz" + dot() + "your low E through a laptop mic", r.removeFromBottom (22.0f), 13.0f, false, ink.withAlpha (0.55f));
                break;
            }

            case 11:  // ---- ready: a green ring that fills up ----
            {
                const auto c = r.getCentre();
                const float radius = juce::jmin (r.getWidth(), r.getHeight()) * 0.34f;
                const float progress = juce::jmin (1.0f, t / 1.2f);
                juce::Path arc;
                arc.addCentredArc (c.x, c.y, radius, radius, 0.0f, 0.0f, juce::MathConstants<float>::twoPi * progress, true);
                g.setColour (ink.withAlpha (0.1f));
                g.drawEllipse (juce::Rectangle<float> (radius * 2.0f, radius * 2.0f).withCentre (c), 12.0f);
                g.setColour (RingComponentColours::green);
                g.strokePath (arc, juce::PathStrokeType (12.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

                if (progress >= 1.0f)
                {
                    juce::Path tick;
                    tick.startNewSubPath (c.x - radius * 0.35f, c.y + 2.0f);
                    tick.lineTo (c.x - radius * 0.08f, c.y + radius * 0.28f);
                    tick.lineTo (c.x + radius * 0.40f, c.y - radius * 0.30f);
                    g.strokePath (tick, juce::PathStrokeType (10.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
                }
                break;
            }

            default: break;
        }
    }

    void clickVisual (juce::Point<float> p, juce::Rectangle<float> r)
    {
        switch (index)
        {
            case 0:   // which string row?
            {
                const int row = juce::jlimit (0, 5, (int) ((p.y - r.getY()) / (r.getHeight() / 6.0f)));
                tap (5 - row);
                play (stringMidi[5 - row]);
                break;
            }

            case 1:   // strum all six, one every 250 ms
            {
                tap (0);
                for (int i = 0; i < 6; ++i)
                {
                    // SafePointer: if the guide is deleted before the timer fires, it becomes null
                    juce::Timer::callAfterDelay (i * 250, [safe = juce::Component::SafePointer<GuideComponent> (this), i]
                    {
                        if (safe != nullptr)
                            safe->play (stringMidi[i]);
                    });
                }
                break;
            }

            case 2:   // the string chips at the bottom
            {
                const float cw = 40.0f, gap = 10.0f, total = 6.0f * cw + 5.0f * gap;
                const float x0 = r.getCentreX() - total * 0.5f;
                if (p.y > r.getBottom() - 44.0f)
                {
                    const int i = (int) ((p.x - x0) / (cw + gap));
                    if (i >= 0 && i < 6) { tap (i); play (stringMidi[i]); }
                }
                break;
            }

            case 3:   // the nearest note on the small ring
            {
                const auto d = p - r.getCentre();
                float deg = juce::radiansToDegrees (std::atan2 (d.x, -d.y));
                if (deg < 0.0f) deg += 360.0f;
                const int i = juce::roundToInt (deg / 30.0f) % 12;
                tap (i);
                play (48 + i);
                break;
            }

            case 4:   // piano: black keys sit on top of the white ones, so a black key wins
            {
                int whiteHit = -1, blackHit = -1;
                pianoKeys (r, [&] (juce::Rectangle<float> key, int semitone, bool black)
                {
                    if (key.contains (p))
                        (black ? blackHit : whiteHit) = semitone;
                });
                const int hit = blackHit >= 0 ? blackHit : whiteHit;
                if (hit >= 0) { tap (hit); play (48 + hit); }
                break;
            }

            case 5:   // which fret space was tapped?
            {
                const auto board = r.withSizeKeepingCentre (r.getWidth(), juce::jmin (r.getHeight(), 110.0f));
                const float u = (p.x - board.getX()) / (board.getWidth() * 2.0f);                // 0 ... 0.5
                const int fret = juce::jlimit (0, 12, (int) std::ceil (-12.0f * std::log2 (1.0f - juce::jlimit (0.0f, 0.5f, u))));
                tap (fret);
                play (40 + fret);
                break;
            }

            case 6:   // A2 / A3 / A4 rows
            {
                const int row = juce::jlimit (0, 2, (int) ((p.y - r.getY()) / (r.getHeight() / 3.0f)));
                tap (row);
                play (45 + 12 * row);
                break;
            }

            case 7:   // the five notes
            {
                static const int midis[] = { 45, 47, 48, 50, 52 };
                const float cw = 62.0f, gap = 14.0f, total = 5.0f * cw + 4.0f * gap + 20.0f;
                float x = r.getCentreX() - total * 0.5f;
                for (int i = 0; i < 5; ++i)
                {
                    if (i == 2) x += 20.0f;
                    if (p.x >= x && p.x < x + cw) { tap (i); play (midis[i]); }
                    x += cw + gap;
                }
                break;
            }

            case 8:   // the four distance pills: play two notes that far apart
            {
                static const int options[] = { 0, 3, 10, 30 };
                const float pw = 74.0f, gap = 10.0f, total = 4.0f * pw + 3.0f * gap;
                const float x0 = r.getCentreX() - total * 0.5f;
                const int i = (int) ((p.x - x0) / (pw + gap));
                if (p.y > r.getBottom() - 44.0f && i >= 0 && i < 4)
                {
                    tap (i);
                    if (playPair)
                        playPair (hz (57), hz (57) * std::pow (2.0f, (float) options[i] / 1200.0f));
                }
                break;
            }

            case 9:  play (69); tap (0); break;      // A4
            case 10: play (40); tap (0); break;      // the low E itself
            default: break;
        }
    }

    // calls draw (rectangle, semitone 0..12, isBlack) for each key of one octave C3..C4
    template <typename Fn>
    static void pianoKeys (juce::Rectangle<float> r, Fn&& draw)
    {
        static const int whiteSemis[] = { 0, 2, 4, 5, 7, 9, 11, 12 };
        static const int blackAfter[] = { 0, 1, 3, 4, 5 };          // a black key after these white keys
        static const int blackSemis[] = { 1, 3, 6, 8, 10 };
        const float kw = r.getWidth() / 8.0f;

        for (int i = 0; i < 8; ++i)
            draw (juce::Rectangle<float> (r.getX() + kw * (float) i, r.getY(), kw, r.getHeight()), whiteSemis[i], false);
        for (int i = 0; i < 5; ++i)
            draw (juce::Rectangle<float> (kw * 0.62f, r.getHeight() * 0.6f)
                      .withPosition (r.getX() + kw * (float) (blackAfter[i] + 1) - kw * 0.31f, r.getY()),
                  blackSemis[i], true);
    }

    // the same colours as the ring (kept here so this file doesn't depend on RingComponent.h)
    struct RingComponentColours
    {
        static inline const juce::Colour green  { 0xff30d158 };
        static inline const juce::Colour orange { 0xffff9f0a };
    };

    //==============================================================================
    // a blue "Next" button
    struct BlueButton  : public juce::Button
    {
        BlueButton() : juce::Button ("Next") {}
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

    std::vector<Slide> slides;
    juce::BorderSize<int> insets;
    int index = 0;
    int selected = -1;               // what was tapped on this slide (-1 = nothing yet)
    double tapTime = -100.0, slideStart = 0.0;

    GlassButton prevButton { "Previous" };
    GlassButton skipButton { "Skip" };
    BlueButton  nextButton;

    juce::Rectangle<float> sheet, topRow, visual, textArea, dotsRow;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (GuideComponent)
};
