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

void hat_covers_all_eight_directions() {
    sdc::XboxHidReport r;
    r.set_hat(0, -1); expect_eq(r.bytes[2], 0, "hat north");
    r.set_hat(1, -1); expect_eq(r.bytes[2], 1, "hat northeast");
    r.set_hat(1,  0); expect_eq(r.bytes[2], 2, "hat east");
    r.set_hat(1,  1); expect_eq(r.bytes[2], 3, "hat southeast");
    r.set_hat(0,  1); expect_eq(r.bytes[2], 4, "hat south");
    r.set_hat(-1, 1); expect_eq(r.bytes[2], 5, "hat southwest");
    r.set_hat(-1, 0); expect_eq(r.bytes[2], 6, "hat west");
    r.set_hat(-1,-1); expect_eq(r.bytes[2], 7, "hat northwest");
    r.set_hat(0,  0); expect_eq(r.bytes[2], 8, "hat neutral");
}

void buttons_span_both_report_bytes() {
    sdc::XboxHidReport r;
    // bits 8-10 land in bytes[1]
    r.set_button(8, true);
    expect_eq(r.bytes[1], 0x01, "guide button in high byte");
    r.set_button(9, true);
    expect_eq(r.bytes[1], 0x03, "thumbL added to high byte");
    r.set_button(8, false);
    expect_eq(r.bytes[1], 0x02, "guide released from high byte");
    // bytes[0] must be unaffected
    expect_eq(r.bytes[0], 0x00, "low byte unaffected by high-byte buttons");
    // buttons 12-15 (d-pad bits)
    r.set_button(12, true);
    expect_eq((r.bytes[1] >> 4) & 1, 1, "dpad-up bit 12 in high byte");
    r.set_button(15, true);
    expect_eq((r.bytes[1] >> 7) & 1, 1, "dpad-right bit 15 in high byte");
}

void xbox_report_full_state_matches_expected_bytes() {
    sdc::XboxHidReport r;

    // All face buttons + start + select pressed: bits 0-3, 6, 7
    for (int b : {0, 1, 2, 3, 6, 7}) r.set_button(b, true);
    expect_eq(r.bytes[0], 0b11001111, "face+start+select in low byte");
    expect_eq(r.bytes[1], 0x00,       "high byte still clear");

    // Hat pointing east
    r.set_hat(1, 0);
    expect_eq(r.bytes[2], 2, "east hat in byte 2");

    // Left trigger full, right trigger half
    r.set_trigger(3, 255);
    r.set_trigger(4, 128);
    expect_eq(r.bytes[3], 255, "left trigger");
    expect_eq(r.bytes[4], 128, "right trigger");

    // Left stick: max-left, max-up  (-32768, -32768)
    r.set_axis(5, -32768);
    r.set_axis(7, -32768);
    expect_eq(r.bytes[5], 0x00, "left-x low byte");
    expect_eq(r.bytes[6], 0x80, "left-x high byte");
    expect_eq(r.bytes[7], 0x00, "left-y low byte");
    expect_eq(r.bytes[8], 0x80, "left-y high byte");

    // Right stick: max-right, max-down (32767, 32767)
    r.set_axis(9,  32767);
    r.set_axis(11, 32767);
    expect_eq(r.bytes[9],  0xff, "right-x low byte");
    expect_eq(r.bytes[10], 0x7f, "right-x high byte");
    expect_eq(r.bytes[11], 0xff, "right-y low byte");
    expect_eq(r.bytes[12], 0x7f, "right-y high byte");

    // Total report size
    expect_eq(r.bytes.size(), sdc::XboxHidReport::size, "report is exactly 13 bytes");
}

void axis_normalization_midpoint_and_positive() {
    // Midpoint should be near zero for i16
    const auto mid = sdc::normalize_abs_i16(0, 65535, 32767);
    expect_true(mid >= -1 && mid <= 1, "stick midpoint maps near zero (i16)");

    // Positive maximum
    expect_eq(sdc::normalize_abs_i16(0, 65535, 65535), static_cast<int16_t>(32767),
              "stick maximum maps to 32767 (i16)");

    // Trigger midpoint
    const auto tmid = static_cast<int>(sdc::normalize_abs_u8(0, 255, 127));
    expect_true(tmid >= 126 && tmid <= 128, "trigger midpoint maps near 127 (u8)");
}

void xbox360_report_header_and_size() {
    sdc::Xbox360Report r;
    expect_eq(r.bytes.size(), sdc::Xbox360Report::size, "Xbox360Report is 20 bytes");
    expect_eq(r.bytes[0], 0x00, "type byte is 0x00");
    expect_eq(r.bytes[1], 0x14, "length byte is 20 (0x14)");
}

void xbox360_report_buttons_map_correctly() {
    sdc::Xbox360Report r;
    // A (abstract bit 0) → byte[3] bit 4
    r.set_button(0, true);
    expect_eq((r.bytes[3] >> 4) & 1, 1, "A button set");
    r.set_button(0, false);
    expect_eq((r.bytes[3] >> 4) & 1, 0, "A button cleared");

    // B (1) → byte[3] bit 5
    r.set_button(1, true);
    expect_eq((r.bytes[3] >> 5) & 1, 1, "B button set");

    // LB (4) → byte[3] bit 0
    r.set_button(4, true);
    expect_eq(r.bytes[3] & 1, 1, "LB set");

    // START (7) → byte[2] bit 4
    r.set_button(7, true);
    expect_eq((r.bytes[2] >> 4) & 1, 1, "START set");

    // GUIDE (8) → byte[3] bit 2
    r.set_button(8, true);
    expect_eq((r.bytes[3] >> 2) & 1, 1, "GUIDE set");

    // byte[2] must be unaffected by byte[3]-only buttons
    const uint8_t byte2_before = r.bytes[2];
    r.set_button(0, true);
    r.set_button(1, true);
    expect_eq(r.bytes[2], byte2_before, "byte[2] unchanged by face buttons");
}

void xbox360_report_hat_sets_dpad_bits() {
    sdc::Xbox360Report r;
    // Clear: neutral
    r.set_hat(0, 0);
    expect_eq(r.bytes[2] & 0x0f, 0, "hat neutral clears dpad nibble");

    // Up: y < 0  → bit 0
    r.set_hat(0, -1);
    expect_eq(r.bytes[2] & 0x0f, 0x01, "hat up sets bit 0");

    // Down: y > 0  → bit 1
    r.set_hat(0, 1);
    expect_eq(r.bytes[2] & 0x0f, 0x02, "hat down sets bit 1");

    // Left: x < 0  → bit 2
    r.set_hat(-1, 0);
    expect_eq(r.bytes[2] & 0x0f, 0x04, "hat left sets bit 2");

    // Right: x > 0  → bit 3
    r.set_hat(1, 0);
    expect_eq(r.bytes[2] & 0x0f, 0x08, "hat right sets bit 3");

    // Diagonal up-right: bits 0+3
    r.set_hat(1, -1);
    expect_eq(r.bytes[2] & 0x0f, 0x09, "hat up-right sets bits 0 and 3");

    // Upper nibble of byte[2] must be unaffected by hat
    r.set_button(7, true); // START → byte[2] bit 4
    const uint8_t upper = r.bytes[2] & 0xf0;
    r.set_hat(-1, 1); // down-left
    expect_eq(r.bytes[2] & 0xf0, upper, "hat does not touch upper nibble of byte[2]");
}

void xbox360_report_triggers_and_axes() {
    sdc::Xbox360Report r;
    r.set_trigger(*sdc::xbox360_trigger_offset(sdc::evdev::ABS_Z),  200); // left
    r.set_trigger(*sdc::xbox360_trigger_offset(sdc::evdev::ABS_RZ), 100); // right
    expect_eq(r.bytes[4], 200, "left trigger at byte 4");
    expect_eq(r.bytes[5], 100, "right trigger at byte 5");

    r.set_axis(*sdc::xbox360_axis_offset(sdc::evdev::ABS_X),  -32768); // left X min
    expect_eq(r.bytes[6], 0x00, "left X low");
    expect_eq(r.bytes[7], 0x80, "left X high");

    r.set_axis(*sdc::xbox360_axis_offset(sdc::evdev::ABS_RY), 32767); // right Y max
    expect_eq(r.bytes[12], 0xff, "right Y low");
    expect_eq(r.bytes[13], 0x7f, "right Y high");
}

void xbox360_y_axis_inversion_expected() {
    // The runtime negates ABS_Y and ABS_RY before calling set_axis so that
    // evdev "up = negative" maps to Xbox 360 "up = positive".
    // Verify the normalization used in the inversion:
    //   normalize_abs_i16(0, 65535, 0)     = -32768 (evdev full-up)
    //   negated → 32768, clamped → 32767   (Xbox 360 full-up)
    const int16_t evdev_up = sdc::normalize_abs_i16(0, 65535, 0);
    expect_eq(evdev_up, static_cast<int16_t>(-32768), "evdev full-up normalises to -32768");
    const int16_t xbox_up = static_cast<int16_t>(
        std::max(-32768, std::min(32767, -(int32_t)evdev_up)));
    expect_eq(xbox_up, static_cast<int16_t>(32767), "negated full-up clamps to 32767");

    const int16_t evdev_down = sdc::normalize_abs_i16(0, 65535, 65535);
    expect_eq(evdev_down, static_cast<int16_t>(32767), "evdev full-down normalises to 32767");
    const int16_t xbox_down = static_cast<int16_t>(
        std::max(-32768, std::min(32767, -(int32_t)evdev_down)));
    expect_eq(xbox_down, static_cast<int16_t>(-32767), "negated full-down gives -32767");

    // X axes are not inverted — verify the offset table excludes them
    expect_true(sdc::xbox360_axis_offset(sdc::evdev::ABS_X).has_value(),  "ABS_X has offset");
    expect_true(sdc::xbox360_axis_offset(sdc::evdev::ABS_Y).has_value(),  "ABS_Y has offset");
    expect_true(sdc::xbox360_axis_offset(sdc::evdev::ABS_RX).has_value(), "ABS_RX has offset");
    expect_true(sdc::xbox360_axis_offset(sdc::evdev::ABS_RY).has_value(), "ABS_RY has offset");
}

void xbox360_offsets_are_correct() {
    expect_eq(*sdc::xbox360_trigger_offset(sdc::evdev::ABS_Z),  4UL, "left trigger offset");
    expect_eq(*sdc::xbox360_trigger_offset(sdc::evdev::ABS_RZ), 5UL, "right trigger offset");
    expect_eq(*sdc::xbox360_axis_offset(sdc::evdev::ABS_X),     6UL, "left X offset");
    expect_eq(*sdc::xbox360_axis_offset(sdc::evdev::ABS_Y),     8UL, "left Y offset");
    expect_eq(*sdc::xbox360_axis_offset(sdc::evdev::ABS_RX),   10UL, "right X offset");
    expect_eq(*sdc::xbox360_axis_offset(sdc::evdev::ABS_RY),   12UL, "right Y offset");
    expect_true(!sdc::xbox360_trigger_offset(sdc::evdev::ABS_X).has_value(), "stick axis not a trigger");
    expect_true(!sdc::xbox360_axis_offset(sdc::evdev::ABS_Z).has_value(),    "trigger not an axis");
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
    hat_covers_all_eight_directions();
    buttons_span_both_report_bytes();
    xbox_report_full_state_matches_expected_bytes();
    axis_normalization_midpoint_and_positive();
    xbox360_report_header_and_size();
    xbox360_report_buttons_map_correctly();
    xbox360_report_hat_sets_dpad_bits();
    xbox360_report_triggers_and_axes();
    xbox360_offsets_are_correct();
    xbox360_y_axis_inversion_expected();

    if (failures != 0) {
        std::cerr << failures << " test expectation(s) failed\n";
        return EXIT_FAILURE;
    }

    std::cout << "All input translation tests passed\n";
    return EXIT_SUCCESS;
}
