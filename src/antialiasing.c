#include "antialiasing.h"
#include <math.h>

// 1. Original algorithm (Pure Bilinear)
// Legacy implementation: Fast for upscaling, but causes aliasing/disappearing lines when downscaling.
AntiAliasColor sample_bilinear_original(const uint32_t* img_pixels, int img_w, int img_h, float src_x, float src_y) {
    int x0 = (int)floorf(src_x);
    int y0 = (int)floorf(src_y);
    int x1 = x0 + 1;
    int y1 = y0 + 1;
    
    float tx = src_x - x0;
    float ty = src_y - y0;
    
    if (x0 < 0) x0 = 0; if (x0 >= img_w) x0 = img_w - 1;
    if (x1 < 0) x1 = 0; if (x1 >= img_w) x1 = img_w - 1;
    if (y0 < 0) y0 = 0; if (y0 >= img_h) y0 = img_h - 1;
    if (y1 < 0) y1 = 0; if (y1 >= img_h) y1 = img_h - 1;
    
    uint32_t c00 = img_pixels[y0 * img_w + x0];
    uint32_t c10 = img_pixels[y0 * img_w + x1];
    uint32_t c01 = img_pixels[y1 * img_w + x0];
    uint32_t c11 = img_pixels[y1 * img_w + x1];
    
    float a00 = (c00 >> 24) & 0xFF;
    float r00 = (c00 >> 16) & 0xFF;
    float g00 = (c00 >> 8) & 0xFF;
    float b00 = c00 & 0xFF;
    
    float a10 = (c10 >> 24) & 0xFF;
    float r10 = (c10 >> 16) & 0xFF;
    float g10 = (c10 >> 8) & 0xFF;
    float b10 = c10 & 0xFF;
    
    float a01 = (c01 >> 24) & 0xFF;
    float r01 = (c01 >> 16) & 0xFF;
    float g01 = (c01 >> 8) & 0xFF;
    float b01 = c01 & 0xFF;
    
    float a11 = (c11 >> 24) & 0xFF;
    float r11 = (c11 >> 16) & 0xFF;
    float g11 = (c11 >> 8) & 0xFF;
    float b11 = c11 & 0xFF;
    
    float w00 = (1.0f - tx) * (1.0f - ty);
    float w10 = tx * (1.0f - ty);
    float w01 = (1.0f - tx) * ty;
    float w11 = tx * ty;
    
    AntiAliasColor color;
    color.a = (uint8_t)(a00 * w00 + a10 * w10 + a01 * w01 + a11 * w11);
    color.r = (uint8_t)(r00 * w00 + r10 * w10 + r01 * w01 + r11 * w11);
    color.g = (uint8_t)(g00 * w00 + g10 * w10 + g01 * w01 + g11 * w11);
    color.b = (uint8_t)(b00 * w00 + b10 * w10 + b01 * w01 + b11 * w11);
    return color;
}

// 2. High-Quality Bilinear SSAA 2x2
// Replaced implementation: Prevents aliasing, but has high computational cost (16 memory reads + float math).
AntiAliasColor sample_ssaa_bilinear(const uint32_t* img_pixels, int img_w, int img_h, float src_x, float src_y, float scale_x, float scale_y) {
    float offsets_x[2] = { -0.25f * scale_x, 0.25f * scale_x };
    float offsets_y[2] = { -0.25f * scale_y, 0.25f * scale_y };
    
    int sum_a = 0, sum_r = 0, sum_g = 0, sum_b = 0;
    
    for (int sy_sub = 0; sy_sub < 2; sy_sub++) {
        for (int sx_sub = 0; sx_sub < 2; sx_sub++) {
            float sub_x = src_x + offsets_x[sx_sub];
            float sub_y = src_y + offsets_y[sy_sub];
            
            int x0 = (int)floorf(sub_x);
            int y0 = (int)floorf(sub_y);
            int x1 = x0 + 1;
            int y1 = y0 + 1;
            
            float tx = sub_x - x0;
            float ty = sub_y - y0;
            
            if (x0 < 0) x0 = 0; if (x0 >= img_w) x0 = img_w - 1;
            if (x1 < 0) x1 = 0; if (x1 >= img_w) x1 = img_w - 1;
            if (y0 < 0) y0 = 0; if (y0 >= img_h) y0 = img_h - 1;
            if (y1 < 0) y1 = 0; if (y1 >= img_h) y1 = img_h - 1;
            
            uint32_t c00 = img_pixels[y0 * img_w + x0];
            uint32_t c10 = img_pixels[y0 * img_w + x1];
            uint32_t c01 = img_pixels[y1 * img_w + x0];
            uint32_t c11 = img_pixels[y1 * img_w + x1];
            
            float w00 = (1.0f - tx) * (1.0f - ty);
            float w10 = tx * (1.0f - ty);
            float w01 = (1.0f - tx) * ty;
            float w11 = tx * ty;
            
            sum_a += (int)(((c00 >> 24) & 0xFF) * w00 + ((c10 >> 24) & 0xFF) * w10 + ((c01 >> 24) & 0xFF) * w01 + ((c11 >> 24) & 0xFF) * w11);
            sum_r += (int)(((c00 >> 16) & 0xFF) * w00 + ((c10 >> 16) & 0xFF) * w10 + ((c01 >> 16) & 0xFF) * w01 + ((c11 >> 16) & 0xFF) * w11);
            sum_g += (int)(((c00 >> 8) & 0xFF) * w00 + ((c10 >> 8) & 0xFF) * w10 + ((c01 >> 8) & 0xFF) * w01 + ((c11 >> 8) & 0xFF) * w11);
            sum_b += (int)((c00 & 0xFF) * w00 + (c10 & 0xFF) * w10 + (c01 & 0xFF) * w01 + (c11 & 0xFF) * w11);
        }
    }
    
    AntiAliasColor color;
    color.a = (uint8_t)(sum_a / 4);
    color.r = (uint8_t)(sum_r / 4);
    color.g = (uint8_t)(sum_g / 4);
    color.b = (uint8_t)(sum_b / 4);
    return color;
}

// 3. Optimized Nearest-Neighbor SSAA 2x2
// Current production implementation: Ultra-lightweight anti-aliasing using bit shifts. Only 4 memory reads and no float multiplication.
AntiAliasColor sample_ssaa_nearest_optimized(const uint32_t* img_pixels, int img_w, int img_h, float src_x, float src_y, float scale_x, float scale_y) {
    float offsets_x[2] = { -0.25f * scale_x, 0.25f * scale_x };
    float offsets_y[2] = { -0.25f * scale_y, 0.25f * scale_y };
    
    int sum_a = 0, sum_r = 0, sum_g = 0, sum_b = 0;
    
    for (int sy_sub = 0; sy_sub < 2; sy_sub++) {
        for (int sx_sub = 0; sx_sub < 2; sx_sub++) {
            float sub_x = src_x + offsets_x[sx_sub];
            float sub_y = src_y + offsets_y[sy_sub];
            
            int x0 = (int)(sub_x + 0.5f);
            int y0 = (int)(sub_y + 0.5f);
            
            if (x0 < 0) x0 = 0; else if (x0 >= img_w) x0 = img_w - 1;
            if (y0 < 0) y0 = 0; else if (y0 >= img_h) y0 = img_h - 1;
            
            uint32_t c = img_pixels[y0 * img_w + x0];
            sum_a += (c >> 24) & 0xFF;
            sum_r += (c >> 16) & 0xFF;
            sum_g += (c >> 8) & 0xFF;
            sum_b += c & 0xFF;
        }
    }
    
    AntiAliasColor color;
    color.a = (uint8_t)(sum_a >> 2);
    color.r = (uint8_t)(sum_r >> 2);
    color.g = (uint8_t)(sum_g >> 2);
    color.b = (uint8_t)(sum_b >> 2);
    return color;
}
