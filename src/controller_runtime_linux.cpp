#include "controller_runtime.hpp"

#include "input_translation.hpp"

#include <linux/input.h>
#include <sys/ioctl.h>
#include <sys/poll.h>
#include <fcntl.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <cerrno>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <optional>
#include <set>
#include <sstream>
#include <stdexcept>
#include <vector>

namespace {

constexpr const char *kGadgetPath = "/sys/kernel/config/usb_gadget/sdc_passthrough";

enum class DeviceKind {
    Keyboard,
    Mouse,
    Gamepad
};

struct InputDevice {
    int fd = -1;
    std::string path;
    std::string name;
    DeviceKind kind;
    std::map<int, input_absinfo> abs_info;
};

bool test_bit(const std::vector<unsigned long> &bits, int bit) {
    constexpr int bits_per_long = static_cast<int>(sizeof(unsigned long) * 8);
    const auto idx = static_cast<size_t>(bit / bits_per_long);
    if (idx >= bits.size()) {
        return false;
    }
    return (bits[idx] & (1UL << (bit % bits_per_long))) != 0;
}

std::vector<unsigned long> ioctl_bits(int fd, unsigned long request, size_t max_bit) {
    const size_t words = (max_bit + sizeof(unsigned long) * 8) / (sizeof(unsigned long) * 8);
    std::vector<unsigned long> bits(words, 0);
    ioctl(fd, request, bits.data());
    return bits;
}

std::string read_first_udc() {
    const std::filesystem::path udc_dir("/sys/class/udc");
    if (!std::filesystem::exists(udc_dir)) {
        return {};
    }
    for (const auto &entry : std::filesystem::directory_iterator(udc_dir)) {
        return entry.path().filename().string();
    }
    return {};
}

std::string describe_path_state(const std::filesystem::path &path) {
    std::ostringstream out;
    std::error_code ec;
    out << path << ": ";
    if (!std::filesystem::exists(path, ec)) {
        out << "missing";
        if (ec) {
            out << " (" << ec.message() << ")";
        }
        return out.str();
    }

    const auto status = std::filesystem::status(path, ec);
    if (ec) {
        out << "status error (" << ec.message() << ")";
        return out.str();
    }

    out << (std::filesystem::is_directory(status) ? "directory" : "not directory");
    out << ", permissions " << std::oct << static_cast<unsigned>(status.permissions() & std::filesystem::perms::all);
    return out.str();
}

void ensure_directory(const std::filesystem::path &path) {
    std::error_code ec;
    std::filesystem::create_directories(path, ec);
    if (ec) {
        std::ostringstream message;
        message << "Cannot create " << path << ": " << ec.message()
                << "\n" << describe_path_state(path.parent_path())
                << "\nCheck that ConfigFS is mounted and that the daemon is running as uid 0.";
        throw std::runtime_error(message.str());
    }
}

void write_text(const std::filesystem::path &path, const std::string &value) {
    std::ofstream out(path);
    if (!out) {
        throw std::runtime_error("Cannot write " + path.string() + ": " + std::strerror(errno));
    }
    out << value;
}

void write_binary(const std::filesystem::path &path, const std::vector<uint8_t> &value) {
    std::ofstream out(path, std::ios::binary);
    if (!out) {
        throw std::runtime_error("Cannot write " + path.string() + ": " + std::strerror(errno));
    }
    out.write(reinterpret_cast<const char *>(value.data()), static_cast<std::streamsize>(value.size()));
}

void write_text_if_exists(const std::filesystem::path &path, const std::string &value) {
    if (std::filesystem::exists(path)) {
        write_text(path, value);
    }
}

void ensure_symlink(const std::filesystem::path &target, const std::filesystem::path &link) {
    std::error_code ec;
    if (!std::filesystem::exists(link, ec)) {
        std::filesystem::create_symlink(target, link, ec);
        if (ec) {
            throw std::runtime_error("Cannot link " + link.string() + ": " + ec.message());
        }
    }
}

void setup_gadget() {
    const std::string udc = read_first_udc();
    if (udc.empty()) {
        throw std::runtime_error("No USB device controller found in /sys/class/udc.");
    }

    ensure_directory(kGadgetPath);
    const std::filesystem::path gadget(kGadgetPath);

    write_text(gadget / "idVendor", "0x1d6b");
    write_text(gadget / "idProduct", "0x0104");
    write_text(gadget / "bcdDevice", "0x0100");
    write_text(gadget / "bcdUSB", "0x0200");

    ensure_directory(gadget / "strings/0x409");
    write_text(gadget / "strings/0x409/serialnumber", "sdc-0001");
    write_text(gadget / "strings/0x409/manufacturer", "SteamDeckController");
    write_text(gadget / "strings/0x409/product", "Input Passthrough Xbox-style HID");

    ensure_directory(gadget / "configs/c.1/strings/0x409");
    write_text(gadget / "configs/c.1/MaxPower", "250");
    write_text(gadget / "configs/c.1/strings/0x409/configuration", "HID keyboard mouse Xbox-style gamepad");

    const std::vector<uint8_t> keyboard_desc = {
        0x05, 0x01, 0x09, 0x06, 0xa1, 0x01, 0x05, 0x07, 0x19, 0xe0, 0x29, 0xe7,
        0x15, 0x00, 0x25, 0x01, 0x75, 0x01, 0x95, 0x08, 0x81, 0x02, 0x95, 0x01,
        0x75, 0x08, 0x81, 0x01, 0x95, 0x06, 0x75, 0x08, 0x15, 0x00, 0x25, 0x65,
        0x05, 0x07, 0x19, 0x00, 0x29, 0x65, 0x81, 0x00, 0xc0
    };
    const std::vector<uint8_t> mouse_desc = {
        0x05, 0x01, 0x09, 0x02, 0xa1, 0x01, 0x09, 0x01, 0xa1, 0x00, 0x05, 0x09,
        0x19, 0x01, 0x29, 0x05, 0x15, 0x00, 0x25, 0x01, 0x95, 0x05, 0x75, 0x01,
        0x81, 0x02, 0x95, 0x01, 0x75, 0x03, 0x81, 0x01, 0x05, 0x01, 0x09, 0x30,
        0x09, 0x31, 0x09, 0x38, 0x15, 0x81, 0x25, 0x7f, 0x75, 0x08, 0x95, 0x03,
        0x81, 0x06, 0xc0, 0xc0
    };
    const std::vector<uint8_t> gamepad_desc = {
        0x05, 0x01, 0x09, 0x05, 0xa1, 0x01,
        0x05, 0x09, 0x19, 0x01, 0x29, 0x10, 0x15, 0x00, 0x25, 0x01, 0x75, 0x01,
        0x95, 0x10, 0x81, 0x02,
        0x05, 0x01, 0x09, 0x39, 0x15, 0x00, 0x25, 0x08, 0x35, 0x00, 0x46, 0x3b,
        0x01, 0x65, 0x14, 0x75, 0x04, 0x95, 0x01, 0x81, 0x42,
        0x75, 0x04, 0x95, 0x01, 0x81, 0x03,
        0x09, 0x32, 0x09, 0x35, 0x15, 0x00, 0x26, 0xff, 0x00, 0x75, 0x08, 0x95,
        0x02, 0x81, 0x02,
        0x09, 0x30, 0x09, 0x31, 0x09, 0x33, 0x09, 0x34, 0x16, 0x00, 0x80, 0x26,
        0xff, 0x7f, 0x75, 0x10, 0x95, 0x04, 0x81, 0x02,
        0xc0
    };

    struct HidFunction {
        const char *name;
        int protocol;
        int subclass;
        int report_len;
        const std::vector<uint8_t> &desc;
    };
    const std::array functions = {
        HidFunction{"hid.usb0", 1, 1, 8, keyboard_desc},
        HidFunction{"hid.usb1", 2, 1, 4, mouse_desc},
        HidFunction{"hid.usb2", 0, 0, static_cast<int>(sdc::XboxHidReport::size), gamepad_desc},
    };

    for (const auto &fn : functions) {
        const auto dir = gadget / "functions" / fn.name;
        ensure_directory(dir);
        write_text(dir / "protocol", std::to_string(fn.protocol));
        write_text(dir / "subclass", std::to_string(fn.subclass));
        write_text(dir / "report_length", std::to_string(fn.report_len));
        write_binary(dir / "report_desc", fn.desc);
        write_text_if_exists(dir / "interval", "1");
        ensure_symlink(dir, gadget / "configs/c.1" / fn.name);
    }

    std::string current;
    {
        std::ifstream in(gadget / "UDC");
        std::getline(in, current);
    }
    if (current.empty()) {
        write_text(gadget / "UDC", udc);
    }
}

void unbind_gadget() {
    const std::filesystem::path gadget(kGadgetPath);
    if (!std::filesystem::exists(gadget / "UDC")) {
        return;
    }
    std::ofstream out(gadget / "UDC");
    if (out) {
        out << "";
    }
}

int open_hidg(int index) {
    const std::string path = "/dev/hidg" + std::to_string(index);
    int fd = open(path.c_str(), O_WRONLY | O_NONBLOCK);
    if (fd < 0) {
        throw std::runtime_error("Cannot open " + path + ": " + std::strerror(errno));
    }
    return fd;
}

void write_report(int fd, const uint8_t *data, size_t len) {
    const ssize_t written = write(fd, data, len);
    (void)written;
}

std::optional<DeviceKind> classify_device(int fd) {
    const auto ev_bits = ioctl_bits(fd, EVIOCGBIT(0, EV_MAX), EV_MAX);
    const auto key_bits = ioctl_bits(fd, EVIOCGBIT(EV_KEY, KEY_MAX), KEY_MAX);
    const auto rel_bits = ioctl_bits(fd, EVIOCGBIT(EV_REL, REL_MAX), REL_MAX);
    const auto abs_bits = ioctl_bits(fd, EVIOCGBIT(EV_ABS, ABS_MAX), ABS_MAX);

    if (test_bit(ev_bits, EV_REL) && test_bit(rel_bits, REL_X) && test_bit(rel_bits, REL_Y)) {
        return DeviceKind::Mouse;
    }
    if (test_bit(ev_bits, EV_ABS) && (test_bit(key_bits, BTN_GAMEPAD) || test_bit(key_bits, BTN_SOUTH) || test_bit(abs_bits, ABS_X))) {
        return DeviceKind::Gamepad;
    }
    if (test_bit(ev_bits, EV_KEY) && test_bit(key_bits, KEY_A) && test_bit(key_bits, KEY_ENTER)) {
        return DeviceKind::Keyboard;
    }
    return std::nullopt;
}

std::vector<InputDevice> open_input_devices() {
    std::vector<InputDevice> devices;
    for (const auto &entry : std::filesystem::directory_iterator("/dev/input")) {
        const auto path = entry.path().string();
        if (entry.path().filename().string().rfind("event", 0) != 0) {
            continue;
        }
        int fd = open(path.c_str(), O_RDONLY | O_NONBLOCK);
        if (fd < 0) {
            continue;
        }
        auto kind = classify_device(fd);
        if (!kind) {
            close(fd);
            continue;
        }

        std::array<char, 256> name{};
        ioctl(fd, EVIOCGNAME(name.size()), name.data());
        InputDevice device{fd, path, name.data(), *kind, {}};
        if (*kind == DeviceKind::Gamepad) {
            for (int code : {ABS_X, ABS_Y, ABS_RX, ABS_RY, ABS_Z, ABS_RZ, ABS_HAT0X, ABS_HAT0Y}) {
                input_absinfo info{};
                if (ioctl(fd, EVIOCGABS(code), &info) == 0) {
                    device.abs_info[code] = info;
                }
            }
        }
        ioctl(fd, EVIOCGRAB, 1);
        devices.push_back(std::move(device));
    }
    return devices;
}

void release_devices(std::vector<InputDevice> &devices) {
    for (auto &device : devices) {
        if (device.fd >= 0) {
            ioctl(device.fd, EVIOCGRAB, 0);
            close(device.fd);
            device.fd = -1;
        }
    }
}

} // namespace

namespace sdc {

ControllerRuntime::ControllerRuntime() = default;

ControllerRuntime::~ControllerRuntime() {
    std::string ignored;
    stop(ignored);
}

bool ControllerRuntime::start(std::string &message) {
    if (worker_.joinable()) {
        if (status().running) {
            message = "Already running";
            return true;
        }
        worker_.join();
    }
    stop_requested_.store(false);
    set_status(true, "Starting", "Opening input devices and USB gadget endpoints.");
    worker_ = std::thread(&ControllerRuntime::worker_main, this);
    message = "Started";
    return true;
}

bool ControllerRuntime::stop(std::string &message) {
    stop_requested_.store(true);
    if (worker_.joinable()) {
        if (status().running) {
            set_status(true, "Stopping", "Releasing grabbed input devices.");
        }
        worker_.join();
    }
    {
        std::lock_guard<std::mutex> lock(status_mutex_);
        status_.running = false;
        if (status_.state != "Error") {
            status_.state = "Stopped";
            status_.details = "No capture active.";
        }
    }
    message = "Stopped";
    return true;
}

RuntimeStatus ControllerRuntime::status() const {
    std::lock_guard<std::mutex> lock(status_mutex_);
    return status_;
}

void ControllerRuntime::set_status(bool running, std::string state, std::string details) {
    std::lock_guard<std::mutex> lock(status_mutex_);
    status_.running = running;
    status_.state = std::move(state);
    status_.details = std::move(details);
}

void ControllerRuntime::worker_main() {
    std::vector<InputDevice> devices;
    int keyboard_fd = -1;
    int mouse_fd = -1;
    int gamepad_fd = -1;

    try {
        set_status(true, "Preparing USB gadget", "Configuring keyboard, mouse, and Xbox-style HID gamepad endpoints.");
        setup_gadget();

        keyboard_fd = open_hidg(0);
        mouse_fd = open_hidg(1);
        gamepad_fd = open_hidg(2);

        devices = open_input_devices();
        if (devices.empty()) {
            throw std::runtime_error("No keyboard, mouse, or gamepad event devices could be opened.");
        }

        std::ostringstream details;
        details << "Forwarding " << devices.size() << " grabbed input device(s):";
        for (const auto &device : devices) {
            details << "\n" << device.path << " - " << device.name;
        }
        set_status(true, "Running", details.str());

        std::array<uint8_t, 8> keyboard_report{};
        std::array<uint8_t, 4> mouse_report{};
        XboxHidReport gamepad_report;
        int hat_x = 0;
        int hat_y = 0;
        std::set<int> pressed_keys;

        while (!stop_requested_.load()) {
            std::vector<pollfd> pfds;
            pfds.reserve(devices.size());
            for (const auto &device : devices) {
                pfds.push_back({device.fd, POLLIN, 0});
            }
            const int ready = poll(pfds.data(), pfds.size(), 100);
            if (ready <= 0) {
                continue;
            }

            for (size_t i = 0; i < pfds.size(); ++i) {
                if ((pfds[i].revents & POLLIN) == 0) {
                    continue;
                }
                input_event event{};
                while (read(devices[i].fd, &event, sizeof(event)) == sizeof(event)) {
                    const auto &device = devices[i];
                    if (device.kind == DeviceKind::Keyboard && event.type == EV_KEY && event.value != 2) {
                        if (event.value) {
                            pressed_keys.insert(event.code);
                        } else {
                            pressed_keys.erase(event.code);
                        }
                        const bool ctrl_down = pressed_keys.contains(KEY_LEFTCTRL) || pressed_keys.contains(KEY_RIGHTCTRL);
                        const bool shift_down = pressed_keys.contains(KEY_LEFTSHIFT) || pressed_keys.contains(KEY_RIGHTSHIFT);
                        if (ctrl_down && shift_down && event.code == KEY_ESC && event.value) {
                            stop_requested_.store(true);
                            continue;
                        }
                        if (auto bit = modifier_bit(event.code)) {
                            if (event.value) keyboard_report[0] |= static_cast<uint8_t>(1U << *bit);
                            else keyboard_report[0] &= static_cast<uint8_t>(~(1U << *bit));
                        } else if (auto usage = key_to_hid(event.code)) {
                            auto begin = keyboard_report.begin() + 2;
                            auto end = keyboard_report.end();
                            if (event.value) {
                                if (std::find(begin, end, *usage) == end) {
                                    auto slot = std::find(begin, end, 0);
                                    if (slot != end) *slot = *usage;
                                }
                            } else {
                                std::replace(begin, end, *usage, static_cast<uint8_t>(0));
                            }
                        }
                        write_report(keyboard_fd, keyboard_report.data(), keyboard_report.size());
                    } else if (device.kind == DeviceKind::Mouse) {
                        if (event.type == EV_KEY) {
                            uint8_t mask = 0;
                            if (event.code == BTN_LEFT) mask = 1;
                            else if (event.code == BTN_RIGHT) mask = 2;
                            else if (event.code == BTN_MIDDLE) mask = 4;
                            else if (event.code == BTN_SIDE) mask = 8;
                            else if (event.code == BTN_EXTRA) mask = 16;
                            if (mask) {
                                if (event.value) mouse_report[0] |= mask;
                                else mouse_report[0] &= static_cast<uint8_t>(~mask);
                                write_report(mouse_fd, mouse_report.data(), mouse_report.size());
                            }
                        } else if (event.type == EV_REL) {
                            std::array<uint8_t, 4> report{mouse_report[0], 0, 0, 0};
                            if (event.code == REL_X) report[1] = static_cast<uint8_t>(clamp_i8(event.value));
                            else if (event.code == REL_Y) report[2] = static_cast<uint8_t>(clamp_i8(event.value));
                            else if (event.code == REL_WHEEL) report[3] = static_cast<uint8_t>(clamp_i8(event.value));
                            else continue;
                            write_report(mouse_fd, report.data(), report.size());
                        }
                    } else if (device.kind == DeviceKind::Gamepad) {
                        bool changed = false;
                        if (event.type == EV_KEY) {
                            if (auto bit = xbox_button_bit(event.code)) {
                                gamepad_report.set_button(*bit, event.value != 0);
                                changed = true;
                            }
                        } else if (event.type == EV_ABS) {
                            auto info = device.abs_info.find(event.code);
                            if (event.code == ABS_HAT0X) {
                                hat_x = event.value;
                                gamepad_report.set_hat(hat_x, hat_y);
                                changed = true;
                            } else if (event.code == ABS_HAT0Y) {
                                hat_y = event.value;
                                gamepad_report.set_hat(hat_x, hat_y);
                                changed = true;
                            } else if (auto offset = xbox_trigger_offset(event.code); offset && info != device.abs_info.end()) {
                                gamepad_report.set_trigger(*offset, normalize_abs_u8(info->second.minimum, info->second.maximum, event.value));
                                changed = true;
                            } else if (auto offset = xbox_axis_offset(event.code); offset && info != device.abs_info.end()) {
                                gamepad_report.set_axis(*offset, normalize_abs_i16(info->second.minimum, info->second.maximum, event.value));
                                changed = true;
                            }
                        }
                        if (changed) {
                            write_report(gamepad_fd, gamepad_report.bytes.data(), gamepad_report.bytes.size());
                        }
                    }
                }
            }
        }

        std::array<uint8_t, 8> zero8{};
        std::array<uint8_t, 4> zero4{};
        write_report(keyboard_fd, zero8.data(), zero8.size());
        write_report(mouse_fd, zero4.data(), zero4.size());
        const XboxHidReport neutral_gamepad_report;
        write_report(gamepad_fd, neutral_gamepad_report.bytes.data(), neutral_gamepad_report.bytes.size());

        release_devices(devices);
        close(keyboard_fd);
        close(mouse_fd);
        close(gamepad_fd);
        unbind_gadget();
        set_status(false, "Stopped", "Capture released.");
    } catch (const std::exception &ex) {
        release_devices(devices);
        if (keyboard_fd >= 0) close(keyboard_fd);
        if (mouse_fd >= 0) close(mouse_fd);
        if (gamepad_fd >= 0) close(gamepad_fd);
        unbind_gadget();
        set_status(false, "Error", ex.what());
    }
}

} // namespace sdc
