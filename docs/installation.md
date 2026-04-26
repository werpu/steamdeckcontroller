# Installation On SteamOS

The current application still runs the GTK UI and the privileged forwarding code in one process. That means it must run as root. The installer below makes setup repeatable, but it is not the final daemon/frontend architecture.

The installer does four things:

1. installs the compiled binary to `/usr/local/bin/steamdeckcontroller`
2. installs a helper script that loads `libcomposite` and checks ConfigFS/UDC state
3. installs a systemd oneshot preparation service
4. installs a desktop launcher that starts the app through `pkexec`

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

From Konsole:

```sh
pkexec /usr/local/bin/steamdeckcontroller
```

Or launch the installed desktop entry:

```text
Steam Deck Controller Passthrough
```

The launcher uses `pkexec`, so it should ask for authentication. This is a temporary compromise until the project has a privileged daemon and unprivileged Steam frontend.

## Add To Steam

In Desktop Mode:

1. Open Steam.
2. Choose **Add a Non-Steam Game**.
3. Add the installed desktop launcher or `/usr/local/bin/steamdeckcontroller`.
4. If adding the binary directly, set the launch command to use `pkexec`.

Running GUI programs through `pkexec` from Gaming Mode can be awkward. If authentication or display access fails there, launch from Desktop Mode or implement the daemon/frontend split described in [Privileged daemon and Steam frontend](daemon-frontend-split.md).

## Uninstall

```sh
sudo packaging/uninstall-steamos.sh
```

This removes:

- `/usr/local/bin/steamdeckcontroller`
- `/usr/local/lib/steamdeckcontroller`
- `/etc/systemd/system/steamdeckcontroller-prepare.service`
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
