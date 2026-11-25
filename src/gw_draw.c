#define STB_TRUETYPE_IMPLEMENTATION
#include "stb_truetype.h"
#include "gw_internal.h"
#include "antialiasing.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Definition of GW_Glyph
typedef struct {
    int width, height;
    int xoff, yoff;
    unsigned char* pixels;
} GW_Glyph;

// Definition of GW_Font
struct GW_Font {
    unsigned char* font_buffer;
    stbtt_fontinfo info;
    float size;
    float scale;
    int ascent, descent, line_gap;
    GW_Glyph ascii_cache[128];
};

// Drawing Helpers
void GW_Clear(GW_Window* win, uint32_t color) {
    if (!win || !win->backbuffer) return;
    int total_pixels = win->width * win->height;
    for (int i = 0; i < total_pixels; i++) {
        win->backbuffer[i] = color;
    }
}

void GW_DrawPixel(GW_Window* win, int x, int y, uint32_t color) {
    if (!win || !win->backbuffer) return;
    int bx1, by1, bx2, by2;
    if (!gw_draw_bounds(win, &bx1, &by1, &bx2, &by2)) return;
    if (x >= bx1 && x < bx2 && y >= by1 && y < by2) {
        win->backbuffer[y * win->width + x] = color;
    }
}

void GW_FillRect(GW_Window* win, int x, int y, int w, int h, uint32_t color) {
    if (!win || !win->backbuffer || w <= 0 || h <= 0) return;

    // Clip rectangle to the window and the clip rect
    int bx1, by1, bx2, by2;
    if (!gw_draw_bounds(win, &bx1, &by1, &bx2, &by2)) return;
    int x1 = x;
    int y1 = y;
    int x2 = x + w;
    int y2 = y + h;

    if (x1 < bx1) x1 = bx1;
    if (y1 < by1) y1 = by1;
    if (x2 > bx2) x2 = bx2;
    if (y2 > by2) y2 = by2;

    for (int cy = y1; cy < y2; cy++) {
        int row_offset = cy * win->width;
        for (int cx = x1; cx < x2; cx++) {
            win->backbuffer[row_offset + cx] = color;
        }
    }
}

void GW_DrawRect(GW_Window* win, int x, int y, int w, int h, uint32_t color) {
    if (!win || !win->backbuffer || w <= 0 || h <= 0) return;
    GW_FillRect(win, x, y, w, 1, color);             // Top
    GW_FillRect(win, x, y + h - 1, w, 1, color);     // Bottom
    GW_FillRect(win, x, y + 1, 1, h - 2, color);     // Left
    GW_FillRect(win, x + w - 1, y + 1, 1, h - 2, color); // Right
}

void GW_DrawLine(GW_Window* win, int x1, int y1, int x2, int y2, uint32_t color) {
    if (!win || !win->backbuffer) return;
    
    int dx = abs(x2 - x1);
    int dy = abs(y2 - y1);
    int sx = (x1 < x2) ? 1 : -1;
    int sy = (y1 < y2) ? 1 : -1;
    int err = dx - dy;
    
    while (1) {
        GW_DrawPixel(win, x1, y1, color);
        if (x1 == x2 && y1 == y2) break;
        int e2 = 2 * err;
        if (e2 > -dy) {
            err -= dy;
            x1 += sx;
        }
        if (e2 < dx) {
            err += dx;
            y1 += sy;
        }
    }
}

// Font loading helper
static unsigned char* load_file(const char* path, size_t* out_size) {
    FILE* f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    size_t size = ftell(f);
    fseek(f, 0, SEEK_SET);
    unsigned char* buf = (unsigned char*)malloc(size);
    if (buf) {
        size_t read_bytes = fread(buf, 1, size, f);
        (void)read_bytes;
        *out_size = size;
    }
    fclose(f);
    return buf;
}

static GW_Font* font_from_buffer(unsigned char* font_data, float font_size);

GW_Font* GW_LoadFontFromMemory(const unsigned char* data, int len, float font_size) {
    if (!data || len <= 0) return NULL;
    unsigned char* copy = (unsigned char*)malloc((size_t)len);
    if (!copy) return NULL;
    memcpy(copy, data, (size_t)len);
    return font_from_buffer(copy, font_size);
}

GW_Font* GW_LoadFont(const char* font_path, float font_size) {
    unsigned char* font_data = NULL;
    size_t font_size_bytes = 0;
    
    if (font_path) {
        font_data = load_file(font_path, &font_size_bytes);
    }
    
    // Windows Fallback list
#ifdef _WIN32
    if (!font_data) font_data = load_file("C:\\Windows\\Fonts\\consola.ttf", &font_size_bytes);
    if (!font_data) font_data = load_file("C:\\Windows\\Fonts\\arial.ttf", &font_size_bytes);
    if (!font_data) font_data = load_file("C:\\Windows\\Fonts\\segoeui.ttf", &font_size_bytes);
#else
    // Linux Fallback list
    if (!font_data) font_data = load_file("/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf", &font_size_bytes);
    if (!font_data) font_data = load_file("/usr/share/fonts/truetype/freefont/FreeMono.ttf", &font_size_bytes);
    if (!font_data) font_data = load_file("/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf", &font_size_bytes);
#endif
    
    if (!font_data) {
        fprintf(stderr, "LibGW Error: Could not load any font file!\n");
        return NULL;
    }

    return font_from_buffer(font_data, font_size);
}

// Takes ownership of font_data (malloc'ed); frees it on failure.
static GW_Font* font_from_buffer(unsigned char* font_data, float font_size) {
    GW_Font* font = (GW_Font*)malloc(sizeof(GW_Font));
    if (!font) {
        free(font_data);
        return NULL;
    }
    memset(font, 0, sizeof(GW_Font));
    font->font_buffer = font_data;
    font->size = font_size;
    
    if (!stbtt_InitFont(&font->info, font->font_buffer, 0)) {
        fprintf(stderr, "LibGW Error: Failed to initialize font info with stb_truetype.\n");
        free(font->font_buffer);
        free(font);
        return NULL;
    }
    
    font->scale = stbtt_ScaleForPixelHeight(&font->info, font_size);
    stbtt_GetFontVMetrics(&font->info, &font->ascent, &font->descent, &font->line_gap);
    
    // Pre-cache ASCII range (32 to 127)
    for (int i = 32; i < 128; i++) {
        int w, h, xoff, yoff;
        font->ascii_cache[i].pixels = stbtt_GetCodepointBitmap(&font->info, 0, font->scale, i, &w, &h, &xoff, &yoff);
        font->ascii_cache[i].width = w;
        font->ascii_cache[i].height = h;
        font->ascii_cache[i].xoff = xoff;
        font->ascii_cache[i].yoff = yoff;
    }
    
    return font;
}

void GW_FreeFont(GW_Font* font) {
    if (!font) return;
    for (int i = 32; i < 128; i++) {
        if (font->ascii_cache[i].pixels) {
            stbtt_FreeBitmap(font->ascii_cache[i].pixels, NULL);
        }
    }
    free(font->font_buffer);
    free(font);
}

static GW_Glyph get_glyph(GW_Font* font, int codepoint, int* needs_free) {
    *needs_free = 0;
    if (codepoint >= 32 && codepoint < 128) {
        return font->ascii_cache[codepoint];
    }
    
    // Render on the fly for unicode/accents
    GW_Glyph glyph;
    int w, h, xoff, yoff;
    glyph.pixels = stbtt_GetCodepointBitmap(&font->info, 0, font->scale, codepoint, &w, &h, &xoff, &yoff);
    glyph.width = w;
    glyph.height = h;
    glyph.xoff = xoff;
    glyph.yoff = yoff;
    *needs_free = 1;
    return glyph;
}

// Text core: works on Unicode codepoints, so UTF-16 surrogate pairs
// (wchar_t on Windows) and UTF-8 both reach stb_truetype as real
// codepoints. Tab = 4 spaces, as before.
static void text_draw_cps(GW_Window* win, GW_Font* font, int x, int y, const uint32_t* cps, int n, uint32_t color) {
    int cur_x = x;
    int base_y = y + (int)(font->ascent * font->scale);

    uint8_t text_r = (color >> 16) & 0xFF;
    uint8_t text_g = (color >> 8) & 0xFF;
    uint8_t text_b = color & 0xFF;

    int bx1, by1, bx2, by2;
    if (!gw_draw_bounds(win, &bx1, &by1, &bx2, &by2)) return;

    for (int i = 0; i < n; i++) {
        int codepoint = (int)cps[i];

        // Handle tab
        if (codepoint == '\t') {
            int advance, lsb;
            stbtt_GetCodepointHMetrics(&font->info, ' ', &advance, &lsb);
            cur_x += (int)(advance * font->scale) * 4; // 4 spaces
            continue;
        }

        // Nothing left to draw past the right edge of the drawable area.
        if (cur_x > bx2) break;

        int needs_free = 0;
        GW_Glyph glyph = get_glyph(font, codepoint, &needs_free);

        int draw_x = cur_x + glyph.xoff;
        int draw_y = base_y + glyph.yoff;

        // Draw the glyph bitmap with alpha blending
        if (glyph.pixels) {
            for (int gy = 0; gy < glyph.height; gy++) {
                int dest_y = draw_y + gy;
                if (dest_y < by1 || dest_y >= by2) continue;

                int dest_row_offset = dest_y * win->width;
                int glyph_row_offset = gy * glyph.width;

                for (int gx = 0; gx < glyph.width; gx++) {
                    int dest_x = draw_x + gx;
                    if (dest_x < bx1 || dest_x >= bx2) continue;

                    uint8_t alpha = glyph.pixels[glyph_row_offset + gx];
                    if (alpha == 0) continue;

                    int dest_idx = dest_row_offset + dest_x;
                    if (alpha == 255) {
                        win->backbuffer[dest_idx] = color;
                    } else {
                        // Linear blend
                        uint32_t bg = win->backbuffer[dest_idx];
                        uint8_t bg_r = (bg >> 16) & 0xFF;
                        uint8_t bg_g = (bg >> 8) & 0xFF;
                        uint8_t bg_b = bg & 0xFF;

                        uint8_t out_r = (uint8_t)(((text_r * alpha) + (bg_r * (255 - alpha))) / 255);
                        uint8_t out_g = (uint8_t)(((text_g * alpha) + (bg_g * (255 - alpha))) / 255);
                        uint8_t out_b = (uint8_t)(((text_b * alpha) + (bg_b * (255 - alpha))) / 255);

                        win->backbuffer[dest_idx] = (out_r << 16) | (out_g << 8) | out_b;
                    }
                }
            }
        }

        // Advance horizontal cursor
        int advance, lsb;
        stbtt_GetCodepointHMetrics(&font->info, codepoint, &advance, &lsb);
        cur_x += (int)(advance * font->scale);

        // Apply kerning
        if (i + 1 < n) {
            int kern = stbtt_GetCodepointKernAdvance(&font->info, codepoint, (int)cps[i + 1]);
            cur_x += (int)(kern * font->scale);
        }

        if (needs_free && glyph.pixels) {
            stbtt_FreeBitmap(glyph.pixels, NULL);
        }
    }
}

static void text_measure_cps(GW_Font* font, const uint32_t* cps, int n, int* width, int* height) {
    int cur_x = 0;
    for (int i = 0; i < n; i++) {
        int codepoint = (int)cps[i];

        if (codepoint == '\t') {
            int advance, lsb;
            stbtt_GetCodepointHMetrics(&font->info, ' ', &advance, &lsb);
            cur_x += (int)(advance * font->scale) * 4;
            continue;
        }

        int advance, lsb;
        stbtt_GetCodepointHMetrics(&font->info, codepoint, &advance, &lsb);
        cur_x += (int)(advance * font->scale);

        if (i + 1 < n) {
            int kern = stbtt_GetCodepointKernAdvance(&font->info, codepoint, (int)cps[i + 1]);
            cur_x += (int)(kern * font->scale);
        }
    }

    if (width) *width = cur_x;
    if (height) {
        *height = (int)((font->ascent - font->descent + font->line_gap) * font->scale);
    }
}

// Decoders into a codepoint buffer: on the stack for short strings, on the
// heap otherwise. Invalid UTF-8 and lone surrogates become U+FFFD.
#define GW_TEXT_STACK 256
static uint32_t* cps_from_wide(const wchar_t* text, uint32_t* stack, int* out_n) {
    int len = 0;
    while (text[len]) len++;
    uint32_t* buf = (len <= GW_TEXT_STACK) ? stack : (uint32_t*)malloc(sizeof(uint32_t) * len);
    if (!buf) { *out_n = 0; return stack; }
    int n = 0;
    for (int i = 0; i < len; i++) {
        uint32_t c = (uint32_t)text[i];
        if (sizeof(wchar_t) == 2 && c >= 0xD800 && c <= 0xDBFF && i + 1 < len &&
            (uint32_t)text[i + 1] >= 0xDC00 && (uint32_t)text[i + 1] <= 0xDFFF) {
            c = 0x10000 + ((c - 0xD800) << 10) + ((uint32_t)text[i + 1] - 0xDC00);
            i++;
        } else if (sizeof(wchar_t) == 2 && c >= 0xD800 && c <= 0xDFFF) {
            c = 0xFFFD;
        }
        buf[n++] = c;
    }
    *out_n = n;
    return buf;
}

static uint32_t* cps_from_utf8(const char* text, uint32_t* stack, int* out_n) {
    const unsigned char* s = (const unsigned char*)text;
    int len = (int)strlen(text);
    uint32_t* buf = (len <= GW_TEXT_STACK) ? stack : (uint32_t*)malloc(sizeof(uint32_t) * len);
    if (!buf) { *out_n = 0; return stack; }
    int n = 0, i = 0;
    while (i < len) {
        uint32_t c = s[i];
        int extra;
        if (c < 0x80) extra = 0;
        else if ((c & 0xE0) == 0xC0) { c &= 0x1F; extra = 1; }
        else if ((c & 0xF0) == 0xE0) { c &= 0x0F; extra = 2; }
        else if ((c & 0xF8) == 0xF0) { c &= 0x07; extra = 3; }
        else { buf[n++] = 0xFFFD; i++; continue; }
        int ok = 1;
        for (int k = 1; k <= extra; k++) {
            if (i + k >= len || (s[i + k] & 0xC0) != 0x80) { ok = 0; break; }
            c = (c << 6) | (s[i + k] & 0x3F);
        }
        if (!ok) { buf[n++] = 0xFFFD; i++; continue; }
        buf[n++] = c;
        i += 1 + extra;
    }
    *out_n = n;
    return buf;
}

void GW_DrawText(GW_Window* win, GW_Font* font, int x, int y, const wchar_t* text, uint32_t color) {
    if (!win || !win->backbuffer || !font || !text) return;
    uint32_t stack[GW_TEXT_STACK];
    int n = 0;
    uint32_t* cps = cps_from_wide(text, stack, &n);
    text_draw_cps(win, font, x, y, cps, n, color);
    if (cps != stack) free(cps);
}

void GW_MeasureText(GW_Font* font, const wchar_t* text, int* width, int* height) {
    if (!font || !text) {
        if (width) *width = 0;
        if (height) *height = 0;
        return;
    }
    uint32_t stack[GW_TEXT_STACK];
    int n = 0;
    uint32_t* cps = cps_from_wide(text, stack, &n);
    text_measure_cps(font, cps, n, width, height);
    if (cps != stack) free(cps);
}

void GW_DrawTextUTF8(GW_Window* win, GW_Font* font, int x, int y, const char* text, uint32_t color) {
    if (!win || !win->backbuffer || !font || !text) return;
    uint32_t stack[GW_TEXT_STACK];
    int n = 0;
    uint32_t* cps = cps_from_utf8(text, stack, &n);
    text_draw_cps(win, font, x, y, cps, n, color);
    if (cps != stack) free(cps);
}

void GW_MeasureTextUTF8(GW_Font* font, const char* text, int* width, int* height) {
    if (!font || !text) {
        if (width) *width = 0;
        if (height) *height = 0;
        return;
    }
    uint32_t stack[GW_TEXT_STACK];
    int n = 0;
    uint32_t* cps = cps_from_utf8(text, stack, &n);
    text_measure_cps(font, cps, n, width, height);
    if (cps != stack) free(cps);
}

void GW_GetFontMetrics(GW_Font* font, int* ascent, int* descent, int* line_height) {
    if (!font) {
        if (ascent) *ascent = 0;
        if (descent) *descent = 0;
        if (line_height) *line_height = 0;
        return;
    }
    if (ascent) *ascent = (int)(font->ascent * font->scale + 0.5f);
    if (descent) *descent = (int)(font->descent * font->scale - 0.5f);
    if (line_height) *line_height = (int)((font->ascent - font->descent + font->line_gap) * font->scale + 0.5f);
}

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#define NANOSVG_IMPLEMENTATION
#include "nanosvg.h"

#define NANOSVGRAST_IMPLEMENTATION
#include "nanosvgrast.h"

#include <math.h>

// Decodes PNG/JPG/BMP/GIF/... bytes into img (frames for animated GIFs).
// Silent: on failure img->pixels and img->frames stay NULL.
static void decode_raster(GW_Image* img, const unsigned char* file_data, size_t size) {
    // Try GIF first
    int* delays = NULL;
    int w = 0, h = 0, frames = 0;
    int comp = 4;
    unsigned char* gif_data = stbi_load_gif_from_memory((const stbi_uc*)file_data, (int)size, &delays, &w, &h, &frames, &comp, 4);
    
    if (gif_data && frames > 0) {
        img->w = w;
        img->h = h;
        img->frame_count = frames;
        img->frame_delays = (int*)malloc(frames * sizeof(int));
        img->frames = (uint32_t**)malloc(frames * sizeof(uint32_t*));
        
        if (img->frame_delays && img->frames) {
            for (int f = 0; f < frames; f++) {
                img->frame_delays[f] = delays[f];
                img->frames[f] = (uint32_t*)malloc(w * h * sizeof(uint32_t));
                if (img->frames[f]) {
                    // Convert RGBA bytes to 0xAARRGGBB
                    uint8_t* rgba = gif_data + f * w * h * 4;
                    for (int i = 0; i < w * h; i++) {
                        uint8_t r = rgba[i * 4 + 0];
                        uint8_t g = rgba[i * 4 + 1];
                        uint8_t b = rgba[i * 4 + 2];
                        uint8_t a = rgba[i * 4 + 3];
                        img->frames[f][i] = (a << 24) | (r << 16) | (g << 8) | b;
                    }
                }
            }
            img->pixels = img->frames[0];
        }
        if (delays) free(delays);
        stbi_image_free(gif_data);
    } else {
        // General format load using stbi_load
        int w = 0, h = 0, comp = 4;
        unsigned char* img_data = stbi_load_from_memory((const stbi_uc*)file_data, (int)size, &w, &h, &comp, 4);
        if (img_data) {
            img->w = w;
            img->h = h;
            img->frame_count = 1;
            img->pixels = (uint32_t*)malloc(w * h * sizeof(uint32_t));
            if (img->pixels) {
                // Convert RGBA bytes to 0xAARRGGBB
                for (int i = 0; i < w * h; i++) {
                    uint8_t r = img_data[i * 4 + 0];
                    uint8_t g = img_data[i * 4 + 1];
                    uint8_t b = img_data[i * 4 + 2];
                    uint8_t a = img_data[i * 4 + 3];
                    img->pixels[i] = (a << 24) | (r << 16) | (g << 8) | b;
                }
            }
            stbi_image_free(img_data);
        }
    }
}

GW_Image* GW_LoadImage(const char* filepath) {
    if (!filepath) return NULL;
    
    // Check file extension to see if it's SVG
    const char* ext = strrchr(filepath, '.');
    int is_svg = (ext && (strcmp(ext, ".svg") == 0 || strcmp(ext, ".SVG") == 0));
    
    size_t size = 0;
    unsigned char* file_data = load_file(filepath, &size);
    if (!file_data) {
        wchar_t wmsg[512];
        swprintf(wmsg, 512, L"No se pudo abrir el archivo: %hs", filepath);
        GW_ShowMessageBox(NULL, "Error al Cargar Imagen", wmsg, NULL, 1);
        return NULL;
    }
    
    GW_Image* img = (GW_Image*)malloc(sizeof(GW_Image));
    if (!img) {
        free(file_data);
        return NULL;
    }
    memset(img, 0, sizeof(GW_Image));
    
    if (is_svg) {
        // Parse SVG using NanoSVG
        // File data must be null-terminated for nanosvg
        unsigned char* svg_str = (unsigned char*)malloc(size + 1);
        if (svg_str) {
            memcpy(svg_str, file_data, size);
            svg_str[size] = '\0';
            
            NSVGimage* svg_img = nsvgParse((char*)svg_str, "px", 96.0f);
            free(svg_str);
            
            if (svg_img) {
                img->w = (int)svg_img->width;
                img->h = (int)svg_img->height;
                if (img->w <= 0) img->w = 800; // fallback default SVG sizes
                if (img->h <= 0) img->h = 600;
                
                img->frame_count = 1;
                img->pixels = (uint32_t*)malloc(img->w * img->h * sizeof(uint32_t));
                
                NSVGrasterizer* rast = nsvgCreateRasterizer();
                if (rast && img->pixels) {
                    nsvgRasterize(rast, svg_img, 0, 0, 1.0f, (unsigned char*)img->pixels, img->w, img->h, img->w * 4);
                    // NanoSVG rasterizes as RGBA bytes. Convert to our 0xAARRGGBB format.
                    uint8_t* rgba = (uint8_t*)img->pixels;
                    for (int i = 0; i < img->w * img->h; i++) {
                        uint8_t r = rgba[i * 4 + 0];
                        uint8_t g = rgba[i * 4 + 1];
                        uint8_t b = rgba[i * 4 + 2];
                        uint8_t a = rgba[i * 4 + 3];
                        img->pixels[i] = (a << 24) | (r << 16) | (g << 8) | b;
                    }
                    nsvgDeleteRasterizer(rast);
                }
                nsvgDelete(svg_img);
            }
        }
    } else {
        decode_raster(img, file_data, size);
    }
    
    free(file_data);
    
    if (!img->pixels && !img->frames) {
        wchar_t wmsg[512];
        swprintf(wmsg, 512, L"El formato de imagen no está soportado actualmente.");
        GW_ShowMessageBox(NULL, "Formato no soportado", wmsg, NULL, 1);
        free(img);
        return NULL;
    }
    
    return img;
}

void GW_FreeImage(GW_Image* img) {
    if (!img) return;
    if (img->frames) {
        for (int f = 0; f < img->frame_count; f++) {
            if (img->frames[f]) {
                free(img->frames[f]);
            }
        }
        free(img->frames);
    } else if (img->pixels) {
        free(img->pixels);
    }
    if (img->frame_delays) {
        free(img->frame_delays);
    }
    free(img);
}

void GW_DrawImage(GW_Window* win, GW_Image* img, int dx, int dy, int dw, int dh, int sx, int sy, int sw, int sh, int rotation, int flip) {
    if (!win || !win->backbuffer || !img || dw <= 0 || dh <= 0) return;
    
    uint32_t* img_pixels = img->pixels;
    if (img->frames && img->current_frame >= 0 && img->current_frame < img->frame_count) {
        img_pixels = img->frames[img->current_frame];
    }
    if (!img_pixels) return;
    
    // Bounds check for source parameters
    if (sw <= 0) sw = img->w;
    if (sh <= 0) sh = img->h;
    if (sx < 0) sx = 0;
    if (sy < 0) sy = 0;
    if (sx + sw > img->w) sw = img->w - sx;
    if (sy + sh > img->h) sh = img->h - sy;
    if (sw <= 0 || sh <= 0) return;
    
    // Target clipping to the window and the clip rect
    int bx1, by1, bx2, by2;
    if (!gw_draw_bounds(win, &bx1, &by1, &bx2, &by2)) return;
    int tx1 = dx;
    int ty1 = dy;
    int tx2 = dx + dw;
    int ty2 = dy + dh;

    if (tx1 < bx1) tx1 = bx1;
    if (ty1 < by1) ty1 = by1;
    if (tx2 > bx2) tx2 = bx2;
    if (ty2 > by2) ty2 = by2;
    if (tx1 >= tx2 || ty1 >= ty2) return;
    
    float inv_dw = 1.0f / (float)dw;
    float inv_dh = 1.0f / (float)dh;
    
    for (int y = ty1; y < ty2; y++) {
        int py = y - dy;
        float v = py * inv_dh;
        int row_offset = y * win->width;
        
        for (int x = tx1; x < tx2; x++) {
            int px = x - dx;
            float u = px * inv_dw;
            
            // Map (u, v) based on rotation
            float su = u;
            float sv = v;
            
            if (rotation == 90) {
                su = 1.0f - v;
                sv = u;
            } else if (rotation == 180) {
                su = 1.0f - u;
                sv = 1.0f - v;
            } else if (rotation == 270) {
                su = v;
                sv = 1.0f - u;
            }
            
            // Apply flip
            if (flip & 1) { // Horizontal flip
                su = 1.0f - su;
            }
            if (flip & 2) { // Vertical flip
                sv = 1.0f - sv;
            }
            
            // Map normalized coordinates to source pixel coordinates
            float src_x = sx + su * (sw - 1);
            float src_y = sy + sv * (sh - 1);
            
            float scale_x = (float)sw / (float)dw;
            float scale_y = (float)sh / (float)dh;
            
            AntiAliasColor color;
            if (scale_x > 1.3f || scale_y > 1.3f) {
                color = sample_ssaa_nearest_optimized(img_pixels, img->w, img->h, src_x, src_y, scale_x, scale_y);
            } else {
                color = sample_bilinear_original(img_pixels, img->w, img->h, src_x, src_y);
            }
            uint8_t a = color.a;
            uint8_t r = color.r;
            uint8_t g = color.g;
            uint8_t b = color.b;
            
            int dest_idx = row_offset + x;
            if (a == 255) {
                win->backbuffer[dest_idx] = (0xFF << 24) | (r << 16) | (g << 8) | b;
            } else if (a > 0) {
                uint32_t bg = win->backbuffer[dest_idx];
                uint8_t bg_r = (bg >> 16) & 0xFF;
                uint8_t bg_g = (bg >> 8) & 0xFF;
                uint8_t bg_b = bg & 0xFF;
                
                uint8_t out_r = (r * a + bg_r * (255 - a)) / 255;
                uint8_t out_g = (g * a + bg_g * (255 - a)) / 255;
                uint8_t out_b = (b * a + bg_b * (255 - a)) / 255;
                
                win->backbuffer[dest_idx] = (0xFF << 24) | (out_r << 16) | (out_g << 8) | out_b;
            }
        }
    }
}

/* ---- Extensions (feat/portmaster-ui): images from memory ---------------- */

GW_Image* GW_LoadImageFromMemory(const unsigned char* data, int len) {
    if (!data || len <= 0) return NULL;
    GW_Image* img = (GW_Image*)malloc(sizeof(GW_Image));
    if (!img) return NULL;
    memset(img, 0, sizeof(GW_Image));
    decode_raster(img, data, (size_t)len);
    if (!img->pixels && !img->frames) {
        free(img);
        return NULL;
    }
    return img;
}

// Rasterizes SVG text at width x height (<= 0: the SVG's own size, scaled
// proportionally if only one side is given).
GW_Image* GW_LoadSVGFromMemory(const char* svg_text, int len, int width, int height) {
    if (!svg_text || len <= 0) return NULL;
    char* str = (char*)malloc((size_t)len + 1);
    if (!str) return NULL;
    memcpy(str, svg_text, (size_t)len);
    str[len] = '\0';
    NSVGimage* svg = nsvgParse(str, "px", 96.0f);
    free(str);
    if (!svg) return NULL;
    float sw = svg->width > 0 ? svg->width : 24.0f;
    float sh = svg->height > 0 ? svg->height : 24.0f;
    float scale;
    if (width <= 0 && height <= 0) { width = (int)(sw + 0.5f); height = (int)(sh + 0.5f); scale = 1.0f; }
    else if (width <= 0) { scale = height / sh; width = (int)(sw * scale + 0.5f); }
    else if (height <= 0) { scale = width / sw; height = (int)(sh * scale + 0.5f); }
    else { float sx = width / sw, sy = height / sh; scale = sx < sy ? sx : sy; }
    if (width <= 0 || height <= 0) { nsvgDelete(svg); return NULL; }

    GW_Image* img = (GW_Image*)malloc(sizeof(GW_Image));
    uint32_t* px = (uint32_t*)calloc((size_t)width * height, sizeof(uint32_t));
    NSVGrasterizer* rast = nsvgCreateRasterizer();
    if (!img || !px || !rast) {
        free(img); free(px);
        if (rast) nsvgDeleteRasterizer(rast);
        nsvgDelete(svg);
        return NULL;
    }
    // Center the scaled drawing in the requested box.
    float tx = (width - sw * scale) * 0.5f, ty = (height - sh * scale) * 0.5f;
    nsvgRasterize(rast, svg, tx, ty, scale, (unsigned char*)px, width, height, width * 4);
    nsvgDeleteRasterizer(rast);
    nsvgDelete(svg);
    uint8_t* rgba = (uint8_t*)px;
    for (int i = 0; i < width * height; i++) {
        uint8_t r = rgba[i * 4 + 0], g = rgba[i * 4 + 1], b = rgba[i * 4 + 2], a = rgba[i * 4 + 3];
        px[i] = ((uint32_t)a << 24) | ((uint32_t)r << 16) | ((uint32_t)g << 8) | b;
    }
    memset(img, 0, sizeof(GW_Image));
    img->w = width;
    img->h = height;
    img->frame_count = 1;
    img->pixels = px;
    return img;
}

GW_Image* GW_CreateImage(int w, int h, const uint32_t* pixels) {
    if (w <= 0 || h <= 0) return NULL;
    GW_Image* img = (GW_Image*)malloc(sizeof(GW_Image));
    uint32_t* px = (uint32_t*)malloc((size_t)w * h * sizeof(uint32_t));
    if (!img || !px) { free(img); free(px); return NULL; }
    if (pixels) memcpy(px, pixels, (size_t)w * h * sizeof(uint32_t));
    else memset(px, 0, (size_t)w * h * sizeof(uint32_t));
    memset(img, 0, sizeof(GW_Image));
    img->w = w;
    img->h = h;
    img->frame_count = 1;
    img->pixels = px;
    return img;
}

// Nearest-neighbour scaled, alpha from the image times tint's alpha, color
// from tint. Meant for monochrome icons (e.g. SVGs rasterized once).
void GW_DrawImageTinted(GW_Window* win, GW_Image* img, int dx, int dy, int dw, int dh, uint32_t tint) {
    if (!win || !win->backbuffer || !img || dw <= 0 || dh <= 0) return;
    uint32_t* src = img->pixels;
    if (img->frames && img->current_frame >= 0 && img->current_frame < img->frame_count) {
        src = img->frames[img->current_frame];
    }
    if (!src || img->w <= 0 || img->h <= 0) return;
    int bx1, by1, bx2, by2;
    if (!gw_draw_bounds(win, &bx1, &by1, &bx2, &by2)) return;
    int x1 = dx < bx1 ? bx1 : dx, y1 = dy < by1 ? by1 : dy;
    int x2 = dx + dw > bx2 ? bx2 : dx + dw, y2 = dy + dh > by2 ? by2 : dy + dh;
    unsigned ta = (tint >> 24) & 0xFF;
    uint32_t rgb = tint & 0x00FFFFFFu;
    for (int y = y1; y < y2; y++) {
        int sy = (int)((int64_t)(y - dy) * img->h / dh);
        uint32_t* row = win->backbuffer + (size_t)y * win->width;
        for (int x = x1; x < x2; x++) {
            int sx = (int)((int64_t)(x - dx) * img->w / dw);
            unsigned a = (src[sy * img->w + sx] >> 24) & 0xFF;
            if (a == 0) continue;
            gw_blend_pixel(&row[x], ((uint32_t)(a * ta / 255) << 24) | rgb, 255);
        }
    }
}
