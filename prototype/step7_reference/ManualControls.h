/*
  ==============================================================================

    ManualControls.h
    Created: 23 Oct 2026
    Author:  Ali Fuat Sakacı

  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>
#include "Theme.h"
#include "HeaderComponent.h"     // for GlassButton

//==============================================================================
/*
    OCTAVE   (2) (3) (4) (5)
              .   .   .         <- dots: octaves that contain an open guitar string
*/
class OctavePicker  : public juce::Component
{
public:
    std::function<void (int octave)> onOctaveChanged;

    OctavePicker()
    {
        for (int i = 0; i < 4; ++i)
        {
            auto& b = buttons[(size_t) i];
            b = std::make_unique<GlassButton> (juce::String (firstOctave + i));
            b->setClickingTogglesState (true);
            b->setRadioGroupId (2);
            b->fontSize = 18.0f;
            b->onColour = juce::Colour (0xff0a84ff).withAlpha (0.75f);        // selected octave: blue
            b->onClick = [this, i]
            {
                if (buttons[(size_t) i]->getToggleState() && onOctaveChanged)
                    onOctaveChanged (firstOctave + i);
            };
            addAndMakeVisible (*b);
        }
        setOctave (2);
    }

    // show this octave as selected, without calling onOctaveChanged
    void setOctave (int octave)
    {
        const int i = juce::jlimit (0, 3, octave - firstOctave);
        buttons[(size_t) i]->setToggleState (true, juce::dontSendNotification);
    }

    void paint (juce::Graphics& g) override
    {
        g.setColour (Theme::ink().withAlpha (0.62f));
        g.setFont (juce::FontOptions (12.0f, juce::Font::bold));
        g.drawText ("OCTAVE", getLocalBounds().removeFromTop (16), juce::Justification::centred);

        // a small dot under the octaves 2, 3 and 4: they contain open strings (E2 ... E4)
        for (int i = 0; i < 3; ++i)
        {
            const auto b = buttons[(size_t) i]->getBounds().toFloat();
            g.fillEllipse (juce::Rectangle<float> (5.0f, 5.0f).withCentre ({ b.getCentreX(), b.getBottom() + 7.0f }));
        }
    }

    void resized() override
    {
        const int size = 46, gap = 12;
        int x = (getWidth() - (4 * size + 3 * gap)) / 2;
        for (auto& b : buttons)
        {
            b->setBounds (x, 20, size, size);
            x += size + gap;
        }
    }

private:
    static constexpr int firstOctave = 2;
    std::array<std::unique_ptr<GlassButton>, 4> buttons;
};

//==============================================================================
/*  An iOS-style on/off switch with a label on its left. */
class SwitchButton  : public juce::ToggleButton
{
public:
    using juce::ToggleButton::ToggleButton;

    void paintButton (juce::Graphics& g, bool, bool) override
    {
        auto r = getLocalBounds().toFloat();
        const bool on = getToggleState();

        // glass capsule behind label + switch
        Theme::drawGlass (g, r.reduced (0.5f), r.getHeight() * 0.5f);

        auto track = r.removeFromRight (64.0f).withSizeKeepingCentre (52.0f, 28.0f);
        g.setColour (on ? juce::Colour (0xff30d158) : Theme::ink().withAlpha (0.25f));
        g.fillRoundedRectangle (track, 14.0f);

        auto knob = juce::Rectangle<float> (24.0f, 24.0f).withCentre ({ on ? track.getRight() - 14.0f : track.getX() + 14.0f,
                                                                         track.getCentreY() });
        Theme::softShadow (g, knob.reduced (2.0f), 10.0f);
        g.setColour (juce::Colours::white);                      // the knob is white in both modes
        g.fillEllipse (knob);

        g.setColour (Theme::ink());

        g.setFont (juce::FontOptions (15.0f, juce::Font::bold));
        g.drawText (getButtonText(), r.withTrimmedLeft (18.0f), juce::Justification::centredLeft);
    }
};

//==============================================================================
/*
    [ >  Play E2 · 82.41 Hz ]   [ Keep listening  (o) ]
*/
class PlayBar  : public juce::Component
{
public:
    std::function<void()> onPlayClicked;
    std::function<void (bool)> onKeepListeningChanged;

    PlayBar()
    {
        playButton.onClick = [this] { if (onPlayClicked) onPlayClicked(); };
        addAndMakeVisible (playButton);

        listenSwitch.setToggleState (true, juce::dontSendNotification);
        listenSwitch.onClick = [this] { if (onKeepListeningChanged) onKeepListeningChanged (listenSwitch.getToggleState()); };
        addAndMakeVisible (listenSwitch);
    }

    void setTarget (const juce::String& name, float hz)
    {
        targetName = name;
        targetText = name + juce::String::fromUTF8 (" \xc2\xb7 ") + juce::String (hz, 2) + " Hz";
        updateText();
    }

    void setPlaying (bool isPlaying)
    {
        if (playing == isPlaying)
            return;
        playing = isPlaying;
        updateText();
    }

    void resized() override
    {
        // on a narrow screen (iPhone) both need to be shorter: "Play E2" and "Listen"
        compact = getWidth() < 440;
        listenSwitch.setButtonText (compact ? "Listen" : "Keep listening");

        auto r = getLocalBounds();
        listenSwitch.setBounds (r.removeFromRight (compact ? 136 : 196));
        r.removeFromRight (12);
        playButton.setBounds (r);
        updateText();
    }

private:
    // the blue Play button
    struct PlayButton  : public juce::Button
    {
        PlayButton() : juce::Button ("Play") {}

        void paintButton (juce::Graphics& g, bool isMouseOver, bool isMouseDown) override
        {
            auto r = getLocalBounds().toFloat().reduced (0.5f);
            // blue tinted glass
            Theme::drawGlass (g, r, r.getHeight() * 0.5f, 0.0f,
                              juce::Colour (0xff0a84ff).withAlpha (isMouseDown ? 1.0f : (isMouseOver ? 0.95f : 0.85f)));

            g.setColour (juce::Colours::white);
            g.setFont (juce::FontOptions (16.0f, juce::Font::bold));
            g.drawText (getButtonText(), getLocalBounds(), juce::Justification::centred);
        }
    };

    void updateText()
    {
        const juce::String icon = juce::String::fromUTF8 (playing ? "\xe2\x96\xa0  " : "\xe2\x96\xb6  ");   // stop / play symbols
        playButton.setButtonText (icon + (playing ? "Stop" : "Play " + (compact ? targetName : targetText)));
    }

    PlayButton playButton;
    SwitchButton listenSwitch { "Keep listening" };
    juce::String targetName { "E2" };
    bool compact = false;
    juce::String targetText { "E2" };
    bool playing = false;
};
