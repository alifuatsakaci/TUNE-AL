/*
  ==============================================================================

    HeaderComponent.h
    Created: 23 Oct 2026 1:12:27pm
    Author:  Ali Fuat Sakacı

  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>
#include "Theme.h"

//==============================================================================
/*
    A "liquid glass" button: translucent white fill, thin light border and a
    highlight on the top edge. When it is "on" (toggled) the fill is brighter.
*/
class GlassButton  : public juce::Button
{
public:
    explicit GlassButton (const juce::String& text) : juce::Button (text) {}

    bool drawBackground = true;     // false for buttons sitting inside another glass shape
    float fontSize = 15.0f;
    juce::Colour onColour;          // fill when toggled on (transparent = the theme's default)

    void paintButton (juce::Graphics& g, bool isMouseOver, bool isMouseDown) override
    {
        auto r = getLocalBounds().toFloat().reduced (0.5f);
        const float radius = r.getHeight() * 0.5f;                  // fully rounded: a capsule
        const bool on = getToggleState();

        if (drawBackground || on)
        {
            float extra = 0.0f;
            if (isMouseOver) extra += 0.04f;
            if (isMouseDown) extra += 0.06f;

            // a liquid glass capsule (see Theme::drawGlass). A selected segment is a brighter
            // glass "puck" on top of the capsule, like the iOS segmented control.
            const auto onFill = onColour.isTransparent() ? Theme::glass (0.22f) : onColour;
            if (on)
                Theme::drawGlass (g, r.reduced (0.5f), radius, 0.0f, onFill.withAlpha (juce::jmin (1.0f, onFill.getFloatAlpha() + extra)),
                                  ! drawBackground);
            else
                Theme::drawGlass (g, r, radius, 0.10f + extra);
        }

        g.setColour (Theme::ink().withAlpha (on || drawBackground ? 1.0f : 0.72f));
        g.setFont (juce::FontOptions (fontSize, on || drawBackground ? juce::Font::bold : juce::Font::plain));
        g.drawText (getButtonText(), getLocalBounds(), juce::Justification::centred);
    }
};

//==============================================================================
/*
    The top bar: the [ Auto | Manual ] switch. (A4 and "?" live in the sidebar,
    on the Mac as a docked panel and on the iPhone as a slide-over drawer.)

    It does not decide anything itself. When something is clicked it calls the
    std::function "callback" below, and MainComponent decides what to do.
*/
class HeaderComponent  : public juce::Component
{
public:
    std::function<void (bool manual)> onModeChanged;   // called with true for Manual, false for Auto

    HeaderComponent()
    {
        // Auto and Manual form a "radio group": turning one on turns the other off
        for (auto* b : { &autoButton, &manualButton })
        {
            b->setClickingTogglesState (true);
            b->setRadioGroupId (1);
            b->drawBackground = false;
            addAndMakeVisible (*b);
        }
        autoButton.setToggleState (true, juce::dontSendNotification);

        autoButton.onClick   = [this] { if (autoButton.getToggleState()   && onModeChanged) onModeChanged (false); };
        manualButton.onClick = [this] { if (manualButton.getToggleState() && onModeChanged) onModeChanged (true); };
    }

    // show Auto or Manual as selected, without calling onModeChanged
    void setManual (bool manual)
    {
        (manual ? manualButton : autoButton).setToggleState (true, juce::dontSendNotification);
    }

    void paint (juce::Graphics& g) override
    {
        // the glass capsule behind the Auto | Manual pair
        auto r = segmentArea.toFloat().reduced (0.5f);
        Theme::drawGlass (g, r, r.getHeight() * 0.5f, 0.08f);
    }

    void resized() override
    {
        const int h = getHeight();                           // expected height: 44 px
        segmentArea = juce::Rectangle<int> (juce::jmin (170, getWidth()), h).withCentre (getLocalBounds().getCentre());
        auto inner = segmentArea.reduced (4);
        autoButton.setBounds (inner.removeFromLeft (inner.getWidth() / 2));
        manualButton.setBounds (inner);
    }

private:
    GlassButton autoButton   { "Auto" };
    GlassButton manualButton { "Manual" };
    juce::Rectangle<int> segmentArea;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (HeaderComponent)
};
