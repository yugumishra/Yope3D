#pragma once
#include <thread>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <functional>
#include <vector>
#include <atomic>

// Fixed-size thread pool with fire-and-wait semantics.
// enqueue() submits a task; wait() blocks until all submitted tasks finish.
// No per-frame thread spawning — workers are created once in the constructor.
class ThreadPool {
public:
    explicit ThreadPool(unsigned int threads);
    ~ThreadPool();

    void enqueue(std::function<void()> task);
    void wait(); // block until all pending tasks complete

    unsigned int size() const { return static_cast<unsigned int>(workers_.size()); }

private:
    std::vector<std::thread>          workers_;
    std::queue<std::function<void()>> tasks_;
    std::mutex                        mutex_;
    std::condition_variable           taskCv_;
    std::condition_variable           doneCv_;
    std::atomic<int>                  active_{0};
    bool                              stop_ = false;
};
