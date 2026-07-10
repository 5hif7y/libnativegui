# MSVC NMake Makefile for LibGW-Graph consolidated library

CC = cl
CFLAGS = /nologo /O2 /I. /D_CRT_SECURE_NO_WARNINGS /D_USE_MATH_DEFINES /D_WIN32 /EHsc
LIB_TOOL = lib

OBJS = \
    gw_win32.obj \
    gw_draw.obj \
    analyse.obj \
    color.obj \
    contour.obj \
    cobject3d.obj \
    draw2dgraph.obj \
    draw3dcontour.obj \
    draw3dgraph.obj \
    fftn.obj \
    freedraw.obj \
    graph.obj \
    koord.obj \
    koord2d.obj \
    koord3d.obj \
    menu.obj \
    mydraw.obj \
    mydraw_2d.obj \
    mygraph.obj \
    myrand.obj \
    neuzeichnen.obj \
    objects3d.obj \
    tfield.obj \
    ufield.obj \
    vector.obj \
    view3d.obj

all: libnativegui.lib

libnativegui.lib: $(OBJS)
    $(LIB_TOOL) /nologo /out:libnativegui.lib $(OBJS)

.c.obj:
    $(CC) $(CFLAGS) /c $<

clean:
    del /Q /F *.obj libnativegui.lib
