#include "input_translation.hpp"

#include <algorithm>

namespace sdc {

XboxHidReport::XboxHidReport() {
    bytes[2] = 8;
}

void XboxHidReport::set_button(int bit, bool pressed) {
    if (bit < 0 || bit > 15) {
        return;
    }
    uint16_t buttons = static_cast<uint16_t>(bytes[0] | (bytes[1] << 8));
    const uint16_t mask = static_cast<uint16_t>(1U << bit);
    if (pressed) {
        buttons |= mask;
    } else {
        buttons &= static_cast<uint16_t>(~mask);
    }
    bytes[0] = static_cast<uint8_t>(buttons & 0xff);
    bytes[1] = static_cast<uint8_t>((buttons >> 8) & 0xff);
}

void XboxHidReport::set_hat(int x, int y) {
    uint8_t hat = 8;
    if (x == 0 && y < 0) hat = 0;
    else if (x > 0 && y < 0) hat = 1;
    else if (x > 0 && y == 0) hat = 2;
    else if (x > 0 && y > 0) hat = 3;
    else if (x == 0 && y > 0) hat = 4;
    else if (x < 0 && y > 0) hat = 5;
    else if (x < 0 && y == 0) hat = 6;
    else if (x < 0 && y < 0) hat = 7;
    bytes[2] = hat;
}

void XboxHidReport::set_trigger(size_t offset, uint8_t value) {
    if (offset < bytes.size()) {
        bytes[offset] = value;
    }
}

void XboxHidReport::set_axis(size_t offset, int16_t value) {
    if (offset + 1 >= bytes.size()) {
        return;
    }
    bytes[offset] = static_cast<uint8_t>(value & 0xff);
    bytes[offset + 1] = static_cast<uint8_t>((value >> 8) & 0xff);
}

std::optional<uint8_t> key_to_hid(int code) {
    if (code >= evdev::KEY_1 && code <= evdev::KEY_9) {
        return static_cast<uint8_t>(0x1e + code - evdev::KEY_1);
    }
    if (code == evdev::KEY_0) return 0x27;
    switch (code) {
        case evdev::KEY_A: return 0x04;
        case evdev::KEY_B: return 0x05;
        case evdev::KEY_C: return 0x06;
        case evdev::KEY_D: return 0x07;
        case evdev::KEY_E: return 0x08;
        case evdev::KEY_F: return 0x09;
        case evdev::KEY_G: return 0x0a;
        case evdev::KEY_H: return 0x0b;
        case evdev::KEY_I: return 0x0c;
        case evdev::KEY_J: return 0x0d;
        case evdev::KEY_K: return 0x0e;
        case evdev::KEY_L: return 0x0f;
        case evdev::KEY_M: return 0x10;
        case evdev::KEY_N: return 0x11;
        case evdev::KEY_O: return 0x12;
        case evdev::KEY_P: return 0x13;
        case evdev::KEY_Q: return 0x14;
        case evdev::KEY_R: return 0x15;
        case evdev::KEY_S: return 0x16;
        case evdev::KEY_T: return 0x17;
        case evdev::KEY_U: return 0x18;
        case evdev::KEY_V: return 0x19;
        case evdev::KEY_W: return 0x1a;
        case evdev::KEY_X: return 0x1b;
        case evdev::KEY_Y: return 0x1c;
        case evdev::KEY_Z: return 0x1d;
        case evdev::KEY_ENTER: return 0x28;
        case evdev::KEY_ESC: return 0x29;
        case evdev::KEY_BACKSPACE: return 0x2a;
        case evdev::KEY_TAB: return 0x2b;
        case evdev::KEY_SPACE: return 0x2c;
        case evdev::KEY_MINUS: return 0x2d;
        case evdev::KEY_EQUAL: return 0x2e;
        case evdev::KEY_LEFTBRACE: return 0x2f;
        case evdev::KEY_RIGHTBRACE: return 0x30;
        case evdev::KEY_BACKSLASH: return 0x31;
        case evdev::KEY_SEMICOLON: return 0x33;
        case evdev::KEY_APOSTROPHE: return 0x34;
        case evdev::KEY_GRAVE: return 0x35;
        case evdev::KEY_COMMA: return 0x36;
        case evdev::KEY_DOT: return 0x37;
        case evdev::KEY_SLASH: return 0x38;
        case evdev::KEY_CAPSLOCK: return 0x39;
        case evdev::KEY_F1: return 0x3a;
        case evdev::KEY_F2: return 0x3b;
        case evdev::KEY_F3: return 0x3c;
        case evdev::KEY_F4: return 0x3d;
        case evdev::KEY_F5: return 0x3e;
        case evdev::KEY_F6: return 0x3f;
        case evdev::KEY_F7: return 0x40;
        case evdev::KEY_F8: return 0x41;
        case evdev::KEY_F9: return 0x42;
        case evdev::KEY_F10: return 0x43;
        case evdev::KEY_F11: return 0x44;
        case evdev::KEY_F12: return 0x45;
        case evdev::KEY_PRINT: return 0x46;
        case evdev::KEY_SCROLLLOCK: return 0x47;
        case evdev::KEY_PAUSE: return 0x48;
        case evdev::KEY_INSERT: return 0x49;
        case evdev::KEY_HOME: return 0x4a;
        case evdev::KEY_PAGEUP: return 0x4b;
        case evdev::KEY_DELETE: return 0x4c;
        case evdev::KEY_END: return 0x4d;
        case evdev::KEY_PAGEDOWN: return 0x4e;
        case evdev::KEY_RIGHT: return 0x4f;
        case evdev::KEY_LEFT: return 0x50;
        case evdev::KEY_DOWN: return 0x51;
        case evdev::KEY_UP: return 0x52;
        default: return std::nullopt;
    }
}

std::optional<int> modifier_bit(int code) {
    switch (code) {
        case evdev::KEY_LEFTCTRL: return 0;
        case evdev::KEY_LEFTSHIFT: return 1;
        case evdev::KEY_LEFTALT: return 2;
        case evdev::KEY_LEFTMETA: return 3;
        case evdev::KEY_RIGHTCTRL: return 4;
        case evdev::KEY_RIGHTSHIFT: return 5;
        case evdev::KEY_RIGHTALT: return 6;
        case evdev::KEY_RIGHTMETA: return 7;
        default: return std::nullopt;
    }
}

std::optional<int> xbox_button_bit(int code) {
    switch (code) {
        case evdev::BTN_SOUTH: return 0;
        case evdev::BTN_EAST: return 1;
        case evdev::BTN_WEST: return 2;
        case evdev::BTN_NORTH: return 3;
        case evdev::BTN_TL: return 4;
        case evdev::BTN_TR: return 5;
        case evdev::BTN_SELECT: return 6;
        case evdev::BTN_START: return 7;
        case evdev::BTN_MODE: return 8;
        case evdev::BTN_THUMBL: return 9;
        case evdev::BTN_THUMBR: return 10;
        case evdev::BTN_DPAD_UP: return 12;
        case evdev::BTN_DPAD_DOWN: return 13;
        case evdev::BTN_DPAD_LEFT: return 14;
        case evdev::BTN_DPAD_RIGHT: return 15;
        default: return std::nullopt;
    }
}

std::optional<size_t> xbox_trigger_offset(int code) {
    switch (code) {
        case evdev::ABS_Z: return 3;
        case evdev::ABS_RZ: return 4;
        default: return std::nullopt;
    }
}

std::optional<size_t> xbox_axis_offset(int code) {
    switch (code) {
        case evdev::ABS_X: return 5;
        case evdev::ABS_Y: return 7;
        case evdev::ABS_RX: return 9;
        case evdev::ABS_RY: return 11;
        default: return std::nullopt;
    }
}

// Xbox360Report — 20-byte wire protocol
// bytes[0]=0x00 (type), bytes[1]=0x14 (length=20)
// bytes[2]: DPAD_UP(0) DPAD_DOWN(1) DPAD_LEFT(2) DPAD_RIGHT(3) START(4) BACK(5) THUMBL(6) THUMBR(7)
// bytes[3]: LB(0) RB(1) GUIDE(2) -(3) A(4) B(5) X(6) Y(7)
// bytes[4]: left trigger, bytes[5]: right trigger
// bytes[6-7]: left stick X, bytes[8-9]: left stick Y
// bytes[10-11]: right stick X, bytes[12-13]: right stick Y
// bytes[14-19]: reserved

Xbox360Report::Xbox360Report() {
    bytes[0] = 0x00; // input report type
    bytes[1] = 0x14; // report length = 20
}

void Xbox360Report::set_button(int bit, bool pressed) {
    int byte_idx = -1;
    int byte_bit = -1;
    switch (bit) {
        case 0:  byte_idx = 3; byte_bit = 4; break; // A
        case 1:  byte_idx = 3; byte_bit = 5; break; // B
        case 2:  byte_idx = 3; byte_bit = 6; break; // X
        case 3:  byte_idx = 3; byte_bit = 7; break; // Y
        case 4:  byte_idx = 3; byte_bit = 0; break; // LB
        case 5:  byte_idx = 3; byte_bit = 1; break; // RB
        case 6:  byte_idx = 2; byte_bit = 5; break; // BACK
        case 7:  byte_idx = 2; byte_bit = 4; break; // START
        case 8:  byte_idx = 3; byte_bit = 2; break; // GUIDE
        case 9:  byte_idx = 2; byte_bit = 6; break; // THUMBL
        case 10: byte_idx = 2; byte_bit = 7; break; // THUMBR
        case 12: byte_idx = 2; byte_bit = 0; break; // DPAD_UP
        case 13: byte_idx = 2; byte_bit = 1; break; // DPAD_DOWN
        case 14: byte_idx = 2; byte_bit = 2; break; // DPAD_LEFT
        case 15: byte_idx = 2; byte_bit = 3; break; // DPAD_RIGHT
        default: return;
    }
    if (pressed) bytes[byte_idx] |= static_cast<uint8_t>(1 << byte_bit);
    else         bytes[byte_idx] &= static_cast<uint8_t>(~(1 << byte_bit));
}

void Xbox360Report::set_hat(int x, int y) {
    bytes[2] &= 0xF0; // clear dpad bits (lower nibble)
    if (y < 0) bytes[2] |= 0x01; // DPAD_UP
    if (y > 0) bytes[2] |= 0x02; // DPAD_DOWN
    if (x < 0) bytes[2] |= 0x04; // DPAD_LEFT
    if (x > 0) bytes[2] |= 0x08; // DPAD_RIGHT
}

void Xbox360Report::set_trigger(size_t offset, uint8_t value) {
    if (offset < bytes.size()) {
        bytes[offset] = value;
    }
}

void Xbox360Report::set_axis(size_t offset, int16_t value) {
    if (offset + 1 >= bytes.size()) {
        return;
    }
    bytes[offset]     = static_cast<uint8_t>(value & 0xff);
    bytes[offset + 1] = static_cast<uint8_t>((value >> 8) & 0xff);
}

std::optional<size_t> xbox360_trigger_offset(int code) {
    switch (code) {
        case evdev::ABS_Z:  return 4; // left trigger
        case evdev::ABS_RZ: return 5; // right trigger
        default: return std::nullopt;
    }
}

std::optional<size_t> xbox360_axis_offset(int code) {
    switch (code) {
        case evdev::ABS_X:  return 6;  // left stick X
        case evdev::ABS_Y:  return 8;  // left stick Y
        case evdev::ABS_RX: return 10; // right stick X
        case evdev::ABS_RY: return 12; // right stick Y
        default: return std::nullopt;
    }
}

int clamp_i8(int value) {
    return std::max(-127, std::min(127, value));
}

int normalize_abs(int minimum, int maximum, int value) {
    if (maximum <= minimum) {
        return 0;
    }
    const double normalized = (static_cast<double>(value - minimum) / (maximum - minimum)) * 254.0 - 127.0;
    return clamp_i8(static_cast<int>(normalized));
}

uint8_t normalize_abs_u8(int minimum, int maximum, int value) {
    if (maximum <= minimum) {
        return 0;
    }
    const double normalized = (static_cast<double>(value - minimum) / (maximum - minimum)) * 255.0;
    return static_cast<uint8_t>(std::max(0, std::min(255, static_cast<int>(normalized))));
}

int16_t normalize_abs_i16(int minimum, int maximum, int value) {
    if (maximum <= minimum) {
        return 0;
    }
    const double normalized = (static_cast<double>(value - minimum) / (maximum - minimum)) * 65535.0 - 32768.0;
    return static_cast<int16_t>(std::max(-32768, std::min(32767, static_cast<int>(normalized))));
}

} // namespace sdc
