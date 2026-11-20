/* GX texture cache index. The authoritative records and equality checks remain
 * in the renderer; the table only narrows the search. No deletion except clear.
 * 512 slots for at most 256 records guarantees an empty terminator. */
#pragma once
#include <stdint.h>
#include <string.h>

struct GxTextureIndex {
    uint16_t slots[512] = {}; // record index + 1; zero means empty

    void clear() { memset(slots, 0, sizeof(slots)); }

    template<typename P> static uint32_t hash(const P &p) {
        uint32_t h = (p.textureAddr >> 3) * 0x9e3779b1u;
        h ^= (p.paletteAddr >> 3) * 0x85ebca6bu;
        h ^= (uint32_t)p.sizeS * 0xc2b2ae35u;
        h ^= (uint32_t)p.sizeT * 0x27d4eb2du;
        h ^= (uint32_t)p.textureFmt * 0x165667b1u;
        h ^= (uint32_t)p.transparent0 * 0xd3a2646cu;
        h ^= h >> 16;
        return h & 511u;
    }

    template<typename P, typename E> static bool same(const P &p, const E &e) {
        return e.addr == p.textureAddr && e.pal == p.paletteAddr &&
            e.w == p.sizeS && e.h == p.sizeT && e.fmt == p.textureFmt &&
            e.trans0 == p.transparent0;
    }

    // Returns the record index or -1, and the existing/first empty slot.
    template<typename P, typename E> int find(const P &p, const E *entries, uint32_t &slot) const {
        slot = hash(p);
        while (slots[slot]) {
            int i = slots[slot] - 1;
            if (same(p, entries[i])) return i;
            slot = (slot + 1) & 511u;
        }
        return -1;
    }
    void insert(uint32_t slot, int index) { slots[slot] = (uint16_t)(index + 1); }
};
