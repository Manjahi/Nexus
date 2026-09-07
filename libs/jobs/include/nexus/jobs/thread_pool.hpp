#pragma once

#include <condition_variable>
#include <cstddef>
#include <functional>
#include <future>
#include <mutex>
#include <queue>
#include <stop_token>
#include <thread>
#include <vector>

namespace nexus::jobs {

/// Fixed-size worker pool. Tasks run in FIFO submission order across workers.
/// Destruction drains the queue (every submitted task runs) before joining.
class ThreadPool {
public:
    /// `thread_count == 0` picks std::thread::hardware_concurrency() (at least 1).
    explicit ThreadPool(unsigned thread_count = 0);
    ~ThreadPool();

    ThreadPool(const ThreadPool&) = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;

    /// Enqueues `task`. The returned future completes when the task finishes and
    /// captures any exception it throws.
    std::future<void> submit(std::function<void()> task);

    /// Blocks until the queue is empty and no task is running.
    void wait_idle();

    [[nodiscard]] std::size_t pending() const;
    [[nodiscard]] unsigned size() const noexcept;

private:
    void worker_loop(const std::stop_token& stop);

    mutable std::mutex mutex_;
    std::condition_variable_any work_cv_;
    std::condition_variable idle_cv_;
    std::queue<std::packaged_task<void()>> tasks_;
    std::size_t active_ = 0;
    unsigned thread_count_ = 0;
    std::vector<std::jthread> workers_;
};

} // namespace nexus::jobs
