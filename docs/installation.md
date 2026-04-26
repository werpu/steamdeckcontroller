# Installation On SteamOS

The application is split into a root daemon and a normal user GTK frontend. The installer makes that setup repeatable on SteamOS.

The installer does four things:

1. installs the compiled frontend to `/usr/local/bin/steamdeckcontroller`
2. installs the compiled daemon to `/usr/local/bin/steamdeckcontrollerd`
3. installs a helper script that loads `libcomposite` and checks ConfigFS/UDC state
4. installs a systemd oneshot preparation service
5. installs a systemd daemon service
6. installs a desktop launcher for the unprivileged frontend

## Build First

From the repository:

```sh
cmake -S . -B build
cmake --build build
```

The GTK target must exist. If CMake says GTK is missing, install GTK development packages first.

## Install

```sh
sudo packaging/install-steamos.sh --build-dir build
```

Then run the preparation service once:

```sh
sudo systemctl start steamdeckcontroller-prepare.service
systemctl status steamdeckcontroller-prepare.service
```

Start the daemon:

```sh
sudo systemctl enable --now steamdeckcontroller.service
systemctl status steamdeckcontroller.service
```

The service checks:

- `libcomposite` can be loaded
- ConfigFS is mounted at `/sys/kernel/config`
- `/sys/kernel/config/usb_gadget` exists
- `/sys/class/udc` contains at least one USB device controller

If the UDC check fails on Steam Deck, enter BIOS and set:

```text
Advanced > USB Configuration > USB Dual Role Device = DRD
```

## Run

Launch the installed desktop entry:

```text
Steam Deck Controller Passthrough
```

The launcher runs the frontend as the normal user. The daemon must already be running through systemd.

## Add To Steam

In Desktop Mode:

1. Open Steam.
2. Choose **Add a Non-Steam Game**.
3. Add the installed desktop launcher or `/usr/local/bin/steamdeckcontroller`.
4. Do not use `sudo` or `pkexec` for the frontend.

If the frontend cannot connect, check:

```sh
systemctl status steamdeckcontroller.service
ls -l /run/steamdeckcontroller/control.sock
```

## Uninstall

```sh
sudo packaging/uninstall-steamos.sh
```

This removes:

- `/usr/local/bin/steamdeckcontroller`
- `/usr/local/bin/steamdeckcontrollerd`
- `/usr/local/lib/steamdeckcontroller`
- `/etc/systemd/system/steamdeckcontroller-prepare.service`
- `/etc/systemd/system/steamdeckcontroller.service`
- `/usr/local/share/applications/steamdeckcontroller.desktop`

Stop capture from the app before uninstalling.

## Why Not Make Everything Rootless?

The app needs privileged access to kernel interfaces:

```text
/sys/kernel/config/usb_gadget
/dev/hidg*
/dev/input/event*
EVIOCGRAB
```

Those should not be writable by a normal Steam session. The proper rootless user experience is a small root daemon plus a normal user frontend.
