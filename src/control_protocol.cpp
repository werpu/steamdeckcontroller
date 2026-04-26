#include "control_protocol.hpp"

#include <sstream>

namespace sdc {

std::string sanitize_command_line(std::string value) {
    while (!value.empty() && (value.back() == '\n' || value.back() == '\r')) {
        value.pop_back();
    }
    return value;
}

std::string format_control_command(const std::string &command) {
    return sanitize_command_line(command) + "\n";
}

std::string format_status_response(const RuntimeStatus &status) {
    std::ostringstream out;
    out << "STATUS " << (status.running ? "RUNNING" : "STOPPED") << ' ' << status.state;
    if (!status.details.empty()) {
        out << '\n' << status.details;
    }
    return out.str();
}

FrontendResponse parse_frontend_response(const std::string &response) {
    FrontendResponse parsed;

    const auto newline = response.find('\n');
    parsed.headline = response.substr(0, newline);
    parsed.details = newline == std::string::npos ? "" : response.substr(newline + 1);

    if (response.rfind("ERR ", 0) == 0) {
        parsed.state = CaptureState::Error;
    } else if (response.rfind("STATUS RUNNING", 0) == 0 || response == "OK Started" || response == "OK Already running") {
        parsed.state = CaptureState::Running;
    } else if (response.rfind("STATUS STOPPED", 0) == 0 || response == "OK Stopped") {
        parsed.state = CaptureState::Stopped;
    }

    if (parsed.details.empty()) {
        parsed.details = response;
    }

    return parsed;
}

std::string handle_control_command(ControlRuntime &runtime, const std::string &command) {
    std::string message;
    if (command == "START") {
        runtime.start(message);
        return "OK " + message;
    }
    if (command == "STOP") {
        runtime.stop(message);
        return "OK " + message;
    }
    if (command == "STATUS") {
        return format_status_response(runtime.status());
    }
    return "ERR Unknown command";
}

} // namespace sdc
