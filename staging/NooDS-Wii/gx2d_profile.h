#pragma once
#include <cstdint>
#ifdef NOODS_V180_PROFILE
#include <tuxedo/ppc/clock.h>
#include <tuxedo/ppc/intrinsics.h>
extern uint64_t g_gx2dTicks[8];
struct Gx2DProfileScope {
    unsigned index;
    uint64_t start;
    explicit Gx2DProfileScope(unsigned i):index(i),start(PPCGetTickCount()) {}
    ~Gx2DProfileScope() {g_gx2dTicks[index]+=PPCGetTickCount()-start;}
};
#define GX2D_PROFILE_QUEUE() Gx2DProfileScope gx2dQueueScope(0)
#define GX2D_PROFILE_BEGIN() uint64_t gx2dPhaseStart=PPCGetTickCount()
#define GX2D_PROFILE_MARK(i) do {uint64_t now=PPCGetTickCount();g_gx2dTicks[i]+=now-gx2dPhaseStart;gx2dPhaseStart=now;} while(0)
#else
#define GX2D_PROFILE_QUEUE() do {} while(0)
#define GX2D_PROFILE_BEGIN() do {} while(0)
#define GX2D_PROFILE_MARK(i) do {} while(0)
#endif
