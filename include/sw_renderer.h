#pragma once
#include "ps2types.h"
// SDL2 surface only - zero OpenGL, zero DirectX
// Funciona em qualquer PC com Intel HD, GMA 950, ou sem GPU dedicada
#include <SDL2/SDL.h>
#include <cstring>
#include <algorithm>

// ─── GS Primitive Types ───────────────────────────────────────────────────────
enum class GSPrim : u8 {
    POINT=0, LINE=1, LINESTRIP=2, TRI=3, TRISTRIP=4, TRIFAN=5, SPRITE=6
};

struct GSVertex {
    s32 x, y, z;       // fixed-point coords (1/16 pixel)
    u32 rgba;           // packed RGBA8
    f32 u, v;           // texture coords
    f32 q;              // perspective divide
};

struct GSTexInfo {
    u8*  data    = nullptr;
    u32  width   = 0;
    u32  height  = 0;
    bool enabled = false;
};

// ─── Software Rasterizer ──────────────────────────────────────────────────────
// Todas as operações em CPU - sem GPU necessária
class SoftwareRenderer {
public:
    SDL_Surface*  surface  = nullptr;   // framebuffer principal
    u32*          pixels   = nullptr;   // acesso direto ao framebuffer
    u32           fbWidth  = 512;
    u32           fbHeight = 384;
    bool          halfRes  = true;      // renderiza em 256x192 e upscale 2x

    // GS state
    u32  clearColor = 0xFF000000;
    bool alphaBlend = false;
    bool depthTest  = true;
    GSTexInfo tex;

    // Stats
    u64 trianglesDrawn = 0;
    u64 pixelsDrawn    = 0;

    bool init(u32 w, u32 h, bool half) {
        fbWidth = w; fbHeight = h; halfRes = half;
        u32 rw = half ? w/2 : w;
        u32 rh = half ? h/2 : h;
        surface = SDL_CreateRGBSurfaceWithFormat(0, rw, rh, 32,
                    SDL_PIXELFORMAT_ARGB8888);
        if (!surface) return false;
        pixels = (u32*)surface->pixels;
        zbuf.assign(rw * rh, 0xFFFFFFFF);
        return true;
    }

    void shutdown() {
        if (surface) { SDL_FreeSurface(surface); surface = nullptr; }
    }

    void clear() {
        u32 rw = halfRes ? fbWidth/2 : fbWidth;
        u32 rh = halfRes ? fbHeight/2 : fbHeight;
        u32 n = rw * rh;
        for (u32 i = 0; i < n; ++i) pixels[i] = clearColor;
        zbuf.assign(n, 0xFFFFFFFF);
    }

    // ── Draw Sprite (rect) — operação mais comum no PS2 ─────────────────────
    void drawSprite(const GSVertex& v0, const GSVertex& v1) {
        u32 rw = halfRes ? fbWidth/2 : fbWidth;
        u32 rh = halfRes ? fbHeight/2 : fbHeight;
        f32 scale = halfRes ? 0.5f : 1.0f;

        s32 x0 = std::max(0, (s32)(v0.x * scale / 16));
        s32 y0 = std::max(0, (s32)(v0.y * scale / 16));
        s32 x1 = std::min((s32)rw, (s32)(v1.x * scale / 16));
        s32 y1 = std::min((s32)rh, (s32)(v1.y * scale / 16));

        for (s32 y = y0; y < y1; ++y) {
            for (s32 x = x0; x < x1; ++x) {
                u32 idx = y * rw + x;
                if (tex.enabled && tex.data) {
                    // Nearest-neighbor texture (mais rápido que bilinear)
                    f32 fu = (f32)(x - x0) / (f32)(x1 - x0);
                    f32 fv = (f32)(y - y0) / (f32)(y1 - y0);
                    u32 tx = (u32)(fu * (tex.width  - 1)) % tex.width;
                    u32 ty = (u32)(fv * (tex.height - 1)) % tex.height;
                    u32 tc; memcpy(&tc, tex.data + (ty*tex.width+tx)*4, 4);
                    pixels[idx] = blendPixel(tc, v0.rgba);
                } else {
                    pixels[idx] = v0.rgba;
                }
                ++pixelsDrawn;
            }
        }
    }

    // ── Draw Triangle (flat shading, sem interpolação de cor por vértice) ────
    void drawTriangle(const GSVertex& a, const GSVertex& b, const GSVertex& c) {
        u32 rw = halfRes ? fbWidth/2 : fbWidth;
        u32 rh = halfRes ? fbHeight/2 : fbHeight;
        f32 sc = halfRes ? 0.5f : 1.0f;

        // Coordenadas em pixels
        s32 ax=(s32)(a.x*sc/16), ay=(s32)(a.y*sc/16);
        s32 bx=(s32)(b.x*sc/16), by=(s32)(b.y*sc/16);
        s32 cx=(s32)(c.x*sc/16), cy=(s32)(c.y*sc/16);

        s32 ymin = std::max(0,        std::min({ay,by,cy}));
        s32 ymax = std::min((s32)rh-1, std::max({ay,by,cy}));

        // Scanline rasterization (simples, funciona em CPU fraca)
        for (s32 y = ymin; y <= ymax; ++y) {
            s32 xmin_s = (s32)rw, xmax_s = 0;
            scanEdge(ax,ay,bx,by, y, xmin_s, xmax_s);
            scanEdge(bx,by,cx,cy, y, xmin_s, xmax_s);
            scanEdge(cx,cy,ax,ay, y, xmin_s, xmax_s);
            xmin_s = std::max(0, xmin_s);
            xmax_s = std::min((s32)rw-1, xmax_s);
            for (s32 x = xmin_s; x <= xmax_s; ++x) {
                u32 idx = y * rw + x;
                pixels[idx] = alphaBlend ? blendPixel(a.rgba, pixels[idx]) : a.rgba;
                ++pixelsDrawn;
            }
        }
        ++trianglesDrawn;
    }

    // ── Blit para janela SDL (com upscale 2x se halfRes) ─────────────────────
    void blitTo(SDL_Surface* dst) {
        if (!surface || !dst) return;
        if (halfRes) {
            // Upscale 2x por nearest-neighbor (muito rápido, sem interpolação)
            SDL_Rect dstRect { 0, 0, (int)fbWidth, (int)fbHeight };
            SDL_BlitScaled(surface, nullptr, dst, &dstRect);
        } else {
            SDL_BlitSurface(surface, nullptr, dst, nullptr);
        }
    }

private:
    std::vector<u32> zbuf;

    static u32 blendPixel(u32 src, u32 dst) {
        u8 sa = (src >> 24) & 0xFF;
        if (sa == 0xFF) return src;
        if (sa == 0x00) return dst;
        u8 sr=(src>>16)&0xFF, sg=(src>>8)&0xFF, sb=src&0xFF;
        u8 dr=(dst>>16)&0xFF, dg=(dst>>8)&0xFF, db=dst&0xFF;
        u8 a = sa;
        u8 r = (u8)((sr*a + dr*(255-a)) >> 8);
        u8 g = (u8)((sg*a + dg*(255-a)) >> 8);
        u8 b = (u8)((sb*a + db*(255-a)) >> 8);
        return 0xFF000000 | (r<<16) | (g<<8) | b;
    }

    static void scanEdge(s32 x0,s32 y0,s32 x1,s32 y1, s32 y,
                         s32& xmin, s32& xmax) {
        if (y0 == y1) return;
        if (y < std::min(y0,y1) || y > std::max(y0,y1)) return;
        s32 x = x0 + (x1-x0) * (y-y0) / (y1-y0);
        xmin = std::min(xmin, x);
        xmax = std::max(xmax, x);
    }
};
