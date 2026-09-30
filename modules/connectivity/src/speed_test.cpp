#include "nexus/module/connectivity/speed_test.hpp"

#include "nexus/core/time.hpp"
#include "nexus/module/connectivity/connectivity_repository.hpp"

#include <chrono>
#include <utility>

namespace nexus::module::connectivity {

namespace {
constexpr std::chrono::milliseconds kDownloadTimeout{15000};
}

nexus::net::HttpProbeResult SpeedTester::default_download(std::string_view url,
                                                          std::chrono::milliseconds timeout) {
    return nexus::net::http_probe(url, timeout);
}

SpeedTester::SpeedTester(std::unique_ptr<ConnectivityRepository> repository, std::string url,
                         DownloadFn download)
    : repository_(std::move(repository)), url_(std::move(url)), download_(std::move(download)) {
}

SpeedTester::~SpeedTester() = default;

void SpeedTester::tick() {
    if (!active_.load(std::memory_order_relaxed) || !download_) {
        return;
    }
    ++ticks_;

    const auto result = download_(url_, kDownloadTimeout);

    SpeedTestRecord record;
    record.ran_at = nexus::core::now();
    record.server = url_;
    if (result.ok() && result.elapsed.count() > 0 && result.bytes_received > 0) {
        const double bits = static_cast<double>(result.bytes_received) * 8.0;
        const double seconds = std::chrono::duration<double>(result.elapsed).count();
        record.download_bps = bits / seconds;
    }
    repository_->record_speed_test(record);
}

} // namespace nexus::module::connectivity
