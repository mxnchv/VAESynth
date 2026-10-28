#include "Recipe.h"
#include "VariationEngine.h"

#include <juce_dsp/juce_dsp.h>

#include <cmath>

namespace vae
{
    namespace
    {
        constexpr float twoPi = juce::MathConstants<float>::twoPi;
        constexpr int maxPartials = 160;
        constexpr float topFrequency = 18000.0f;
        // Turns an analysed band magnitude back into noise of the same level
        // (measured: white noise in, white noise out, at 44.1/48 kHz and both FFT sizes).
        constexpr float noiseCalibration = 1.27f;

        float midiToHz (double note) noexcept { return (float) (440.0 * std::pow (2.0, (note - 69.0) / 12.0)); }

        // Fast sine from a table, for banks of a hundred oscillators.
        struct SineTable
        {
            static constexpr int size = 4096;
            std::array<float, size + 1> values {};

            SineTable()
            {
                for (int i = 0; i <= size; ++i)
                    values[(size_t) i] = std::sin (twoPi * (float) i / (float) size);
            }

            float operator() (float phase) const noexcept  // phase in 0..1
            {
                const auto position = phase * (float) size;
                const auto index = (int) position;
                const auto frac = position - (float) index;
                return values[(size_t) index] + (values[(size_t) index + 1] - values[(size_t) index]) * frac;
            }
        };

        const SineTable& sine()
        {
            static const SineTable table;
            return table;
        }

        // Log-spaced noise bands from 40 Hz to the top of the spectrum.
        std::vector<float> makeBandEdges (double sampleRate)
        {
            const auto top = juce::jmin (topFrequency, (float) (sampleRate * 0.47));
            constexpr int bands = 28;
            std::vector<float> edges;
            for (int b = 0; b <= bands; ++b)
                edges.push_back (40.0f * std::pow (top / 40.0f, (float) b / (float) bands));
            return edges;
        }
    }

    // ---------------------------------------------------------------- analysis

    RecipePtr analyse (const juce::AudioBuffer<float>& audio, double sampleRate, int rootNote)
    {
        auto recipe = std::make_shared<Recipe>();
        recipe->sampleRate = sampleRate;
        recipe->rootNote = rootNote;
        recipe->fundamental = midiToHz (rootNote);
        recipe->numSamples = audio.getNumSamples();
        recipe->stereo = audio.getNumChannels() > 1;
        recipe->peak = audio.getMagnitude (0, audio.getNumSamples());

        const auto length = audio.getNumSamples();
        std::vector<float> mono ((size_t) length, 0.0f);
        for (int ch = 0; ch < audio.getNumChannels(); ++ch)
            for (int i = 0; i < length; ++i)
                mono[(size_t) i] += audio.getSample (ch, i) / (float) audio.getNumChannels();

        if (recipe->stereo)
        {
            double lr = 0.0, ll = 0.0, rr = 0.0;
            for (int i = 0; i < length; ++i)
            {
                const auto l = audio.getSample (0, i), r = audio.getSample (1, i);
                lr += (double) l * r; ll += (double) l * l; rr += (double) r * r;
            }
            const auto correlation = ll > 0.0 && rr > 0.0 ? lr / std::sqrt (ll * rr) : 1.0;
            recipe->width = (float) juce::jlimit (0.0, 1.0, 1.0 - correlation);
        }

        // A window long enough to separate partials even for low notes.
        auto order = 11;
        while ((1 << order) < 4.0 * sampleRate / recipe->fundamental && order < 14)
            ++order;
        const auto n = 1 << order;
        recipe->fftSize = n;
        recipe->hop = n / 8;
        recipe->numFrames = juce::jmax (1, length / recipe->hop + 1);

        juce::dsp::FFT fft (order);
        std::vector<float> window ((size_t) n);
        float windowSum = 0.0f;
        for (int i = 0; i < n; ++i)
        {
            window[(size_t) i] = 0.5f - 0.5f * std::cos (twoPi * (float) i / (float) n);
            windowSum += window[(size_t) i];
        }

        const auto bins = n / 2 + 1;
        const auto binHz = (float) (sampleRate / n);
        std::vector<float> magnitudes ((size_t) recipe->numFrames * (size_t) bins);
        std::vector<float> frame ((size_t) n * 2);

        for (int t = 0; t < recipe->numFrames; ++t)
        {
            std::fill (frame.begin(), frame.end(), 0.0f);
            const auto start = t * recipe->hop - n / 2;  // frame t is centred on sample t·hop
            for (int i = 0; i < n; ++i)
            {
                const auto s = start + i;
                if (s >= 0 && s < length)
                    frame[(size_t) i] = mono[(size_t) s] * window[(size_t) i];
            }
            fft.performFrequencyOnlyForwardTransform (frame.data(), true);
            std::copy (frame.begin(), frame.begin() + bins, magnitudes.begin() + (std::ptrdiff_t) ((size_t) t * (size_t) bins));
        }

        auto magnitude = [&] (int t, int bin) { return magnitudes[(size_t) t * (size_t) bins + (size_t) juce::jlimit (0, bins - 1, bin)]; };

        // Where each partial really sits, measured around the loudest moment.
        int loudest = 0;
        double loudestEnergy = -1.0;
        for (int t = 0; t < recipe->numFrames; ++t)
        {
            double energy = 0.0;
            for (int b = 0; b < bins; ++b)
                energy += (double) magnitude (t, b) * magnitude (t, b);
            if (energy > loudestEnergy) { loudestEnergy = energy; loudest = t; }
        }

        std::vector<float> average ((size_t) bins, 0.0f);
        float averagePeak = 0.0f;
        for (int t = loudest; t < juce::jmin (recipe->numFrames, loudest + 6); ++t)
            for (int b = 0; b < bins; ++b)
                average[(size_t) b] += magnitude (t, b);
        for (auto v : average)
            averagePeak = juce::jmax (averagePeak, v);

        const auto f0 = recipe->fundamental;
        const auto top = juce::jmin (topFrequency, (float) (sampleRate * 0.45));
        recipe->numPartials = juce::jlimit (1, maxPartials, (int) (top / f0));

        // Peak near a frequency, refined by a parabola through the log magnitudes.
        auto findPeak = [&] (const std::vector<float>& spectrum, float around, float halfWidth, float& value)
        {
            const auto lo = juce::jmax (1, (int) ((around - halfWidth) / binHz));
            const auto hi = juce::jmin (bins - 2, (int) ((around + halfWidth) / binHz) + 1);
            auto best = juce::jlimit (1, bins - 2, (int) std::round (around / binHz));
            for (int b = lo; b <= hi; ++b)
                if (spectrum[(size_t) b] > spectrum[(size_t) best])
                    best = b;

            const auto a = std::log (spectrum[(size_t) best - 1] + 1.0e-9f), c = std::log (spectrum[(size_t) best] + 1.0e-9f),
                       d = std::log (spectrum[(size_t) best + 1] + 1.0e-9f);
            const auto curvature = a - 2.0f * c + d;
            const auto shift = curvature < 0.0f ? juce::jlimit (-0.5f, 0.5f, 0.5f * (a - d) / curvature) : 0.0f;
            value = std::exp (c - 0.25f * (a - d) * shift);
            return ((float) best + shift) * binHz;
        };

        recipe->ratios.assign ((size_t) recipe->numPartials, 1.0f);
        auto stretch = 1.0f;
        for (int k = 1; k <= recipe->numPartials; ++k)
        {
            float value = 0.0f;
            const auto found = findPeak (average, (float) k * f0 * stretch, f0 * 0.35f, value);
            if (value > averagePeak * 1.0e-4f)
            {
                const auto ratio = juce::jlimit (0.85f, 1.3f, found / ((float) k * f0));
                recipe->ratios[(size_t) k - 1] = ratio;
                stretch = 0.7f * stretch + 0.3f * ratio;
            }
            else
            {
                recipe->ratios[(size_t) k - 1] = stretch;
            }
        }

        // How loud each partial is over time.
        const auto sineScale = 2.0f / windowSum;
        recipe->amplitudes.assign ((size_t) recipe->numFrames * (size_t) recipe->numPartials, 0.0f);
        std::vector<float> spectrum ((size_t) bins);
        for (int t = 0; t < recipe->numFrames; ++t)
        {
            std::copy (magnitudes.begin() + (std::ptrdiff_t) ((size_t) t * (size_t) bins),
                       magnitudes.begin() + (std::ptrdiff_t) ((size_t) (t + 1) * (size_t) bins), spectrum.begin());
            for (int k = 1; k <= recipe->numPartials; ++k)
            {
                float value = 0.0f;
                findPeak (spectrum, (float) k * f0 * recipe->ratios[(size_t) k - 1], binHz * 2.0f, value);
                recipe->amplitudes[(size_t) (t * recipe->numPartials + k - 1)] = value * sineScale;
            }
        }

        // Noise: what is left between the partials, averaged per band.
        recipe->bandEdges = makeBandEdges (sampleRate);
        recipe->numBands = (int) recipe->bandEdges.size() - 1;
        std::vector<bool> masked ((size_t) bins, false);
        const auto maskHalfWidth = juce::jmax (2.0f, 0.2f * f0 / binHz);
        for (int k = 1; k <= recipe->numPartials; ++k)
        {
            const auto centre = (float) k * f0 * recipe->ratios[(size_t) k - 1] / binHz;
            for (auto b = (int) (centre - maskHalfWidth); b <= (int) (centre + maskHalfWidth); ++b)
                if (b >= 0 && b < bins)
                    masked[(size_t) b] = true;
        }

        recipe->noise.assign ((size_t) recipe->numFrames * (size_t) recipe->numBands, 0.0f);
        for (int t = 0; t < recipe->numFrames; ++t)
        {
            for (int band = 0; band < recipe->numBands; ++band)
            {
                const auto lo = juce::jmax (1, (int) (recipe->bandEdges[(size_t) band] / binHz));
                const auto hi = juce::jmin (bins - 1, juce::jmax (lo, (int) (recipe->bandEdges[(size_t) band + 1] / binHz)));
                double sum = 0.0;
                int count = 0;
                float lowest = 1.0e9f;
                for (int b = lo; b <= hi; ++b)
                {
                    lowest = juce::jmin (lowest, magnitude (t, b));
                    if (! masked[(size_t) b]) { sum += magnitude (t, b); ++count; }
                }
                recipe->noise[(size_t) (t * recipe->numBands + band)] = count > 0 ? (float) (sum / count) : lowest;
            }
        }

        return recipe;
    }

    // ---------------------------------------------------------------- genes

    Genes genesFrom (const Traits& node, const Traits& seed)
    {
        using namespace trait;
        auto d = [&] (Index i) { return juce::jlimit (-1.0f, 1.0f, (node[i] - seed[i]) * 2.0f); };

        Genes g;
        g.tilt = d (bright);
        g.evenOdd = d (odd);
        g.inharmonic = d (metal);
        g.formant = d (ratio);
        g.attack = d (attack);
        g.decay = d (decay);
        g.body = d (body);
        g.noise = d (noise);
        g.chorus = d (chorus);
        g.reversed = isReversed (node) && ! isReversed (seed);

        if (hasOctaveUp (node) && ! hasOctaveUp (seed))
            g.octaveLayer = 1;
        else if (hasOctaveDown (node) && ! hasOctaveDown (seed))
            g.octaveLayer = -1;
        g.octaveGain = juce::jmin (0.9f, std::abs (node[shift] - 0.5f) * 3.0f - 0.6f);
        return g;
    }

    // ---------------------------------------------------------------- resynthesis

    juce::AudioBuffer<float> resynthesise (const Recipe& r, int note, const Genes& g, double maxSeconds, std::uint32_t noiseSeed)
    {
        const auto sr = r.sampleRate;
        const auto f0 = r.fundamental * std::pow (2.0f, (float) (note - r.rootNote) / 12.0f);
        const auto timeScale = g.decay > 0.0f ? 1.0f + g.decay * 1.5f : 1.0f;
        const auto length = (int) juce::jmin ((double) r.numSamples * timeScale, maxSeconds * sr);
        const auto stereo = r.stereo || g.chorus > 0.05f;
        const auto top = juce::jmin (topFrequency, (float) (sr * 0.45));
        const auto formantWarp = std::pow (2.0f, g.formant * 0.6f);
        const auto resonance = juce::jlimit (300.0f, 5000.0f, r.fundamental * 4.0f) * formantWarp;
        const auto lastPosition = (float) r.numPartials * r.ratios.back();

        juce::AudioBuffer<float> out (stereo ? 2 : 1, juce::jmax (1, length));
        out.clear();
        if (length <= 0 || r.numFrames == 0)
            return out;

        // Where in the analysed sound we are at output sample s (stretched, maybe reversed).
        auto framePosition = [&] (int s)
        {
            auto position = (float) s / timeScale / (float) r.hop;
            if (g.reversed)
                position = (float) (r.numFrames - 1) - position;
            return juce::jlimit (0.0f, (float) (r.numFrames - 1), position);
        };

        // Each new partial reads the seed's colour at its own frequency, so the
        // timbre keeps its formants on every note instead of moving with it.
        struct Voice
        {
            float frequency, gain, attackTime, damping;
            int lower, upper;
            float weight, rolloff;
            float phaseL, phaseR, detune;
        };

        Rng rng (noiseSeed);
        std::vector<Voice> voices;

        auto addPartials = [&] (float fundamental, float layerGain)
        {
            const auto b = g.inharmonic > 0.0f ? g.inharmonic * g.inharmonic * 0.01f : 0.0f;
            for (int j = 1; j <= maxPartials; ++j)
            {
                auto ratio = r.ratios[(size_t) juce::jmin (j, r.numPartials) - 1];
                if (g.inharmonic < 0.0f)
                    ratio = 1.0f + (ratio - 1.0f) * (1.0f + g.inharmonic);
                ratio *= std::sqrt (1.0f + b * (float) (j * j)) / std::sqrt (1.0f + b);

                const auto frequency = (float) j * fundamental * ratio;
                if (frequency >= top)
                    break;

                // Position in the seed's partial series where this frequency's colour lives.
                const auto position = frequency / formantWarp / r.fundamental;
                Voice v {};
                v.frequency = frequency;
                if (position <= r.ratios[0])
                {
                    v.lower = v.upper = 0;
                    v.weight = 0.0f;
                    v.rolloff = juce::jmax (0.05f, position / r.ratios[0]);
                }
                else if (position >= lastPosition)
                {
                    v.lower = v.upper = r.numPartials - 1;
                    v.weight = 0.0f;
                    v.rolloff = std::pow (lastPosition / position, 3.0f);
                }
                else
                {
                    auto k = 0;
                    while (k + 1 < r.numPartials && (float) (k + 2) * r.ratios[(size_t) k + 1] < position)
                        ++k;
                    const auto p0 = (float) (k + 1) * r.ratios[(size_t) k], p1 = (float) (k + 2) * r.ratios[(size_t) juce::jmin (k + 1, r.numPartials - 1)];
                    v.lower = k;
                    v.upper = juce::jmin (k + 1, r.numPartials - 1);
                    v.weight = p1 > p0 ? juce::jlimit (0.0f, 1.0f, (position - p0) / (p1 - p0)) : 0.0f;
                    v.rolloff = 1.0f;
                }

                auto gain = layerGain * std::pow (frequency / fundamental, g.tilt);
                if (j > 1 && g.evenOdd > 0.0f && j % 2 == 0) gain *= 1.0f - g.evenOdd * 0.9f;
                if (j > 1 && g.evenOdd < 0.0f && j % 2 == 1) gain *= 1.0f + g.evenOdd * 0.9f;
                if (g.body > 0.0f)
                {
                    const auto octaves = std::log2 (frequency / resonance) / 0.3f;
                    gain *= 1.0f + g.body * 5.0f * std::exp (-0.5f * octaves * octaves);
                }

                v.gain = gain;
                v.attackTime = g.attack > 0.0f ? g.attack * g.attack * 0.8f * (1.0f + (float) j * 0.03f) : 0.0f;
                v.damping = g.decay < 0.0f ? -g.decay * 6.0f * (1.0f + (float) j * 0.08f) : 0.0f;
                v.phaseL = rng.uniform();
                v.phaseR = std::fmod (v.phaseL + rng.uniform() * r.width * 0.5f, 1.0f);
                v.detune = g.chorus > 0.05f ? std::pow (2.0f, g.chorus * 14.0f / 1200.0f) : 1.0f;
                voices.push_back (v);
            }
        };

        addPartials (f0, 1.0f);
        if (g.octaveLayer != 0 && g.octaveGain > 0.0f)
            addPartials (f0 * (g.octaveLayer > 0 ? 2.0f : 0.5f), g.octaveGain);

        auto* left = out.getWritePointer (0);
        auto* right = stereo ? out.getWritePointer (1) : nullptr;
        const auto& table = sine();
        constexpr int block = 64;
        const auto twinGain = g.chorus > 0.05f ? 0.7f * g.chorus : 0.0f;

        for (auto& v : voices)
        {
            const auto increment = v.frequency / (float) sr;
            const auto twinUp = increment * v.detune, twinDown = increment / v.detune;
            auto phaseL = v.phaseL, phaseR = v.phaseR, twinL = v.phaseL, twinR = v.phaseR;

            auto amplitudeAt = [&] (int s)
            {
                const auto position = framePosition (s);
                const auto t0 = (int) position;
                const auto t1 = juce::jmin (t0 + 1, r.numFrames - 1);
                const auto ft = position - (float) t0;
                const auto a0 = r.amplitude (t0, v.lower) * (1.0f - v.weight) + r.amplitude (t0, v.upper) * v.weight;
                const auto a1 = r.amplitude (t1, v.lower) * (1.0f - v.weight) + r.amplitude (t1, v.upper) * v.weight;
                auto amplitude = (a0 + (a1 - a0) * ft) * v.rolloff * v.gain;

                const auto seconds = (float) s / (float) sr;
                if (v.attackTime > 0.0f)
                    amplitude *= juce::jmin (1.0f, seconds / v.attackTime);
                if (v.damping > 0.0f)  // the faster decay starts once the attack is over
                    amplitude *= std::exp (-juce::jmax (0.0f, seconds - v.attackTime) * v.damping);
                return amplitude;
            };

            for (int start = 0; start < length; start += block)
            {
                const auto count = juce::jmin (block, length - start);
                const auto from = amplitudeAt (start), to = amplitudeAt (start + count);
                if (from < 1.0e-6f && to < 1.0e-6f)
                {
                    // Silent here: just keep the phases moving.
                    phaseL = std::fmod (phaseL + increment * (float) count, 1.0f);
                    phaseR = std::fmod (phaseR + increment * (float) count, 1.0f);
                    twinL = std::fmod (twinL + twinUp * (float) count, 1.0f);
                    twinR = std::fmod (twinR + twinDown * (float) count, 1.0f);
                    continue;
                }

                const auto step = (to - from) / (float) count;
                auto amplitude = from;
                for (int i = 0; i < count; ++i)
                {
                    auto l = table (phaseL);
                    auto rt = table (phaseR);
                    if (twinGain > 0.0f)
                    {
                        // Detuned twins, one up on the left and one down on the right.
                        l += table (twinL) * twinGain;
                        rt += table (twinR) * twinGain;
                        twinL += twinUp;   if (twinL >= 1.0f) twinL -= 1.0f;
                        twinR += twinDown; if (twinR >= 1.0f) twinR -= 1.0f;
                    }

                    left[start + i] += l * amplitude;
                    if (right != nullptr)
                        right[start + i] += rt * amplitude;

                    phaseL += increment; if (phaseL >= 1.0f) phaseL -= 1.0f;
                    phaseR += increment; if (phaseR >= 1.0f) phaseR -= 1.0f;
                    amplitude += step;
                }
            }
        }

        // Noise: random-phase frames shaped like the analysed bands, overlap-added.
        const auto noiseGain = std::pow (2.0f, g.noise * 2.0f);
        const auto n = r.fftSize;
        const auto hop = n / 4;
        const auto bins = n / 2 + 1;
        const auto binHz = (float) (sr / n);
        juce::dsp::FFT fft ((int) std::round (std::log2 ((double) n)));
        std::vector<float> window ((size_t) n), data ((size_t) n * 2);
        for (int i = 0; i < n; ++i)
            window[(size_t) i] = 0.5f - 0.5f * std::cos (twoPi * (float) i / (float) n);


        std::vector<float> binGain ((size_t) bins, 0.0f);
        std::vector<int> binBand ((size_t) bins, -1);
        for (int b = 1; b < bins; ++b)
        {
            const auto frequency = (float) b * binHz;
            const auto colourFrequency = frequency / formantWarp;
            for (int band = 0; band < r.numBands; ++band)
                if (colourFrequency >= r.bandEdges[(size_t) band] && colourFrequency < r.bandEdges[(size_t) band + 1])
                    binBand[(size_t) b] = band;

            auto gain = noiseGain * std::pow (juce::jmax (1.0f, frequency / f0), g.tilt * 0.5f);
            if (g.body > 0.0f)
            {
                const auto octaves = std::log2 (juce::jmax (1.0f, frequency) / resonance) / 0.3f;
                gain *= 1.0f + g.body * 3.0f * std::exp (-0.5f * octaves * octaves);
            }
            binGain[(size_t) b] = gain;
        }

        for (int channel = 0; channel < out.getNumChannels(); ++channel)
        {
            Rng noiseRng (noiseSeed + 977u * (std::uint32_t) (channel + 1));
            auto* dest = out.getWritePointer (channel);

            for (int centre = 0; centre < length + n / 2; centre += hop)
            {
                const auto position = framePosition (juce::jlimit (0, length - 1, centre));
                const auto t0 = (int) position;
                const auto t1 = juce::jmin (t0 + 1, r.numFrames - 1);
                const auto ft = position - (float) t0;
                const auto seconds = (float) centre / (float) sr;
                const auto attackTime = g.attack > 0.0f ? g.attack * g.attack * 0.8f : 0.0f;
                auto envelope = 1.0f;
                if (attackTime > 0.0f)
                    envelope *= juce::jmin (1.0f, seconds / attackTime);
                if (g.decay < 0.0f)
                    envelope *= std::exp (-juce::jmax (0.0f, seconds - attackTime) * -g.decay * 6.0f);

                std::fill (data.begin(), data.end(), 0.0f);
                for (int b = 1; b < bins - 1; ++b)
                {
                    const auto band = binBand[(size_t) b];
                    if (band < 0)
                        continue;
                    const auto level = r.noiseLevel (t0, band) * (1.0f - ft) + r.noiseLevel (t1, band) * ft;
                    const auto magnitude = level * binGain[(size_t) b] * envelope;
                    const auto phase = noiseRng.uniform() * twoPi;
                    data[(size_t) (2 * b)] = magnitude * std::cos (phase);
                    data[(size_t) (2 * b + 1)] = magnitude * std::sin (phase);
                }

                fft.performRealOnlyInverseTransform (data.data());

                const auto start = centre - n / 2;
                for (int i = 0; i < n; ++i)
                {
                    const auto s = start + i;
                    if (s >= 0 && s < length)
                        dest[s] += data[(size_t) i] * window[(size_t) i] * noiseCalibration;
                }
            }
        }

        // Never louder than full scale.
        const auto peak = out.getMagnitude (0, length);
        if (peak > 0.95f)
            out.applyGain (0.95f / peak);
        return out;
    }
}
