#include "PlantSession.h"

#include <algorithm>
#include <cstring>
#include <map>
#include <numeric>

namespace
{
    // Exact bits, so a restored plant regrows identically.
    juce::String traitsToString (const vae::Traits& t)
    {
        juce::StringArray values;
        for (auto v : t)
        {
            std::uint32_t bits;
            std::memcpy (&bits, &v, sizeof (bits));
            values.add (juce::String::toHexString ((juce::int64) bits));
        }
        return values.joinIntoString (" ");
    }

    vae::Traits traitsFromString (const juce::String& text, const vae::Traits& fallback)
    {
        juce::StringArray values;
        values.addTokens (text, " ", {});
        if (values.size() != (int) vae::trait::count)
            return fallback;

        vae::Traits t {};
        for (size_t i = 0; i < t.size(); ++i)
        {
            const auto bits = (std::uint32_t) values[(int) i].getHexValue64();
            float v;
            std::memcpy (&v, &bits, sizeof (v));
            t[i] = std::isfinite (v) ? juce::jlimit (0.0f, 1.0f, v) : fallback[i];
        }
        return t;
    }
}

PlantSession::PlantSession (std::atomic<float>& wildParameter)
    : juce::Thread ("VAESynth render"), wild (wildParameter)
{
    formats.registerBasicFormats();
    startThread (juce::Thread::Priority::low);
    startTimer (2000);
}

PlantSession::~PlantSession()
{
    stopTimer();
    signalThreadShouldExit();
    notify();
    stopThread (4000);

    // Release anything still waiting in the audition queue.
    const auto scope = auditionFifo.read (auditionFifo.getNumReady());
    for (auto [start, size] : { std::pair { scope.startIndex1, scope.blockSize1 }, std::pair { scope.startIndex2, scope.blockSize2 } })
        for (int i = start; i < start + size; ++i)
            auditions[(size_t) i].sound->decReferenceCount();
}

std::uint32_t PlantSession::newRngSeed()
{
    return (std::uint32_t) juce::Random::getSystemRandom().nextInt64() | 1u;
}

// ---------------------------------------------------------------- actions

void PlantSession::chooseLibrarySeed (int index)
{
    auto source = vae::makeLibrarySource (index);
    history.clear();
    current = -1;
    pushGeneration ({ source, vae::library[(size_t) source->libraryIndex].traits, wild.load(), newRngSeed(), source->name });
}

bool PlantSession::loadSeedFile (const juce::File& file, juce::String& error)
{
    auto source = vae::loadSampleSource (formats, file, error);
    if (source == nullptr)
        return false;

    history.clear();
    current = -1;
    pushGeneration ({ source, vae::sampleNeutral, wild.load(), newRngSeed(), source->name });
    return true;
}

void PlantSession::plantNode (int nodeIndex)
{
    if (! hasPlant() || nodeIndex <= 0 || nodeIndex >= (int) plant.nodes.size())
        return;

    const auto& source = history[(size_t) current].source;
    pushGeneration ({ source, plant.nodes[(size_t) nodeIndex].traits, wild.load(), newRngSeed(), describeNode (nodeIndex) });
}

void PlantSession::regrow()
{
    if (! hasPlant())
        return;

    auto& generation = history[(size_t) current];
    generation.rngSeed = newRngSeed();
    generation.wild = wild.load();
    growCurrent();
}

void PlantSession::applyWild()
{
    if (! hasPlant())
        return;

    // The same rngSeed draws the same branches and the same directions, so only
    // how far each bud moves changes, and the picked bud stays the same bud.
    auto& generation = history[(size_t) current];
    if (juce::approximatelyEqual (generation.wild, wild.load()))
        return;

    generation.wild = wild.load();
    growCurrent (true);
}

void PlantSession::goTo (int generationIndex)
{
    if (generationIndex < 0 || generationIndex >= (int) history.size() || generationIndex == current)
        return;

    current = generationIndex;
    growCurrent();
}

void PlantSession::select (int nodeIndex)
{
    selected = hasPlant() && nodeIndex > 0 && nodeIndex < (int) plant.nodes.size() ? nodeIndex : -1;

    const juce::ScopedLock sl (lock);
    playableNode = juce::jmax (0, selected);
    publishPlayable();
}

void PlantSession::audition (int nodeIndex, int note, float velocity, float pan)
{
    vae::RenderedSound::Ptr sound;
    {
        const juce::ScopedLock sl (lock);
        if (nodeIndex < 0 || nodeIndex >= (int) sounds.size())
            return;
        sound = sounds[(size_t) nodeIndex];
    }

    if (sound == nullptr || auditionFifo.getFreeSpace() == 0)
        return;

    // The queue entry owns one reference until the audio thread takes it.
    sound->incReferenceCount();
    const auto scope = auditionFifo.write (1);
    const auto index = scope.blockSize1 > 0 ? scope.startIndex1 : scope.startIndex2;
    auditions[(size_t) index] = { sound.get(), note, velocity, pan };
}

// ---------------------------------------------------------------- queries

int PlantSession::getHomeNote() const noexcept
{
    return hasPlant() ? history[(size_t) current].source->rootNote : 60;
}

bool PlantSession::isRendered (int nodeIndex) const
{
    const juce::ScopedLock sl (lock);
    return nodeIndex >= 0 && nodeIndex < (int) sounds.size() && sounds[(size_t) nodeIndex] != nullptr;
}

juce::String PlantSession::describeNode (int nodeIndex) const
{
    if (! hasPlant() || nodeIndex < 0 || nodeIndex >= (int) plant.nodes.size())
        return {};

    if (nodeIndex == 0)
        return history[(size_t) current].name;

    return vae::describeDifference (plant.nodes[(size_t) nodeIndex].traits, plant.nodes[0].traits);
}

// ---------------------------------------------------------------- growing and rendering

void PlantSession::pushGeneration (Generation generation)
{
    history.resize ((size_t) (current + 1));
    history.push_back (std::move (generation));
    current = (int) history.size() - 1;
    growCurrent();
}

void PlantSession::growCurrent (bool keepSelection)
{
    const auto& generation = history[(size_t) current];
    plant = vae::Plant::grow (generation.traits, generation.wild, generation.rngSeed, engine);
    if (! keepSelection || selected >= (int) plant.nodes.size())
        selected = -1;
    ++plantVersion;
    if (! keepSelection)
        ++shapeVersion;

    // Render in the order the buds appear, so the seed is ready first.
    std::vector<int> order ((size_t) plant.nodes.size());
    std::iota (order.begin(), order.end(), 0);
    std::stable_sort (order.begin(), order.end(), [this] (int a, int b)
    {
        return plant.nodes[(size_t) a].born < plant.nodes[(size_t) b].born;
    });

    std::vector<vae::Traits> traits;
    for (const auto& node : plant.nodes)
        traits.push_back (node.traits);

    {
        const juce::ScopedLock sl (lock);
        ++renderJob;
        jobSource = generation.source;
        jobTraits = std::move (traits);
        jobOrder = std::move (order);
        sounds.assign (plant.nodes.size(), nullptr);
        playableNode = juce::jmax (0, selected);
        publishPlayable();
    }

    notify();
}

void PlantSession::run()
{
    int handledJob = 0;

    while (! threadShouldExit())
    {
        int job;
        vae::SeedSourcePtr source;
        std::vector<vae::Traits> traits;
        std::vector<int> order;
        {
            const juce::ScopedLock sl (lock);
            job = renderJob;
            source = jobSource;
            traits = jobTraits;
            order = jobOrder;
        }

        if (job == handledJob || source == nullptr)
        {
            wait (-1);
            continue;
        }

        for (auto index : order)
        {
            if (threadShouldExit())
                return;

            {
                const juce::ScopedLock sl (lock);
                if (renderJob != job)
                    break;
            }

            auto sound = engine.render (traits[(size_t) index], *source);

            const juce::ScopedLock sl (lock);
            if (renderJob != job)
                break;

            sounds[(size_t) index] = sound;
            keepAlive.push_back (sound);
            if (index == playableNode)
                publishPlayable();
        }

        handledJob = job;
    }
}

void PlantSession::publishPlayable()
{
    // Called with `lock` held.
    vae::RenderedSound::Ptr sound;
    if (playableNode < (int) sounds.size())
        sound = sounds[(size_t) playableNode];

    const juce::SpinLock::ScopedLockType sl (playableLock);
    playable = sound;
}

vae::RenderedSound::Ptr PlantSession::getPlayableSound()
{
    const juce::SpinLock::ScopedTryLockType sl (playableLock);
    return sl.isLocked() ? playable : nullptr;
}

void PlantSession::timerCallback()
{
    // Sounds nobody uses any more are freed here, on the message thread.
    const juce::ScopedLock sl (lock);
    keepAlive.erase (std::remove_if (keepAlive.begin(), keepAlive.end(),
                                     [] (const vae::RenderedSound::Ptr& s) { return s->getReferenceCount() == 1; }),
                     keepAlive.end());
}

bool PlantSession::waitUntilRendered (int timeoutMs)
{
    const auto deadline = juce::Time::getMillisecondCounter() + (juce::uint32) timeoutMs;
    while (juce::Time::getMillisecondCounter() < deadline)
    {
        {
            const juce::ScopedLock sl (lock);
            if (std::all_of (sounds.begin(), sounds.end(), [] (const auto& s) { return s != nullptr; }))
                return true;
        }
        juce::Thread::sleep (5);
    }
    return false;
}

// ---------------------------------------------------------------- session state

juce::ValueTree PlantSession::toValueTree() const
{
    juce::ValueTree tree ("Plant");
    tree.setProperty ("current", current, nullptr);
    tree.setProperty ("selected", selected, nullptr);

    // Each sound file is stored once, with its audio, and generations point to it.
    std::map<const vae::SeedSource*, int> sourceIds;
    for (const auto& g : history)
    {
        if (g.source->kind != vae::SeedSource::Kind::sample || sourceIds.count (g.source.get()) > 0)
            continue;

        const auto id = (int) sourceIds.size();
        sourceIds[g.source.get()] = id;

        juce::ValueTree source ("Source");
        source.setProperty ("id", id, nullptr);
        source.setProperty ("name", g.source->name, nullptr);
        source.setProperty ("file", g.source->file.getFullPathName(), nullptr);
        source.setProperty ("root", g.source->rootNote, nullptr);
        source.setProperty ("pitched", g.source->pitched, nullptr);
        source.setProperty ("audio", g.source->embeddedAudio, nullptr);
        tree.appendChild (source, nullptr);
    }

    for (const auto& g : history)
    {
        juce::ValueTree child ("Generation");
        const auto isSample = g.source->kind == vae::SeedSource::Kind::sample;
        if (isSample)
            child.setProperty ("source", sourceIds[g.source.get()], nullptr);
        child.setProperty ("kind", isSample ? "sample" : "library", nullptr);
        child.setProperty ("library", g.source->libraryIndex, nullptr);
        child.setProperty ("file", isSample ? g.source->file.getFullPathName() : juce::String(), nullptr);
        child.setProperty ("root", g.source->rootNote, nullptr);
        child.setProperty ("traits", traitsToString (g.traits), nullptr);
        child.setProperty ("wild", g.wild, nullptr);
        child.setProperty ("rng", (juce::int64) g.rngSeed, nullptr);
        child.setProperty ("name", g.name, nullptr);
        tree.appendChild (child, nullptr);
    }

    return tree;
}

void PlantSession::restore (const juce::ValueTree& tree)
{
    if (! tree.hasType ("Plant") || tree.getChildWithName ("Generation").isValid() == false)
        return;

    std::vector<Generation> restored;
    std::map<juce::String, vae::SeedSourcePtr> loaded;

    // Sounds saved inside the session: they play even if the file is gone.
    std::map<int, vae::SeedSourcePtr> embedded;
    for (const auto& source : tree)
        if (source.hasType ("Source"))
            embedded[(int) source["id"]] = vae::decodeSampleSource (source["audio"].toString(), source["name"].toString(),
                                                                     juce::File (source["file"].toString()),
                                                                     (int) source.getProperty ("root", 60), (bool) source["pitched"]);

    for (const auto& child : tree)
    {
        if (! child.hasType ("Generation"))
            continue;

        Generation g;
        if (child["kind"].toString() == "sample")
        {
            const auto path = child["file"].toString();
            if (child.hasProperty ("source") && embedded[(int) child["source"]] != nullptr)
                loaded[path] = embedded[(int) child["source"]];

            if (loaded.count (path) == 0)
            {
                juce::String error;
                auto source = vae::loadSampleSource (formats, juce::File (path), error);
                if (source != nullptr)
                    source->rootNote = (int) child.getProperty ("root", source->rootNote);
                loaded[path] = source;
            }

            g.source = loaded[path];
            if (g.source == nullptr)
            {
                // The file moved or was deleted: fall back to a library seed.
                auto fallback = vae::makeLibrarySource (0);
                fallback->name = "Missing: " + juce::File (path).getFileName();
                g.source = fallback;
                g.traits = vae::library[0].traits;
            }
            else
            {
                g.traits = traitsFromString (child["traits"].toString(), vae::sampleNeutral);
            }
        }
        else
        {
            g.source = vae::makeLibrarySource ((int) child["library"]);
            g.traits = traitsFromString (child["traits"].toString(), vae::library[(size_t) g.source->libraryIndex].traits);
        }

        g.wild = juce::jlimit (0.0f, 1.0f, (float) child.getProperty ("wild", 0.5f));
        g.rngSeed = (std::uint32_t) (juce::int64) child.getProperty ("rng", 1);
        g.name = child.getProperty ("name", g.source->name).toString();
        if (g.source->name.startsWith ("Missing: "))
            g.name = g.source->name;
        restored.push_back (std::move (g));
    }

    history = std::move (restored);
    current = juce::jlimit (0, (int) history.size() - 1, (int) tree.getProperty ("current", 0));
    growCurrent();
    select ((int) tree.getProperty ("selected", -1));
}
