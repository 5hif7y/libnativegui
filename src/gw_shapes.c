// Platform-independent extensions (feat/portmaster-ui): clip rect, memory
// release, wheel mode and anti-aliased shapes. Shapes use a signed distance
// per pixel center for the coverage (1 px soft edge); interior spans skip
// the distance math.
#include "gw_internal.h"
#include <math.h>
#include <stdlib.h>

void GW_Free(void* ptr) {
    free(ptr);
}

void GW_SetWheelMode(GW_Window* win, int mode) {
    if (win) win->wheel_mode = mode;
}

void GW_SetClipRect(GW_Window* win, int x, int y, int w, int h) {
    if (!win) return;
    if (w < 0) w = 0;
    if (h < 0) h = 0;
    win->clip_set = 1;
    win->clip_x = x;
    win->clip_y = y;
    win->clip_w = w;
    win->clip_h = h;
}

void GW_ResetClipRect(GW_Window* win) {
    if (win) win->clip_set = 0;
}

void GW_GetClipRect(GW_Window* win, int* x, int* y, int* w, int* h) {
    int x1 = 0, y1 = 0, x2 = 0, y2 = 0;
    if (win && !gw_draw_bounds(win, &x1, &y1, &x2, &y2)) { x2 = x1; y2 = y1; }
    if (x) *x = x1;
    if (y) *y = y1;
    if (w) *w = x2 - x1;
    if (h) *h = y2 - y1;
}

static int clamp_span(const GW_Window* win, int x, int y, int w, int h, int* x1, int* y1, int* x2, int* y2) {
    int bx1, by1, bx2, by2;
    if (!gw_draw_bounds(win, &bx1, &by1, &bx2, &by2)) return 0;
    *x1 = x < bx1 ? bx1 : x;
    *y1 = y < by1 ? by1 : y;
    *x2 = x + w > bx2 ? bx2 : x + w;
    *y2 = y + h > by2 ? by2 : y + h;
    return *x1 < *x2 && *y1 < *y2;
}

static unsigned coverage(float d) {
    // d = signed distance from the pixel center to the edge (< 0 inside).
    float c = 0.5f - d;
    if (c <= 0.0f) return 0;
    if (c >= 1.0f) return 255;
    return (unsigned)(c * 255.0f + 0.5f);
}

void GW_FillRectBlend(GW_Window* win, int x, int y, int w, int h, uint32_t color) {
    if (!win || !win->backbuffer || w <= 0 || h <= 0) return;
    if (((color >> 24) & 0xFF) == 0) return;
    int x1, y1, x2, y2;
    if (!clamp_span(win, x, y, w, h, &x1, &y1, &x2, &y2)) return;
    for (int cy = y1; cy < y2; cy++) {
        uint32_t* row = win->backbuffer + (size_t)cy * win->width;
        for (int cx = x1; cx < x2; cx++) gw_blend_pixel(&row[cx], color, 255);
    }
}

// Signed distance from (px, py) to a rounded rect given by its center,
// half extents and radius.
static float sd_round_rect(float px, float py, float cx, float cy, float hw, float hh, float r) {
    float qx = fabsf(px - cx) - (hw - r);
    float qy = fabsf(py - cy) - (hh - r);
    float ox = qx > 0 ? qx : 0, oy = qy > 0 ? qy : 0;
    float outside = sqrtf(ox * ox + oy * oy);
    float m = qx > qy ? qx : qy;
    float inside = m < 0 ? m : 0;
    return outside + inside - r;
}

static void round_rect_impl(GW_Window* win, int x, int y, int w, int h, int radius, int thickness, uint32_t color) {
    if (!win || !win->backbuffer || w <= 0 || h <= 0) return;
    if (((color >> 24) & 0xFF) == 0) return;
    int max_r = (w < h ? w : h) / 2;
    if (radius > max_r) radius = max_r;
    if (radius < 0) radius = 0;
    int x1, y1, x2, y2;
    if (!clamp_span(win, x, y, w, h, &x1, &y1, &x2, &y2)) return;
    float cx = x + w * 0.5f, cy = y + h * 0.5f, hw = w * 0.5f, hh = h * 0.5f, r = (float)radius;
    // Band near the border where the distance has to be computed: the
    // corners for a fill, the whole outline for a stroke.
    int band = thickness > 0 ? thickness + 1 : radius + 1;
    for (int py = y1; py < y2; py++) {
        uint32_t* row = win->backbuffer + (size_t)py * win->width;
        int near_y = (py < y + band) || (py >= y + h - band);
        for (int px = x1; px < x2; px++) {
            int near_x = (px < x + band) || (px >= x + w - band);
            if (thickness <= 0) {
                // Fill: only the corner squares need the distance; straight
                // edges of an integer rect are pixel-exact.
                if (radius == 0 || !(near_x && near_y)) { gw_blend_pixel(&row[px], color, 255); continue; }
            } else if (!near_x && !near_y) {
                continue; // hollow middle of a stroke
            }
            float d = sd_round_rect(px + 0.5f, py + 0.5f, cx, cy, hw, hh, r);
            unsigned cov = coverage(d);
            if (thickness > 0) {
                unsigned inner = coverage(d + (float)thickness);
                cov = cov > inner ? cov - inner : 0;
            }
            if (cov) gw_blend_pixel(&row[px], color, cov);
        }
    }
}

void GW_FillRoundRect(GW_Window* win, int x, int y, int w, int h, int radius, uint32_t color) {
    round_rect_impl(win, x, y, w, h, radius, 0, color);
}

void GW_DrawRoundRect(GW_Window* win, int x, int y, int w, int h, int radius, int thickness, uint32_t color) {
    if (thickness <= 0) thickness = 1;
    round_rect_impl(win, x, y, w, h, radius, thickness, color);
}

void GW_FillCircle(GW_Window* win, int cx, int cy, int radius, uint32_t color) {
    if (!win || !win->backbuffer || radius <= 0) return;
    if (((color >> 24) & 0xFF) == 0) return;
    int x1, y1, x2, y2;
    if (!clamp_span(win, cx - radius - 1, cy - radius - 1, 2 * radius + 3, 2 * radius + 3, &x1, &y1, &x2, &y2)) return;
    float fcx = (float)cx, fcy = (float)cy, r = (float)radius;
    for (int py = y1; py < y2; py++) {
        uint32_t* row = win->backbuffer + (size_t)py * win->width;
        float dy = py + 0.5f - fcy;
        for (int px = x1; px < x2; px++) {
            float dx = px + 0.5f - fcx;
            unsigned cov = coverage(sqrtf(dx * dx + dy * dy) - r);
            if (cov) gw_blend_pixel(&row[px], color, cov);
        }
    }
}

void GW_DrawLineAA(GW_Window* win, float x1f, float y1f, float x2f, float y2f, float thickness, uint32_t color) {
    if (!win || !win->backbuffer) return;
    if (((color >> 24) & 0xFF) == 0) return;
    if (thickness <= 0) thickness = 1.0f;
    float hr = thickness * 0.5f;
    int minx = (int)floorf((x1f < x2f ? x1f : x2f) - hr - 1);
    int maxx = (int)ceilf((x1f > x2f ? x1f : x2f) + hr + 1);
    int miny = (int)floorf((y1f < y2f ? y1f : y2f) - hr - 1);
    int maxy = (int)ceilf((y1f > y2f ? y1f : y2f) + hr + 1);
    int bx1, by1, bx2, by2;
    if (!clamp_span(win, minx, miny, maxx - minx, maxy - miny, &bx1, &by1, &bx2, &by2)) return;
    float vx = x2f - x1f, vy = y2f - y1f;
    float len2 = vx * vx + vy * vy;
    for (int py = by1; py < by2; py++) {
        uint32_t* row = win->backbuffer + (size_t)py * win->width;
        for (int px = bx1; px < bx2; px++) {
            float wx = px + 0.5f - x1f, wy = py + 0.5f - y1f;
            float t = len2 > 0 ? (wx * vx + wy * vy) / len2 : 0;
            if (t < 0) t = 0;
            if (t > 1) t = 1;
            float ex = wx - t * vx, ey = wy - t * vy;
            unsigned cov = coverage(sqrtf(ex * ex + ey * ey) - hr);
            if (cov) gw_blend_pixel(&row[px], color, cov);
        }
    }
}
