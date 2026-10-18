#include "GrainEngine.h"

namespace vae
{
    void GrainEngine::prepare (double newSampleRate, int maxBlockSize)
    {
        sampleRate = newSampleRate;
        blockCapacity = juce::jmax (1, maxBlockSize);
        envelopeBuffer.assign ((size_t) (maxVoices * blockCapacity), 0.0f);

        for (auto& voice : voices)
            voice.envelope.setSampleRate (sampleRate);

        reset();
    }

    void GrainEngine::reset()
    {
        for (auto& voice : voices)
        {
            voice.held = false;
            voice.note = -1;
            voice.envelope.reset();
        }

        for (auto& grain : grains)
            grain.active = false;
    }

    int GrainEngine::getNumActiveGrains() const noexcept
    {
        int count = 0;
        for (const auto& grain : grains)
            count += grain.active ? 1 : 0;
        return count;
    }

    void GrainEngine::process (juce::AudioBuffer<float>& output, const juce::MidiBuffer& midi, const EngineSettings& settings)
    {
        for (auto& voice : voices)
            voice.envelope.setParameters (settings.envelope);

        // Hosts may send blocks larger than announced: render in chunks that fit the envelope buffer.
        auto renderRange = [&] (int start, int numSamples)
        {
            while (numSamples > 0)
            {
                const auto chunk = juce::jmin (numSamples, blockCapacity);
                renderSegment (output, start, chunk, settings);
                start += chunk;
                numSamples -= chunk;
            }
        };

        const auto total = output.getNumSamples();
        int position = 0;

        for (const auto metadata : midi)
        {
            const auto eventPosition = juce::jlimit (position, total, metadata.samplePosition);
            renderRange (position, eventPosition - position);
            handleMidi (metadata.getMessage());
            position = eventPosition;
        }

        renderRange (position, total - position);
    }

    void GrainEngine::handleMidi (const juce::MidiMessage& message)
    {
        if (message.isNoteOn())
        {
            Voice* target = nullptr;

            for (auto& voice : voices)
                if (! voice.held && ! voice.envelope.isActive())
                {
                    target = &voice;
                    break;
                }

            if (target == nullptr)
            {
                target = &voices[0];
                for (auto& voice : voices)
                    if (voice.startedAt < target->startedAt)
                        target = &voice;
            }

            target->held = true;
            target->note = message.getNoteNumber();
            target->velocity = message.getFloatVelocity();
            target->startedAt = ++noteCounter;
            target->samplesToNextGrain = 0.0;
            target->envelope.noteOn();
        }
        else if (message.isNoteOff())
        {
            for (auto& voice : voices)
                if (voice.held && voice.note == message.getNoteNumber())
                {
                    voice.held = false;
                    voice.envelope.noteOff();
                }
        }
        else if (message.isAllNotesOff() || message.isAllSoundOff())
        {
            for (auto& voice : voices)
            {
                voice.held = false;
                voice.envelope.noteOff();
            }
        }
    }

    void GrainEngine::renderSegment (juce::AudioBuffer<float>& output, int start, int numSamples, const EngineSettings& settings)
    {
        const auto interval = sampleRate / juce::jmax (0.1f, settings.density);

        for (int v = 0; v < maxVoices; ++v)
        {
            auto& voice = voices[(size_t) v];
            auto* envelope = envelopeBuffer.data() + v * blockCapacity;

            if (! voice.envelope.isActive())
            {
                std::fill (envelope, envelope + numSamples, 0.0f);
                continue;
            }

            for (int i = 0; i < numSamples; ++i)
            {
                envelope[i] = voice.envelope.getNextSample();
                voice.samplesToNextGrain -= 1.0;

                if (voice.samplesToNextGrain <= 0.0)
                {
                    spawnGrain (v, i, settings);
                    voice.samplesToNextGrain += interval * (0.9 + 0.2 * random.nextDouble());
                }
            }
        }

        auto* left = output.getWritePointer (0, start);
        auto* right = output.getNumChannels() > 1 ? output.getWritePointer (1, start) : nullptr;

        for (auto& grain : grains)
        {
            if (! grain.active)
                continue;

            const auto* source = grain.source->audio.getReadPointer (0);
            const auto sourceLength = grain.source->audio.getNumSamples();
            const auto* envelope = envelopeBuffer.data() + grain.voice * blockCapacity;

            for (int i = grain.delay; i < numSamples; ++i)
            {
                const auto index = (int) grain.readPosition;

                if (grain.position >= grain.length || index + 1 >= sourceLength)
                {
                    grain.active = false;
                    break;
                }

                const auto frac = (float) (grain.readPosition - index);
                const auto sample = source[index] + frac * (source[index + 1] - source[index]);
                const auto amp = sample * envelope[i] * grainWindow ((float) grain.position / (float) grain.length, grain.shape);

                if (right != nullptr)
                {
                    left[i] += amp * grain.gainLeft;
                    right[i] += amp * grain.gainRight;
                }
                else
                {
                    left[i] += amp * 0.5f * (grain.gainLeft + grain.gainRight);
                }

                grain.readPosition += grain.increment;
                ++grain.position;
            }

            grain.delay = 0;
        }
    }

    void GrainEngine::spawnGrain (int voiceIndex, int delay, const EngineSettings& settings)
    {
        if (map == nullptr || map->getGrains().empty())
            return;

        Grain* slot = nullptr;
        for (auto& grain : grains)
            if (! grain.active)
            {
                slot = &grain;
                break;
            }

        if (slot == nullptr)
            return;

        const auto& voice = voices[(size_t) voiceIndex];

        // Playing harder pushes the note outwards, on whichever side the cursor is.
        const auto side = settings.radius < 0.0f ? -1.0f : 1.0f;
        const auto radius = juce::jlimit (-1.5f, 1.5f, settings.radius + side * settings.velocityToRadius * (voice.velocity - 0.5f));

        auto target = settings.direction * radius;
        if (settings.spray > 0.0f)
            target = target + randomInBall() * (settings.spray * 0.5f);

        const auto& mapGrains = map->getGrains();

        // Towards the centre the cursor hears the average of everything:
        // the closer to it, the more grains are picked from the whole map.
        const auto chosen = random.nextFloat() >= juce::jmin (1.0f, target.length())
                              ? random.nextInt ((int) mapGrains.size())
                              : pickGrain (target, juce::jlimit (1, maxNeighbours, settings.neighbours));

        const auto& mapGrain = mapGrains[(size_t) chosen];
        const auto* source = map->getSources()[(size_t) mapGrain.source].get();

        auto root = 60;
        if (rootNotes != nullptr && mapGrain.source < maxSources)
            root = (*rootNotes)[(size_t) mapGrain.source].load (std::memory_order_relaxed);

        auto semitones = settings.transpose ? (float) (voice.note - root) : 0.0f;
        semitones += settings.detuneCents * 0.01f * (random.nextFloat() * 2.0f - 1.0f);

        const auto increment = std::pow (2.0, semitones / 12.0) * source->sampleRate / sampleRate;
        const auto sourceLength = source->audio.getNumSamples();

        auto length = juce::jmax (16, (int) (settings.grainSizeMs * 0.001 * sampleRate));
        length = juce::jmin (length, (int) ((sourceLength - 2) / increment));

        if (length < 16)
            return;

        const auto span = length * increment;
        const auto readStart = juce::jlimit (0.0, juce::jmax (0.0, sourceLength - span - 2.0), (double) mapGrain.start);

        const auto overlap = settings.density * settings.grainSizeMs * 0.001f;
        const auto gain = voice.velocity * 0.8f / std::sqrt (juce::jmax (1.0f, overlap));
        const auto pan = settings.width * (random.nextFloat() * 2.0f - 1.0f);
        const auto angle = (pan + 1.0f) * juce::MathConstants<float>::pi * 0.25f;

        slot->active = true;
        slot->source = source;
        slot->voice = voiceIndex;
        slot->delay = delay;
        slot->length = length;
        slot->position = 0;
        slot->readPosition = readStart;
        slot->increment = increment;
        slot->gainLeft = gain * std::cos (angle);
        slot->gainRight = gain * std::sin (angle);
        slot->shape = settings.shape;
    }

    int GrainEngine::pickGrain (Vec3 target, int neighbours)
    {
        std::array<float, maxNeighbours> bestDistance {};
        std::array<int, maxNeighbours> bestIndex {};
        int count = 0;

        const auto& mapGrains = map->getGrains();

        for (int i = 0; i < (int) mapGrains.size(); ++i)
        {
            const auto d = (mapGrains[(size_t) i].position - target).lengthSquared();

            if (count == neighbours && d >= bestDistance[(size_t) (count - 1)])
                continue;

            auto j = count < neighbours ? count++ : count - 1;
            while (j > 0 && bestDistance[(size_t) (j - 1)] > d)
            {
                bestDistance[(size_t) j] = bestDistance[(size_t) (j - 1)];
                bestIndex[(size_t) j] = bestIndex[(size_t) (j - 1)];
                --j;
            }

            bestDistance[(size_t) j] = d;
            bestIndex[(size_t) j] = i;
        }

        std::array<float, maxNeighbours> weights {};
        float total = 0.0f;
        for (int i = 0; i < count; ++i)
        {
            weights[(size_t) i] = 1.0f / (std::sqrt (bestDistance[(size_t) i]) + 0.05f);
            total += weights[(size_t) i];
        }

        auto pick = random.nextFloat() * total;
        for (int i = 0; i < count; ++i)
        {
            pick -= weights[(size_t) i];
            if (pick <= 0.0f)
                return bestIndex[(size_t) i];
        }

        return bestIndex[(size_t) juce::jmax (0, count - 1)];
    }

    Vec3 GrainEngine::randomInBall()
    {
        for (;;)
        {
            const Vec3 p { random.nextFloat() * 2.0f - 1.0f, random.nextFloat() * 2.0f - 1.0f, random.nextFloat() * 2.0f - 1.0f };
            if (p.lengthSquared() <= 1.0f)
                return p;
        }
    }

    float GrainEngine::grainWindow (float t, float shape) noexcept
    {
        constexpr auto halfPi = juce::MathConstants<float>::halfPi;

        // Attack shortens and the tail turns exponential as shape goes to 1.
        const auto attack = juce::jmap (shape, 0.5f, 0.03f);

        if (t < attack)
        {
            const auto s = std::sin (halfPi * t / attack);
            return s * s;
        }

        const auto u = (t - attack) / (1.0f - attack);
        const auto c = std::cos (halfPi * u);
        const auto smooth = c * c;
        const auto percussive = std::exp (-6.0f * u) * (1.0f - u);
        return juce::jmap (shape, smooth, percussive);
    }
}
