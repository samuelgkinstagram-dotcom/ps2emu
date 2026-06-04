#pragma once
#include <cstdint>
#include <cstring>
#include <array>
#include <vector>
#include <string>
#include <functional>
#include <memory>
#include <fstream>
#include <iostream>
#include <chrono>
#include <thread>
#include <atomic>
#include <mutex>
#include <cassert>

// ─── Basic Types ───────────────────────────────────────────────────────────────
using u8  = uint8_t;
using u16 = uint16_t;
using u32 = uint32_t;
using u64 = uint64_t;
using s8  = int8_t;
using s16 = int16_t;
using s32 = int32_t;
using s64 = int64_t;
using f32 = float;
using f64 = double;

// ─── PS2 Memory Map ────────────────────────────────────────────────────────────
constexpr u32 PS2_RAM_SIZE       = 32 * 1024 * 1024;  // 32 MB Main RAM
constexpr u32 PS2_BIOS_SIZE      =  4 * 1024 * 1024;  //  4 MB BIOS ROM
constexpr u32 PS2_SCRATCHPAD     = 16 * 1024;          // 16 KB Scratchpad
constexpr u32 PS2_SPR_SIZE       = 16 * 1024;          // 16 KB SPR
constexpr u32 PS2_VRAM_SIZE      =  4 * 1024 * 1024;  //  4 MB VRAM (GS)
constexpr u32 PS2_IOP_RAM_SIZE   =  2 * 1024 * 1024;  //  2 MB IOP RAM
constexpr u32 PS2_RDRAM_BASE     = 0x00000000;
constexpr u32 PS2_BIOS_BASE      = 0x1FC00000;
constexpr u32 PS2_SCRATCH_BASE   = 0x70000000;
constexpr u32 PS2_IO_BASE        = 0x10000000;
constexpr u32 PS2_GS_BASE        = 0x12000000;

// ─── EE CPU Constants ──────────────────────────────────────────────────────────
constexpr u32 EE_CLOCK_HZ    = 294'912'000;  // 294.912 MHz
constexpr u32 IOP_CLOCK_HZ   =  36'864'000;  //  36.864 MHz
constexpr u32 GS_CLOCK_HZ    = 147'456'000;  // 147.456 MHz
constexpr u32 CYCLES_PER_FRAME_NTSC = EE_CLOCK_HZ / 60;
constexpr u32 CYCLES_PER_FRAME_PAL  = EE_CLOCK_HZ / 50;

// ─── MIPS R5900 Register File ──────────────────────────────────────────────────
struct alignas(16) Reg128 {
    union {
        u8  u8v[16];
        u16 u16v[8];
        u32 u32v[4];
        u64 u64v[2];
        s8  s8v[16];
        s16 s16v[8];
        s32 s32v[4];
        s64 s64v[2];
    };
    Reg128() { memset(this, 0, sizeof(*this)); }
};

// ─── Emulator Config ───────────────────────────────────────────────────────────
struct EmulatorConfig {
    // ── Video ── (padrão ultra-leve para Celeron / Intel HD / sem GPU)
    u32 renderWidth      = 512;    // abaixo do nativo PS2 (640) pra poupar CPU
    u32 renderHeight     = 384;    // idem
    u32 targetFPS        = 30;     // 30 fps é alcançável em Celeron; 60 só se CPU aguentar
    bool skipFrames      = true;   // LIGADO: descarta frames se CPU atrasar
    u32 frameSkipMax     = 3;      // descarta até 3 frames seguidos antes de forçar render
    bool vsync           = false;  // DESLIGADO: vsync consome CPU extra desnecessariamente
    bool interlace       = false;  // DESLIGADO: desentrelaçamento é caro em CPU fraca
    bool halfResolution  = true;   // LIGADO: renderiza em metade e faz upscale barato
    // ── CPU ──
    u32 cpuThreads       = 1;      // Celeron é single-core efetivo; mais threads = overhead
    bool jitEnabled      = false;  // JIT desligado: mais seguro, menos RAM, menos crash
    bool fastBoot        = true;   // pula intro da BIOS (economiza ~3s de CPU)
    bool eeOverclock     = false;  // DESLIGADO: overclock virtual aumenta uso de CPU real
    // ── Memória ── total alvo: < 350 MB RAM
    bool scratchpadEmu   = true;
    u32  ramCacheKB      = 256;    // cache interno limitado a 256 KB (não 1+ MB)
    bool lazyAlloc       = true;   // aloca memória só quando necessário
    // ── Áudio ──
    bool audioEnabled    = true;
    u32  audioSampleRate = 22050;  // 22 KHz em vez de 44 KHz: metade do trabalho da SPU2
    u32  audioBufferMs   = 100;    // buffer maior = menos stuttering em CPU fraca
    bool audioInterp     = false;  // interpolação de áudio DESLIGADA (caro)
    // ── Paths ──
    std::string biosPath    = "bios/SCPH-70012.bin";
    std::string memCardPath = "saves/";
    std::string savePath    = "states/";
    // ── Perfil de desempenho (auto-detectado na inicialização) ──
    // POTATO = Celeron/Atom + Intel HD + 2 GB RAM  ← SEU PC
    // LOW    = Core2Duo + Intel HD + 2–4 GB
    // MEDIUM = i3/Ryzen3 + GT 730 + 4 GB
    // HIGH   = i5+ + GTX 1050+ + 8 GB
    enum Profile { POTATO=0, LOW=1, MEDIUM=2, HIGH=3 } profile = POTATO;
};

// ─── Frame timing ──────────────────────────────────────────────────────────────
struct FrameTimer {
    using Clock = std::chrono::steady_clock;
    using TP    = Clock::time_point;
    TP   frameStart;
    f64  targetMs = 1000.0 / 60.0;
    f64  actualFps = 0.0;
    u64  frameCount = 0;
    u32  skipCounter = 0;

    void begin()  { frameStart = Clock::now(); }
    f64  elapsedMs() const {
        return std::chrono::duration<f64, std::milli>(Clock::now() - frameStart).count();
    }
    bool shouldSkip(u32 maxSkip) {
        if (skipCounter >= maxSkip) { skipCounter=0; return false; }
        if (elapsedMs() > targetMs * 1.4) { ++skipCounter; return true; }
        skipCounter = 0; return false;
    }
    void end() {
        f64 e = elapsedMs();
        actualFps = e > 0 ? 1000.0/e : 60.0;
        ++frameCount;
        // sleep remainder
        f64 remain = targetMs - e;
        if (remain > 1.0)
            std::this_thread::sleep_for(std::chrono::milliseconds((s64)remain - 1));
    }
};
