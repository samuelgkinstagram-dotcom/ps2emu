#pragma once
#include "ps2types.h"

// ─── Memory Bus ────────────────────────────────────────────────────────────────
// Handles: Main RAM, BIOS, Scratchpad, IOP RAM, MMIO, RDRAM mirrors
// Optimized: direct pointer fast-path for RAM/BIOS reads (no branch per byte)
class MemBus {
public:
    // Physical memory regions
    std::vector<u8> ram;        // 32 MB EE Main RAM
    std::vector<u8> bios;       //  4 MB BIOS
    std::vector<u8> scratchpad; // 16 KB Scratchpad
    std::vector<u8> iopRam;     //  2 MB IOP RAM
    std::vector<u8> vram;       //  4 MB GS VRAM (local)

    // MMIO callbacks
    using ReadCB  = std::function<u32(u32)>;
    using WriteCB = std::function<void(u32,u32)>;
    struct MMIORegion { u32 base, size; ReadCB read; WriteCB write; };
    std::vector<MMIORegion> mmioRegions;

    bool biosLoaded = false;

    MemBus() {
        ram.assign(PS2_RAM_SIZE, 0);
        bios.assign(PS2_BIOS_SIZE, 0);
        scratchpad.assign(PS2_SCRATCHPAD, 0);
        iopRam.assign(PS2_IOP_RAM_SIZE, 0);
        vram.assign(PS2_VRAM_SIZE, 0);
    }

    bool loadBios(const std::string& path) {
        std::ifstream f(path, std::ios::binary);
        if (!f) return false;
        f.read((char*)bios.data(), PS2_BIOS_SIZE);
        biosLoaded = f.gcount() > 0;
        return biosLoaded;
    }

    void registerMMIO(u32 base, u32 size, ReadCB r, WriteCB w) {
        mmioRegions.push_back({base, size, r, w});
    }

    // ── Address translation (virtual → physical) ──────────────────────────────
    static u32 translate(u32 vaddr) {
        // KSEG0/KSEG1 → strip top 3 bits
        if ((vaddr >> 29) == 4 || (vaddr >> 29) == 5)
            return vaddr & 0x1FFFFFFF;
        return vaddr & 0x1FFFFFFF;
    }

    // ── Read helpers ──────────────────────────────────────────────────────────
    u8  read8 (u32 addr) { return readN<u8> (addr); }
    u16 read16(u32 addr) { return readN<u16>(addr); }
    u32 read32(u32 addr) { return readN<u32>(addr); }
    u64 read64(u32 addr) { return readN<u64>(addr); }

    void write8 (u32 addr, u8  v) { writeN<u8> (addr, v); }
    void write16(u32 addr, u16 v) { writeN<u16>(addr, v); }
    void write32(u32 addr, u32 v) { writeN<u32>(addr, v); }
    void write64(u32 addr, u64 v) { writeN<u64>(addr, v); }

    // 128-bit (for EE LQ/SQ instructions)
    Reg128 read128(u32 addr) {
        Reg128 r;
        r.u64v[0] = read64(addr);
        r.u64v[1] = read64(addr+8);
        return r;
    }
    void write128(u32 addr, const Reg128& v) {
        write64(addr,   v.u64v[0]);
        write64(addr+8, v.u64v[1]);
    }

private:
    template<typename T>
    T readN(u32 vaddr) {
        u32 paddr = translate(vaddr);
        // ── Fast paths ────────────────────────────────────────────────────────
        if (paddr < PS2_RAM_SIZE)
            return load<T>(ram.data() + paddr);
        if (paddr >= 0x1FC00000 && paddr < 0x20000000)
            return load<T>(bios.data() + (paddr - 0x1FC00000));
        if (paddr >= 0x70000000 && paddr < 0x70004000)
            return load<T>(scratchpad.data() + (paddr - 0x70000000));
        // ── MMIO ─────────────────────────────────────────────────────────────
        for (auto& r : mmioRegions) {
            if (paddr >= r.base && paddr < r.base + r.size)
                return (T)r.read(paddr);
        }
        // open bus
        return 0;
    }

    template<typename T>
    void writeN(u32 vaddr, T v) {
        u32 paddr = translate(vaddr);
        if (paddr < PS2_RAM_SIZE) {
            store<T>(ram.data() + paddr, v); return;
        }
        if (paddr >= 0x70000000 && paddr < 0x70004000) {
            store<T>(scratchpad.data() + (paddr - 0x70000000), v); return;
        }
        for (auto& r : mmioRegions) {
            if (paddr >= r.base && paddr < r.base + r.size) {
                r.write(paddr, (u32)v); return;
            }
        }
    }

    template<typename T> static T    load (const u8* p) { T v; memcpy(&v,p,sizeof(T)); return v; }
    template<typename T> static void store(u8* p, T v)  { memcpy(p,&v,sizeof(T)); }
};

// ─── DMA Controller (DMAC) ────────────────────────────────────────────────────
// Handles memory→peripheral transfers without burdening EE CPU
struct DMAChannel {
    u32 madr = 0;   // Memory address
    u32 qwc  = 0;   // Quad-word count
    u32 chcr = 0;   // Control
    bool active() const { return (chcr & 0x100) != 0; }
};

class DMAC {
public:
    static constexpr int NUM_CH = 10;
    std::array<DMAChannel, NUM_CH> ch;
    u32 ctrl = 0, stat = 0, pcr = 0;
    MemBus* bus = nullptr;

    void reset() {
        for (auto& c : ch) c = {};
        ctrl = stat = pcr = 0;
    }

    u32 readReg(u32 addr) {
        u32 idx = (addr >> 8) & 0xF;
        u32 off = addr & 0xFF;
        if (idx < NUM_CH) {
            if (off == 0x00) return ch[idx].chcr;
            if (off == 0x10) return ch[idx].madr;
            if (off == 0x20) return ch[idx].qwc;
        }
        if (addr == 0x1000E000) return ctrl;
        if (addr == 0x1000E010) return stat;
        return 0;
    }

    void writeReg(u32 addr, u32 v) {
        u32 idx = (addr >> 8) & 0xF;
        u32 off = addr & 0xFF;
        if (idx < NUM_CH) {
            if (off == 0x00) { ch[idx].chcr = v; if (v & 0x100) startTransfer(idx); }
            if (off == 0x10) ch[idx].madr = v;
            if (off == 0x20) ch[idx].qwc  = v;
        }
        if (addr == 0x1000E000) ctrl = v;
    }

    void startTransfer(int idx) {
        // Basic memory-fill / clear DMA (used heavily by PS2 games for VRAM upload)
        auto& c = ch[idx];
        // Just mark done — real implementation would transfer data to GS/SPU2
        c.chcr &= ~0x100;
        stat |= (1 << idx);   // raise IRQ for this channel
    }
};
