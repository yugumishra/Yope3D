#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <thread>
#include <atomic>

// ---------------------------------------------------------------------------
// TextureStreamer
//
// Decodes texture bytes (stb_image, CPU-only) on a single worker thread so
// scene load never blocks on it. GPU upload is NOT done here: Texture::load
// submits to the shared graphics queue, which is externally synchronized with
// the render loop, so upload must stay on the main thread. AssetManager pumps
// popNext() once per frame and does the actual Texture::load call.
// ---------------------------------------------------------------------------

class TextureStreamer {
public:
    struct DecodedTexture {
        std::string          key;    // material path / synthetic "<glb>#imgN" key
        bool                 srgb = true;
        std::vector<uint8_t> pixels;
        int                  width  = 0;
        int                  height = 0;
    };

    TextureStreamer() = default;
    ~TextureStreamer();

    TextureStreamer(const TextureStreamer&) = delete;
    TextureStreamer& operator=(const TextureStreamer&) = delete;

    void start();
    void stop();

    // Copies encodedBytes (the caller's buffer, e.g. a glb chunk, may not
    // outlive this call) and queues it for background decode.
    void enqueueDecode(const std::string& key, bool srgb,
                       const uint8_t* encodedBytes, int len);

    // Pops one decoded-and-ready texture. Call from the main thread only.
    // Returns false if nothing is ready yet.
    bool popNext(DecodedTexture& out);

    // Job counts for progress reporting. completedCount includes decode
    // failures (dropped jobs) as well as successes, so it always catches up to
    // enqueuedCount — a stuck decode never leaves a progress bar short of 100%.
    int enqueuedCount()  const { return enqueuedCount_.load(std::memory_order_relaxed); }
    int completedCount() const { return completedCount_.load(std::memory_order_relaxed); }

private:
    struct DecodeJob {
        std::string          key;
        bool                 srgb;
        std::vector<uint8_t> encodedBytes;
    };

    void workerLoop();

    std::thread       worker_;
    std::atomic<bool> running_{false};

    std::mutex              jobMtx_;
    std::condition_variable jobCv_;
    std::queue<DecodeJob>   jobs_;

    std::mutex                  readyMtx_;
    std::queue<DecodedTexture>  ready_;

    std::atomic<int> enqueuedCount_{0};
    std::atomic<int> completedCount_{0};
};
