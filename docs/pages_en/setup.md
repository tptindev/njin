# Environment setup {#setup}

This page walks you through installing what you need to build njin on each operating system. When you are done you can run
`njin_pong`, and continue with @ref getting_started.

## What you need

| What you need | What it is for | Requirement |
|---|---|---|
| A C++20 compiler | building the engine and games | GCC 15.2 has been tested; older versions have not |
| **CMake** | configuring the build | 3.28 or newer |
| **Ninja** | running the build (the presets use it) | any version |
| **Git** | CMake downloads raylib 6.0, EnTT v4.0.0 and Dear ImGui during the first configure | any version |
| An internet connection | for the first configure (not needed again after that) | |

The compiler, CMake and Ninja must run from a terminal: typing the command name and getting a version means it is right.

## Status of each platform

This page is honest about what has been tested:

| Platform | Compiler | Status |
|---|---|---|
| Windows 10/11 | GCC (w64devkit) | **Used to develop njin and to try out the steps below** |
| Windows 10/11 | MSVC (Visual Studio) | In CI (`.github/workflows/build.yml`), not tried by hand |
| Linux (Ubuntu) | GCC | **Tested** on Ubuntu 26.04 (WSL2): a fresh clone, the steps below, every target builds, Pong runs |
| Linux (Debian) | GCC | In CI, not tried by hand |
| Linux (Fedora, Arch) | GCC | Not tested. Package names follow the raylib build guide |
| macOS | Clang (Xcode) | Not tested |

If you hit an error on a "not tested" platform, it may be a real bug in njin and not your fault: please report it in the repo's
Issues section.

## Windows with GCC (w64devkit)

This is njin's development environment.

1. **Compiler**: install **w64devkit** (GCC for Windows, no complicated installation). Download it from
   <https://github.com/skeeto/w64devkit/releases>, and extract it to a folder with no spaces or Vietnamese
   characters (for example `C:\Dev\w64devkit`). The build bundled inside the raylib installer also works: the one that was tested
   is `C:\Dev\raylib\w64devkit` with GCC 15.2.
2. **CMake, Ninja and Git** with winget (available on Windows 11 and recent Windows 10 versions):

   ```
   winget install Kitware.CMake
   winget install Ninja-build.Ninja
   winget install Git.Git
   ```

   You can skip CMake or Ninja if the w64devkit `bin` folder you downloaded already has them. Check in step 4.
3. **Add to PATH**: add w64devkit's `bin` folder (for example `C:\Dev\w64devkit\bin`) to Windows's `Path` environment
   variable (Settings, search for "environment variables", edit `Path`). Open a **new** terminal to pick up the
   change.
4. **Check**: in the new terminal:

   ```
   g++ --version
   cmake --version
   ninja --version
   git --version
   ```

   All four commands must print a version, and CMake must be 3.28 or newer. Tested with GCC 15.2, CMake 4.0 and
   Ninja 1.13.
5. **Build and run** (see @ref setup_first_build below).

On Windows there are also two scripts in the repo root: `build.bat` configures and builds Debug, `run.bat` builds and runs
`njin_sandbox`. See @ref getting_started.

## Windows with MSVC (Visual Studio)

@warning This route is in CI but has not been tried by hand. If you stumble, report the error in Issues.

1. Install **Visual Studio 2022** (the free Community edition) or **Build Tools for Visual Studio 2022**, and choose the
   **Desktop development with C++** workload.
2. Install CMake and Git as above: `winget install Kitware.CMake` and `winget install Git.Git`. Ninja is not needed.
3. In the repo folder:

   ```
   cmake -S . -B build -A x64
   cmake --build build --config Release --parallel
   ```

   These are exactly the two commands CI runs. Do not use `cmake --preset`, because the presets choose Ninja.
4. The program is in `build\bin\Release\`, for example `build\bin\Release\njin_pong.exe`.

## Linux

@note Tested on **Ubuntu 26.04 running in WSL2** with GCC 15.2, CMake 4.2.3 and Ninja 1.13.2: installing exactly the Ubuntu
package list below on a fresh install, cloning the repo, `cmake --preset debug`, building `njin_pong` (then building every target,
including njin_inspector and the sample games) all succeeded, and Pong opened a window. That window is drawn by Mesa's
software renderer (`llvmpipe`) through WSLg, so it has not been tested with a real GPU on Linux. Debian, Fedora and Arch have not been tested.

raylib draws with OpenGL on X11 so it needs their development libraries.

**Ubuntu and Debian** (Ubuntu tested, Debian not):

```
sudo apt update
sudo apt install build-essential cmake ninja-build git pkg-config \
  libgl1-mesa-dev libx11-dev libxrandr-dev libxi-dev libxcursor-dev libxinerama-dev
```

**Fedora** (not tested):

```
sudo dnf install gcc-c++ cmake ninja-build git mesa-libGL-devel libX11-devel \
  libXrandr-devel libXi-devel libXcursor-devel libXinerama-devel alsa-lib-devel
```

**Arch** (not tested):

```
sudo pacman -S --needed base-devel cmake ninja git mesa libx11 libxrandr libxi libxcursor libxinerama alsa-lib
```

Check with `g++ --version`, `cmake --version` (3.28 or newer), `ninja --version`. An old distro may have CMake below
3.28: in that case install a newer version from the CMake website, or `pip install cmake`.

Then build and run as below. On Linux the executable has no `.exe` extension: `build/bin/njin_pong`.

## macOS

@warning Not tested. These are the standard steps to build a raylib project, not verified with njin.

1. Install Xcode's command line tools: `xcode-select --install`.
2. Install [Homebrew](https://brew.sh), then: `brew install cmake ninja git`.
3. Build and run as below.

njin's source code has separate branches for POSIX (the inspector's networking, reading file times), but these branches have
never been compiled on macOS.

## First build and run {#setup_first_build}

From the repo root, once everything is installed:

```
git clone https://github.com/tptindev/njin.git
cd njin
cmake --preset debug
cmake --build --preset debug --target njin_pong
```

The first configure downloads raylib, EnTT and Dear ImGui from GitHub so it takes a while and needs the internet; later ones
do not. Then run:

| Operating system | Command |
|---|---|
| Windows (GCC) | `build\bin\njin_pong.exe` |
| Windows (MSVC) | `build\bin\Release\njin_pong.exe` |
| Linux, macOS | `build/bin/njin_pong` |

@image html pong_menu.png "The njin_pong menu window: if you see it, your environment is complete"

A Pong window opening means your environment is complete. Drop `--target njin_pong` to build everything (the sample games and
njin_inspector). The `release` preset builds into `build-release/` with optimizations, see @ref getting_started.

## Running into errors

| Symptom | Common cause | Fix |
|---|---|---|
| `cmake: command not found` or `'cmake' is not recognized` | CMake is not installed, or the terminal was opened before you edited `Path` | Install CMake, open a new terminal |
| CMake says it cannot find `Ninja` | Ninja is not installed or not in `Path` | Install Ninja, open a new terminal, check `ninja --version` |
| Configure stops at "Cloning into 'raylib-src'" or reports a git error | Git is missing, no internet, or the network blocks GitHub | Check `git --version` and that you can reach `github.com` |
| CMake says it needs version 3.28 | An old CMake | Install a newer CMake |
| The linker cannot write `njin_*.exe` (Windows) | The game is still running | Close the game and build again |
| The game says it cannot find `assets/...` | Run from the wrong folder | Games with assets run from their own folder `build/bin/<game name>/`, see @ref samples |
| Linux: missing `X11/Xlib.h`, `GL/gl.h` | Missing development libraries | Install the packages in the Linux section |
| You changed the compiler but CMake still uses the old one | CMake remembers the compiler in `build/` | Delete the `build/` folder and configure again |

## Next steps

- @ref learn : if you are not yet comfortable with C, C++, CMake or shaders, 13 lessons from the beginning
- @ref getting_started : your first program
- @ref first_jump and @ref first_walk : a running character in 50 lines
