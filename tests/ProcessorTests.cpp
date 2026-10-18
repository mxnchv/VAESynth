// Drives the full plugin processor headlessly: background sample loading,
// rendering MIDI, and saving/restoring a session with its samples and map.

#include "../Source/PluginProcessor.h"

#include <iostream>

namespace
{
    int failures = 0;

    void check (bool ok, const juce::String& what)
    {
        std::cout << (ok ? "PASS  " : "FAIL  ") << what << std::endl;
        failures += ok ? 0 : 1;
    }

    juce::File writeTestWav (const juce::File& dir, const juce::String& name, double frequency, bool addNoise)
    {
        constexpr double rate = 44100.0;
        juce::AudioBuffer<float> buffer (2, (int) rate * 2);
        juce::Random random (7);

        for (int i = 0; i < buffer.getNumSamples(); ++i)
        {
            auto s = 0.4f * (float) std::sin (juce::MathConstants<double>::twoPi * frequency * i / rate);
            if (addNoise && i > buffer.getNumSamples() / 2)
                s = 0.3f * (random.nextFloat() * 2.0f - 1.0f);
            buffer.setSample (0, i, s);
            buffer.setSample (1, i, s);
        }

        auto file = dir.getChildFile (name);
        file.deleteFile();
        juce::WavAudioFormat wav;
        std::unique_ptr<juce::OutputStream> stream (file.createOutputStream().release());
        auto writer = wav.createWriterFor (stream, juce::AudioFormatWriterOptions{}.withSampleRate (rate).withNumChannels (2).withBitsPerSample (24));
        writer->writeFromAudioSampleBuffer (buffer, 0, buffer.getNumSamples());
        return file;
    }

    bool waitForLoading (VAESynthProcessor& p, size_t expectedSources)
    {
        for (int i = 0; i < 400; ++i)
        {
            juce::Thread::sleep (10);
            auto map = p.getMap();
            if (! p.isLoading() && map != nullptr && map->getSources().size() == expectedSources)
                return true;
        }
        return false;
    }

    float renderNote (VAESynthProcessor& p, int blocks)
    {
        juce::AudioBuffer<float> buffer (2, 512);
        double sum = 0.0;
        for (int b = 0; b < blocks; ++b)
        {
            juce::MidiBuffer midi;
            if (b == 0)
                midi.addEvent (juce::MidiMessage::noteOn (1, 64, 0.9f), 0);
            p.processBlock (buffer, midi);
            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < buffer.getNumSamples(); ++i)
                    sum += (double) buffer.getSample (ch, i) * buffer.getSample (ch, i);
        }
        juce::AudioBuffer<float> off (2, 512);
        juce::MidiBuffer midi;
        midi.addEvent (juce::MidiMessage::allNotesOff (1), 0);
        p.processBlock (off, midi);
        return (float) std::sqrt (sum / (blocks * 1024.0));
    }
}

int main()
{
    juce::ScopedJuceInitialiser_GUI juce;
    auto dir = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("vaesynth-tests");
    dir.createDirectory();

    const auto pad = writeTestWav (dir, "pad.wav", 220.0, true);
    const auto bell = writeTestWav (dir, "bell.wav", 880.0, false);

    juce::MemoryBlock saved;
    vae::Vec3 firstGrain;

    {
        VAESynthProcessor p;
        p.setPlayConfigDetails (0, 2, 48000.0, 512);
        p.prepareToPlay (48000.0, 512);

        p.addSampleFiles ({ pad, bell });
        check (waitForLoading (p, 2), "two dropped files load in the background");
        check (p.getMap()->getProjection().valid, "the map is locked after loading");
        check (renderNote (p, 40) > 1.0e-3f, "a note sounds once samples are loaded");

        const auto bellSlot = p.getMap()->getSources()[1]->slot;
        p.setRootNote (bellSlot, 67);
        p.setMuted (bellSlot, true);
        p.getState().getParameter ("radius")->setValueNotifyingHost (0.2f);
        firstGrain = p.getMap()->getGrains().front().position;
        p.getStateInformation (saved);

        // Removing a sample keeps the others where they were.
        const auto padSource = p.getMap()->getSources()[0];
        const auto bellFirst = std::find_if (p.getMap()->getGrains().begin(), p.getMap()->getGrains().end(),
                                             [] (const vae::MapGrain& g) { return g.source == 1; })->position;
        p.removeSample (padSource);
        check (waitForLoading (p, 1), "a sample can be removed");
        check ((p.getMap()->getGrains().front().position - bellFirst).length() < 1.0e-4f, "removing a sample does not move the others");
        check (p.getMap()->getSources()[0]->slot == bellSlot && p.isMuted (bellSlot), "the remaining sample keeps its colour slot and settings");

        p.clearSamples();
        check (waitForLoading (p, 0) && p.getMap()->getGrains().empty(), "clear all empties the sphere");

        p.addSampleFiles ({ pad });
        check (waitForLoading (p, 1) && p.getMap()->getProjection().valid, "samples can be added again after clearing");
    }

    {
        VAESynthProcessor p;
        p.setPlayConfigDetails (0, 2, 48000.0, 512);
        p.prepareToPlay (48000.0, 512);
        p.setStateInformation (saved.getData(), (int) saved.getSize());

        check (waitForLoading (p, 2), "a saved session reloads its samples");
        const auto bellSlot = p.getMap()->getSources()[1]->slot;
        check ((p.getMap()->getGrains().front().position - firstGrain).length() < 1.0e-4f, "the restored map matches the saved one");
        check (p.getRootNote (bellSlot) == 67, "root notes are restored");
        check (p.isMuted (bellSlot), "mute state is restored");
        check (std::abs (p.getState().getParameter ("radius")->getValue() - 0.2f) < 1.0e-4f, "parameters are restored");
        check (renderNote (p, 40) > 1.0e-3f, "the restored session sounds");

        p.rebuildMap();
        check (waitForLoading (p, 2) && p.getMap()->getProjection().valid, "rebuild map keeps a valid map");
    }

    dir.deleteRecursively();
    std::cout << (failures == 0 ? "\nAll tests passed" : "\nSome tests failed") << std::endl;
    return failures == 0 ? 0 : 1;
}
