/*
  ==============================================================================

    PitchStabiliser.h
    Created: 23 Oct 2026 11:44:28am
    Author:  Ali Fuat Sakacı

  ==============================================================================
*/

#pragma once

#include <array>
#include <algorithm>
#include <cmath>

//==============================================================================
/*
    Turns raw YIN readings into a stable value for the display.
    C++ port of run() in prototype/stabilise_lab.py.

    Works in "semitones from A4": 0 = A4 (440 Hz), 1 = A#4, -12 = A3, ...
    so a note is an integer and the cents are the fractional part x 100.
*/
class PitchStabiliser
{
public:
    static constexpr float noPitch = -1000.0f;   // returned when there is nothing to show

    void prepare (double sampleRate, int hopSize)
    {
        const float frameTime = (float) hopSize / (float) sampleRate;
        smoothCoeff = std::exp (-frameTime / smoothTau);   // same formula as the level meter
        reset();
    }

    void reset()
    {
        note = noNote;
        historySize = 0;
        candidateCount = 0;
        holdFrames = 0;
        previousDb = -100.0f;
        cents = 0.0f;
        quietFrames = 0;
        noteAge = 0;
        fresh = false;
    }

    // Call once per analysis frame. frequency = 0 if YIN found no pitch.
    // Returns the pitch to display (note + cents / 100, in semitones from A4) or noPitch.
    float process (float frequency, float confidence, float levelDb, float referenceHz = 440.0f)
    {
        // 1) onset: a new pluck starts a fresh note, but the old one stays on screen
        //    until the new readings agree (otherwise the display blinks off at every pluck)
        if (levelDb - previousDb > onsetJumpDb)
        {
            holdFrames = holdAfterOnset;
            historySize = 0;
            fresh = true;
        }
        previousDb = levelDb;

        //    gate + release: when it goes quiet, keep the note a moment before clearing it
        if (levelDb < gateDb)
            return quietFrame();

        if (holdFrames > 0)              // still inside the pick attack
        {
            --holdFrames;
            return current();
        }

        // 2) confidence check (no clear pitch counts as "quiet" too)
        if (frequency <= 0.0f || confidence < minConfidence)
            return quietFrame();

        quietFrames = 0;
        const float reading = 12.0f * std::log2 (frequency / referenceHz);

        // 4) note lock: a ringing string can't jump a semitone, so far readings are outliers...
        if (note != noNote && ! fresh && std::abs (reading - (float) note) > 0.5f)
        {
            const int nearest = (int) std::round (reading);
            candidateCount = (nearest == candidate) ? candidateCount + 1 : 1;
            candidate = nearest;

            if (candidateCount < noteChangeFrames)
                return current();

            note = nearest;              // ...unless they persist (e.g. a big turn of the peg)
            historySize = 0;
            noteAge = 0;
            cents = 100.0f * (reading - (float) note);
        }
        candidateCount = 0;

        // 3) median of the last few readings
        addToHistory (reading);
        const float m = median();

        if (note == noNote || fresh)     // just after a pluck: wait until readings agree
        {
            if (! lastReadingsAgree())
                return current();        // meanwhile keep showing the old note (or nothing)

            const int newNote = (int) std::round (m);
            if (newNote != note)
            {
                cents = 100.0f * (m - (float) newNote);
                noteAge = 0;
            }
            note = newNote;
            fresh = false;
        }

        ++noteAge;

        // 5) one-pole smoothing of the cents
        const float target = std::clamp (100.0f * (m - (float) note), -50.0f, 50.0f);
        cents = smoothCoeff * cents + (1.0f - smoothCoeff) * target;

        return current();
    }

private:
    // a frame with nothing usable: keep the note for a short "release", then clear it.
    // Only notes that lasted a little while are held, so a click doesn't stick on screen.
    float quietFrame()
    {
        ++quietFrames;
        if (quietFrames > (noteAge >= minNoteFrames ? releaseFrames : 0))
        {
            note = noNote;
            historySize = 0;
        }
        return current();
    }

    float current() const
    {
        return note == noNote ? noPitch : (float) note + cents / 100.0f;
    }

    void addToHistory (float value)
    {
        if (historySize == medianSize)   // full: drop the oldest (shift left)
        {
            std::copy (history.begin() + 1, history.end(), history.begin());
            --historySize;
        }
        history[(size_t) historySize++] = value;
    }

    float median() const
    {
        std::array<float, medianSize> sorted = history;      // a copy, on the stack: no allocation
        std::sort (sorted.begin(), sorted.begin() + historySize);
        const int mid = historySize / 2;
        return (historySize % 2 == 1) ? sorted[(size_t) mid]
                                      : 0.5f * (sorted[(size_t) mid - 1] + sorted[(size_t) mid]);
    }

    bool lastReadingsAgree() const
    {
        if (historySize < startFrames)
            return false;

        const auto first = history.begin() + (historySize - startFrames);
        const auto last  = history.begin() + historySize;
        return *std::max_element (first, last) - *std::min_element (first, last) <= 0.5f;
    }

    // same values as stabilise_lab.py
    static constexpr float gateDb           = -62.0f;  // just above a quiet room: YIN's confidence rejects noise
    static constexpr int   releaseFrames    = 16;      // ~0.35 s: keep the note this long after it goes quiet...
    static constexpr int   minNoteFrames    = 8;       // ...if it lasted ~0.2 s
    static constexpr float onsetJumpDb      = 6.0f;
    static constexpr int   holdAfterOnset   = 3;       // frames (~70 ms)
    static constexpr float minConfidence    = 0.85f;
    static constexpr int   medianSize       = 5;
    static constexpr int   startFrames      = 3;
    static constexpr int   noteChangeFrames = 10;      // ~0.2 s
    static constexpr float smoothTau        = 0.15f;   // seconds
    static constexpr int   noNote           = -10000;

    float smoothCoeff = 0.0f;
    std::array<float, medianSize> history {};
    int historySize = 0;
    int note = noNote;
    int candidate = noNote, candidateCount = 0;
    int holdFrames = 0;
    float previousDb = -100.0f;
    float cents = 0.0f;
    int quietFrames = 0;             // frames in a row without a usable reading
    int noteAge = 0;                 // usable readings since the note was chosen
    bool fresh = false;              // a new pluck started: the next agreeing readings choose the note
};
