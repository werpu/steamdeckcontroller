# Steam Deck Controller Passthrough

GTK/Linux prototype that grabs local evdev keyboard, mouse, and gamepad input and forwards it to a connected USB host as HID keyboard, mouse, and generic gamepad devices.

## Requirements

- Linux with `/dev/input/event*`
- USB gadget/device-mode capable hardware
- ConfigFS mounted at `/sys/kernel/config`
- A non-empty `/sys/class/udc`
- GTK 3 development package
- Root privileges, or equivalent access to `/dev/input`, `/dev/hidg*`, and `/sys/kernel/config/usb_gadget`

On Debian/Ubuntu-style systems:

```sh
sudo apt install build-essential cmake pkg-config libgtk-3-dev
```

## Build

```sh
cmake -S . -B build
cmake --build build
```

## Run

```sh
sudo ./build/steamdeckcontroller
```

Press **Start** to create the USB gadget and grab matching input devices. Press **Stop** to release the grabs and unbind the gadget.

While capture is active, `Ctrl+Shift+Esc` is reserved as a local emergency stop chord.

## Notes

Keyboard and relative mouse forwarding are direct HID translations. The controller endpoint is a generic HID gamepad, not a true Xbox/XInput device. If `/sys/class/udc` is empty, the current hardware/kernel cannot present itself as a USB device through software alone.
