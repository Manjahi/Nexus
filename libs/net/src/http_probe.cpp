#include "nexus/net/probe.hpp"

#include <curl/curl.h>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <string>

namespace nexus::net {

namespace {

void ensure_curl_global_init() {
    static const int once = [] {
        ::curl_global_init(CURL_GLOBAL_DEFAULT);
        return 0;
    }();
    (void)once;
}

std::size_t discard_and_count(char* /*ptr*/, std::size_t size, std::size_t nmemb, void* userdata) {
    const std::size_t bytes = size * nmemb;
    *static_cast<std::uint64_t*>(userdata) += bytes;
    return bytes;
}

ProbeStatus map_curl_error(CURLcode code) {
    switch (code) {
        case CURLE_OPERATION_TIMEDOUT:
            return ProbeStatus::Timeout;
        case CURLE_COULDNT_RESOLVE_HOST:
        case CURLE_COULDNT_RESOLVE_PROXY:
            return ProbeStatus::DnsFailure;
        case CURLE_COULDNT_CONNECT:
        case CURLE_GOT_NOTHING:
        case CURLE_RECV_ERROR:
        case CURLE_SEND_ERROR:
            return ProbeStatus::Unreachable;
        default:
            return ProbeStatus::Error;
    }
}

} // namespace

HttpProbeResult http_probe(std::string_view url, std::chrono::milliseconds timeout) {
    ensure_curl_global_init();

    HttpProbeResult result;
    CURL* handle = ::curl_easy_init();
    if (handle == nullptr) {
        result.detail = "curl_easy_init failed";
        return result;
    }

    const std::string url_str(url);
    std::uint64_t bytes = 0;

    ::curl_easy_setopt(handle, CURLOPT_URL, url_str.c_str());
    ::curl_easy_setopt(handle, CURLOPT_TIMEOUT_MS, static_cast<long>(timeout.count()));
    ::curl_easy_setopt(handle, CURLOPT_NOSIGNAL, 1L);
    ::curl_easy_setopt(handle, CURLOPT_FOLLOWLOCATION, 1L);
    ::curl_easy_setopt(handle, CURLOPT_MAXREDIRS, 5L);
    ::curl_easy_setopt(handle, CURLOPT_USERAGENT, "NexusPC/0.1 connectivity-probe");
    ::curl_easy_setopt(handle, CURLOPT_ACCEPT_ENCODING, "");
    ::curl_easy_setopt(handle, CURLOPT_WRITEFUNCTION, &discard_and_count);
    ::curl_easy_setopt(handle, CURLOPT_WRITEDATA, &bytes);

    const auto start = std::chrono::steady_clock::now();
    const CURLcode code = ::curl_easy_perform(handle);
    result.elapsed = std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now() - start);
    result.bytes_received = bytes;

    if (code == CURLE_OK) {
        long http_code = 0;
        ::curl_easy_getinfo(handle, CURLINFO_RESPONSE_CODE, &http_code);
        result.status_code = static_cast<int>(http_code);
        result.status = http_code > 0 ? ProbeStatus::Ok : ProbeStatus::Error;
    } else {
        result.status = map_curl_error(code);
        result.detail = ::curl_easy_strerror(code);
    }

    ::curl_easy_cleanup(handle);
    return result;
}

} // namespace nexus::net
