#include "controller_runtime.hpp"

#include "input_translation.hpp"

#include <linux/input.h>
#include <sys/ioctl.h>
#include <sys/mount.h>
#include <sys/poll.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <cerrno>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <optional>
#include <set>
#include <sstream>
#include <stdexcept>
#include <vector>

namespace {

// ConfigFS gadget root
constexpr const char *kGadgetPath = "/sys/kernel/config/usb_gadget/sdc_passthrough";

// FunctionFS mount point and function name
constexpr const char *kFfsMountDir  = "/run/steamdeckcontroller/ffs-xbox360";
constexpr const char *kFfsFuncName  = "xbox360";

// Xbox 360 wired controller identifiers
constexpr const char *kVendorId  = "0x045e";
constexpr const char *kProductId = "0x028e";
constexpr const char *kBcdDevice = "0x0114";
constexpr const char *kBcdUsb    = "0x0200";

// FunctionFS event types
constexpr uint8_t kFfsEventBind    = 0;
constexpr uint8_t kFfsEventUnbind  = 1;
constexpr uint8_t kFfsEventEnable  = 2;
constexpr uint8_t kFfsEventDisable = 3;
constexpr uint8_t kFfsEventSetup   = 4;

struct FfsEvent {
    union {
        struct {
            uint8_t  bRequestType;
            uint8_t  bRequest;
            uint16_t wValue;
            uint16_t wIndex;
            uint16_t wLength;
        } ctrl;
    } u;
    uint8_t type;
    uint8_t _pad[3];
};

// ---------------------------------------------------------------------------
// Device classification

enum class DeviceKind { Keyboard, Mouse, Gamepad };

struct InputDevice {
    int fd = -1;
    std::string path;
    std::string name;
    DeviceKind kind;
    std::map<int, input_absinfo> abs_info;
};

bool test_bit(const std::vector<unsigned long> &bits, int bit) {
    constexpr int bpl = static_cast<int>(sizeof(unsigned long) * 8);
    const auto idx = static_cast<size_t>(bit / bpl);
    if (idx >= bits.size()) return false;
    return (bits[idx] & (1UL << (bit % bpl))) != 0;
}

std::vector<unsigned long> ioctl_bits(int fd, unsigned long req, size_t max_bit) {
    const size_t words = (max_bit + sizeof(unsigned long)*8) / (sizeof(unsigned long)*8);
    std::vector<unsigned long> bits(words, 0);
    ioctl(fd, req, bits.data());
    return bits;
}

std::optional<DeviceKind> classify_device(int fd) {
    const auto ev  = ioctl_bits(fd, EVIOCGBIT(0, EV_MAX), EV_MAX);
    const auto key = ioctl_bits(fd, EVIOCGBIT(EV_KEY, KEY_MAX), KEY_MAX);
    const auto rel = ioctl_bits(fd, EVIOCGBIT(EV_REL, REL_MAX), REL_MAX);
    const auto abs = ioctl_bits(fd, EVIOCGBIT(EV_ABS, ABS_MAX), ABS_MAX);

    // Touch devices are deliberately NOT classified (and thus never grabbed):
    // the touchscreen stays local so it can drive the frontend's STOP button
    // while keyboard/mouse/gamepad are grabbed and forwarded to the USB host.
    // (Touchscreens also expose ABS_X/ABS_Y, so the old ABS_X-only gamepad
    // check wrongly grabbed them; require real gamepad buttons instead.)
    const bool is_touch = test_bit(key, BTN_TOUCH) || test_bit(abs, ABS_MT_SLOT);

    if (!is_touch && test_bit(ev, EV_KEY) &&
        (test_bit(key, BTN_GAMEPAD) || test_bit(key, BTN_SOUTH)))
        return DeviceKind::Gamepad;
    if (!is_touch && test_bit(ev, EV_REL) && test_bit(rel, REL_X) && test_bit(rel, REL_Y))
        return DeviceKind::Mouse;
    if (test_bit(ev, EV_KEY) && test_bit(key, KEY_A) && test_bit(key, KEY_ENTER))
        return DeviceKind::Keyboard;
    return std::nullopt;
}

// ---------------------------------------------------------------------------
// Filesystem / gadget helpers

std::string read_first_udc() {
    const std::filesystem::path udc_dir("/sys/class/udc");
    if (!std::filesystem::exists(udc_dir)) return {};
    for (const auto &e : std::filesystem::directory_iterator(udc_dir))
        return e.path().filename().string();
    return {};
}

std::string describe_path_state(const std::filesystem::path &path) {
    std::ostringstream out;
    std::error_code ec;
    out << path << ": ";
    if (!std::filesystem::exists(path, ec)) {
        out << "missing";
        if (ec) out << " (" << ec.message() << ")";
        return out.str();
    }
    const auto st = std::filesystem::status(path, ec);
    if (ec) { out << "status error (" << ec.message() << ")"; return out.str(); }
    out << (std::filesystem::is_directory(st) ? "directory" : "not directory");
    out << ", permissions " << std::oct
        << static_cast<unsigned>(st.permissions() & std::filesystem::perms::all);
    return out.str();
}

void ensure_directory(const std::filesystem::path &path) {
    std::error_code ec;
    std::filesystem::create_directories(path, ec);
    if (ec) {
        std::ostringstream msg;
        msg << "Cannot create " << path << ": " << ec.message()
            << "\n" << describe_path_state(path.parent_path())
            << "\nCheck that ConfigFS is mounted and the daemon runs as uid 0.";
        throw std::runtime_error(msg.str());
    }
}

void write_text(const std::filesystem::path &path, const std::string &value) {
    std::ofstream out(path);
    if (!out) throw std::runtime_error("Cannot write " + path.string() + ": " + std::strerror(errno));
    out << value;
}

void write_binary(const std::filesystem::path &path, const std::vector<uint8_t> &value) {
    std::ofstream out(path, std::ios::binary);
    if (!out) throw std::runtime_error("Cannot write " + path.string() + ": " + std::strerror(errno));
    out.write(reinterpret_cast<const char *>(value.data()),
              static_cast<std::streamsize>(value.size()));
}

void write_text_if_exists(const std::filesystem::path &path, const std::string &value) {
    if (std::filesystem::exists(path)) write_text(path, value);
}

void ensure_symlink(const std::filesystem::path &target, const std::filesystem::path &link) {
    std::error_code ec;
    if (std::filesystem::exists(link, ec)) return;
    if (ec) throw std::runtime_error("Cannot check " + link.string() + ": " + ec.message());
    std::filesystem::create_symlink(target, link, ec);
    if (ec) throw std::runtime_error("Cannot link " + link.string() + ": " + ec.message());
}

// ---------------------------------------------------------------------------
// FunctionFS binary descriptor block builder

static void append_le16(std::vector<uint8_t> &v, uint16_t x) {
    v.push_back(x & 0xff);
    v.push_back((x >> 8) & 0xff);
}
static void append_le32(std::vector<uint8_t> &v, uint32_t x) {
    v.push_back(x & 0xff);
    v.push_back((x >> 8) & 0xff);
    v.push_back((x >> 16) & 0xff);
    v.push_back((x >> 24) & 0xff);
}

// Interface + 2 endpoints for one speed tier
static std::vector<uint8_t> xbox360_interface_block(uint16_t ep_max_pkt) {
    std::vector<uint8_t> b;
    // Interface descriptor
    b.push_back(9);     // bLength
    b.push_back(0x04);  // bDescriptorType: INTERFACE
    b.push_back(0);     // bInterfaceNumber
    b.push_back(0);     // bAlternateSetting
    b.push_back(2);     // bNumEndpoints
    b.push_back(0xFF);  // bInterfaceClass: vendor
    b.push_back(0x5D);  // bInterfaceSubClass: Xbox
    b.push_back(0x01);  // bInterfaceProtocol: Xbox 360 gamepad
    b.push_back(0);     // iInterface
    // EP1 IN — interrupt, gamepad reports
    b.push_back(7);     // bLength
    b.push_back(0x05);  // bDescriptorType: ENDPOINT
    b.push_back(0x81);  // bEndpointAddress: IN ep1
    b.push_back(0x03);  // bmAttributes: interrupt
    append_le16(b, ep_max_pkt);
    b.push_back(4);     // bInterval (4ms)
    // EP2 OUT — interrupt, rumble
    b.push_back(7);
    b.push_back(0x05);
    b.push_back(0x01);  // OUT ep1
    b.push_back(0x03);
    append_le16(b, ep_max_pkt);
    b.push_back(8);     // bInterval (8ms)
    return b;
}

std::vector<uint8_t> build_ffs_descriptors() {
    const auto fs_block = xbox360_interface_block(20);  // full-speed
    const auto hs_block = xbox360_interface_block(32);  // high-speed

    std::vector<uint8_t> result;
    append_le32(result, 3);                                   // FUNCTIONFS_DESCRIPTORS_MAGIC_V2
    append_le32(result, 5*4 + fs_block.size() + hs_block.size()); // total length
    append_le32(result, 1 | 2);                               // HAS_FS_DESC | HAS_HS_DESC
    append_le32(result, 3);                                   // fs_count (iface + 2 eps)
    append_le32(result, 3);                                   // hs_count
    result.insert(result.end(), fs_block.begin(), fs_block.end());
    result.insert(result.end(), hs_block.begin(), hs_block.end());
    return result;
}

std::vector<uint8_t> build_ffs_strings() {
    std::vector<uint8_t> result;
    append_le32(result, 2);   // FUNCTIONFS_STRINGS_MAGIC
    append_le32(result, 16);  // length (header only, no strings)
    append_le32(result, 0);   // str_count
    append_le32(result, 0);   // lang_count
    return result;
}

// ---------------------------------------------------------------------------
// Gadget setup / teardown

// Sets up the composite gadget and returns the open FunctionFS ep0 fd via
// ep0_fd_out. ep0 must remain open for the lifetime of the session: closing
// the last ep0 handle resets the FunctionFS function and discards the
// descriptors written to it. The caller owns the fd and must close it on stop.
void setup_gadget(int &ep0_fd_out) {
    const std::string udc = read_first_udc();
    if (udc.empty())
        throw std::runtime_error(
            "The Steam Deck must be connected directly to a PC or console before "
            "pressing Start.\n"
            "Connect the USB-C port straight to the host (not through a dock), then "
            "press Start again.");

    const std::filesystem::path gadget(kGadgetPath);
    ensure_directory(gadget);

    // Xbox 360 identifiers
    write_text(gadget / "idVendor",  kVendorId);
    write_text(gadget / "idProduct", kProductId);
    write_text(gadget / "bcdDevice", kBcdDevice);
    write_text(gadget / "bcdUSB",    kBcdUsb);
    write_text(gadget / "bDeviceClass",    "0xff");
    write_text(gadget / "bDeviceSubClass", "0xff");
    write_text(gadget / "bDeviceProtocol", "0xff");

    ensure_directory(gadget / "strings/0x409");
    write_text(gadget / "strings/0x409/serialnumber",  "sdc-0001");
    write_text(gadget / "strings/0x409/manufacturer",  "SteamDeckController");
    write_text(gadget / "strings/0x409/product",       "Steam Deck Xbox 360 Controller");

    ensure_directory(gadget / "configs/c.1/strings/0x409");
    write_text(gadget / "configs/c.1/MaxPower",                    "250");
    write_text(gadget / "configs/c.1/strings/0x409/configuration", "Xbox 360 HID composite");

    // --- HID keyboard ---
    const std::vector<uint8_t> keyboard_desc = {
        0x05,0x01,0x09,0x06,0xa1,0x01,0x05,0x07,0x19,0xe0,0x29,0xe7,
        0x15,0x00,0x25,0x01,0x75,0x01,0x95,0x08,0x81,0x02,0x95,0x01,
        0x75,0x08,0x81,0x01,0x95,0x06,0x75,0x08,0x15,0x00,0x25,0x65,
        0x05,0x07,0x19,0x00,0x29,0x65,0x81,0x00,0xc0
    };
    ensure_directory(gadget / "functions/hid.keyboard");
    write_text(gadget / "functions/hid.keyboard/protocol",      "1");
    write_text(gadget / "functions/hid.keyboard/subclass",      "1");
    write_text(gadget / "functions/hid.keyboard/report_length", "8");
    write_binary(gadget / "functions/hid.keyboard/report_desc", keyboard_desc);
    write_text_if_exists(gadget / "functions/hid.keyboard/interval", "1");
    ensure_symlink(gadget / "functions/hid.keyboard",
                   gadget / "configs/c.1/hid.keyboard");

    // --- HID mouse ---
    const std::vector<uint8_t> mouse_desc = {
        0x05,0x01,0x09,0x02,0xa1,0x01,0x09,0x01,0xa1,0x00,0x05,0x09,
        0x19,0x01,0x29,0x05,0x15,0x00,0x25,0x01,0x95,0x05,0x75,0x01,
        0x81,0x02,0x95,0x01,0x75,0x03,0x81,0x01,0x05,0x01,0x09,0x30,
        0x09,0x31,0x09,0x38,0x15,0x81,0x25,0x7f,0x75,0x08,0x95,0x03,
        0x81,0x06,0xc0,0xc0
    };
    ensure_directory(gadget / "functions/hid.mouse");
    write_text(gadget / "functions/hid.mouse/protocol",      "2");
    write_text(gadget / "functions/hid.mouse/subclass",      "1");
    write_text(gadget / "functions/hid.mouse/report_length", "4");
    write_binary(gadget / "functions/hid.mouse/report_desc", mouse_desc);
    write_text_if_exists(gadget / "functions/hid.mouse/interval", "1");
    ensure_symlink(gadget / "functions/hid.mouse",
                   gadget / "configs/c.1/hid.mouse");

    // --- FunctionFS Xbox 360 gamepad ---
    ensure_directory(gadget / "functions/ffs.xbox360");
    ensure_directory(kFfsMountDir);
    // Best-effort cleanup of a stale mount left behind by an unclean shutdown,
    // otherwise mount() below would fail with EBUSY and the daemon could never
    // recover after a crash.
    umount(kFfsMountDir);
    if (mount(kFfsFuncName, kFfsMountDir, "functionfs", 0, nullptr) != 0)
        throw std::runtime_error(std::string("FunctionFS mount failed: ") + std::strerror(errno));

    // Open ep0 and write descriptors + strings. ep0 stays open from here on:
    // the kernel discards the descriptors if the last ep0 handle is closed.
    {
        const auto descs = build_ffs_descriptors();
        const auto strs  = build_ffs_strings();
        const std::string ep0 = std::string(kFfsMountDir) + "/ep0";
        int fd = open(ep0.c_str(), O_RDWR);
        if (fd < 0)
            throw std::runtime_error("Cannot open FunctionFS ep0: " + std::string(std::strerror(errno)));
        // Hand ownership to the caller immediately so a throw below still
        // closes the fd via the caller's cleanup path.
        ep0_fd_out = fd;
        if (write(fd, descs.data(), descs.size()) != static_cast<ssize_t>(descs.size()) ||
            write(fd, strs.data(),  strs.size())  != static_cast<ssize_t>(strs.size())) {
            throw std::runtime_error("Cannot write FunctionFS descriptors: " + std::string(std::strerror(errno)));
        }
    }

    ensure_symlink(gadget / "functions/ffs.xbox360",
                   gadget / "configs/c.1/ffs.xbox360");

    // Bind to UDC
    write_text(gadget / "UDC", udc);
}

void unbind_gadget() {
    const std::filesystem::path gadget(kGadgetPath);
    if (!std::filesystem::exists(gadget / "UDC")) return;

    // Unbind
    std::ofstream udc(gadget / "UDC");
    if (udc) udc << "";

    // Remove symlinks
    for (const auto &link : { "hid.keyboard", "hid.mouse", "ffs.xbox360" }) {
        std::error_code ec;
        std::filesystem::remove(gadget / "configs/c.1" / link, ec);
    }

    // Unmount FunctionFS
    umount(kFfsMountDir);
    std::error_code ec;
    std::filesystem::remove(kFfsMountDir, ec);
}

// ---------------------------------------------------------------------------
// Input device helpers

std::vector<InputDevice> open_input_devices() {
    std::vector<InputDevice> devices;
    for (const auto &entry : std::filesystem::directory_iterator("/dev/input")) {
        if (entry.path().filename().string().rfind("event", 0) != 0) continue;
        int fd = open(entry.path().string().c_str(), O_RDONLY | O_NONBLOCK);
        if (fd < 0) continue;
        auto kind = classify_device(fd);
        if (!kind) { close(fd); continue; }

        std::array<char, 256> name{};
        ioctl(fd, EVIOCGNAME(name.size()), name.data());
        InputDevice dev{fd, entry.path().string(), name.data(), *kind, {}};
        if (*kind == DeviceKind::Gamepad) {
            for (int code : {ABS_X, ABS_Y, ABS_RX, ABS_RY, ABS_Z, ABS_RZ, ABS_HAT0X, ABS_HAT0Y}) {
                input_absinfo info{};
                if (ioctl(fd, EVIOCGABS(code), &info) == 0)
                    dev.abs_info[code] = info;
            }
        }
        const char *kind_name = (*kind == DeviceKind::Keyboard) ? "keyboard"
                              : (*kind == DeviceKind::Mouse)    ? "mouse" : "gamepad";
        // EVIOCGRAB can fail (e.g. another process — Steam in Gaming Mode — already
        // holds the device). We MUST know: a failed grab means events still go to
        // that other process and never reach us, so nothing is forwarded.
        if (ioctl(fd, EVIOCGRAB, 1) != 0) {
            std::cerr << "steamdeckcontrollerd: WARNING EVIOCGRAB FAILED on "
                      << dev.path << " [" << kind_name << "] '" << dev.name
                      << "': " << std::strerror(errno)
                      << " — another process holds this device; its events will NOT be forwarded.\n";
        } else {
            std::cerr << "steamdeckcontrollerd: grabbed " << dev.path
                      << " [" << kind_name << "] '" << dev.name << "'\n";
        }
        devices.push_back(std::move(dev));
    }
    return devices;
}

void release_devices(std::vector<InputDevice> &devices) {
    for (auto &dev : devices) {
        if (dev.fd >= 0) { ioctl(dev.fd, EVIOCGRAB, 0); close(dev.fd); dev.fd = -1; }
    }
}

void write_report(int fd, const uint8_t *data, size_t len, const char *what) {
    if (fd < 0) {
        std::cerr << "steamdeckcontrollerd: DROP " << what
                  << " report — endpoint not open (fd<0)\n";
        return;
    }
    const ssize_t written = write(fd, data, len);
    if (written < 0) {
        std::cerr << "steamdeckcontrollerd: WRITE FAILED " << what
                  << " report: " << std::strerror(errno) << '\n';
    } else if (static_cast<size_t>(written) != len) {
        std::cerr << "steamdeckcontrollerd: SHORT WRITE " << what
                  << " report: " << written << "/" << len << " bytes\n";
    }
}

} // namespace

// ---------------------------------------------------------------------------
// ControllerRuntime

namespace sdc {

ControllerRuntime::ControllerRuntime() = default;

ControllerRuntime::~ControllerRuntime() {
    std::string ignored;
    stop(ignored);
}

bool ControllerRuntime::start(std::string &message) {
    if (worker_.joinable()) {
        if (status().running) { message = "Already running"; return true; }
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
        if (status().running) set_status(true, "Stopping", "Releasing grabbed input devices.");
        worker_.join();
    }
    {
        std::lock_guard<std::mutex> lock(status_mutex_);
        status_.running = false;
        if (status_.state != "Error") {
            status_.state   = "Stopped";
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
    status_.state   = std::move(state);
    status_.details = std::move(details);
}

void ControllerRuntime::worker_main() {
    std::vector<InputDevice> devices;
    int keyboard_fd  = -1;
    int mouse_fd     = -1;
    int ep0_fd       = -1;
    int ep1_fd       = -1; // opened on FUNCTIONFS_ENABLE

    auto close_ep = [&] {
        if (ep1_fd >= 0) { close(ep1_fd); ep1_fd = -1; }
        if (ep0_fd >= 0) { close(ep0_fd); ep0_fd = -1; }
    };

    try {
        set_status(true, "Preparing USB gadget", "Configuring Xbox 360 endpoints via FunctionFS.");
        // setup_gadget opens ep0, writes descriptors, and keeps ep0 open
        // (closing it would discard the descriptors). We own ep0_fd from here.
        setup_gadget(ep0_fd);

        // hidg0 = keyboard, hidg1 = mouse (matches ConfigFS link order)
        keyboard_fd = open("/dev/hidg0", O_WRONLY | O_NONBLOCK);
        if (keyboard_fd < 0) throw std::runtime_error("Cannot open /dev/hidg0 (keyboard): " + std::string(std::strerror(errno)));
        mouse_fd = open("/dev/hidg1", O_WRONLY | O_NONBLOCK);
        if (mouse_fd < 0) throw std::runtime_error("Cannot open /dev/hidg1 (mouse): " + std::string(std::strerror(errno)));

        devices = open_input_devices();
        if (devices.empty()) throw std::runtime_error("No keyboard, mouse, or gamepad event devices found.");

        std::ostringstream details;
        details << "Forwarding " << devices.size() << " grabbed input device(s):";
        for (const auto &d : devices) details << "\n" << d.path << " - " << d.name;
        set_status(true, "Running", details.str());

        std::array<uint8_t, 8> keyboard_report{};
        std::array<uint8_t, 4> mouse_report{};
        Xbox360Report gamepad_report;
        int hat_x = 0, hat_y = 0;
        std::set<int> pressed_keys;
        std::set<std::string> seen_event_devices; // DEBUG: log first event per device

        while (!stop_requested_.load()) {
            // Build poll set: ep0 + all input devices
            std::vector<pollfd> pfds;
            pfds.push_back({ep0_fd, POLLIN, 0});
            for (const auto &d : devices) pfds.push_back({d.fd, POLLIN, 0});

            if (poll(pfds.data(), pfds.size(), 100) <= 0) continue;

            // --- ep0: FunctionFS events ---
            if (pfds[0].revents & POLLIN) {
                FfsEvent ev{};
                if (read(ep0_fd, &ev, sizeof(ev)) == sizeof(ev)) {
                    switch (ev.type) {
                    case kFfsEventEnable:
                        ep1_fd = open((std::string(kFfsMountDir) + "/ep1").c_str(), O_WRONLY | O_NONBLOCK);
                        std::cerr << "steamdeckcontrollerd: FFS ENABLE — ep1 "
                                  << (ep1_fd >= 0 ? "opened (gamepad reports active)"
                                                  : (std::string("open FAILED: ") + std::strerror(errno)))
                                  << '\n';
                        break;
                    case kFfsEventDisable:
                        std::cerr << "steamdeckcontrollerd: FFS DISABLE — closing ep1\n";
                        if (ep1_fd >= 0) { close(ep1_fd); ep1_fd = -1; }
                        break;
                    case kFfsEventBind:
                        std::cerr << "steamdeckcontrollerd: FFS BIND\n";
                        break;
                    case kFfsEventSetup: {
                        // ACK vendor control requests from Windows XInput driver
                        const uint8_t dir = ev.u.ctrl.bRequestType & 0x80;
                        if (dir) {
                            // IN: device→host — send zeros of requested length
                            const uint16_t len = ev.u.ctrl.wLength;
                            std::vector<uint8_t> resp(len, 0);
                            write(ep0_fd, resp.data(), len);
                        } else {
                            // OUT: host→device — read any data then ACK with ZLP
                            if (ev.u.ctrl.wLength > 0) {
                                std::vector<uint8_t> buf(ev.u.ctrl.wLength);
                                read(ep0_fd, buf.data(), buf.size());
                            }
                            static const uint8_t zlp = 0;
                            write(ep0_fd, &zlp, 0);
                        }
                        break;
                    }
                    default: break;
                    }
                }
            }

            // --- input devices ---
            for (size_t i = 0; i < devices.size(); ++i) {
                if ((pfds[i + 1].revents & POLLIN) == 0) continue;
                input_event event{};
                while (read(devices[i].fd, &event, sizeof(event)) == sizeof(event)) {
                    const auto &dev = devices[i];

                    // DEBUG: log the first incoming event per device so we can see
                    // whether the grabbed devices actually deliver input to us.
                    if (event.type != 0 /* skip EV_SYN spam */ &&
                        seen_event_devices.insert(dev.path).second) {
                        std::cerr << "steamdeckcontrollerd: first input event from "
                                  << dev.path << " '" << dev.name << "' (type=" << event.type
                                  << " code=" << event.code << " val=" << event.value << ")\n";
                    }

                    // Keyboard
                    if (dev.kind == DeviceKind::Keyboard && event.type == EV_KEY && event.value != 2) {
                        if (event.value) pressed_keys.insert(event.code);
                        else             pressed_keys.erase(event.code);

                        const bool ctrl  = pressed_keys.contains(KEY_LEFTCTRL)  || pressed_keys.contains(KEY_RIGHTCTRL);
                        const bool shift = pressed_keys.contains(KEY_LEFTSHIFT) || pressed_keys.contains(KEY_RIGHTSHIFT);
                        if (ctrl && shift && event.code == KEY_ESC && event.value) {
                            stop_requested_.store(true);
                            continue;
                        }
                        if (auto bit = modifier_bit(event.code)) {
                            if (event.value) keyboard_report[0] |=  static_cast<uint8_t>(1U << *bit);
                            else             keyboard_report[0] &= static_cast<uint8_t>(~(1U << *bit));
                        } else if (auto usage = key_to_hid(event.code)) {
                            auto begin = keyboard_report.begin() + 2;
                            auto end   = keyboard_report.end();
                            if (event.value) {
                                if (std::find(begin, end, *usage) == end) {
                                    auto slot = std::find(begin, end, 0);
                                    if (slot != end) *slot = *usage;
                                }
                            } else {
                                std::replace(begin, end, *usage, static_cast<uint8_t>(0));
                            }
                        }
                        write_report(keyboard_fd, keyboard_report.data(), keyboard_report.size(), "keyboard");
                    }

                    // Mouse
                    else if (dev.kind == DeviceKind::Mouse) {
                        if (event.type == EV_KEY) {
                            uint8_t mask = 0;
                            if      (event.code == BTN_LEFT)   mask = 1;
                            else if (event.code == BTN_RIGHT)  mask = 2;
                            else if (event.code == BTN_MIDDLE) mask = 4;
                            else if (event.code == BTN_SIDE)   mask = 8;
                            else if (event.code == BTN_EXTRA)  mask = 16;
                            if (mask) {
                                if (event.value) mouse_report[0] |= mask;
                                else             mouse_report[0] &= static_cast<uint8_t>(~mask);
                                write_report(mouse_fd, mouse_report.data(), mouse_report.size(), "mouse");
                            }
                        } else if (event.type == EV_REL) {
                            std::array<uint8_t, 4> report{mouse_report[0], 0, 0, 0};
                            if      (event.code == REL_X)     report[1] = static_cast<uint8_t>(clamp_i8(event.value));
                            else if (event.code == REL_Y)     report[2] = static_cast<uint8_t>(clamp_i8(event.value));
                            else if (event.code == REL_WHEEL) report[3] = static_cast<uint8_t>(clamp_i8(event.value));
                            else continue;
                            write_report(mouse_fd, report.data(), report.size(), "mouse");
                        }
                    }

                    // Gamepad → Xbox 360 report via FunctionFS ep1
                    else if (dev.kind == DeviceKind::Gamepad) {
                        bool changed = false;
                        if (event.type == EV_KEY) {
                            if (auto bit = xbox_button_bit(event.code)) {
                                gamepad_report.set_button(*bit, event.value != 0);
                                changed = true;
                            }
                        } else if (event.type == EV_ABS) {
                            auto info = dev.abs_info.find(event.code);
                            if (event.code == ABS_HAT0X) {
                                hat_x = event.value;
                                gamepad_report.set_hat(hat_x, hat_y);
                                changed = true;
                            } else if (event.code == ABS_HAT0Y) {
                                hat_y = event.value;
                                gamepad_report.set_hat(hat_x, hat_y);
                                changed = true;
                            } else if (auto off = xbox360_trigger_offset(event.code);
                                       off && info != dev.abs_info.end()) {
                                gamepad_report.set_trigger(*off, normalize_abs_u8(
                                    info->second.minimum, info->second.maximum, event.value));
                                changed = true;
                            } else if (auto off = xbox360_axis_offset(event.code);
                                       off && info != dev.abs_info.end()) {
                                int16_t val = normalize_abs_i16(
                                    info->second.minimum, info->second.maximum, event.value);
                                // Xbox 360 Y axes: positive = up; evdev Y axes: positive = down
                                if (event.code == ABS_Y || event.code == ABS_RY)
                                    val = static_cast<int16_t>(
                                        std::max(-32768, std::min(32767, -(int32_t)val)));
                                gamepad_report.set_axis(*off, val);
                                changed = true;
                            }
                        }
                        if (changed)
                            write_report(ep1_fd, gamepad_report.bytes.data(), gamepad_report.bytes.size(), "gamepad");
                    }
                }
            }
        }

        // Zero out all reports on stop
        const std::array<uint8_t, 8> zero8{};
        const std::array<uint8_t, 4> zero4{};
        write_report(keyboard_fd, zero8.data(), zero8.size(), "keyboard");
        write_report(mouse_fd,    zero4.data(), zero4.size(), "mouse");
        const Xbox360Report neutral_gamepad;
        write_report(ep1_fd, neutral_gamepad.bytes.data(), neutral_gamepad.bytes.size(), "gamepad");

        release_devices(devices);
        close(keyboard_fd); keyboard_fd = -1;
        close(mouse_fd);    mouse_fd    = -1;
        close_ep();
        unbind_gadget();
        set_status(false, "Stopped", "Capture released.");

    } catch (const std::exception &ex) {
        release_devices(devices);
        if (keyboard_fd >= 0) { close(keyboard_fd); }
        if (mouse_fd    >= 0) { close(mouse_fd);    }
        close_ep();
        unbind_gadget();
        set_status(false, "Error", ex.what());
    }
}

} // namespace sdc
