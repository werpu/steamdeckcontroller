#pragma once

#include <string>

namespace sdc {

struct RuntimeStatus {
    bool running = false;
    std::string state = "Stopped";
    std::string details = "No capture active.";
};

class ControlRuntime {
public:
    virtual ~ControlRuntime() = default;
    virtual bool start(std::string &message) = 0;
    virtual bool stop(std::string &message) = 0;
    virtual RuntimeStatus status() const = 0;
};

} // namespace sdc
