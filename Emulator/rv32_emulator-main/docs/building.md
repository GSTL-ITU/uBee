# Building from source

There is a download for Linux and one for Windows on the
[latest release](https://github.com/A-Eren/rv32_emulator/releases/latest), and
neither needs a compiler. Build from source when you want to change something,
run the tests, or use a platform the downloads do not cover.

## What it needs

A C++17 compiler, CMake 3.20+ and Ninja. Qt6 is optional: without it everything
still builds, minus the desktop window — `cmake` says which way it went.

```bash
sudo apt install ninja-build qt6-base-dev
```

Qt 6.3 or newer if you want the window. 6.2 — what Ubuntu 22.04 ships — is too
old: the GUI uses `QFormLayout::setRowVisible` and the `QMenu::addAction`
overload taking a `StandardKey`, neither of which exists there.

No other dependencies. The test harness is 150 lines in `tests/rv_test.hpp`,
and there is no external RISC-V toolchain anywhere in this project — it
assembles its own code.

## Build and test

```bash
cmake --preset debug
cmake --build build/debug
ctest --preset debug
```

Three presets:

| Preset | What it is |
|---|---|
| `debug` | Warnings as errors (`RV32_WERROR=ON`) — the default |
| `asan` | The same, plus ASan and UBSan |
| `release` | `RelWithDebInfo`, no test preset of its own |

Both binaries land in `build/<preset>/`: `rv32` for the command line and
`rv32-gui` for the window.

## The Linux AppImage

```bash
./packaging/build-appimage.sh
```

It fetches `linuxdeploy` and its Qt plugin into `build/appimage-tools/` on
first run, needs no root, and leaves `build/rv32-x86_64.AppImage`.

The AppImage carries its Qt inside it, so the Qt it is built against does not
have to be the distribution's — point it elsewhere with `CMAKE_PREFIX_PATH`
(read by CMake) and `QMAKE` (read by the linuxdeploy Qt plugin). CI does exactly
that: it builds on Ubuntu 22.04 for the old glibc, and installs Qt 6.5.3 from
`aqtinstall` rather than using the 6.2 that distribution has.

What an AppImage does *not* carry is glibc, so it runs only on distributions no
older than the one that built it. Building on the oldest runner is what keeps
Debian 12 and RHEL 9 in range.

`./packaging/install.sh <appimage>` then puts it in the applications menu; see
the README for what that touches.

## Windows

Install [MSYS2](https://www.msys2.org/) and, from a **UCRT64** shell:

```bash
pacman -S --needed mingw-w64-ucrt-x86_64-{gcc,cmake,ninja,qt6-base,qt6-tools}
cmake --preset release
cmake --build build/release
ctest --test-dir build/release --output-on-failure
```

`--test-dir` rather than `--preset` because there is no release test preset;
it runs the same tests the Linux job does, the GUI ones included.

UCRT64 rather than MSVC on purpose: `cmake/CompilerWarnings.cmake` is a GCC
warning set, so Windows compiles the same code with the same compiler as Linux
and a warning cannot hide on one platform. UCRT rather than msvcrt because the
C99 `printf` formats have to work.

`src/cmd/tty.cpp` is the only file that asks the operating system anything —
whether a stream is a terminal, and whether it will render colour. On Windows
that second question means asking the console to turn ANSI escapes on, which
conhost does not do by itself.

To hand the result to someone else, `windeployqt` has to put the Qt DLLs and
the `platforms\` plugins next to `rv32-gui.exe`, or it will not start on a
machine without MSYS2. The `windows` job in `.github/workflows/ci.yml` does
this, and then proves it by running the directory with nothing but Windows on
`PATH`.

## What CI builds

`.github/workflows/ci.yml` runs on every push to `main`, every pull request,
and every `v*` tag:

- **`test`** — g++ and clang++ × `debug` and `asan`, four jobs. Both compilers
  because `-Werror` is on and a warning only one of them emits should still
  fail. Each job also runs the example programs, checks the `.mem` export, and
  renders two GUI frames on Qt's offscreen platform.
- **`windows`** — the UCRT64 build, bundled and checked on a clean `PATH`.
- **`appimage`** — the AppImage, checked headlessly and through `install.sh`'s
  menu integration.
- **`release`** — only on a `v*` tag, and only after all three of the above are
  green.

A push to `main` leaves the Windows bundle and the AppImage as build artifacts
on the run, which is how to get a build that has no tag yet. Artifacts expire
and need a GitHub login; a release does not.

## Cutting a release

A release is made by pushing a tag, and by nothing else:

```bash
# bump the version first: project(rv32_emulator VERSION x.y.z) in CMakeLists.txt
git tag -a v1.2.0 -m "rv32 1.2.0"
git push origin v1.2.0
```

The `release` job then attaches `rv32-windows-x64.zip` and
`rv32-x86_64.AppImage` to a GitHub release, with notes generated from the
commits since the last tag. Because it `needs` the three build jobs, a tag on a
red build publishes nothing.

The version is declared **once**, in `project()` in `CMakeLists.txt`, and
reaches both front-ends as the `RV32_VERSION` compile definition — so
`rv32 --version`, `rv32-gui`'s about box and the tag cannot disagree.
