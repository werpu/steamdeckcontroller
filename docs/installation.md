# Installation On SteamOS

The application is split into a privileged root daemon and an unprivileged GTK frontend. The self-extracting installer handles both in one step.

Files are installed to locations that survive SteamOS system updates:

| File | Location |
|---|---|
| `steamdeckcontrollerd` | `~/.local/share/steamdeckcontroller/` |
| `prepare-gadget.sh` | `~/.local/share/steamdeckcontroller/` |
| `steamdeckcontroller` | `~/.local/bin/` |
| `steamdeckcontroller-prepare.service` | `/etc/systemd/system/` |
| `steamdeckcontroller.service` | `/etc/systemd/system/` |

The daemon runs as root via systemd. The frontend binary is owned by and runs as the normal `deck` user.

## Build First

**On the Steam Deck or any Linux x86_64 machine:**

```sh
bin/build_under_x86.sh
```

**On macOS (cross-compile via Docker):**

```sh
bin/build_under_macos.sh
```

Both build scripts produce `steamdeckcontroller-install.sh` and `steamdeckcontroller-uninstall.sh` in the repository root at the end of the build.

See [Cross-building on macOS](cross-build-macos.md) for the full macOS setup including Docker and Colima.

## Install

Transfer the installer to the Steam Deck:

```sh
scp dist/steamdeckcontroller-install.sh dist/steamdeckcontroller-uninstall.sh deck@steamdeck.local:~/
```

On the Steam Deck, run via `sudo` from the `deck` account:

```sh
sudo ./steamdeckcontroller-install.sh
```

Running via `sudo` (not directly as root) is required so the installer can identify your home directory.

Then start the services:

```sh
sudo systemctl start steamdeckcontroller-prepare.service
sudo systemctl start steamdeckcontroller.service
systemctl status steamdeckcontroller.service
```

The preparation service checks:

- `libcomposite` can be loaded
- ConfigFS is mounted at `/sys/kernel/config`
- `/sys/kernel/config/usb_gadget` exists
- `/sys/class/udc` contains at least one USB device controller

If the UDC check fails on Steam Deck, enter BIOS and set:

```text
Advanced > USB Configuration > USB Dual Role Device = DRD
```

## Run

Make sure `~/.local/bin` is on your `PATH`. Add to `~/.bash_profile` or `~/.profile` if it is not:

```sh
export PATH="$HOME/.local/bin:$PATH"
```

Launch the frontend:

```sh
steamdeckcontroller
```

The daemon must already be running through systemd. The frontend runs as the normal user and only sends `START`, `STOP`, and `STATUS` commands over the Unix socket.

## Add To Steam

In Desktop Mode:

1. Open Steam.
2. Choose **Add a Non-Steam Game**.
3. Point it at `~/.local/bin/steamdeckcontroller`.
4. Do not use `sudo` or `pkexec` — the frontend is intentionally unprivileged.

If the frontend cannot connect to the daemon, check:

```sh
systemctl status steamdeckcontroller.service
ls -l /run/steamdeckcontroller/control.sock
```

## Uninstall

```sh
sudo ./steamdeckcontroller-uninstall.sh
```

This stops and disables both services, removes the service files from `/etc/systemd/system/`, and deletes:

- `~/.local/share/steamdeckcontroller/`
- `~/.local/bin/steamdeckcontroller`

Stop capture from the app before uninstalling.

## Why Install to the Home Directory?

SteamOS uses an A/B partition scheme where system updates replace the root filesystem. Anything written to `/usr` or `/usr/local` is wiped on each update. The home partition (`/home`) is on a separate partition and is never touched by updates. Installing to `~/.local/` is the correct approach for software that needs to survive across SteamOS updates.

The systemd service files in `/etc/systemd/system/` are on an overlay that persists across minor updates. After a major SteamOS update you may need to re-enable the services:

```sh
sudo systemctl enable steamdeckcontroller-prepare.service
sudo systemctl enable steamdeckcontroller.service
```

The binaries themselves in `~/.local/` are unaffected.
