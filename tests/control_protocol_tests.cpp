#include "control_protocol.hpp"

#include <cstdlib>
#include <iostream>
#include <string>

namespace {

int failures = 0;

void expect_true(bool condition, const std::string &name) {
    if (!condition) {
        ++failures;
        std::cerr << "FAIL: " << name << '\n';
    }
}

template <typename T, typename U>
void expect_eq(const T &actual, const U &expected, const std::string &name) {
    if (!(actual == expected)) {
        ++failures;
        std::cerr << "FAIL: " << name << " expected " << expected << " got " << actual << '\n';
    }
}

class FakeRuntime : public sdc::ControlRuntime {
public:
    bool start(std::string &message) override {
        ++start_calls;
        status_.running = true;
        status_.state = "Running";
        status_.details = "Fake forwarding";
        message = "Started";
        return true;
    }

    bool stop(std::string &message) override {
        ++stop_calls;
        status_.running = false;
        status_.state = "Stopped";
        status_.details = "Fake stopped";
        message = "Stopped";
        return true;
    }

    sdc::RuntimeStatus status() const override {
        ++status_calls;
        return status_;
    }

    int start_calls = 0;
    int stop_calls = 0;
    mutable int status_calls = 0;

private:
    sdc::RuntimeStatus status_{false, "Stopped", "No capture active."};
};

void protocol_sanitizes_command_lines() {
    expect_eq(sdc::sanitize_command_line("START\n"), std::string("START"), "line feed stripped");
    expect_eq(sdc::sanitize_command_line("STOP\r\n"), std::string("STOP"), "carriage return and line feed stripped");
    expect_eq(sdc::sanitize_command_line("STATUS"), std::string("STATUS"), "plain command unchanged");
}

void client_formats_wire_commands() {
    expect_eq(sdc::format_control_command("START"), std::string("START\n"), "client START wire command");
    expect_eq(sdc::format_control_command("STOP"), std::string("STOP\n"), "client STOP wire command");
    expect_eq(sdc::format_control_command("STATUS"), std::string("STATUS\n"), "client STATUS wire command");
    expect_eq(sdc::format_control_command("START\n"), std::string("START\n"), "client does not double-terminate command");
    expect_eq(sdc::sanitize_command_line(sdc::format_control_command("STOP")), std::string("STOP"), "server can parse client STOP wire command");
}

void protocol_formats_status_responses() {
    sdc::RuntimeStatus running{true, "Running", "Forwarding 3 devices"};
    expect_eq(
        sdc::format_status_response(running),
        std::string("STATUS RUNNING Running\nForwarding 3 devices"),
        "running status response");

    sdc::RuntimeStatus stopped{false, "Stopped", ""};
    expect_eq(
        sdc::format_status_response(stopped),
        std::string("STATUS STOPPED Stopped"),
        "stopped status response without details");
}

void frontend_response_parser_identifies_state_and_details() {
    auto running = sdc::parse_frontend_response("STATUS RUNNING Running\nForwarding devices");
    expect_eq(static_cast<int>(running.state), static_cast<int>(sdc::CaptureState::Running), "running response state");
    expect_eq(running.headline, std::string("STATUS RUNNING Running"), "running response headline");
    expect_eq(running.details, std::string("Forwarding devices"), "running response details");

    auto stopped = sdc::parse_frontend_response("OK Stopped");
    expect_eq(static_cast<int>(stopped.state), static_cast<int>(sdc::CaptureState::Stopped), "stopped response state");
    expect_eq(stopped.details, std::string("OK Stopped"), "stopped response fallback details");

    auto error = sdc::parse_frontend_response("ERR Cannot connect");
    expect_eq(static_cast<int>(error.state), static_cast<int>(sdc::CaptureState::Error), "error response state");

    auto unknown = sdc::parse_frontend_response("OK Something else");
    expect_eq(static_cast<int>(unknown.state), static_cast<int>(sdc::CaptureState::Unknown), "unknown response state");
}

void command_handler_dispatches_to_runtime() {
    FakeRuntime runtime;

    expect_eq(sdc::handle_control_command(runtime, "STATUS"), std::string("STATUS STOPPED Stopped\nNo capture active."), "initial status");
    expect_eq(runtime.status_calls, 1, "status called once");

    expect_eq(sdc::handle_control_command(runtime, "START"), std::string("OK Started"), "start response");
    expect_eq(runtime.start_calls, 1, "start called once");
    expect_eq(sdc::handle_control_command(runtime, "STATUS"), std::string("STATUS RUNNING Running\nFake forwarding"), "running status");

    expect_eq(sdc::handle_control_command(runtime, "STOP"), std::string("OK Stopped"), "stop response");
    expect_eq(runtime.stop_calls, 1, "stop called once");
    expect_eq(sdc::handle_control_command(runtime, "STATUS"), std::string("STATUS STOPPED Stopped\nFake stopped"), "stopped status");

    expect_eq(sdc::handle_control_command(runtime, "BOGUS"), std::string("ERR Unknown command"), "unknown command response");
    expect_eq(runtime.start_calls, 1, "unknown command does not start");
    expect_eq(runtime.stop_calls, 1, "unknown command does not stop");
}

} // namespace

int main() {
    protocol_sanitizes_command_lines();
    client_formats_wire_commands();
    protocol_formats_status_responses();
    frontend_response_parser_identifies_state_and_details();
    command_handler_dispatches_to_runtime();

    if (failures != 0) {
        std::cerr << failures << " test expectation(s) failed\n";
        return EXIT_FAILURE;
    }

    std::cout << "All control protocol tests passed\n";
    return EXIT_SUCCESS;
}
