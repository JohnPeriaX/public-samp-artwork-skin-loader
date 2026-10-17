#include "game/render.h"

#include "core/log.h"
#include "core/samp.h"
#include "game/gta.h"
#include "game/rwmodel.h"
#include "net/artwork.h"
#include "net/dlcompat.h"
#include "net/directdl.h"

#include <windows.h>
#include <urmem.hpp>

#include <cstdint>
#include <cstring>
#include <unordered_map>

namespace render {
namespace {

urmem::hook g_processHook;
urmem::hook g_r3RemoteSpawnHook;
urmem::hook g_r3LocalSpawnHook;

bool BytesMatch(uintptr_t rva, const unsigned char* expected, size_t n) {
    if (!samp::Base()) return false;
    return std::memcmp(reinterpret_cast<const void*>(samp::Addr(rva)), expected, n) == 0;
}

// the swap we applied to one base model's template clump
struct Swap {
    void* original = nullptr; // the game's own template clump for this base
    void* custom = nullptr;   // the custom clump we pointed it at
};
std::unordered_map<int, Swap> g_swapped; // base model id -> swap

uintptr_t GtaDelta() {
    static uintptr_t d = reinterpret_cast<uintptr_t>(GetModuleHandleA(nullptr)) - 0x400000;
    return d;
}

void* ModelInfo(int id) {
    auto arr = reinterpret_cast<void**>(gta::ms_modelInfoPtrs + GtaDelta());
    return arr[id];
}

void** ClumpField(void* mi) {
    return reinterpret_cast<void**>(reinterpret_cast<char*>(mi) + gta::kModelInfo_RwObject);
}

// point a base template at our custom clump, only if a player wears it (limits bleed onto same-base
// peds). the base's own clump is saved once so RestoreAll can put it back
void SwapOne(const artwork::ReadyModel& rm, void* /*ctx*/) {
    void* mi = ModelInfo(rm.baseId);
    if (!mi) return; // base model slot empty
    void** field = ClumpField(mi);

    // already swapped: check the streamer hasn't stolen our clump, so we always notice the loss
    auto it = g_swapped.find(rm.baseId);
    if (it != g_swapped.end()) {
        if (*field == it->second.custom) return; // still installed
        // streamer unloaded the base and destroyed our clump: abandon the dangling pointer, reload,
        // and re-swap next frame
        artwork::OnCustomClumpLost(it->second.custom, /*reload*/ true);
        g_swapped.erase(it);
        return;
    }

    // keep the base resident before it is worn, so the first assignment renders on the first try
    if (!*field) { rwmodel::RequestModel(rm.baseId); return; }

    const bool direct = samp::GetRevision() == samp::Revision::R3_1;
    const bool assigned = direct
        ? directdl::IsCustomAssigned(static_cast<uint32_t>(rm.newId))
        : dlcompat::IsCustomAssigned(static_cast<uint32_t>(rm.newId));
    if (!assigned) return; // loaded, unworn
    if (*field == rm.clump) return; // already ours but unrecorded (rare) - leave it

    g_swapped[rm.baseId] = { *field, rm.clump }; // remember the game's own clump once
    *field = rm.clump;
    CS_LOGI("render: base %d template clump -> custom %d %p", rm.baseId, rm.newId, rm.clump);
}

void Tick() {
    artwork::ForEachReady(&SwapOne, nullptr);
}

int __fastcall Hooked_R3RemoteSpawn(void* self, void* /*edx*/, char a2, int modelId, int a4,
                                    void* position, float rotation, unsigned long color, char style) {
    int baseId = 0;
    const bool custom = directdl::GetBaseModel(modelId, baseId);
    if (custom) {
        const uint16_t playerId = *reinterpret_cast<volatile uint16_t*>(
            reinterpret_cast<uintptr_t>(self) + 0xAB);
        const int customId = modelId;
        directdl::SetPlayerCustom(playerId, static_cast<uint32_t>(customId));
        artwork::Pump();
        EnsureSwapForCustom(static_cast<unsigned>(customId));
        modelId = baseId;
        CS_LOGI("render: R3-1 remote spawn player=%u custom=%d -> base=%d",
                playerId, customId, baseId);
    } else {
        const uint16_t playerId = *reinterpret_cast<volatile uint16_t*>(
            reinterpret_cast<uintptr_t>(self) + 0xAB);
        directdl::ClearPlayerCustom(playerId);
    }
    return g_r3RemoteSpawnHook.call<urmem::calling_convention::thiscall, int>(
        self, a2, modelId, a4, position, rotation, color, style);
}

int __fastcall Hooked_R3LocalSpawn(void* self, void* /*edx*/) {
    int32_t* skin = reinterpret_cast<int32_t*>(reinterpret_cast<uintptr_t>(self) + 0x150);
    const int32_t original = *skin;
    int32_t baseId = 0;
    if (directdl::GetBaseModel(original, baseId)) {
        directdl::SetLocalCustom(static_cast<uint32_t>(original));
        artwork::Pump();
        EnsureSwapForCustom(static_cast<unsigned>(original));
        *skin = baseId;
        const int result = g_r3LocalSpawnHook.call<urmem::calling_convention::thiscall, int>(self);
        *skin = original;
        CS_LOGI("render: R3-1 local spawn custom=%d -> base=%d", original, baseId);
        return result;
    }
    directdl::SetLocalCustom(0);
    return g_r3LocalSpawnHook.call<urmem::calling_convention::thiscall, int>(self);
}

// CNetGame::Process(this), per frame - our main-thread heartbeat: apply downloads, reconcile swaps
void __fastcall Hooked_Process(void* self, void* /*edx*/) {
    g_processHook.call<urmem::calling_convention::thiscall, void>(self);
    artwork::Pump();
    Tick();
}

} // namespace

void RestoreAll() {
    for (auto& kv : g_swapped) {
        void* mi = ModelInfo(kv.first);
        if (!mi) continue;
        void** field = ClumpField(mi);
        if (*field == kv.second.custom) {
            *field = kv.second.original; // put the game's own clump back
        } else {
            // streamer already destroyed our clump: don't clobber the fresh one, and tell artwork to
            // drop the dangling pointer so the upcoming free won't double-free
            artwork::OnCustomClumpLost(kv.second.custom, /*reload*/ false);
        }
    }
    if (!g_swapped.empty())
        CS_LOGI("render: restored %zu base template(s) to their original clump", g_swapped.size());
    g_swapped.clear();
}

void EnsureSwapForCustom(unsigned customId) {
    artwork::ReadyModel rm;
    if (artwork::GetReadyModel(static_cast<int>(customId), rm))
        SwapOne(rm, nullptr); // record original + install our clump before the ped is rebuilt
}

bool Init() {
    if (samp::GetRevision() == samp::Revision::R3_1) {
        static const unsigned char kPoolProcessPrologue[] = { 0x55, 0x8B, 0xEC, 0x6A, 0xFF };
        static const unsigned char kRemoteSpawnPrologue[] = { 0x56, 0x8B, 0xF1 };
        static const unsigned char kLocalSpawnPrologue[] = { 0x64, 0xA1, 0x00, 0x00, 0x00, 0x00 };
        if (!BytesMatch(samp::r3_1::kCPlayerPool_Process, kPoolProcessPrologue, sizeof(kPoolProcessPrologue))
            || !BytesMatch(samp::r3_1::kCRemotePlayer_Spawn, kRemoteSpawnPrologue, sizeof(kRemoteSpawnPrologue))
            || !BytesMatch(samp::r3_1::kCLocalPlayer_Spawn, kLocalSpawnPrologue, sizeof(kLocalSpawnPrologue))) {
            CS_LOGE("render: R3-1 hook prologue mismatch; refusing player/artwork hooks");
            return false;
        }

        g_r3RemoteSpawnHook.install(samp::Addr(samp::r3_1::kCRemotePlayer_Spawn),
                                    urmem::get_func_addr(&Hooked_R3RemoteSpawn),
                                    urmem::hook::type::jmp);
        g_r3LocalSpawnHook.install(samp::Addr(samp::r3_1::kCLocalPlayer_Spawn),
                                   urmem::get_func_addr(&Hooked_R3LocalSpawn),
                                   urmem::hook::type::jmp);
        g_processHook.install(samp::Addr(samp::r3_1::kCPlayerPool_Process),
                              urmem::get_func_addr(&Hooked_Process),
                              urmem::hook::type::jmp);
        CS_LOGI("render: R3-1 hooks installed: CRemotePlayer::Spawn=0x%08X, CLocalPlayer::Spawn=0x%08X, CPlayerPool::Process=0x%08X",
                static_cast<unsigned>(samp::Addr(samp::r3_1::kCRemotePlayer_Spawn)),
                static_cast<unsigned>(samp::Addr(samp::r3_1::kCLocalPlayer_Spawn)),
                static_cast<unsigned>(samp::Addr(samp::r3_1::kCPlayerPool_Process)));
        return true;
    }

    if (!samp::IsR5Build()) {
        CS_LOGE("render: unsupported build; skipping");
        return false;
    }
    g_processHook.install(samp::Addr(samp::r5::kCNetGame_Process),
                          urmem::get_func_addr(&Hooked_Process),
                          urmem::hook::type::jmp);
    CS_LOGI("render: hooked CNetGame::Process @ 0x%08X (per-frame tick)",
            static_cast<unsigned>(samp::Addr(samp::r5::kCNetGame_Process)));
    return true;
}

} // namespace render
