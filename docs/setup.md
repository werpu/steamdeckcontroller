# Setup Guide

This guide describes the daemon/frontend setup. The daemon runs as root through systemd. The GTK frontend runs as the normal Steam user and only sends commands to the daemon.

## 1. Steam Deck Firmware Setup

The Deck USB-C port must expose USB device/gadget mode.

Enter BIOS:

1. Power off the Deck.
2. Hold **Volume Up**.
3. Press **Power** once.
4. Keep holding **Volume Up** until the setup screen appears.
5. Open **Setup Utility**.

Set:

```text
Advanced > USB Configuration > USB Dual Role Device = DRD
```

Boot back into SteamOS and check:

```sh
ls /sys/class/udc
```

This must print at least one controller name.

## 2. Build

From the repository:

```sh
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

On SteamOS, make sure GTK development files are installed if the GUI target is not built.

## 3. Install

Run:

```sh
sudo packaging/install-steamos.sh --build-dir build
```

The installer places:

```text
/usr/local/bin/steamdeckcontroller
/usr/local/bin/steamdeckcontrollerd
/etc/systemd/system/steamdeckcontroller-prepare.service
/etc/systemd/system/steamdeckcontroller.service
```

## 4. Start Services

Prepare gadget support:

```sh
sudo systemctl start steamdeckcontroller-prepare.service
systemctl status steamdeckcontroller-prepare.service
```

Start and enable the daemon:

```sh
sudo systemctl enable --now steamdeckcontroller.service
systemctl status steamdeckcontroller.service
```

Check the control socket:

```sh
ls -l /run/steamdeckcontroller/control.sock
```

## 5. Test The Daemon

Without starting the GUI, test the socket:

```sh
printf 'STATUS\n' | socat - UNIX-CONNECT:/run/steamdeckcontroller/control.sock
```

Expected shape:

```text
STATUS STOPPED Stopped
No capture active.
```

Start capture:

```sh
printf 'START\n' | socat - UNIX-CONNECT:/run/steamdeckcontroller/control.sock
```

Stop capture:

```sh
printf 'STOP\n' | socat - UNIX-CONNECT:/run/steamdeckcontroller/control.sock
```

If `socat` is not installed, use the GTK frontend for manual testing.

## 6. Add The Frontend To Steam

Add this as a non-Steam game:

```text
/usr/local/bin/steamdeckcontroller
```

Do not run it through `sudo` or `pkexec`. The frontend is intentionally unprivileged.

The systemd daemon must already be running. The GUI only sends:

```text
START
STOP
STATUS
```

## 7. Use

1. Connect the Steam Deck USB-C port directly to the host computer.
2. Open the frontend from Steam.
3. Press **Start**.
4. The daemon grabs keyboard, mouse, and controller event devices and forwards them to the host.
5. Press **Stop** from the touchscreen or another ungrabbed input path.

Keyboard, mouse, and controller events are blocked locally while grabbed. Touch input usually remains local because the touchscreen is not grabbed by the current classifier.

## 8. Troubleshooting

No UDC:

```sh
ls /sys/class/udc
```

If empty, check BIOS DRD mode.

No `/sys/kernel/config/usb_gadget`:

```sh
sudo modprobe libcomposite
mount | grep configfs
ls /sys/kernel/config/usb_gadget
```

Daemon logs:

```sh
journalctl -u steamdeckcontroller.service -f
```

Preparation logs:

```sh
journalctl -u steamdeckcontroller-prepare.service -f
```

## 9. Uninstall

```sh
sudo packaging/uninstall-steamos.sh
```
