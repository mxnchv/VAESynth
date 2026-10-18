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

        FeatureVector measureWindow (const float* samples, int length, double sampleRate,
                                     juce::dsp::FFT& fft, const std::vector<float>& window,
                                     std::vector<float>& fftData, float& rmsDb)
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

        for (int start = 0; start + windowLength <= numSamples; start += hop)
        {
            float rmsDb = 0.0f;
            const auto features = measureWindow (data + start, windowLength, sampleRate, fft, window, fftData, rmsDb);

            if (rmsDb < silenceDb)
                continue;

            source->grains.push_back ({ start, (float) start / (float) juce::jmax (1, numSamples - windowLength), features });
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
            for (const auto& grain : map->sources[(size_t) s]->grains)
                map->grains.push_back ({ s, grain.start, grain.timeNorm, map->projection.project (grain.features) });

        return map;
    }
}
