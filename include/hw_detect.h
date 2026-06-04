#pragma once
#include "ps2types.h"
#include <thread>
#include <fstream>
#include <sstream>

#ifdef _WIN32
#  include <windows.h>
#  include <psapi.h>
#endif

// ─── Detecta hardware e escolhe perfil automaticamente ────────────────────────
// Celeron + 2GB + sem GPU → POTATO
// Se errar pra cima, o usuário percebe travamento e pode baixar manualmente
struct HardwareDetector {

    struct Info {
        u64  ramTotalMB  = 0;
        u64  ramFreeMB   = 0;
        int  cpuCores    = 1;
        bool hasSSE2     = false;
        bool hasSSE4     = false;
        bool hasDedGPU   = false;   // GPU dedicada (não Intel HD / não APU)
        std::string cpuName;
        std::string gpuName;
    };

    static Info detect() {
        Info i;
        i.cpuCores = (int)std::thread::hardware_concurrency();
        if (i.cpuCores < 1) i.cpuCores = 1;
        detectCPU(i);
        detectRAM(i);
        return i;
    }

    // Escolhe perfil baseado no hardware detectado
    static EmulatorConfig::Profile chooseProfile(const Info& i) {
        // < 2 GB RAM → POTATO independente da CPU
        if (i.ramTotalMB < 1800) return EmulatorConfig::POTATO;
        // Sem SSE2 → PC muito antigo, POTATO
        if (!i.hasSSE2)          return EmulatorConfig::POTATO;
        // 1 núcleo, sem GPU dedicada → POTATO
        if (i.cpuCores <= 1 && !i.hasDedGPU) return EmulatorConfig::POTATO;
        // 2 núcleos, sem GPU dedicada, < 4 GB → LOW
        if (i.cpuCores <= 2 && !i.hasDedGPU && i.ramTotalMB < 3800)
            return EmulatorConfig::LOW;
        // 4 núcleos sem GPU ou 2 núcleos com GPU → MEDIUM
        if (!i.hasDedGPU || i.cpuCores <= 2) return EmulatorConfig::MEDIUM;
        return EmulatorConfig::HIGH;
    }

    // Aplica perfil na config
    static void applyProfile(EmulatorConfig& cfg, EmulatorConfig::Profile p) {
        cfg.profile = p;
        switch (p) {
        case EmulatorConfig::POTATO:
            // ── TUDO no mínimo ─ seu cenário: Celeron + 2GB + sem GPU ─────────
            cfg.renderWidth     = 512;
            cfg.renderHeight    = 384;
            cfg.targetFPS       = 30;
            cfg.skipFrames      = true;
            cfg.frameSkipMax    = 3;
            cfg.vsync           = false;
            cfg.interlace       = false;
            cfg.halfResolution  = true;
            cfg.cpuThreads      = 1;
            cfg.jitEnabled      = false;
            cfg.audioSampleRate = 22050;
            cfg.audioBufferMs   = 120;
            cfg.audioInterp     = false;
            cfg.ramCacheKB      = 256;
            cfg.lazyAlloc       = true;
            break;

        case EmulatorConfig::LOW:
            cfg.renderWidth     = 640;
            cfg.renderHeight    = 448;
            cfg.targetFPS       = 30;
            cfg.skipFrames      = true;
            cfg.frameSkipMax    = 2;
            cfg.vsync           = false;
            cfg.interlace       = false;
            cfg.halfResolution  = false;
            cfg.cpuThreads      = 2;
            cfg.jitEnabled      = false;
            cfg.audioSampleRate = 32000;
            cfg.audioBufferMs   = 80;
            cfg.audioInterp     = false;
            cfg.ramCacheKB      = 512;
            cfg.lazyAlloc       = true;
            break;

        case EmulatorConfig::MEDIUM:
            cfg.renderWidth     = 640;
            cfg.renderHeight    = 448;
            cfg.targetFPS       = 60;
            cfg.skipFrames      = true;
            cfg.frameSkipMax    = 1;
            cfg.vsync           = true;
            cfg.interlace       = false;
            cfg.halfResolution  = false;
            cfg.cpuThreads      = 2;
            cfg.jitEnabled      = false;
            cfg.audioSampleRate = 44100;
            cfg.audioBufferMs   = 50;
            cfg.audioInterp     = true;
            cfg.ramCacheKB      = 1024;
            cfg.lazyAlloc       = false;
            break;

        case EmulatorConfig::HIGH:
            cfg.renderWidth     = 1280;
            cfg.renderHeight    = 896;
            cfg.targetFPS       = 60;
            cfg.skipFrames      = false;
            cfg.frameSkipMax    = 0;
            cfg.vsync           = true;
            cfg.interlace       = true;
            cfg.halfResolution  = false;
            cfg.cpuThreads      = 4;
            cfg.jitEnabled      = true;
            cfg.audioSampleRate = 48000;
            cfg.audioBufferMs   = 32;
            cfg.audioInterp     = true;
            cfg.ramCacheKB      = 4096;
            cfg.lazyAlloc       = false;
            break;
        }
    }

    static std::string profileName(EmulatorConfig::Profile p) {
        switch(p) {
        case EmulatorConfig::POTATO: return "POTATO (PC muito fraco)";
        case EmulatorConfig::LOW:    return "LOW (PC basico)";
        case EmulatorConfig::MEDIUM: return "MEDIUM (PC intermediario)";
        case EmulatorConfig::HIGH:   return "HIGH (PC gamer)";
        }
        return "?";
    }

private:
    static void detectRAM(Info& i) {
#ifdef _WIN32
        MEMORYSTATUSEX ms; ms.dwLength = sizeof(ms);
        if (GlobalMemoryStatusEx(&ms)) {
            i.ramTotalMB = ms.ullTotalPhys  / (1024*1024);
            i.ramFreeMB  = ms.ullAvailPhys  / (1024*1024);
        }
#else
        // Linux: lê /proc/meminfo
        std::ifstream f("/proc/meminfo");
        std::string line;
        while (std::getline(f, line)) {
            if (line.find("MemTotal:") != std::string::npos) {
                u64 kb = 0; sscanf(line.c_str(), "MemTotal: %llu kB", &kb);
                i.ramTotalMB = kb / 1024;
            }
            if (line.find("MemAvailable:") != std::string::npos) {
                u64 kb = 0; sscanf(line.c_str(), "MemAvailable: %llu kB", &kb);
                i.ramFreeMB = kb / 1024;
            }
        }
#endif
    }

    static void detectCPU(Info& i) {
#if defined(__SSE2__) || defined(_M_X64)
        i.hasSSE2 = true;
#endif
#if defined(__SSE4_1__)
        i.hasSSE4 = true;
#endif
#ifdef _WIN32
        HKEY hKey;
        if (RegOpenKeyExA(HKEY_LOCAL_MACHINE,
            "HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0",
            0, KEY_READ, &hKey) == ERROR_SUCCESS) {
            char buf[256] = {}; DWORD sz = sizeof(buf);
            RegQueryValueExA(hKey, "ProcessorNameString", nullptr, nullptr,
                             (LPBYTE)buf, &sz);
            i.cpuName = buf;
            RegCloseKey(hKey);
        }
#else
        std::ifstream f("/proc/cpuinfo");
        std::string line;
        while (std::getline(f, line)) {
            if (line.find("model name") != std::string::npos) {
                auto pos = line.find(':');
                if (pos != std::string::npos)
                    i.cpuName = line.substr(pos+2);
                break;
            }
        }
#endif
        // Heurística: se o nome contém "Celeron", "Atom", "Pentium" → sem GPU dedicada
        std::string low = i.cpuName;
        for (auto& c : low) c = tolower(c);
        i.hasDedGPU = !(low.find("celeron") != std::string::npos ||
                        low.find("atom")    != std::string::npos ||
                        low.find("pentium") != std::string::npos);
    }
};
