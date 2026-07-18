#ifndef ANTIALIASING_H
#define ANTIALIASING_H

#include <stdint.h>

// Represents an RGBA pixel color
typedef struct {
    uint8_t a;
    uint8_t r;
    uint8_t g;
    uint8_t b;
} AntiAliasColor;

// 1. Original algorithm (Pure Bilinear)
// Legacy implementation: Fast for upscaling, but causes aliasing/disappearing lines when downscaling.
AntiAliasColor sample_bilinear_original(const uint32_t* img_pixels, int img_w, int img_h, float src_x, float src_y);

// 2. High-Quality Bilinear SSAA 2x2
// Replaced implementation: Prevents aliasing, but has high computational cost (16 memory reads + float math).
AntiAliasColor sample_ssaa_bilinear(const uint32_t* img_pixels, int img_w, int img_h, float src_x, float src_y, float scale_x, float scale_y);

// 3. Optimized Nearest-Neighbor SSAA 2x2
// Current production implementation: Ultra-lightweight anti-aliasing using bit shifts. Only 4 memory reads and no float multiplication.
AntiAliasColor sample_ssaa_nearest_optimized(const uint32_t* img_pixels, int img_w, int img_h, float src_x, float src_y, float scale_x, float scale_y);

#endif // ANTIALIASING_H
