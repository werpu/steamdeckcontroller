#pragma once

#include <cstdint>
#include <optional>

namespace sdc {

namespace evdev {
constexpr int KEY_ESC = 1;
constexpr int KEY_1 = 2;
constexpr int KEY_9 = 10;
constexpr int KEY_0 = 11;
constexpr int KEY_MINUS = 12;
constexpr int KEY_EQUAL = 13;
constexpr int KEY_BACKSPACE = 14;
constexpr int KEY_TAB = 15;
constexpr int KEY_Q = 16;
constexpr int KEY_W = 17;
constexpr int KEY_E = 18;
constexpr int KEY_R = 19;
constexpr int KEY_T = 20;
constexpr int KEY_Y = 21;
constexpr int KEY_U = 22;
constexpr int KEY_I = 23;
constexpr int KEY_O = 24;
constexpr int KEY_P = 25;
constexpr int KEY_LEFTBRACE = 26;
constexpr int KEY_RIGHTBRACE = 27;
constexpr int KEY_ENTER = 28;
constexpr int KEY_LEFTCTRL = 29;
constexpr int KEY_A = 30;
constexpr int KEY_S = 31;
constexpr int KEY_D = 32;
constexpr int KEY_F = 33;
constexpr int KEY_G = 34;
constexpr int KEY_H = 35;
constexpr int KEY_J = 36;
constexpr int KEY_K = 37;
constexpr int KEY_L = 38;
constexpr int KEY_SEMICOLON = 39;
constexpr int KEY_APOSTROPHE = 40;
constexpr int KEY_GRAVE = 41;
constexpr int KEY_LEFTSHIFT = 42;
constexpr int KEY_BACKSLASH = 43;
constexpr int KEY_Z = 44;
constexpr int KEY_X = 45;
constexpr int KEY_C = 46;
constexpr int KEY_V = 47;
constexpr int KEY_B = 48;
constexpr int KEY_N = 49;
constexpr int KEY_M = 50;
constexpr int KEY_COMMA = 51;
constexpr int KEY_DOT = 52;
constexpr int KEY_SLASH = 53;
constexpr int KEY_RIGHTSHIFT = 54;
constexpr int KEY_LEFTALT = 56;
constexpr int KEY_SPACE = 57;
constexpr int KEY_CAPSLOCK = 58;
constexpr int KEY_F1 = 59;
constexpr int KEY_F2 = 60;
constexpr int KEY_F3 = 61;
constexpr int KEY_F4 = 62;
constexpr int KEY_F5 = 63;
constexpr int KEY_F6 = 64;
constexpr int KEY_F7 = 65;
constexpr int KEY_F8 = 66;
constexpr int KEY_F9 = 67;
constexpr int KEY_F10 = 68;
constexpr int KEY_SCROLLLOCK = 70;
constexpr int KEY_F11 = 87;
constexpr int KEY_F12 = 88;
constexpr int KEY_RIGHTCTRL = 97;
constexpr int KEY_RIGHTALT = 100;
constexpr int KEY_HOME = 102;
constexpr int KEY_UP = 103;
constexpr int KEY_PAGEUP = 104;
constexpr int KEY_LEFT = 105;
constexpr int KEY_RIGHT = 106;
constexpr int KEY_END = 107;
constexpr int KEY_DOWN = 108;
constexpr int KEY_PAGEDOWN = 109;
constexpr int KEY_INSERT = 110;
constexpr int KEY_DELETE = 111;
constexpr int KEY_PAUSE = 119;
constexpr int KEY_LEFTMETA = 125;
constexpr int KEY_RIGHTMETA = 126;
constexpr int KEY_PRINT = 210;

constexpr int BTN_SOUTH = 0x130;
constexpr int BTN_EAST = 0x131;
constexpr int BTN_NORTH = 0x133;
constexpr int BTN_WEST = 0x134;
constexpr int BTN_TL = 0x136;
constexpr int BTN_TR = 0x137;
constexpr int BTN_SELECT = 0x13a;
constexpr int BTN_START = 0x13b;
constexpr int BTN_MODE = 0x13c;
constexpr int BTN_THUMBL = 0x13d;
constexpr int BTN_THUMBR = 0x13e;

constexpr int ABS_X = 0x00;
constexpr int ABS_Y = 0x01;
constexpr int ABS_Z = 0x02;
constexpr int ABS_RX = 0x03;
constexpr int ABS_RY = 0x04;
constexpr int ABS_RZ = 0x05;
} // namespace evdev

std::optional<uint8_t> key_to_hid(int code);
std::optional<int> modifier_bit(int code);
std::optional<int> gamepad_button_bit(int code);
int gamepad_axis_index(int code);
int clamp_i8(int value);
int normalize_abs(int minimum, int maximum, int value);

} // namespace sdc
