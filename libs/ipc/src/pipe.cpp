// std::getenv is safe here (result copied immediately into a std::string,
// same reasoning as apps/vault/src/main.cpp's NEXUSPC_VAULT_PATH read).
#if defined(_MSC_VER) && !defined(_CRT_SECURE_NO_WARNINGS)
#define _CRT_SECURE_NO_WARNINGS
#endif

#include "nexus/ipc/pipe.hpp"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>

#include <sddl.h>

#include <array>
#include <atomic>
#include <cstdlib>
#include <thread>

namespace nexus::ipc {

namespace {

constexpr DWORD kBufferSize = 4096;
constexpr std::uint32_t kMaxMessageBytes = 16 * 1024 * 1024;

std::string full_pipe_name(const std::string& name) { return "\\\\.\\pipe\\" + name; }

// Restricts the pipe to the creating user (OW) and SYSTEM (SY) - without an
// explicit descriptor CreateNamedPipeA falls back to Windows' default DACL,
// which lets any locally logged-on user connect to a fully predictable pipe
// name. A named pipe's ACL is its own kernel object, entirely separate from
// any NTFS permissions on the vault file.
struct PipeSecurity {
    PSECURITY_DESCRIPTOR descriptor = nullptr;
    SECURITY_ATTRIBUTES attributes{};

    PipeSecurity() {
        if (::ConvertStringSecurityDescriptorToSecurityDescriptorW(
                L"D:(A;;GA;;;OW)(A;;GA;;;SY)", SDDL_REVISION_1, &descriptor, nullptr)) {
            attributes.nLength = sizeof(SECURITY_ATTRIBUTES);
            attributes.lpSecurityDescriptor = descriptor;
            attributes.bInheritHandle = FALSE;
        }
    }

    ~PipeSecurity() {
        if (descriptor != nullptr) {
            ::LocalFree(descriptor);
        }
    }

    // nullptr (Windows' default DACL) if the descriptor failed to build,
    // rather than refusing to create the pipe at all.
    LPSECURITY_ATTRIBUTES ptr() { return descriptor != nullptr ? &attributes : nullptr; }

    PipeSecurity(const PipeSecurity&) = delete;
    PipeSecurity& operator=(const PipeSecurity&) = delete;
};

bool write_all(HANDLE handle, const std::uint8_t* data, std::size_t size) {
    std::size_t written = 0;
    while (written < size) {
        DWORD chunk = 0;
        const DWORD want = static_cast<DWORD>(
            std::min<std::size_t>(size - written, static_cast<std::size_t>(0xFFFFFFFFu)));
        if (!::WriteFile(handle, data + written, want, &chunk, nullptr) || chunk == 0) {
            return false;
        }
        written += chunk;
    }
    return true;
}

bool read_all(HANDLE handle, std::uint8_t* data, std::size_t size) {
    std::size_t got = 0;
    while (got < size) {
        DWORD chunk = 0;
        const DWORD want = static_cast<DWORD>(
            std::min<std::size_t>(size - got, static_cast<std::size_t>(0xFFFFFFFFu)));
        if (!::ReadFile(handle, data + got, want, &chunk, nullptr) || chunk == 0) {
            return false; // broken pipe, closed peer, or other I/O error
        }
        got += chunk;
    }
    return true;
}

} // namespace

struct PipeConnection::Impl {
    HANDLE handle = INVALID_HANDLE_VALUE;

    ~Impl() {
        if (handle != INVALID_HANDLE_VALUE) {
            ::CloseHandle(handle);
        }
    }
};

PipeConnection::PipeConnection(std::unique_ptr<Impl> impl) noexcept : impl_(std::move(impl)) {}
PipeConnection::~PipeConnection() = default;
PipeConnection::PipeConnection(PipeConnection&&) noexcept = default;
PipeConnection& PipeConnection::operator=(PipeConnection&&) noexcept = default;

bool PipeConnection::send(std::span<const std::uint8_t> message) {
    if (!is_open()) {
        return false;
    }
    const auto len = static_cast<std::uint32_t>(message.size());
    const std::array<std::uint8_t, 4> prefix{
        static_cast<std::uint8_t>(len & 0xFF), static_cast<std::uint8_t>((len >> 8) & 0xFF),
        static_cast<std::uint8_t>((len >> 16) & 0xFF), static_cast<std::uint8_t>((len >> 24) & 0xFF)};

    if (!write_all(impl_->handle, prefix.data(), prefix.size())) {
        return false;
    }
    return message.empty() || write_all(impl_->handle, message.data(), message.size());
}

std::optional<std::vector<std::uint8_t>> PipeConnection::receive() {
    if (!is_open()) {
        return std::nullopt;
    }
    std::array<std::uint8_t, 4> prefix{};
    if (!read_all(impl_->handle, prefix.data(), prefix.size())) {
        return std::nullopt;
    }
    const std::uint32_t len = static_cast<std::uint32_t>(prefix[0]) |
                              (static_cast<std::uint32_t>(prefix[1]) << 8) |
                              (static_cast<std::uint32_t>(prefix[2]) << 16) |
                              (static_cast<std::uint32_t>(prefix[3]) << 24);
    if (len > kMaxMessageBytes) {
        return std::nullopt;
    }

    std::vector<std::uint8_t> out(len);
    if (len > 0 && !read_all(impl_->handle, out.data(), out.size())) {
        return std::nullopt;
    }
    return out;
}

void PipeConnection::close() noexcept {
    if (impl_ && impl_->handle != INVALID_HANDLE_VALUE) {
        ::CloseHandle(impl_->handle);
        impl_->handle = INVALID_HANDLE_VALUE;
    }
}

bool PipeConnection::is_open() const noexcept {
    return impl_ && impl_->handle != INVALID_HANDLE_VALUE;
}

struct PipeServer::Impl {
    std::string full_name;
    std::atomic<bool> closing{false};
};

PipeServer::PipeServer(std::string name) : impl_(std::make_unique<Impl>()) {
    impl_->full_name = full_pipe_name(name);
}

PipeServer::~PipeServer() { close(); }

std::optional<PipeConnection> PipeServer::accept() {
    if (impl_->closing.load(std::memory_order_relaxed)) {
        return std::nullopt;
    }

    PipeSecurity security;
    HANDLE handle = ::CreateNamedPipeA(
        impl_->full_name.c_str(), PIPE_ACCESS_DUPLEX,
        PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT, PIPE_UNLIMITED_INSTANCES, kBufferSize,
        kBufferSize, 0, security.ptr());
    if (handle == INVALID_HANDLE_VALUE) {
        return std::nullopt;
    }

    const BOOL connected = ::ConnectNamedPipe(handle, nullptr);
    if (!connected && ::GetLastError() != ERROR_PIPE_CONNECTED) {
        ::CloseHandle(handle);
        return std::nullopt;
    }

    if (impl_->closing.load(std::memory_order_relaxed)) {
        ::CloseHandle(handle);
        return std::nullopt;
    }

    auto conn_impl = std::make_unique<PipeConnection::Impl>();
    conn_impl->handle = handle;
    return PipeConnection(std::move(conn_impl));
}

void PipeServer::close() noexcept {
    if (impl_->closing.exchange(true, std::memory_order_relaxed)) {
        return; // already closed
    }
    // Unblock a thread waiting inside ConnectNamedPipe by connecting (and
    // immediately dropping) a throwaway client.
    HANDLE dummy = ::CreateFileA(impl_->full_name.c_str(), GENERIC_READ | GENERIC_WRITE, 0,
                                 nullptr, OPEN_EXISTING, 0, nullptr);
    if (dummy != INVALID_HANDLE_VALUE) {
        ::CloseHandle(dummy);
    }
}

std::optional<PipeConnection> PipeClient::connect(const std::string& name,
                                                  std::chrono::milliseconds timeout) {
    const std::string full_name = full_pipe_name(name);
    const auto deadline = std::chrono::steady_clock::now() + timeout;

    while (true) {
        HANDLE handle = ::CreateFileA(full_name.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr,
                                      OPEN_EXISTING, 0, nullptr);
        if (handle != INVALID_HANDLE_VALUE) {
            auto conn_impl = std::make_unique<PipeConnection::Impl>();
            conn_impl->handle = handle;
            return PipeConnection(std::move(conn_impl));
        }

        const DWORD err = ::GetLastError();
        if (err != ERROR_FILE_NOT_FOUND && err != ERROR_PIPE_BUSY) {
            return std::nullopt; // not a "try again shortly" situation
        }
        if (std::chrono::steady_clock::now() >= deadline) {
            return std::nullopt;
        }
        if (err == ERROR_PIPE_BUSY) {
            ::WaitNamedPipeA(full_name.c_str(), 250);
        } else {
            std::this_thread::sleep_for(std::chrono::milliseconds{50});
        }
    }
}

std::string vault_pipe_name() {
    if (const char* override_name = std::getenv("NEXUSPC_VAULT_PIPE")) {
        if (override_name[0] != '\0') {
            return override_name;
        }
    }
    std::array<char, 256> buffer{};
    DWORD size = static_cast<DWORD>(buffer.size());
    if (::GetUserNameA(buffer.data(), &size) && size > 0) {
        return "nexuspc-vault-" + std::string(buffer.data(), size - 1);
    }
    return "nexuspc-vault-default";
}

} // namespace nexus::ipc
