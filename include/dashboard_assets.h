#pragma once
#include <stdint.h>
extern const uint16_t dashboard_background[];
extern const uint8_t font_alpha[], font_widths[];
constexpr char font_chars[] = "0123456789.,%ABCDEFGHIJKLMNOPQRSTUVWXYZ /-h";
struct BandPixel { uint32_t offset; uint16_t threshold, active, inactive; };
extern const BandPixel band_pixels[];
extern const uint32_t band_ranges[];
constexpr float dashboard_y_scale = 600.0f / 570.0f;
extern const uint32_t highlight_pixels[], highlight_ranges[];
extern const uint16_t engine_icon_pixels[];
constexpr int engine_icon_x=854,engine_icon_y=24,engine_icon_width=76,engine_icon_height=55;
