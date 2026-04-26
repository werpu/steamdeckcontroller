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

void xbox_buttons_and_axes_map_to_report_positions() {
    expect_eq(*sdc::xbox_button_bit(sdc::evdev::BTN_SOUTH), 0, "A button bit");
    expect_eq(*sdc::xbox_button_bit(sdc::evdev::BTN_EAST), 1, "B button bit");
    expect_eq(*sdc::xbox_button_bit(sdc::evdev::BTN_WEST), 2, "X button bit");
    expect_eq(*sdc::xbox_button_bit(sdc::evdev::BTN_NORTH), 3, "Y button bit");
    expect_eq(*sdc::xbox_button_bit(sdc::evdev::BTN_MODE), 8, "guide button bit");
    expect_true(!sdc::xbox_button_bit(9999).has_value(), "unknown gamepad button unmapped");

    expect_eq(*sdc::xbox_trigger_offset(sdc::evdev::ABS_Z), 3UL, "left trigger report offset");
    expect_eq(*sdc::xbox_trigger_offset(sdc::evdev::ABS_RZ), 4UL, "right trigger report offset");
    expect_true(!sdc::xbox_trigger_offset(sdc::evdev::ABS_X).has_value(), "stick axis is not a trigger");

    expect_eq(*sdc::xbox_axis_offset(sdc::evdev::ABS_X), 5UL, "left stick X report offset");
    expect_eq(*sdc::xbox_axis_offset(sdc::evdev::ABS_Y), 7UL, "left stick Y report offset");
    expect_eq(*sdc::xbox_axis_offset(sdc::evdev::ABS_RX), 9UL, "right stick X report offset");
    expect_eq(*sdc::xbox_axis_offset(sdc::evdev::ABS_RY), 11UL, "right stick Y report offset");
    expect_true(!sdc::xbox_axis_offset(sdc::evdev::ABS_Z).has_value(), "trigger is not a stick axis");
}

void absolute_axes_normalize_to_signed_hid_range() {
    expect_eq(sdc::normalize_abs(0, 65535, 0), -127, "absolute minimum maps to -127");
    expect_eq(sdc::normalize_abs(0, 65535, 65535), 127, "absolute maximum maps to 127");
    expect_true(std::abs(sdc::normalize_abs(0, 65535, 32768)) <= 1, "absolute midpoint maps near zero");
    expect_eq(sdc::normalize_abs(5, 5, 5), 0, "invalid range maps to neutral");
    expect_eq(sdc::normalize_abs(0, 100, -100), -127, "low out-of-range values clamp");
    expect_eq(sdc::normalize_abs(0, 100, 200), 127, "high out-of-range values clamp");

    expect_eq(static_cast<int>(sdc::normalize_abs_u8(0, 1023, 0)), 0, "trigger minimum maps to 0");
    expect_eq(static_cast<int>(sdc::normalize_abs_u8(0, 1023, 1023)), 255, "trigger maximum maps to 255");
    expect_eq(sdc::normalize_abs_i16(0, 65535, 0), static_cast<int16_t>(-32768), "stick minimum maps to -32768");
    expect_eq(sdc::normalize_abs_i16(0, 65535, 65535), static_cast<int16_t>(32767), "stick maximum maps to 32767");
}

void mouse_relative_values_clamp_to_hid_range() {
    expect_eq(sdc::clamp_i8(-500), -127, "negative mouse delta clamps");
    expect_eq(sdc::clamp_i8(0), 0, "zero mouse delta stays zero");
    expect_eq(sdc::clamp_i8(500), 127, "positive mouse delta clamps");
}

void xbox_report_writes_expected_bytes() {
    sdc::XboxHidReport report;
    expect_eq(report.bytes[2], 8, "neutral hat value");

    report.set_button(0, true);
    report.set_button(7, true);
    expect_eq(report.bytes[0], 0x81, "button low byte");
    report.set_button(0, false);
    expect_eq(report.bytes[0], 0x80, "button release clears bit");

    report.set_hat(1, -1);
    expect_eq(report.bytes[2], 1, "hat northeast");
    report.set_hat(0, 0);
    expect_eq(report.bytes[2], 8, "hat neutral");

    report.set_trigger(3, 200);
    expect_eq(report.bytes[3], 200, "left trigger byte");

    report.set_axis(5, -32768);
    expect_eq(report.bytes[5], 0, "axis low byte");
    expect_eq(report.bytes[6], 128, "axis high byte");
}

} // namespace

int main() {
    keyboard_letters_follow_hid_usage_order();
    keyboard_digits_and_controls_map_to_hid_usage_ids();
    unknown_keys_are_not_mapped();
    modifier_bits_match_boot_keyboard_report_layout();
    xbox_buttons_and_axes_map_to_report_positions();
    absolute_axes_normalize_to_signed_hid_range();
    mouse_relative_values_clamp_to_hid_range();
    xbox_report_writes_expected_bytes();

    if (failures != 0) {
        std::cerr << failures << " test expectation(s) failed\n";
        return EXIT_FAILURE;
    }

    std::cout << "All input translation tests passed\n";
    return EXIT_SUCCESS;
}
