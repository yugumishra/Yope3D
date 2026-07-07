#include "TextureStreamer.h"
#include "ImageLoader.h"

TextureStreamer::~TextureStreamer() {
    stop();
}

void TextureStreamer::start() {
    if (running_.exchange(true)) return;   // already running
    worker_ = std::thread([this] { workerLoop(); });
}

void TextureStreamer::stop() {
    if (!running_.exchange(false)) return;  // not running
    jobCv_.notify_all();
    if (worker_.joinable()) worker_.join();

    // Drop anything left queued/decoded — the scene owning these keys is gone.
    { std::lock_guard lk(jobMtx_);   std::queue<DecodeJob>{}.swap(jobs_); }
    { std::lock_guard lk(readyMtx_); std::queue<DecodedTexture>{}.swap(ready_); }

    // Fresh counts for the next scene's streaming session.
    enqueuedCount_.store(0, std::memory_order_relaxed);
    completedCount_.store(0, std::memory_order_relaxed);
}

void TextureStreamer::enqueueDecode(const std::string& key, bool srgb,
                                    const uint8_t* encodedBytes, int len)
{
    if (!running_.load() || !encodedBytes || len <= 0) return;
    DecodeJob job;
    job.key = key;
    job.srgb = srgb;
    job.encodedBytes.assign(encodedBytes, encodedBytes + len);
    {
        std::lock_guard lk(jobMtx_);
        jobs_.push(std::move(job));
    }
    enqueuedCount_.fetch_add(1, std::memory_order_relaxed);
    jobCv_.notify_one();
}

bool TextureStreamer::popNext(DecodedTexture& out) {
    std::lock_guard lk(readyMtx_);
    if (ready_.empty()) return false;
    out = std::move(ready_.front());
    ready_.pop();
    return true;
}

void TextureStreamer::workerLoop() {
    while (true) {
        DecodeJob job;
        {
            std::unique_lock lk(jobMtx_);
            jobCv_.wait(lk, [this] { return !jobs_.empty() || !running_.load(); });
            // stop() must return promptly regardless of how large the remaining
            // backlog is — it already discards jobs_/ready_ right after join(),
            // so there is no point decoding a queue nobody will consume.
            if (!running_.load()) return;
            job = std::move(jobs_.front());
            jobs_.pop();
        }

        try {
            LoadedImage img = ImageLoader::loadFromMemory(job.encodedBytes.data(),
                                                           static_cast<int>(job.encodedBytes.size()));
            DecodedTexture decoded;
            decoded.key    = std::move(job.key);
            decoded.srgb   = job.srgb;
            decoded.pixels = std::move(img.pixels);
            decoded.width  = img.width;
            decoded.height = img.height;

            std::lock_guard lk(readyMtx_);
            ready_.push(std::move(decoded));
        } catch (...) {
            // Decode failure: drop the job. MaterialCache::loadOrDefault already
            // falls back to the 1x1 default when a key never becomes resident.
        }
        // Counted whether decode succeeded or not, so progress reporting always
        // reaches 100% instead of stalling short on a bad image.
        completedCount_.fetch_add(1, std::memory_order_relaxed);
    }
}
