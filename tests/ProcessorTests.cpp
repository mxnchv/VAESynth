// Drives the full plugin processor headlessly: growing and rendering plants,
// playing them from MIDI and the editor, planting, and saving/restoring a session.

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

    juce::File writeTestWav (const juce::File& dir, const juce::String& name, double frequency)
    {
        constexpr double rate = 44100.0;
        juce::AudioBuffer<float> buffer (2, (int) rate * 2);

        for (int i = 0; i < buffer.getNumSamples(); ++i)
        {
            const auto s = 0.4f * (float) std::exp (-2.0 * i / rate) * (float) std::sin (juce::MathConstants<double>::twoPi * frequency * i / rate);
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

    // Plays one MIDI note (or nothing) and returns the RMS of the output.
    float render (VAESynthProcessor& p, int blocks, int note)
    {
        juce::AudioBuffer<float> buffer (2, 512);
        double sum = 0.0;
        for (int b = 0; b < blocks; ++b)
        {
            juce::MidiBuffer midi;
            if (b == 0 && note >= 0)
                midi.addEvent (juce::MidiMessage::noteOn (1, note, 0.9f), 0);
            p.processBlock (buffer, midi);
            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < buffer.getNumSamples(); ++i)
                    sum += (double) buffer.getSample (ch, i) * buffer.getSample (ch, i);
        }

        juce::AudioBuffer<float> off (2, 512);
        juce::MidiBuffer midi;
        midi.addEvent (juce::MidiMessage::allNotesOff (1), 0);
        p.processBlock (off, midi);
        for (int b = 0; b < 2500; ++b)  // let the reverb tail die
        {
            juce::MidiBuffer none;
            p.processBlock (off, none);
        }
        return (float) std::sqrt (sum / (blocks * 1024.0));
    }

    bool openAndCloseEditor (VAESynthProcessor& p)
    {
        std::unique_ptr<juce::AudioProcessorEditor> editor (p.createEditorAndMakeActive());
        if (editor == nullptr)
            return false;
        editor->setSize (900, 600);
        editor->setSize (760, 520);
        p.editorBeingDeleted (editor.get());
        return true;
    }
}

int main()
{
    juce::ScopedJuceInitialiser_GUI juce;
    auto dir = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("vaesynth-tests");
    dir.createDirectory();
    const auto sample = writeTestWav (dir, "e3.wav", 164.81);

    juce::MemoryBlock saved;
    std::vector<vae::Traits> savedTraits;

    {
        VAESynthProcessor p;
        p.setPlayConfigDetails (0, 2, 48000.0, 512);
        p.prepareToPlay (48000.0, 512);
        auto& session = p.getSession();

        check (session.hasPlant() && session.getPlant().nodes.size() > 30, "it opens with a plant already grown");
        check (session.waitUntilRendered (20000), "every bud renders in the background");
        check (render (p, 40, 57) > 1.0e-3f, "MIDI plays the seed");
        check (render (p, 40, -1) < 1.0e-6f, "silence without notes");

        session.audition (5, 60, 0.8f, 0.3f);
        check (render (p, 40, -1) > 1.0e-3f, "sweeping over a bud plays it");

        juce::String error;
        check (! session.loadSeedFile (dir.getChildFile ("missing.wav"), error) && error.isNotEmpty(), "a missing file is refused with a reason");
        check (session.loadSeedFile (sample, error), "your own sound becomes the seed");
        check (session.getHomeNote() == 52, "its note is detected");
        check (session.getHistory().size() == 1 && session.getCurrentGeneration() == 0, "a new seed starts a new history");
        check (session.waitUntilRendered (20000), "the new plant renders");

        session.select (4);
        check (session.getSelected() == 4, "a bud can be picked");
        check (render (p, 40, 52) > 1.0e-3f, "MIDI plays the picked bud");

        session.plantNode (4);
        check (session.getHistory().size() == 2 && session.getCurrentGeneration() == 1, "planting a bud grows a second generation");
        check (session.getSelected() == -1, "a new plant starts with nothing picked");
        check (session.getPlant().nodes[0].traits == session.getHistory()[1].traits, "the planted bud is the new seed");
        check (session.waitUntilRendered (20000), "the second plant renders");

        session.goTo (0);
        check (session.getCurrentGeneration() == 0, "you can go back to an earlier seed");
        session.goTo (1);
        session.select (3);

        p.getState().getParameter (ParamIDs::space.getParamID())->setValueNotifyingHost (0.8f);
        for (const auto& n : session.getPlant().nodes)
            savedTraits.push_back (n.traits);
        p.getStateInformation (saved);

        session.regrow();
        check (session.getHistory().size() == 2 && session.getPlant().nodes[1].traits != savedTraits[1], "regrow grows a different plant from the same seed");

        check (openAndCloseEditor (p), "the editor opens and resizes");
    }

    {
        VAESynthProcessor p;
        p.setPlayConfigDetails (0, 2, 48000.0, 512);
        p.prepareToPlay (48000.0, 512);
        p.setStateInformation (saved.getData(), (int) saved.getSize());
        auto& session = p.getSession();

        check (session.getHistory().size() == 2 && session.getCurrentGeneration() == 1, "a saved session restores its history");
        auto same = session.getPlant().nodes.size() == savedTraits.size();
        for (size_t i = 0; same && i < savedTraits.size(); ++i)
            same = session.getPlant().nodes[i].traits == savedTraits[i];
        check (same, "the restored plant is the same plant");
        check (session.getSelected() == 3, "the picked bud is restored");
        check (session.getHomeNote() == 52, "the sample is reloaded from its file");
        check (std::abs (p.getState().getParameter (ParamIDs::space.getParamID())->getValue() - 0.8f) < 1.0e-4f, "parameters are restored");
        check (session.waitUntilRendered (20000) && render (p, 40, 52) > 1.0e-3f, "the restored session sounds");
    }

    {
        sample.deleteFile();
        VAESynthProcessor p;
        p.setStateInformation (saved.getData(), (int) saved.getSize());
        check (p.getSession().hasPlant() && p.getSession().describeNode (0).startsWith ("Missing"), "a missing file falls back to a library seed and says so");
    }

    dir.deleteRecursively();
    std::cout << (failures == 0 ? "\nAll tests passed" : "\nSome tests failed") << std::endl;
    return failures == 0 ? 0 : 1;
}
