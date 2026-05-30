# Testing

The project has CTest-based unit tests that run without Steam Deck hardware.

Tests run automatically at the end of each build script:

**On the Steam Deck or any Linux x86_64 machine:**

```sh
bin/build_under_x86.sh
```

**On macOS — tests run inside the Docker container:**

```sh
bin/build_under_macos.sh
```

To run tests standalone on Linux after a build (build directory is `build/`):

```sh
ctest --test-dir build --output-on-failure
```

## Test Targets

`input_translation_tests`

Validates pure input/report translation:

- evdev keyboard code to USB HID usage mapping
- keyboard modifier bits
- mouse relative value clamping
- Xbox-style controller button mappings
- trigger and stick axis normalization
- Xbox-style HID report byte packing

`control_protocol_tests`

Validates the daemon/frontend protocol without touching USB gadget hardware:

- command line sanitizing
- client wire format:
  - `START\n`
  - `STOP\n`
  - `STATUS\n`
- daemon status response formatting
- frontend response parsing
- mock-runtime dispatch for `START`
- mock-runtime dispatch for `STOP`
- mock-runtime dispatch for `STATUS`
- unknown command handling

The mock runtime lets the command handler be tested without `/sys/kernel/config`, `/dev/input`, `/dev/hidg*`, root privileges, or systemd.

## Not Covered By Unit Tests

These need manual or integration testing on the Steam Deck:

- ConfigFS gadget creation
- `/sys/class/udc` availability
- `/dev/hidg*` endpoint creation
- `EVIOCGRAB` behavior
- actual input forwarding latency
- host-side USB enumeration
- systemd service startup
- Gaming Mode launch behavior

## Manual Daemon Protocol Check

On the Deck, with `steamdeckcontroller.service` running:

```sh
printf 'STATUS\n' | socat - UNIX-CONNECT:/run/steamdeckcontroller/control.sock
printf 'START\n' | socat - UNIX-CONNECT:/run/steamdeckcontroller/control.sock
printf 'STOP\n' | socat - UNIX-CONNECT:/run/steamdeckcontroller/control.sock
```

Expected response shapes:

```text
STATUS STOPPED Stopped
OK Started
OK Stopped
```
