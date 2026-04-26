# Privileged Daemon And Steam Frontend

Launching the GTK app from Steam as root is not a good long-term design. The safer design is a privileged system service that owns the kernel interfaces and a normal Steam-launched frontend that only sends commands.

```text
Steam / GTK frontend
        |
        | local IPC: start, stop, status
        v
privileged systemd service
        |
        | /dev/input/event*
        | /sys/kernel/config/usb_gadget
        | /dev/hidg*
        v
USB gadget + grabbed input devices
```

## Responsibility Split

The daemon owns everything that needs elevated privileges:

- creating and binding the USB gadget
- opening `/dev/hidg*`
- opening `/dev/input/event*`
- using `EVIOCGRAB`
- forwarding evdev events to HID reports
- releasing grabs and unbinding the gadget on stop

The frontend stays unprivileged:

- shows status
- exposes Start and Stop buttons
- sends commands to the daemon
- renders errors from the daemon

The frontend does not receive or forward input events. Once capture is active, the daemon owns the grabbed evdev file descriptors and the USB HID gadget endpoints.

## IPC Shape

A simple Unix domain socket is enough.

Suggested socket path:

```text
/run/steamdeckcontroller/control.sock
```

Suggested text protocol:

```text
START
STOP
STATUS
```

Example responses:

```text
OK STARTED
OK STOPPED
STATUS RUNNING Forwarding 3 devices
ERR No USB device controller found in /sys/class/udc
ERR Permission denied opening /dev/input/event4
```

Text commands are intentionally boring. They are easy to test with:

```sh
printf 'STATUS\n' | socat - UNIX-CONNECT:/run/steamdeckcontroller/control.sock
```

## systemd Unit

Install the daemon binary somewhere root-owned, for example:

```text
/usr/local/bin/steamdeckcontrollerd
```

Create:

```text
/etc/systemd/system/steamdeckcontroller.service
```

Example:

```ini
[Unit]
Description=Steam Deck Controller USB Passthrough Daemon
After=multi-user.target

[Service]
Type=simple
ExecStart=/usr/local/bin/steamdeckcontrollerd
Restart=on-failure
RuntimeDirectory=steamdeckcontroller
RuntimeDirectoryMode=0755

# The daemon needs these privileges for ConfigFS, evdev, hidg, and EVIOCGRAB.
User=root
Group=root

[Install]
WantedBy=multi-user.target
```

Enable it:

```sh
sudo systemctl daemon-reload
sudo systemctl enable --now steamdeckcontroller.service
```

Check logs:

```sh
journalctl -u steamdeckcontroller.service -f
```

## Frontend Flow

The GTK app now works as a frontend-only process:

```text
Start button -> connect socket -> send START -> show response
Stop button  -> connect socket -> send STOP  -> show response
timer        -> connect socket -> send STATUS -> update label
```

This lets Steam launch the frontend normally. The service already has the required privileges.

The GUI starts and stops capture, not the daemon process itself. The daemon process is expected to be started by systemd and remain available in the background.

## Local Input During Capture

When capture starts, the daemon grabs the selected keyboard, mouse, and controller event devices with `EVIOCGRAB`. Those events are then blocked from the local SteamOS/Wayland session and forwarded to the connected USB host.

Touch input usually remains local because the touchscreen is a separate absolute touch device and is not classified as a keyboard, relative mouse, or gamepad by the current grab logic. That means the Stop button can still work from the Deck touchscreen while keyboard/mouse/controller input is being forwarded.

Do not rely only on the Stop button. Keep at least one recovery path:

- touchscreen, if it remains ungrabbed
- `Ctrl+Shift+Esc` emergency chord
- SSH into the Deck and send `STOP` to the socket
- systemd stop/restart of the daemon

## Recovery Behavior

The daemon should handle cleanup even if the frontend exits:

1. keep forwarding after frontend disconnects
2. stop on explicit `STOP`
3. release all `EVIOCGRAB` grabs on daemon shutdown
4. send neutral HID reports before unbinding
5. clear the gadget `UDC` on shutdown

Add a signal handler for:

```text
SIGINT
SIGTERM
```

Both should call the same cleanup path.

## Current Implementation State

The split is implemented in these pieces:

- `steamdeckcontrollerd`: root daemon and Unix socket server
- `ControllerRuntime`: owns gadget setup, evdev grabbing, and forwarding
- `steamdeckcontroller`: normal GTK frontend
- `steamdeckcontroller_core`: shared translation/runtime library

Remaining work:

1. Add a small `steamdeckcontrollerctl` command for shell control without `socat`.
2. Tighten socket permissions or add a group-based access policy.
3. Add structured daemon logs for device selection and forwarding errors.
4. Add config for device include/exclude rules.
