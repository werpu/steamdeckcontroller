# Cross-Building Linux x86_64 Binaries on macOS

The Steam Deck runs Linux x86_64. If you develop on macOS you cannot build natively, but you can produce the correct Linux binaries using Docker running an x86_64 Ubuntu container. This page covers the full setup from a fresh macOS install.

## Prerequisites

You need [Homebrew](https://brew.sh). If it is not installed yet:

```sh
/bin/bash -c "$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/HEAD/install.sh)"
```

## 1. Install Docker CLI and Colima

Docker Desktop is not required. The Docker CLI plus [Colima](https://github.com/abiosoft/colima) (a lightweight VM that runs the Docker daemon) is enough.

```sh
brew install docker colima
```

`docker` is the CLI client. `colima` provides the daemon (replaces Docker Desktop's background service).

## 2. Start the Docker Daemon

```sh
colima start
```

On first run this downloads a small Linux VM image (~100 MB) and registers an x86_64 and arm64 emulator. Subsequent starts take a few seconds.

Verify the daemon is reachable:

```sh
docker info
```

You should see `Server Version` in the output. If you see a socket error, wait a moment and retry — the VM can take 5–10 seconds to fully boot.

Colima does not start automatically on login. Run `colima start` at the beginning of each development session, or add it to your shell profile / a launch agent if you prefer it always available.

## 3. Build and Package

From the repository root:

```sh
bin/build_under_macos.sh
```

The script:

1. Checks that Docker is available and that the daemon is running (calls `colima start` automatically if the daemon is not yet up).
2. Builds a `ubuntu:24.04` x86_64 Docker image with `build-essential`, `cmake`, `ninja-build`, `pkg-config`, and `libgtk-3-dev`.
3. Runs the CMake build inside the container and extracts the binaries to `dist/`.
4. Calls `bin/make-installer.sh` which packages everything into two ready-to-ship scripts.

A full build including the Ubuntu image pull and package installation takes around 3–5 minutes on the first run. Subsequent runs reuse Docker's layer cache and finish in under a minute.

## 4. Output

After a successful build everything lands in `dist/`:

```text
dist/steamdeckcontrollerd            — Linux x86_64 daemon binary
dist/steamdeckcontroller             — Linux x86_64 GTK frontend binary
dist/steamdeckcontroller-install.sh  — self-extracting installer for the Steam Deck
dist/steamdeckcontroller-uninstall.sh — companion uninstall script
```

The installer and uninstaller do not depend on the repository — they are the only files you need to copy to the Deck.

## 5. Deploy to the Steam Deck

Copy the two scripts over SSH or a USB drive:

```sh
scp dist/steamdeckcontroller-install.sh dist/steamdeckcontroller-uninstall.sh deck@steamdeck.local:~/
```

## 6. Install on the Steam Deck

```sh
sudo ./steamdeckcontroller-install.sh
```

Must be run via `sudo` from the `deck` account (not as root directly), so the installer can determine your home directory.

The installer places files in locations that survive SteamOS system updates:

| File | Location |
|---|---|
| `steamdeckcontrollerd` | `~/.local/share/steamdeckcontroller/` |
| `prepare-gadget.sh` | `~/.local/share/steamdeckcontroller/` |
| `steamdeckcontroller` | `~/.local/bin/` |
| systemd services | `/etc/systemd/system/` |

The service files are generated at install time with the correct absolute paths baked in.
The daemon runs as root via systemd. The frontend binary is owned by and runs as the `deck` user.

After install, start the services:

```sh
sudo systemctl start steamdeckcontroller-prepare.service
sudo systemctl start steamdeckcontroller.service
```

Make sure `~/.local/bin` is on your `PATH`. Add to `~/.bash_profile` or `~/.profile` if needed:

```sh
export PATH="$HOME/.local/bin:$PATH"
```

## 7. Uninstall

```sh
sudo ./steamdeckcontroller-uninstall.sh
```

This stops and disables both services, removes the service files, and deletes the binaries from `~/.local/share/steamdeckcontroller/` and `~/.local/bin/`. Stop capture from the app before uninstalling.

## Troubleshooting

**`command not found: docker`**
Homebrew's Docker CLI is installed to a prefix that may not be on your `PATH`. Run `brew doctor` and follow the instructions to fix your shell profile.

**`Cannot connect to the Docker daemon`**
The Colima VM is not running. Run `colima start` and wait for the `READY` message before retrying.

**`image platform mismatch` warning**
This warning is expected on Apple Silicon. Docker uses QEMU to run the x86_64 container and the binaries produced are correct Linux x86_64 regardless.

**Build fails inside Docker**
Check the output for the failing step. Common causes:
- Network issues during `apt-get` — retry; the Ubuntu mirrors are occasionally slow.
- A source file changed that triggers a CMake error — fix the code and re-run `bin/build_under_macos.sh`.

**Frontend not found after install**
`~/.local/bin` may not be on `PATH`. Add `export PATH="$HOME/.local/bin:$PATH"` to `~/.bash_profile`.

**Stopping Colima**
When you are done developing, stop the VM to reclaim memory:

```sh
colima stop
```
