/*
  ==============================================================================

    Theme.h
    Created: 26 Oct 2026
    Author:  Ali Fuat Sakacı

  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>

#if JUCE_MAC
 #include <objc/runtime.h>      // lets plain C++ ask macOS (an Objective-C system) a question
 #include <objc/message.h>
#elif JUCE_IOS
 extern "C" bool UIAccessibilityIsReduceMotionEnabled();         // plain C functions in UIKit
 extern "C" bool UIAccessibilityIsReduceTransparencyEnabled();
#endif

//==============================================================================
/*
    Light and dark mode.

    Every component asks Theme for its colours instead of hard-coding white or black:
      ink()    text, lines and glass tints   (white in dark mode, near-black in light mode)
      paper()  the window background         (black in dark mode, Apple's light grey in light mode)
      glass()  the fill of glass panels and buttons

    The state lives in two "function-local statics": a static variable inside a function
    is created once and shared by everyone who calls the function.
*/
struct Theme
{
    enum class Mode { system, light, dark };

    static Mode& mode()  { static Mode m = Mode::dark; return m; }     // what the user chose (dark by default)
    static bool& dark()  { static bool d = true;       return d; }     // what is shown right now

    //==============================================================================
    // Remembering the choice: a small settings file on disk (in "Application Support" on the Mac,
    // inside the app's own folder on the iPhone). PropertiesFile stores simple key = value pairs.
    static juce::PropertiesFile& settings()
    {
        static juce::PropertiesFile file ([]
        {
            juce::PropertiesFile::Options options;
            options.applicationName     = "GuitarTuner";
            options.folderName          = "GuitarTuner";
            options.filenameSuffix      = ".settings";
            options.osxLibrarySubFolder = "Application Support";
            return options;
        }());
        return file;
    }

    static void loadSettings()
    {
        const auto saved = settings().getValue ("appearance", "dark");
        mode() = saved == "light" ? Mode::light : (saved == "system" ? Mode::system : Mode::dark);
    }

    static void saveSettings()
    {
        settings().setValue ("appearance", mode() == Mode::light ? "light" : (mode() == Mode::system ? "system" : "dark"));
        settings().saveIfNeeded();
    }

    // work out light or dark, and give pop-up menus and alert windows the same look
    static void update()
    {
        dark() = mode() == Mode::system ? juce::Desktop::getInstance().isDarkModeActive()
                                        : mode() == Mode::dark;

        if (auto* lf = dynamic_cast<juce::LookAndFeel_V4*> (&juce::LookAndFeel::getDefaultLookAndFeel()))
            lf->setColourScheme (dark() ? juce::LookAndFeel_V4::getDarkColourScheme()
                                        : juce::LookAndFeel_V4::getLightColourScheme());
    }

    //==============================================================================
    // Accessibility: System Settings > Accessibility > Display on the Mac
    // (Motion / Display & Text Size on the iPhone).
    //   Reduce Motion        -> no sliding or bouncing, things fade instead; the background stops drifting
    //   Reduce Transparency  -> no see-through glass: a plain background and solid panels
    static bool& reduceMotion()        { static bool b = false; return b; }
    static bool& reduceTransparency()  { static bool b = false; return b; }

    // read the two settings from the system; returns true if one of them changed
    static bool readAccessibilitySettings()
    {
        bool motion = false, transparency = false;
       #if JUCE_MAC
        // [[NSWorkspace sharedWorkspace] accessibilityDisplayShouldReduceMotion], written in C++
        using GetObject = id (*) (id, SEL);
        using GetFlag   = signed char (*) (id, SEL);       // Objective-C's BOOL
        if (auto workspace = ((GetObject) objc_msgSend) ((id) objc_getClass ("NSWorkspace"), sel_registerName ("sharedWorkspace")))
        {
            motion       = ((GetFlag) objc_msgSend) (workspace, sel_registerName ("accessibilityDisplayShouldReduceMotion")) != 0;
            transparency = ((GetFlag) objc_msgSend) (workspace, sel_registerName ("accessibilityDisplayShouldReduceTransparency")) != 0;
        }
       #elif JUCE_IOS
        motion       = UIAccessibilityIsReduceMotionEnabled();
        transparency = UIAccessibilityIsReduceTransparencyEnabled();
       #endif
        const bool changed = motion != reduceMotion() || transparency != reduceTransparency();
        reduceMotion() = motion;
        reduceTransparency() = transparency;
        return changed;
    }

    static juce::Colour ink()    { return dark() ? juce::Colours::white : juce::Colour (0xff1d1d1f); }
    static juce::Colour paper()  { return dark() ? juce::Colours::black : juce::Colour (0xfff2f2f7); }
    static juce::Colour sheet()  { return dark() ? juce::Colour (0xff2a2a2e) : juce::Colour (0xfffbfbfd); }   // slideshow

    // glass fill: a faint white tint on black, or a milky white layer on light grey
    static juce::Colour glass (float darkAlpha)
    {
        if (reduceTransparency())     // frosted, almost solid
            return dark() ? juce::Colours::white.withAlpha (juce::jmin (1.0f, 0.06f + darkAlpha * 1.6f))
                          : juce::Colours::white.withAlpha (juce::jmin (0.97f, 0.65f + darkAlpha * 3.0f));

        return dark() ? juce::Colours::white.withAlpha (darkAlpha)
                      : juce::Colours::white.withAlpha (juce::jmin (0.85f, 0.35f + darkAlpha * 4.0f));
    }

    //==============================================================================
    // Liquid glass. Glass looks like glass because of what light does to it:
    //   1) a translucent BODY, so the colourful background shows through
    //   2) a SHEEN: light falling on the top half
    //   3) a RIM: the edge is bright where the light hits (top-left), faint in the middle,
    //      with a small glint again at the bottom-right
    //   4) a soft SHADOW that lifts it off the background
    // strength scales the sheen and rim (1 = buttons and panels, less for flat cards).

    static void drawGlass (juce::Graphics& g, juce::Rectangle<float> r, float radius,
                           float fill = 0.10f, juce::Colour tint = {}, bool shadow = false, float strength = 1.0f)
    {
        radius = juce::jmin (radius, r.getHeight() * 0.5f, r.getWidth() * 0.5f);

        if (shadow)
            softShadow (g, r, radius);

        g.setColour (tint.isTransparent() ? glass (fill) : tint);                          // 1) body
        g.fillRoundedRectangle (r, radius);

        g.setGradientFill (sheen (r, strength));                                            // 2) sheen
        g.fillRoundedRectangle (r, radius);

        g.setGradientFill (rim (r, strength));                                              // 3) rim
        g.drawRoundedRectangle (r.reduced (0.6f), radius, 1.2f);
    }

    // the same for a circle
    static void drawGlassCircle (juce::Graphics& g, juce::Rectangle<float> r,
                                 float fill = 0.10f, juce::Colour tint = {}, bool shadow = false, float strength = 1.0f)
    {
        drawGlass (g, r, r.getWidth() * 0.5f, fill, tint, shadow, strength);
    }

    static juce::ColourGradient sheen (juce::Rectangle<float> r, float strength = 1.0f)
    {
        const auto white = juce::Colours::white;
        return { white.withAlpha ((dark() ? 0.13f : 0.45f) * strength), r.getX(), r.getY(),
                 white.withAlpha (0.0f), r.getX(), r.getY() + r.getHeight() * 0.55f, false };
    }

    static juce::ColourGradient rim (juce::Rectangle<float> r, float strength = 1.0f)
    {
        const auto white = juce::Colours::white;
        juce::ColourGradient grad (white.withAlpha ((dark() ? 0.60f : 1.0f) * strength), r.getX(), r.getY(),
                                   white.withAlpha ((dark() ? 0.22f : 0.75f) * strength), r.getRight(), r.getBottom(), false);
        grad.addColour (0.5, dark() ? white.withAlpha (0.07f * strength) : ink().withAlpha (0.10f * strength));
        return grad;
    }

    // a cheap soft shadow: a few slightly bigger, lower, very faint copies of the shape
    static void softShadow (juce::Graphics& g, juce::Rectangle<float> r, float radius)
    {
        const float base = dark() ? 0.20f : 0.05f;
        for (int i = 5; i >= 1; --i)
        {
            const float grow = 2.0f * (float) i;
            g.setColour (juce::Colours::black.withAlpha (base / (float) i));
            g.fillRoundedRectangle (r.translated (0.0f, grow * 0.6f).expanded (grow), radius + grow);
        }
    }

    // coloured TEXT (orange, green, blue) needs to be darker on a light background to stay readable;
    // neutral colours (ink, greys) have low saturation and are returned unchanged
    static juce::Colour readable (juce::Colour c)
    {
        return (dark() || c.getSaturation() < 0.3f) ? c : c.darker (0.55f);
    }

    static float glowAlpha()     { return dark() ? 0.26f : 0.20f; }   // the coloured glow behind the ring
};
