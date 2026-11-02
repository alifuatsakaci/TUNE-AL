/*
  ==============================================================================

    PluckSynth.h
    Created: 23 Oct 2026
    Author:  Ali Fuat Sakacı

  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>

//==============================================================================
/*
    Karplus-Strong plucked string: the reference tone for manual mode.

    A delay line one period long is filled with noise and fed back through
    a 2-point average (a gentle low-pass, so the sound mellows like a real string).

    Exact tuning: the loop delay must equal fs / f, which is usually not a whole
    number of samples. Rounding it would put the tone up to ~6 cents out of tune,
    so a first-order ALLPASS filter adds the missing fraction of a sample.
      loop delay = N (delay line) + 0.5 (average) + d (allpass)

    Threads: the GUI thread only calls pluck() / stop() / isPlaying().
    Everything else runs on the audio thread, and nothing allocates there.
*/
class PluckSynth
{
public:
    // Audio thread, before playback starts: the only place memory is allocated
    void prepare (double newSampleRate)
    {
        sampleRate = newSampleRate;
        delay.assign ((size_t) (sampleRate / 30.0) + 4, 0.0f);   // room for notes down to 30 Hz
        playing = false;
        playingFlag.store (false);
    }

    // GUI thread: ask for a new pluck at this frequency
    void pluck (float hz)  { requested.store (hz); }

    // GUI thread: ask the string to fade out
    void stop()            { requested.store (-1.0f); }

    bool isPlaying() const { return playingFlag.load(); }

    // Audio thread: ADD the string's sound to every output channel
    void render (juce::AudioBuffer<float>& buffer, int startSample, int numSamples)
    {
        const float request = requested.exchange (0.0f);       // read and reset in one step
        if (request > 0.0f)       startString (request);
        else if (request < 0.0f)  fadingOut = true;

        if (! playing)
            return;

        for (int i = 0; i < numSamples; ++i)
        {
            const float s = tick() * gain;

            for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
                buffer.addSample (ch, startSample + i, s);

            // fade out quickly when stopped, or when the note has rung long enough
            if (fadingOut || --samplesLeft <= 0)
            {
                gain *= 0.999f;
                if (gain < 0.0001f)
                {
                    playing = false;
                    playingFlag.store (false);
                    return;
                }
            }
        }
    }

private:
    void startString (float hz)
    {
        const float period = (float) sampleRate / hz;               // P = fs / f, in samples
        length = juce::jlimit (2, (int) delay.size(), (int) std::floor (period - 0.6f));
        const float d = period - 0.5f - (float) length;             // the fraction the allpass must add
        allpassCoeff = (1.0f - d) / (1.0f + d);

        // fill one period with noise, slightly smoothed for a softer, rounder pluck
        float smooth = 0.0f, mean = 0.0f;
        for (int i = 0; i < length; ++i)
        {
            smooth = 0.6f * smooth + 0.4f * (random.nextFloat() * 2.0f - 1.0f);
            delay[(size_t) i] = smooth;
            mean += smooth;
        }
        mean /= (float) length;
        for (int i = 0; i < length; ++i)
            delay[(size_t) i] -= mean;                               // no DC offset

        // loss per trip round the loop, so the note decays by 60 dB in about 5 s
        loss = std::pow (10.0f, -3.0f / (5.0f * hz));

        readPos = 0;
        prevOut = apIn = apOut = 0.0f;
        gain = 0.35f;
        samplesLeft = (int) (4.0 * sampleRate);                      // ring 4 s, then fade
        fadingOut = false;
        playing = true;
        playingFlag.store (true);
    }

    float tick()
    {
        const float out = delay[(size_t) readPos];                   // the sample from one period ago
        const float avg = 0.5f * (out + prevOut);                    // 2-point average: 0.5 sample delay
        prevOut = out;

        const float ap = allpassCoeff * avg + apIn - allpassCoeff * apOut;   // allpass: fractional delay
        apIn  = avg;
        apOut = ap;

        delay[(size_t) readPos] = ap * loss;                         // feed back into the loop
        readPos = (readPos + 1) % length;
        return out;
    }

    double sampleRate = 48000.0;
    std::vector<float> delay;
    int length = 2, readPos = 0, samplesLeft = 0;
    float allpassCoeff = 0.0f, loss = 0.999f, gain = 0.0f;
    float prevOut = 0.0f, apIn = 0.0f, apOut = 0.0f;
    bool playing = false, fadingOut = false;
    juce::Random random;

    std::atomic<float> requested { 0.0f };       // GUI -> audio: >0 pluck at this Hz, <0 stop
    std::atomic<bool>  playingFlag { false };    // audio -> GUI: is it sounding?
};
