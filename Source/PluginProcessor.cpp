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
    grainSize        = param (ParamIDs::grainSize);
    density          = param (ParamIDs::density);
    spray            = param (ParamIDs::spray);
    shape            = param (ParamIDs::shape);
    neighbours       = param (ParamIDs::neighbours);
    keyMode          = param (ParamIDs::keyMode);
    detune           = param (ParamIDs::detune);
    attack           = param (ParamIDs::attack);
    decay            = param (ParamIDs::decay);
    sustain          = param (ParamIDs::sustain);
    release          = param (ParamIDs::release);
    width            = param (ParamIDs::width);
    gain             = param (ParamIDs::gain);
    velocityToRadius = param (ParamIDs::velocityToRadius);

    for (auto& root : rootNotes)
        root.store (60);

    formats.registerBasicFormats();
    engine.setRootNotes (&rootNotes);

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

    auto az = azimuth->load();
    auto el = elevation->load();
    auto rad = radius->load();

    const auto mode = (TrajectoryMode) juce::roundToInt (trajectoryMode->load());

    if (mode != TrajectoryMode::manual)
    {
        trajectoryTime += buffer.getNumSamples() / getSampleRate() * trajectorySpeed->load();
        const auto t = trajectoryTime;

        if (mode == TrajectoryMode::orbit)
        {
            az = wrapDegrees (az + 36.0 * t);
        }
        else
        {
            az = wrapDegrees (az + 40.0 * t);
            el = juce::jlimit (-90.0f, 90.0f, el + 60.0f * (float) std::sin (0.7 * t + 1.0));
            rad = rad * (float) std::sin (0.9 * t);
        }
    }

    liveAzimuth.store (az);
    liveElevation.store (el);
    liveRadius.store (rad);

    engine.process (buffer, midi, readSettings (vae::directionFromAngles (az, el), rad));

    outputGain.setTargetValue (juce::Decibels::decibelsToGain (gain->load()));
    outputGain.applyGain (buffer, buffer.getNumSamples());
}

vae::EngineSettings VAESynthProcessor::readSettings (vae::Vec3 direction, float rad) const
{
    vae::EngineSettings s;
    s.direction        = direction;
    s.radius           = rad;
    s.grainSizeMs      = grainSize->load();
    s.density          = density->load();
    s.spray            = spray->load();
    s.shape            = shape->load();
    s.neighbours       = juce::roundToInt (neighbours->load());
    s.transpose        = (KeyMode) juce::roundToInt (keyMode->load()) == KeyMode::transpose;
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
    return file.hasFileExtension ("wav;aif;aiff;flac;ogg;mp3");
}

void VAESynthProcessor::addSampleFiles (const juce::Array<juce::File>& files)
{
    for (const auto& file : files)
        if (isSupportedAudioFile (file))
            queueLoad (file, 60);
}

void VAESynthProcessor::queueLoad (const juce::File& file, int rootNote)
{
    ++pendingJobs;
    loader.addJob ([this, file, rootNote]
    {
        loadFile (file, rootNote);
        --pendingJobs;
    });
}

void VAESynthProcessor::loadFile (const juce::File& file, int rootNote)
{
    juce::String error;
    auto source = vae::loadAndAnalyse (formats, file, error);

    const juce::ScopedLock sl (libraryLock);

    if (source == nullptr || source->grains.empty())
    {
        lastError = error;
        return;
    }

    if ((int) library.size() >= maxSamples)
    {
        lastError = "Sample limit reached (" + juce::String (maxSamples) + ")";
        return;
    }

    rootNotes[library.size()].store (rootNote);
    library.push_back (std::move (source));

    auto map = vae::GrainMap::build (library, frozenProjection.valid ? &frozenProjection : nullptr);

    // Files dropped together are placed together; the map is locked once the
    // last of them is in, so samples added later never move the existing ones.
    if (! frozenProjection.valid && pendingJobs.load() <= 1 && map->getProjection().valid)
        frozenProjection = map->getProjection();

    publishMap (map);
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

int VAESynthProcessor::getRootNote (int sourceIndex) const
{
    return juce::isPositiveAndBelow (sourceIndex, (int) rootNotes.size()) ? rootNotes[(size_t) sourceIndex].load() : 60;
}

void VAESynthProcessor::setRootNote (int sourceIndex, int midiNote)
{
    if (juce::isPositiveAndBelow (sourceIndex, (int) rootNotes.size()))
        rootNotes[(size_t) sourceIndex].store (juce::jlimit (0, 127, midiNote));
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

        for (size_t i = 0; i < library.size(); ++i)
        {
            juce::ValueTree sample (sampleType);
            sample.setProperty ("path", library[i]->file.getFullPathName(), nullptr);
            sample.setProperty ("root", rootNotes[i].load(), nullptr);
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

    {
        const juce::ScopedLock sl (libraryLock);
        retired.insert (retired.end(), library.begin(), library.end());
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
            queueLoad (file, (int) sample.getProperty ("root", 60));
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
