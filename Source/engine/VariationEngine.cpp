#include "VariationEngine.h"

#include <juce_dsp/juce_dsp.h>

#include <cmath>
#include <vector>

namespace vae
{
    namespace
    {
        constexpr float twoPi = juce::MathConstants<float>::twoPi;

        float midiToHz (double note) noexcept { return (float) (440.0 * std::pow (2.0, (note - 69.0) / 12.0)); }

        // Value of an exponential ramp from `from` to `to` over `duration`, held afterwards.
        float expRamp (float from, float to, float time, float duration) noexcept
        {
            if (time <= 0.0f) return from;
            if (time >= duration) return to;
            return from * std::pow (to / from, time / duration);
        }

        float safeFrequency (float hz, double sampleRate) noexcept
        {
            return juce::jlimit (20.0f, (float) (sampleRate * 0.45), hz);
        }

        using Coefficients = juce::dsp::IIR::Coefficients<float>;

        void applyFilter (std::vector<float>& signal, Coefficients::Ptr coefficients)
        {
            juce::dsp::IIR::Filter<float> filter (coefficients);
            for (auto& s : signal)
                s = filter.processSample (s);
        }

        // Band-passed noise burst with its own envelope, added into `signal`.
        void addNoise (std::vector<float>& signal, const Traits& t, float f0, double sampleRate,
                       float attack, float decay, Rng& rng)
        {
            using namespace trait;
            if (t[noise] <= 0.03f)
                return;

            auto bandpass = Coefficients::makeBandPass (sampleRate, safeFrequency (juce::jmin (12000.0f, f0 * (2.0f + t[bright] * 6.0f)), sampleRate), 1.2f);
            juce::dsp::IIR::Filter<float> filter (bandpass);

            const auto rise = juce::jmax (0.004f, attack * 0.6f);
            const auto end = attack + juce::jmin (decay, 0.12f + t[noise] * decay);
            const auto peak = t[noise] * 0.9f;

            for (size_t i = 0; i < signal.size(); ++i)
            {
                const auto time = (float) ((double) i / sampleRate);
                if (time > end)
                    break;

                const auto level = time < rise ? peak * time / rise : expRamp (peak, 0.0001f, time - rise, end - rise);
                signal[i] += filter.processSample (rng.uniform() * 2.0f - 1.0f) * level;
            }
        }

        // One cycle of the synth's harmonic wave, band-limited for `frequency`.
        std::vector<float> makeWavetable (const Traits& t, float frequency, double sampleRate)
        {
            using namespace trait;
            constexpr int size = 2048;
            std::vector<float> table (size, 0.0f);

            const auto tilt = 2.4f - t[bright] * 1.9f;
            for (int h = 1; h <= 24 && frequency * (float) h < sampleRate * 0.45; ++h)
            {
                const auto amplitude = (1.0f / std::pow ((float) h, tilt)) * (h % 2 == 0 ? 1.0f - t[odd] * 0.9f : 1.0f);
                for (int i = 0; i < size; ++i)
                    table[(size_t) i] += amplitude * std::sin (twoPi * (float) h * (float) i / (float) size);
            }

            float peak = 1.0e-6f;
            for (auto v : table) peak = juce::jmax (peak, std::abs (v));
            for (auto& v : table) v /= peak;
            return table;
        }

        float readTable (const std::vector<float>& table, double phase) noexcept
        {
            const auto size = (double) table.size();
            const auto position = phase * size;
            const auto index = (int) position;
            const auto frac = (float) (position - index);
            const auto a = table[(size_t) (index % (int) size)], b = table[(size_t) ((index + 1) % (int) size)];
            return a + (b - a) * frac;
        }

        void normalisePeak (std::vector<float>& signal, float target, bool onlyDown)
        {
            float peak = 0.0f;
            for (auto v : signal) peak = juce::jmax (peak, std::abs (v));
            if (peak > 1.0e-6f && (! onlyDown || peak > target))
                for (auto& v : signal) v *= target / peak;
        }

        RenderedSound::Ptr makeSound (std::vector<float> signal, double sampleRate, int rootNote)
        {
            RenderedSound::Ptr sound (new RenderedSound());
            sound->sampleRate = sampleRate;
            sound->rootNote = rootNote;
            sound->audio.setSize (1, (int) signal.size());
            sound->audio.copyFrom (0, 0, signal.data(), (int) signal.size());
            return sound;
        }

        // The built-in synth that plays the library seeds: harmonic oscillators,
        // FM shimmer, a decaying low-pass and a breath of noise.
        RenderedSound::Ptr renderSynth (const Traits& t, int note, std::uint32_t noiseSeed)
        {
            using namespace trait;
            const auto sr = DspVariationEngine::synthSampleRate;
            const auto f0 = midiToHz (note);

            const auto swell = isReversed (t);
            const auto attackTime = swell ? 0.35f + t[decay] * 0.6f : 0.002f + t[attack] * t[attack] * 0.35f;
            const auto decayTime = 0.15f + std::pow (t[decay], 1.5f) * 3.2f;
            const auto total = (float) juce::jmin ((double) attackTime + decayTime + 0.05, DspVariationEngine::maxBudSeconds);
            const auto length = (size_t) (total * sr);

            struct Osc { std::vector<float> table; float frequency; float detune; float gain; double phase = 0.0; };
            std::vector<Osc> oscillators;
            oscillators.push_back ({ makeWavetable (t, f0, sr), f0, 1.0f, 1.0f });

            if (t[chorus] > 0.08f)
                oscillators.push_back ({ makeWavetable (t, f0, sr), f0, std::pow (2.0f, (4.0f + t[chorus] * 18.0f) / 1200.0f), 0.6f });

            if (hasOctaveUp (t) || hasOctaveDown (t))
            {
                const auto f = f0 * (hasOctaveUp (t) ? 2.0f : 0.5f);
                oscillators.push_back ({ makeWavetable (t, f, sr), f, 1.0f, juce::jmin (0.8f, std::abs (t[shift] - 0.5f) * 2.5f - 0.5f) });
            }

            const auto fm = t[metal] > 0.05f;
            const auto modFrequency = f0 * (1.0f + t[ratio] * 3.17f);
            const auto depthFrom = juce::jmax (1.0f, f0 * t[metal] * 5.0f), depthTo = juce::jmax (1.0f, f0 * t[metal] * 0.4f);
            double modPhase = 0.0;

            std::vector<float> signal (length, 0.0f);
            Rng rng (noiseSeed);
            addNoise (signal, t, f0, sr, attackTime, decayTime, rng);

            for (size_t i = 0; i < length; ++i)
            {
                const auto time = (float) ((double) i / sr);
                float wobble = 0.0f;
                if (fm)
                {
                    wobble = expRamp (depthFrom, depthTo, time, attackTime + decayTime * 0.5f) * std::sin (twoPi * (float) modPhase);
                    modPhase = std::fmod (modPhase + modFrequency / sr, 1.0);
                }

                float sum = 0.0f;
                for (auto& osc : oscillators)
                {
                    sum += readTable (osc.table, osc.phase) * osc.gain;
                    const auto hz = juce::jmax (0.0f, osc.frequency * osc.detune + wobble);
                    osc.phase = std::fmod (osc.phase + hz / sr, 1.0);
                }

                signal[i] += sum;
            }

            // Low-pass that closes as the note decays.
            juce::dsp::StateVariableTPTFilter<float> filter;
            filter.prepare ({ sr, 64, 1 });
            filter.setType (juce::dsp::StateVariableTPTFilterType::lowpass);
            filter.setResonance (0.4f + t[body] * 5.0f);

            const auto cutoffFrom = safeFrequency (juce::jmin (17000.0f, f0 * (2.0f + t[bright] * 26.0f)), sr);
            const auto cutoffTo = safeFrequency (juce::jlimit (90.0f, 17000.0f, f0 * (1.2f + t[bright] * 5.0f)), sr);
            const auto sweep = attackTime + decayTime * 0.7f;

            for (size_t i = 0; i < length; ++i)
            {
                if (i % 32 == 0)
                    filter.setCutoffFrequency (expRamp (cutoffFrom, cutoffTo, (float) ((double) i / sr), sweep));

                const auto time = (float) ((double) i / sr);
                const auto envelope = time < attackTime ? time / attackTime : expRamp (1.0f, 0.0001f, time - attackTime, total - attackTime);
                signal[i] = filter.processSample (0, signal[i]) * envelope;
            }

            normalisePeak (signal, 0.5f, false);
            return makeSound (std::move (signal), sr, note);
        }

        // Linear-interpolated read of `source` at a fractional position, 0 outside it.
        float readAt (const std::vector<float>& source, double position) noexcept
        {
            const auto index = (int) position;
            if (index < 0 || index + 1 >= (int) source.size())
                return 0.0f;

            const auto frac = (float) (position - index);
            return source[(size_t) index] + (source[(size_t) index + 1] - source[(size_t) index]) * frac;
        }

        // Your sound with effects piled on top: copies (detuned, octave), ring
        // modulation, tone and resonance filters, breath, and a new envelope.
        RenderedSound::Ptr renderSample (const Traits& t, const SeedSource& seed, std::uint32_t noiseSeed)
        {
            using namespace trait;
            const auto sr = seed.sampleRate;

            if (isNeutral (t))
            {
                RenderedSound::Ptr sound (new RenderedSound());
                sound->audio.makeCopyOf (seed.audio);
                sound->sampleRate = sr;
                sound->rootNote = seed.rootNote;
                return sound;
            }

            std::vector<float> source ((size_t) seed.audio.getNumSamples(), 0.0f);
            for (int ch = 0; ch < seed.audio.getNumChannels(); ++ch)
                for (size_t i = 0; i < source.size(); ++i)
                    source[i] += seed.audio.getSample (ch, (int) i) / (float) seed.audio.getNumChannels();

            if (isReversed (t))
                std::reverse (source.begin(), source.end());

            const auto sourceSeconds = (float) ((double) source.size() / sr);
            const auto attackTime = t[attack] * t[attack] * 0.5f;
            const auto cut = t[decay] < 0.97f;
            const auto hold = cut ? 0.06f + std::pow (t[decay], 1.6f) * sourceSeconds : sourceSeconds;
            const auto endTime = juce::jmin ((double) juce::jmin (sourceSeconds, attackTime + hold) + 0.02, DspVariationEngine::maxBudSeconds);
            const auto length = (size_t) juce::jmax (1.0, endTime * sr);
            const auto f0 = midiToHz (seed.rootNote);

            struct Copy { double rate; float gain; };
            std::vector<Copy> copies { { 1.0, 1.0f } };
            if (t[chorus] > 0.08f)
                copies.push_back ({ std::pow (2.0, (6.0 + t[chorus] * 22.0) / 1200.0), 0.6f });
            if (hasOctaveUp (t) || hasOctaveDown (t))
                copies.push_back ({ hasOctaveUp (t) ? 2.0 : 0.5, juce::jmin (0.9f, std::abs (t[shift] - 0.5f) * 3.0f - 0.6f) });

            const auto ring = t[metal] > 0.06f;
            const auto ringFrequency = f0 * (1.5f + t[ratio] * 3.17f);

            std::vector<float> signal (length, 0.0f);
            for (size_t i = 0; i < length; ++i)
            {
                float sum = 0.0f;
                for (const auto& copy : copies)
                    sum += readAt (source, (double) i * copy.rate) * copy.gain;

                if (ring)
                {
                    const auto modulator = std::sin (twoPi * ringFrequency * (float) ((double) i / sr));
                    sum = sum * (1.0f - t[metal] * 0.6f) + sum * modulator * t[metal] * 1.4f;
                }

                signal[i] = sum;
            }

            Rng rng (noiseSeed);
            addNoise (signal, t, f0, sr, juce::jmax (0.004f, attackTime), juce::jmin (hold, 0.5f), rng);

            // Darker buds close a low-pass; brighter ones lift the top and thin the bottom.
            const auto tone = t[bright] - 0.5f;
            if (tone < -0.04f)
                applyFilter (signal, Coefficients::makeLowPass (sr, safeFrequency (18000.0f * std::pow (2.0f, tone * 14.0f), sr), 0.7f));
            else if (tone > 0.04f)
                applyFilter (signal, Coefficients::makeHighShelf (sr, safeFrequency (2500.0f, sr), 0.7f, juce::Decibels::decibelsToGain (tone * 26.0f)));
            if (tone > 0.15f)
                applyFilter (signal, Coefficients::makeHighPass (sr, safeFrequency (40.0f * std::pow (2.0f, tone * 11.0f), sr), 0.7f));

            if (t[body] > 0.05f)
                applyFilter (signal, Coefficients::makePeakFilter (sr, safeFrequency (juce::jmin (9000.0f, f0 * (2.0f + t[ratio] * 3.0f)), sr),
                                                                   6.0f, juce::Decibels::decibelsToGain (t[body] * 18.0f)));

            const auto fadeStart = attackTime + hold * 0.25f;
            const auto end = (float) endTime;
            for (size_t i = 0; i < length; ++i)
            {
                const auto time = (float) ((double) i / sr);
                auto gain = 1.0f;
                if (attackTime > 0.003f && time < attackTime)
                    gain = time / attackTime;
                if (cut && time > fadeStart)
                    gain *= expRamp (1.0f, 0.0001f, time - fadeStart, juce::jmax (0.01f, end - fadeStart));
                signal[i] *= gain;
            }

            // Never louder than full scale; quieter buds stay quieter.
            normalisePeak (signal, 0.95f, true);
            return makeSound (std::move (signal), sr, seed.rootNote);
        }

        std::uint32_t hashTraits (const Traits& t) noexcept
        {
            std::uint32_t h = 2166136261u;
            for (auto v : t)
            {
                h ^= (std::uint32_t) (v * 1000003.0f);
                h *= 16777619u;
            }
            return h;
        }
    }

    Traits DspVariationEngine::mutate (const Traits& parent, const MutationRequest& request, Rng& rng) const
    {
        using namespace trait;
        Traits child {};

        for (size_t i = 0; i < count; ++i)
        {
            // The octave and reverse switches need bigger steps to ever flip.
            const auto scale = i == shift ? 2.2f : (i == reverse ? 2.6f : 1.0f);
            child[i] = juce::jlimit (0.0f, 1.0f, parent[i] + rng.gauss() * request.amount * scale);
        }

        // Branches leaning right get brighter and more metallic, left ones darker and softer.
        child[bright] = juce::jlimit (0.0f, 1.0f, child[bright] + request.lean * request.amount * 1.2f);
        child[metal] = juce::jlimit (0.0f, 1.0f, child[metal] + juce::jmax (0.0f, request.lean) * request.amount * 0.6f);
        return child;
    }

    RenderedSound::Ptr DspVariationEngine::render (const Traits& traits, const SeedSource& seed) const
    {
        // Noise is seeded from the traits so the same node always renders the same.
        const auto noiseSeed = hashTraits (traits);

        if (seed.kind == SeedSource::Kind::library)
            return renderSynth (traits, seed.rootNote, noiseSeed);

        return renderSample (traits, seed, noiseSeed);
    }
}
