#include <gtk/gtk.h>

#ifdef __linux__
#include <linux/input.h>
#include <sys/ioctl.h>
#include <sys/poll.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cerrno>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <set>
#include <sstream>
#include <string>
#include <thread>
#include <vector>
#endif

namespace {

GtkWidget *g_status_label = nullptr;
GtkWidget *g_detail_label = nullptr;
GtkWidget *g_start_button = nullptr;
GtkWidget *g_stop_button = nullptr;

#ifdef __linux__

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

struct StatusSnapshot {
    bool running = false;
    std::string status = "Stopped";
    std::string details = "No capture active.";
};

std::mutex g_status_mutex;
StatusSnapshot g_status;
std::atomic_bool g_stop_requested{false};
std::thread g_worker;

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

void set_status(bool running, std::string status, std::string details) {
    {
        std::lock_guard<std::mutex> lock(g_status_mutex);
        g_status.running = running;
        g_status.status = std::move(status);
        g_status.details = std::move(details);
    }
    g_idle_add([](gpointer) -> gboolean {
        StatusSnapshot snapshot;
        {
            std::lock_guard<std::mutex> lock(g_status_mutex);
            snapshot = g_status;
        }
        gtk_label_set_text(GTK_LABEL(g_status_label), snapshot.status.c_str());
        gtk_label_set_text(GTK_LABEL(g_detail_label), snapshot.details.c_str());
        gtk_widget_set_sensitive(g_start_button, !snapshot.running);
        gtk_widget_set_sensitive(g_stop_button, snapshot.running);
        return G_SOURCE_REMOVE;
    }, nullptr);
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

    std::filesystem::create_directories(kGadgetPath);
    const std::filesystem::path gadget(kGadgetPath);

    write_text(gadget / "idVendor", "0x1d6b");
    write_text(gadget / "idProduct", "0x0104");
    write_text(gadget / "bcdDevice", "0x0100");
    write_text(gadget / "bcdUSB", "0x0200");

    std::filesystem::create_directories(gadget / "strings/0x409");
    write_text(gadget / "strings/0x409/serialnumber", "sdc-0001");
    write_text(gadget / "strings/0x409/manufacturer", "SteamDeckController");
    write_text(gadget / "strings/0x409/product", "Input Passthrough");

    std::filesystem::create_directories(gadget / "configs/c.1/strings/0x409");
    write_text(gadget / "configs/c.1/MaxPower", "250");
    write_text(gadget / "configs/c.1/strings/0x409/configuration", "HID keyboard mouse gamepad");

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
        0x05, 0x01, 0x09, 0x05, 0xa1, 0x01, 0x05, 0x09, 0x19, 0x01, 0x29, 0x10,
        0x15, 0x00, 0x25, 0x01, 0x75, 0x01, 0x95, 0x10, 0x81, 0x02, 0x05, 0x01,
        0x09, 0x30, 0x09, 0x31, 0x09, 0x33, 0x09, 0x34, 0x09, 0x32, 0x09, 0x35,
        0x15, 0x81, 0x25, 0x7f, 0x75, 0x08, 0x95, 0x06, 0x81, 0x02, 0xc0
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
        HidFunction{"hid.usb2", 0, 0, 8, gamepad_desc},
    };

    for (const auto &fn : functions) {
        const auto dir = gadget / "functions" / fn.name;
        std::filesystem::create_directories(dir);
        write_text(dir / "protocol", std::to_string(fn.protocol));
        write_text(dir / "subclass", std::to_string(fn.subclass));
        write_text(dir / "report_length", std::to_string(fn.report_len));
        write_binary(dir / "report_desc", fn.desc);
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

std::optional<uint8_t> key_to_hid(int code) {
    if (code >= KEY_A && code <= KEY_Z) {
        return static_cast<uint8_t>(0x04 + code - KEY_A);
    }
    if (code >= KEY_1 && code <= KEY_9) {
        return static_cast<uint8_t>(0x1e + code - KEY_1);
    }
    if (code == KEY_0) return 0x27;
    switch (code) {
        case KEY_ENTER: return 0x28;
        case KEY_ESC: return 0x29;
        case KEY_BACKSPACE: return 0x2a;
        case KEY_TAB: return 0x2b;
        case KEY_SPACE: return 0x2c;
        case KEY_MINUS: return 0x2d;
        case KEY_EQUAL: return 0x2e;
        case KEY_LEFTBRACE: return 0x2f;
        case KEY_RIGHTBRACE: return 0x30;
        case KEY_BACKSLASH: return 0x31;
        case KEY_SEMICOLON: return 0x33;
        case KEY_APOSTROPHE: return 0x34;
        case KEY_GRAVE: return 0x35;
        case KEY_COMMA: return 0x36;
        case KEY_DOT: return 0x37;
        case KEY_SLASH: return 0x38;
        case KEY_CAPSLOCK: return 0x39;
        case KEY_F1: return 0x3a;
        case KEY_F2: return 0x3b;
        case KEY_F3: return 0x3c;
        case KEY_F4: return 0x3d;
        case KEY_F5: return 0x3e;
        case KEY_F6: return 0x3f;
        case KEY_F7: return 0x40;
        case KEY_F8: return 0x41;
        case KEY_F9: return 0x42;
        case KEY_F10: return 0x43;
        case KEY_F11: return 0x44;
        case KEY_F12: return 0x45;
        case KEY_PRINT: return 0x46;
        case KEY_SCROLLLOCK: return 0x47;
        case KEY_PAUSE: return 0x48;
        case KEY_INSERT: return 0x49;
        case KEY_HOME: return 0x4a;
        case KEY_PAGEUP: return 0x4b;
        case KEY_DELETE: return 0x4c;
        case KEY_END: return 0x4d;
        case KEY_PAGEDOWN: return 0x4e;
        case KEY_RIGHT: return 0x4f;
        case KEY_LEFT: return 0x50;
        case KEY_DOWN: return 0x51;
        case KEY_UP: return 0x52;
        default: return std::nullopt;
    }
}

std::optional<int> modifier_bit(int code) {
    switch (code) {
        case KEY_LEFTCTRL: return 0;
        case KEY_LEFTSHIFT: return 1;
        case KEY_LEFTALT: return 2;
        case KEY_LEFTMETA: return 3;
        case KEY_RIGHTCTRL: return 4;
        case KEY_RIGHTSHIFT: return 5;
        case KEY_RIGHTALT: return 6;
        case KEY_RIGHTMETA: return 7;
        default: return std::nullopt;
    }
}

void write_report(int fd, const uint8_t *data, size_t len) {
    const ssize_t written = write(fd, data, len);
    (void)written;
}

int clamp_i8(int value) {
    return std::max(-127, std::min(127, value));
}

int normalize_abs(const input_absinfo &info, int value) {
    if (info.maximum <= info.minimum) {
        return 0;
    }
    const double normalized = (static_cast<double>(value - info.minimum) / (info.maximum - info.minimum)) * 254.0 - 127.0;
    return clamp_i8(static_cast<int>(normalized));
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
            for (int code : {ABS_X, ABS_Y, ABS_RX, ABS_RY, ABS_Z, ABS_RZ}) {
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

std::optional<int> gamepad_button_bit(int code) {
    switch (code) {
        case BTN_SOUTH: return 0;
        case BTN_EAST: return 1;
        case BTN_NORTH: return 2;
        case BTN_WEST: return 3;
        case BTN_TL: return 4;
        case BTN_TR: return 5;
        case BTN_SELECT: return 6;
        case BTN_START: return 7;
        case BTN_THUMBL: return 8;
        case BTN_THUMBR: return 9;
        case BTN_MODE: return 10;
        default: return std::nullopt;
    }
}

int gamepad_axis_index(int code) {
    switch (code) {
        case ABS_X: return 2;
        case ABS_Y: return 3;
        case ABS_RX: return 4;
        case ABS_RY: return 5;
        case ABS_Z: return 6;
        case ABS_RZ: return 7;
        default: return -1;
    }
}

void worker_main() {
    try {
        set_status(true, "Preparing USB gadget", "Configuring keyboard, mouse, and generic HID gamepad endpoints.");
        setup_gadget();

        int keyboard_fd = open_hidg(0);
        int mouse_fd = open_hidg(1);
        int gamepad_fd = open_hidg(2);

        auto devices = open_input_devices();
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
        std::array<uint8_t, 8> gamepad_report{};
        std::set<int> pressed_keys;

        while (!g_stop_requested.load()) {
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
                            g_stop_requested.store(true);
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
                            if (auto bit = gamepad_button_bit(event.code)) {
                                const uint16_t mask = static_cast<uint16_t>(1U << *bit);
                                uint16_t buttons = static_cast<uint16_t>(gamepad_report[0] | (gamepad_report[1] << 8));
                                if (event.value) buttons |= mask;
                                else buttons &= static_cast<uint16_t>(~mask);
                                gamepad_report[0] = static_cast<uint8_t>(buttons & 0xff);
                                gamepad_report[1] = static_cast<uint8_t>((buttons >> 8) & 0xff);
                                changed = true;
                            }
                        } else if (event.type == EV_ABS) {
                            const int axis = gamepad_axis_index(event.code);
                            auto info = device.abs_info.find(event.code);
                            if (axis >= 0 && info != device.abs_info.end()) {
                                gamepad_report[axis] = static_cast<uint8_t>(normalize_abs(info->second, event.value));
                                changed = true;
                            }
                        }
                        if (changed) {
                            write_report(gamepad_fd, gamepad_report.data(), gamepad_report.size());
                        }
                    }
                }
            }
        }

        std::array<uint8_t, 8> zero8{};
        std::array<uint8_t, 4> zero4{};
        write_report(keyboard_fd, zero8.data(), zero8.size());
        write_report(mouse_fd, zero4.data(), zero4.size());
        write_report(gamepad_fd, zero8.data(), zero8.size());

        release_devices(devices);
        close(keyboard_fd);
        close(mouse_fd);
        close(gamepad_fd);
        unbind_gadget();
        set_status(false, "Stopped", "Capture released.");
    } catch (const std::exception &ex) {
        unbind_gadget();
        set_status(false, "Error", ex.what());
    }
}

void start_capture() {
    if (g_worker.joinable()) {
        g_worker.join();
    }
    g_stop_requested.store(false);
    set_status(true, "Starting", "Opening input devices and USB gadget endpoints.");
    g_worker = std::thread(worker_main);
}

void stop_capture() {
    g_stop_requested.store(true);
    set_status(true, "Stopping", "Releasing grabbed input devices.");
}

#else

void start_capture() {
    gtk_label_set_text(GTK_LABEL(g_status_label), "Unsupported platform");
    gtk_label_set_text(GTK_LABEL(g_detail_label), "This program needs Linux evdev and USB gadget support.");
}

void stop_capture() {}

#endif

void on_start_clicked(GtkButton *, gpointer) {
    start_capture();
}

void on_stop_clicked(GtkButton *, gpointer) {
    stop_capture();
}

void on_destroy(GtkWidget *, gpointer) {
#ifdef __linux__
    g_stop_requested.store(true);
    if (g_worker.joinable()) {
        g_worker.join();
    }
#endif
    gtk_main_quit();
}

} // namespace

int main(int argc, char **argv) {
    gtk_init(&argc, &argv);

    GtkWidget *window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(window), "Steam Deck Controller Passthrough");
    gtk_window_set_default_size(GTK_WINDOW(window), 520, 240);
    gtk_container_set_border_width(GTK_CONTAINER(window), 18);
    g_signal_connect(window, "destroy", G_CALLBACK(on_destroy), nullptr);

    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
    gtk_container_add(GTK_CONTAINER(window), box);

    GtkWidget *title = gtk_label_new("USB input passthrough");
    gtk_widget_set_halign(title, GTK_ALIGN_START);
    PangoAttrList *attrs = pango_attr_list_new();
    pango_attr_list_insert(attrs, pango_attr_weight_new(PANGO_WEIGHT_BOLD));
    pango_attr_list_insert(attrs, pango_attr_scale_new(1.4));
    gtk_label_set_attributes(GTK_LABEL(title), attrs);
    pango_attr_list_unref(attrs);
    gtk_box_pack_start(GTK_BOX(box), title, FALSE, FALSE, 0);

    g_status_label = gtk_label_new("Stopped");
    gtk_widget_set_halign(g_status_label, GTK_ALIGN_START);
    gtk_box_pack_start(GTK_BOX(box), g_status_label, FALSE, FALSE, 0);

    g_detail_label = gtk_label_new("No capture active.");
    gtk_widget_set_halign(g_detail_label, GTK_ALIGN_START);
    gtk_label_set_xalign(GTK_LABEL(g_detail_label), 0.0f);
    gtk_label_set_line_wrap(GTK_LABEL(g_detail_label), TRUE);
    gtk_box_pack_start(GTK_BOX(box), g_detail_label, TRUE, TRUE, 0);

    GtkWidget *buttons = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_widget_set_halign(buttons, GTK_ALIGN_END);
    gtk_box_pack_end(GTK_BOX(box), buttons, FALSE, FALSE, 0);

    g_start_button = gtk_button_new_with_label("Start");
    g_stop_button = gtk_button_new_with_label("Stop");
    gtk_widget_set_sensitive(g_stop_button, FALSE);
    g_signal_connect(g_start_button, "clicked", G_CALLBACK(on_start_clicked), nullptr);
    g_signal_connect(g_stop_button, "clicked", G_CALLBACK(on_stop_clicked), nullptr);
    gtk_box_pack_start(GTK_BOX(buttons), g_start_button, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(buttons), g_stop_button, FALSE, FALSE, 0);

    gtk_widget_show_all(window);
    gtk_main();
    return 0;
}
