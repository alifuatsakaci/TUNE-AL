/*
  ==============================================================================

    RingComponent.h
    Created: 23 Oct 2026 1:05:48pm
    Author:  Ali Fuat Sakacı

  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>
#include "Theme.h"

//==============================================================================
/*
    The chromatic ring from the Figma design: 12 notes around a thick band,
    C at the top, a needle that follows the pitch (30 degrees per semitone),
    and a centre disc with the note name, the cents and a fine-tuning bar.

    It only draws. MainComponent tells it what to show with setReading().
    All sizes are written for a 342 px ring (the Figma size) and scaled by k.
*/
class RingComponent  : public juce::Component
{
public:
    // Colours from the design (iOS system colours)
    static inline const juce::Colour green  { 0xff30d158 };
    static inline const juce::Colour orange { 0xffff9f0a };
    static inline const juce::Colour blue   { 0xff0a84ff };

    // manual mode: called with the note number (0 = C ... 11 = B) when a note on the ring is clicked
    std::function<void (int note)> onNoteClicked;

    RingComponent() = default;

    void setReading (bool newHasPitch, int newNote, int newOctave, float newCents)
    {
        newNote  = juce::jlimit (0, 11, newNote);
        newCents = juce::jlimit (-50.0f, 50.0f, newCents);

        // only redraw if something visible changed (the timer calls this 60 times per second)
        if (newHasPitch == hasPitch && newNote == note && newOctave == octave && std::abs (newCents - cents) < 0.05f)
            return;

        hasPitch = newHasPitch;
        note     = newNote;
        octave   = newOctave;
        cents    = newCents;
        repaint();
    }

    void setManual (bool shouldBeManual)
    {
        manual = shouldBeManual;
        setMouseCursor (manual ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
        repaint();
    }

    // manual mode: the note you are tuning to, and how far the guitar is from it
    void setTarget (int newNote, int newOctave, float newHz)
    {
        targetNote = newNote;
        targetOctave = newOctave;
        targetHz = newHz;
        repaint();
    }

    void setTargetCents (bool hasCents, float newCents)
    {
        newCents = juce::jlimit (-50.0f, 50.0f, newCents);
        if (hasCents == hasTargetCents && std::abs (newCents - targetCents) < 0.05f)
            return;                                              // nothing changed: no redraw
        hasTargetCents = hasCents;
        targetCents = newCents;
        repaint();
    }

    // manual mode: which note did the user click? (angle -> note, like the needle in reverse)
    void mouseDown (const juce::MouseEvent& e) override
    {
        if (! manual || ! onNoteClicked)
            return;

        const auto c = getLocalBounds().toFloat().getCentre();
        const float k = (float) juce::jmin (getWidth(), getHeight()) / 342.0f;
        const auto d = e.position - c;
        const float radius = d.getDistanceFromOrigin() / k;

        if (radius < 60.0f || radius > 170.0f)          // not on the ring
            return;

        float degrees = juce::radiansToDegrees (std::atan2 (d.x, -d.y));   // 0 = up, clockwise
        if (degrees < 0.0f)
            degrees += 360.0f;

        onNoteClicked (juce::roundToInt (degrees / 30.0f) % 12);
    }

    // The colour that means "how in tune": green, orange, grey when silent, blue in manual mode
    juce::Colour getStateColour() const
    {
        if (manual)
            return blue;
        if (! hasPitch)
            return Theme::ink().withAlpha (0.35f);
        return std::abs (cents) <= 3.0f ? green : orange;
    }

    void paint (juce::Graphics& g) override
    {
        const float size = (float) juce::jmin (getWidth(), getHeight());
        const float k    = size / 342.0f;                       // scale from the 342 px design
        const juce::Point<float> c (getWidth() * 0.5f, getHeight() * 0.5f);
        const auto colour = getStateColour();
        const auto ink    = Theme::ink();

        // a point on a circle: JUCE angles start at 12 o'clock and go clockwise
        auto pointAt = [c, k] (float degrees, float radius)
        {
            const float a = juce::degreesToRadians (degrees);
            return c + juce::Point<float> (radius * k * std::sin (a), -radius * k * std::cos (a));
        };

        // a circle of the given radius (in design pixels) around the centre
        auto circle = [c, k] (float radius)
        {
            return juce::Rectangle<float> (2.0f * radius * k, 2.0f * radius * k).withCentre (c);
        };

        const float outerR = 154.0f, innerR = 102.0f, labelR = 128.0f;
        const float innerProportion = innerR / outerR;

        // ---- the band: a full "pie segment" with a hole in the middle (a donut) ----
        //      filled like glass: translucent body + a sheen from above (see Theme::drawGlass)
        juce::Path band;
        band.addPieSegment (circle (outerR), 0.0f, juce::MathConstants<float>::twoPi, innerProportion);
        g.setColour (Theme::glass (0.07f).withMultipliedAlpha (Theme::dark() ? 1.0f : 0.7f));
        g.fillPath (band);
        g.setGradientFill (Theme::sheen (circle (outerR), 0.9f));
        g.fillPath (band);

        // ---- the active note: a 30-degree slice of the donut ----
        // automatic mode: the note you are playing. Manual mode: the note you chose.
        const int sliceNote = manual ? targetNote : note;
        if (hasPitch || manual)
        {
            juce::Path slice;
            slice.addPieSegment (circle (outerR),
                                 juce::degreesToRadians (sliceNote * 30.0f - 15.0f),
                                 juce::degreesToRadians (sliceNote * 30.0f + 15.0f),
                                 innerProportion);
            g.setColour (colour.withAlpha (0.82f));
            g.fillPath (slice);
        }

        // ---- glass edges: the outer rim catches light at the top-left, the inner rim at the bottom-right ----
        g.setGradientFill (Theme::rim (circle (outerR)));
        g.drawEllipse (circle (outerR).reduced (0.6f), 1.4f);
        auto innerRim = Theme::rim (circle (innerR), 0.7f);
        std::swap (innerRim.point1, innerRim.point2);           // light from the other side on the inside edge
        g.setGradientFill (innerRim);
        g.drawEllipse (circle (innerR), 1.2f);

        // ---- ticks between semitones ----
        g.setColour (ink.withAlpha (0.16f));
        for (int i = 0; i < 12; ++i)
            g.drawLine (juce::Line<float> (pointAt (i * 30.0f + 15.0f, innerR), pointAt (i * 30.0f + 15.0f, outerR)), 1.0f);

        // ---- the needle: two pieces with a gap, so it never covers the note label ----
        if (hasPitch)
        {
            const float angle = note * 30.0f + cents * 0.3f;     // 30 degrees per semitone
            for (auto [from, to] : { std::pair { 94.0f, 114.0f }, std::pair { 142.0f, 162.0f } })
            {
                const juce::Line<float> piece (pointAt (angle, from), pointAt (angle, to));
                g.setColour (Theme::paper().withAlpha (0.55f));      // dark outline first
                g.drawLine (piece, 8.0f * k);
                g.setColour (ink);                                       // white line on top
                g.drawLine (piece, 3.5f * k);
            }
        }

        // ---- the 12 note labels ----
        // UTF-8 text: \xe2\x99\xaf is the sharp sign, \xe2\x99\xad the flat sign
        static const char* names[] = { "C", "C\xe2\x99\xaf", "D", "D\xe2\x99\xaf", "E", "F", "F\xe2\x99\xaf", "G", "G\xe2\x99\xaf", "A", "A\xe2\x99\xaf", "B" };
        static const char* flats[] = { "", "D\xe2\x99\xad", "", "E\xe2\x99\xad", "", "", "G\xe2\x99\xad", "", "A\xe2\x99\xad", "", "B\xe2\x99\xad", "" };

        for (int i = 0; i < 12; ++i)
        {
            const bool active = (hasPitch || manual) && i == sliceNote;
            const bool sharp  = flats[i][0] != 0;
            const auto p      = pointAt (i * 30.0f, labelR);

            g.setColour (ink.withAlpha (active ? 1.0f : 0.70f));
            g.setFont (font ((sharp ? 13.0f : 17.0f) * k, true));
            auto box = juce::Rectangle<float> (44.0f * k, 18.0f * k).withCentre (p.translated (0.0f, sharp ? -6.0f * k : 0.0f));
            g.drawText (juce::String::fromUTF8 (names[i]), box, juce::Justification::centred);

            if (sharp)
            {
                g.setColour (ink.withAlpha (active ? 0.85f : 0.50f));
                g.setFont (font (10.0f * k, false));
                g.drawText (juce::String::fromUTF8 (flats[i]), box.translated (0.0f, 13.0f * k), juce::Justification::centred);
            }
        }

        // ---- centre disc (glass) ----
        //      a glass lens: body, rim, and a soft oval highlight near the top, like light on a dome
        const auto disc = circle (90.0f);
        Theme::softShadow (g, disc.reduced (6.0f * k), disc.getWidth() * 0.5f);
        Theme::drawGlassCircle (g, disc, 0.08f);
        const auto highlight = disc.withSizeKeepingCentre (disc.getWidth() * 0.70f, disc.getHeight() * 0.38f)
                                   .withY (disc.getY() + disc.getHeight() * 0.05f);
        g.setGradientFill (juce::ColourGradient (juce::Colours::white.withAlpha (Theme::dark() ? 0.06f : 0.28f), highlight.getCentreX(), highlight.getY(),
                                                 juce::Colours::white.withAlpha (0.0f), highlight.getCentreX(), highlight.getBottom(), false));
        g.fillEllipse (highlight);

        if (! hasPitch && ! manual)
        {
            g.setColour (ink.withAlpha (0.55f));
            g.setFont (font (17.0f * k, false));
            g.drawText ("play a string", disc, juce::Justification::centred);
            return;
        }

        // what the centre shows: the detected note (automatic) or the chosen target (manual)
        const int   shownNote   = manual ? targetNote : note;
        const int   shownOctave = manual ? targetOctave : octave;
        const bool  showBar     = manual ? hasTargetCents : true;
        const float barCents    = manual ? targetCents : cents;
        const auto  barColour   = std::abs (barCents) <= 3.0f ? green : orange;

        // note name with the octave number next to it, e.g. "E" + small "2"
        const juce::String noteName = juce::String::fromUTF8 (names[shownNote]);
        g.setColour (ink);
        g.setFont (font (66.0f * k, true));
        const float nameWidth = juce::GlyphArrangement::getStringWidth (g.getCurrentFont(), noteName);
        auto nameBox = juce::Rectangle<float> (nameWidth, 70.0f * k).withCentre (c.translated (-8.0f * k, -20.0f * k));
        g.drawText (noteName, nameBox, juce::Justification::centred);

        g.setColour (ink.withAlpha (0.62f));
        g.setFont (font (22.0f * k, false));
        g.drawText (juce::String (shownOctave), nameBox.withX (nameBox.getRight() + 2.0f * k).withWidth (30.0f * k).withTrimmedTop (22.0f * k),
                    juce::Justification::centredLeft);

        // cents (automatic) or the target frequency (manual)
        const int roundedCents = juce::roundToInt (cents);
        const juce::String centsText = manual ? juce::String (targetHz, 2) + " Hz"
            : (roundedCents > 0 ? "+" : (roundedCents < 0 ? juce::String::fromUTF8 ("\xe2\x88\x92") : juce::String())) + juce::String (std::abs (roundedCents)) + " cents";
        g.setColour (Theme::readable (colour));
        g.setFont (font (15.0f * k, true));
        g.drawText (centsText, juce::Rectangle<float> (160.0f * k, 20.0f * k).withCentre (c.translated (0.0f, 26.0f * k)),
                    juce::Justification::centred);

        if (! showBar)
            return;

        // fine-tuning bar: -50 ... +50 cents across 104 px, green zone = +-3 cents
        const auto bar = juce::Rectangle<float> (104.0f * k, 3.0f * k).withCentre (c.translated (0.0f, 50.0f * k));
        g.setColour (ink.withAlpha (0.20f));
        g.fillRoundedRectangle (bar, 1.5f * k);
        g.setColour (green.withAlpha (0.85f));
        g.fillRoundedRectangle (juce::Rectangle<float> (6.0f * k, 11.0f * k).withCentre (bar.getCentre()), 2.0f * k);

        const auto dotCentre = juce::Point<float> (bar.getCentreX() + barCents * 1.04f * k, bar.getCentreY());
        const auto dot = juce::Rectangle<float> (10.0f * k, 10.0f * k).withCentre (dotCentre);
        g.setColour (ink);
        g.fillEllipse (dot.expanded (1.5f * k));
        g.setColour (manual ? barColour : colour);
        g.fillEllipse (dot);
    }

private:
    static juce::Font font (float height, bool bold)
    {
        return juce::Font (juce::FontOptions (height, bold ? juce::Font::bold : juce::Font::plain));
    }

    bool  hasPitch = false;
    bool  manual   = false;

    // manual mode
    int   targetNote = 4, targetOctave = 2;
    float targetHz = 82.41f;
    bool  hasTargetCents = false;
    float targetCents = 0.0f;
    int   note     = 4;      // 0 = C ... 11 = B
    int   octave   = 2;
    float cents    = 0.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (RingComponent)
};
