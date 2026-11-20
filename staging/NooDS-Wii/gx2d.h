#pragma once
#include <cstdint>
#ifndef NOODS_GX_2D
#define NOODS_GX_2D 0
#endif
#ifndef NOODS_GX_2D_IO
#define NOODS_GX_2D_IO 0
#endif
#ifndef NOODS_GX_2D_BATCH
#define NOODS_GX_2D_BATCH 0
#endif
#ifndef NOODS_GX_2D_PAL_CACHE
#define NOODS_GX_2D_PAL_CACHE 0
#endif
#ifndef NOODS_GX_2D_VERIFY
#define NOODS_GX_2D_VERIFY 0
#endif
#ifndef NOODS_GX_2D_BITMAP
#define NOODS_GX_2D_BITMAP 0
#endif
#if NOODS_GX_2D_BITMAP && (!NOODS_GX_2D || !NOODS_GX_2D_BATCH || NOODS_GX_2D_IO < 7)
#error GX_2D_BITMAP requires GX_2D=1, GX_2D_BATCH=1 and GX_2D_IO>=7
#endif
// Snapshot at the emulated scanline, not at end-of-frame: no guest VRAM or
// palette pointers survive into the queued draw. CI8 images are 256x4; only
// row zero is sampled (at its texel center).
struct alignas(32) Gx2DLine {
    uint8_t indices[4][1024];
    uint8_t palettes[4][512];
#if NOODS_GX_2D_BITMAP
    bool bitmap; // indices[0][0..511] is a linear big-endian RGB5A3 row
#endif
    uint16_t backdrop;
    uint8_t enabled, priority[4];
#if NOODS_GX_2D_IO >= 4
    const uint8_t *paletteRefs[4];
#endif
};
Gx2DLine *gx2dBegin(unsigned engine, unsigned line);
bool gx2dSetPalette(Gx2DLine *row,unsigned engine,unsigned bg,const uint8_t *src,bool eight);
void gx2dDiscard(unsigned engine, unsigned line);
void gx2dFinish(unsigned engine, uint32_t *framebuffer);
// Call only after the old core and its GX submissions have stopped.
void gx2dReleaseCaches();
extern uint32_t g_gx2dStats[8]; // queued lines, BGs, passes, compared px, wrong px, 4bpp, 8bpp, alloc failures

#if NOODS_GX_2D_BITMAP
extern uint32_t g_gxBitmapStats[4]; // lines, compared pixels, wrong pixels, finish passes
#endif
