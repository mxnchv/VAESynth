#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace
{
    float wrapDegrees (double degrees)
    {
        auto wrapped = std::fmod (degrees + 180.0, 360.0);
        if (wrapped < 0.0)
            wrapped += 360.0;
        return (float) (wrapped - 180.0);
    }

    const juce::Identifier libraryType { "Library" };
    const juce::Identifier sampleType  { "Sample" };

    // A trajectory "cycle" is 10 units of trajectory time: one full orbit at 1x
    // takes 10 seconds, and a synced cycle lasts the chosen number of bars.
    constexpr double cycleUnits = 10.0;

    // Removed samples stay in memory this long after nothing references them,
    // so a grain that was already playing can finish.
    constexpr juce::uint32 retiredGraceMs = 5000;
}

VAESynthProcessor::VAESynthProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      state (*this, nullptr, "VAESynth", createParameterLayout())
{
    auto param = [this] (const juce::ParameterID& id) { return state.getRawParameterValue (id.getParamID()); };

    azimuth          = param (ParamIDs::azimuth);
    elevation        = param (ParamIDs::elevation);
    radius           = param (ParamIDs::radius);
    trajectoryMode   = param (ParamIDs::trajectoryMode);
    trajectorySpeed  = param (ParamIDs::trajectorySpeed);
    speedMode        = param (ParamIDs::speedMode);
    speedSync        = param (ParamIDs::speedSync);
    grainSize        = param (ParamIDs::grainSize);
    sizeMode         = param (ParamIDs::sizeMode);
    sizeSync         = param (ParamIDs::sizeSync);
    density          = param (ParamIDs::density);
    densityMode      = param (ParamIDs::densityMode);
    densitySync      = param (ParamIDs::densitySync);
    spray            = param (ParamIDs::spray);
    shape            = param (ParamIDs::shape);
    neighbours       = param (ParamIDs::neighbours);
    keyMode          = param (ParamIDs::keyMode);
    detune           = param (ParamIDs::detune);
    scaleRoot        = param (ParamIDs::scaleRoot);
    scaleType        = param (ParamIDs::scaleType);
    harmonicFilter   = param (ParamIDs::harmonicFilter);
    attack           = param (ParamIDs::attack);
    decay            = param (ParamIDs::decay);
    sustain          = param (ParamIDs::sustain);
    release          = param (ParamIDs::release);
    width            = param (ParamIDs::width);
    gain             = param (ParamIDs::gain);
    velocityToRadius = param (ParamIDs::velocityToRadius);

    formats.registerBasicFormats();
    engine.setSampleSlots (&slots);

    startTimerHz (2);
}

VAESynthProcessor::~VAESynthProcessor()
{
    stopTimer();
    loader.removeAllJobs (true, 10000);
}

//==============================================================================
void VAESynthProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    engine.prepare (sampleRate, samplesPerBlock);
    outputGain.reset (sampleRate, 0.02);
    outputGain.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (gain->load()));
}

bool VAESynthProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& out = layouts.getMainOutputChannelSet();
    return out == juce::AudioChannelSet::mono() || out == juce::AudioChannelSet::stereo();
}

void VAESynthProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    buffer.clear();

    {
        const juce::SpinLock::ScopedTryLockType lock (mapSwapLock);

        if (lock.isLocked() && pendingMap != nullptr)
        {
            audioMap = pendingMap;
            pendingMap = nullptr;
            engine.setMap (audioMap.get());
        }
    }

    // Host transport. Without a host (or a tempo) everything runs at 120 BPM.
    double tempo = 120.0, ppq = 0.0, beatsPerBar = 4.0;
    bool playing = false;

    if (auto* transport = getPlayHead())
    {
        if (const auto position = transport->getPosition())
        {
            if (const auto bpm = position->getBpm(); bpm.hasValue() && *bpm > 0.0)
                tempo = *bpm;

            if (const auto beats = position->getPpqPosition())
            {
                ppq = *beats;
                playing = position->getIsPlaying();
            }

            if (const auto signature = position->getTimeSignature(); signature.hasValue() && signature->denominator > 0)
                beatsPerBar = signature->numerator * 4.0 / signature->denominator;
        }
    }

    liveTempo.store (tempo);

    auto az = azimuth->load();
    auto el = elevation->load();
    auto rad = radius->load();

    const auto mode = (TrajectoryMode) juce::roundToInt (trajectoryMode->load());

    if (mode != TrajectoryMode::manual)
    {
        if ((ClockMode) juce::roundToInt (speedMode->load()) == ClockMode::sync)
        {
            const auto cycleBeats = CycleLengths::bars (juce::roundToInt (speedSync->load())) * beatsPerBar;

            // Locked to the song position while playing, so the same bar always
            // finds the cursor in the same place.
            if (playing)
                trajectoryTime = ppq / cycleBeats * cycleUnits;
            else
                trajectoryTime += buffer.getNumSamples() / getSampleRate() * cycleUnits / (cycleBeats * 60.0 / tempo);
        }
        else
        {
            trajectoryTime += buffer.getNumSamples() / getSampleRate() * trajectorySpeed->load();
        }

        const auto t = trajectoryTime;
        az = wrapDegrees (az + 36.0 * t);

        if (mode == TrajectoryMode::lissajous)
        {
            el = juce::jlimit (-90.0f, 90.0f, el + 60.0f * (float) std::sin (0.7 * t + 1.0));
            rad = rad * (float) std::sin (0.9 * t);
        }
    }

    liveAzimuth.store (az);
    liveElevation.store (el);
    liveRadius.store (rad);

    const auto settings = readSettings (vae::directionFromAngles (az, el), rad, tempo, ppq, playing);

    if (const auto auditioned = pendingAudition.exchange (-1); auditioned >= 0)
        engine.audition (auditioned, settings);

    engine.process (buffer, midi, settings);

    outputGain.setTargetValue (juce::Decibels::decibelsToGain (gain->load()));
    outputGain.applyGain (buffer, buffer.getNumSamples());
}

vae::EngineSettings VAESynthProcessor::readSettings (vae::Vec3 direction, float rad, double tempo, double ppq, bool playing) const
{
    const auto beatMs = 60000.0 / tempo;
    const auto synced = [] (std::atomic<float>* modeParam) { return (ClockMode) juce::roundToInt (modeParam->load()) == ClockMode::sync; };

    vae::EngineSettings s;
    s.direction        = direction;
    s.radius           = rad;
    s.grainSizeMs      = synced (sizeMode) ? (float) (NoteDivisions::beats (juce::roundToInt (sizeSync->load())) * beatMs)
                                           : grainSize->load();
    s.density          = density->load();
    s.clock.synced          = synced (densityMode);
    s.clock.hostPlaying     = playing;
    s.clock.ppqAtBlockStart = ppq;
    s.clock.samplesPerBeat  = getSampleRate() * 60.0 / tempo;
    s.clock.divisionBeats   = NoteDivisions::beats (juce::roundToInt (densitySync->load()));
    s.spray            = spray->load();
    s.shape            = shape->load();
    s.neighbours       = juce::roundToInt (neighbours->load());
    s.pitchMode        = (vae::PitchMode) juce::roundToInt (keyMode->load());
    s.scale            = vae::Scale::fromIndices (juce::roundToInt (scaleRoot->load()), juce::roundToInt (scaleType->load()));
    s.harmonicFilter   = harmonicFilter->load();
    s.detuneCents      = detune->load();
    s.width            = width->load();
    s.velocityToRadius = velocityToRadius->load();
    s.envelope         = { attack->load() * 0.001f, decay->load() * 0.001f, sustain->load(), release->load() * 0.001f };
    return s;
}

VAESynthProcessor::Cursor VAESynthProcessor::getLiveCursor() const noexcept
{
    return { liveAzimuth.load(), liveElevation.load(), liveRadius.load() };
}

juce::AudioProcessorEditor* VAESynthProcessor::createEditor()
{
    return new VAESynthEditor (*this);
}

//==============================================================================
bool VAESynthProcessor::isSupportedAudioFile (const juce::File& file)
{
    return file.hasFileExtension ("wav;aif;aiff;flac;ogg;mp3;caf");
}

void VAESynthProcessor::addSampleFiles (const juce::Array<juce::File>& files)
{
    for (const auto& file : files)
        if (isSupportedAudioFile (file))
            queueLoad ({ file });
}

void VAESynthProcessor::queueLoad (const LoadRequest& request)
{
    ++pendingJobs;
    loader.addJob ([this, request]
    {
        loadFile (request);
        --pendingJobs;
    });
}

void VAESynthProcessor::loadFile (const LoadRequest& request)
{
    juce::String error;
    auto source = vae::loadAndAnalyse (formats, request.file, error);

    const juce::ScopedLock sl (libraryLock);

    if (source == nullptr || source->grains.empty())
    {
        lastError = error;
        return;
    }

    const auto freeSlot = std::find (slotUsed.begin(), slotUsed.end(), false);

    if ((int) library.size() >= maxSamples || freeSlot == slotUsed.end())
    {
        lastError = "Sample limit reached (" + juce::String (maxSamples) + ")";
        return;
    }

    source->slot = (int) std::distance (slotUsed.begin(), freeSlot);
    *freeSlot = true;

    const auto root = request.rootNote >= 0 ? request.rootNote : (source->detectedRoot >= 0 ? source->detectedRoot : 60);
    slots.reset (source->slot, root);
    slots.muted[(size_t) source->slot].store (request.muted);
    slots.soloed[(size_t) source->slot].store (request.soloed);

    library.push_back (std::move (source));

    auto map = vae::GrainMap::build (library, frozenProjection.valid ? &frozenProjection : nullptr);

    // Files dropped together are placed together; the map is locked once the
    // last of them is in, so samples added later never move the existing ones.
    if (! frozenProjection.valid && pendingJobs.load() <= 1 && map->getProjection().valid)
        frozenProjection = map->getProjection();

    publishMap (map);
}

void VAESynthProcessor::removeSample (const vae::SampleSourcePtr& source)
{
    ++pendingJobs;
    loader.addJob ([this, source]
    {
        {
            const juce::ScopedLock sl (libraryLock);
            const auto it = std::find (library.begin(), library.end(), source);

            if (it != library.end())
            {
                retire (*it);
                library.erase (it);

                // An empty library starts over: the next samples get a fresh map.
                if (library.empty())
                    frozenProjection = {};

                publishMap (vae::GrainMap::build (library, frozenProjection.valid ? &frozenProjection : nullptr));
            }
        }

        --pendingJobs;
    });
}

void VAESynthProcessor::clearSamples()
{
    ++pendingJobs;
    loader.addJob ([this]
    {
        {
            const juce::ScopedLock sl (libraryLock);
            for (const auto& source : library)
                retire (source);

            library.clear();
            frozenProjection = {};
            lastError = {};
            publishMap (vae::GrainMap::build ({}, nullptr));
        }

        --pendingJobs;
    });
}

void VAESynthProcessor::retire (const vae::SampleSourcePtr& source)
{
    // libraryLock is held by the caller
    slotUsed[(size_t) source->slot] = false;
    slots.reset (source->slot);
    retired.push_back ({ source, 0 });
}

void VAESynthProcessor::rebuildMap()
{
    ++pendingJobs;
    loader.addJob ([this]
    {
        {
            const juce::ScopedLock sl (libraryLock);
            auto map = vae::GrainMap::build (library, nullptr);
            frozenProjection = map->getProjection();
            publishMap (map);
        }

        --pendingJobs;
    });
}

void VAESynthProcessor::publishMap (vae::GrainMap::Ptr map)
{
    // libraryLock is held by the caller
    liveMaps.add (map);
    latestMap = map;

    const juce::SpinLock::ScopedLockType lock (mapSwapLock);
    pendingMap = map;
}

void VAESynthProcessor::timerCallback()
{
    const juce::ScopedLock sl (libraryLock);

    // A count of 1 means only liveMaps still holds the map, so the audio
    // thread is done with it and it can be freed here.
    for (int i = liveMaps.size(); --i >= 0;)
        if (liveMaps[i] != latestMap && liveMaps[i]->getReferenceCount() == 1)
            liveMaps.remove (i);

    const auto now = juce::Time::getMillisecondCounter();

    for (auto it = retired.begin(); it != retired.end();)
    {
        if (it->source.use_count() > 1)
        {
            it->unusedSince = 0;
            ++it;
        }
        else if (it->unusedSince == 0)
        {
            it->unusedSince = juce::jmax (1u, now);
            ++it;
        }
        else if (now - it->unusedSince > retiredGraceMs)
        {
            it = retired.erase (it);
        }
        else
        {
            ++it;
        }
    }
}

vae::GrainMap::Ptr VAESynthProcessor::getMap() const
{
    const juce::ScopedLock sl (libraryLock);
    return latestMap;
}

juce::String VAESynthProcessor::getLastError() const
{
    const juce::ScopedLock sl (libraryLock);
    return lastError;
}

int VAESynthProcessor::getRootNote (int slot) const
{
    return juce::isPositiveAndBelow (slot, vae::SampleSlots::count) ? slots.rootNote[(size_t) slot].load() : 60;
}

void VAESynthProcessor::setRootNote (int slot, int midiNote)
{
    if (juce::isPositiveAndBelow (slot, vae::SampleSlots::count))
        slots.rootNote[(size_t) slot].store (juce::jlimit (0, 127, midiNote));
}

bool VAESynthProcessor::isMuted (int slot) const
{
    return juce::isPositiveAndBelow (slot, vae::SampleSlots::count) && slots.muted[(size_t) slot].load();
}

void VAESynthProcessor::setMuted (int slot, bool shouldBeMuted)
{
    if (juce::isPositiveAndBelow (slot, vae::SampleSlots::count))
        slots.muted[(size_t) slot].store (shouldBeMuted);
}

bool VAESynthProcessor::isSoloed (int slot) const
{
    return juce::isPositiveAndBelow (slot, vae::SampleSlots::count) && slots.soloed[(size_t) slot].load();
}

void VAESynthProcessor::setSoloed (int slot, bool shouldBeSoloed)
{
    if (juce::isPositiveAndBelow (slot, vae::SampleSlots::count))
        slots.soloed[(size_t) slot].store (shouldBeSoloed);
}

//==============================================================================
void VAESynthProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto tree = state.copyState();
    tree.removeChild (tree.getChildWithName (libraryType), nullptr);

    juce::ValueTree libraryTree (libraryType);

    {
        const juce::ScopedLock sl (libraryLock);
        libraryTree.appendChild (frozenProjection.toValueTree(), nullptr);

        for (const auto& source : library)
        {
            juce::ValueTree sample (sampleType);
            sample.setProperty ("path", source->file.getFullPathName(), nullptr);
            sample.setProperty ("root", slots.rootNote[(size_t) source->slot].load(), nullptr);
            sample.setProperty ("muted", slots.muted[(size_t) source->slot].load(), nullptr);
            sample.setProperty ("soloed", slots.soloed[(size_t) source->slot].load(), nullptr);
            libraryTree.appendChild (sample, nullptr);
        }
    }

    tree.appendChild (libraryTree, nullptr);

    if (auto xml = tree.createXml())
        copyXmlToBinary (*xml, destData);
}

void VAESynthProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    auto xml = getXmlFromBinary (data, sizeInBytes);

    if (xml == nullptr || ! xml->hasTagName (state.state.getType()))
        return;

    auto tree = juce::ValueTree::fromXml (*xml);
    const auto libraryTree = tree.getChildWithName (libraryType).createCopy();
    tree.removeChild (tree.getChildWithName (libraryType), nullptr);
    state.replaceState (tree);

    loader.removeAllJobs (true, 10000);
    pendingJobs.store (0);

    {
        const juce::ScopedLock sl (libraryLock);
        for (const auto& source : library)
            retire (source);

        library.clear();
        lastError = {};
        frozenProjection = vae::Projection::fromValueTree (libraryTree.getChildWithName ("Projection"));
        publishMap (vae::GrainMap::build ({}, nullptr));
    }

    for (const auto& sample : libraryTree)
    {
        if (! sample.hasType (sampleType))
            continue;

        const juce::File file (sample.getProperty ("path").toString());

        if (file.existsAsFile())
        {
            queueLoad ({ file, (int) sample.getProperty ("root", 60), (bool) sample.getProperty ("muted", false),
                         (bool) sample.getProperty ("soloed", false) });
        }
        else
        {
            const juce::ScopedLock sl (libraryLock);
            lastError = "Missing sample: " + file.getFullPathName();
        }
    }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new VAESynthProcessor();
}
