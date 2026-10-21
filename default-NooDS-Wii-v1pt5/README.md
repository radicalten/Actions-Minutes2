# NooDS-Wii v1.70 — Live Runtime `ARM JIT` Toggle (`On` / `Off`) + True-Pitch Direct AI DMA Audio

## Summary of Changes in `v1.70` (`NooDS-Wii-v1pt70/NooDS-Wii.dol`)

`v1.70` builds directly on `v1.69` (preserving true-pitch sub-`60 FPS` audio, 4-slot `48,000 Hz` Direct Hardware AI DMA, cyan-blue stylus selection cursor, `Hide Top Menu` + same-hotkey unhide, live `Audio Emulation` toggle, `32.00 MiB` banked GBA ROM support, `Skip Frames` `None`–`30`, and full controller support) and adds a **live, persistent `ARM JIT` (`On` / `Off`) toggle right on Page 1 of the Settings menu**:

1. **Live Runtime `ARM JIT` Toggle in Settings (`NooDS-Wii/main.cpp`, `NooDS-Wii/settings.h`, `NooDS-Wii/settings.cpp`, `NooDS-Wii/interpreter.cpp`):**
   - Added **`ARM JIT`** (`On` / `Off`, default **`On`**) as Row 3 on Page 1 of the Settings overlay (`Quit Game`, `Hide Top Menu`, **`ARM JIT`**, `Audio Emulation`, `Skip Frames`, `Screen Layout`) — immediately visible without scrolling both in the ROM Browser and in-game (`B+PLUS`, `MINUS+PLUS`, or `B+START`).
   - Toggleable live at any moment (`Left` / `Right` / `A` on Wiimote, Classic Controller, or GameCube pad) with **zero ROM reload required**:
     - **`ARM JIT: On` (`Settings::armJit = 1`, default):** Executes compiled PowerPC JIT blocks for NDS ARM9/ARM7 ARM & THUMB execution (`2.39x` speedup on `suite-poly.nds`).
     - **`ARM JIT: Off` (`Settings::armJit = 0`):** Immediately switches execution cleanly on the next architectural instruction boundary to the pure C++ ARM/THUMB interpreter while keeping all `ArmPpcJit::invalidateRange(...)` / `invalidateCpu(...)` write-tracking hooks active in the background — guaranteeing **zero stale JIT cache entries** if code/overlays are loaded into RAM while `ARM JIT` is `Off` and the user switches `ARM JIT` back `On` mid-game.
   - Persists automatically in `sd:/noods.ini` (`armJit=1` / `armJit=0`).

---

## Verification Summary

### 1. Live Head-to-Head `ARM JIT: On` vs `ARM JIT: Off` Speed & Determinism (`suite-poly.nds`, `frameskip = 0`)

| Mode (`sd:/noods.ini`) | 240-Frame Execution Time | Effective Unskipped 3D FPS | Exact Guest Cycles (`cyc`) | 64-Bit System RAM/VRAM Hash | 64-Bit Framebuffer Hash |
|---|---|---|---|---|---|
| **`ARM JIT = On` (`armJit=1`)** | **`9,556.4 ms`** (`2.39x` faster) | **`25.1 FPS`** | `134,294,370` | `D25A4964D65E337A` | `5F2BAB5D0D10ED9D` |
| **`ARM JIT = Off` (`armJit=0`)** | `22,830.1 ms` (`1.00x` interp) | `10.5 FPS` | `134,294,370` | `D25A4964D65E337A` | `5F2BAB5D0D10ED9D` |

### 2. Live Interactive X11 OpenGL Verification (`docs/v170_*.png`)
- Verified live `60.0 FPS` gameplay (`FPS: 60.10`, `Display: 59.9  Emu: 60 FPS`) with cyan-blue stylus cursor (`docs/v170_1_gameplay_60fps.png`).
- Verified toggling `ARM JIT: On -> Off -> On` live mid-game (`docs/v170_2_settings_arm_jit_on.png`, `docs/v170_3_settings_arm_jit_off.png`), confirming `armJit=0` and `armJit=1` saved cleanly to `sd:/noods.ini` and `515.6 Hz` true-pitch audio across the entire `On -> Off -> On` transition.

### 3. Full `frameskip = 0` Accuracy Suites (`/home/user/test/run_all.sh`)
- **`suite.gba`:** **`6,997 / 6,998` (`99.99%`, `ALL DONE = True`)**
- **`nds-2d-agent-suite.nds`:** **`8 / 8` (`100.00%`, Exact framebuffer match `True`)**
- **`suite-poly.nds`:** **`28 / 28` CPU groups + `77 / 77` 3D scenes (`expectedPx = 102,942`, `missing = 0`, `overdraw = 0`, `wrongCol = 0`)**

## Build Instructions

```bash
export DEVKITPRO=/opt/devkitpro
export DEVKITPPC=/opt/devkitpro/devkitPPC
export PATH="/opt/devkitpro/tools/bin:/opt/devkitpro/devkitPPC/bin:$PATH"
make clean && make -j$(nproc)
```
