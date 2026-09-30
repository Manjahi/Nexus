// UFR-011: the vault's "real end-to-end pass" was previously a one-time
// manual Windows-UI-Automation exercise (see dev-workflow notes), not a
// repeatable test - tests/unit/vault/test_protocol.cpp exercises the same
// verb-handling logic, but always in-process, directly against a VaultStore.
// This spawns the REAL nexuspc-vault.exe and drives it over a REAL named
// pipe with the real wire protocol, the way apps/desktop's VaultClient
// actually does - catching anything specific to the process boundary
// (spawn timing, pipe accept-loop behavior, real (de)serialization over the
// wire) that an in-process test structurally cannot.

#include "nexus/ipc/pipe.hpp"

#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#define WIN32_LEAN_AND_MEAN
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <system_error>
#include <vector>
#include <windows.h>

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace {

fs::path current_executable_dir() {
    wchar_t buffer[MAX_PATH];
    const DWORD len = GetModuleFileNameW(nullptr, buffer, MAX_PATH);
    return fs::path(std::wstring(buffer, len)).parent_path();
}

// Both this test binary and nexuspc-vault.exe land under the same multi-
// config build directory, in a same-named config subfolder (Debug/Release/
// ...): .../tests/integration/<config>/this.exe and
// .../apps/vault/<config>/nexuspc-vault.exe.
fs::path find_vault_executable() {
    const fs::path test_dir = current_executable_dir();
    const fs::path config = test_dir.filename();
    const fs::path build_root = test_dir.parent_path().parent_path().parent_path();
    return build_root / "apps" / "vault" / config / "nexuspc-vault.exe";
}

fs::path make_scratch_dir(std::string_view tag) {
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    return fs::temp_directory_path() /
           ("nexuspc_vault_process_" + std::string(tag) + "_" + std::to_string(stamp));
}

/// Owns the spawned nexuspc-vault.exe child process and always terminates
/// it on destruction (a REQUIRE failure unwinds through this destructor via
/// Catch2's mechanism, so a failed assertion mid-test still can't leak an
/// orphan process the way a bare CreateProcess + manual cleanup could).
class VaultProcess {
public:
    VaultProcess(const fs::path& exe_path, const fs::path& vault_path,
                 const std::string& pipe_name) {
        // Both apps/vault/src/main.cpp (NEXUSPC_VAULT_PATH) and
        // libs/ipc/src/pipe.cpp (NEXUSPC_VAULT_PIPE) read these via the
        // narrow std::getenv, so set them narrow here too rather than
        // mixing _wputenv_s/_putenv_s.
        _putenv_s("NEXUSPC_VAULT_PATH", vault_path.string().c_str());
        _putenv_s("NEXUSPC_VAULT_PIPE", pipe_name.c_str());

        STARTUPINFOW startup{};
        startup.cb = sizeof(startup);
        PROCESS_INFORMATION info{};
        std::wstring cmdline = L"\"" + exe_path.wstring() + L"\"";
        const BOOL ok = ::CreateProcessW(exe_path.c_str(), cmdline.data(), nullptr, nullptr, FALSE,
                                         0, nullptr, nullptr, &startup, &info);
        if (ok) {
            process_ = info.hProcess;
            ::CloseHandle(info.hThread);
        }
    }

    ~VaultProcess() {
        if (process_ != nullptr) {
            ::TerminateProcess(process_, 0);
            ::WaitForSingleObject(process_, 2000);
            ::CloseHandle(process_);
        }
    }

    VaultProcess(const VaultProcess&) = delete;
    VaultProcess& operator=(const VaultProcess&) = delete;

    [[nodiscard]] bool spawned() const noexcept { return process_ != nullptr; }

private:
    HANDLE process_ = nullptr;
};

json send(nexus::ipc::PipeConnection& conn, const json& request) {
    const std::string bytes = request.dump();
    const std::vector<std::uint8_t> buf(bytes.begin(), bytes.end());
    if (!conn.send(buf)) {
        return json{{"ok", false}, {"error", "send failed"}};
    }
    const auto response_bytes = conn.receive();
    if (!response_bytes) {
        return json{{"ok", false}, {"error", "receive failed"}};
    }
    return json::parse(response_bytes->begin(), response_bytes->end(), nullptr, false);
}

} // namespace

TEST_CASE(
    "the real nexuspc-vault.exe serves create/put/get/list/export/lock/unlock over a real pipe",
    "[integration][vault]") {
    const fs::path vault_exe = find_vault_executable();
    INFO("looking for vault exe at: " << vault_exe.string());
    REQUIRE(fs::exists(vault_exe));

    const auto tag = std::chrono::steady_clock::now().time_since_epoch().count();
    const std::string pipe_name = "nexuspc-vault-test-" + std::to_string(tag);
    const fs::path scratch_base = make_scratch_dir("main");
    const fs::path vault_path = scratch_base / "vault.nxv";
    const fs::path export_path = make_scratch_dir("export") / "vault-backup.nxv";
    fs::create_directories(scratch_base);
    fs::create_directories(export_path.parent_path());

    VaultProcess process(vault_exe, vault_path, pipe_name);
    REQUIRE(process.spawned());

    auto connection = nexus::ipc::PipeClient::connect(pipe_name, std::chrono::milliseconds{5000});
    REQUIRE(connection.has_value());

    const auto status = send(*connection, {{"verb", "status"}});
    REQUIRE(status.value("ok", false));
    REQUIRE(status.value("locked", false));
    REQUIRE_FALSE(status.value("vault_exists", true));

    const auto create =
        send(*connection, {{"verb", "create"}, {"master_password", "hunter2-real-pipe"}});
    REQUIRE(create.value("ok", false));

    const json entry = {
        {"id", ""}, {"title", "Email"}, {"username", "alice"}, {"password", "s3cret-real-pipe"}};
    const auto put = send(*connection, {{"verb", "put"}, {"entry", entry}});
    REQUIRE(put.value("ok", false));
    const std::string id = put.value("id", std::string{});
    REQUIRE_FALSE(id.empty());

    const auto get = send(*connection, {{"verb", "get"}, {"id", id}});
    REQUIRE(get.value("ok", false));
    REQUIRE(get["entry"].value("password", std::string{}) == "s3cret-real-pipe");

    const auto list = send(*connection, {{"verb", "list"}});
    REQUIRE(list.value("ok", false));
    REQUIRE(list["entries"].size() == 1);
    REQUIRE_FALSE(list["entries"][0].contains("password")); // never sent over the wire in list

    const auto exported =
        send(*connection, {{"verb", "export"}, {"destination", export_path.string()}});
    REQUIRE(exported.value("ok", false));
    REQUIRE(fs::exists(export_path));

    const auto lock = send(*connection, {{"verb", "lock"}});
    REQUIRE(lock.value("ok", false));

    const auto after_lock = send(*connection, {{"verb", "list"}});
    REQUIRE_FALSE(after_lock.value("ok", true));

    const auto unlock =
        send(*connection, {{"verb", "unlock"}, {"master_password", "hunter2-real-pipe"}});
    REQUIRE(unlock.value("ok", false));

    const auto relisted = send(*connection, {{"verb", "list"}});
    REQUIRE(relisted.value("ok", false));
    REQUIRE(relisted["entries"].size() == 1);

    std::error_code ec;
    fs::remove_all(scratch_base, ec);
    fs::remove_all(export_path.parent_path(), ec);
}
