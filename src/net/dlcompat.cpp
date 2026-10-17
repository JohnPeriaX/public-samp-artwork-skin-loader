#include "net/dlcompat.h"

#include "core/log.h"
#include "core/samp.h"
#include "game/render.h"
#include "net/masquerade.h"
#include "net/directdl.h"
#include "net/artwork.h"

#include <windows.h>
#include <urmem.hpp>
#include <cstring>
#include <raknet/rakclient.h> // RPCParameters

#include <unordered_map>

namespace dlcompat {
namespace {

urmem::hook g_skinHook;
urmem::hook g_r3WorldPlayerAddHook;
urmem::hook g_r3SetPlayerSkinHook;
urmem::hook g_r3SetSpawnInfoHook;
std::unordered_map<uint16_t, uint32_t> g_playerCustomSkin; // playerId -> custom skin id

// Rewrite a 0.3DL SetPlayerSkin payload to the 0.3.7 layout and capture the custom id.
// dl wire: u16 PlayerID, u32 base skin, u32 custom skin (80 bits).
// R3-1 handler expects: u32 PlayerID, u32 skin (64 bits).
void __cdecl Hooked_R3SetPlayerSkin(RPCParameters* p) {
    if (p && p->input && p->numberOfBitsOfData >= 80) {
        unsigned char* d = p->input;
        const uint16_t playerId = *reinterpret_cast<uint16_t*>(d + 0);
        const uint32_t skin = *reinterpret_cast<uint32_t*>(d + 2);
        const uint32_t customSkin = *reinterpret_cast<uint32_t*>(d + 6);

        if (customSkin != 0) {
            g_playerCustomSkin[playerId] = customSkin;
            directdl::SetPlayerCustom(playerId, customSkin);
            render::EnsureSwapForCustom(customSkin);
            CS_LOGI("dlcompat: R3-1 SetPlayerSkin player %u -> custom %u (base %u)",
                    playerId, customSkin, skin);
        } else {
            g_playerCustomSkin.erase(playerId);
            directdl::ClearPlayerCustom(playerId);
        }

        *reinterpret_cast<uint32_t*>(d + 0) = playerId;
        *reinterpret_cast<uint32_t*>(d + 4) = skin;
        p->numberOfBitsOfData = 64;
    }
    g_r3SetPlayerSkinHook.call<urmem::calling_convention::cdeclcall, void>(p);
}

// Rewrite 0.3DL WorldPlayerAdd by removing the u32 CustomSkin inserted after Skin.
// The remaining fields are byte-for-byte identical to the R3-1 parser's expected layout.
void __cdecl Hooked_R3WorldPlayerAdd(RPCParameters* p) {
    if (p && p->input && p->numberOfBitsOfData >= 54u * 8u) {
        unsigned char* d = p->input;
        const uint16_t playerId = *reinterpret_cast<uint16_t*>(d + 0);
        const uint32_t skin = *reinterpret_cast<uint32_t*>(d + 3);
        const uint32_t customSkin = *reinterpret_cast<uint32_t*>(d + 7);
        const unsigned bytes = BITS_TO_BYTES(p->numberOfBitsOfData);

        if (customSkin != 0) {
            g_playerCustomSkin[playerId] = customSkin;
            directdl::SetPlayerCustom(playerId, customSkin);
            render::EnsureSwapForCustom(customSkin);
            CS_LOGI("dlcompat: R3-1 WorldPlayerAdd player %u -> custom %u (base %u)",
                    playerId, customSkin, skin);
        } else {
            g_playerCustomSkin.erase(playerId);
            directdl::ClearPlayerCustom(playerId);
        }

        if (bytes >= 11) {
            std::memmove(d + 7, d + 11, bytes - 11);
            *reinterpret_cast<uint32_t*>(d + 3) = skin;
            p->numberOfBitsOfData -= 32;
        }
    }
    g_r3WorldPlayerAddHook.call<urmem::calling_convention::cdeclcall, void>(p);
}

// Rewrite 0.3DL SetSpawnInfo by removing the u32 CustomSkin after Skin.
// RPC 68 starts with a u8 team field, then a byte-aligned u32 skin and u32 custom skin.
// Require the full 50-byte 0.3DL structure before rewriting, so legacy/short payloads pass through.
void __cdecl Hooked_R3SetSpawnInfo(RPCParameters* p) {
    if (p && p->input && p->numberOfBitsOfData >= 50u * 8u) {
        unsigned char* d = p->input;
        uint32_t skin = 0;
        uint32_t customSkin = 0;
        std::memcpy(&skin, d + 1, sizeof(skin));
        std::memcpy(&customSkin, d + 5, sizeof(customSkin));

        // Only custom-skin IDs in the supported registry are valid rewrite candidates.
        int32_t baseModel = 0;
        const bool knownCustom = customSkin >= 20000u && customSkin <= 30000u &&
                                 directdl::GetBaseModel(static_cast<int32_t>(customSkin), baseModel);

        if (knownCustom) {
            directdl::SetLocalCustom(customSkin);
            render::EnsureSwapForCustom(customSkin);
            CS_LOGI("dlcompat: R3-1 SetSpawnInfo local -> custom %u (base %u)",
                    customSkin, skin);
        } else {
            directdl::SetLocalCustom(0);
            CS_LOGW("dlcompat: R3-1 SetSpawnInfo custom field rejected (value=%u skin=%u bits=%u)",
                    customSkin, skin, p->numberOfBitsOfData);
        }

        const unsigned bytes = BITS_TO_BYTES(p->numberOfBitsOfData);
        if (bytes >= 9) {
            std::memmove(d + 5, d + 9, bytes - 9);
            std::memcpy(d + 1, &skin, sizeof(skin));
            p->numberOfBitsOfData -= 32;
        }
    }
    g_r3SetSpawnInfoHook.call<urmem::calling_convention::cdeclcall, void>(p);
}

// ScrSetPlayerSkin(RPCParameters*), __cdecl. dl wire: u16 PlayerID, u32 Skin, u32 CustomSkin (80
// bits); 0.3.7 wire: u32 PlayerID, u32 skin (64 bits).
void __cdecl Hooked_ScrSetPlayerSkin(RPCParameters* p) {
    if (masq::Enabled() && p && p->input && p->numberOfBitsOfData >= 80) {
        unsigned char* d = p->input;
        uint16_t playerId = *reinterpret_cast<uint16_t*>(d + 0);
        uint32_t skin = *reinterpret_cast<uint32_t*>(d + 2);
        uint32_t customSkin = *reinterpret_cast<uint32_t*>(d + 6);

        if (customSkin != 0) {
            g_playerCustomSkin[playerId] = customSkin;
            directdl::SetPlayerCustom(playerId, customSkin);
            CS_LOGI("dlcompat: SetPlayerSkin player %u -> custom %u (base %u)",
                    playerId, customSkin, skin);
        } else {
            g_playerCustomSkin.erase(playerId);
            directdl::ClearPlayerCustom(playerId);
        }

        // Rewrite in place to the 0.3.7 layout so SA-MP applies the valid base skin.
        *reinterpret_cast<uint32_t*>(d + 0) = playerId;
        *reinterpret_cast<uint32_t*>(d + 4) = skin;
        p->numberOfBitsOfData = 64;

        // Swap the base template before SA-MP rebuilds the ped.
        if (customSkin != 0)
            render::EnsureSwapForCustom(customSkin);
    }
    g_skinHook.call<urmem::calling_convention::cdeclcall, void>(p);
}

} // namespace

uint32_t GetPlayerCustomSkin(uint16_t playerId) {
    auto it = g_playerCustomSkin.find(playerId);
    return it == g_playerCustomSkin.end() ? 0u : it->second;
}

bool AnyCustomSkin(uint32_t& customIdOut) {
    if (g_playerCustomSkin.empty()) return false;
    customIdOut = g_playerCustomSkin.begin()->second;
    return true;
}

bool IsCustomAssigned(uint32_t customId) {
    for (auto& kv : g_playerCustomSkin)
        if (kv.second == customId) return true;
    return false;
}

bool Init() {
    if (samp::GetRevision() == samp::Revision::R3_1) {
        g_r3WorldPlayerAddHook.install(samp::Addr(samp::r3_1::kScrWorldPlayerAdd),
                                       urmem::get_func_addr(&Hooked_R3WorldPlayerAdd),
                                       urmem::hook::type::jmp);
        g_r3SetPlayerSkinHook.install(samp::Addr(samp::r3_1::kScrSetPlayerSkin),
                                      urmem::get_func_addr(&Hooked_R3SetPlayerSkin),
                                      urmem::hook::type::jmp);
        g_r3SetSpawnInfoHook.install(samp::Addr(samp::r3_1::kScrSetSpawnInfo),
                                     urmem::get_func_addr(&Hooked_R3SetSpawnInfo),
                                     urmem::hook::type::jmp);
        CS_LOGI("dlcompat: R3-1 hooks installed: WorldPlayerAdd=0x%08X, SetPlayerSkin=0x%08X, SetSpawnInfo=0x%08X",
                static_cast<unsigned>(samp::Addr(samp::r3_1::kScrWorldPlayerAdd)),
                static_cast<unsigned>(samp::Addr(samp::r3_1::kScrSetPlayerSkin)),
                static_cast<unsigned>(samp::Addr(samp::r3_1::kScrSetSpawnInfo)));
        return true;
    }

    if (!samp::IsR5Build()) {
        CS_LOGE("dlcompat: unsupported build; skipping");
        return false;
    }
    g_skinHook.install(samp::Addr(samp::r5::kScrSetPlayerSkin),
                       urmem::get_func_addr(&Hooked_ScrSetPlayerSkin),
                       urmem::hook::type::jmp);
    CS_LOGI("dlcompat: hooked ScrSetPlayerSkin @ 0x%08X",
            static_cast<unsigned>(samp::Addr(samp::r5::kScrSetPlayerSkin)));
    return true;
}

} // namespace dlcompat
