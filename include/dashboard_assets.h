#pragma once
#include <stdint.h>
extern const uint16_t dashboard_background[];
extern const uint8_t font_alpha[], font_widths[];
constexpr char font_chars[] = "0123456789.,%ABCDEFGHIJKLMNOPQRSTUVWXYZ /-h";
struct BandPixel { uint32_t offset; uint16_t threshold, active, inactive; };
extern const BandPixel band_pixels[];
extern const uint32_t band_ranges[];
constexpr float dashboard_y_scale = 600.0f / 570.0f;
