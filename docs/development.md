# Development and running ShareKeys

ShareKeys is a C++17 / Qt Widgets desktop application. Run the same executable
on every computer and select **Host** or **Client**. Live input sharing requires
Windows 10/11 and an unlocked interactive desktop.

## Build on Windows

Install CMake 3.21+, Visual Studio 2022 with Desktop development with C++, and
Qt 6 for MSVC 2022 x64. Qt 5.15 is also supported with a matching compiler.
Use a Developer PowerShell terminal:

```powershell
cmake -S . -B build -A x64 -DCMAKE_PREFIX_PATH="C:/Qt/<version>/msvc2022_64"
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
& "C:/Qt/<version>/msvc2022_64/bin/windeployqt.exe" --release .\build\Release\sharekeys.exe
.\build\Release\sharekeys.exe
```

Replace the Qt path with the installed version. `windeployqt` copies the required
Qt DLLs and platform plugin beside the executable. Allow ShareKeys through Windows
Firewall on the private LAN. The default TCP port is **24820**; an inbound rule is
needed on the host, not on clients.

For a portable folder, run `scripts/package-windows.ps1` from that same terminal.
The script builds, tests, installs into `out/ShareKeys`, and runs `windeployqt`.
It does not publish anything or change firewall settings.

## Connect computers

1. On the computer with the physical keyboard and mouse, select **Host**. Keep
   the listen address `0.0.0.0` for all IPv4 interfaces, or specify its LAN address.
2. Start the host and copy its generated connection key.
3. On each other computer, select **Client**, enter a unique name, the host's
   LAN address, the same port, and the copied key. Click **Connect to host**.
4. The host lists authenticated clients. Choose a row and click **Control
   selected computer**, or use the switching hotkey.
5. In **Monitor layout**, adjust X/Y origins and apply. Touching monitor edges
   allow switching. Dimensions use native pixels, independent of Windows scaling.
   A default layout places client desktops to the right of the host.
6. Select clients in the **Mirror** column, then start keyboard mirroring.
   The host receives the same keys. Mouse movement, buttons, and scrolling remain
   local to the host. End mirroring to return to individual host control.

| Shortcut | Action |
|---|---|
| Ctrl+Alt+Home | Return control to the host and end mirroring |
| Ctrl+Alt+Right | Cycle host and connected clients in individual mode |
| Ctrl+Alt+M | Toggle keyboard mirror using selected participants |

Hotkeys are reserved while host sharing is running. Startup fails clearly if
another application already registered one. Change the edge-switch setting while
stopped. Connection settings, participants, and layout are saved through QSettings;
the connection key is **not saved**. A new host launch generates a new key.
Reconnect is explicit after failures so input does not resume unexpectedly.

## Linux development and interface demo

Linux builds the core, networking, tests, and GUI, but cannot start live capture
or injection. It is a development environment, not a supported KVM endpoint.

```sh
cmake -S . -B build -G Ninja
cmake --build build
ctest --test-dir build --output-on-failure
./build/sharekeys --demo
```

`--demo` uses labeled sample computers without opening sockets or capturing input.
`--smoke-test` opens and closes the window, optionally with `--screenshot path.png`.
For headless UI checks, set `QT_QPA_PLATFORM=offscreen`.

Core-only builds need no Qt:

```sh
cmake -S . -B build-core -DSHAREKEYS_BUILD_UI=OFF
cmake --build build-core
ctest --test-dir build-core --output-on-failure
```

## Source layout

```text
src/core/              Events, routing, held-input tracking, monitor geometry
src/platform/windows/  Windows hooks, raw mouse input, injection, monitor discovery
src/network/           Framing, authenticated sessions, heartbeats, ordered delivery
src/app/               Host/client orchestration and recovery
src/ui/                Qt configuration, status, monitor layout, activity view
tests/                 Core, geometry, protocol, and loopback integration checks
docs/                  Architecture, protocol, development, Windows acceptance
third_party/deskflow/   Adapted mouse-button encoder, provenance, licenses
scripts/               Windows packaging
.github/workflows/     Linux and Windows build/test jobs
```

The build tests use fake input backends for network integration; they do not type
into applications. Native Windows capture/injection must also pass the manual
checks in [windows-validation.md](windows-validation.md).
