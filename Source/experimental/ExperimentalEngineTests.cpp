// Tests for the experimental pretrained-model engine. Built only with
// -DVAESYNTH_EXPERIMENTAL_PRETRAINED=ON -DVAESYNTH_BUILD_TESTS=ON.
//
// Without a server they check the mutation (against values from
// vaesynth_exp/plant.py) and that a missing server leaves buds unlit.
// With VAESYNTH_TEST_PORT set to a running server's port they also check
// real round trips.

#include "PretrainedVariationEngine.h"

#include <cmath>
#include <cstring>
#include <iostream>

using namespace vae;

namespace
{
    int failures = 0;

    void expect (bool condition, const char* what)
    {
        std::cout << (condition ? "  ok    " : "  FAIL  ") << what << "\n";
        if (! condition)
            ++failures;
    }

    std::shared_ptr<SeedSource> toneSeed (int channels, double rate, double seconds)
    {
        auto seed = std::make_shared<SeedSource>();
        seed->kind = SeedSource::Kind::sample;
        seed->sampleRate = rate;
        seed->rootNote = 57;
        seed->audio.setSize (channels, (int) (rate * seconds));
        for (int ch = 0; ch < channels; ++ch)
            for (int i = 0; i < seed->audio.getNumSamples(); ++i)
                seed->audio.setSample (ch, i, (float) (0.4 * std::sin (2.0 * juce::MathConstants<double>::pi * 220.0 * i / rate)
                                                       * std::exp (-1.5 * i / rate) * (ch == 0 ? 1.0 : 0.7)));
        return seed;
    }

    bool finite (const juce::AudioBuffer<float>& audio)
    {
        for (int ch = 0; ch < audio.getNumChannels(); ++ch)
            for (int i = 0; i < audio.getNumSamples(); ++i)
                if (! std::isfinite (audio.getSample (ch, i)))
                    return false;
        return true;
    }

    void testMutationMatchesPython()
    {
        std::cout << "Mutation (same as vaesynth_exp/plant.py)\n";
        PretrainedVariationEngine::Settings s;
        s.direction = { -0.7117402f, -0.5601642f, 0.2878827f, 0.1696077f, 0.0455403f, -0.0213719f,
                        -0.1373956f, 0.1250173f, 0.0014334f, -0.1756652f, -0.0100398f };
        s.sigma = 1.0f;
        s.leanK = 1.2f;
        s.leash = 0.1f;

        // Reference: mutate() in plant.py, Rng(42), then a grandchild.
        const Traits a { 0.3787611f, 0.4525315f, 0.3262011f, 0.3920177f, 0.4486961f, 0.6220897f,
                         0.4682020f, 0.3243213f, 0.4334517f, 0.1657683f, 0.5561672f };
        const Traits b { 0.7282190f, 0.8656901f, 0.3650952f, 0.5922425f, 0.6511384f, 0.7008401f,
                         0.2104694f, 0.8040335f, 0.2621005f, 0.0285360f, 0.4355668f };

        Rng rng (42);
        const auto childA = PretrainedVariationEngine::mutateWith (sampleNeutral, { 0.3f, 0.1f }, rng, s);
        const auto childB = PretrainedVariationEngine::mutateWith (childA, { -0.8f, 0.2f }, rng, s);

        auto close = [] (const Traits& x, const Traits& y)
        {
            for (size_t i = 0; i < x.size(); ++i)
                if (std::abs (x[i] - y[i]) > 1.0e-5f)
                    return false;
            return true;
        };
        expect (close (childA, a), "child of the seed matches Python");
        expect (close (childB, b), "grandchild matches Python");
        expect (PretrainedVariationEngine::isOrigin (sampleNeutral) && PretrainedVariationEngine::isOrigin (library[2].traits),
                "the seed and library patches are origins");
        expect (! PretrainedVariationEngine::isOrigin (childA), "a bud is not an origin");
    }

    void testWithoutServer()
    {
        std::cout << "No server running\n";
        PretrainedVariationEngine engine (1);  // nothing listens on port 1
        const auto started = juce::Time::getMillisecondCounter();
        expect (! engine.getSettings().loaded, "settings are not loaded");

        const auto seed = toneSeed (1, 44100.0, 1.0);
        Rng rng (7);
        const auto bud = engine.mutate (sampleNeutral, { 0.5f, 0.2f }, rng);
        expect (engine.render (bud, *seed) == nullptr, "a bud stays unlit (nullptr)");

        const auto same = engine.render (sampleNeutral, *seed);
        expect (same != nullptr && same->audio.getNumSamples() == seed->audio.getNumSamples(), "the seed still plays exactly as loaded");
        expect (juce::Time::getMillisecondCounter() - started < 5000, "and it gives up quickly");
    }

    void testWithServer (int port)
    {
        std::cout << "Server on 127.0.0.1:" << port << "\n";
        PretrainedVariationEngine engine (port);
        const auto s = engine.getSettings();
        expect (s.loaded && s.configVersion > 0, "settings loaded from /info");

        for (int channels = 1; channels <= 2; ++channels)
        {
            const auto seed = toneSeed (channels, 48000.0, 1.0);
            Rng rng (11);
            const auto bud = engine.mutate (sampleNeutral, { 0.4f, 0.3f }, rng);
            const auto first = engine.render (bud, *seed);
            expect (first != nullptr, channels == 1 ? "mono seed: bud rendered" : "stereo seed: bud rendered");
            if (first == nullptr)
                continue;
            expect (first->audio.getNumChannels() == channels, "same channels as the seed");
            expect (first->audio.getNumSamples() > 0 && juce::approximatelyEqual (first->sampleRate, 44100.0) && first->rootNote == 57, "length, rate and note");
            expect (finite (first->audio), "finite samples");

            const auto again = engine.render (bud, *seed);
            bool equal = again != nullptr && again->audio.getNumSamples() == first->audio.getNumSamples();
            for (int ch = 0; equal && ch < channels; ++ch)
                equal = std::memcmp (again->audio.getReadPointer (ch), first->audio.getReadPointer (ch),
                                     sizeof (float) * (size_t) first->audio.getNumSamples()) == 0;
            expect (equal, "same bud, same audio");
        }

        const auto library0 = makeLibrarySource (0);
        Rng rng (3);
        const auto libraryBud = engine.mutate (library[0].traits, { -0.2f, 0.2f }, rng);
        expect (engine.render (libraryBud, *library0) != nullptr, "a library seed's bud is rendered too");
    }
}

int main()
{
    testMutationMatchesPython();
    testWithoutServer();

    const auto port = juce::SystemStats::getEnvironmentVariable ("VAESYNTH_TEST_PORT", {}).getIntValue();
    if (port > 0)
        testWithServer (port);
    else
        std::cout << "(set VAESYNTH_TEST_PORT to also test against a running server)\n";

    std::cout << (failures == 0 ? "All passed\n" : "Some tests failed\n");
    return failures == 0 ? 0 : 1;
}
