#include "input_translation.hpp"

#include <cstdlib>
#include <iostream>
#include <string>

namespace {

int failures = 0;

void expect_true(bool condition, const std::string &name) {
    if (!condition) {
        ++failures;
        std::cerr << "FAIL: " << name << '\n';
    }
}

template <typename T, typename U>
void expect_eq(const T &actual, const U &expected, const std::string &name) {
    if (!(actual == expected)) {
        ++failures;
        std::cerr << "FAIL: " << name << " expected " << expected << " got " << actual << '\n';
    }
}

void keyboard_letters_follow_hid_usage_order() {
    expect_eq(static_cast<int>(*sdc::key_to_hid(sdc::evdev::KEY_A)), 0x04, "KEY_A maps to HID A");
    expect_eq(static_cast<int>(*sdc::key_to_hid(sdc::evdev::KEY_B)), 0x05, "KEY_B maps to HID B");
    expect_eq(static_cast<int>(*sdc::key_to_hid(sdc::evdev::KEY_Z)), 0x1d, "KEY_Z maps to HID Z");
}

void keyboard_digits_and_controls_map_to_hid_usage_ids() {
    expect_eq(static_cast<int>(*sdc::key_to_hid(sdc::evdev::KEY_1)), 0x1e, "KEY_1 maps to HID 1");
    expect_eq(static_cast<int>(*sdc::key_to_hid(sdc::evdev::KEY_9)), 0x26, "KEY_9 maps to HID 9");
    expect_eq(static_cast<int>(*sdc::key_to_hid(sdc::evdev::KEY_0)), 0x27, "KEY_0 maps to HID 0");
    expect_eq(static_cast<int>(*sdc::key_to_hid(sdc::evdev::KEY_ENTER)), 0x28, "Enter maps to HID enter");
    expect_eq(static_cast<int>(*sdc::key_to_hid(sdc::evdev::KEY_ESC)), 0x29, "Escape maps to HID escape");
    expect_eq(static_cast<int>(*sdc::key_to_hid(sdc::evdev::KEY_SPACE)), 0x2c, "Space maps to HID space");
    expect_eq(static_cast<int>(*sdc::key_to_hid(sdc::evdev::KEY_RIGHT)), 0x4f, "Right arrow maps to HID right");
    expect_eq(static_cast<int>(*sdc::key_to_hid(sdc::evdev::KEY_UP)), 0x52, "Up arrow maps to HID up");
}

void unknown_keys_are_not_mapped() {
    expect_true(!sdc::key_to_hid(9999).has_value(), "unknown keyboard key is unmapped");
}

void modifier_bits_match_boot_keyboard_report_layout() {
    expect_eq(*sdc::modifier_bit(sdc::evdev::KEY_LEFTCTRL), 0, "left control modifier bit");
    expect_eq(*sdc::modifier_bit(sdc::evdev::KEY_LEFTSHIFT), 1, "left shift modifier bit");
    expect_eq(*sdc::modifier_bit(sdc::evdev::KEY_LEFTALT), 2, "left alt modifier bit");
    expect_eq(*sdc::modifier_bit(sdc::evdev::KEY_LEFTMETA), 3, "left meta modifier bit");
    expect_eq(*sdc::modifier_bit(sdc::evdev::KEY_RIGHTCTRL), 4, "right control modifier bit");
    expect_eq(*sdc::modifier_bit(sdc::evdev::KEY_RIGHTSHIFT), 5, "right shift modifier bit");
    expect_eq(*sdc::modifier_bit(sdc::evdev::KEY_RIGHTALT), 6, "right alt modifier bit");
    expect_eq(*sdc::modifier_bit(sdc::evdev::KEY_RIGHTMETA), 7, "right meta modifier bit");
    expect_true(!sdc::modifier_bit(sdc::evdev::KEY_A).has_value(), "non-modifier has no modifier bit");
}

void gamepad_buttons_and_axes_map_to_report_positions() {
    expect_eq(*sdc::gamepad_button_bit(sdc::evdev::BTN_SOUTH), 0, "south button bit");
    expect_eq(*sdc::gamepad_button_bit(sdc::evdev::BTN_EAST), 1, "east button bit");
    expect_eq(*sdc::gamepad_button_bit(sdc::evdev::BTN_MODE), 10, "mode button bit");
    expect_true(!sdc::gamepad_button_bit(9999).has_value(), "unknown gamepad button unmapped");

    expect_eq(sdc::gamepad_axis_index(sdc::evdev::ABS_X), 2, "ABS_X report index");
    expect_eq(sdc::gamepad_axis_index(sdc::evdev::ABS_Y), 3, "ABS_Y report index");
    expect_eq(sdc::gamepad_axis_index(sdc::evdev::ABS_RX), 4, "ABS_RX report index");
    expect_eq(sdc::gamepad_axis_index(sdc::evdev::ABS_RY), 5, "ABS_RY report index");
    expect_eq(sdc::gamepad_axis_index(sdc::evdev::ABS_Z), 6, "ABS_Z report index");
    expect_eq(sdc::gamepad_axis_index(sdc::evdev::ABS_RZ), 7, "ABS_RZ report index");
    expect_eq(sdc::gamepad_axis_index(9999), -1, "unknown axis report index");
}

void absolute_axes_normalize_to_signed_hid_range() {
    expect_eq(sdc::normalize_abs(0, 65535, 0), -127, "absolute minimum maps to -127");
    expect_eq(sdc::normalize_abs(0, 65535, 65535), 127, "absolute maximum maps to 127");
    expect_true(std::abs(sdc::normalize_abs(0, 65535, 32768)) <= 1, "absolute midpoint maps near zero");
    expect_eq(sdc::normalize_abs(5, 5, 5), 0, "invalid range maps to neutral");
    expect_eq(sdc::normalize_abs(0, 100, -100), -127, "low out-of-range values clamp");
    expect_eq(sdc::normalize_abs(0, 100, 200), 127, "high out-of-range values clamp");
}

void mouse_relative_values_clamp_to_hid_range() {
    expect_eq(sdc::clamp_i8(-500), -127, "negative mouse delta clamps");
    expect_eq(sdc::clamp_i8(0), 0, "zero mouse delta stays zero");
    expect_eq(sdc::clamp_i8(500), 127, "positive mouse delta clamps");
}

} // namespace

int main() {
    keyboard_letters_follow_hid_usage_order();
    keyboard_digits_and_controls_map_to_hid_usage_ids();
    unknown_keys_are_not_mapped();
    modifier_bits_match_boot_keyboard_report_layout();
    gamepad_buttons_and_axes_map_to_report_positions();
    absolute_axes_normalize_to_signed_hid_range();
    mouse_relative_values_clamp_to_hid_range();

    if (failures != 0) {
        std::cerr << failures << " test expectation(s) failed\n";
        return EXIT_FAILURE;
    }

    std::cout << "All input translation tests passed\n";
    return EXIT_SUCCESS;
}
