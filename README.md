# PktSwerve

PktSwerve is a small tray app that helps you reach websites your network or provider blocks. It does this by changing how your computer sends the first packets of a connection, so the filters that block those sites no longer recognise them.

It runs only on your own computer. It is not a VPN, it does not send your traffic through any server, and it does not make you anonymous.

![PktSwerve settings window](git_img/ss1.png)
![PktSwerve settings window](git_img/ss2.png)
![PktSwerve settings window](git_img/ss3.png)

## What you need

- **Windows:** Windows 10 or 11 (64-bit). The app asks for administrator rights when it starts.
- **macOS:** macOS 12 or newer, Apple Silicon or Intel. It asks for your password the first time you press Start.

Linux and Android are not supported.

## Install

### Windows

1. Download `PktSwerve-win64.zip` from the [latest release](https://github.com/luridlane-dev/PktSwerve/releases/latest).
2. Extract the whole folder (keep the WinDivert files next to `PktSwerve.exe`).
3. Run `PktSwerve.exe` and accept the administrator prompt.

To remove it completely (app, startup task, WinDivert driver, settings, logs), use **Uninstall** in the tray menu, or right-click `uninstall.ps1` in the folder and choose **Run with PowerShell**. Deleting the folder alone leaves the startup task and the driver behind.

### macOS

Paste this line into Terminal:

```bash
curl -fsSL https://raw.githubusercontent.com/luridlane-dev/PktSwerve/main/packaging/macos/install.sh | bash
```

It downloads the app, installs it in Applications and opens it. Downloading with `curl` is what keeps macOS from showing a security warning, because the app is not notarized.

To remove it completely (app, helper, settings, logs):

```bash
curl -fsSL https://raw.githubusercontent.com/luridlane-dev/PktSwerve/main/packaging/macos/uninstall.sh | bash
```

## Use it

1. Click the PktSwerve icon in the tray (Windows) or menu bar (macOS).
2. Press **Start**. Press **Stop** to turn it off.
3. Open **Settings** to choose what it works on:
   - **Selected Sites & Apps:** only the sites and programs you list. Everything else is left alone. This is the best place to begin.
   - **Global:** all web traffic.
   - **Disabled:** does nothing.

If a site still does not open, try the **DNS** tab: pick another DNS server and press **Test Connection**. The **Methods** tab holds the more technical options; the defaults are fine for most people, and if you change them, change one at a time.

## Updates

PktSwerve checks for a new version once a day. When one is available, an **Update available** item appears in the tray menu. Open it, go to **Settings → About** and press **Install**. The app downloads the update, checks that the file is intact, restarts itself and keeps your settings. You can also check by hand or switch the daily check off on the same tab.

## If something goes wrong

- **Windows: nothing happens or it closes at once.** Run it as administrator and make sure `WinDivert.dll` and `WinDivert64.sys` are in the same folder as `PktSwerve.exe`.
- **No tray icon.** Check your notification area settings in Windows.
- **A site breaks after changing a setting.** Go back to the defaults with **Reset to Defaults** in Settings.
- **Logs** help when asking for support:
  - Windows: `%LOCALAPPDATA%\PktSwerve\pktswerve.log`
  - macOS: `~/Library/Application Support/PktSwerve/` (`pktswerve.log` and `pktswerve-helper.log`)

## How it works

For websites, PktSwerve splits the first message of a connection (the TLS handshake or the HTTP request) into smaller pieces, and can optionally send decoy packets. It can also send DNS questions to a server you choose. On Windows it uses [WinDivert](https://reqrypt.org/windivert.html) to do this; on macOS a built-in packet-filter rule hands the traffic to a small local proxy that does the splitting. Nothing is decrypted, and nothing leaves your computer except your normal traffic.

Because of that, it cannot hide who you are from the sites you visit, and it can fail on networks that block in other ways.

## Build from source

You need CMake 3.16+, Qt 6 (Widgets, Network, Concurrent) and a C++17 compiler.

```powershell
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
```

On Windows, also download [WinDivert](https://reqrypt.org/windivert.html) and put these files in place before configuring (the folder is ignored by Git):

```text
vendor/windivert/include/windivert.h
vendor/windivert/lib/WinDivert.lib
```

After building, copy `WinDivert.dll` and `WinDivert64.sys` next to `PktSwerve.exe`. On macOS no extra files are needed.

### Publishing a release

Set the new version in `CMakeLists.txt`, commit, then push a matching tag:

```bash
git tag v1.0.4
git push origin v1.0.4
```

GitHub Actions builds Windows and macOS and publishes the files, with checksums, as a release. A tag with a suffix such as `v1.0.4-rc1` becomes a pre-release, which the update check ignores.

## Disclaimer

Use PktSwerve only on networks and traffic you are allowed to change. Bypassing filters may be restricted by local law, network policy or a service agreement, and you are responsible for following them.

## License and links

GNU General Public License v3.0, see [LICENSE](LICENSE).

- [GitHub repository](https://github.com/luridlane-dev/PktSwerve)
- [Issue tracker](https://github.com/luridlane-dev/PktSwerve/issues)
- [Project website](https://luridlane.com)
