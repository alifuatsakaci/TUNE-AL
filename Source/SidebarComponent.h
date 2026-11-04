/*
  ==============================================================================

    SidebarComponent.h
    Created: 26 Oct 2026
    Author:  Ali Fuat Sakacı

  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>
#include "Theme.h"
#include "HeaderComponent.h"          // GlassButton
#include "StringChipsComponent.h"     // the standard tuning table

//==============================================================================
/*
    The glass sidebar of the Mac layout (from the Figma design):

        (top bar)          room for the sidebar button, top right
        INSTRUMENT [SOON]  Guitar, 6 strings        (bass, ukulele... coming soon)   *
        STRINGS            six rows: chip, name, frequency, live status
        TUNING             Standard  E A D G B E
        SONG TUNING  [AI]  [ search a song... ]      (coming soon)                    *
        REFERENCE PITCH    A4 = 440 Hz   [-] [+]
        [ ? Music basics ]

    * only shown when showComingSoon is true (see below)

    Like the header, it only reports clicks through callbacks.
*/
class SidebarComponent  : public juce::Component
{
public:
    // The two sections that only announce future features (more instruments, AI song tuning).
    // Off for the App Store: Apple rejects apps with "coming soon" placeholders (guideline 2.1).
    // Turn it back on to keep working on them.
    static constexpr bool showComingSoon = false;

    std::function<void (int stringIndex)> onStringClicked;
    std::function<void (float hz)> onReferenceChanged;
    std::function<void()> onHelpClicked;
    std::function<void()> onAppearanceClicked;     // cycles System -> Light -> Dark

    // called with the text when Return is pressed in the song search box. Not connected yet:
    // later an AI service will look up the song and answer with its tuning (e.g. "Drop D").
    std::function<void (const juce::String& song)> onSongSearch;

    SidebarComponent()
    {
        for (int i = 0; i < 6; ++i)
        {
            rows[(size_t) i] = std::make_unique<StringRow> (i);
            rows[(size_t) i]->onClick = [this, i] { if (onStringClicked) onStringClicked (i); };
            addAndMakeVisible (*rows[(size_t) i]);
        }

        minusButton.onClick = [this] { changeReference (-1.0f); };
        plusButton.onClick  = [this] { changeReference (+1.0f); };
        helpButton.onClick  = [this] { if (onHelpClicked) onHelpClicked(); };
        for (auto* b : { &minusButton, &plusButton })
            b->fontSize = 17.0f;

        // the song search box: a TextEditor styled as glass, with a hint text when empty
        searchBox.setFont (juce::FontOptions (14.0f));
        searchBox.setTextToShowWhenEmpty ("Search a song...", Theme::ink().withAlpha (0.4f));   // set again in themeChanged()
        searchBox.setIndents (34, 0);                              // room for the magnifier icon
        searchBox.setJustification (juce::Justification::centredLeft);
        searchBox.setColour (juce::TextEditor::backgroundColourId, juce::Colours::transparentBlack);
        searchBox.setColour (juce::TextEditor::outlineColourId, juce::Colours::transparentBlack);
        searchBox.setColour (juce::TextEditor::focusedOutlineColourId, juce::Colour (0xff0a84ff).withAlpha (0.8f));
        searchBox.setColour (juce::TextEditor::textColourId, Theme::ink());
        searchBox.setColour (juce::CaretComponent::caretColourId, juce::Colour (0xff0a84ff));
        searchBox.onReturnKey = [this]
        {
            const auto song = searchBox.getText().trim();
            if (song.isEmpty())
                return;
            searchMessage = "AI song tuning is coming soon";
            repaint();
            if (onSongSearch)
                onSongSearch (song);
        };
        searchBox.onTextChange = [this] { searchMessage.clear(); repaint(); };
        addAndMakeVisible (searchBox);

        appearanceButton.onClick = [this] { if (onAppearanceClicked) onAppearanceClicked(); };
        addAndMakeVisible (appearanceButton);

        addAndMakeVisible (minusButton);
        addAndMakeVisible (plusButton);
        addAndMakeVisible (helpButton);
    }

    static constexpr int topBarHeight = 34;

    // true when the sidebar slides over the tuner (iPhone drawer), false when docked (Mac)
    void setSolid (bool shouldBeSolid)
    {
        if (solid == shouldBeSolid)
            return;
        solid = shouldBeSolid;
        repaint();
    }     // empty strip at the top: MainComponent puts the toggle there

    // highlight one string row (-1 = none) and show a short status on it, e.g. "-18¢"
    void setActive (int stringIndex, juce::Colour colour, const juce::String& status)
    {
        for (int i = 0; i < 6; ++i)
            rows[(size_t) i]->setState (i == stringIndex, colour, i == stringIndex ? status : juce::String());
    }

    // light/dark changed: most things just repaint, but the text box keeps its own colours
    void themeChanged()
    {
        searchBox.setColour (juce::TextEditor::textColourId, Theme::ink());
        searchBox.applyColourToAllText (Theme::ink());
        searchBox.setTextToShowWhenEmpty ("Search a song...", Theme::ink().withAlpha (0.4f));
        appearanceButton.repaint();
        repaint();
    }

    void setReference (float hz)
    {
        reference = hz;
        for (auto& r : rows)
            r->setReference (hz);
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        const auto ink = Theme::ink();
        auto r = getLocalBounds().toFloat().reduced (0.5f);

        // the glass panel (its shadow is drawn by MainComponent, outside the sidebar's own bounds).
        // As a drawer it lies on top of the tuner, so it uses a heavy, almost opaque "frosted" glass:
        // without a real blur, see-through glass would show the ring and text underneath.
        if (solid)
            Theme::drawGlass (g, r, 20.0f, 0.0f, Theme::dark() ? juce::Colour (0xf21c1c22) : juce::Colour (0xf5f8f8fa), false);
        else
            Theme::drawGlass (g, r, 20.0f, 0.06f);

        // instrument: only the 6-string guitar for now
        if (! instrumentRow.isEmpty())
        {
            caption (g, "INSTRUMENT", instrumentCaption);
            badge (g, "SOON", instrumentCaption.getX() + 88.0f, instrumentCaption.getCentreY(), juce::Colour (0xff64d2ff));
            card (g, instrumentRow);
            auto ir = instrumentRow.reduced (14.0f, 5.0f);
            text (g, "Guitar" + juce::String::fromUTF8 (" \xc2\xb7 ") + "6 strings", ir.removeFromTop (ir.getHeight() * 0.55f), 14.0f, true, ink);
            const bool showSoon = juce::Time::getMillisecondCounter() < instrumentMessageUntil;
            text (g, showSoon ? "Bass, 7-string, ukulele: coming soon" : "More instruments coming soon", ir, 12.0f, false,
                  showSoon ? juce::Colour (0xff64d2ff) : ink.withAlpha (0.6f));
        }

        caption (g, "STRINGS", stringsCaption);
        caption (g, "TUNING", tuningCaption);
        caption (g, "REFERENCE PITCH", referenceCaption);

        // song tuning: caption with an "AI" badge, the search card, and a magnifier icon
        if (! songCaption.isEmpty())
        {
            caption (g, "SONG TUNING", songCaption);
            badge (g, "AI", songCaption.getX() + 96.0f, songCaption.getCentreY(), juce::Colour (0xffda8fff));   // purple: "smart" features

            card (g, songRow);
            const auto icon = juce::Rectangle<float> (12.0f, 12.0f).withPosition (songRow.getX() + 14.0f, songRow.getCentreY() - 8.0f);
            g.setColour (ink.withAlpha (0.55f));
            g.drawEllipse (icon, 1.6f);
            g.drawLine (icon.getRight() - 1.5f, icon.getBottom() - 1.5f, icon.getRight() + 3.5f, icon.getBottom() + 3.5f, 1.8f);

            if (searchMessage.isNotEmpty())
                text (g, searchMessage, songMessage.withTrimmedLeft (6.0f), 12.0f, false, juce::Colour (0xffda8fff));
        }

        // tuning row (standard tuning only, for now)
        card (g, tuningRow);
        auto t = tuningRow.reduced (14.0f, 6.0f);
        text (g, "Standard", t.removeFromTop (t.getHeight() * 0.55f), 14.0f, true, ink);
        text (g, "E  A  D  G  B  E", t, 12.0f, false, ink.withAlpha (0.6f));

        // reference row: the text on the left, the - and + buttons sit on the right
        card (g, referenceRow);
        auto rr = referenceRow.reduced (14.0f, 6.0f).withTrimmedRight (76.0f);
        text (g, "A4 = " + juce::String (juce::roundToInt (reference)) + " Hz", rr.removeFromTop (rr.getHeight() * 0.55f), 14.0f, true, ink);
        text (g, juce::roundToInt (reference) == 440 ? "International standard" : "Standard is 440 Hz", rr, 12.0f, false, ink.withAlpha (0.6f));
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (14).toFloat();
        r.removeFromTop ((float) topBarHeight);                // the sidebar button sits up here

        instrumentCaption = instrumentRow = {};
        if (showComingSoon)
        {
            instrumentCaption = r.removeFromTop (22.0f);
            instrumentRow = r.removeFromTop (44.0f);
        }

        // on short screens (iPhone SE, a small window) the six string rows get a little thinner,
        // so the reference pitch and the buttons at the bottom never overlap
        const float fixedParts = 32.0f + 76.0f + 78.0f + 8.0f + 44.0f;        // strings caption, tuning, reference, gap, buttons
        const float rowHeight  = juce::jlimit (36.0f, 46.0f, (r.getHeight() - 10.0f - fixedParts) / 6.0f - 2.0f);

        r.removeFromTop (10.0f);
        stringsCaption = r.removeFromTop (22.0f);
        for (auto& row : rows)
        {
            row->setBounds (r.removeFromTop (rowHeight).toNearestInt());
            r.removeFromTop (2.0f);
        }

        r.removeFromTop (10.0f);
        tuningCaption = r.removeFromTop (22.0f);
        tuningRow = r.removeFromTop (44.0f);

        // the song search only appears if there is room for it (it is the least important part)
        const bool roomForSong = showComingSoon
                                  && r.getHeight() > 10.0f + 22.0f + 40.0f + 16.0f + 10.0f + 22.0f + 46.0f + 10.0f + 44.0f;
        searchBox.setVisible (roomForSong);
        songCaption = songRow = songMessage = {};
        if (roomForSong)
        {
            r.removeFromTop (10.0f);
            songCaption = r.removeFromTop (22.0f);
            songRow = r.removeFromTop (40.0f);
            searchBox.setBounds (songRow.reduced (4.0f, 4.0f).toNearestInt());
            songMessage = r.removeFromTop (16.0f);
        }

        r.removeFromTop (10.0f);
        referenceCaption = r.removeFromTop (22.0f);
        referenceRow = r.removeFromTop (46.0f);
        auto buttons = referenceRow.reduced (10.0f).removeFromRight (70.0f);
        plusButton.setBounds (buttons.removeFromRight (32.0f).toNearestInt());
        buttons.removeFromRight (6.0f);
        minusButton.setBounds (buttons.removeFromRight (32.0f).toNearestInt());

        auto bottomRow = r.removeFromBottom (44.0f);
        appearanceButton.setBounds (bottomRow.removeFromRight (44.0f).toNearestInt());
        bottomRow.removeFromRight (8.0f);
        helpButton.setBounds (bottomRow.toNearestInt());
    }

    // a click on the instrument card: tell the user more instruments are on the way
    void mouseDown (const juce::MouseEvent& e) override
    {
        if (! instrumentRow.contains (e.position))
            return;

        instrumentMessageUntil = juce::Time::getMillisecondCounter() + 3000;   // show it for 3 s
        repaint();
        juce::Timer::callAfterDelay (3100, [safe = juce::Component::SafePointer<SidebarComponent> (this)]
        {
            if (safe != nullptr)
                safe->repaint();
        });
    }

private:
    //==============================================================================
    // one string row: a round chip, the name and frequency, and a status on the right
    class StringRow  : public juce::Button
    {
    public:
        explicit StringRow (int stringIndex) : juce::Button ("string"), index (stringIndex) {}

        void setState (bool shouldBeActive, juce::Colour newColour, const juce::String& newStatus)
        {
            if (active == shouldBeActive && colour == newColour && status == newStatus)
                return;
            active = shouldBeActive;
            colour = newColour;
            status = newStatus;
            repaint();
        }

        void setReference (float hz) { reference = hz; repaint(); }

        void paintButton (juce::Graphics& g, bool over, bool down) override
        {
            const auto ink = Theme::ink();
            const auto& s = StringChipsComponent::standardTuning[index];
            auto r = getLocalBounds().toFloat().reduced (0.5f);

            if (active)
            {
                Theme::drawGlass (g, r, 12.0f, 0.0f, colour.withAlpha (0.22f), false, 0.8f);
                g.setColour (colour.withAlpha (0.55f));
                g.drawRoundedRectangle (r, 12.0f, 1.0f);
            }
            else if (over || down)
            {
                g.setColour (ink.withAlpha (down ? 0.10f : 0.06f));
                g.fillRoundedRectangle (r, 12.0f);
            }

            auto content = r.reduced (8.0f, 0.0f);
            auto chipArea = content.removeFromLeft (36.0f).withSizeKeepingCentre (36.0f, 36.0f);
            g.setColour (active ? colour.withAlpha (0.35f) : ink.withAlpha (0.08f));
            g.fillEllipse (chipArea);
            g.setColour (active ? colour : ink.withAlpha (0.18f));
            g.drawEllipse (chipArea, 1.0f);
            g.setColour (ink);
            g.setFont (juce::FontOptions (13.0f, juce::Font::bold));
            g.drawText (juce::String (s.name) + juce::String (s.octave), chipArea, juce::Justification::centred);

            content.removeFromLeft (12.0f);
            if (status.isNotEmpty())
            {
                g.setColour (Theme::readable (colour));
                g.setFont (juce::FontOptions (13.0f, juce::Font::bold));
                g.drawText (status, content.removeFromRight (64.0f), juce::Justification::centredRight);
            }

            static const char* names[] = { "Low E string", "A string", "D string", "G string", "B string", "High E string" };
            const int midi = 12 * (s.octave + 1) + s.note;
            const float hz = reference * std::pow (2.0f, (float) (midi - 69) / 12.0f);

            g.setColour (ink);
            g.setFont (juce::FontOptions (14.0f, juce::Font::bold));
            g.drawText (names[index], content.removeFromTop (content.getHeight() * 0.55f), juce::Justification::bottomLeft);
            g.setColour (ink.withAlpha (0.6f));
            g.setFont (juce::FontOptions (12.0f));
            g.drawText (juce::String (hz, 2) + " Hz", content, juce::Justification::topLeft);
        }

    private:
        const int index;
        bool active = false;
        juce::Colour colour;
        juce::String status;
        float reference = 440.0f;
    };

    //==============================================================================
    void changeReference (float delta)
    {
        const float hz = juce::jlimit (415.0f, 466.0f, reference + delta);   // about a semitone either way
        setReference (hz);
        if (onReferenceChanged)
            onReferenceChanged (hz);
    }

    static void caption (juce::Graphics& g, const juce::String& s, juce::Rectangle<float> r)
    {
        text (g, s, r.withTrimmedLeft (6.0f), 11.0f, true, Theme::ink().withAlpha (0.5f));
    }

    // a small rounded label like "AI" or "SOON" next to a caption
    static void badge (juce::Graphics& g, const juce::String& label, float x, float centreY, juce::Colour colour)
    {
        auto r = juce::Rectangle<float> (label.length() > 2 ? 38.0f : 26.0f, 16.0f).withPosition (x, centreY - 8.0f);
        g.setColour (colour.withAlpha (0.22f));
        g.fillRoundedRectangle (r, 8.0f);
        g.setColour (colour);
        g.setFont (juce::FontOptions (10.0f, juce::Font::bold));
        g.drawText (label, r, juce::Justification::centred);
    }

    static void card (juce::Graphics& g, juce::Rectangle<float> r)
    {
        Theme::drawGlass (g, r, 12.0f, 0.05f, {}, false, 0.5f);    // a flatter piece of glass inside the panel
    }

    static void text (juce::Graphics& g, const juce::String& s, juce::Rectangle<float> r, float size, bool bold, juce::Colour c)
    {
        g.setColour (Theme::readable (c));
        g.setFont (juce::FontOptions (size, bold ? juce::Font::bold : juce::Font::plain));
        g.drawText (s, r, juce::Justification::centredLeft);
    }

    std::array<std::unique_ptr<StringRow>, 6> rows;
    GlassButton minusButton { juce::String::fromUTF8 ("\xe2\x88\x92") };   // a real minus sign
    GlassButton plusButton  { "+" };
    GlassButton helpButton  { "?   Music basics" };

    // round button whose icon shows the appearance: half-filled circle (system), sun (light), moon (dark)
    struct AppearanceButton  : public juce::Button
    {
        AppearanceButton() : juce::Button ("Appearance") { setTooltip ("Appearance: Dark / Light / System"); }

        void paintButton (juce::Graphics& g, bool over, bool down) override
        {
            const auto ink = Theme::ink();
            auto r = getLocalBounds().toFloat().reduced (0.5f);
            Theme::drawGlassCircle (g, r, down ? 0.18f : (over ? 0.14f : 0.10f));

            const auto c = r.getCentre();
            g.setColour (ink);
            switch (Theme::mode())
            {
                case Theme::Mode::system:                         // a circle, half filled
                {
                    auto circle = juce::Rectangle<float> (16.0f, 16.0f).withCentre (c);
                    g.drawEllipse (circle, 1.6f);
                    juce::Path half;
                    half.addPieSegment (circle, juce::MathConstants<float>::pi, juce::MathConstants<float>::twoPi, 0.0f);
                    g.fillPath (half);
                    break;
                }
                case Theme::Mode::light:                          // a sun: a disc with 8 rays
                {
                    g.fillEllipse (juce::Rectangle<float> (8.0f, 8.0f).withCentre (c));
                    for (int i = 0; i < 8; ++i)
                    {
                        const float a = (float) i * juce::MathConstants<float>::pi / 4.0f;
                        g.drawLine (c.x + 7.0f * std::sin (a), c.y - 7.0f * std::cos (a),
                                    c.x + 10.0f * std::sin (a), c.y - 10.0f * std::cos (a), 1.6f);
                    }
                    break;
                }
                case Theme::Mode::dark:                           // a moon: a disc with a bite taken out
                {
                    juce::Path disc;
                    disc.addEllipse (juce::Rectangle<float> (16.0f, 16.0f).withCentre (c));
                    juce::Path moon (disc);
                    moon.addEllipse (juce::Rectangle<float> (13.0f, 13.0f).withCentre (c.translated (5.0f, -4.0f)));
                    moon.setUsingNonZeroWinding (false);          // even-odd: where the circles overlap stays empty
                    juce::Graphics::ScopedSaveState state (g);
                    g.reduceClipRegion (disc);                    // ...and nothing is drawn outside the first disc
                    g.fillPath (moon);
                    break;
                }
            }
        }
    };
    AppearanceButton appearanceButton;
    juce::TextEditor searchBox;
    juce::String searchMessage;
    float reference = 440.0f;
    bool solid = false;

    juce::Rectangle<float> stringsCaption, tuningCaption, tuningRow, referenceCaption, referenceRow;
    juce::Rectangle<float> songCaption, songRow, songMessage;
    juce::Rectangle<float> instrumentCaption, instrumentRow;
    juce::uint32 instrumentMessageUntil = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SidebarComponent)
};

//==============================================================================
/*
    The round button that opens and closes the sidebar, with the usual
    "sidebar" icon: a window outline with a panel on its left.
*/
class SidebarToggleButton  : public juce::Button
{
public:
    SidebarToggleButton() : juce::Button ("Sidebar") { setTooltip ("Show or hide the sidebar (Ctrl+Cmd+S)"); }

    void paintButton (juce::Graphics& g, bool over, bool down) override
    {
        const auto ink = Theme::ink();
        auto r = getLocalBounds().toFloat().reduced (0.5f);
        Theme::drawGlassCircle (g, r, down ? 0.18f : (over ? 0.14f : 0.10f));

        auto icon = r.withSizeKeepingCentre (18.0f, 14.0f);
        g.setColour (ink.withAlpha (0.9f));
        g.drawRoundedRectangle (icon, 3.0f, 1.6f);
        g.drawLine (icon.getX() + 6.0f, icon.getY(), icon.getX() + 6.0f, icon.getBottom(), 1.6f);
    }
};

//==============================================================================
/*
    The dark veil behind the sidebar when it slides over the tuner (iPhone "drawer").
    Tapping it closes the drawer.
*/
class DrawerScrim  : public juce::Component
{
public:
    std::function<void()> onTap;

    void setAmount (float newAmount)          // 0 = invisible ... 1 = fully dimmed
    {
        amount = newAmount;
        setVisible (amount > 0.01f);
        repaint();
    }

    void paint (juce::Graphics& g) override   { g.fillAll (juce::Colours::black.withAlpha (0.45f * amount)); }
    void mouseDown (const juce::MouseEvent&) override  { if (onTap) onTap(); }

private:
    float amount = 0.0f;
};
