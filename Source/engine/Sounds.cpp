#include "Sounds.h"
#include "Traits.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace vae
{
    namespace
    {
        // Fundamental of a window via YIN on a copy decimated to ~11 kHz.
        // Returns a fractional MIDI note, or -1 when there is no clear pitch.
        float detectPitch (const float* samples, int length, double sampleRate,
                           std::vector<float>& decimated, std::vector<float>& difference)
        {
            const auto factor = juce::jmax (1, (int) std::round (sampleRate / 11025.0));
            const auto rate = sampleRate / factor;
            const auto n = length / factor;

            decimated.resize ((size_t) juce::jmax (0, n));
            for (int i = 0; i < n; ++i)
            {
                float sum = 0.0f;
                for (int k = 0; k < factor; ++k)
                    sum += samples[i * factor + k];
                decimated[(size_t) i] = sum / (float) factor;
            }

            const auto maxLag = juce::jmin (n / 2, (int) (rate / 50.0));
            const auto minLag = juce::jmax (2, (int) (rate / 1500.0));
            const auto span = n - maxLag;

            if (span < 32 || maxLag <= minLag + 2)
                return -1.0f;

            difference.assign ((size_t) maxLag + 2, 1.0f);
            double running = 0.0;

            for (int lag = 1; lag <= maxLag + 1 && lag + span <= n; ++lag)
            {
                double sum = 0.0;
                for (int j = 0; j < span; ++j)
                {
                    const auto delta = decimated[(size_t) j] - decimated[(size_t) (j + lag)];
                    sum += (double) delta * delta;
                }

                running += sum;
                difference[(size_t) lag] = running > 0.0 ? (float) (sum * lag / running) : 1.0f;
            }

            int best = -1;
            for (int lag = minLag; lag < maxLag; ++lag)
            {
                if (difference[(size_t) lag] < 0.15f)
                {
                    while (lag + 1 < maxLag && difference[(size_t) (lag + 1)] < difference[(size_t) lag])
                        ++lag;
                    best = lag;
                    break;
                }
            }

            if (best < 0)
            {
                best = minLag;
                for (int lag = minLag; lag < maxLag; ++lag)
                    if (difference[(size_t) lag] < difference[(size_t) best])
                        best = lag;

                if (difference[(size_t) best] > 0.3f)
                    return -1.0f;
            }

            auto refined = (double) best;
            const auto a = difference[(size_t) (best - 1)], b = difference[(size_t) best], c = difference[(size_t) (best + 1)];
            const auto curvature = a - 2.0f * b + c;
            if (curvature > 1.0e-9f)
                refined += 0.5 * (a - c) / curvature;

            const auto frequency = rate / refined;
            return (float) (69.0 + 12.0 * std::log2 (frequency / 440.0));
        }
    }

    std::shared_ptr<SeedSource> makeLibrarySource (int index)
    {
        auto source = std::make_shared<SeedSource>();
        source->kind = SeedSource::Kind::library;
        source->libraryIndex = juce::jlimit (0, (int) library.size() - 1, index);
        source->name = library[(size_t) source->libraryIndex].name;
        source->rootNote = 60;
        source->pitched = true;
        return source;
    }

    std::shared_ptr<SeedSource> loadSampleSource (juce::AudioFormatManager& formats,
                                                  const juce::File& file,
                                                  juce::String& error)
    {
        std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file));

        if (reader == nullptr)
        {
            error = "Unsupported or unreadable file: " + file.getFileName();
            return nullptr;
        }

        const auto length = (int) std::min (reader->lengthInSamples, (juce::int64) (maxSeedSeconds * reader->sampleRate));
        const auto channels = juce::jlimit (1, 2, (int) reader->numChannels);

        if (length < (int) (0.02 * reader->sampleRate))
        {
            error = file.getFileName() + " is too short";
            return nullptr;
        }

        auto source = std::make_shared<SeedSource>();
        source->kind = SeedSource::Kind::sample;
        source->name = file.getFileNameWithoutExtension();
        source->file = file;
        source->sampleRate = reader->sampleRate;
        source->audio.setSize (channels, length);
        reader->read (&source->audio, 0, length, 0, true, channels > 1);

        if (source->audio.getMagnitude (0, length) < 1.0e-4f)
        {
            error = file.getFileName() + " is silent";
            return nullptr;
        }

        const auto root = detectRootNote (source->audio, source->sampleRate);
        source->pitched = root >= 0;
        source->rootNote = root >= 0 ? root : 60;
        return source;
    }

    int detectRootNote (const juce::AudioBuffer<float>& audio, double sampleRate)
    {
        const auto length = audio.getNumSamples();
        std::vector<float> mono ((size_t) length, 0.0f);
        for (int ch = 0; ch < audio.getNumChannels(); ++ch)
            for (int i = 0; i < length; ++i)
                mono[(size_t) i] += audio.getSample (ch, i) / (float) audio.getNumChannels();

        const auto window = (int) (0.1 * sampleRate);
        const auto hop = window / 2;
        const auto lastStart = juce::jmin (length - window, (int) (3.0 * sampleRate));

        std::vector<float> decimated, difference, pitches;
        int windows = 0;

        for (int start = 0; start <= lastStart; start += hop)
        {
            double energy = 0.0;
            for (int i = 0; i < window; ++i)
                energy += (double) mono[(size_t) (start + i)] * mono[(size_t) (start + i)];

            const auto rmsDb = juce::Decibels::gainToDecibels ((float) std::sqrt (energy / window));
            if (rmsDb < -45.0f)
                continue;

            ++windows;
            const auto pitch = detectPitch (mono.data() + start, window, sampleRate, decimated, difference);
            if (pitch >= 0.0f)
                pitches.push_back (pitch);
        }

        if (windows == 0 || (float) pitches.size() < 0.3f * (float) windows)
            return -1;

        std::nth_element (pitches.begin(), pitches.begin() + (std::ptrdiff_t) (pitches.size() / 2), pitches.end());
        return juce::jlimit (0, 127, (int) std::round (pitches[pitches.size() / 2]));
    }
}
