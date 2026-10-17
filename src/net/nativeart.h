// native 0.3DL artwork activation
#pragma once

#include <cstdint>

namespace nativeart {

// register samp.dll's dormant 0.3dl artwork handlers so its client downloads every custom model and
// renders custom objects natively. needs samp base resolved + net::Init(); guarded by IsR5Build()
bool Init();

// skin bridge: r5's native client renders objects but not skins, though it still downloads skin
// files and tracks them in the manager. our clump-swap renderer sources skins from there instead of
// owning the artwork rpcs, avoiding a second handler and conflicts with the object path

struct ManagerSkin {
    int32_t newId = 0;   // custom skin id (20000-30000)
    int32_t baseId = 0;  // base ped the skin rides on
    uint32_t dffCrc = 0;
    uint32_t txdCrc = 0;
    bool ready = false;  // samp.dll has both files downloaded + verified in its cache
};

// visit every type=1 (skin) entry in the manager and return how many exist; visit may be null to
// only count. main-thread only, returns 0 until the manager exists (before connecting)
using SkinVisitor = void (*)(const ManagerSkin&, void* ctx);
int ForEachSkin(SkinVisitor visit, void* ctx);

// samp.dll's per-server cache path for a checksum; false if the manager/cache dir isn't ready
bool CachePath(uint32_t crc, bool isDff, char* out, unsigned n);

// changes whenever samp.dll (re)creates the manager - a reset signal for reconnect/gmx
const void* ManagerToken();

// re-report each finished file to samp.dll's dialog updater so its stuck-at-0% progress advances.
// main thread only (touches the ui) - call each frame
void PumpProgress();

} // namespace nativeart
