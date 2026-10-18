// Headless checks for the analysis, the map and the granular engine.
// Run by CI on macOS and Windows; exits non-zero on failure.

#include "../Source/engine/GrainEngine.h"
#include "../Source/engine/GrainMap.h"

#include <iostream>

namespace
{
    constexpr double sampleRate = 48000.0;
    constexpr int blockSize = 512;
    int failures = 0;

    void check (bool ok, const juce::String& what)
    {
        std::cout << (ok ? "PASS  " : "FAIL  ") << what << std::endl;
        failures += ok ? 0 : 1;
    }

    juce::AudioBuffer<float> sine (double frequency, double seconds, float level = 0.5f)
    {
        juce::AudioBuffer<float> buffer (1, (int) (seconds * sampleRate));
        for (int i = 0; i < buffer.getNumSamples(); ++i)
            buffer.setSample (0, i, level * (float) std::sin (juce::MathConstants<double>::twoPi * frequency * i / sampleRate));
        return buffer;
    }

    juce::AudioBuffer<float> noise (double seconds, float level = 0.3f)
    {
        juce::Random random (42);
        juce::AudioBuffer<float> buffer (1, (int) (seconds * sampleRate));
        for (int i = 0; i < buffer.getNumSamples(); ++i)
            buffer.setSample (0, i, level * (random.nextFloat() * 2.0f - 1.0f));
        return buffer;
    }

    juce::AudioBuffer<float> concatenate (const juce::AudioBuffer<float>& a, const juce::AudioBuffer<float>& b)
    {
        juce::AudioBuffer<float> result (1, a.getNumSamples() + b.getNumSamples());
        result.copyFrom (0, 0, a, 0, 0, a.getNumSamples());
        result.copyFrom (0, a.getNumSamples(), b, 0, 0, b.getNumSamples());
        return result;
    }

    struct RenderStats
    {
        float rms = 0.0f, peak = 0.0f;
        bool finite = true;
    };

    RenderStats render (vae::GrainEngine& engine, const vae::EngineSettings& settings, int numBlocks,
                        std::function<void (int, juce::MidiBuffer&)> events = {}, int samplesPerBlock = blockSize)
    {
        RenderStats stats;
        double sumSquares = 0.0;
        juce::AudioBuffer<float> out (2, samplesPerBlock);

        for (int block = 0; block < numBlocks; ++block)
        {
            out.clear();
            juce::MidiBuffer midi;
            if (events)
                events (block, midi);

            engine.process (out, midi, settings);

            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < samplesPerBlock; ++i)
                {
                    const auto s = out.getSample (ch, i);
                    stats.finite = stats.finite && std::isfinite (s);
                    stats.peak = juce::jmax (stats.peak, std::abs (s));
                    sumSquares += (double) s * s;
                }
        }

        stats.rms = (float) std::sqrt (sumSquares / juce::jmax (1, numBlocks * samplesPerBlock * 2));
        return stats;
    }
}

int main()
{
    // --- Analysis --------------------------------------------------------
    auto toneThenNoise = vae::analyseAudio ("tone-noise", concatenate (sine (440.0, 1.0), noise (1.0)), sampleRate);
    auto bass = vae::analyseAudio ("bass", sine (55.0, 1.0), sampleRate);
    juce::AudioBuffer<float> silentBuffer (1, (int) sampleRate);
    silentBuffer.clear();
    auto silence = vae::analyseAudio ("silence", silentBuffer, sampleRate);

    check (toneThenNoise->grains.size() > 30, "analysis finds grains in a 2 s sample");
    check (silence->grains.empty(), "silent audio produces no grains");

    // --- Map -------------------------------------------------------------
    auto map = vae::GrainMap::build ({ toneThenNoise, bass }, nullptr);
    const auto& grains = map->getGrains();

    check (map->getProjection().valid, "projection is fitted");
    check (grains.size() == toneThenNoise->grains.size() + bass->grains.size(), "every grain is placed");

    bool onSphere = true;
    for (const auto& g : grains)
        onSphere = onSphere && std::abs (g.position.length() - 1.0f) < 1.0e-3f;
    check (onSphere, "grains sit on the unit sphere");

    // Tone grains and noise grains should land in clearly different places.
    vae::Vec3 toneCentre, noiseCentre;
    int toneCount = 0, noiseCount = 0;
    for (const auto& g : grains)
    {
        if (g.source != 0)
            continue;
        if (g.timeNorm < 0.4f) { toneCentre = toneCentre + g.position; ++toneCount; }
        if (g.timeNorm > 0.6f) { noiseCentre = noiseCentre + g.position; ++noiseCount; }
    }
    const auto separation = (toneCentre * (1.0f / (float) juce::jmax (1, toneCount)) - noiseCentre * (1.0f / (float) juce::jmax (1, noiseCount))).length();
    check (separation > 0.5f, "tonal and noisy grains are far apart (" + juce::String (separation, 2) + ")");

    const auto restored = vae::Projection::fromValueTree (map->getProjection().toValueTree());
    check (restored.valid && (restored.project (toneThenNoise->grains[0].features) - grains[0].position).length() < 1.0e-3f,
           "projection survives a save/load round trip");

    auto extended = vae::GrainMap::build ({ toneThenNoise, bass, vae::analyseAudio ("noise", noise (1.0), sampleRate) },
                                          &map->getProjection());
    check ((extended->getGrains()[0].position - grains[0].position).length() < 1.0e-6f,
           "a frozen map keeps existing grains in place when samples are added");

    auto empty = vae::GrainMap::build ({}, nullptr);
    check (empty->getGrains().empty(), "an empty library builds an empty map");

    // --- Engine ----------------------------------------------------------
    std::array<std::atomic<int>, vae::GrainEngine::maxSources> roots;
    for (auto& r : roots)
        r.store (60);

    vae::GrainEngine engine;
    engine.prepare (sampleRate, blockSize);
    engine.setRootNotes (&roots);
    engine.setMap (map.get());

    vae::EngineSettings settings;
    const auto blocksPerSecond = (int) (sampleRate / blockSize);

    auto held = render (engine, settings, blocksPerSecond, [] (int block, juce::MidiBuffer& midi)
    {
        if (block == 0)
            midi.addEvent (juce::MidiMessage::noteOn (1, 60, 0.8f), 0);
    });
    check (held.rms > 1.0e-3f, "a held note makes sound (rms " + juce::String (held.rms, 4) + ")");
    check (held.finite && held.peak < 4.0f, "output is finite and bounded (peak " + juce::String (held.peak, 2) + ")");

    render (engine, settings, 1, [] (int, juce::MidiBuffer& midi) { midi.addEvent (juce::MidiMessage::noteOff (1, 60), 0); });
    render (engine, settings, blocksPerSecond * 2);
    auto tail = render (engine, settings, 4);
    check (tail.rms < 1.0e-6f && engine.getNumActiveGrains() == 0, "sound stops after note off and release");

    // Extremes: long dense grains, full chord, texture mode, centre and far antipode.
    auto extreme = settings;
    extreme.grainSizeMs = 500.0f;
    extreme.density = 200.0f;
    extreme.neighbours = 16;
    extreme.spray = 1.0f;
    extreme.shape = 1.0f;
    extreme.transpose = false;
    extreme.radius = -1.5f;

    auto chord = render (engine, extreme, blocksPerSecond, [] (int block, juce::MidiBuffer& midi)
    {
        if (block == 0)
            for (int note = 36; note < 36 + 10 * 7; note += 7)
                midi.addEvent (juce::MidiMessage::noteOn (1, note, 1.0f), 0);
    });
    check (chord.finite && chord.peak < 8.0f, "extreme settings with a 10-note chord stay finite (peak " + juce::String (chord.peak, 2) + ")");
    check (engine.getNumActiveGrains() <= vae::GrainEngine::maxGrains, "grain pool is never exceeded");

    extreme.radius = 0.0f;
    auto centre = render (engine, extreme, 8);
    check (centre.finite && centre.rms > 1.0e-4f, "the centre of the sphere still sounds");

    auto oversized = render (engine, settings, 4, {}, blockSize * 4);
    check (oversized.finite, "blocks larger than announced are handled");

    engine.reset();
    engine.setMap (empty.get());
    auto silent = render (engine, settings, 8, [] (int block, juce::MidiBuffer& midi)
    {
        if (block == 0)
            midi.addEvent (juce::MidiMessage::noteOn (1, 60, 1.0f), 0);
    });
    check (silent.peak <= 0.0f, "an empty map is silent and does not crash");

    std::cout << (failures == 0 ? "\nAll tests passed" : "\nSome tests failed") << std::endl;
    return failures == 0 ? 0 : 1;
}
