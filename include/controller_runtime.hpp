#pragma once

#include "control_runtime.hpp"

#include <atomic>
#include <mutex>
#include <string>
#include <thread>

namespace sdc {

class ControllerRuntime : public ControlRuntime {
public:
    ControllerRuntime();
    ~ControllerRuntime();

    ControllerRuntime(const ControllerRuntime &) = delete;
    ControllerRuntime &operator=(const ControllerRuntime &) = delete;

    bool start(std::string &message) override;
    bool stop(std::string &message) override;
    RuntimeStatus status() const override;

private:
    void worker_main();
    void set_status(bool running, std::string state, std::string details);

    mutable std::mutex status_mutex_;
    RuntimeStatus status_;
    std::atomic_bool stop_requested_{false};
    std::thread worker_;
};

} // namespace sdc
