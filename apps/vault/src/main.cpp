// std::getenv is safe here (single-threaded startup, result copied immediately).
#if defined(_MSC_VER) && !defined(_CRT_SECURE_NO_WARNINGS)
#define _CRT_SECURE_NO_WARNINGS
#endif

#include <nlohmann/json.hpp>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "nexus/ipc/pipe.hpp"
#include "nexus/vault/protocol.hpp"
#include "nexus/vault/vault_store.hpp"

namespace {

std::filesystem::path default_vault_path() {
    if (const char* override_path = std::getenv("NEXUSPC_VAULT_PATH")) {
        if (override_path[0] != '\0') {
            return std::filesystem::path(override_path);
        }
    }

    std::filesystem::path dir;
    if (const char* appdata = std::getenv("APPDATA")) {
        dir = std::filesystem::path(appdata) / "NexusPC";
    } else {
        dir = std::filesystem::temp_directory_path() / "NexusPC";
    }
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    return dir / "vault.nxv";
}

std::vector<std::uint8_t> to_bytes(const std::string& text) {
    std::vector<std::uint8_t> out(text.size());
    std::memcpy(out.data(), text.data(), text.size());
    return out;
}

bool send_json(nexus::ipc::PipeConnection& conn, const nlohmann::json& body) {
    return conn.send(to_bytes(body.dump()));
}

constexpr auto kAutoLockAfter = std::chrono::minutes{5};
constexpr auto kAutoLockPollInterval = std::chrono::seconds{5};

} // namespace

int main() {
    const std::filesystem::path vault_path = default_vault_path();
    nexus::vault::VaultStore store(vault_path);

    // Guards every VaultStore access: the auto-lock thread and the request
    // loop both touch it, and VaultStore itself has no internal locking.
    std::mutex store_mutex;
    std::atomic<std::chrono::steady_clock::time_point> last_activity{
        std::chrono::steady_clock::now()};

    // Locks independently of what the UI does or whether it's still alive
    // (spec section 2 / ADR-0003) - a frozen or malicious UI process cannot
    // keep the vault unlocked by simply not sending requests.
    std::atomic<bool> shutting_down{false};
    std::thread auto_lock_thread([&] {
        while (!shutting_down.load(std::memory_order_relaxed)) {
            std::this_thread::sleep_for(kAutoLockPollInterval);
            std::lock_guard<std::mutex> lock(store_mutex);
            if (!store.locked()) {
                const auto idle =
                    std::chrono::steady_clock::now() - last_activity.load(std::memory_order_relaxed);
                if (idle >= kAutoLockAfter) {
                    store.lock();
                }
            }
        }
    });

    nexus::ipc::PipeServer server(nexus::ipc::vault_pipe_name());
    std::printf("nexuspc-vault listening (vault file: %s)\n", vault_path.string().c_str());

    // Set by the "shutdown" verb (VaultClient sends it when the desktop app
    // is closing normally) so this process exits instead of outliving the
    // UI indefinitely - see docs/UFR_CONFORMANCE.md's "process-lifecycle
    // gaps".
    bool shutdown_requested = false;

    while (!shutdown_requested) {
        auto conn = server.accept();
        if (!conn) {
            break; // server closed (no signal handler wires this up yet)
        }

        while (true) {
            const auto message = conn->receive();
            if (!message) {
                break; // client disconnected; go back to accept() for the next one
            }

            nlohmann::json request;
            bool parse_ok = true;
            try {
                request = nlohmann::json::parse(message->begin(), message->end());
            } catch (const nlohmann::json::parse_error&) {
                parse_ok = false;
            }

            nlohmann::json response;
            if (!parse_ok) {
                response = {{"ok", false}, {"error", "malformed JSON request"}};
            } else if (parse_ok && request.value("verb", std::string{}) == "shutdown") {
                std::lock_guard<std::mutex> lock(store_mutex);
                store.lock();
                response = {{"ok", true}};
                shutdown_requested = true;
            } else {
                std::lock_guard<std::mutex> lock(store_mutex);
                response = nexus::vault::handle_request(store, request);
                last_activity.store(std::chrono::steady_clock::now(), std::memory_order_relaxed);
            }

            send_json(*conn, response);
            if (shutdown_requested) {
                break;
            }
        }
    }

    shutting_down.store(true, std::memory_order_relaxed);
    auto_lock_thread.join();
    return 0;
}
