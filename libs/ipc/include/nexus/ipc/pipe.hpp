#pragma once

#include <chrono>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace nexus::ipc {

/// One connected end of a local IPC channel: ordered, length-prefixed byte
/// messages, request or response, one at a time. Backed by a Windows named
/// pipe today (spec section 5's "local RPC abstraction"); the surface is
/// deliberately transport-agnostic so another OS's local transport could
/// implement it later without changing PipeServer/PipeClient callers.
class PipeConnection {
public:
    ~PipeConnection();
    PipeConnection(PipeConnection&& other) noexcept;
    PipeConnection& operator=(PipeConnection&& other) noexcept;
    PipeConnection(const PipeConnection&) = delete;
    PipeConnection& operator=(const PipeConnection&) = delete;

    /// Writes one message (a 4-byte length prefix, then the bytes). False on
    /// a broken or closed connection.
    [[nodiscard]] bool send(std::span<const std::uint8_t> message);

    /// Blocks for the next full message. nullopt if the peer closed the
    /// connection, an I/O error occurred, or a malformed length prefix was
    /// received.
    [[nodiscard]] std::optional<std::vector<std::uint8_t>> receive();

    void close() noexcept;
    [[nodiscard]] bool is_open() const noexcept;

private:
    friend class PipeServer;
    friend class PipeClient;
    struct Impl;
    explicit PipeConnection(std::unique_ptr<Impl> impl) noexcept;
    std::unique_ptr<Impl> impl_;
};

/// Listens on a named local endpoint. Accepts one client connection at a
/// time - the vault expects exactly one UI client per unlock session; a
/// second connection attempt simply waits behind the first.
class PipeServer {
public:
    explicit PipeServer(std::string name);
    ~PipeServer();
    PipeServer(const PipeServer&) = delete;
    PipeServer& operator=(const PipeServer&) = delete;

    /// Blocks until a client connects or the server is closed. nullopt means
    /// "stop serving" (a clean close()) or an unrecoverable listen error -
    /// callers should treat both the same way: exit the accept loop.
    [[nodiscard]] std::optional<PipeConnection> accept();

    /// Unblocks a pending accept() and prevents further connections.
    void close() noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

class PipeClient {
public:
    /// Connects to `name`, retrying at short intervals until `timeout`
    /// elapses (the server process may still be starting up). nullopt if no
    /// server was listening within the timeout.
    [[nodiscard]] static std::optional<PipeConnection> connect(
        const std::string& name,
        std::chrono::milliseconds timeout = std::chrono::milliseconds{3000});
};

/// The pipe name nexuspc-ui and nexuspc-vault agree on. Namespaced per
/// Windows account so multiple users on the same machine don't collide.
[[nodiscard]] std::string vault_pipe_name();

} // namespace nexus::ipc
