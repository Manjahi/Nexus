#include "VaultClient.hpp"

#include <chrono>
#include <cstring>
#include <nlohmann/json.hpp>
#include <QCoreApplication>
#include <QDir>
#include <QProcess>
#include <string>
#include <vector>

namespace nexuspc::desktop {

namespace {

std::vector<std::uint8_t> to_bytes(const std::string& text) {
    std::vector<std::uint8_t> out(text.size());
    std::memcpy(out.data(), text.data(), text.size());
    return out;
}

} // namespace

bool VaultClient::ensure_connected() {
    if (connection_ && connection_->is_open()) {
        return true;
    }
    connection_.reset();

    auto conn = nexus::ipc::PipeClient::connect(nexus::ipc::vault_pipe_name(),
                                                std::chrono::milliseconds{300});
    if (!conn) {
        if (!spawn_attempted_) {
            spawn_attempted_ = true;
            const QString exe = QDir(QCoreApplication::applicationDirPath())
                                    .filePath(QStringLiteral("nexuspc-vault.exe"));
            QProcess::startDetached(exe, {});
        }
        conn = nexus::ipc::PipeClient::connect(nexus::ipc::vault_pipe_name(),
                                               std::chrono::milliseconds{4000});
    }

    if (!conn) {
        return false;
    }
    connection_ = std::move(conn);
    return true;
}

nlohmann::json VaultClient::request(nlohmann::json body) {
    std::lock_guard<std::mutex> lock(mutex_);

    if (!ensure_connected()) {
        return {{"ok", false}, {"error", "could not reach nexuspc-vault"}};
    }

    if (!connection_->send(to_bytes(body.dump()))) {
        connection_.reset();
        return {{"ok", false}, {"error", "lost connection to nexuspc-vault"}};
    }

    const auto message = connection_->receive();
    if (!message) {
        connection_.reset();
        return {{"ok", false}, {"error", "no response from nexuspc-vault"}};
    }

    try {
        return nlohmann::json::parse(message->begin(), message->end());
    } catch (const nlohmann::json::parse_error&) {
        return {{"ok", false}, {"error", "malformed response from nexuspc-vault"}};
    }
}

void VaultClient::shutdown_if_running() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!spawn_attempted_) {
        return; // this session never spawned or reached nexuspc-vault
    }
    if (!ensure_connected()) {
        return; // already gone
    }
    if (connection_->send(to_bytes(nlohmann::json{{"verb", "shutdown"}}.dump()))) {
        [[maybe_unused]] const auto ack =
            connection_->receive(); // wait for it so it has locked before we return
    }
    connection_.reset();
}

} // namespace nexuspc::desktop
