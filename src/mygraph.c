#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <errno.h>
#include <time.h>
#include "x11_compat.h"

#include "mygraph.h"
#include "menu.h"
#include "neuzeichnen.h"

mywindow *windows = NULL;
int nowindows = -1;

static grstr *debug_graph;

GW_Font* g_font_ui = NULL;
void DrawChangedValues();

void DefineDebugGraph(grstr *graph) { debug_graph = graph; }

void DebugGraph() {
    int i;
    printf("DebugGraph: graph=%p\n", debug_graph);
    printf("AnzNxN_R=%i", debug_graph->anzNxN_R);
    for (i = 0; i < debug_graph->anzNxN_R; i++) printf("%p,", debug_graph->pNxN_R[i]);
    printf("\n");
}

long ind(int x, int y) {
    x = x % 1000; // default size mapping
    if (x < 0) x += 1000;
    y = y % 1000;
    if (y < 0) y += 1000;
    return x * 1000 + 1000;
}

indfn getind() { return ind; }

void DrawGraphs() {
    int i;
    for (i = 0; i < nowindows + 1; i++) {
        if (windows[i].installed && (windows[i].type & zeichentypemask) && windows[i].neu_zeichnenreq) {
            windows[i].neu_zeichnen(NULL, &windows[i], 0, "", 0.0, 0.0, 0, 0, windows[i].menu->graph);
            windows[i].neu_zeichnenreq = 0;
            GW_Present(windows[i].myw);
        }
    }
}

void Events(int newdata) {
    GW_Event ev;
    int i;

    if (nowindows + 1 < 1) {
        printf("mygraph.c:Events():No windows have been defined. Missing one EndMenu() command?\n");
        exit(1);
    }
    if (newdata) {
        for (i = 0; i < nowindows + 1; i++) {
            if ((windows[i].type & zeichentypemask) && (windows[i].installed != 0)) {
                windows[i].neu_zeichnenreq = 1;
                definerequests(windows[i].menu->graph, windows[i].neu_zeichnentype, windows[i].menu->zd);
            }
        }
    }

    while (GW_PollEvent(&ev)) {
        if (ev.type == GW_EVENT_QUIT) {
            exit(0);
        }
        else if (ev.type == GW_EVENT_WINDOW_EXPOSE) {
            for (i = 0; i < nowindows + 1; i++) {
                if (windows[i].installed && windows[i].myw == ev.window) {
                    windows[i].neu_zeichnenreq = 1;
                    definerequests(windows[i].menu->graph, windows[i].neu_zeichnentype, windows[i].menu->zd);
                    break;
                }
            }
        }
        else if (ev.type == GW_EVENT_WINDOW_RESIZE) {
            for (i = 0; i < nowindows + 1; i++) {
                if (windows[i].installed && windows[i].myw == ev.window) {
                    int w = ev.resize.width;
                    int h = ev.resize.height;
                    if (windows[i].type & menutypemask) {
                        windows[i].menumaxy = h;
                        windows[i].menumaxx = windows[i].MenuRatio * w;
                        if (windows[i].menumaxx < 120) windows[i].menumaxx = 120;
                    } else {
                        windows[i].menumaxx = 0;
                    }
                    windows[i].zeichenmaxx = w - windows[i].menumaxx;
                    windows[i].zeichenmaxy = h;
                    windows[i].newdraw = 1;
                    windows[i].neu_zeichnenreq = 1;
                    definerequests(windows[i].menu->graph, windows[i].neu_zeichnentype, windows[i].menu->zd);
                    break;
                }
            }
        }
        else if (ev.type == GW_EVENT_MOUSE_BUTTON_DOWN) {
            int mx = ev.mouse_button.x;
            int my = ev.mouse_button.y;
            int button = ev.mouse_button.button;
            int Shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
            int Ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;

            for (i = 0; i < nowindows + 1; i++) {
                if (windows[i].installed && windows[i].myw == ev.window) {
                    if (mx < windows[i].menumaxx) {
                        int j = my * windows[i].nomenu / windows[i].menumaxy;
                        if (j >= 0 && j < windows[i].nomenu) {
                            windows[i].menuaction(&windows[i], j, 2 * mx / windows[i].menumaxx, button, Shift, Ctrl);
                        }
                    }
                    break;
                }
            }
        }
    }

    DrawChangedValues();
}

void DrawChangedValues() {
  menudat     *md;
  char text[1000];
  int i, j;

  for (i = 0; i < nowindows + 1; i++) {
    if (windows[i].installed) {
      md = windows[i].menu;
      for (j = 0; j < windows[i].nomenu; j++) {
        switch (md->mydata[j].md.mytype) {
        case int_:
          sprintf(text, "%s = %i",
                  md->mydata[j].md.name,
                  *(md->mydata[j].md.choice.myint.i));
          break;
        case long_:  
          sprintf(text, "%s = %li",
                  md->mydata[j].md.name,
                  *md->mydata[j].md.choice.mylong.i);
          break;
        case double_:
          sprintf(text, "%s = %g",
                  md->mydata[j].md.name,
                  *md->mydata[j].md.choice.mydouble.d);
          break; 
        case int_arr:
          sprintf(text, "%s[%i]=%i", md->mydata[j].md.name,
                  md->mydata[j].md.choice.myint_arr.no,
                  md->mydata[j].md.choice.myint_arr.i[md->mydata[j].md.choice.myint_arr.no]);
          break;
        case int_arrp:
          sprintf(text, "%s[%i]{%i}=%i", md->mydata[j].md.name,
                  md->mydata[j].md.choice.myint_arrp.no,
                  *md->mydata[j].md.choice.myint_arrp.max,
                  md->mydata[j].md.choice.myint_arrp.i[md->mydata[j].md.choice.myint_arrp.no]);
          break;
        case double_arr:
          sprintf(text, "%s[%i]=%10.5g", md->mydata[j].md.name,
                  md->mydata[j].md.choice.mydouble_arr.no,
                  md->mydata[j].md.choice.mydouble_arr.d[md->mydata[j].md.choice.mydouble_arr.no]);
          break;
        case double_arrp:
          if (*md->mydata[j].md.choice.mydouble_arrp.max > 0)
              sprintf(text, "%s[%i]{%i}=%10.5g", md->mydata[j].md.name,
                      md->mydata[j].md.choice.mydouble_arrp.no,
                      *md->mydata[j].md.choice.mydouble_arrp.max,
                      (*md->mydata[j].md.choice.mydouble_arrp.d)[md->mydata[j].md.choice.mydouble_arrp.no]);
          else 
              sprintf(text, "%s[%i]{%i}=N/A", md->mydata[j].md.name,
                      md->mydata[j].md.choice.mydouble_arrp.no,
                      *md->mydata[j].md.choice.mydouble_arrp.max);
          break;
        case boolean_:
          if (*md->mydata[j].md.choice.myint.i != 0)
            sprintf(text, "%s on", md->mydata[j].md.name);
          else
            sprintf(text, "%s off", md->mydata[j].md.name);
          break;
        default: 
          sprintf(text, "%s", windows[i].menutext[j]);
        }
        if (0 != strcmp(windows[i].menutext[j], text)) {
          sprintf(windows[i].menutext[j], "%s", text);
          zeichne_menutext(NULL, windows[i].menuw[j], NULL, NULL, windows[i].menutext[j]);
        }
      }
    }
  }
}

extern void initfonts(Display **myd);

void InitGraphics() {
    fprintf(stderr,
            "Graph library version 0.9, Copyright (C) 1992-2009 Alexander Wagner and Johannes Schlosser\n"
            "Ported to LibGW by Antigravity.\n\n");

    fprintf(stderr, "InitGraphics: Loading font...\n");
    if (!g_font_ui) {
        g_font_ui = GW_LoadFont("C:\\Windows\\Fonts\\segoeui.ttf", 14.0f);
        if (!g_font_ui) {
            fprintf(stderr, "InitGraphics: Failed segoeui.ttf, trying arial.ttf...\n");
            g_font_ui = GW_LoadFont("C:\\Windows\\Fonts\\arial.ttf", 14.0f);
        }
    }
    fprintf(stderr, "InitGraphics: g_font_ui = %p\n", g_font_ui);

    fprintf(stderr, "InitGraphics: Defining window info...\n");
    DefineWindowInformation(&windows, &nowindows);
    
    fprintf(stderr, "InitGraphics: Setting GW functions...\n");
    setGW();

    fprintf(stderr, "InitGraphics: Initializing compatibility fonts...\n");
    Display* dummy_display = NULL;
    initfonts(&dummy_display);
    fprintf(stderr, "InitGraphics: initfonts finished.\n");
}

void CloseGraphics() {
    int i;
    for (i = 0; i < nowindows + 1; i++) {
        if (windows[i].installed) {
            GW_DestroyWindow(windows[i].myw);
            windows[i].installed = 0;
        }
    }
    if (g_font_ui) {
        GW_FreeFont(g_font_ui);
        g_font_ui = NULL;
    }
}
