# Steam Deck Controller Passthrough

GTK/Linux prototype that grabs local evdev keyboard, mouse, and gamepad input and forwards it to a connected USB host as HID keyboard, mouse, and Xbox-style HID gamepad devices.

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

**On Linux x86_64 / Steam Deck:**

```sh
bin/build_under_x86.sh
```

**On macOS:**

Install the build dependencies once (Homebrew: `cmake`, `docker`, `colima`, `docker-buildx`, and the buildx CLI-plugin link):

```sh
bin/macos_install_build_deps.sh
```

Then either run the native build (compiles and runs the unit tests, no Docker):

```sh
bin/build_for_macos.sh
```

or cross-compile the Linux binaries and build the installer via Docker:

```sh
colima start
bin/build_under_macos.sh
```

The Docker build uses BuildKit (via `docker-buildx`); without it Docker falls back to the deprecated legacy builder, which still works but prints a deprecation warning.

See [Cross-building on macOS](docs/cross-build-macos.md) for the full walkthrough.

If GTK 3 is not installed, CMake still builds the portable unit tests but skips the GTK application target.

## Test

Tests run automatically at the end of each build script. On Linux, to run them standalone:

```sh
ctest --test-dir build --output-on-failure
```

On macOS, `bin/build_for_macos.sh` runs the tests natively, and `bin/build_under_macos.sh` runs them inside Docker.

## Run

```sh
sudo ./build/steamdeckcontroller
```

Press **Start** to create the USB gadget and grab matching input devices. Press **Stop** to release the grabs and unbind the gadget.

While capture is active, `Ctrl+Shift+Esc` is reserved as a local emergency stop chord.

## Notes

Keyboard and relative mouse forwarding are direct HID translations. The controller endpoint uses an Xbox-style HID layout with A/B/X/Y buttons, shoulders, guide/back/start, stick buttons, D-pad, analog triggers, and 16-bit stick axes.

When the running kernel exposes the HID gadget `interval` ConfigFS attribute, the app requests interval `1` for each HID function before binding the gadget. Older kernels do not expose this attribute, so the app leaves their kernel defaults in place.

This is still not a true Xbox 360/XInput USB device. Real wired Xbox controllers use vendor-specific USB interfaces, so full XInput emulation would need a FunctionFS/raw-gadget implementation instead of the kernel HID gadget function. If `/sys/class/udc` is empty, the current hardware/kernel cannot present itself as a USB device through software alone.

## Documentation

- [Architecture](docs/architecture.md)
- [Input translation](docs/input-translation.md)
- [Runtime, verification, and latency](docs/runtime-and-latency.md)
- [Setup guide](docs/setup.md)
- [Testing](docs/testing.md)
- [Installation on SteamOS](docs/installation.md)
- [Cross-building on macOS](docs/cross-build-macos.md)
- [Privileged daemon and Steam frontend](docs/daemon-frontend-split.md)
