#include "GrainMap.h"

#include <juce_dsp/juce_dsp.h>

#include <algorithm>
#include <numeric>

namespace vae
{
    namespace
    {
        constexpr double analysisWindowSeconds = 0.1;
        constexpr int fftOrder = 12;
        constexpr int fftSize = 1 << fftOrder;
        constexpr float silenceDb = -60.0f;
        constexpr int maxGrainsPerSample = 1000;

        // Eigen-decomposition of a symmetric matrix (cyclic Jacobi).
        // Returns eigenvalues; eigenvectors are the columns of `vectors`.
        using Matrix = std::array<std::array<double, numFeatures>, numFeatures>;

        std::array<double, numFeatures> jacobiEigen (Matrix a, Matrix& vectors)
        {
            constexpr int n = numFeatures;

            for (int i = 0; i < n; ++i)
                for (int j = 0; j < n; ++j)
                    vectors[(size_t) i][(size_t) j] = i == j ? 1.0 : 0.0;

            for (int sweep = 0; sweep < 100; ++sweep)
            {
                double offDiagonal = 0.0;
                for (int i = 0; i < n; ++i)
                    for (int j = i + 1; j < n; ++j)
                        offDiagonal += a[(size_t) i][(size_t) j] * a[(size_t) i][(size_t) j];

                if (offDiagonal < 1.0e-18)
                    break;

                for (int p = 0; p < n; ++p)
                {
                    for (int q = p + 1; q < n; ++q)
                    {
                        const auto apq = a[(size_t) p][(size_t) q];
                        if (std::abs (apq) < 1.0e-15)
                            continue;

                        const auto theta = (a[(size_t) q][(size_t) q] - a[(size_t) p][(size_t) p]) / (2.0 * apq);
                        const auto t = (theta >= 0.0 ? 1.0 : -1.0) / (std::abs (theta) + std::sqrt (theta * theta + 1.0));
                        const auto c = 1.0 / std::sqrt (t * t + 1.0);
                        const auto s = t * c;

                        for (int k = 0; k < n; ++k)
                        {
                            const auto akp = a[(size_t) k][(size_t) p], akq = a[(size_t) k][(size_t) q];
                            a[(size_t) k][(size_t) p] = c * akp - s * akq;
                            a[(size_t) k][(size_t) q] = s * akp + c * akq;
                        }

                        for (int k = 0; k < n; ++k)
                        {
                            const auto apk = a[(size_t) p][(size_t) k], aqk = a[(size_t) q][(size_t) k];
                            a[(size_t) p][(size_t) k] = c * apk - s * aqk;
                            a[(size_t) q][(size_t) k] = s * apk + c * aqk;
                        }

                        for (int k = 0; k < n; ++k)
                        {
                            const auto vkp = vectors[(size_t) k][(size_t) p], vkq = vectors[(size_t) k][(size_t) q];
                            vectors[(size_t) k][(size_t) p] = c * vkp - s * vkq;
                            vectors[(size_t) k][(size_t) q] = s * vkp + c * vkq;
                        }
                    }
                }
            }

            std::array<double, numFeatures> values {};
            for (int i = 0; i < n; ++i)
                values[(size_t) i] = a[(size_t) i][(size_t) i];

            return values;
        }

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

        FeatureVector measureWindow (const float* samples, int length, double sampleRate,
                                     juce::dsp::FFT& fft, const std::vector<float>& window,
                                     std::vector<float>& fftData, float& rmsDb, Chroma& chroma)
        {
            double sumSquares = 0.0;
            int zeroCrossings = 0;

            for (int i = 0; i < length; ++i)
            {
                sumSquares += (double) samples[i] * samples[i];
                if (i > 0 && (samples[i - 1] < 0.0f) != (samples[i] < 0.0f))
                    ++zeroCrossings;
            }

            rmsDb = juce::Decibels::gainToDecibels ((float) std::sqrt (sumSquares / juce::jmax (1, length)), -120.0f);

            std::fill (fftData.begin(), fftData.end(), 0.0f);
            const auto used = juce::jmin (length, fftSize);
            for (int i = 0; i < used; ++i)
                fftData[(size_t) i] = samples[i] * window[(size_t) (i * fftSize / juce::jmax (1, used))];

            fft.performFrequencyOnlyForwardTransform (fftData.data(), true);

            constexpr int bins = fftSize / 2;
            const auto binHz = sampleRate / fftSize;
            double magSum = 0.0, weightedSum = 0.0, logSum = 0.0;

            for (int b = 1; b < bins; ++b)
            {
                const auto mag = (double) fftData[(size_t) b] + 1.0e-9;
                magSum += mag;
                weightedSum += mag * b * binHz;
                logSum += std::log (mag);
            }

            // Chroma from spectral peaks only, each at its interpolated frequency:
            // whole bins smear a low note over its neighbouring semitones.
            chroma.fill (0.0f);
            float chromaTotal = 0.0f, peakLevel = 0.0f;
            for (int b = 1; b < bins; ++b)
                peakLevel = juce::jmax (peakLevel, fftData[(size_t) b]);

            for (int b = 2; b < bins - 1; ++b)
            {
                const auto m = fftData[(size_t) b];
                if (m <= fftData[(size_t) (b - 1)] || m < fftData[(size_t) (b + 1)] || m < peakLevel * 0.01f)
                    continue;

                const auto a = std::log (fftData[(size_t) (b - 1)] + 1.0e-12f), c = std::log (fftData[(size_t) (b + 1)] + 1.0e-12f);
                const auto centre = std::log (m + 1.0e-12f);
                const auto curvature = a - 2.0f * centre + c;
                const auto offset = curvature < -1.0e-9f ? 0.5f * (a - c) / curvature : 0.0f;
                const auto frequency = (b + juce::jlimit (-0.5f, 0.5f, offset)) * binHz;

                if (frequency < 55.0 || frequency > 5000.0)
                    continue;

                const auto note = (int) std::round (69.0 + 12.0 * std::log2 (frequency / 440.0));
                const auto energy = m * m;
                chroma[(size_t) (((note % 12) + 12) % 12)] += energy;
                chromaTotal += energy;
            }

            if (chromaTotal > 0.0f)
                for (auto& c : chroma)
                    c /= chromaTotal;

            const auto centroid = magSum > 0.0 ? weightedSum / magSum : 0.0;
            const auto flatness = std::exp (logSum / (bins - 1)) / (magSum / (bins - 1));

            double cumulative = 0.0, rolloff = 0.0;
            for (int b = 1; b < bins; ++b)
            {
                cumulative += (double) fftData[(size_t) b] + 1.0e-9;
                if (cumulative >= 0.85 * magSum)
                {
                    rolloff = b * binHz;
                    break;
                }
            }

            return { rmsDb,
                     (float) std::log2 (centroid + 1.0),
                     (float) flatness,
                     (float) std::log2 (rolloff + 1.0),
                     (float) zeroCrossings / (float) juce::jmax (1, length) };
        }
    }

    std::shared_ptr<SampleSource> analyseAudio (const juce::String& name,
                                                juce::AudioBuffer<float> mono,
                                                double sampleRate)
    {
        auto source = std::make_shared<SampleSource>();
        source->name = name;
        source->sampleRate = sampleRate;
        source->audio = std::move (mono);

        const auto windowLength = juce::jmax (64, (int) std::round (analysisWindowSeconds * sampleRate));
        const auto numSamples = source->audio.getNumSamples();

        // Long files get a wider hop so no sample floods the map (and the
        // engine's nearest-grain search) with more than ~maxGrainsPerSample.
        const auto hop = juce::jmax (windowLength / 2, (numSamples - windowLength) / maxGrainsPerSample);
        const auto* data = source->audio.getReadPointer (0);

        juce::dsp::FFT fft (fftOrder);
        std::vector<float> fftData ((size_t) fftSize * 2);
        std::vector<float> window ((size_t) fftSize);
        juce::dsp::WindowingFunction<float>::fillWindowingTables (window.data(), (size_t) fftSize,
                                                                  juce::dsp::WindowingFunction<float>::hann, false);

        std::vector<float> decimated, difference;
        std::vector<float> pitches;

        for (int start = 0; start + windowLength <= numSamples; start += hop)
        {
            float rmsDb = 0.0f;
            Chroma chroma {};
            const auto features = measureWindow (data + start, windowLength, sampleRate, fft, window, fftData, rmsDb, chroma);

            if (rmsDb < silenceDb)
                continue;

            const auto pitch = rmsDb > -45.0f ? detectPitch (data + start, windowLength, sampleRate, decimated, difference) : -1.0f;
            if (pitch >= 0.0f)
                pitches.push_back (pitch);

            source->grains.push_back ({ start, (float) start / (float) juce::jmax (1, numSamples - windowLength),
                                        features, pitch, chroma });
        }

        // A sample counts as pitched when at least 30 % of its grains are; its
        // root is then the median grain pitch.
        if (! source->grains.empty() && (float) pitches.size() >= 0.3f * (float) source->grains.size())
        {
            std::nth_element (pitches.begin(), pitches.begin() + (std::ptrdiff_t) (pitches.size() / 2), pitches.end());
            source->detectedRoot = juce::jlimit (0, 127, (int) std::round (pitches[pitches.size() / 2]));
        }

        return source;
    }

    std::shared_ptr<SampleSource> loadAndAnalyse (juce::AudioFormatManager& formats,
                                                  const juce::File& file,
                                                  juce::String& error,
                                                  double maxSeconds)
    {
        std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file));

        if (reader == nullptr)
        {
            error = "Unsupported or unreadable file: " + file.getFileName();
            return nullptr;
        }

        const auto length = (int) std::min (reader->lengthInSamples, (juce::int64) (maxSeconds * reader->sampleRate));
        const auto channels = (int) reader->numChannels;

        juce::AudioBuffer<float> buffer (juce::jmax (1, channels), length);
        reader->read (&buffer, 0, length, 0, true, true);

        juce::AudioBuffer<float> mono (1, length);
        mono.clear();
        for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
            mono.addFrom (0, 0, buffer, ch, 0, length, 1.0f / (float) buffer.getNumChannels());

        auto source = analyseAudio (file.getFileNameWithoutExtension(), std::move (mono), reader->sampleRate);
        source->file = file;

        if (source->grains.empty())
            error = file.getFileName() + " is silent or too short";

        return source;
    }

    Projection Projection::fit (const std::vector<FeatureVector>& data)
    {
        Projection p;

        if (data.size() < 2)
            return p;

        const auto n = (double) data.size();

        for (int f = 0; f < numFeatures; ++f)
        {
            double sum = 0.0, sumSquares = 0.0;
            for (const auto& row : data)
            {
                sum += row[(size_t) f];
                sumSquares += (double) row[(size_t) f] * row[(size_t) f];
            }

            const auto mean = sum / n;
            p.mean[(size_t) f] = (float) mean;
            p.scale[(size_t) f] = (float) juce::jmax (1.0e-6, std::sqrt (juce::jmax (0.0, sumSquares / n - mean * mean)));
        }

        Matrix covariance {};
        for (const auto& row : data)
        {
            FeatureVector z;
            for (int f = 0; f < numFeatures; ++f)
                z[(size_t) f] = (row[(size_t) f] - p.mean[(size_t) f]) / p.scale[(size_t) f];

            for (int i = 0; i < numFeatures; ++i)
                for (int j = 0; j < numFeatures; ++j)
                    covariance[(size_t) i][(size_t) j] += (double) z[(size_t) i] * z[(size_t) j] / n;
        }

        Matrix vectors {};
        const auto values = jacobiEigen (covariance, vectors);

        std::array<int, numFeatures> order {};
        std::iota (order.begin(), order.end(), 0);
        std::sort (order.begin(), order.end(), [&] (int a, int b) { return values[(size_t) a] > values[(size_t) b]; });

        for (int k = 0; k < 3; ++k)
        {
            const auto column = order[(size_t) k];
            FeatureVector axis {};
            int largest = 0;

            for (int f = 0; f < numFeatures; ++f)
            {
                axis[(size_t) f] = (float) vectors[(size_t) f][(size_t) column];
                if (std::abs (axis[(size_t) f]) > std::abs (axis[(size_t) largest]))
                    largest = f;
            }

            // Eigenvector signs are arbitrary; pin them so the same data
            // always produces the same map.
            if (axis[(size_t) largest] < 0.0f)
                for (auto& v : axis)
                    v = -v;

            p.axes[(size_t) k] = axis;
            p.axisGain[(size_t) k] = (float) (1.0 / std::sqrt (juce::jmax (1.0e-6, values[(size_t) column])));
        }

        p.valid = true;
        return p;
    }

    Vec3 Projection::project (const FeatureVector& features) const noexcept
    {
        std::array<float, 3> coords {};

        for (int k = 0; k < 3; ++k)
        {
            float dot = 0.0f;
            for (int f = 0; f < numFeatures; ++f)
                dot += axes[(size_t) k][(size_t) f] * (features[(size_t) f] - mean[(size_t) f]) / scale[(size_t) f];

            coords[(size_t) k] = dot * axisGain[(size_t) k];
        }

        return Vec3 { coords[0], coords[1], coords[2] }.normalised();
    }

    juce::ValueTree Projection::toValueTree() const
    {
        juce::StringArray values;
        auto add = [&] (float v) { values.add (juce::String (v, 7)); };

        for (auto v : mean)  add (v);
        for (auto v : scale) add (v);
        for (const auto& axis : axes)
            for (auto v : axis)
                add (v);
        for (auto v : axisGain) add (v);

        juce::ValueTree tree ("Projection");
        tree.setProperty ("valid", valid, nullptr);
        tree.setProperty ("data", values.joinIntoString (" "), nullptr);
        return tree;
    }

    Projection Projection::fromValueTree (const juce::ValueTree& tree)
    {
        Projection p;

        if (! tree.hasType ("Projection") || ! (bool) tree.getProperty ("valid", false))
            return p;

        juce::StringArray values;
        values.addTokens (tree.getProperty ("data").toString(), " ", {});

        constexpr int expected = numFeatures * 2 + numFeatures * 3 + 3;
        if (values.size() != expected)
            return p;

        int i = 0;
        auto next = [&] { return values[i++].getFloatValue(); };

        for (auto& v : p.mean)  v = next();
        for (auto& v : p.scale) v = next();
        for (auto& axis : p.axes)
            for (auto& v : axis)
                v = next();
        for (auto& v : p.axisGain) v = next();

        p.valid = true;
        return p;
    }

    GrainMap::Ptr GrainMap::build (std::vector<SampleSourcePtr> sources, const Projection* frozenProjection)
    {
        GrainMap::Ptr map (new GrainMap());
        map->sources = std::move (sources);

        if (frozenProjection != nullptr && frozenProjection->valid)
        {
            map->projection = *frozenProjection;
        }
        else
        {
            std::vector<FeatureVector> features;
            for (const auto& source : map->sources)
                for (const auto& grain : source->grains)
                    features.push_back (grain.features);

            map->projection = Projection::fit (features);
        }

        if (! map->projection.valid)
            return map;

        for (int s = 0; s < (int) map->sources.size(); ++s)
        {
            const auto& sourceGrains = map->sources[(size_t) s]->grains;
            for (int g = 0; g < (int) sourceGrains.size(); ++g)
                map->grains.push_back ({ s, g, sourceGrains[(size_t) g].start, sourceGrains[(size_t) g].timeNorm,
                                         map->projection.project (sourceGrains[(size_t) g].features) });
        }

        return map;
    }
}
