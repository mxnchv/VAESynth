#include "PretrainedVariationEngine.h"

#include <cstring>

namespace vae
{
    namespace
    {
        constexpr int infoTimeoutMs = 1500;     // GET /info, done once (on the message thread)
        constexpr int renderTimeoutMs = 60000;  // one bud, or encoding a seed
        constexpr juce::int64 infoRetryMs = 5000;

        bool sameTraits (const Traits& a, const Traits& b) noexcept
        {
            for (size_t i = 0; i < a.size(); ++i)
                if (std::abs (a[i] - b[i]) > 1.0e-5f)
                    return false;
            return true;
        }

        // Identifies a seed's audio, so the server encodes each seed only once.
        juce::String audioKey (const juce::AudioBuffer<float>& audio, double sampleRate)
        {
            juce::uint64 h = 1469598103934665603ull;
            auto mix = [&h] (juce::uint64 v) { h ^= v; h *= 1099511628211ull; };
            mix ((juce::uint64) juce::roundToInt (sampleRate));
            mix ((juce::uint64) audio.getNumChannels());
            mix ((juce::uint64) audio.getNumSamples());
            for (int ch = 0; ch < audio.getNumChannels(); ++ch)
            {
                const auto* data = audio.getReadPointer (ch);
                for (int i = 0; i < audio.getNumSamples(); ++i)
                {
                    std::uint32_t bits;
                    std::memcpy (&bits, data + i, sizeof (bits));
                    mix (bits);
                }
            }
            return juce::String::toHexString ((juce::int64) h);
        }

        void appendFloat (juce::MemoryOutputStream& out, float v)
        {
            std::uint32_t bits;
            std::memcpy (&bits, &v, sizeof (bits));
            out.writeInt ((int) bits);  // MemoryOutputStream writes little-endian
        }

        float readFloat (const char* data) noexcept
        {
            const auto bits = juce::ByteOrder::littleEndianInt (data);
            float v;
            std::memcpy (&v, &bits, sizeof (v));
            return v;
        }
    }

    PretrainedVariationEngine::PretrainedVariationEngine (int portToUse)
    {
        port = portToUse > 0 ? portToUse
                             : juce::SystemStats::getEnvironmentVariable ("VAESYNTH_SERVER_PORT", {}).getIntValue();
        if (port <= 0)
            port = defaultPort;
    }

    bool PretrainedVariationEngine::isOrigin (const Traits& t) noexcept
    {
        if (sameTraits (t, sampleNeutral))
            return true;
        for (const auto& seed : library)
            if (sameTraits (t, seed.traits))
                return true;
        return false;
    }

    Traits PretrainedVariationEngine::mutateWith (const Traits& parent, const MutationRequest& request,
                                                  Rng& rng, const Settings& s) noexcept
    {
        // A child of the seed starts from the seed itself: coordinates 0.5.
        const auto origin = isOrigin (parent);
        Traits child {};
        for (size_t i = 0; i < trait::count; ++i)
        {
            const auto base = origin ? 0.5f : parent[i];
            const auto step = rng.gauss() * request.amount * s.sigma;
            const auto pull = s.leash * (0.5f - base);
            const auto push = request.lean * request.amount * s.leanK * s.direction[i];
            child[i] = juce::jlimit (0.0f, 1.0f, base + step + pull + push);
        }
        return child;
    }

    PretrainedVariationEngine::Settings PretrainedVariationEngine::getSettings() const
    {
        const std::lock_guard<std::mutex> lock (mutex);
        if (settings.loaded)
            return settings;

        const auto now = juce::Time::currentTimeMillis();
        if (lastInfoAttempt != 0 && now - lastInfoAttempt < infoRetryMs)
            return settings;
        lastInfoAttempt = juce::jmax ((juce::int64) 1, now);

        const auto response = request ("GET", "/info", {}, {}, infoTimeoutMs);
        if (response.status == 200)
        {
            const auto info = juce::JSON::parse (response.body.toString());
            if (const auto* direction = info["direction"].getArray(); direction != nullptr && direction->size() == (int) trait::count)
            {
                for (size_t i = 0; i < trait::count; ++i)
                    settings.direction[i] = (float) (double) (*direction)[(int) i];
                settings.sigma = (float) (double) info["sigma"];
                settings.leanK = (float) (double) info["lean_k"];
                settings.leash = (float) (double) info["leash"];
                settings.configVersion = (int) info["config_version"];
                settings.loaded = true;
                DBG ("Experimental engine: server config v" << settings.configVersion);
            }
        }

        if (! settings.loaded)
        {
            DBG ("Experimental engine: no server on 127.0.0.1:" << port << " (lean has no direction until it answers)");
        }

        return settings;
    }

    Traits PretrainedVariationEngine::mutate (const Traits& parent, const MutationRequest& request, Rng& rng) const
    {
        return mutateWith (parent, request, rng, getSettings());
    }

    const juce::AudioBuffer<float>* PretrainedVariationEngine::seedAudio (const SeedSource& seed, double& sampleRate) const
    {
        if (seed.kind == SeedSource::Kind::sample)
        {
            sampleRate = seed.sampleRate;
            return &seed.audio;
        }

        // A library seed is the built-in synth: render its patch once and use that as the seed's audio.
        if (! juce::isPositiveAndBelow (seed.libraryIndex, (int) library.size()))
            return nullptr;

        const std::lock_guard<std::mutex> lock (mutex);
        auto& rendered = libraryAudio[seed.libraryIndex];
        if (rendered == nullptr)
            rendered = dsp.render (library[(size_t) seed.libraryIndex].traits, seed);
        if (rendered == nullptr)
            return nullptr;

        sampleRate = rendered->sampleRate;
        return &rendered->audio;
    }

    bool PretrainedVariationEngine::uploadSeed (const juce::String& key, const juce::AudioBuffer<float>& audio, double sampleRate) const
    {
        const auto channels = juce::jmin (2, audio.getNumChannels());
        juce::MemoryOutputStream body ((size_t) audio.getNumSamples() * (size_t) channels * 4 + 16);
        for (int i = 0; i < audio.getNumSamples(); ++i)
            for (int ch = 0; ch < channels; ++ch)
                appendFloat (body, audio.getSample (ch, i));

        juce::StringPairArray headers;
        headers.set ("X-VAE-Key", key);
        headers.set ("X-VAE-Rate", juce::String (juce::roundToInt (sampleRate)));
        headers.set ("X-VAE-Channels", juce::String (channels));

        const auto response = request ("POST", "/seed", headers, body.getMemoryBlock(), renderTimeoutMs);
        if (response.status != 200)
        {
            DBG ("Experimental engine: seed upload failed (" << response.status << ") " << response.body.toString());
        }
        return response.status == 200;
    }

    RenderedSound::Ptr PretrainedVariationEngine::render (const Traits& traits, const SeedSource& seed) const
    {
        // The seed itself sounds exactly as before.
        if (seed.kind == SeedSource::Kind::sample && isNeutral (traits))
            return dsp.render (traits, seed);
        if (seed.kind == SeedSource::Kind::library && juce::isPositiveAndBelow (seed.libraryIndex, (int) library.size())
            && sameTraits (traits, library[(size_t) seed.libraryIndex].traits))
            return dsp.render (traits, seed);

        double sampleRate = 0.0;
        const auto* audio = seedAudio (seed, sampleRate);
        if (audio == nullptr || audio->getNumSamples() == 0 || audio->getNumChannels() == 0)
            return nullptr;

        const auto key = audioKey (*audio, sampleRate);
        juce::StringArray coords;
        for (auto v : traits)
            coords.add (juce::String ((double) v, 9));

        juce::StringPairArray headers;
        headers.set ("X-VAE-Key", key);
        headers.set ("X-VAE-Coords", coords.joinIntoString (","));

        auto response = request ("POST", "/render", headers, {}, renderTimeoutMs);
        if (response.status == 404 && response.body.toString().contains ("unknown_seed"))
        {
            if (! uploadSeed (key, *audio, sampleRate))
                return nullptr;
            response = request ("POST", "/render", headers, {}, renderTimeoutMs);
        }

        if (response.status != 200)
        {
            DBG ("Experimental engine: bud not rendered (" << response.status << ") " << response.body.toString());
            return nullptr;  // the bud stays unlit
        }

        const auto channels = response.headers["X-Channels"].getIntValue();
        const auto frames = response.headers["X-Frames"].getIntValue();
        const auto rate = response.headers["X-Sample-Rate"].getDoubleValue();
        if (channels < 1 || channels > 2 || frames <= 0 || rate <= 0.0
            || response.body.getSize() != (size_t) channels * (size_t) frames * 4)
            return nullptr;

        RenderedSound::Ptr sound (new RenderedSound());
        sound->sampleRate = rate;
        sound->rootNote = seed.rootNote;
        sound->audio.setSize (channels, frames);
        const auto* data = static_cast<const char*> (response.body.getData());
        for (int i = 0; i < frames; ++i)
            for (int ch = 0; ch < channels; ++ch)
                sound->audio.setSample (ch, i, readFloat (data + ((size_t) i * (size_t) channels + (size_t) ch) * 4));
        return sound;
    }

    PretrainedVariationEngine::Response PretrainedVariationEngine::request (const juce::String& method, const juce::String& path,
                                                                            const juce::StringPairArray& headers,
                                                                            const juce::MemoryBlock& body, int timeoutMs) const
    {
        const auto deadline = juce::Time::currentTimeMillis() + timeoutMs;
        auto remaining = [deadline] { return (int) juce::jmax ((juce::int64) 0, deadline - juce::Time::currentTimeMillis()); };

        juce::StreamingSocket socket;
        if (! socket.connect ("127.0.0.1", port, juce::jmin (timeoutMs, 1000)))
            return {};

        juce::String head;
        head << method << " " << path << " HTTP/1.1\r\n"
             << "Host: 127.0.0.1:" << port << "\r\n"
             << "Connection: close\r\n"
             << "Content-Length: " << (juce::int64) body.getSize() << "\r\n";
        if (body.getSize() > 0)
            head << "Content-Type: application/octet-stream\r\n";
        for (int i = 0; i < headers.size(); ++i)
            head << headers.getAllKeys()[i] << ": " << headers.getAllValues()[i] << "\r\n";
        head << "\r\n";

        auto writeAll = [&] (const void* data, size_t size)
        {
            size_t done = 0;
            while (done < size)
            {
                if (socket.waitUntilReady (false, remaining()) != 1)
                    return false;
                const auto n = socket.write (static_cast<const char*> (data) + done, (int) juce::jmin (size - done, (size_t) 65536));
                if (n <= 0)
                    return false;
                done += (size_t) n;
            }
            return true;
        };

        if (! writeAll (head.toRawUTF8(), head.getNumBytesAsUTF8()) || ! writeAll (body.getData(), body.getSize()))
            return {};

        juce::MemoryOutputStream received;
        char buffer[65536];
        for (;;)
        {
            const auto ready = socket.waitUntilReady (true, remaining());
            if (ready == 0)
                return {};  // timeout
            if (ready < 0)
                break;
            const auto n = socket.read (buffer, (int) sizeof (buffer), false);
            if (n <= 0)
                break;  // the server closed the connection: the reply is complete
            received.write (buffer, (size_t) n);
        }

        const auto* data = static_cast<const char*> (received.getData());
        const auto size = received.getDataSize();
        size_t headerEnd = 0;
        for (size_t i = 0; i + 3 < size; ++i)
            if (data[i] == '\r' && data[i + 1] == '\n' && data[i + 2] == '\r' && data[i + 3] == '\n')
            {
                headerEnd = i;
                break;
            }
        if (headerEnd == 0)
            return {};

        Response response;
        juce::StringArray lines;
        lines.addLines (juce::String::fromUTF8 (data, (int) headerEnd));
        response.status = lines[0].fromFirstOccurrenceOf (" ", false, false).getIntValue();
        for (int i = 1; i < lines.size(); ++i)
            if (lines[i].containsChar (':'))
                response.headers.set (lines[i].upToFirstOccurrenceOf (":", false, false).trim(),
                                      lines[i].fromFirstOccurrenceOf (":", false, false).trim());

        const auto bodyStart = headerEnd + 4;
        auto bodySize = size - bodyStart;
        if (response.headers.containsKey ("Content-Length"))
        {
            const auto expected = (size_t) response.headers["Content-Length"].getLargeIntValue();
            if (bodySize < expected)
                return {};  // cut short
            bodySize = expected;
        }
        response.body.append (data + bodyStart, bodySize);
        return response;
    }
}
