#pragma once
#include <cstddef>
#include <cstdint>

// Same-width, basic-block-local load forwarding. Never removes a guest-state
// store or changes branch displacements. Unknown instructions and stores are
// barriers; every statically addressed branch target starts with empty facts.
namespace NoodsJit {
inline unsigned forwardPpcGuestLoads(uint32_t *code, size_t words, int usrOffset) {
    if (words > 4096) return 0;
    bool target[4096] = {};
    for (size_t i = 0; i < words; ++i) {
        const uint32_t w = code[i], op = w >> 26;
        if ((op == 18 || op == 16) && (w & 2u)) return 0; // absolute-branch target not analysed
        if (op != 18 && op != 16) continue;
        const int32_t displacement = op == 16 ? (int32_t)(int16_t)(w & 0xFFFCu) :
            ((int32_t)(w << 6) >> 6) & ~3;
        const int64_t at = (int64_t)i * 4 + displacement;
        if (at >= 0 && at < (int64_t)words * 4 && !(at & 3)) target[at / 4] = true;
    }
    int tag[32];
    auto clear = [&]() { for (int &v : tag) v = -1; };
    auto kill = [&](unsigned r) { if (r == 31) clear(); else tag[r] = -1; };
    auto tracked = [](unsigned r) { return (r >= 3 && r <= 12) || (r >= 28 && r <= 30); };
    auto guestCell = [&](int off) {
        const int n = off - usrOffset;
        return n >= 0 && !(n & 3) && (n < 8 * 4 || n == 15 * 4);
    };
    clear();
    unsigned replaced = 0;
    for (size_t i = 0; i < words; ++i) {
        if (target[i]) clear();
        const uint32_t w = code[i];
        const unsigned op = w >> 26, rt = (w >> 21) & 31, ra = (w >> 16) & 31;
        const int off = (int16_t)w;
        if (op == 32) { // lwz
            int source = -1;
            if (ra == 31 && guestCell(off) && tracked(rt)) {
                for (unsigned r = 0; r < 32; ++r)
                    if (tag[r] == off && tracked(r)) { source = (int)r; break; }
            }
            kill(rt);
            if (source >= 0) {
                // mr rt,source = or rt,source,source, without touching CR/XER.
                code[i] = (31u << 26) | ((unsigned)source << 21) | (rt << 16) |
                          ((unsigned)source << 11) | (444u << 1);
                ++replaced;
            }
            if (rt != 31 && ra == 31 && guestCell(off) && tracked(rt)) tag[rt] = off;
            continue;
        }
        if (op == 36) { // stw
            if (ra != 31 || (off & 3)) { clear(); continue; }
            // Aligned stores through the Interpreter base only alias an exact
            // word. Every other/indirect/partial store is a barrier below.
            for (int &v : tag) if (v == off) v = -1;
            if (guestCell(off) && tracked(rt)) tag[rt] = off;
            continue;
        }
        switch (op) {
        case 7: case 8: case 12: case 13: case 14: case 15: // D-form arithmetic -> RT
        case 34: case 40: case 42: // non-updating byte/halfword loads -> RT
            kill(rt); break;
        case 20: case 21: case 23: // rotates -> RA
        case 24: case 25: case 26: case 27: case 28: case 29: // logical immediates -> RA
            kill(ra); break;
        case 10: case 11: // compare immediate, CR only
            break;
        case 31: {
            const unsigned xo = (w >> 1) & 1023;
            switch (xo) {
            // Whitelist only register-only operations. Killing BOTH RT and RA
            // is intentionally conservative across their two destination layouts.
            case 0: case 8: case 10: case 11: case 19: case 24: case 26: case 28:
            case 32: case 40: case 60: case 75: case 104: case 124: case 136:
            case 138: case 144: case 200: case 202: case 232: case 234: case 235:
            case 266: case 284: case 316: case 339: case 412: case 444: case 459:
            case 476: case 491: case 536: case 792: case 824: case 922: case 954:
                kill(rt); kill(ra); break;
            default: clear(); break;
            }
            break;
        }
        default: clear(); break; // branches, calls, update loads, foreign stores, unknowns
        }
    }
    return replaced;
}
} // namespace NoodsJit
