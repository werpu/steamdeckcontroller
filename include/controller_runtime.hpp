#pragma once

#include <atomic>
#include <mutex>
#include <string>
#include <thread>

namespace sdc {

struct RuntimeStatus {
    bool running = false;
    std::string state = "Stopped";
    std::string details = "No capture active.";
};

class ControllerRuntime {
public:
    ControllerRuntime();
    ~ControllerRuntime();

    ControllerRuntime(const ControllerRuntime &) = delete;
    ControllerRuntime &operator=(const ControllerRuntime &) = delete;

    bool start(std::string &message);
    bool stop(std::string &message);
    RuntimeStatus status() const;

private:
    void worker_main();
    void set_status(bool running, std::string state, std::string details);

    mutable std::mutex status_mutex_;
    RuntimeStatus status_;
    std::atomic_bool stop_requested_{false};
    std::thread worker_;
};

} // namespace sdc
