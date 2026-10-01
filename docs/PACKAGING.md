# Fake2D Packaging Notes

Cross-platform build and distribution checklist. Fake2D is a static C++23
library (`libfake2d.a`) plus the `fake2d_hello` sample executable; GLFW is
fetched at configure time by CPM, and OpenGL is the only system GPU API.

## Common: build configurations

```bash
# Full engine with FakeLua scripting (default)
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel

# Engine-only skeleton (no FakeLua dependency; what CI packages)
cmake -S . -B build -DFAKE2D_WITH_FAKELUA=OFF

# Headless verification on any platform
./build/bin/fake2d_hello --headless --frames 60
./build/bin/fake2d_hello --headless --bench
```

Runtime working directory matters for the sample: it loads `scripts/` and
writes `assets/` relative to CWD. Ship the `scripts/` folder next to the
binary (the CMake build copies it into the binary output directory already).

## Linux

Dependencies (Debian/Ubuntu):

```bash
sudo apt install build-essential cmake ninja-build pkg-config \
  libgl1-mesa-dev libx11-dev libxrandr-dev libxi-dev \
  libxcursor-dev libxinerama-dev libwayland-dev libxkbcommon-dev
```

- Audio uses miniaudio's PulseAudio/PipeWire/ALSA backends and only needs
  `libpthread`/`libdl` (linked automatically).
- Headless/CI: run under `xvfb-run` with `LIBGL_ALWAYS_SOFTWARE=1` (see
  `.github/workflows/build.yml`).
- Packaging a binary tarball: `build/bin/fake2d_hello` + `scripts/`;
  glibc determines the minimum distro — build on the oldest distro you
  support (or use a manylinux-style container).

## Windows

- Toolchain: Visual Studio 2022 (MSVC 19.3x supports the required C++23
  features) or recent clang-cl; Windows 10 SDK provides `OpenGL32.lib`.
- Configure from a "Developer Command Prompt":

```bat
cmake -S . -B build -G "Visual Studio 17 2022" -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
```

- GLFW and miniaudio need no extra system packages (WASAPI backend,
  built-in). CPM downloads GLFW on first configure — keep network access or
  pre-populate the CPM cache for offline builds.
- Distribute `fake2d_hello.exe` together with the `scripts\` folder; the
  executable links the static runtime libraries dynamically by default
  (`/MD`), so target machines need the matching Visual C++ Redistributable.

## macOS

- Toolchain: Xcode command line tools (`xcode-select --install`), Apple
  Clang 15+; Homebrew cmake. OpenGL 3.3 Core works on macOS 10.11+ (deprecated
  since 10.14 but fully functional).
- The build links `Foundation`, `CoreAudio`, and `AudioToolbox` frameworks
  automatically (miniaudio CoreAudio backend).
- HiDPI: GLFW enables the retina framebuffer by default; the engine reads
  `glfwGetFramebufferSize` + content scale, so windows render at native
  resolution with logical-point game coordinates.
- App bundle layout for distribution:

```
Fake2D.app/Contents/MacOS/fake2d_hello
Fake2D.app/Contents/Resources/scripts/
Fake2D.app/Contents/Info.plist
```

Ad-hoc signing is enough for local use (`codesign --force --sign -`); notarize
with a Developer ID for public distribution.

## Versioning

Semantic Versioning: `MAJOR.MINOR.PATCH` kept in sync between
`CMakeLists.txt(project VERSION)` and `include/fake2d/version.h`. Pushing a
`vMAJOR.MINOR.PATCH` tag triggers `.github/workflows/release.yml`, which
builds the engine-only (FAKE2D_WITH_FAKELUA=OFF) binaries on Linux, Windows,
and macOS and attaches them to a GitHub Release draft.
