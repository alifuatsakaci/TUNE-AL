/*
  ==============================================================================

    StringChipsComponent.h
    Created: 23 Oct 2026
    Author:  Ali Fuat Sakacı

  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>
#include "Theme.h"

//==============================================================================
/*
    One round glass button for a guitar string: the note name, with the octave
    number underneath. When it is the string you are tuning it gets a tinted
    fill and a coloured ring.
*/
class StringChip  : public juce::Button
{
public:
    StringChip (const juce::String& noteName, int octaveNumber)
        : juce::Button (noteName), octave (octaveNumber) {}

    void setHighlight (bool shouldHighlight, juce::Colour newColour)
    {
        if (highlighted == shouldHighlight && colour == newColour)
            return;                                   // nothing changed: skip the repaint
        highlighted = shouldHighlight;
        colour = newColour;
        repaint();
    }

    void paintButton (juce::Graphics& g, bool isMouseOver, bool isMouseDown) override
    {
        auto r = getLocalBounds().toFloat().reduced (1.0f);
        const auto ink = Theme::ink();

        if (highlighted)
        {
            Theme::drawGlassCircle (g, r, 0.0f, colour.withAlpha (0.32f));   // glass tinted with the state colour
            g.setColour (colour);
            g.drawEllipse (r, 1.5f);
        }
        else
        {
            Theme::drawGlassCircle (g, r, isMouseDown ? 0.18f : (isMouseOver ? 0.13f : 0.08f));
        }

        // note name on top, octave number below it
        auto text = r.withSizeKeepingCentre (r.getWidth(), 32.0f);
        g.setColour (ink);
        g.setFont (juce::FontOptions (18.0f, juce::Font::bold));
        g.drawText (getButtonText(), text.removeFromTop (20.0f), juce::Justification::centred);
        g.setColour (ink.withAlpha (0.62f));
        g.setFont (juce::FontOptions (11.0f));
        g.drawText (juce::String (octave), text, juce::Justification::centred);
    }

    const int octave;

private:
    bool highlighted = false;
    juce::Colour colour;
};

//==============================================================================
/*
    The row of six strings in standard tuning: E2 A2 D3 G3 B3 E4.
    It highlights the string you are playing, and reports clicks through
    onStringClicked (used for manual mode in Step 7d).
*/
class StringChipsComponent  : public juce::Component
{
public:
    // note numbers: 0 = C, 1 = C#, ... 11 = B
    struct GuitarString { const char* name; int note; int octave; };
    static constexpr GuitarString standardTuning[6] = {
        { "E", 4, 2 }, { "A", 9, 2 }, { "D", 2, 3 }, { "G", 7, 3 }, { "B", 11, 3 }, { "E", 4, 4 }
    };

    std::function<void (int stringIndex)> onStringClicked;

    StringChipsComponent()
    {
        for (int i = 0; i < 6; ++i)
        {
            // std::make_unique creates the object and hands ownership to the unique_ptr:
            // when this component is destroyed, the chips are deleted automatically
            chips[(size_t) i] = std::make_unique<StringChip> (standardTuning[i].name, standardTuning[i].octave);
            chips[(size_t) i]->onClick = [this, i] { if (onStringClicked) onStringClicked (i); };
            addAndMakeVisible (*chips[(size_t) i]);
        }
    }

    // Highlight the string with this note and octave (pass note = -1 for none)
    void setActive (int note, int octave, juce::Colour colour)
    {
        for (int i = 0; i < 6; ++i)
        {
            const bool match = standardTuning[i].note == note && standardTuning[i].octave == octave;
            chips[(size_t) i]->setHighlight (match, colour);
        }
    }

    void resized() override
    {
        const int size = 48, gap = 10;
        const int total = 6 * size + 5 * gap;
        int x = (getWidth() - total) / 2;

        for (auto& chip : chips)
        {
            chip->setBounds (x, (getHeight() - size) / 2, size, size);
            x += size + gap;
        }
    }

private:
    std::array<std::unique_ptr<StringChip>, 6> chips;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (StringChipsComponent)
};
