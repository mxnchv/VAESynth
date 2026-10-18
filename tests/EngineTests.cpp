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

    std::shared_ptr<vae::SampleSource> analyse (const juce::String& name, juce::AudioBuffer<float> audio, int slot)
    {
        auto source = vae::analyseAudio (name, std::move (audio), sampleRate);
        source->slot = slot;
        return source;
    }

    double frequency (double midiNote) { return 440.0 * std::pow (2.0, (midiNote - 69.0) / 12.0); }

    struct RenderStats
    {
        float rms = 0.0f, peak = 0.0f;
        bool finite = true;
        int zeroCrossings = 0;
        int measuredSamples = 0;
    };

    // Renders blocks; `perBlock` may add MIDI and adjust settings before each one.
    // Zero crossings are counted after `skipSamples` to measure pitch.
    RenderStats render (vae::GrainEngine& engine, vae::EngineSettings settings, int numBlocks,
                        std::function<void (int, juce::MidiBuffer&, vae::EngineSettings&)> perBlock = {},
                        int samplesPerBlock = blockSize, int skipSamples = 0)
    {
        RenderStats stats;
        double sumSquares = 0.0;
        juce::AudioBuffer<float> out (2, samplesPerBlock);
        float previous = 0.0f;

        for (int block = 0; block < numBlocks; ++block)
        {
            out.clear();
            juce::MidiBuffer midi;
            if (perBlock)
                perBlock (block, midi, settings);

            engine.process (out, midi, settings);

            for (int i = 0; i < samplesPerBlock; ++i)
            {
                for (int ch = 0; ch < 2; ++ch)
                {
                    const auto s = out.getSample (ch, i);
                    stats.finite = stats.finite && std::isfinite (s);
                    stats.peak = juce::jmax (stats.peak, std::abs (s));
                    sumSquares += (double) s * s;
                }

                const auto s = out.getSample (0, i);
                if (block * samplesPerBlock + i >= skipSamples)
                {
                    if ((previous < 0.0f) != (s < 0.0f))
                        ++stats.zeroCrossings;
                    ++stats.measuredSamples;
                }
                previous = s;
            }
        }

        stats.rms = (float) std::sqrt (sumSquares / juce::jmax (1, numBlocks * samplesPerBlock * 2));
        return stats;
    }

    double measuredFrequency (const RenderStats& stats)
    {
        return stats.zeroCrossings * 0.5 * sampleRate / juce::jmax (1, stats.measuredSamples);
    }

    void noteOnAtStart (int block, juce::MidiBuffer& midi, int note, float velocity = 0.8f)
    {
        if (block == 0)
            midi.addEvent (juce::MidiMessage::noteOn (1, note, velocity), 0);
    }

    // Direction that points at the middle of a source's grains.
    vae::Vec3 centreOf (const vae::GrainMap& map, int sourceIndex)
    {
        vae::Vec3 sum;
        for (const auto& g : map.getGrains())
            if (g.source == sourceIndex)
                sum = sum + g.position;
        return sum.normalised();
    }
}

int main()
{
    // --- Analysis --------------------------------------------------------
    auto toneThenNoise = analyse ("tone-noise", concatenate (sine (440.0, 1.0), noise (1.0)), 0);
    auto bass = analyse ("bass", sine (55.0, 1.0), 1);

    juce::AudioBuffer<float> silentBuffer (1, (int) sampleRate);
    silentBuffer.clear();
    auto silence = analyse ("silence", silentBuffer, 2);

    check (toneThenNoise->grains.size() > 30, "analysis finds grains in a 2 s sample");
    check (silence->grains.empty(), "silent audio produces no grains");

    int pitchedRight = 0, tonalGrains = 0, unpitchedNoise = 0, noiseGrains = 0;
    for (const auto& g : toneThenNoise->grains)
    {
        if (g.timeNorm < 0.4f)
        {
            ++tonalGrains;
            pitchedRight += std::abs (g.pitch - 69.0f) < 0.3f ? 1 : 0;
        }
        else if (g.timeNorm > 0.6f)
        {
            ++noiseGrains;
            unpitchedNoise += g.pitch < 0.0f ? 1 : 0;
        }
    }
    check (pitchedRight >= tonalGrains * 9 / 10, "grains of a 440 Hz tone are detected as A4 (" + juce::String (pitchedRight) + "/" + juce::String (tonalGrains) + ")");
    check (unpitchedNoise >= noiseGrains * 8 / 10, "noise grains are detected as unpitched (" + juce::String (unpitchedNoise) + "/" + juce::String (noiseGrains) + ")");
    check (toneThenNoise->grains.front().chroma[9] > 0.8f, "a 440 Hz grain's chroma peaks on A");

    auto pureA = analyse ("a4", sine (440.0, 1.0), 3);
    check (pureA->detectedRoot == 69, "the root note of a 440 Hz sample is detected as A4");
    check (analyse ("noise", noise (1.0), 4)->detectedRoot == -1, "noise has no detected root");
    check (bass->detectedRoot == 33, "a 55 Hz sample is detected as A1 (" + juce::String (bass->detectedRoot) + ")");

    // --- Scales ----------------------------------------------------------
    const auto cMajor = vae::Scale::fromIndices (0, 1);
    const auto aMinorPentatonic = vae::Scale::fromIndices (9, 9);
    check (cMajor.snap (61.0f) == 60 && cMajor.snap (66.0f) == 65 && cMajor.snap (64.4f) == 64, "notes snap to C major, ties going down");
    check (aMinorPentatonic.snap (70.0f) == 69 && aMinorPentatonic.snap (71.0f) == 72 && aMinorPentatonic.snap (73.0f) == 72,
           "notes snap to A minor pentatonic");
    check (! vae::Scale::fromIndices (0, 0).active && vae::Scale::fromIndices (0, 0).snap (61.3f) == 61, "scale off leaves notes alone");

    // --- Map -------------------------------------------------------------
    auto map = vae::GrainMap::build ({ toneThenNoise, bass }, nullptr);
    const auto& grains = map->getGrains();

    check (map->getProjection().valid, "projection is fitted");
    check (grains.size() == toneThenNoise->grains.size() + bass->grains.size(), "every grain is placed");

    bool onSphere = true;
    for (const auto& g : grains)
        onSphere = onSphere && std::abs (g.position.length() - 1.0f) < 1.0e-3f;
    check (onSphere, "grains sit on the unit sphere");

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

    auto extended = vae::GrainMap::build ({ toneThenNoise, bass, analyse ("noise", noise (1.0), 2) }, &map->getProjection());
    check ((extended->getGrains()[0].position - grains[0].position).length() < 1.0e-6f,
           "a frozen map keeps existing grains in place when samples are added");

    auto empty = vae::GrainMap::build ({}, nullptr);
    check (empty->getGrains().empty(), "an empty library builds an empty map");

    // --- Engine ----------------------------------------------------------
    vae::SampleSlots slots;
    vae::GrainEngine engine;
    engine.prepare (sampleRate, blockSize);
    engine.setSampleSlots (&slots);
    engine.setMap (map.get());

    vae::EngineSettings settings;
    const auto blocksPerSecond = (int) (sampleRate / blockSize);

    auto held = render (engine, settings, blocksPerSecond, [] (int b, juce::MidiBuffer& m, auto&) { noteOnAtStart (b, m, 60); });
    check (held.rms > 1.0e-3f, "a held note makes sound (rms " + juce::String (held.rms, 4) + ")");
    check (held.finite && held.peak < 4.0f, "output is finite and bounded (peak " + juce::String (held.peak, 2) + ")");

    render (engine, settings, 1, [] (int, juce::MidiBuffer& m, auto&) { m.addEvent (juce::MidiMessage::noteOff (1, 60), 0); });
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
    extreme.pitchMode = vae::PitchMode::texture;
    extreme.harmonicFilter = 1.0f;
    extreme.scale = cMajor;
    extreme.radius = -1.5f;

    auto chord = render (engine, extreme, blocksPerSecond, [] (int block, juce::MidiBuffer& midi, auto&)
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

    // --- Pitch modes -----------------------------------------------------
    // One 440 Hz sample whose root is (wrongly) set to C3: Transpose trusts the
    // root, Tune trusts each grain's detected pitch.
    auto toneMap = vae::GrainMap::build ({ pureA }, nullptr);
    slots.reset (pureA->slot, 60);

    auto pitchSettings = settings;
    pitchSettings.detuneCents = 0.0f;
    pitchSettings.spray = 0.0f;
    pitchSettings.velocityToRadius = 0.0f;
    pitchSettings.width = 0.0f;
    pitchSettings.grainSizeMs = 120.0f;
    pitchSettings.direction = centreOf (*toneMap, 0);

    auto playedFrequency = [&] (vae::PitchMode mode, int note)
    {
        engine.reset();
        engine.setMap (toneMap.get());
        pitchSettings.pitchMode = mode;
        const auto stats = render (engine, pitchSettings, blocksPerSecond, [note] (int b, juce::MidiBuffer& m, auto&) { noteOnAtStart (b, m, note); },
                                   blockSize, (int) (0.2 * sampleRate));
        return measuredFrequency (stats);
    };

    const auto tuned = playedFrequency (vae::PitchMode::tune, 72);
    check (std::abs (tuned / frequency (72) - 1.0) < 0.03, "Tune plays C5 for a C5 key whatever the root (" + juce::String (tuned, 1) + " Hz)");

    const auto transposed = playedFrequency (vae::PitchMode::transpose, 72);
    check (std::abs (transposed / 880.0 - 1.0) < 0.03, "Transpose shifts from the root: +12 from C3 doubles A4 (" + juce::String (transposed, 1) + " Hz)");

    const auto texture = playedFrequency (vae::PitchMode::texture, 50);
    check (std::abs (texture / 440.0 - 1.0) < 0.03, "Texture keeps the grain's own pitch (" + juce::String (texture, 1) + " Hz)");

    pitchSettings.scale = cMajor;
    const auto snapped = playedFrequency (vae::PitchMode::tune, 73);
    check (std::abs (snapped / frequency (72) - 1.0) < 0.03, "with C major, a C#5 key plays C5 (" + juce::String (snapped, 1) + " Hz)");
    pitchSettings.scale = {};

    // --- Mute / solo -----------------------------------------------------
    engine.reset();
    engine.setMap (map.get());
    auto spawnedWith = [&] (std::function<void()> setup)
    {
        slots.reset (0);
        slots.reset (1);
        setup();
        const auto before0 = engine.getGrainsSpawned (0), before1 = engine.getGrainsSpawned (1);
        auto s = settings;
        s.radius = 0.0f;  // the centre draws from every sample
        render (engine, s, blocksPerSecond / 2, [] (int b, juce::MidiBuffer& m, auto&) { noteOnAtStart (b, m, 60); });
        render (engine, s, 1, [] (int, juce::MidiBuffer& m, auto&) { m.addEvent (juce::MidiMessage::allNotesOff (1), 0); });
        return std::make_pair (engine.getGrainsSpawned (0) - before0, engine.getGrainsSpawned (1) - before1);
    };

    const auto withMute = spawnedWith ([&] { slots.muted[0].store (true); });
    check (withMute.first == 0 && withMute.second > 0, "a muted sample is never played");

    const auto withSolo = spawnedWith ([&] { slots.soloed[0].store (true); });
    check (withSolo.first > 0 && withSolo.second == 0, "a soloed sample is the only one played");
    slots.reset (0);
    slots.reset (1);

    // --- Tempo sync ------------------------------------------------------
    // 120 BPM at 48 kHz: a beat is 24000 samples; 1/8 notes over 2 seconds (4 beats).
    auto countGrains = [&] (bool synced, bool playing)
    {
        engine.reset();
        auto s = settings;
        s.clock = { synced, playing, 0.0, 24000.0, 0.5 };
        s.density = 24.0f;
        const auto before = engine.getGrainsSpawned (0) + engine.getGrainsSpawned (1);
        render (engine, s, (int) (2.0 * sampleRate / blockSize), [] (int b, juce::MidiBuffer& m, vae::EngineSettings& st)
        {
            noteOnAtStart (b, m, 60);
            st.clock.ppqAtBlockStart = b * (double) blockSize / st.clock.samplesPerBeat;
        });
        return engine.getGrainsSpawned (0) + engine.getGrainsSpawned (1) - before;
    };

    const auto onGrid = countGrains (true, true);
    check (onGrid == 8, "synced to the host, 1/8 notes give 8 grains in 4 beats (" + juce::String (onGrid) + ")");
    const auto stopped = countGrains (true, false);
    check (stopped == 8, "synced with the transport stopped, the tempo still sets the rate (" + juce::String (stopped) + ")");
    const auto freeRunning = countGrains (false, false);
    check (std::abs (freeRunning - 48) <= 6, "free mode follows the density in grains per second (" + juce::String (freeRunning) + ")");

    // --- Harmonic filter -------------------------------------------------
    // Two near-identical sines, C4 and C#4, played untransposed: with C major
    // and the filter at full, the out-of-key one is almost never chosen.
    auto cNote = analyse ("c4", sine (frequency (60), 1.0), 5);
    auto cSharp = analyse ("c#4", sine (frequency (61), 1.0), 6);
    auto pair = vae::GrainMap::build ({ cNote, cSharp }, nullptr);
    engine.reset();
    engine.setMap (pair.get());

    auto harmonicSettings = settings;
    harmonicSettings.pitchMode = vae::PitchMode::transpose;  // roots are C4: key 60 plays both as recorded
    harmonicSettings.scale = cMajor;
    harmonicSettings.neighbours = 16;
    harmonicSettings.spray = 0.0f;
    harmonicSettings.direction = (centreOf (*pair, 0) + centreOf (*pair, 1)).normalised();

    auto shareOfC = [&] (float filter)
    {
        harmonicSettings.harmonicFilter = filter;
        const auto c0 = engine.getGrainsSpawned (5), s0 = engine.getGrainsSpawned (6);
        render (engine, harmonicSettings, blocksPerSecond, [] (int b, juce::MidiBuffer& m, auto&) { noteOnAtStart (b, m, 60); });
        render (engine, harmonicSettings, 1, [] (int, juce::MidiBuffer& m, auto&) { m.addEvent (juce::MidiMessage::allNotesOff (1), 0); });
        const auto c = engine.getGrainsSpawned (5) - c0, s = engine.getGrainsSpawned (6) - s0;
        return (double) c / juce::jmax (1, c + s);
    };

    const auto withoutFilter = shareOfC (0.0f);
    const auto withFilter = shareOfC (1.0f);
    check (withFilter > 0.9 && withFilter > withoutFilter + 0.2,
           "the harmonic filter prefers in-key grains (" + juce::String (withoutFilter, 2) + " -> " + juce::String (withFilter, 2) + ")");

    // --- Audition ----------------------------------------------------------
    engine.reset();
    engine.setMap (map.get());
    engine.readPlayedGrains ([] (int) {});
    engine.audition (3, settings);
    auto auditioned = render (engine, settings, 8);
    std::vector<int> played;
    engine.readPlayedGrains ([&] (int index) { played.push_back (index); });
    check (auditioned.rms > 1.0e-4f && played.size() == 1 && played[0] == 3, "clicking a point plays that grain without a note");

    engine.reset();
    engine.setMap (empty.get());
    auto silent = render (engine, settings, 8, [] (int b, juce::MidiBuffer& m, auto&) { noteOnAtStart (b, m, 60, 1.0f); });
    check (silent.peak <= 0.0f, "an empty map is silent and does not crash");

    std::cout << (failures == 0 ? "\nAll tests passed" : "\nSome tests failed") << std::endl;
    return failures == 0 ? 0 : 1;
}
