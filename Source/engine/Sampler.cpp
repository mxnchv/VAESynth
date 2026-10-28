#include "Sampler.h"

#include <cmath>

namespace vae
{
    namespace
    {
        constexpr double releaseSeconds = 0.35;
    }

    void Sampler::prepare (double newSampleRate)
    {
        sampleRate = newSampleRate;
        allNotesOff();
    }

    void Sampler::start (const RenderedSound::Ptr& sound, int note, float velocity, float pan, bool fromKey)
    {
        if (sound == nullptr || sound->audio.getNumSamples() < 2)
            return;

        // A free voice, or else the oldest one.
        auto* voice = &voices[0];
        for (auto& v : voices)
        {
            if (v.sound == nullptr) { voice = &v; break; }
            if (v.age < voice->age) voice = &v;
        }

        const auto semitones = (double) (note - sound->rootNote);
        const auto angle = (juce::jlimit (-1.0f, 1.0f, pan) + 1.0f) * juce::MathConstants<float>::pi * 0.25f;

        voice->sound = sound;
        voice->position = 0.0;
        voice->increment = std::pow (2.0, semitones / 12.0) * sound->sampleRate / sampleRate;
        voice->gainLeft = velocity * std::cos (angle) * juce::MathConstants<float>::sqrt2;
        voice->gainRight = velocity * std::sin (angle) * juce::MathConstants<float>::sqrt2;
        voice->release = 1.0f;
        voice->releaseStep = 0.0f;
        voice->note = note;
        voice->fromKey = fromKey;
        voice->keyDown = fromKey;
        voice->age = ++counter;
    }

    void Sampler::beginRelease (Voice& voice)
    {
        if (voice.releaseStep <= 0.0f)
            voice.releaseStep = (float) (1.0 / (releaseSeconds * sampleRate));
    }

    void Sampler::noteOff (int note)
    {
        for (auto& v : voices)
        {
            if (v.sound != nullptr && v.fromKey && v.keyDown && v.note == note)
            {
                v.keyDown = false;
                if (! sustain)
                    beginRelease (v);
            }
        }
    }

    void Sampler::setSustain (bool down)
    {
        sustain = down;
        if (! down)
            for (auto& v : voices)
                if (v.sound != nullptr && v.fromKey && ! v.keyDown)
                    beginRelease (v);
    }

    void Sampler::allNotesOff()
    {
        for (auto& v : voices)
            v.sound = nullptr;
        sustain = false;
    }

    int Sampler::activeVoices() const noexcept
    {
        int n = 0;
        for (const auto& v : voices)
            if (v.sound != nullptr)
                ++n;
        return n;
    }

    void Sampler::render (juce::AudioBuffer<float>& output, int startSample, int numSamples)
    {
        auto* left = output.getWritePointer (0, startSample);
        auto* right = output.getNumChannels() > 1 ? output.getWritePointer (1, startSample) : nullptr;

        for (auto& v : voices)
        {
            if (v.sound == nullptr)
                continue;

            const auto& audio = v.sound->audio;
            const auto length = audio.getNumSamples();
            const auto* sourceLeft = audio.getReadPointer (0);
            const auto* sourceRight = audio.getReadPointer (audio.getNumChannels() > 1 ? 1 : 0);

            for (int i = 0; i < numSamples; ++i)
            {
                const auto index = (int) v.position;
                if (index + 1 >= length || v.release <= 0.0f)
                {
                    v.sound = nullptr;
                    break;
                }

                const auto frac = (float) (v.position - index);
                const auto l = sourceLeft[index] + (sourceLeft[index + 1] - sourceLeft[index]) * frac;
                const auto r = sourceRight[index] + (sourceRight[index + 1] - sourceRight[index]) * frac;

                left[i] += l * v.gainLeft * v.release;
                if (right != nullptr)
                    right[i] += r * v.gainRight * v.release;

                v.position += v.increment;
                v.release -= v.releaseStep;
            }
        }
    }
}
