# libnativegui

`libnativegui` is a consolidated C library for cross-platform desktop windowing, graphics, and scientific plotting. It is a unified library that brings together two core components:

1. **LibGW**: A lightweight native OS unifier layer. On Windows, it handles standard window registration, message processing, and double-buffered software rasterization blitted to GDI.
2. **libgraph (Wagner)**: A ported low-level scientific plotting library (originally for X11/Linux) designed by Alexander Wagner for 2D/3D charts, density maps, contour plots, and interactive GUI menus.

By mapping standard X11 drawing concepts to LibGW (via `x11_compat.h`), `libnativegui` allows academic and low-level physics simulations to run natively on Windows without changing their core drawing logic.

## Project Structure

* **Core Windowing & Drawing (LibGW)**:
  * `gw.h` / `gw_internal.h`: Core LibGW API declarations.
  * `gw_win32.c` / `gw_x11.c`: Win32 (Windows) and X11 (Unix-like) windowing and event queues.
  * `gw_draw.c`: High-performance software rasterization (lines, circles, text, SVGs).
* **Plotting & GUI (Wagner)**:
  * `mygraph.c` / `mygraph.h` / `menu.c` / `menu.h`: Scientific plotting and menus.
  * `analyse.c`, `contour.c`, `draw2dgraph.c`, `draw3dgraph.c`, `view3d.c`: Numerical analysis and 2D/3D projection logic.
  * `x11_compat.h`: Translation layer mapping X11 types (`Display`, `Window`, `GC`) to LibGW.
* **Third-Party Integrations**:
  * `stb_truetype.h` / `stb_image.h`: Public domain font and image loading.
  * `nanosvg.h` / `nanosvgrast.h`: SVG parsing and rendering.
  * `dirent.h`: Toni Ronkko's POSIX compatibility headers for directory scanning under MSVC.

## Building the Library (CMake)

This library supports building with **CMake** on both Windows (MSVC, MinGW, Clang) and UNIX-like systems (Linux, macOS, BSD).

### Building on Windows/Unix:

1. Create a build directory:
   ```bash
   mkdir build
   cd build
   ```
2. Run CMake configuration:
   ```bash
   cmake ..
   ```
3. Build the library:
   ```bash
   cmake --build .
   ```

On Windows, this generates `libnativegui.lib` (or `libnativegui.a` for MinGW). On Unix-like systems, this searches for the native X11 development headers and builds `libnativegui.a`.

### Alternative Building on Windows (MSVC NMake)

You can also compile directly using MSVC NMake:
1. Open a Visual Studio Developer Command Prompt.
2. Run:
   ```cmd
   nmake
   ```
   This generates the static library `libnativegui.lib`.

## Linking with a Client Application

To use this library in your own C application:

1. `#include "mygraph.h"` in your C source files.
2. Link your application with the compiled static library and the required platform libraries:
   * **Windows**: `user32.lib`, `gdi32.lib`, `comdlg32.lib`, `shell32.lib`.
   * **Unix**: `-lX11`, `-lm`.

Example compilation command on Windows (MSVC):
```cmd
cl test.c libnativegui.lib user32.lib gdi32.lib comdlg32.lib shell32.lib /I. /EHsc
```
