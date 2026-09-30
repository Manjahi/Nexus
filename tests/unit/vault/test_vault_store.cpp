#include "nexus/vault/vault_store.hpp"

#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>

namespace fs = std::filesystem;
using namespace nexus::vault;

namespace {

struct Scratch {
    fs::path path;
    Scratch() {
        const auto tag = std::chrono::steady_clock::now().time_since_epoch().count();
        path = fs::temp_directory_path() / ("nexuspc_vaultstore_" + std::to_string(tag) + ".nxv");
    }
    ~Scratch() {
        std::error_code ec;
        fs::remove(path, ec);
        fs::remove(fs::path(path.string() + ".tmp"), ec);
    }
};

nexus::crypto::KdfParams cheap_params() {
    return nexus::crypto::KdfParams{/*opslimit=*/1, /*memlimit=*/8 * 1024 * 1024};
}

Entry make_entry(std::string title, std::string password) {
    Entry e;
    e.title = std::move(title);
    e.password = std::move(password);
    return e;
}

} // namespace

TEST_CASE("a fresh store is locked and reports no vault until created", "[vault][store]") {
    Scratch scratch;
    VaultStore store(scratch.path);
    REQUIRE(store.locked());
    REQUIRE_FALSE(store.vault_exists());
}

TEST_CASE("create unlocks the store immediately", "[vault][store]") {
    Scratch scratch;
    VaultStore store(scratch.path);
    REQUIRE(store.create("hunter2", cheap_params()));
    REQUIRE_FALSE(store.locked());
    REQUIRE(store.vault_exists());
    REQUIRE(store.entry_count() == 0);
}

TEST_CASE("put creates an entry, list/get see it, and it survives a lock/unlock cycle",
          "[vault][store]") {
    Scratch scratch;
    VaultStore store(scratch.path);
    REQUIRE(store.create("hunter2", cheap_params()));

    const auto id = store.put(make_entry("Email", "correct-horse-battery-staple"));
    REQUIRE(id.has_value());
    REQUIRE(store.entry_count() == 1);

    const auto listed = store.list();
    REQUIRE(listed.size() == 1);
    REQUIRE(listed[0].id == *id);
    REQUIRE(listed[0].title == "Email");

    store.lock();
    REQUIRE(store.locked());
    REQUIRE(store.list().empty());
    REQUIRE_FALSE(store.get(*id).has_value());

    REQUIRE(store.unlock("hunter2"));
    const auto fetched = store.get(*id);
    REQUIRE(fetched.has_value());
    REQUIRE(fetched->password == "correct-horse-battery-staple");
}

TEST_CASE("put with an unknown id fails (it's an update, not a create)", "[vault][store]") {
    Scratch scratch;
    VaultStore store(scratch.path);
    REQUIRE(store.create("hunter2", cheap_params()));

    Entry e;
    e.id = "does-not-exist";
    e.title = "Ghost";
    REQUIRE_FALSE(store.put(e).has_value());
}

TEST_CASE("put with an existing id updates in place", "[vault][store]") {
    Scratch scratch;
    VaultStore store(scratch.path);
    REQUIRE(store.create("hunter2", cheap_params()));
    const auto id = store.put(make_entry("Email", "old-password"));
    REQUIRE(id.has_value());

    Entry updated;
    updated.id = *id;
    updated.title = "Email (renamed)";
    updated.password = "new-password";
    REQUIRE(store.put(updated) == id);
    REQUIRE(store.entry_count() == 1);

    const auto fetched = store.get(*id);
    REQUIRE(fetched->title == "Email (renamed)");
    REQUIRE(fetched->password == "new-password");
}

TEST_CASE("remove deletes an entry and persists", "[vault][store]") {
    Scratch scratch;
    VaultStore store(scratch.path);
    REQUIRE(store.create("hunter2", cheap_params()));
    const auto id = store.put(make_entry("Temp", "pw"));
    REQUIRE(id.has_value());

    REQUIRE(store.remove(*id));
    REQUIRE_FALSE(store.remove(*id)); // already gone
    REQUIRE(store.entry_count() == 0);

    REQUIRE(store.unlock(
        "hunter2")); // re-unlock is a no-op while already unlocked... but verify persistence:
    store.lock();
    REQUIRE(store.unlock("hunter2"));
    REQUIRE(store.entry_count() == 0);
}

TEST_CASE("operations on a locked store fail without touching disk", "[vault][store]") {
    Scratch scratch;
    VaultStore store(scratch.path);
    REQUIRE_FALSE(store.put(make_entry("x", "y")).has_value());
    REQUIRE_FALSE(store.remove("anything"));
    REQUIRE(store.list().empty());
    REQUIRE(store.health().empty());
}

TEST_CASE("health flags weak, reused, and old entries", "[vault][store]") {
    Scratch scratch;
    VaultStore store(scratch.path);
    REQUIRE(store.create("hunter2", cheap_params()));

    const auto weak_id = store.put(make_entry("Weak", "abc123"));
    const auto strong_id = store.put(make_entry("Strong", "Tr0ub4dor&3xtra!Long"));
    const auto reused_a = store.put(make_entry("Reused A", "SamePassword123!"));
    const auto reused_b = store.put(make_entry("Reused B", "SamePassword123!"));
    REQUIRE(weak_id.has_value());
    REQUIRE(strong_id.has_value());
    REQUIRE(reused_a.has_value());
    REQUIRE(reused_b.has_value());

    const auto findings = store.health();

    const auto has_issue = [&](const std::string& id, HealthIssue issue) {
        return std::any_of(findings.begin(), findings.end(), [&](const HealthFinding& f) {
            return f.entry_id == id && f.issue == issue;
        });
    };

    REQUIRE(has_issue(*weak_id, HealthIssue::Weak));
    REQUIRE_FALSE(has_issue(*strong_id, HealthIssue::Weak));
    REQUIRE(has_issue(*reused_a, HealthIssue::Reused));
    REQUIRE(has_issue(*reused_b, HealthIssue::Reused));
    REQUIRE_FALSE(has_issue(*strong_id, HealthIssue::Reused));
}

// A secure note has an empty password field by construction (nothing in
// the UI ever sets one for EntryKind::SecureNote) - before health()
// special-cased EntryKind::Password, every note would have shown up as a
// spurious "Weak" finding purely because is_weak_password("") is true.
TEST_CASE("health never flags a secure note as a weak password", "[vault][store]") {
    Scratch scratch;
    VaultStore store(scratch.path);
    REQUIRE(store.create("hunter2", cheap_params()));

    Entry note;
    note.kind = EntryKind::SecureNote;
    note.title = "Recovery codes";
    note.notes = "1234-5678-9012";
    const auto id = store.put(note);
    REQUIRE(id.has_value());

    const auto findings = store.health();
    REQUIRE(std::none_of(findings.begin(), findings.end(),
                         [&](const HealthFinding& f) { return f.entry_id == *id; }));
}

TEST_CASE("export_to seals a second file that unlocks with the same password", "[vault][store]") {
    Scratch scratch;
    Scratch export_scratch;
    VaultStore store(scratch.path);
    REQUIRE(store.create("hunter2", cheap_params()));
    REQUIRE(store.put(make_entry("Email", "correct-horse-battery-staple")).has_value());

    REQUIRE(store.export_to(export_scratch.path));

    VaultStore exported(export_scratch.path);
    REQUIRE(exported.vault_exists());
    REQUIRE_FALSE(exported.unlock("wrong-password"));
    REQUIRE(exported.unlock("hunter2"));
    REQUIRE(exported.entry_count() == 1);
    const auto listed = exported.list();
    REQUIRE(listed[0].title == "Email");
}

TEST_CASE("export_to refuses to overwrite an existing file", "[vault][store]") {
    Scratch scratch;
    Scratch export_scratch;
    VaultStore store(scratch.path);
    REQUIRE(store.create("hunter2", cheap_params()));

    // A pre-existing file at the destination - export_to() must not clobber it.
    {
        std::ofstream out(export_scratch.path, std::ios::binary);
        out << "not a vault";
    }
    REQUIRE_FALSE(store.export_to(export_scratch.path));
}

TEST_CASE("export_to fails while locked", "[vault][store]") {
    Scratch scratch;
    Scratch export_scratch;
    VaultStore store(scratch.path);
    REQUIRE_FALSE(store.export_to(export_scratch.path));
}
