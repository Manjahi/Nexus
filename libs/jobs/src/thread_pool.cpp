#include "nexus/jobs/thread_pool.hpp"

#include <algorithm>
#include <utility>

namespace nexus::jobs {

ThreadPool::ThreadPool(unsigned thread_count) {
    if (thread_count == 0) {
        thread_count = std::max(1U, std::thread::hardware_concurrency());
    }
    thread_count_ = thread_count;
    workers_.reserve(thread_count);
    for (unsigned i = 0; i < thread_count; ++i) {
        workers_.emplace_back([this](std::stop_token stop) { worker_loop(stop); });
    }
}

ThreadPool::~ThreadPool() {
    // Ask workers to stop, then let them drain remaining tasks before joining
    // (std::jthread destructor requests stop and joins).
    for (std::jthread& worker : workers_) {
        worker.request_stop();
    }
    work_cv_.notify_all();
}

std::future<void> ThreadPool::submit(std::function<void()> task) {
    std::packaged_task<void()> packaged(std::move(task));
    std::future<void> future = packaged.get_future();
    {
        std::scoped_lock lock(mutex_);
        tasks_.push(std::move(packaged));
    }
    work_cv_.notify_one();
    return future;
}

void ThreadPool::wait_idle() {
    std::unique_lock lock(mutex_);
    idle_cv_.wait(lock, [this] { return tasks_.empty() && active_ == 0; });
}

std::size_t ThreadPool::pending() const {
    std::scoped_lock lock(mutex_);
    return tasks_.size();
}

unsigned ThreadPool::size() const noexcept {
    return thread_count_;
}

void ThreadPool::worker_loop(const std::stop_token& stop) {
    // Deliberately NOT condition_variable_any::wait(lock, stop_token, pred):
    // MSVC STL had a real deadlock in that overload under concurrent use by
    // multiple threads (microsoft/STL#2218), which is exactly this pool's
    // shape (thread_count workers all waiting on the same work_cv_). Wake on
    // stop via an explicit stop_callback instead, same pattern Scheduler::run
    // already uses, and fold the stop check into a plain wait's predicate.
    const std::stop_callback wake(stop, [this] { work_cv_.notify_all(); });
    while (true) {
        std::packaged_task<void()> task;
        {
            std::unique_lock lock(mutex_);
            work_cv_.wait(lock, [this, &stop] { return stop.stop_requested() || !tasks_.empty(); });
            if (tasks_.empty()) {
                return; // woken by stop request with nothing left to do
            }
            task = std::move(tasks_.front());
            tasks_.pop();
            ++active_;
        }

        task();

        {
            std::scoped_lock lock(mutex_);
            --active_;
            if (tasks_.empty() && active_ == 0) {
                idle_cv_.notify_all();
            }
        }
    }
}

} // namespace nexus::jobs
