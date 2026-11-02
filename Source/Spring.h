/*
  ==============================================================================

    Spring.h
    Created: 30 Oct 2026
    Author:  Ali Fuat Sakacı

  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>

//==============================================================================
/*
    A spring animation, the way Apple animates sheets and drawers.

    Instead of "move from A to B in 0.3 seconds", the value is attached to its target
    by a spring: every frame the spring pulls it towards the target, and friction slows
    it down. Two numbers describe the feel (Apple's names):

      damping   1.0 = no overshoot, a smooth settle; less than 1 = it overshoots a little
                (0.8: a tiny bounce, used after a flick)
      response  roughly how many seconds it takes to get there (smaller = snappier)

    Why a spring and not a fixed animation? It keeps its velocity. If the target changes
    halfway (you click again, or grab the drawer), it simply turns around from where it is,
    at the speed it has, with no jump. And a drag can hand its finger speed to the spring.

    It is the mass-spring-damper from physics class (with mass = 1):
        x'' = -k (x - target) - c x'     with  k = (2 pi / response)^2,  c = 2 * damping * sqrt (k)
*/
struct Spring
{
    float value    = 0.0f;      // where it is now (what is drawn)
    float velocity = 0.0f;      // units per second
    float target   = 0.0f;      // where it is going
    float damping  = 1.0f;
    float response = 0.3f;

    void setParameters (float newDamping, float newResponse)   { damping = newDamping; response = newResponse; }

    // jump straight to a value and stop (used while a finger holds it)
    void snapTo (float newValue)    { value = target = newValue; velocity = 0.0f; }

    bool isSettled() const          { return value == target && velocity == 0.0f; }

    // move forward by dt seconds; returns false when it is resting and nothing changed
    bool step (float dt)
    {
        if (isSettled())
            return false;

        const float omega     = juce::MathConstants<float>::twoPi / response;
        const float stiffness = omega * omega;
        const float friction  = 2.0f * damping * omega;

        // a few small steps per frame keep the simulation accurate ("semi-implicit Euler")
        constexpr int subSteps = 4;
        const float h = dt / (float) subSteps;
        for (int i = 0; i < subSteps; ++i)
        {
            const float acceleration = -stiffness * (value - target) - friction * velocity;
            velocity += acceleration * h;
            value    += velocity * h;
        }

        // close enough and slow enough: rest exactly on the target
        if (std::abs (value - target) < 0.0005f && std::abs (velocity) < 0.005f)
            snapTo (target);

        return true;
    }
};

//==============================================================================
// Two helpers from Apple's "Designing Fluid Interfaces" talk (WWDC 2018)

// Where a flicked object would come to rest if it kept sliding and slowed down like a scroll view.
// velocity in pixels per second -> distance in pixels
inline float projectMomentum (float velocity, float decelerationRate = 0.998f)
{
    return (velocity / 1000.0f) * decelerationRate / (1.0f - decelerationRate);
}

// Rubber band: past the edge, the object follows the finger less and less (like pulling a list
// past its end) instead of stopping dead. overshoot and dimension in pixels.
inline float rubberBand (float overshoot, float dimension, float constant = 0.55f)
{
    return (overshoot * dimension * constant) / (dimension + constant * std::abs (overshoot));
}
