# Zen Apps desktop suite

This directory contains six standalone desktop applications:

- `zen-notes`
- `zen-tasks`
- `zen-calendar`
- `zen-clock`
- `zen-calculator`
- `zen-reminders`

They are normal Qt 6/C++ desktop binaries. They do not render a fake operating-system shell and do not require ZenovOS to boot. Every application opens in its own native window and can be installed independently from the in-kernel ZenovOS ports.

The applications intentionally show no product version in their windows. Package managers still receive a technical build revision because reproducible installation, upgrades and rollback require one.

## Shared local workspace

All six applications use the same SQLite database:

```text
$XDG_DATA_HOME/ZenApps/workspace.sqlite
```

Set `ZEN_APPS_DATA_DIR` or pass `--data-dir PATH` to use another workspace. The data is local-first and inspectable with ordinary SQLite tools.

Cross-application behavior is implemented in the shared data model:

- a note can create a linked task or reminder;
- open tasks and due reminders appear in Calendar;
- a task can create a 30-minute Calendar block;
- completed focus sessions from Clock are written to the local timeline;
- Calculator results can be pinned directly into Notes.

## Build on Arch Linux

Install the official dependencies:

```bash
sudo pacman -S --needed base-devel cmake ninja qt6-base
```

Build, test and install:

```bash
cmake -S desktop -B build/desktop -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_INSTALL_PREFIX="$HOME/.local" \
  -DBUILD_TESTING=ON
cmake --build build/desktop
QT_QPA_PLATFORM=offscreen ctest --test-dir build/desktop --output-on-failure
cmake --install build/desktop
```

The desktop launchers are installed into `~/.local/share/applications` and the binaries into `~/.local/bin`.

A source `PKGBUILD` is available at `desktop/packaging/arch/PKGBUILD`.

## Linux distribution package

Create a relocatable archive with CPack:

```bash
cmake --build build/desktop --target package
```

This produces `.tar.gz` and `.zip` installation archives. A distro may package the same CMake install tree into its native format.

## Windows and macOS

The project uses the same CMake source tree on all three desktop platforms. CI builds every executable on Linux, Windows and macOS, runs the shared model tests, and performs a smoke launch of all six windows.

## Test and screenshot modes

Every executable supports:

```text
--smoke-test
--data-dir PATH
--screenshot OUTPUT.png
```

`--screenshot` captures only the application window. It does not include or imitate a desktop shell.
