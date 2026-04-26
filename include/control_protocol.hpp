#pragma once

#include "control_runtime.hpp"

#include <string>

namespace sdc {

enum class CaptureState {
    Unknown,
    Running,
    Stopped,
    Error
};

struct FrontendResponse {
    CaptureState state = CaptureState::Unknown;
    std::string headline;
    std::string details;
};

std::string sanitize_command_line(std::string value);
std::string format_control_command(const std::string &command);
std::string format_status_response(const RuntimeStatus &status);
FrontendResponse parse_frontend_response(const std::string &response);

std::string handle_control_command(ControlRuntime &runtime, const std::string &command);

} // namespace sdc
