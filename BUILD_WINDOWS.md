# Building on Windows (MSYS2 / MinGW)

This is the simplest way to build on Windows — no Visual Studio, no vcpkg. The
project's `CMakeLists.txt` builds unchanged. Requires a GPU with OpenGL 3.3
(any Windows GPU from the last ~decade).

## 1. Install MSYS2

Download and run the installer from <https://www.msys2.org>. When it finishes,
open the **"MSYS2 MinGW 64-bit"** shell from the Start menu — the one labelled
**MINGW64**. Do *not* use the plain "MSYS" shell; the compiler and libraries
below only exist in the MINGW64 environment.

## 2. Install the compiler and dependencies

In the MINGW64 shell:

```bash
pacman -Syu        # update first; if it asks to close the window, reopen
                   # MINGW64 and run it once more
pacman -S --needed \
  mingw-w64-x86_64-gcc \
  mingw-w64-x86_64-cmake \
  mingw-w64-x86_64-ninja \
  mingw-w64-x86_64-pkgconf \
  mingw-w64-x86_64-glfw \
  mingw-w64-x86_64-glew \
  mingw-w64-x86_64-glm \
  mingw-w64-x86_64-libpng
```

## 3. Build

Unpack the project somewhere, then from its root directory:

```bash
cmake -B build -G Ninja
cmake --build build -j
```

This produces `build/minecraft.exe`.

## 4. Run

Run it **from the project root** so it finds the texture pack folder
(`Java Default Textures 1.21.7 - 1.21.8`):

```bash
./build/minecraft.exe
```

Launch it **from the MINGW64 shell**, not by double-clicking in Explorer — the
shell already has the required MinGW DLLs on its PATH. To point at textures in a
different location, pass `--assets "C:/path/to/textures"` (forward or back
slashes both work).

Controls and command-line flags are documented in `README.md`.

## Optional: a standalone build you can double-click / share

To run the `.exe` outside the MINGW64 shell (or hand it to someone without
MSYS2), copy its dependent DLLs next to `minecraft.exe`. List exactly which ones
it needs from the MINGW64 shell:

```bash
ldd build/minecraft.exe | grep mingw64
```

Copy each of those `.dll` files from `C:\msys64\mingw64\bin` into the folder
containing `minecraft.exe` (typically `libglfw-3.dll`, `glew32.dll`,
`libpng16-16.dll`, `zlib1.dll`, and the GCC runtime `libgcc_s_seh-1.dll`,
`libstdc++-6.dll`, `libwinpthread-1.dll`). Then zip that folder **together with
the textures folder** — the game still needs the assets at runtime.
