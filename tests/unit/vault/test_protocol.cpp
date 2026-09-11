#include "nexus/vault/protocol.hpp"

#include <nlohmann/json.hpp>

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <filesystem>
#include <string>
#include <system_error>

#include "nexus/vault/vault_store.hpp"

namespace fs = std::filesystem;
using namespace nexus::vault;
using json = nlohmann::json;

namespace {

struct Scratch {
    fs::path path;
    Scratch() {
        const auto tag = std::chrono::steady_clock::now().time_since_epoch().count();
        path = fs::temp_directory_path() / ("nexuspc_protocol_" + std::to_string(tag) + ".nxv");
    }
    ~Scratch() {
        std::error_code ec;
        fs::remove(path, ec);
        fs::remove(fs::path(path.string() + ".tmp"), ec);
    }
};

// The protocol layer doesn't take KdfParams (real vaults always use the
// interactive profile), so these tests pay the real Argon2id cost - keep the
// count of create()/unlock() calls per test small.

} // namespace

TEST_CASE("status on a fresh path reports locked, no vault, zero entries", "[vault][protocol]") {
    Scratch scratch;
    VaultStore store(scratch.path);

    const auto response = handle_request(store, json{{"verb", "status"}});
    REQUIRE(response["ok"] == true);
    REQUIRE(response["locked"] == true);
    REQUIRE(response["vault_exists"] == false);
    REQUIRE(response["entry_count"] == 0);
}

TEST_CASE("create requires a non-empty password", "[vault][protocol]") {
    Scratch scratch;
    VaultStore store(scratch.path);

    const auto response = handle_request(store, json{{"verb", "create"}, {"master_password", ""}});
    REQUIRE(response["ok"] == false);
}

TEST_CASE("create unlocks the vault; a second create fails", "[vault][protocol]") {
    Scratch scratch;
    VaultStore store(scratch.path);

    const auto first = handle_request(store, json{{"verb", "create"}, {"master_password", "hunter2"}});
    REQUIRE(first["ok"] == true);
    REQUIRE(store.locked() == false);

    const auto second =
        handle_request(store, json{{"verb", "create"}, {"master_password", "hunter2"}});
    REQUIRE(second["ok"] == false);
}

TEST_CASE("lock, then unlock with right/wrong password", "[vault][protocol]") {
    Scratch scratch;
    VaultStore store(scratch.path);
    REQUIRE(handle_request(store, json{{"verb", "create"}, {"master_password", "hunter2"}})["ok"] ==
           true);

    REQUIRE(handle_request(store, json{{"verb", "lock"}})["ok"] == true);
    REQUIRE(store.locked());

    const auto wrong =
        handle_request(store, json{{"verb", "unlock"}, {"master_password", "nope"}});
    REQUIRE(wrong["ok"] == false);
    REQUIRE(store.locked());

    const auto right =
        handle_request(store, json{{"verb", "unlock"}, {"master_password", "hunter2"}});
    REQUIRE(right["ok"] == true);
    REQUIRE_FALSE(store.locked());
}

TEST_CASE("list/get/put/delete/health all fail while locked", "[vault][protocol]") {
    Scratch scratch;
    VaultStore store(scratch.path);

    REQUIRE(handle_request(store, json{{"verb", "list"}})["ok"] == false);
    REQUIRE(handle_request(store, json{{"verb", "get"}, {"id", "x"}})["ok"] == false);
    REQUIRE(handle_request(store, json{{"verb", "put"}, {"entry", json{{"id", ""}}}})["ok"] ==
           false);
    REQUIRE(handle_request(store, json{{"verb", "delete"}, {"id", "x"}})["ok"] == false);
    REQUIRE(handle_request(store, json{{"verb", "health"}})["ok"] == false);
}

TEST_CASE("put creates an entry, get returns it, list omits the password", "[vault][protocol]") {
    Scratch scratch;
    VaultStore store(scratch.path);
    REQUIRE(handle_request(store, json{{"verb", "create"}, {"master_password", "hunter2"}})["ok"] ==
           true);

    json entry = {{"id", ""}, {"title", "Email"}, {"username", "alice"}, {"password", "s3cret"}};
    const auto put_response = handle_request(store, json{{"verb", "put"}, {"entry", entry}});
    REQUIRE(put_response["ok"] == true);
    const std::string id = put_response["id"];
    REQUIRE_FALSE(id.empty());

    const auto get_response = handle_request(store, json{{"verb", "get"}, {"id", id}});
    REQUIRE(get_response["ok"] == true);
    REQUIRE(get_response["entry"]["title"] == "Email");
    REQUIRE(get_response["entry"]["password"] == "s3cret");

    const auto list_response = handle_request(store, json{{"verb", "list"}});
    REQUIRE(list_response["ok"] == true);
    REQUIRE(list_response["entries"].size() == 1);
    REQUIRE(list_response["entries"][0]["title"] == "Email");
    REQUIRE_FALSE(list_response["entries"][0].contains("password"));
}

TEST_CASE("put with an unknown id fails; delete then re-delete fails", "[vault][protocol]") {
    Scratch scratch;
    VaultStore store(scratch.path);
    REQUIRE(handle_request(store, json{{"verb", "create"}, {"master_password", "hunter2"}})["ok"] ==
           true);

    const auto bad_update = handle_request(
        store, json{{"verb", "put"}, {"entry", json{{"id", "ghost"}, {"title", "x"}}}});
    REQUIRE(bad_update["ok"] == false);

    const auto put_response = handle_request(
        store, json{{"verb", "put"}, {"entry", json{{"id", ""}, {"title", "Temp"}}}});
    REQUIRE(put_response["ok"] == true);
    const std::string id = put_response["id"];

    REQUIRE(handle_request(store, json{{"verb", "delete"}, {"id", id}})["ok"] == true);
    REQUIRE(handle_request(store, json{{"verb", "delete"}, {"id", id}})["ok"] == false);
}

TEST_CASE("generate_password honors length and rejects an invalid policy", "[vault][protocol]") {
    Scratch scratch;
    VaultStore store(scratch.path);

    const auto default_response = handle_request(store, json{{"verb", "generate_password"}});
    REQUIRE(default_response["ok"] == true);
    REQUIRE(default_response["password"].get<std::string>().size() == 20);

    const auto sized =
        handle_request(store, json{{"verb", "generate_password"}, {"length", 8}});
    REQUIRE(sized["ok"] == true);
    REQUIRE(sized["password"].get<std::string>().size() == 8);

    const auto invalid = handle_request(
        store, json{{"verb", "generate_password"},
                    {"lowercase", false},
                    {"uppercase", false},
                    {"digits", false},
                    {"symbols", false}});
    REQUIRE(invalid["ok"] == false);
}

TEST_CASE("health reports findings for the unlocked vault", "[vault][protocol]") {
    Scratch scratch;
    VaultStore store(scratch.path);
    REQUIRE(handle_request(store, json{{"verb", "create"}, {"master_password", "hunter2"}})["ok"] ==
           true);
    REQUIRE(handle_request(store,
                           json{{"verb", "put"},
                                {"entry", json{{"id", ""}, {"title", "Weak"}, {"password", "123"}}}})
               ["ok"] == true);

    const auto response = handle_request(store, json{{"verb", "health"}});
    REQUIRE(response["ok"] == true);
    REQUIRE(response["findings"].size() >= 1);
}

TEST_CASE("an unknown verb and a missing verb both fail", "[vault][protocol]") {
    Scratch scratch;
    VaultStore store(scratch.path);

    REQUIRE(handle_request(store, json{{"verb", "not_a_real_verb"}})["ok"] == false);
    REQUIRE(handle_request(store, json{{"nope", 1}})["ok"] == false);
}
