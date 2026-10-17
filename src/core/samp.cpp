#include "samp.h"
#include "log.h"

#include <windows.h>
#include <urmem.hpp>

#include <cstdarg>
#include <cstdio>

namespace samp {
namespace {

uintptr_t g_base = 0;
uint32_t g_imageSize = 0;
uint32_t g_timeDateStamp = 0;
uint32_t g_entryPointRva = 0;
Revision g_revision = Revision::Unknown;

bool ReadPe(uintptr_t base) {
    auto dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) return false;
    auto nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(base + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE) return false;
    g_imageSize = nt->OptionalHeader.SizeOfImage;
    g_timeDateStamp = nt->FileHeader.TimeDateStamp;
    g_entryPointRva = nt->OptionalHeader.AddressOfEntryPoint;
    return true;
}

} // namespace

bool Init() {
    if (g_base) return true;
    HMODULE h = GetModuleHandleA("samp.dll");
    if (!h) return false;
    auto base = reinterpret_cast<uintptr_t>(h);
    if (!ReadPe(base)) return false;
    g_base = base;
    if (g_imageSize == r5::kImageSize && g_timeDateStamp == r5::kTimeDateStamp) {
        g_revision = Revision::R5_1;
    } else if (g_entryPointRva == r3_1::kEntryPoint) {
        // Identify the exact R3-1 build by its verified PE entry point.
        g_revision = Revision::R3_1;
    }
    return true;
}

Revision GetRevision() { return g_revision; }

const char* RevisionName() {
    switch (g_revision) {
    case Revision::R3_1: return "SA-MP 0.3.7 R3-1";
    case Revision::R5_1: return "SA-MP 0.3.7 R5-1";
    default: return "Unknown SA-MP build";
    }
}

bool IsSupportedNetworkRevision() {
    return g_revision == Revision::R3_1 || g_revision == Revision::R5_1;
}

uintptr_t Base() { return g_base; }
uint32_t ImageSize() { return g_imageSize; }
uint32_t TimeDateStamp() { return g_timeDateStamp; }

bool IsR5Build() {
    return g_base && g_imageSize == r5::kImageSize && g_timeDateStamp == r5::kTimeDateStamp;
}

void DumpFingerprint() {
    if (!g_base) {
        CS_LOGW("DumpFingerprint: samp.dll not resolved");
        return;
    }
    CS_LOGI("samp.dll base=0x%08X SizeOfImage=0x%X (%u bytes) TimeDateStamp=0x%08X",
            static_cast<unsigned>(g_base), g_imageSize, g_imageSize, g_timeDateStamp);
    CS_LOGI("detected revision: %s (entry point RVA=0x%08X)", RevisionName(),
            static_cast<unsigned>(g_entryPointRva));
    if (IsR5Build())
        CS_LOGI("fingerprint matches known 0.3.7-R5-1 build");
    else if (g_revision == Revision::R3_1)
        CS_LOGI("network mapping matches verified custommodels R3-1 registry");
    else
        CS_LOGW("fingerprint/entry-point not in the supported build registry");

    // These byte probes are R5-only. Never dereference the R5 table on R3-1.
    if (g_revision == Revision::R5_1) {
        auto dump = [](const char* name, uintptr_t rva) {
            const auto* p = reinterpret_cast<const uint8_t*>(Addr(rva));
            CS_LOGI("  @%-28s rva 0x%06X : %02X %02X %02X %02X %02X %02X %02X %02X",
                    name, static_cast<unsigned>(rva),
                    p[0], p[1], p[2], p[3], p[4], p[5], p[6], p[7]);
        };
        dump("CChat::AddMessage", r5::kCChat_AddMessage);
        dump("CNetGame::GetRakClient", r5::kCNetGame_GetRakClient);
        dump("CNetGame::Packet_ConnSucceeded", r5::kCNetGame_Packet_ConnectionSucceeded);
        CS_LOGI("  RefChat=0x%08X RefNetGame=0x%08X",
                *reinterpret_cast<uint32_t*>(Addr(r5::kRefChat)),
                *reinterpret_cast<uint32_t*>(Addr(r5::kRefNetGame)));
    }
}

bool DebugChat(const char* fmt, ...) {
    char buf[512];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    buf[sizeof(buf) - 1] = '\0';

    // Chat offsets are R5-only in this project. R3-1 must never touch the R5 table.
    if (!g_base || g_revision != Revision::R5_1) return false;
    void* chat = *reinterpret_cast<void**>(Addr(r5::kRefChat));
    if (!chat) return false;

    // CChat::AddMessage(this, D3DCOLOR color, const char* text)  thiscall
    urmem::call_function<urmem::calling_convention::thiscall, void>(
        Addr(r5::kCChat_AddMessage), chat, static_cast<unsigned long>(0xFFFFFFFF), buf);
    CS_LOGI("[chat] %s", buf);
    return true;
}

} // namespace samp
