#include "AudioSystem.h"
#include "MusicStream.h"
#include "../assets/AudioLoader.h"
#include "../assets/AssetResolve.h"
#include <algorithm>
#include <filesystem>
#include <stdexcept>
#include <cstdint>

AudioSystem::AudioSystem() = default;
AudioSystem::~AudioSystem() { cleanup(); }

void AudioSystem::init() {
    if (initialised_) return;

    device_ = alcOpenDevice(nullptr);  // nullptr = default device
    if (!device_)
        throw std::runtime_error("AudioSystem: failed to open OpenAL device");

    context_ = alcCreateContext(device_, nullptr);
    if (!context_ || alcMakeContextCurrent(context_) == ALC_FALSE)
        throw std::runtime_error("AudioSystem: failed to create/activate OpenAL context");

    alDistanceModel(AL_INVERSE_DISTANCE_CLAMPED);
    alDopplerFactor(3.0f);    // exaggerated for audible demo
    alSpeedOfSound(343.3f);

    initialised_ = true;
}

void AudioSystem::cleanup() {
    if (!initialised_) return;

    fades_.clear();
    musicStreams_.clear();  // stops sources, closes decoders, deletes buffers
    sources_.clear();       // alDeleteSources in ~Source
    buffers_.clear();       // alDeleteBuffers in ~SoundBuffer
    musicBuffers_.clear();

    alcMakeContextCurrent(nullptr);
    if (context_) { alcDestroyContext(context_); context_ = nullptr; }
    if (device_)  { alcCloseDevice(device_);    device_  = nullptr; }

    initialised_ = false;
}

AudioSystem::SoundBuffer* AudioSystem::loadSound(const std::string& path) {
    auto it = buffers_.find(path);
    if (it != buffers_.end()) return it->second.get();

    std::vector<uint8_t> bytes = assets::readBytes(path);
    if (bytes.empty())
        throw std::runtime_error("AudioSystem: failed to load: " + path);
    LoadedAudio audio = AudioLoader::loadFromMemory(bytes.data(), static_cast<int>(bytes.size()));

    // OpenAL does not spatialize stereo sources — down-mix to mono so distance
    // attenuation and panning work correctly for positional emitters.
    if (audio.channels == 2) {
        std::vector<int16_t> mono;
        mono.reserve(audio.samples.size() / 2);
        for (size_t i = 0; i + 1 < audio.samples.size(); i += 2) {
            int32_t mixed = static_cast<int32_t>(audio.samples[i])
                          + static_cast<int32_t>(audio.samples[i + 1]);
            mono.push_back(static_cast<int16_t>(mixed / 2));
        }
        audio.samples  = std::move(mono);
        audio.channels = 1;
    }

    auto buf = std::make_unique<SoundBuffer>();
    alGenBuffers(1, &buf->id);
    alBufferData(buf->id, AL_FORMAT_MONO16,
                 audio.samples.data(),
                 static_cast<ALsizei>(audio.samples.size() * sizeof(int16_t)),
                 static_cast<ALsizei>(audio.sampleRate));

    SoundBuffer* ptr = buf.get();
    buffers_[path] = std::move(buf);
    return ptr;
}

Source* AudioSystem::createSource(SoundBuffer* buffer) {
    sources_.push_back(std::make_unique<Source>(buffer->id, this));
    return sources_.back().get();
}

Source* AudioSystem::playTransient(SoundBuffer* buffer) {
    // Recycle a finished transient voice if one is free.
    for (auto& s : sources_) {
        if (s->transient && !s->isPlaying()) {
            s->setBuffer(buffer->id);
            s->setGain(1.0f);
            s->setPitch(1.0f);
            s->enableLooping(false);
            return s.get();
        }
    }
    // None reusable — allocate one and tag it transient.
    sources_.push_back(std::make_unique<Source>(buffer->id, this));
    Source* s = sources_.back().get();
    s->transient = true;
    return s;
}

void AudioSystem::pauseAll() {
    for (auto& src : sources_) {
        if (src->isPlaying()) {
            src->pausedBySystem = true;
            src->pause();
        }
    }
    for (auto& ms : musicStreams_) {
        Source* src = ms->source();
        if (src->isPlaying()) {
            src->pausedBySystem = true;
            src->pause();
        }
    }
}

void AudioSystem::resumeAll() {
    for (auto& src : sources_) {
        if (src->pausedBySystem) {
            src->pausedBySystem = false;
            src->play();
        }
    }
    for (auto& ms : musicStreams_) {
        Source* src = ms->source();
        if (src->pausedBySystem) {
            src->pausedBySystem = false;
            src->play();
        }
    }
}

void AudioSystem::stopAll() {
    for (auto& src : sources_) src->stop();
    for (auto& ms : musicStreams_) ms->source()->stop();
}

void AudioSystem::removeSource(Source* src) {
    fades_.erase(std::remove_if(fades_.begin(), fades_.end(),
        [src](const GainFade& f) { return f.src == src; }), fades_.end());

    // If this source belongs to a streaming MusicStream, drop the whole
    // stream — it owns the source, decoder, and AL buffers together.
    auto msIt = std::find_if(musicStreams_.begin(), musicStreams_.end(),
        [src](const std::unique_ptr<MusicStream>& m) { return m->ownsSource(src); });
    if (msIt != musicStreams_.end()) {
        musicStreams_.erase(msIt);
        return;
    }

    auto it = std::find_if(sources_.begin(), sources_.end(),
        [src](const std::unique_ptr<Source>& p){ return p.get() == src; });
    if (it != sources_.end()) {
        (*it)->stop();
        sources_.erase(it);
    }
}

void AudioSystem::setBusGain(Source::Bus bus, float gain) {
    busGain_[static_cast<size_t>(bus)] = gain;
    for (auto& s : sources_) if (s->getBus() == bus) s->refreshGain();
    for (auto& ms : musicStreams_) if (ms->source()->getBus() == bus) ms->source()->refreshGain();
}

void AudioSystem::setMasterGain(float gain) {
    masterGain_ = gain;
    for (auto& s : sources_) s->refreshGain();
    for (auto& ms : musicStreams_) ms->source()->refreshGain();
}

void AudioSystem::fadeGain(Source* src, float target, float durationSeconds, ui::Ease ease) {
    float duration = std::max(durationSeconds, 0.0001f);
    for (auto& f : fades_) {
        if (f.src == src) { f = {src, src->baseGain(), target, duration, 0.0f, ease}; return; }
    }
    fades_.push_back({src, src->baseGain(), target, duration, 0.0f, ease});
}

void AudioSystem::update(float dt) {
    for (auto it = fades_.begin(); it != fades_.end(); ) {
        it->elapsed += dt;
        float t = std::min(it->elapsed / it->duration, 1.0f);
        it->src->setGain(it->from + (it->to - it->from) * ui::applyEase(it->ease, t));
        if (t >= 1.0f) it = fades_.erase(it); else ++it;
    }

    for (auto it = musicStreams_.begin(); it != musicStreams_.end(); ) {
        if (!(*it)->refill()) it = musicStreams_.erase(it); else ++it;
    }
}

AudioSystem::SoundBuffer* AudioSystem::loadMusicBuffer(const std::string& path) {
    auto it = musicBuffers_.find(path);
    if (it != musicBuffers_.end()) return it->second.get();

    std::vector<uint8_t> bytes = assets::readBytes(path);
    if (bytes.empty())
        throw std::runtime_error("AudioSystem: failed to load: " + path);
    LoadedAudio audio = AudioLoader::loadFromMemory(bytes.data(), static_cast<int>(bytes.size()));

    auto buf = std::make_unique<SoundBuffer>();
    alGenBuffers(1, &buf->id);
    ALenum format = audio.channels == 2 ? AL_FORMAT_STEREO16 : AL_FORMAT_MONO16;
    alBufferData(buf->id, format,
                 audio.samples.data(),
                 static_cast<ALsizei>(audio.samples.size() * sizeof(int16_t)),
                 static_cast<ALsizei>(audio.sampleRate));

    SoundBuffer* ptr = buf.get();
    musicBuffers_[path] = std::move(buf);
    return ptr;
}

Source* AudioSystem::playMusic(const std::string& path, bool loop, float fadeInSeconds, bool stream) {
    Source* src = nullptr;
    if (stream) {
        auto ms = std::make_unique<MusicStream>(path, loop, this);
        src = ms->source();
        musicStreams_.push_back(std::move(ms));
    } else {
        auto* buf = loadMusicBuffer(path);
        sources_.push_back(std::make_unique<Source>(buf->id, this));
        src = sources_.back().get();
        src->enableLooping(loop);
    }

    src->setRelative(true);
    src->setBus(Source::Bus::Music);

    if (fadeInSeconds > 0.0f) {
        src->setGain(0.0f);
        fadeGain(src, 1.0f, fadeInSeconds);
    } else {
        src->setGain(1.0f);
    }

    src->play();
    return src;
}
