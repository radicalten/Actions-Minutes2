/*
    v1p161 experimental pure-GX scanline overlay.
    The CPU derives a final-image primitive list for NDS-only rasterization
    features. GX remains the only writer of the displayed 3D framebuffer.
*/

#pragma once

#include <cstdint>

#ifndef NOODS_V161_GX_OVERLAY
#define NOODS_V161_GX_OVERLAY 0
#endif
#ifndef NOODS_GX_OVERLAY_MAX_POLYS
#define NOODS_GX_OVERLAY_MAX_POLYS 128
#endif

constexpr uint32_t MAX_GX_OVERLAY_RUNS = 32768;

struct GxOverlayRun {
    uint16_t y;
    uint16_t x0;
    uint16_t x1; // exclusive
    uint16_t reserved;
    uint32_t color; // NooDS 6-bit RGBA word, including BIT(26)
};

extern uint32_t g_gxOverlayActive;
extern uint32_t g_gxOverlayBudgetSkip; // large/complex frame: skip DS span-capture attempts
extern uint32_t g_gxOverlayRunCount;
extern GxOverlayRun g_gxOverlayRuns[MAX_GX_OVERLAY_RUNS];
extern uint32_t g_gxOverlayStats[8];
