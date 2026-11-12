#pragma once
#include "dashboard_assets.h"
constexpr unsigned gauge_face_size=320;
struct GaugeFaceSpec { const char* name; float minimum,maximum; };
extern const GaugeFaceSpec gauge_face_specs[10];
extern const uint16_t gauge_face_pixels[];
extern const BandPixel gauge_face_bands[];
extern const uint32_t gauge_face_ranges[];
