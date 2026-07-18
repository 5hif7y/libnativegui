#include "x11_compat.h"
#include "mydraw_2d.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

static Drawable mydrawable;
static Drawable mydrawabledouble;
static Drawable mydrawto;

static int shape = Nonconvex;
static int mode = CoordModeOrigin;
static int myXactive = 1;
static int myXmode = 0;
static int xsize = 1000;
static int ysize = 1000;
static int Xxsize = 1000;
static int Xysize = 1000;

// PostScript exporter globals
static double PSscale = 0.056;
#define  maxPSxsizeA4 10000.0
#define  maxPSysizeA4 (maxPSxsizeA4*29.5/21) /* DIN A4 */
#define  maxPSxsizeLetter 10278.0
#define  maxPSysizeLetter (maxPSxsizeLetter*11/8.5) /* American Letter */

static int maxPSxsize, maxPSysize, PSlandscape;
static int PSxmin, PSymin, PSxmax, PSymax;
static FILE* PSfile = NULL;
static char* PSfilename = NULL;
static fpos_t BBoxPos;
static double psfontalpha = 0.0;
static int PSxsize = 1000;
static int PSysize = 1000;

#define PIXELS 1024
static unsigned long mypixels[PIXELS];

struct PScolor { double red, green, blue; };
static struct PScolor myPSmap[PIXELS];

char* PSfontname[] = {
    "Times-Roman"
};

int _black = 0;
int _white = 0;
int _red = 0;
int _green = 0;
int _blue = 0;
int _yellow = 0;

int black() { return _black; }
int white() { return _white; }
int red() { return _red; }
int green() { return _green; }
int blue() { return _blue; }
int yellow() { return _yellow; }

void set_black(int f) { _black = f; }
void set_white(int f) { _white = f; }
void set_red(int f) { _red = f; }
void set_green(int f) { _green = f; }
void set_blue(int f) { _blue = f; }
void set_yellow(int f) { _yellow = f; }

// Compatibility mappings for German names
int schwarz() { return black(); }
int weiss() { return white(); }
int rot() { return red(); }
int gruen() { return green(); }
int blau() { return blue(); }
int gelb() { return yellow(); }
void set_schwarz(int f) { set_black(f); }
void set_weiss(int f) { set_white(f); }
void set_rot(int f) { set_red(f); }
void set_gruen(int f) { set_green(f); }
void set_blau(int f) { set_blue(f); }
void set_gelb(int f) { set_yellow(f); }

// Dynamic function pointers
void (*mydrawline)(int color, int x1, int y1, int x2, int y2) = NULL;
void (*mypolygon)(int color, XPoint *points, int n) = NULL;
void (*mypolygon_line)(int color, int color_line, XPoint *points, int n) = NULL;
void (*myline_polygon)(int color_line, XPoint *points, int n) = NULL;
void (*mytriangle_2)(int color, int color_line, XPoint *points) = NULL;
void (*mycircle)(int color, int x, int y, int r) = NULL;
void (*myfilledcircle)(int color, int x, int y, int r) = NULL;
void (*densityfield)(int nox, int noy, double zmin, double zmax,
                     int colmin, int colmax, double *f,
                     int xofs, int yofs, int xsize, int ysize, int hex) = NULL;
void (*twodensityfield)(int nox, int noy,
                        double z1min, double z1max, double z2min, double z2max,
                        int colomin, int colx, int coly, double *f1, double *f2,
                        int xofs, int yofs, int xsize, int ysize) = NULL;

void (*mytext)(int color, int x, int y, const char *text, int orient) = NULL;
void (*myselectfont)(int font, char *testtext, double length) = NULL;
void (*mysetfontdirection)(double alpha) = NULL;
void (*myclear)() = NULL;
void (*myshow)() = NULL;

// Expose active UI font
extern GW_Font* g_font_ui;

// Forward declarations of GW backend functions
void mydrawlineGW(int color, int x1, int y1, int x2, int y2);
void mypolygonGW(int color, XPoint *points, int n);
void mypolygon_lineGW(int color, int color_line, XPoint *points, int n);
void myline_polygonGW(int color_line, XPoint *points, int n);
void mytriangle_2GW(int color, int color_line, XPoint *points);
void mycircleGW(int color, int x, int y, int r);
void myfilledcircleGW(int color, int x, int y, int r);
void densityfieldsqGW(int nox, int noy, double zmin, double zmax, int colmin, int colmax, double *f,
                      int xofs, int yofs, int xsize, int ysize);
void densityfieldGW(int nox, int noy, double zmin, double zmax, int colmin, int colmax, double *f,
                    int xofs, int yofs, int xsize, int ysize, int hex);
void twodensityfieldGW(int nox, int noy, double z1min, double z1max, double z2min, double z2max,
                       int colmin, int colx, int coly, double *f1, double *f2,
                       int xofs, int yofs, int xsize, int ysize);
void mytextGW(int color, int x, int y, const char *text, int orient);
void myselectfontGW(int font, char *testtext, double length);
void mysetfontdirectionGW(double alpha);
void myclearGW();
void myshowGW();

// Forward declarations of PS backend functions
void PSxlimit(int x);
void PSylimit(int y);
void mydrawlinePS(int color, int x1, int y1, int x2, int y2);
void mypolygonPS(int color, XPoint *points, int n);
void mypolygon_linePS(int color, int color_line, XPoint *points, int n);
void myline_polygonPS(int color_line, XPoint *points, int n);
void mytriangle_2PS(int color, int color_line, XPoint *points);
void mycirclePS(int color, int x, int y, int r);
void myfilledcirclePS(int color, int x, int y, int r);
void densityfieldsqPS(int nox, int noy, double zmin, double zmax, int colmin, int colmax, double *f,
                      int xofs, int yofs, int xsize, int ysize);
void densityfieldhexPS(int nox, int noy, double zmin, double zmax, int colmin, int colmax, double *f,
                       int xofs, int yofs, int xsize, int ysize);
void densityfieldPS(int nox, int noy, double zmin, double zmax, int colmin, int colmax, double *f,
                    int xofs, int yofs, int xsize, int ysize, int hex);
void twodensityfieldPS(int nox, int noy, double z1min, double z1max, double z2min, double z2max,
                       int colmin, int colx, int coly, double *f1, double *f2,
                       int xofs, int yofs, int xsize, int ysize);
void mytext_PS(int color, int x, int y, const char *text, int orient);
void myselectfont_PS(int font, char *testtext, double length);
void mysetfontdirection_PS(double alpha);
void myclearPS();
void myshowPS();

// PostScript limit helper
void PSxlimit(int x) {
    if (x < PSxmin) PSxmin = x;
    if (x > PSxmax) PSxmax = x;
}
void PSylimit(int y) {
    if (y < PSymin) PSymin = y;
    if (y > PSymax) PSymax = y;
}

// Helper to draw filled triangle directly onto LibGW backbuffer
void draw_filled_triangle_helper(GW_Window* win, int x0, int y0, int x1, int y1, int x2, int y2, uint32_t color) {
    int w, h;
    uint32_t* pixels = GW_GetBackbuffer(win, &w, &h);
    if (!pixels) return;

    // Sort vertices by y coordinate: y0 <= y1 <= y2
    if (y0 > y1) { int tx = x0; x0 = x1; x1 = tx; int ty = y0; y0 = y1; y1 = ty; }
    if (y0 > y2) { int tx = x0; x0 = x2; x2 = tx; int ty = y0; y0 = y2; y2 = ty; }
    if (y1 > y2) { int tx = x1; x1 = x2; x2 = tx; int ty = y1; y1 = y2; y2 = ty; }

    int total_height = y2 - y0;
    if (total_height == 0) return;

    for (int i = 0; i < total_height; i++) {
        int second_half = i > y1 - y0 || y1 == y0;
        int segment_height = second_half ? y2 - y1 : y1 - y0;
        if (segment_height == 0) continue;
        
        float alpha = (float)i / total_height;
        float beta  = (float)(i - (second_half ? y1 - y0 : 0)) / segment_height;
        
        int ax = x0 + (x2 - x0) * alpha;
        int bx = second_half ? x1 + (x2 - x1) * beta : x0 + (x1 - x0) * beta;
        
        if (ax > bx) { int tx = ax; ax = bx; bx = tx; }
        
        int py = y0 + i;
        if (py < 0 || py >= h) continue;
        
        int start_x = ax;
        int end_x = bx;
        if (start_x < 0) start_x = 0;
        if (end_x >= w) end_x = w - 1;
        
        for (int px = start_x; px <= end_x; px++) {
            pixels[py * w + px] = color;
        }
    }
}

// Pixel plotter helper
void plot_pixels(GW_Window* win, int px, int py, int w, int h, uint32_t* pixels, uint32_t color) {
    if (px >= 0 && px < w && py >= 0 && py < h) {
        pixels[py * w + px] = color;
    }
}

// Helper to draw circle outline
void draw_circle_helper(GW_Window* win, int xc, int yc, int r, uint32_t color) {
    int x = 0;
    int y = r;
    int d = 3 - 2 * r;
    
    int w, h;
    uint32_t* pixels = GW_GetBackbuffer(win, &w, &h);
    if (!pixels) return;
    
    while (y >= x) {
        plot_pixels(win, xc + x, yc + y, w, h, pixels, color);
        plot_pixels(win, xc - x, yc + y, w, h, pixels, color);
        plot_pixels(win, xc + x, yc - y, w, h, pixels, color);
        plot_pixels(win, xc - y, yc - y, w, h, pixels, color);
        plot_pixels(win, xc + y, yc + x, w, h, pixels, color);
        plot_pixels(win, xc - y, yc + x, w, h, pixels, color);
        plot_pixels(win, xc + y, yc - x, w, h, pixels, color);
        plot_pixels(win, xc - y, yc - x, w, h, pixels, color);
        
        x++;
        if (d > 0) {
            y--;
            d = d + 4 * (x - y) + 10;
        } else {
            d = d + 4 * x + 6;
        }
    }
}

// Helper to draw filled circle
void draw_filled_circle_helper(GW_Window* win, int xc, int yc, int r, uint32_t color) {
    int w, h;
    uint32_t* pixels = GW_GetBackbuffer(win, &w, &h);
    if (!pixels) return;
    
    for (int y = -r; y <= r; y++) {
        int py = yc + y;
        if (py < 0 || py >= h) continue;
        int dx = (int)sqrt(r * r - y * y);
        int x1 = xc - dx;
        int x2 = xc + dx;
        if (x1 < 0) x1 = 0;
        if (x2 >= w) x2 = w - 1;
        for (int px = x1; px <= x2; px++) {
            pixels[py * w + px] = color;
        }
    }
}

// LibGW Drawing Implementations
void mydrawlineGW(int color, int x1, int y1, int x2, int y2) {
    GW_DrawLine((GW_Window*)mydrawto, x1, y1, x2, y2, mypixels[color]);
}

void mypolygonGW(int color, XPoint *points, int n) {
    uint32_t c = mypixels[color];
    if (n == 3) {
        draw_filled_triangle_helper((GW_Window*)mydrawto, points[0].x, points[0].y, points[1].x, points[1].y, points[2].x, points[2].y, c);
    } else if (n == 4) {
        draw_filled_triangle_helper((GW_Window*)mydrawto, points[0].x, points[0].y, points[1].x, points[1].y, points[2].x, points[2].y, c);
        draw_filled_triangle_helper((GW_Window*)mydrawto, points[0].x, points[0].y, points[2].x, points[2].y, points[3].x, points[3].y, c);
    }
}

void mypolygon_lineGW(int color, int color_line, XPoint *points, int n) {
    mypolygonGW(color, points, n);
    myline_polygonGW(color_line, points, n);
}

void myline_polygonGW(int color_line, XPoint *points, int n) {
    uint32_t c = mypixels[color_line];
    for (int i = 0; i < n - 1; i++) {
        GW_DrawLine((GW_Window*)mydrawto, points[i].x, points[i].y, points[i+1].x, points[i+1].y, c);
    }
    GW_DrawLine((GW_Window*)mydrawto, points[n-1].x, points[n-1].y, points[0].x, points[0].y, c);
}

void mytriangle_2GW(int color, int color_line, XPoint *points) {
    mypolygonGW(color, points, 3);
    mydrawlineGW(color_line, points[0].x, points[0].y, points[1].x, points[1].y);
    mydrawlineGW(color_line, points[0].x, points[0].y, points[2].x, points[2].y);
}

void mycircleGW(int color, int x, int y, int r) {
    draw_circle_helper((GW_Window*)mydrawto, x, y, r, mypixels[color]);
}

void myfilledcircleGW(int color, int x, int y, int r) {
    draw_filled_circle_helper((GW_Window*)mydrawto, x, y, r, mypixels[color]);
}

void densityfieldsqGW(int nox, int noy, double zmin, double zmax, int colmin, int colmax, double *f,
                      int xofs, int yofs, int xsize, int ysize) {
    if (zmin == zmax) return;
    double dx = 1.0 * xsize / (nox + 1);
    double dy = 1.0 * ysize / (noy + 1);
    yofs += ysize;

    for (int i = 0; i < nox; i++) {
        int x = xofs + dx * (i + 0.5);
        int x1 = xofs + dx * (i + 1.5);
        for (int j = 0; j < noy; j++) {
            int y = yofs - dy * (j + 0.5);
            int y1 = yofs - dy * (j + 1.5);
            
            double d = (f[i * noy + j] - zmin) / (zmax - zmin);
            if (d < 0.0) d = 0.0;
            if (d > 1.0) d = 1.0;
            
            int color_idx = colmin + d * (colmax - colmin);
            GW_FillRect((GW_Window*)mydrawto, x, y1, x1 - x, y - y1, mypixels[color_idx]);
        }
    }
}

void densityfieldGW(int nox, int noy, double zmin, double zmax, int colmin, int colmax, double *f,
                    int xofs, int yofs, int xsize, int ysize, int hex) {
    densityfieldsqGW(nox, noy, zmin, zmax, colmin, colmax, f, xofs, yofs, xsize, ysize);
}

void twodensityfieldGW(int nox, int noy, double z1min, double z1max, double z2min, double z2max,
                       int colmin, int colx, int coly, double *f1, double *f2,
                       int xofs, int yofs, int xsize, int ysize) {
    if (z1min == z1max || z2min == z2max) return;
    double dx = 1.0 * xsize / (nox + 1);
    double dy = 1.0 * ysize / (noy + 1);
    yofs += ysize;

    for (int i = 0; i < nox; i++) {
        int x = xofs + dx * (i + 0.5);
        int x1 = xofs + dx * (i + 1.5);
        for (int j = 0; j < noy; j++) {
            int y = yofs - dy * (j + 0.5);
            int y1 = yofs - dy * (j + 1.5);

            int d1 = colx * (f1[i * noy + j] - z1min) / (z1max - z1min);
            int d2 = coly * (f2[i * noy + j] - z2min) / (z2max - z2min);

            if (d1 < 0) d1 = 0;
            if (d1 > colx - 1) d1 = colx - 1;
            if (d2 < 0) d2 = 0;
            if (d2 > coly - 1) d2 = coly - 1;

            int color_idx = colmin + d1 * coly + d2;
            GW_FillRect((GW_Window*)mydrawto, x, y1, x1 - x, y - y1, mypixels[color_idx]);
        }
    }
}

void mytextGW(int color, int x, int y, const char *text, int orient) {
    if (!text || text[0] == '\0') return;
    
    wchar_t wtext[512] = {0};
    GW_UTF8ToWide(text, wtext, 512);

    int tw = 0, th = 0;
    GW_MeasureText(g_font_ui, wtext, &tw, &th);

    int draw_x = x;
    int draw_y = y - th; // Shift up because LibGW draws from the top-left, and X11 draws from the baseline
    
    if (orient == 1) {
        draw_x = x - tw / 2;
    } else if (orient == 2) {
        draw_x = x - tw;
    }

    GW_DrawText((GW_Window*)mydrawto, g_font_ui, draw_x, draw_y, wtext, mypixels[color]);
}

void myselectfontGW(int font, char *testtext, double length) {
    // We use the scalable Segoe UI/Consolas g_font_ui directly
}

void mysetfontdirectionGW(double alpha) {
    // Direction not supported in standard rasterized text
}

void myclearGW() {
    GW_Clear((GW_Window*)mydrawto, mypixels[0]);
}

void myshowGW() {
    GW_Present((GW_Window*)mydrawto);
}

void setdisplay(Display *md) {
    // Dummy
}

void setXwindow(Display *md, Drawable mydr, Drawable mydoubledr, GC myGC, int mode, int _xsize, int _ysize) {
    Xxsize = _xsize;
    Xysize = _ysize;
    xsize = Xxsize;
    ysize = Xysize;
    mydrawable = mydr;
    mydrawabledouble = mydoubledr;
    mydrawto = mydrawable;
    if (mode == doublebufferX) mydrawto = mydrawabledouble;
    myXmode = mode;
}

void initX(Display *md, Drawable mydr, Drawable mydoubledr, GC myGC, int mode, int _xsize, int _ysize) {
    setXwindow(md, mydr, mydoubledr, myGC, mode, _xsize, _ysize);
    setGW();
}

void setGW() {
    xsize = Xxsize;
    ysize = Xysize;
    mydrawline = mydrawlineGW;
    mypolygon = mypolygonGW;
    mypolygon_line = mypolygon_lineGW;
    myline_polygon = myline_polygonGW;
    mytriangle_2 = mytriangle_2GW;
    mycircle = mycircleGW;
    myfilledcircle = myfilledcircleGW;
    densityfield = densityfieldGW;
    twodensityfield = twodensityfieldGW;
    mytext = mytextGW;
    myselectfont = myselectfontGW;
    mysetfontdirection = mysetfontdirectionGW;
    myclear = myclearGW;
    myshow = myshowGW;
    myXactive = 1;
}

void setX() {
    setGW();
}

int ColorRampStart() { return 0; }
int ColorRampEnd() { return 0; }
int NumberOfColors() { return PIXELS; }
void SetColorRampStart(int i) {}
void SetColorRampEnd(int i) {}
void SetColorFieldStart(int i) {}
int ColorFieldStart() { return 0; }
void SetColorFieldX(int i) {}
int ColorFieldX() { return 0; }
void SetColorFieldY(int i) {}
int ColorFieldY() { return 0; }
int GetFreeColors() { return PIXELS; }

void setcolormap(XColor *map, int n) {
    for (int i = 0; i < n; i++) {
        uint8_t r = map[i].red >> 8;
        uint8_t g = map[i].green >> 8;
        uint8_t b = map[i].blue >> 8;
        mypixels[i] = 0xFF000000 | (r << 16) | (g << 8) | b;
        myPSmap[i].red = r / 255.0;
        myPSmap[i].green = g / 255.0;
        myPSmap[i].blue = b / 255.0;
    }
}

// PostScript Exporter Implementations
void myclearPS() {
    rewind(PSfile);
    fprintf(PSfile, "%%!PS-Adobe-3.0 EPSF-3.0\n");
    fprintf(PSfile, "%%%%Creator: automatically produced by mydraw.c (ported to LibGW by Antigravity)\n");
    fgetpos(PSfile, &BBoxPos);
    if (PSlandscape)
        fprintf(PSfile, "%%%%BoundingBox: %07.3f %07.3f %07.3f %07.3f             ",
            0.0, PSscale * (maxPSxsize - xsize), PSscale * ysize, PSscale * maxPSxsize);
    else
        fprintf(PSfile, "%%%%BoundingBox: %07.3f %07.3f %07.3f %07.3f             ",
            0.0, 0.0, PSscale * xsize, PSscale * maxPSysize);
    fprintf(PSfile, "\n/Pol {lineto lineto setrgbcolor fill} def\n");
    fprintf(PSfile, "\n/Tgl {newpath moveto lineto lineto setrgbcolor "
        "gsave fill grestore 0.0 0.0 0.0 setrgbcolor stroke} def\n");
    fprintf(PSfile, "\n/P4 {newpath moveto lineto lineto lineto setrgbcolor "
        "gsave fill grestore setrgbcolor stroke} def\n");
    fprintf(PSfile, "gsave\n");
    if (PSlandscape)
        fprintf(PSfile, "90 rotate\n");
    else
        fprintf(PSfile, "0 %07.3f translate\n", PSscale * maxPSysize);
    fprintf(PSfile, "%7.3f %7.3f scale\n", PSscale, PSscale);
    fprintf(PSfile, "\n");
    fprintf(PSfile, "/rightshow\n"
        "{ dup stringwidth pop\n"
        "  neg 0 rmoveto\n"
        "  show\n"
        "} def\n\n"
        "/centershow\n"
        "{ dup stringwidth pop\n"
        "  -2 div 0 rmoveto\n"
        "  show\n"
        "} def\n\n");

    fprintf(PSfile, "5 setlinewidth\n");
    PSxmin = PSxsize; PSymin = PSysize; PSxmax = 0; PSymax = 0;
}

void myshowPS() {
    int i;
    fprintf(PSfile, "showpage\n");
    fsetpos(PSfile, &BBoxPos);
    if (PSlandscape)
        fprintf(PSfile, "%%%%BoundingBox: %07.3f %07.3f %07.3f %07.3f             ",
            0.0, PSscale * (maxPSxsize - xsize), PSscale * ysize, PSscale * maxPSxsize);
    else
        fprintf(PSfile, "%%%%BoundingBox: %07.3f %07.3f %07.3f %07.3f             ",
            PSscale * PSxmin, PSscale * (maxPSysize - PSymax),
            PSscale * PSxmax, PSscale * (maxPSysize - PSymin));
    i = fclose(PSfile);
    if (i != 0) printf("Could not close PostScript file! error %i\n", i);
    PSfile = NULL;
}

void mydrawlinePS(int color, int x1, int y1, int x2, int y2) {
    PSxlimit(x1);
    PSxlimit(x2);
    PSylimit(y1);
    PSylimit(y2);
    fprintf(PSfile, "newpath\n");
    fprintf(PSfile, "%d %d moveto\n", x1, -y1);
    fprintf(PSfile, "%d %d lineto\n", x2, -y2);
    fprintf(PSfile, "%5.3f %5.3f %5.3f setrgbcolor\n",
        myPSmap[color].red,
        myPSmap[color].green,
        myPSmap[color].blue);
    fprintf(PSfile, "stroke\n");
}

void mycirclePS(int color, int x, int y, int r) {
    PSxlimit(x + r);
    PSxlimit(x - r);
    PSylimit(y + r);
    PSylimit(y - r);
    fprintf(PSfile, "newpath\n");
    fprintf(PSfile, "%5.3f %5.3f %5.3f setrgbcolor\n",
        myPSmap[color].red,
        myPSmap[color].green,
        myPSmap[color].blue);
    fprintf(PSfile, "%d %d %d 0 360 arc\n", x, -y, r);
    fprintf(PSfile, "stroke\n");
}

void myfilledcirclePS(int color, int x, int y, int r) {
    PSxlimit(x + r);
    PSxlimit(x - r);
    PSylimit(y + r);
    PSylimit(y - r);
    fprintf(PSfile, "newpath\n");
    fprintf(PSfile, "%5.3f %5.3f %5.3f setrgbcolor\n",
        myPSmap[color].red,
        myPSmap[color].green,
        myPSmap[color].blue);
    fprintf(PSfile, "%d %d %d 0 360 arc\n", x, -y, r);
    fprintf(PSfile, "fill\n");
}

void mypolygonPS(int color, XPoint *points, int n) {
    int i;
    for (i = 0; i < n; i++) { PSxlimit(points[i].x); PSylimit(points[i].y); }
    fprintf(PSfile, "newpath\n");
    fprintf(PSfile, "%d %d moveto\n", points[0].x, -points[0].y);
    for (i = 1; i < n; i++)
        fprintf(PSfile, "%d %d lineto\n", points[i].x, -points[i].y);
    fprintf(PSfile, "%5.3f %5.3f %5.3f setrgbcolor\n",
        myPSmap[color].red,
        myPSmap[color].green,
        myPSmap[color].blue);
    fprintf(PSfile, "fill\n");
}

void mypolygon_line4PS(int color, int color_line, XPoint *points) {
    int i;
    for (i = 0; i < 4; i++) { PSxlimit(points[i].x); PSylimit(points[i].y); }
    fprintf(PSfile, "%5.3f %5.3f %5.3f %5.3f %5.3f %5.3f"
        " %d %d %d %d %d %d %d %d  P4\n",
        myPSmap[color_line].red,
        myPSmap[color_line].green,
        myPSmap[color_line].blue,
        myPSmap[color].red,
        myPSmap[color].green,
        myPSmap[color].blue,
        points[0].x, -points[0].y,
        points[1].x, -points[1].y,
        points[2].x, -points[2].y,
        points[3].x, -points[3].y);
}

void mypolygon_linePS(int color, int color_line, XPoint *points, int n) {
    int i;
    for (i = 0; i < n; i++) { PSxlimit(points[i].x); PSylimit(points[i].y); }
    if (n == 4)
        mypolygon_line4PS(color, color_line, points);
    else {
        fprintf(PSfile, "newpath\n");
        fprintf(PSfile, "%d %d moveto\n", points[0].x, -points[0].y);
        for (i = 1; i < n; i++)
            fprintf(PSfile, "%d %d lineto\n", points[i].x, -points[i].y);
        fprintf(PSfile, "closepath\n");
        fprintf(PSfile, "gsave\n");
        fprintf(PSfile, "%5.3f %5.3f %5.3f setrgbcolor\n",
            myPSmap[color].red,
            myPSmap[color].green,
            myPSmap[color].blue);
        fprintf(PSfile, "fill\n");
        fprintf(PSfile, "grestore\n");
        fprintf(PSfile, "%5.3f %5.3f %5.3f setrgbcolor\n",
            myPSmap[color_line].red,
            myPSmap[color_line].green,
            myPSmap[color_line].blue);
        fprintf(PSfile, "stroke\n");
    }
}

void myline_polygonPS(int color_line, XPoint *points, int n) {
    int i;
    for (i = 0; i < n; i++) { PSxlimit(points[i].x); PSylimit(points[i].y); }
    fprintf(PSfile, "newpath\n");
    fprintf(PSfile, "%d %d moveto\n", points[0].x, -points[0].y);
    for (i = 1; i < n; i++)
        fprintf(PSfile, "%d %d lineto\n", points[i].x, -points[i].y);
    fprintf(PSfile, "%5.3f %5.3f %5.3f setrgbcolor\n",
        myPSmap[color_line].red,
        myPSmap[color_line].green,
        myPSmap[color_line].blue);
    fprintf(PSfile, "stroke\n");
}

void mytriangle_2PS(int color, int color_line, XPoint *points) {
    int i;
    for (i = 0; i < 3; i++) { PSxlimit(points[i].x); PSylimit(points[i].y); }
    fprintf(PSfile, "%5.3f %5.3f %5.3f %d %d %d %d %d %d  Tgl\n",
        myPSmap[color].red,
        myPSmap[color].green,
        myPSmap[color].blue,
        points[1].x, -points[1].y,
        points[0].x, -points[0].y,
        points[2].x, -points[2].y);
}

void densityfieldsqPS(int nox, int noy, double zmin, double zmax, int colmin, int colmax, double *f,
                      int xofs, int yofs, int xsize, int ysize) {
    int i, j, col;
    int r, g, b;
    double d;

    if (zmin != zmax) {
        yofs += ysize;
        PSxlimit(xofs + 0.5 * xsize / (nox + 1));
        PSylimit(yofs - 0.5 * ysize / (noy + 1));
        PSxlimit(xofs + xsize * (1 - .5 / (nox + 1)));
        PSylimit(yofs - ysize * (1 - .5 / (noy + 1)));

        fprintf(PSfile, "gsave\n");
        fprintf(PSfile, "/picstr %i string def\n", nox * 3);
        fprintf(PSfile, "%e %e translate\n",
            xofs + 0.5 * xsize / (nox + 1), -yofs + 0.5 * ysize / (noy + 1));
        fprintf(PSfile, "%e %e scale\n",
            xsize * (1 - 1.0 / (nox + 1)), ysize * (1 - 1.0 / (noy + 1)));
        fprintf(PSfile, "%% Image geometry\n %i %i 8\n", nox, noy);
        fprintf(PSfile, "%% Transformation Matrix\n [%i 0 0 %i 0 0]\n", nox, noy);
        fprintf(PSfile, "{currentfile picstr readhexstring pop}\n");
        fprintf(PSfile, "false 3\ncolorimage\n");
        for (j = 0; j < noy; j++) {
            for (i = 0; i < nox; i++) {
                d = (f[i * noy + j] - zmin) / (zmax - zmin);
                if (d < 0) d = 0;
                else if (d > 1) d = 1;
                col = colmin + d * (colmax - colmin);
                r = myPSmap[col].red * 255;
                g = myPSmap[col].green * 255;
                b = myPSmap[col].blue * 255;
                fprintf(PSfile, "%02x%02x%02x", r, g, b);
            }
            fprintf(PSfile, "\n");
        }
        fprintf(PSfile, "grestore\n\n");
    }
}

void densityfieldhexPS(int nox, int noy, double zmin, double zmax, int colmin, int colmax, double *f,
                       int xofs, int yofs, int xsize, int ysize) {
    densityfieldsqPS(nox, noy, zmin, zmax, colmin, colmax, f, xofs, yofs, xsize, ysize);
}

void densityfieldPS(int nox, int noy, double zmin, double zmax, int colmin, int colmax, double *f,
                    int xofs, int yofs, int xsize, int ysize, int hex) {
    if (hex)
        densityfieldsqPS(nox, noy, zmin, zmax, colmin, colmax, f, xofs, yofs, xsize, ysize);
    else
        densityfieldhexPS(nox, noy, zmin, zmax, colmin, colmax, f, xofs, yofs, xsize, ysize);
}

void twodensityfieldPS(int nox, int noy, double z1min, double z1max, double z2min, double z2max,
                       int colmin, int colx, int coly, double *f1, double *f2,
                       int xofs, int yofs, int xsize, int ysize) {
    int i, j, col, d1, d2;
    int r, g, b;

    if ((z1min != z1max) && (z2min != z2max)) {
        yofs += ysize;
        PSxlimit(xofs + 0.5 * xsize / (nox + 1));
        PSylimit(yofs - 0.5 * ysize / (noy + 1));
        PSxlimit(xofs + xsize * (1 - .5 / (nox + 1)));
        PSylimit(yofs - ysize * (1 - .5 / (noy + 1)));

        fprintf(PSfile, "gsave\n");
        fprintf(PSfile, "/picstr %i string def\n", nox * 3);
        fprintf(PSfile, "%e %e translate\n",
            xofs + 0.5 * xsize / (nox + 1), -yofs + 0.5 * ysize / (noy + 1));
        fprintf(PSfile, "%e %e scale\n",
            xsize * (1 - 1.0 / (nox + 1)), ysize * (1 - 1.0 / (noy + 1)));
        fprintf(PSfile, "%% Image geometry\n %i %i 8\n", nox, noy);
        fprintf(PSfile, "%% Transformation Matrix\n [%i 0 0 %i 0 0]\n", nox, noy);
        fprintf(PSfile, "{currentfile picstr readhexstring pop}\n");
        fprintf(PSfile, "false 3\ncolorimage\n");
        for (j = 0; j < noy; j++) {
            for (i = 0; i < nox; i++) {
                d1 = colx * (f1[i * noy + j] - z1min) / (z1max - z1min);
                if (d1 < 0) d1 = 0;
                else if (d1 > colx - 1) d1 = colx - 1;
                d2 = colx * (f2[i * noy + j] - z2min) / (z2max - z2min);
                if (d2 < 0) d2 = 0;
                else if (d2 > coly - 1) d2 = coly - 1;
                col = colmin + d1 * coly + d2;
                r = myPSmap[col].red * 255;
                g = myPSmap[col].green * 255;
                b = myPSmap[col].blue * 255;
                fprintf(PSfile, "%02x%02x%02x", r, g, b);
            }
            fprintf(PSfile, "\n");
        }
        fprintf(PSfile, "grestore\n\n");
    }
}

void mytext_PS(int color, int x, int y, const char *text, int orient) {
    PSxlimit(x); PSylimit(y);
    fprintf(PSfile, "gsave\n");
    fprintf(PSfile, "%5.3f %5.3f %5.3f setrgbcolor\n",
        myPSmap[color].red,
        myPSmap[color].green,
        myPSmap[color].blue);

    fprintf(PSfile, " %d %d moveto\n", x, -y);
    fprintf(PSfile, " %f rotate\n", psfontalpha);
    switch (orient) {
    case 2: /* to act mark */
        fprintf(PSfile, "(");
        fprintf(PSfile, "%s", text);
        fprintf(PSfile, ") rightshow\n");
        break;

    case 1: /* centered */
        fprintf(PSfile, "(");
        fprintf(PSfile, "%s", text);
        fprintf(PSfile, ") centershow\n");
        break;

    case 0: /* from act. mark */
        fprintf(PSfile, "(");
        fprintf(PSfile, "%s", text);
        fprintf(PSfile, ") show\n");
        break;
    }
    fprintf(PSfile, "grestore\n");
}

void myselectfont_PS(int font, char *testtext, double length) {
    fprintf(PSfile, "/");
    fprintf(PSfile, "%s", PSfontname[font]);
    fprintf(PSfile, " findfont dup\n"
        "30 scalefont\n"
        "setfont\n"
        "(");
    fprintf(PSfile, "%s", testtext);
    fprintf(PSfile, ") stringwidth pop\n"
        "30 %10.5f  mul exch div scalefont\n"
        "setfont\n", length);
}

void mysetfontdirection_PS(double alpha) {
    psfontalpha = alpha;
}

void setPS() {
    xsize = PSxsize;
    ysize = PSysize;
    mydrawline = mydrawlinePS;
    mypolygon = mypolygonPS;
    mypolygon_line = mypolygon_linePS;
    myline_polygon = myline_polygonPS;
    mytriangle_2 = mytriangle_2PS;
    mycircle = mycirclePS;
    myfilledcircle = myfilledcirclePS;
    densityfield = densityfieldPS;
    twodensityfield = twodensityfieldPS;
    mytext = mytext_PS;
    myselectfont = myselectfont_PS;
    mysetfontdirection = mysetfontdirection_PS;
    myclear = myclearPS;
    myshow = myshowPS;
    myXactive = 0;
    myclear();
}

int initPS(char *filename, double _xsize, double _ysize,
    int PSletter, int PSlandscape_, int *_PSxsize, int *_PSysize) {
    switch (PSletter) {
    case 0:
        if (PSlandscape_) {
            PSysize = maxPSysize = _xsize * maxPSxsizeA4;
            PSxsize = maxPSxsize = _ysize * maxPSysizeA4;
        }
        else {
            PSxsize = maxPSxsize = _xsize * maxPSxsizeA4;
            PSysize = maxPSysize = _ysize * maxPSysizeA4;
        }
        break;
    case 1:
        if (PSlandscape_) {
            PSysize = maxPSysize = _xsize * maxPSxsizeLetter;
            PSxsize = maxPSxsize = _ysize * maxPSysizeLetter;
        }
        else {
            PSxsize = maxPSxsize = _xsize * maxPSxsizeLetter;
            PSysize = maxPSysize = _ysize * maxPSysizeLetter;
        }
        break;
    default:
        fprintf(stderr, "mydrawc:initPS:Error format %i not 0=A4 or 1=letter\n",
            PSletter);
        PSxsize = _xsize * maxPSxsizeA4;
        PSysize = _ysize * maxPSysizeA4;
        break;
    }
    PSlandscape = PSlandscape_;
    PSfilename = filename;
    PSfile = fopen(PSfilename, "w");
    if (PSfile == NULL) {
        fprintf(stderr, "Could not open file %s! Waiting... ", PSfilename);
        Sleep(4000);
        PSfile = fopen(PSfilename, "w");
        if (PSfile == NULL) {
            fprintf(stderr, " failed again!\n");
            return 1;
        }
        else fprintf(stderr, " O.K. now.\n");
    }

    setPS();
    *_PSxsize = PSxsize;
    *_PSysize = PSysize;
    return 0;
}
