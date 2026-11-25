cmake_minimum_required(VERSION 3.12)
# MSVC: NATIVEGUI_STATIC_CRT=ON builds against the static C runtime (/MT),
# which is what Odin (and other static-CRT callers) link; default is CMake's
# dynamic /MD. Needs CMake >= 3.15 (policy CMP0091).
if(POLICY CMP0091)
    cmake_policy(SET CMP0091 NEW)
endif()
option(NATIVEGUI_STATIC_CRT "MSVC: link the static C runtime (/MT)" OFF)
# X11: use MessageBox-X11 and sofd (x_fib) for GW_ShowMessageBox and the file
# dialogs instead of the built-in ones (gw_x11_dialogs.c). They are not in
# this repository: link them yourself when ON.
option(NATIVEGUI_X11_DIALOGS "X11: external MessageBox-X11 + sofd dialogs" OFF)
if(NATIVEGUI_STATIC_CRT)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()
project(libnativegui C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

# Common library source files
set(COMMON_SOURCES
    src/antialiasing.c
    src/gw_draw.c
    src/gw_shapes.c
    src/gw_ui.c
)

# Wagner's library source files
set(WAGNER_SOURCES
    src/analyse.c
    src/color.c
    src/contour.c
    src/cobject3d.c
    src/draw2dgraph.c
    src/draw3dcontour.c
    src/draw3dgraph.c
    src/fftn.c
    src/freedraw.c
    src/graph.c
    src/koord.c
    src/koord2d.c
    src/koord3d.c
    src/menu.c
    src/mydraw.c
    src/mydraw_2d.c
    src/mygraph.c
    src/myrand.c
    src/neuzeichnen.c
    src/objects3d.c
    src/tfield.c
    src/ufield.c
    src/vector.c
    src/view3d.c
)

# Platform-dependent source files and libraries
if(WIN32)
    set(PLATFORM_SOURCES
        src/gw_win32.c
        src/gw_win32_ext.c
    )
    set(PLATFORM_LIBS
        user32
        gdi32
        comdlg32
        shell32
    )
    add_compile_definitions(_CRT_SECURE_NO_WARNINGS _USE_MATH_DEFINES _WIN32)
else()
    set(PLATFORM_SOURCES
        src/gw_x11.c
        src/gw_x11_ext.c
        src/gw_x11_dialogs.c
    )
    find_package(X11 REQUIRED)
    set(PLATFORM_LIBS
        ${X11_LIBRARIES}
        m # Math library for Unix-like OS
    )
    include_directories(${X11_INCLUDE_DIR})
    if(NATIVEGUI_X11_DIALOGS)
        add_compile_definitions(GW_X11_EXTERNAL_DIALOGS)
    endif()
endif()

# Define the static library target
add_library(nativegui STATIC ${COMMON_SOURCES} ${WAGNER_SOURCES} ${PLATFORM_SOURCES})

# Include directories
target_include_directories(nativegui PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)

# Link libraries
target_link_libraries(nativegui PUBLIC ${PLATFORM_LIBS})
