#include "nexus/ipc/pipe.hpp"

#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <future>
#include <string>
#include <thread>
#include <vector>

using namespace nexus::ipc;

namespace {
std::string unique_pipe_name(const char* tag) {
    const auto id = std::chrono::steady_clock::now().time_since_epoch().count();
    return "nexuspc_test_" + std::string(tag) + "_" + std::to_string(id);
}
} // namespace

TEST_CASE("client and server exchange a request/response message", "[ipc][pipe]") {
    const std::string name = unique_pipe_name("echo");
    PipeServer server(name);

    auto server_future = std::async(std::launch::async, [&server]() -> std::vector<std::uint8_t> {
        auto conn = server.accept();
        if (!conn) {
            return {};
        }
        const auto request = conn->receive();
        if (!request) {
            return {};
        }
        if (!conn->send(*request)) { // echo
            return {};
        }
        return *request;
    });

    auto client = PipeClient::connect(name, std::chrono::milliseconds{2000});
    REQUIRE(client.has_value());

    const std::vector<std::uint8_t> message{1, 2, 3, 4, 5};
    REQUIRE(client->send(message));

    const auto response = client->receive();
    REQUIRE(response.has_value());
    REQUIRE(*response == message);

    const auto server_saw = server_future.get();
    REQUIRE(server_saw == message);
}

TEST_CASE("send and receive round-trip an empty message", "[ipc][pipe]") {
    const std::string name = unique_pipe_name("empty");
    PipeServer server(name);

    // Assertions run on the main thread only - Catch2 REQUIRE isn't safe to
    // call concurrently from multiple threads in one TEST_CASE, so the
    // background thread just reports what it saw.
    auto server_future = std::async(std::launch::async, [&server] {
        auto conn = server.accept();
        if (!conn) {
            return false;
        }
        const auto request = conn->receive();
        if (!request || !request->empty()) {
            return false;
        }
        return conn->send({});
    });

    auto client = PipeClient::connect(name, std::chrono::milliseconds{2000});
    REQUIRE(client.has_value());
    REQUIRE(client->send({}));

    const auto response = client->receive();
    REQUIRE(response.has_value());
    REQUIRE(response->empty());

    REQUIRE(server_future.get());
}

TEST_CASE("connect fails within the timeout when nothing is listening", "[ipc][pipe]") {
    const std::string name = unique_pipe_name("nobody_home");
    const auto start = std::chrono::steady_clock::now();
    const auto client = PipeClient::connect(name, std::chrono::milliseconds{300});
    const auto elapsed = std::chrono::steady_clock::now() - start;

    REQUIRE_FALSE(client.has_value());
    REQUIRE(elapsed < std::chrono::seconds{2});
}

TEST_CASE("receive returns nullopt after the peer disconnects", "[ipc][pipe]") {
    const std::string name = unique_pipe_name("hangup");
    PipeServer server(name);

    auto server_future = std::async(std::launch::async, [&server] {
        auto conn = server.accept();
        if (!conn) {
            return false;
        }
        conn->close(); // hang up without sending anything
        return true;
    });

    auto client = PipeClient::connect(name, std::chrono::milliseconds{2000});
    REQUIRE(client.has_value());

    REQUIRE(server_future.get());
    REQUIRE_FALSE(client->receive().has_value());
}

TEST_CASE("server close unblocks a pending accept", "[ipc][pipe]") {
    const std::string name = unique_pipe_name("shutdown");
    PipeServer server(name);

    auto accept_future = std::async(std::launch::async, [&server] { return server.accept(); });

    // Give the accept() call a moment to actually block inside ConnectNamedPipe.
    std::this_thread::sleep_for(std::chrono::milliseconds{100});
    server.close();

    const auto status = accept_future.wait_for(std::chrono::seconds{2});
    REQUIRE(status == std::future_status::ready);
    REQUIRE_FALSE(accept_future.get().has_value());
}
