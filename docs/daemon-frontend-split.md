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

The GTK app changes from this:

```text
Start button -> setup_gadget() + open input devices + forward events
```

to this:

```text
Start button -> connect socket -> send START -> show response
Stop button  -> connect socket -> send STOP  -> show response
timer        -> connect socket -> send STATUS -> update label
```

This lets Steam launch the frontend normally. The service already has the required privileges.

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

## Development Migration Plan

1. Move gadget/input forwarding code from `main.cpp` into a reusable runtime class.
2. Keep `input_translation` as the pure tested library.
3. Add `steamdeckcontrollerd`, a CLI daemon target that runs as root and exposes the Unix socket.
4. Change the GTK executable to a frontend-only target.
5. Add manual integration tests with `socat`.
6. Add the systemd unit file under a packaging or deploy directory.

The current code already has the important split started: report translation is in `steamdeckcontroller_core`, while Linux runtime and GTK are still mixed in `main.cpp`.
