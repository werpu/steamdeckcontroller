#include "controller_runtime.hpp"

#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

#include <atomic>
#include <csignal>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>

namespace {

constexpr const char *kSocketDir = "/run/steamdeckcontroller";
constexpr const char *kSocketPath = "/run/steamdeckcontroller/control.sock";

std::atomic_bool g_shutdown_requested{false};

void on_signal(int) {
    g_shutdown_requested.store(true);
}

std::string sanitize_line(std::string value) {
    while (!value.empty() && (value.back() == '\n' || value.back() == '\r')) {
        value.pop_back();
    }
    return value;
}

std::string status_response(const sdc::RuntimeStatus &status) {
    std::ostringstream out;
    out << "STATUS " << (status.running ? "RUNNING" : "STOPPED") << ' ' << status.state;
    if (!status.details.empty()) {
        out << '\n' << status.details;
    }
    return out.str();
}

void write_all(int fd, const std::string &message) {
    std::string wire = message;
    wire.push_back('\n');
    const char *data = wire.data();
    size_t remaining = wire.size();
    while (remaining > 0) {
        const ssize_t written = write(fd, data, remaining);
        if (written <= 0) {
            return;
        }
        data += written;
        remaining -= static_cast<size_t>(written);
    }
}

std::string read_command(int fd) {
    std::string command;
    char buffer[128]{};
    while (command.find('\n') == std::string::npos && command.size() < 4096) {
        const ssize_t read_count = read(fd, buffer, sizeof(buffer));
        if (read_count <= 0) {
            break;
        }
        command.append(buffer, static_cast<size_t>(read_count));
    }
    const auto newline = command.find('\n');
    if (newline != std::string::npos) {
        command.resize(newline);
    }
    return sanitize_line(command);
}

int create_server_socket() {
    std::filesystem::create_directories(kSocketDir);
    chmod(kSocketDir, 0755);
    unlink(kSocketPath);

    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) {
        throw std::runtime_error(std::string("socket failed: ") + std::strerror(errno));
    }

    sockaddr_un address{};
    address.sun_family = AF_UNIX;
    std::strncpy(address.sun_path, kSocketPath, sizeof(address.sun_path) - 1);

    if (bind(fd, reinterpret_cast<sockaddr *>(&address), sizeof(address)) < 0) {
        const std::string error = std::strerror(errno);
        close(fd);
        throw std::runtime_error("bind failed: " + error);
    }
    chmod(kSocketPath, 0666);

    if (listen(fd, 8) < 0) {
        const std::string error = std::strerror(errno);
        close(fd);
        throw std::runtime_error("listen failed: " + error);
    }

    return fd;
}

std::string handle_command(sdc::ControllerRuntime &runtime, const std::string &command) {
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
        return status_response(runtime.status());
    }
    return "ERR Unknown command";
}

} // namespace

int main() {
    std::signal(SIGINT, on_signal);
    std::signal(SIGTERM, on_signal);

    sdc::ControllerRuntime runtime;
    int server_fd = -1;

    try {
        server_fd = create_server_socket();
        std::cout << "steamdeckcontrollerd listening on " << kSocketPath << '\n';

        while (!g_shutdown_requested.load()) {
            int client_fd = accept(server_fd, nullptr, nullptr);
            if (client_fd < 0) {
                if (errno == EINTR) {
                    continue;
                }
                throw std::runtime_error(std::string("accept failed: ") + std::strerror(errno));
            }

            const std::string command = read_command(client_fd);
            const std::string response = handle_command(runtime, command);
            write_all(client_fd, response);
            close(client_fd);
        }
    } catch (const std::exception &ex) {
        std::cerr << "steamdeckcontrollerd: " << ex.what() << '\n';
        if (server_fd >= 0) {
            close(server_fd);
        }
        unlink(kSocketPath);
        return 1;
    }

    std::string ignored;
    runtime.stop(ignored);
    if (server_fd >= 0) {
        close(server_fd);
    }
    unlink(kSocketPath);
    return 0;
}
