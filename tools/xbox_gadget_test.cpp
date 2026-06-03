// Standalone diagnostic tool.
//
// Presents ONLY a pure Xbox 360 controller over USB — no keyboard, no mouse,
// no Steam, no control socket. The Xbox vendor interface is the ONLY interface,
// so it sits at interface 0 like a real wired controller (unlike the daemon's
// composite, where it is interface 2 behind the HID keyboard/mouse).
//
// It also injects a slow, visible test pattern (toggles the A button, wiggles
// the left stick) so you can confirm in joy.cpl / a gamepad tester that the
// device BOTH enumerates AND delivers input — isolating the USB-gadget layer
// from the daemon / Steam Input / evdev path entirely.
//
//   sudo ./xbox_gadget_test
//
// Stop with Ctrl+C; the gadget is torn down cleanly on exit.

#include "input_translation.hpp"

#include <sys/mount.h>
#include <sys/poll.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

#include <array>
#include <atomic>
#include <cerrno>
#include <csignal>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace {

constexpr const char *kGadgetPath  = "/sys/kernel/config/usb_gadget/xbox_test";
constexpr const char *kFfsMountDir = "/run/xbox_gadget_test/ffs";
constexpr const char *kFfsFuncName = "xboxtest";

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

std::atomic_bool g_stop{false};
void on_signal(int) { g_stop.store(true); }

std::string read_first_udc() {
    const std::filesystem::path udc_dir("/sys/class/udc");
    if (!std::filesystem::exists(udc_dir)) return {};
    for (const auto &e : std::filesystem::directory_iterator(udc_dir))
        return e.path().filename().string();
    return {};
}

void write_text(const std::filesystem::path &path, const std::string &value) {
    std::ofstream out(path);
    if (!out) throw std::runtime_error("open " + path.string() + ": " + std::strerror(errno));
    out << value;
    out.flush();
    if (!out) throw std::runtime_error("write " + path.string() + ": " + std::strerror(errno));
}

void write_binary(const std::filesystem::path &path, const std::vector<uint8_t> &value) {
    std::ofstream out(path, std::ios::binary);
    if (!out) throw std::runtime_error("open " + path.string() + ": " + std::strerror(errno));
    out.write(reinterpret_cast<const char *>(value.data()),
              static_cast<std::streamsize>(value.size()));
}

void ensure_dir(const std::filesystem::path &p) {
    std::error_code ec;
    std::filesystem::create_directories(p, ec);
    if (ec) throw std::runtime_error("mkdir " + p.string() + ": " + ec.message());
}

void ensure_symlink(const std::filesystem::path &target, const std::filesystem::path &link) {
    std::error_code ec;
    if (std::filesystem::exists(link, ec)) return;
    std::filesystem::create_symlink(target, link, ec);
    if (ec) throw std::runtime_error("link " + link.string() + ": " + ec.message());
}

void append_le16(std::vector<uint8_t> &v, uint16_t x) {
    v.push_back(x & 0xff); v.push_back((x >> 8) & 0xff);
}
void append_le32(std::vector<uint8_t> &v, uint32_t x) {
    v.push_back(x & 0xff); v.push_back((x >> 8) & 0xff);
    v.push_back((x >> 16) & 0xff); v.push_back((x >> 24) & 0xff);
}

std::vector<uint8_t> xbox360_interface_block(uint16_t ep_max_pkt) {
    std::vector<uint8_t> b;
    // Interface descriptor — Xbox 360 gamepad, interface 0
    b.insert(b.end(), {9, 0x04, 0, 0, 2, 0xFF, 0x5D, 0x01, 0});
    // NOTE: a real wired pad carries a 17-byte vendor descriptor (type 0x21)
    // here, but FunctionFS rejects it with EINVAL: the kernel parser only
    // accepts a USB_TYPE_CLASS|0x01 (0x21) descriptor when the interface class
    // is HID/CCID/DFU, and ours is vendor (0xFF). xusb binds on VID/PID +
    // class 0xFF/0x5D/proto 0x01, so we omit it.
    // EP1 IN — interrupt
    b.insert(b.end(), {7, 0x05, 0x81, 0x03});
    append_le16(b, ep_max_pkt);
    b.push_back(4);
    // EP1 OUT — interrupt (rumble)
    b.insert(b.end(), {7, 0x05, 0x01, 0x03});
    append_le16(b, ep_max_pkt);
    b.push_back(8);
    return b;
}

std::vector<uint8_t> build_ffs_descriptors() {
    const auto fs_block = xbox360_interface_block(20);
    const auto hs_block = xbox360_interface_block(32);
    std::vector<uint8_t> r;
    append_le32(r, 3);                                   // MAGIC_V2
    append_le32(r, 5 * 4 + fs_block.size() + hs_block.size());
    append_le32(r, 1 | 2);                               // HAS_FS | HAS_HS
    append_le32(r, 3);                                   // fs_count (iface + 2 ep)
    append_le32(r, 3);                                   // hs_count
    r.insert(r.end(), fs_block.begin(), fs_block.end());
    r.insert(r.end(), hs_block.begin(), hs_block.end());
    return r;
}

std::vector<uint8_t> build_ffs_strings() {
    std::vector<uint8_t> r;
    append_le32(r, 2);   // STRINGS_MAGIC
    append_le32(r, 16);
    append_le32(r, 0);
    append_le32(r, 0);
    return r;
}

// Without the daemon, nothing has loaded the USB-gadget plumbing. The configfs
// root rejects an unregistered top-level group with EPERM, so creating the
// gadget dir fails with "Operation not permitted" until libcomposite registers
// /sys/kernel/config/usb_gadget. Do that here so the tool runs standalone.
void ensure_gadget_subsystem() {
    namespace fs = std::filesystem;
    constexpr const char *kSubsys = "/sys/kernel/config/usb_gadget";
    if (fs::exists(kSubsys)) return;

    if (std::system("modprobe libcomposite") != 0)
        std::cerr << "warning: 'modprobe libcomposite' failed\n";

    // Still absent → configfs isn't mounted. Mount it; libcomposite (now loaded)
    // registers usb_gadget the moment configfs appears.
    if (!fs::exists(kSubsys) &&
        mount("none", "/sys/kernel/config", "configfs", 0, nullptr) != 0)
        std::cerr << "warning: mount configfs: " << std::strerror(errno) << "\n";

    if (!fs::exists(kSubsys))
        throw std::runtime_error(
            "/sys/kernel/config/usb_gadget unavailable — run as root; needs "
            "configfs mounted and the libcomposite module.");
}

int setup_gadget() {
    ensure_gadget_subsystem();

    const std::string udc = read_first_udc();
    if (udc.empty())
        throw std::runtime_error("No UDC in /sys/class/udc — connect the Deck "
                                 "directly to a host PC (not a dock), BIOS USB = DRD.");

    const std::filesystem::path g(kGadgetPath);
    ensure_dir(g);

    write_text(g / "idVendor",       "0x045e");
    write_text(g / "idProduct",      "0x028e");
    write_text(g / "bcdDevice",      "0x0114");
    write_text(g / "bcdUSB",         "0x0200");
    write_text(g / "bDeviceClass",   "0xff");
    write_text(g / "bDeviceSubClass","0xff");
    write_text(g / "bDeviceProtocol","0xff");

    ensure_dir(g / "strings/0x409");
    write_text(g / "strings/0x409/serialnumber", "xboxtest-1");
    write_text(g / "strings/0x409/manufacturer", "TestGadget");
    write_text(g / "strings/0x409/product",      "Xbox 360 Test Controller");

    ensure_dir(g / "configs/c.1/strings/0x409");
    write_text(g / "configs/c.1/MaxPower",                    "250");
    write_text(g / "configs/c.1/strings/0x409/configuration", "Xbox 360 test");

    ensure_dir(g / "functions/ffs.xboxtest");
    ensure_dir(kFfsMountDir);
    umount(kFfsMountDir);
    if (mount(kFfsFuncName, kFfsMountDir, "functionfs", 0, nullptr) != 0)
        throw std::runtime_error(std::string("FunctionFS mount: ") + std::strerror(errno));

    const std::string ep0 = std::string(kFfsMountDir) + "/ep0";
    int ep0_fd = open(ep0.c_str(), O_RDWR);
    if (ep0_fd < 0)
        throw std::runtime_error(std::string("open ep0: ") + std::strerror(errno));

    const auto descs = build_ffs_descriptors();
    const auto strs  = build_ffs_strings();
    if (write(ep0_fd, descs.data(), descs.size()) != static_cast<ssize_t>(descs.size()) ||
        write(ep0_fd, strs.data(),  strs.size())  != static_cast<ssize_t>(strs.size())) {
        close(ep0_fd);
        throw std::runtime_error(std::string("write descriptors: ") + std::strerror(errno));
    }

    ensure_symlink(g / "functions/ffs.xboxtest", g / "configs/c.1/ffs.xboxtest");
    write_text(g / "UDC", udc);

    std::cout << "Gadget bound to UDC: " << udc << "\n";
    return ep0_fd;
}

void teardown_gadget() {
    const std::filesystem::path g(kGadgetPath);
    std::error_code ec;
    if (std::filesystem::exists(g / "UDC")) {
        std::ofstream udc(g / "UDC");
        if (udc) udc << "";
    }
    std::filesystem::remove(g / "configs/c.1/ffs.xboxtest", ec);
    umount(kFfsMountDir);
    std::filesystem::remove_all(kFfsMountDir, ec);
    std::filesystem::remove_all(g / "functions/ffs.xboxtest", ec);
    std::filesystem::remove_all(g / "configs/c.1/strings/0x409", ec);
    std::filesystem::remove_all(g / "configs/c.1", ec);
    std::filesystem::remove_all(g / "strings/0x409", ec);
    std::filesystem::remove(g, ec);
    std::cout << "Gadget torn down.\n";
}

} // namespace

int main() {
    std::signal(SIGINT, on_signal);
    std::signal(SIGTERM, on_signal);
    std::signal(SIGPIPE, SIG_IGN);

    if (geteuid() != 0) {
        std::cerr << "Run as root: sudo " << "xbox_gadget_test\n";
        return 1;
    }

    int ep0_fd = -1;
    int ep1_fd = -1;
    try {
        ep0_fd = setup_gadget();
        std::cout << "Waiting for host to enumerate... (open joy.cpl on the PC)\n";

        sdc::Xbox360Report report;
        bool a_pressed = false;
        int16_t stick = 0;
        int16_t dir = 4000;
        int tick = 0;

        while (!g_stop.load()) {
            struct pollfd pfd { ep0_fd, POLLIN, 0 };
            const int ready = poll(&pfd, 1, 100);

            if (ready > 0 && (pfd.revents & POLLIN)) {
                FfsEvent ev{};
                if (read(ep0_fd, &ev, sizeof(ev)) == sizeof(ev)) {
                    switch (ev.type) {
                    case kFfsEventBind:   std::cout << "FFS BIND\n"; break;
                    case kFfsEventEnable:
                        ep1_fd = open((std::string(kFfsMountDir) + "/ep1").c_str(),
                                      O_WRONLY | O_NONBLOCK);
                        std::cout << "FFS ENABLE — host configured us; ep1 "
                                  << (ep1_fd >= 0 ? "open, sending test pattern"
                                                  : "OPEN FAILED") << "\n";
                        break;
                    case kFfsEventDisable:
                        std::cout << "FFS DISABLE\n";
                        if (ep1_fd >= 0) { close(ep1_fd); ep1_fd = -1; }
                        break;
                    case kFfsEventSetup: {
                        const uint8_t dir_in = ev.u.ctrl.bRequestType & 0x80;
                        if (dir_in) {
                            std::vector<uint8_t> resp(ev.u.ctrl.wLength, 0);
                            write(ep0_fd, resp.data(), ev.u.ctrl.wLength);
                        } else {
                            if (ev.u.ctrl.wLength > 0) {
                                std::vector<uint8_t> buf(ev.u.ctrl.wLength);
                                read(ep0_fd, buf.data(), buf.size());
                            }
                            uint8_t zlp = 0;
                            write(ep0_fd, &zlp, 0);
                        }
                        break;
                    }
                    default: break;
                    }
                }
            }

            // ~ every 500ms (5 poll ticks) emit a visible test pattern
            if (ep1_fd >= 0 && ++tick >= 5) {
                tick = 0;
                a_pressed = !a_pressed;
                report.set_button(0, a_pressed);              // toggle A
                stick = static_cast<int16_t>(stick + dir);
                if (stick > 24000 || stick < -24000) dir = -dir;
                report.set_axis(*sdc::xbox360_axis_offset(sdc::evdev::ABS_X), stick);
                const ssize_t w = write(ep1_fd, report.bytes.data(), report.bytes.size());
                if (w < 0)
                    std::cerr << "ep1 write failed: " << std::strerror(errno) << "\n";
            }
        }
    } catch (const std::exception &ex) {
        std::cerr << "error: " << ex.what() << "\n";
    }

    if (ep1_fd >= 0) close(ep1_fd);
    if (ep0_fd >= 0) close(ep0_fd);
    teardown_gadget();
    return 0;
}
