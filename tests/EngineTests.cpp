// Headless checks for the plant, the variation engine, note detection and
// the sampler. Run by CI on macOS and Windows; exits non-zero on failure.

#include "../Source/engine/Plant.h"
#include "../Source/engine/Sampler.h"
#include "../Source/engine/Scale.h"
#include "../Source/engine/RecipeEngine.h"

#include <juce_dsp/juce_dsp.h>

#include <iostream>

namespace
{
    int failures = 0;

    void check (bool ok, const juce::String& what)
    {
        std::cout << (ok ? "PASS  " : "FAIL  ") << what << std::endl;
        failures += ok ? 0 : 1;
    }

    std::shared_ptr<vae::SeedSource> sineSource (double frequency, double seconds, double rate = 44100.0)
    {
        auto source = std::make_shared<vae::SeedSource>();
        source->kind = vae::SeedSource::Kind::sample;
        source->name = "sine";
        source->sampleRate = rate;
        source->audio.setSize (2, (int) (seconds * rate));
        for (int i = 0; i < source->audio.getNumSamples(); ++i)
        {
            const auto envelope = (float) std::exp (-2.0 * i / rate);
            const auto s = 0.5f * envelope * (float) std::sin (juce::MathConstants<double>::twoPi * frequency * i / rate);
            source->audio.setSample (0, i, s);
            source->audio.setSample (1, i, s * 0.8f);
        }
        source->rootNote = vae::detectRootNote (source->audio, rate);
        source->pitched = source->rootNote >= 0;
        return source;
    }

    struct Stats { float peak = 0.0f; double rms = 0.0; bool finite = true; double seconds = 0.0; };

    Stats measure (const vae::RenderedSound& s)
    {
        Stats st;
        double sum = 0.0;
        for (int ch = 0; ch < s.audio.getNumChannels(); ++ch)
            for (int i = 0; i < s.audio.getNumSamples(); ++i)
            {
                const auto v = s.audio.getSample (ch, i);
                st.finite = st.finite && std::isfinite (v);
                st.peak = juce::jmax (st.peak, std::abs (v));
                sum += (double) v * v;
            }
        st.rms = std::sqrt (sum / juce::jmax (1, s.audio.getNumSamples() * s.audio.getNumChannels()));
        st.seconds = s.audio.getNumSamples() / s.sampleRate;
        return st;
    }

    vae::Traits randomTraits (vae::Rng& rng)
    {
        vae::Traits t {};
        for (auto& v : t) v = rng.uniform();
        return t;
    }

    // A tone with a fixed colour: partials follow a resonance around `formant` Hz.
    juce::AudioBuffer<float> formantTone (double f0, double formant, double seconds, double rate = 44100.0)
    {
        juce::AudioBuffer<float> b (1, (int) (seconds * rate));
        b.clear();
        for (int k = 1; k * f0 < 15000.0; ++k)
        {
            const auto octaves = std::log2 (k * f0 / formant);
            const auto amplitude = 0.25 * std::exp (-0.5 * octaves * octaves / 0.25) + 0.01 / k;
            for (int i = 0; i < b.getNumSamples(); ++i)
                b.addSample (0, i, (float) (amplitude * std::sin (juce::MathConstants<double>::twoPi * k * f0 * i / rate) * std::exp (-1.2 * i / rate)));
        }
        return b;
    }

    // Spectral centroid in Hz of the first `seconds`, from a plain DFT on a few frames.
    double centroid (const juce::AudioBuffer<float>& b, double rate)
    {
        juce::dsp::FFT fft (12);
        std::vector<float> data (8192, 0.0f);
        double num = 0.0, den = 0.0;
        for (int start = 2048; start + 4096 < juce::jmin (b.getNumSamples(), 30000); start += 4096)
        {
            std::fill (data.begin(), data.end(), 0.0f);
            for (int i = 0; i < 4096; ++i)
                data[(size_t) i] = b.getSample (0, start + i) * (0.5f - 0.5f * std::cos (juce::MathConstants<float>::twoPi * (float) i / 4096.0f));
            fft.performFrequencyOnlyForwardTransform (data.data());
            for (int k = 1; k < 2048; ++k)
            {
                num += k * rate / 4096.0 * data[(size_t) k];
                den += data[(size_t) k];
            }
        }
        return num / juce::jmax (1.0e-9, den);
    }

    float soundingSeconds (const juce::AudioBuffer<float>& b, double rate)
    {
        const auto peak = b.getMagnitude (0, b.getNumSamples());
        int last = 0;
        for (int i = 0; i < b.getNumSamples(); ++i)
            if (std::abs (b.getSample (0, i)) > peak * 0.05f)
                last = i;
        return (float) (last / rate);
    }
}

int main()
{
    vae::DspVariationEngine engine;

    // ---- plant
    {
        const auto a = vae::Plant::grow (vae::library[0].traits, 0.5f, 1234, engine);
        const auto b = vae::Plant::grow (vae::library[0].traits, 0.5f, 1234, engine);
        const auto c = vae::Plant::grow (vae::library[0].traits, 0.5f, 99, engine);

        auto same = a.nodes.size() == b.nodes.size();
        for (size_t i = 0; same && i < a.nodes.size(); ++i)
            same = a.nodes[i].traits == b.nodes[i].traits && juce::exactlyEqual (a.nodes[i].angle, b.nodes[i].angle);
        check (same, "the same seed and rng seed grow the same plant");
        check (a.nodes.size() != c.nodes.size() || a.nodes[5].traits != c.nodes[5].traits, "another rng seed grows another plant");
        check (a.nodes.size() >= 30 && a.nodes.size() <= 90, "a plant has a few dozen buds (" + juce::String ((int) a.nodes.size()) + ")");

        auto ordered = true, inRange = true;
        for (size_t i = 1; i < a.nodes.size(); ++i)
        {
            const auto& n = a.nodes[i];
            ordered = ordered && n.parent >= 0 && n.parent < (int) i && n.depth == a.nodes[(size_t) n.parent].depth + 1;
            inRange = inRange && n.depth <= vae::Plant::maxDepth;
            for (auto v : n.traits)
                inRange = inRange && v >= 0.0f && v <= 1.0f;
        }
        check (ordered, "every bud comes after its parent, one level deeper");
        check (inRange, "depth and traits stay in range");
        check (a.nodes[0].traits == vae::library[0].traits, "the seed node is the seed itself");

        // Wild at 0 still varies, less than at 1, along the same branches.
        auto spread = [&] (float wild)
        {
            double sum = 0.0;
            const auto p = vae::Plant::grow (vae::library[1].traits, wild, 7, engine);
            for (const auto& n : p.nodes)
                for (size_t k = 0; k < n.traits.size(); ++k)
                    sum += std::abs (n.traits[k] - vae::library[1].traits[k]);
            return sum / (double) p.nodes.size();
        };
        check (spread (0.0f) > 0.05, "with Wild at 0 the buds still vary a little");
        check (spread (1.0f) > spread (0.0f) * 2.0, "Wild at 1 varies much more than at 0");

        const auto tame = vae::Plant::grow (vae::library[1].traits, 0.0f, 7, engine);
        const auto wildOne = vae::Plant::grow (vae::library[1].traits, 1.0f, 7, engine);
        auto sameShape = tame.nodes.size() == wildOne.nodes.size();
        for (size_t i = 0; sameShape && i < tame.nodes.size(); ++i)
            sameShape = juce::exactlyEqual (tame.nodes[i].angle, wildOne.nodes[i].angle) && tame.nodes[i].parent == wildOne.nodes[i].parent;
        check (sameShape, "changing Wild keeps the shape of the plant");

        double left = 0.0, right = 0.0;
        int nl = 0, nr = 0;
        for (std::uint32_t seed = 1; seed < 40; ++seed)
            for (const auto& n : vae::Plant::grow (vae::library[1].traits, 0.8f, seed, engine).nodes)
            {
                if (n.angle < -0.3f) { left += n.traits[vae::trait::bright]; ++nl; }
                if (n.angle > 0.3f)  { right += n.traits[vae::trait::bright]; ++nr; }
            }
        check (right / nr > left / nl + 0.05, "buds on the right are brighter than buds on the left");
        check (a.fullyGrownAfter() > 1.0f && a.fullyGrownAfter() < 4.0f, "a plant grows in a couple of seconds");
    }

    // ---- library seeds
    for (int i = 0; i < (int) vae::library.size(); ++i)
    {
        const auto source = vae::makeLibrarySource (i);
        const auto sound = engine.render (vae::library[(size_t) i].traits, *source);
        const auto st = measure (*sound);
        check (st.finite && st.peak > 0.3f && st.peak <= 0.51f && st.seconds <= 4.6,
               juce::String ("library seed ") + vae::library[(size_t) i].name + " renders a clean one-shot");
    }

    // ---- your own sound
    {
        const auto e3 = sineSource (164.81, 2.0);
        check (e3->rootNote == 52, "the note of a sample is detected (E3 = 52, got " + juce::String (e3->rootNote) + ")");

        auto noiseSource = std::make_shared<vae::SeedSource>();
        noiseSource->audio.setSize (1, 44100);
        juce::Random random (3);
        for (int i = 0; i < 44100; ++i)
            noiseSource->audio.setSample (0, i, random.nextFloat() * 0.6f - 0.3f);
        check (vae::detectRootNote (noiseSource->audio, 44100.0) < 0, "noise has no note");

        const auto seed = engine.render (vae::sampleNeutral, *e3);
        auto identical = seed->audio.getNumChannels() == 2 && seed->audio.getNumSamples() == e3->audio.getNumSamples();
        for (int ch = 0; identical && ch < 2; ++ch)
            for (int i = 0; identical && i < e3->audio.getNumSamples(); ++i)
                identical = juce::exactlyEqual (seed->audio.getSample (ch, i), e3->audio.getSample (ch, i));
        check (identical, "the seed plays your sound exactly as loaded");
        check (seed->rootNote == 52 && juce::exactlyEqual (seed->sampleRate, 44100.0), "the seed keeps its note and sample rate");

        // Every effect on its own changes the sound and stays clean.
        const std::pair<vae::trait::Index, float> moves[] {
            { vae::trait::bright, 0.1f }, { vae::trait::bright, 0.9f }, { vae::trait::metal, 0.7f }, { vae::trait::body, 0.7f },
            { vae::trait::attack, 0.6f }, { vae::trait::decay, 0.3f }, { vae::trait::noise, 0.5f }, { vae::trait::chorus, 0.7f },
            { vae::trait::shift, 0.95f }, { vae::trait::shift, 0.05f }, { vae::trait::reverse, 0.95f },
        };
        const auto seedStats = measure (*seed);
        for (auto [index, value] : moves)
        {
            auto t = vae::sampleNeutral;
            t[index] = value;
            const auto bud = engine.render (t, *e3);
            const auto st = measure (*bud);

            double difference = 0.0;
            const auto n = juce::jmin (bud->audio.getNumSamples(), seed->audio.getNumSamples());
            for (int i = 0; i < n; ++i)
                difference += std::abs (bud->audio.getSample (0, i) - seed->audio.getSample (0, i));

            check (st.finite && st.peak <= 0.951f && st.rms > 1.0e-4 && difference > 1.0,
                   "effect " + juce::String ((int) index) + " = " + juce::String (value, 2) + " changes the sound and stays clean");
        }
        check (seedStats.peak > 0.3f, "the test sample is audible");

        // A stereo seed keeps its stereo image in every bud.
        {
            auto t = vae::sampleNeutral;
            t[vae::trait::bright] = 0.2f;
            t[vae::trait::metal] = 0.5f;
            const auto bud = engine.render (t, *e3);
            const auto left = bud->audio.getRMSLevel (0, 0, bud->audio.getNumSamples());
            const auto right = bud->audio.getNumChannels() > 1 ? bud->audio.getRMSLevel (1, 0, bud->audio.getNumSamples()) : left;
            check (bud->audio.getNumChannels() == 2 && std::abs (right / left - 0.8f) < 0.02f,
                   "buds of a stereo sound stay stereo with the same balance");
        }

        const auto shorter = [&]
        {
            auto t = vae::sampleNeutral;
            t[vae::trait::decay] = 0.2f;
            return engine.render (t, *e3)->audio.getNumSamples();
        }();
        check (shorter < e3->audio.getNumSamples() / 2, "a lower decay cuts the tail");

        vae::Rng rng (5);
        auto allClean = true;
        for (int i = 0; i < 60; ++i)
        {
            const auto t = randomTraits (rng);
            const auto st1 = measure (*engine.render (t, *e3));
            const auto st2 = measure (*engine.render (t, *vae::makeLibrarySource (i % 5)));
            allClean = allClean && st1.finite && st2.finite && st1.peak <= 0.951f && st2.peak <= 0.51f
                       && st1.seconds <= 4.6 && st2.seconds <= 4.6;
        }
        check (allClean, "random buds always render finite, bounded and at most 4.5 s");

        const auto again1 = engine.render (randomTraits (rng = vae::Rng (9)), *e3);
        const auto again2 = engine.render (randomTraits (rng = vae::Rng (9)), *e3);
        auto same = again1->audio.getNumSamples() == again2->audio.getNumSamples();
        for (int i = 0; same && i < again1->audio.getNumSamples(); ++i)
            same = juce::exactlyEqual (again1->audio.getSample (0, i), again2->audio.getSample (0, i));
        check (same, "rendering is deterministic");
    }


    // ---- recipe: take a sound apart and rebuild it
    {
        const double rate = 44100.0;
        const auto tone = formantTone (220.0, 1200.0, 2.0);
        const auto recipe = vae::analyse (tone, rate, 57);
        const auto rebuilt = vae::resynthesise (*recipe, 57, {}, 10.0, 1);

        const auto levelIn = tone.getRMSLevel (0, 0, tone.getNumSamples());
        const auto levelOut = rebuilt.getRMSLevel (0, 0, rebuilt.getNumSamples());
        check (std::abs (juce::Decibels::gainToDecibels (levelOut / levelIn)) < 1.0f, "a rebuilt tone has the same level (within 1 dB)");
        check (std::abs (centroid (rebuilt, rate) / centroid (tone, rate) - 1.0) < 0.08, "a rebuilt tone has the same colour");

        // An octave up keeps its length and its colour, unlike a sampler.
        const auto up = vae::resynthesise (*recipe, 69, {}, 10.0, 1);
        check (vae::detectRootNote (up, rate) == 69, "a rebuilt note sounds at the note asked for");
        check (std::abs (soundingSeconds (up, rate) - soundingSeconds (tone, rate)) < 0.15f, "an octave up lasts as long as the original");
        const auto colourShift = centroid (up, rate) / centroid (tone, rate);
        check (colourShift < 1.35, "an octave up keeps its colour (centroid x" + juce::String (colourShift, 2) + ", a sampler gives x2)");

        juce::Random random (11);
        juce::AudioBuffer<float> noise (1, (int) rate);
        for (int i = 0; i < noise.getNumSamples(); ++i)
            noise.setSample (0, i, (random.nextFloat() * 2.0f - 1.0f) * 0.3f);
        const auto noiseOut = vae::resynthesise (*vae::analyse (noise, rate, 60), 60, {}, 10.0, 3);
        const auto noiseRatio = noiseOut.getRMSLevel (0, 2048, noiseOut.getNumSamples() - 4096) / noise.getRMSLevel (0, 0, noise.getNumSamples());
        check (std::abs (juce::Decibels::gainToDecibels (noiseRatio)) < 2.0f, "rebuilt noise has the same level (within 2 dB)");

        // Every gene changes the sound and stays clean.
        auto genesWith = [] (auto setter) { vae::Genes g; setter (g); return g; };
        const std::pair<const char*, vae::Genes> changes[] {
            { "tilt",      genesWith ([] (vae::Genes& g) { g.tilt = 0.8f; }) },
            { "even/odd",  genesWith ([] (vae::Genes& g) { g.evenOdd = 0.9f; }) },
            { "inharmonic",genesWith ([] (vae::Genes& g) { g.inharmonic = 0.9f; }) },
            { "formant",   genesWith ([] (vae::Genes& g) { g.formant = 0.8f; }) },
            { "attack",    genesWith ([] (vae::Genes& g) { g.attack = 0.7f; }) },
            { "decay",     genesWith ([] (vae::Genes& g) { g.decay = -0.7f; }) },
            { "body",      genesWith ([] (vae::Genes& g) { g.body = 0.8f; }) },
            { "noise",     genesWith ([] (vae::Genes& g) { g.noise = 0.9f; }) },
            { "chorus",    genesWith ([] (vae::Genes& g) { g.chorus = 0.8f; }) },
            { "octave",    genesWith ([] (vae::Genes& g) { g.octaveLayer = 1; g.octaveGain = 0.6f; }) },
            { "reversed",  genesWith ([] (vae::Genes& g) { g.reversed = true; }) },
        };
        for (const auto& [name, genes] : changes)
        {
            const auto bud = vae::resynthesise (*recipe, 57, genes, 4.5, 1);
            double difference = 0.0, finiteOk = 1.0;
            for (int i = 0; i < juce::jmin (bud.getNumSamples(), rebuilt.getNumSamples()); ++i)
            {
                difference += std::abs (bud.getSample (0, i) - rebuilt.getSample (0, i));
                if (! std::isfinite (bud.getSample (0, i))) finiteOk = 0.0;
            }
            check (finiteOk > 0.0 && bud.getMagnitude (0, bud.getNumSamples()) <= 0.951f && difference > 5.0,
                   juce::String ("recipe gene ") + name + " changes the sound and stays clean");
        }

        const auto stretched = vae::resynthesise (*recipe, 57, genesWith ([] (vae::Genes& g) { g.inharmonic = 1.0f; }), 4.5, 1);
        check (vae::detectRootNote (stretched, rate) != 57 || centroid (stretched, rate) > centroid (rebuilt, rate) * 1.05,
               "the inharmonic gene moves the partials");

        // The engine: the seed at its own note is the original; other notes are rebuilt.
        vae::RecipeVariationEngine recipeEngine;
        auto source = std::make_shared<vae::SeedSource>();
        source->kind = vae::SeedSource::Kind::sample;
        source->audio = tone;
        source->sampleRate = rate;
        source->rootNote = 57;
        const auto seed = recipeEngine.renderNote (vae::sampleNeutral, *source, 57);
        auto identical = seed->audio.getNumSamples() == tone.getNumSamples();
        for (int i = 0; identical && i < tone.getNumSamples(); ++i)
            identical = juce::exactlyEqual (seed->audio.getSample (0, i), tone.getSample (0, i));
        check (identical, "with the recipe engine the seed at its own note is still the original");
        const auto seedUp = recipeEngine.renderNote (vae::sampleNeutral, *source, 64);
        check (seedUp->rootNote == 64 && vae::detectRootNote (seedUp->audio, rate) == 64, "the seed at another note is rebuilt at that note");

        vae::Rng rng (21);
        auto allClean = true;
        for (int i = 0; i < 25; ++i)
        {
            const auto t = randomTraits (rng);
            const auto st1 = measure (*recipeEngine.renderNote (t, *source, 50 + i));
            const auto st2 = measure (*recipeEngine.renderNote (t, *vae::makeLibrarySource (i % 5), 48 + i));
            allClean = allClean && st1.finite && st2.finite && st1.peak <= 0.951f && st2.peak <= 0.951f && st1.rms > 1.0e-3 && st2.rms > 1.0e-3;
        }
        check (allClean, "random recipe buds always render finite, audible, bounded and level with the seed");

        const auto t0 = juce::Time::getMillisecondCounterHiRes();
        for (int i = 0; i < 10; ++i)
            recipeEngine.renderNote (randomTraits (rng), *source, 57);
        std::cout << "      (recipe bud render: " << juce::String ((juce::Time::getMillisecondCounterHiRes() - t0) / 10.0, 1) << " ms each)" << std::endl;
    }

    // ---- names
    {
        auto t = vae::sampleNeutral;
        check (vae::describeDifference (t, vae::sampleNeutral) == "Close to the seed", "an unchanged bud is close to the seed");
        t[vae::trait::bright] = 0.1f;
        t[vae::trait::shift] = 0.9f;
        check (vae::describeDifference (t, vae::sampleNeutral) == "Darker, octave up", "buds are named by what changed");
    }

    // ---- sampler
    {
        vae::Sampler sampler;
        sampler.prepare (48000.0);

        vae::RenderedSound::Ptr sound (new vae::RenderedSound());
        sound->sampleRate = 48000.0;
        sound->rootNote = 60;
        sound->audio.setSize (1, 48000);
        for (int i = 0; i < 48000; ++i)
            sound->audio.setSample (0, i, 0.5f * (float) std::sin (juce::MathConstants<double>::twoPi * 440.0 * i / 48000.0));

        juce::AudioBuffer<float> out (2, 512);
        out.clear();
        sampler.start (sound, 60, 1.0f, 0.0f, true);
        sampler.render (out, 0, 512);
        check (out.getMagnitude (0, 0, 512) > 0.4f && out.getMagnitude (1, 0, 512) > 0.4f, "a started voice sounds on both sides");

        // An octave up plays twice as fast, so it ends in half the time.
        sampler.allNotesOff();
        sampler.start (sound, 72, 1.0f, 0.0f, false);
        int blocks = 0;
        while (sampler.activeVoices() > 0 && blocks < 200)
        {
            out.clear();
            sampler.render (out, 0, 512);
            ++blocks;
        }
        check (std::abs (blocks - 47) <= 1, "an octave up plays twice as fast (" + juce::String (blocks) + " blocks)");

        sampler.start (sound, 60, 1.0f, 0.0f, true);
        sampler.noteOff (60);
        for (int b = 0; b < 40; ++b) { out.clear(); sampler.render (out, 0, 512); }
        check (sampler.activeVoices() == 0, "releasing a key fades the voice out");

        sampler.setSustain (true);
        sampler.start (sound, 60, 1.0f, 0.0f, true);
        sampler.noteOff (60);
        for (int b = 0; b < 40; ++b) { out.clear(); sampler.render (out, 0, 512); }
        check (sampler.activeVoices() == 1, "the sustain pedal holds a released key");
        sampler.setSustain (false);
        for (int b = 0; b < 40; ++b) { out.clear(); sampler.render (out, 0, 512); }
        check (sampler.activeVoices() == 0, "lifting the pedal releases it");

        for (int i = 0; i < 30; ++i)
            sampler.start (sound, 60 + i % 12, 0.5f, 0.0f, false);
        check (sampler.activeVoices() == vae::Sampler::numVoices, "voices are capped and the oldest are stolen");
        check (sound->getReferenceCount() == 1 + vae::Sampler::numVoices, "voices hold a reference to their sound");
        sampler.allNotesOff();
        check (sound->getReferenceCount() == 1, "stopped voices let their sound go");
    }

    // ---- scale
    {
        const auto aMinorPentatonic = vae::Scale::fromIndices (9, vae::Scale::typeNames().indexOf ("Pentatonic minor"));
        check (aMinorPentatonic.snap (61.0f) == 60 && aMinorPentatonic.snap (63.0f) == 62, "notes snap into the scale");
        check (vae::Scale::fromIndices (0, 0).snap (61.0f) == 61, "with the scale off every note passes");
    }

    std::cout << (failures == 0 ? "\nAll tests passed" : "\nSome tests failed") << std::endl;
    return failures == 0 ? 0 : 1;
}
