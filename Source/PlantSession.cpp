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
    scheduleNotes();
    publishPlayable();
    notify();
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

    pushAudition (sound, note, velocity, pan);
}

void PlantSession::auditionRebuiltSeed()
{
    vae::RenderedSound::Ptr sound;
    {
        const juce::ScopedLock sl (lock);
        sound = rebuiltSeed;
    }

    pushAudition (sound, getHomeNote(), 0.8f, 0.0f);
}

void PlantSession::setEngine (Engine kind)
{
    if (kind == engineKind)
        return;

    // Same plant, same picked bud, rendered by the other engine.
    engineKind = kind;
    if (hasPlant())
        growCurrent (true);
}

const vae::VariationEngine& PlantSession::activeEngine() const
{
    if (engineKind == Engine::recipe)
        return recipeEngine;
    return engine;
}

void PlantSession::pushAudition (const vae::RenderedSound::Ptr& sound, int note, float velocity, float pan)
{
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
    plant = vae::Plant::grow (generation.traits, generation.wild, generation.rngSeed, activeEngine());
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

    // With the recipe engine, the rebuilt seed comes right after the seed.
    if (engineKind == Engine::recipe)
        order.insert (order.begin() + 1, -1);

    {
        const juce::ScopedLock sl (lock);
        ++renderJob;
        jobEngine = &activeEngine();
        jobSource = generation.source;
        jobTraits = std::move (traits);
        jobOrder = std::move (order);
        budCursor = 0;
        rebuiltSeed = nullptr;
        sounds.assign (plant.nodes.size(), nullptr);
        playableNode = juce::jmax (0, selected);
        scheduleNotes();
        publishPlayable();
    }

    notify();
}

void PlantSession::scheduleNotes()
{
    // Called with `lock` held. The notes around home, nearest first, so what
    // you are most likely to play is ready first.
    ++noteJob;
    noteSounds.fill (nullptr);
    noteOrder.clear();
    noteCursor = notesDone = 0;

    if (jobEngine == nullptr || ! jobEngine->rendersEveryNote() || playableNode >= (int) jobTraits.size())
        return;

    noteTraits = jobTraits[(size_t) playableNode];
    const auto home = getHomeNote();
    for (int distance = 0; distance <= 24; ++distance)
        for (auto note : { home + distance, home - distance })
            if (note >= 0 && note < 128 && (distance > 0 || note == home) && std::find (noteOrder.begin(), noteOrder.end(), note) == noteOrder.end())
                noteOrder.push_back (note);
}

void PlantSession::run()
{
    while (! threadShouldExit())
    {
        // One task at a time, picked under the lock: notes of the picked bud
        // first (they are what MIDI plays), then the buds.
        enum class Task { none, note, bud, rebuilt };
        auto task = Task::none;
        int job = 0, index = -1, note = 0;
        const vae::VariationEngine* taskEngine = nullptr;
        vae::SeedSourcePtr source;
        vae::Traits traits {};
        {
            const juce::ScopedLock sl (lock);
            if (jobSource != nullptr && noteCursor < noteOrder.size())
            {
                task = Task::note;
                job = noteJob;
                note = noteOrder[noteCursor++];
                traits = noteTraits;
            }
            else if (jobSource != nullptr && budCursor < jobOrder.size())
            {
                job = renderJob;
                index = jobOrder[budCursor++];
                task = index < 0 ? Task::rebuilt : Task::bud;
                if (index >= 0)
                    traits = jobTraits[(size_t) index];
            }
            taskEngine = jobEngine;
            source = jobSource;
        }

        if (task == Task::none)
        {
            wait (-1);
            continue;
        }

        vae::RenderedSound::Ptr sound;
        if (task == Task::note)             sound = taskEngine->renderNote (traits, *source, note);
        else if (task == Task::rebuilt)     sound = recipeEngine.renderRebuiltSeed (*source);
        else                                sound = taskEngine->render (traits, *source);

        const juce::ScopedLock sl (lock);

        if (task == Task::note)
        {
            if (job != noteJob)
                continue;
            ++notesDone;
            if (sound == nullptr)
                continue;
            noteSounds[(size_t) note] = sound;
            keepAlive.push_back (sound);
            publishPlayable();
            continue;
        }

        // An engine that could not render this bud leaves it unlit and silent.
        if (job != renderJob || sound == nullptr)
            continue;

        keepAlive.push_back (sound);
        if (task == Task::rebuilt)
        {
            rebuiltSeed = sound;
            continue;
        }

        sounds[(size_t) index] = sound;
        if (index == playableNode)
            publishPlayable();
    }
}

void PlantSession::publishPlayable()
{
    // Called with `lock` held. A new set every time: the audio thread may
    // still be reading the previous one, which `keepAliveSets` keeps alive.
    PlayableSounds::Ptr set (new PlayableSounds());
    if (playableNode < (int) sounds.size())
        set->base = sounds[(size_t) playableNode];
    set->notes = noteSounds;
    keepAliveSets.push_back (set);

    const juce::SpinLock::ScopedLockType sl (playableLock);
    playable = set;
}

PlantSession::PlayableSounds::Ptr PlantSession::getPlayableSounds()
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
    keepAliveSets.erase (std::remove_if (keepAliveSets.begin(), keepAliveSets.end(),
                                         [] (const PlayableSounds::Ptr& s) { return s->getReferenceCount() == 1; }),
                         keepAliveSets.end());
}

bool PlantSession::waitUntilRendered (int timeoutMs)
{
    const auto deadline = juce::Time::getMillisecondCounter() + (juce::uint32) timeoutMs;
    while (juce::Time::getMillisecondCounter() < deadline)
    {
        {
            const juce::ScopedLock sl (lock);
            if (std::all_of (sounds.begin(), sounds.end(), [] (const auto& s) { return s != nullptr; })
                && notesDone >= noteOrder.size()
                && (engineKind != Engine::recipe || rebuiltSeed != nullptr))
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
    tree.setProperty ("engine", engineKind == Engine::recipe ? "recipe" : "effects", nullptr);

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
    engineKind = tree["engine"].toString() == "recipe" ? Engine::recipe : Engine::effects;
    current = juce::jlimit (0, (int) history.size() - 1, (int) tree.getProperty ("current", 0));
    growCurrent();
    select ((int) tree.getProperty ("selected", -1));
}
