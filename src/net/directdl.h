// SA-MP 0.3.DL artwork compatibility implemented directly for 0.3.7-R3-1.
#pragma once

#include <cstdint>

namespace directdl {

struct ManagerSkin {
    int32_t newId = 0;
    int32_t baseId = 0;
    uint32_t dffCrc = 0;
    uint32_t txdCrc = 0;
    uint32_t dffSize = 0;
    uint32_t txdSize = 0;
    int32_t virtualWorld = -1;
    uint8_t timeOn = 0;
    uint8_t timeOff = 0;
    bool ready = false;
};

using SkinVisitor = void (*)(const ManagerSkin&, void* ctx);

bool Init();
void Pump();
int ForEachSkin(SkinVisitor visit, void* ctx);
bool CachePath(uint32_t crc, bool isDff, char* out, unsigned n);
const void* ManagerToken();

// Direct 0.3DL player-model bridge used by the R3-1 skin path.
bool GetBaseModel(int32_t newId, int32_t& baseId);
bool GetCustomModelForBase(int32_t baseId, int32_t& customId);
void SetPlayerCustom(uint16_t playerId, uint32_t customId);
void ClearPlayerCustom(uint16_t playerId);
void SetLocalCustom(uint32_t customId);
bool IsCustomAssigned(uint32_t customId);

} // namespace directdl
