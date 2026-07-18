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
    if (x >= 0 && x < win->width && y >= 0 && y < win->height) {
        win->backbuffer[y * win->width + x] = color;
    }
}

void GW_FillRect(GW_Window* win, int x, int y, int w, int h, uint32_t color) {
    if (!win || !win->backbuffer || w <= 0 || h <= 0) return;
    
    // Clip rectangle to window bounds
    int x1 = x;
    int y1 = y;
    int x2 = x + w;
    int y2 = y + h;
    
    if (x1 < 0) x1 = 0;
    if (y1 < 0) y1 = 0;
    if (x2 > win->width) x2 = win->width;
    if (y2 > win->height) y2 = win->height;
    
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

void GW_DrawText(GW_Window* win, GW_Font* font, int x, int y, const wchar_t* text, uint32_t color) {
    if (!win || !win->backbuffer || !font || !text) return;
    
    int cur_x = x;
    int base_y = y + (int)(font->ascent * font->scale);
    
    uint8_t text_r = (color >> 16) & 0xFF;
    uint8_t text_g = (color >> 8) & 0xFF;
    uint8_t text_b = color & 0xFF;
    
    for (int i = 0; text[i] != L'\0'; i++) {
        wchar_t codepoint = text[i];
        
        // Handle tab
        if (codepoint == L'\t') {
            int advance, lsb;
            stbtt_GetCodepointHMetrics(&font->info, L' ', &advance, &lsb);
            cur_x += (int)(advance * font->scale) * 4; // 4 spaces
            continue;
        }
        
        int needs_free = 0;
        GW_Glyph glyph = get_glyph(font, codepoint, &needs_free);
        
        int draw_x = cur_x + glyph.xoff;
        int draw_y = base_y + glyph.yoff;
        
        // Draw the glyph bitmap with alpha blending
        if (glyph.pixels) {
            for (int gy = 0; gy < glyph.height; gy++) {
                int dest_y = draw_y + gy;
                if (dest_y < 0 || dest_y >= win->height) continue;
                
                int dest_row_offset = dest_y * win->width;
                int glyph_row_offset = gy * glyph.width;
                
                for (int gx = 0; gx < glyph.width; gx++) {
                    int dest_x = draw_x + gx;
                    if (dest_x < 0 || dest_x >= win->width) continue;
                    
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
        if (text[i + 1] != L'\0') {
            int kern = stbtt_GetCodepointKernAdvance(&font->info, codepoint, text[i + 1]);
            cur_x += (int)(kern * font->scale);
        }
        
        if (needs_free && glyph.pixels) {
            stbtt_FreeBitmap(glyph.pixels, NULL);
        }
    }
}

void GW_MeasureText(GW_Font* font, const wchar_t* text, int* width, int* height) {
    if (!font) {
        if (width) *width = 0;
        if (height) *height = 0;
        return;
    }
    
    int cur_x = 0;
    for (int i = 0; text[i] != L'\0'; i++) {
        wchar_t codepoint = text[i];
        
        if (codepoint == L'\t') {
            int advance, lsb;
            stbtt_GetCodepointHMetrics(&font->info, L' ', &advance, &lsb);
            cur_x += (int)(advance * font->scale) * 4;
            continue;
        }
        
        int advance, lsb;
        stbtt_GetCodepointHMetrics(&font->info, codepoint, &advance, &lsb);
        cur_x += (int)(advance * font->scale);
        
        if (text[i + 1] != L'\0') {
            int kern = stbtt_GetCodepointKernAdvance(&font->info, codepoint, text[i + 1]);
            cur_x += (int)(kern * font->scale);
        }
    }
    
    if (width) *width = cur_x;
    if (height) {
        *height = (int)((font->ascent - font->descent + font->line_gap) * font->scale);
    }
}

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#define NANOSVG_IMPLEMENTATION
#include "nanosvg.h"

#define NANOSVGRAST_IMPLEMENTATION
#include "nanosvgrast.h"

#include <math.h>

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
        // Try GIF first
        int* delays = NULL;
        int w = 0, h = 0, frames = 0;
        int comp = 4;
        unsigned char* gif_data = stbi_load_gif_from_memory(file_data, (int)size, &delays, &w, &h, &frames, &comp, 4);
        
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
            unsigned char* img_data = stbi_load_from_memory(file_data, (int)size, &w, &h, &comp, 4);
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
    
    // Target clipping to window bounds
    int tx1 = dx;
    int ty1 = dy;
    int tx2 = dx + dw;
    int ty2 = dy + dh;
    
    if (tx1 < 0) tx1 = 0;
    if (ty1 < 0) ty1 = 0;
    if (tx2 > win->width) tx2 = win->width;
    if (ty2 > win->height) ty2 = win->height;
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
