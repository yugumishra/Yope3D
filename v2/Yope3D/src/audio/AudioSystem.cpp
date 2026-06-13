#include "AudioSystem.h"
#include "../assets/AudioLoader.h"
#include <filesystem>
#include <stdexcept>
#include <cstdint>

#ifdef YOPE_EMBED_ASSETS
#include "generated/embedded_assets.h"
#endif

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

    sources_.clear();  // alDeleteSources in ~Source
    buffers_.clear();  // alDeleteBuffers in ~SoundBuffer

    alcMakeContextCurrent(nullptr);
    if (context_) { alcDestroyContext(context_); context_ = nullptr; }
    if (device_)  { alcCloseDevice(device_);    device_  = nullptr; }

    initialised_ = false;
}

AudioSystem::SoundBuffer* AudioSystem::loadSound(const std::string& path) {
    auto it = buffers_.find(path);
    if (it != buffers_.end()) return it->second.get();

    // Mirror AssetManager::loadTexture's embed / filesystem branching.
    LoadedAudio audio;
#ifdef YOPE_EMBED_ASSETS
    EmbeddedAsset asset = getEmbeddedAsset(path.c_str());
    if (asset.data) {
        audio = AudioLoader::loadFromMemory(asset.data, static_cast<int>(asset.size));
    } else {
        throw std::runtime_error("AudioSystem: embedded asset not found: " + path);
    }
#else
    std::string fullPath = (std::filesystem::path(YOPE_ASSETS_DIR) / path).string();
    audio = AudioLoader::load(fullPath);
#endif

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
    sources_.push_back(std::make_unique<Source>(buffer->id));
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
    sources_.push_back(std::make_unique<Source>(buffer->id));
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
}

void AudioSystem::resumeAll() {
    for (auto& src : sources_) {
        if (src->pausedBySystem) {
            src->pausedBySystem = false;
            src->play();
        }
    }
}

void AudioSystem::stopAll() {
    for (auto& src : sources_) src->stop();
}

void AudioSystem::removeSource(Source* src) {
    auto it = std::find_if(sources_.begin(), sources_.end(),
        [src](const std::unique_ptr<Source>& p){ return p.get() == src; });
    if (it != sources_.end()) {
        (*it)->stop();
        sources_.erase(it);
    }
}
