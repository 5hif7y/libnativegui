cmake_minimum_required(VERSION 3.12)
project(libnativegui C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

# Common library source files
set(WAGNER_SOURCES
    analyse.c
    color.c
    contour.c
    cobject3d.c
    draw2dgraph.c
    draw3dcontour.c
    draw3dgraph.c
    fftn.c
    freedraw.c
    graph.c
    koord.c
    koord2d.c
    koord3d.c
    menu.c
    mydraw.c
    mydraw_2d.c
    mygraph.c
    myrand.c
    neuzeichnen.c
    objects3d.c
    tfield.c
    ufield.c
    vector.c
    view3d.c
)

# Platform-dependent source files and libraries
if(WIN32)
    set(PLATFORM_SOURCES
        gw_win32.c
        gw_draw.c
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
        gw_x11.c
        gw_draw.c
    )
    find_package(X11 REQUIRED)
    set(PLATFORM_LIBS
        ${X11_LIBRARIES}
        m # Math library for Unix-like OS
    )
    include_directories(${X11_INCLUDE_DIR})
endif()

# Define the static library target
add_library(nativegui STATIC ${WAGNER_SOURCES} ${PLATFORM_SOURCES})

# Include directories
target_include_directories(nativegui PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

# Link libraries
target_link_libraries(nativegui PUBLIC ${PLATFORM_LIBS})
