#include "control_protocol.hpp"
#include "controller_runtime.hpp"

#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <grp.h>
#include <pwd.h>
#include <unistd.h>

#include <atomic>
#include <csignal>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {

constexpr const char *kSocketDir   = "/run/steamdeckcontroller";
constexpr const char *kSocketPath  = "/run/steamdeckcontroller/control.sock";
constexpr const char *kSocketGroup = "steamdeckctl";

std::atomic_bool g_shutdown_requested{false};

void on_signal(int) {
    g_shutdown_requested.store(true);
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
    return sdc::sanitize_command_line(command);
}

// Resolve the gid for kSocketGroup; returns -1 if the group does not exist.
gid_t resolve_socket_group() {
    const struct group *grp = getgrnam(kSocketGroup);
    return grp ? grp->gr_gid : static_cast<gid_t>(-1);
}

// Returns true if uid is allowed to send commands to the daemon.
// Accepts root (uid 0) and any account that is a member of kSocketGroup.
bool is_permitted_peer(uid_t uid) {
    if (uid == 0) return true;

    const struct passwd *pw = getpwuid(uid);
    if (!pw) return false;

    const struct group *grp = getgrnam(kSocketGroup);
    if (!grp) return false;

    // Primary group match
    if (pw->pw_gid == grp->gr_gid) return true;

    // Supplementary group member list
    for (char **member = grp->gr_mem; *member != nullptr; ++member) {
        if (std::strcmp(*member, pw->pw_name) == 0) return true;
    }

    return false;
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

    // Restrict socket access to the steamdeckctl group (mode 0660).
    // If the group does not exist fall back to 0600 (root-only) rather than
    // 0666 (world-writable), so rogue processes cannot reach the daemon.
    const gid_t gid = resolve_socket_group();
    if (gid != static_cast<gid_t>(-1)) {
        chown(kSocketPath, 0, gid);
        chmod(kSocketPath, 0660);
    } else {
        std::cerr << "steamdeckcontrollerd: group '" << kSocketGroup
                  << "' not found — socket restricted to root (0600).\n"
                  << "Create the group and add your user: "
                  << "groupadd " << kSocketGroup
                  << " && usermod -aG " << kSocketGroup << " $USER\n";
        chmod(kSocketPath, 0600);
    }

    if (listen(fd, 8) < 0) {
        const std::string error = std::strerror(errno);
        close(fd);
        throw std::runtime_error("listen failed: " + error);
    }

    return fd;
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

            // Verify the connecting process's credentials before reading anything.
            struct ucred cred{};
            socklen_t cred_len = sizeof(cred);
            if (getsockopt(client_fd, SOL_SOCKET, SO_PEERCRED, &cred, &cred_len) < 0) {
                std::cerr << "steamdeckcontrollerd: SO_PEERCRED failed: "
                          << std::strerror(errno) << '\n';
                close(client_fd);
                continue;
            }
            if (!is_permitted_peer(cred.uid)) {
                std::cerr << "steamdeckcontrollerd: rejected connection from uid="
                          << cred.uid << " pid=" << cred.pid << '\n';
                close(client_fd);
                continue;
            }

            const std::string command = read_command(client_fd);
            const std::string response = sdc::handle_control_command(runtime, command);
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
