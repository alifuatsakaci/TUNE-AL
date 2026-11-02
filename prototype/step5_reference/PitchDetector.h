#pragma once

#include <vector>
#include <cmath>
#include <algorithm>

//==============================================================================
/*
    YIN pitch detector - a C++ port of pitch_yin() in prototype/pitch_lab.py.

    Plain C++, no JUCE: it only needs an array of samples, so it can be tested
    on its own and reused later in a plug-in.
*/
class PitchDetector
{
public:
    static constexpr int frameSize = 2048;   // samples analysed at once (~43 ms at 48 kHz)

    // Call before audio starts. This is the only place where memory is allocated.
    void prepare (double newSampleRate)
    {
        sampleRate = newSampleRate;

        const int W = frameSize / 2;
        tauMin = (int) (sampleRate / maxFrequency);
        tauMax = std::min ((int) (sampleRate / minFrequency), frameSize - W);

        diff.assign ((size_t) tauMax + 1, 0.0f);
        cmnd.assign ((size_t) tauMax + 1, 1.0f);
    }

    // x must point to frameSize samples.
    // Returns the frequency in Hz, or 0 if there is no clear pitch.
    float detect (const float* x)
    {
        const int W = frameSize / 2;

        // Step 1: difference function  d(tau) = sum (x[i] - x[i+tau])^2
        for (int tau = 1; tau <= tauMax; ++tau)
        {
            float sum = 0.0f;
            for (int i = 0; i < W; ++i)
            {
                const float delta = x[i] - x[i + tau];
                sum += delta * delta;
            }
            diff[(size_t) tau] = sum;
        }

        // Step 2: cumulative mean normalised difference  d'(tau) = d(tau) * tau / sum_{j<=tau} d(j)
        cmnd[0] = 1.0f;
        float runningSum = 0.0f;
        for (int tau = 1; tau <= tauMax; ++tau)
        {
            runningSum += diff[(size_t) tau];
            cmnd[(size_t) tau] = runningSum > 0.0f ? diff[(size_t) tau] * (float) tau / runningSum
                                                   : 1.0f;
        }

        // Step 3: the FIRST dip below the threshold, then slide to the bottom of that dip
        int best = -1;
        for (int tau = tauMin; tau < tauMax; ++tau)
        {
            if (cmnd[(size_t) tau] < threshold)
            {
                while (tau + 1 < tauMax && cmnd[(size_t) tau + 1] < cmnd[(size_t) tau])
                    ++tau;
                best = tau;
                break;
            }
        }

        if (best < 0)            // nothing periodic enough: better no answer than a wrong one
        {
            confidence = 0.0f;
            return 0.0f;
        }

        // Step 3b: octave check - if d'(2 tau) is much lower, the real period is 2 tau
        while (2 * best + 2 < tauMax)
        {
            int doubled = 2 * best - 2;
            for (int t = doubled + 1; t <= 2 * best + 2; ++t)
                if (cmnd[(size_t) t] < cmnd[(size_t) doubled])
                    doubled = t;

            if (cmnd[(size_t) doubled] < octaveRatio * cmnd[(size_t) best])
                best = doubled;
            else
                break;
        }

        confidence = 1.0f - cmnd[(size_t) best];

        // Step 4: parabolic interpolation for sub-sample precision
        return (float) (sampleRate / parabolic (best));
    }

    float getConfidence() const { return confidence; }   // 0..1, how periodic the last frame was

private:
    double parabolic (int i) const
    {
        if (i <= 0 || i >= tauMax)
            return i;

        const float left = cmnd[(size_t) i - 1], mid = cmnd[(size_t) i], right = cmnd[(size_t) i + 1];
        const float denom = left - 2.0f * mid + right;
        if (denom == 0.0f)
            return i;

        const float offset = 0.5f * (left - right) / denom;
        return i + std::clamp (offset, -1.0f, 1.0f);
    }

    static constexpr float minFrequency = 60.0f;     // below the low E string (82 Hz)
    static constexpr float maxFrequency = 1000.0f;
    static constexpr float threshold    = 0.15f;     // same values as the Python prototype
    static constexpr float octaveRatio  = 0.4f;

    double sampleRate = 48000.0;
    int tauMin = 48, tauMax = 800;
    float confidence = 0.0f;
    std::vector<float> diff, cmnd;    // d(tau) and d'(tau), allocated once in prepare()
};
