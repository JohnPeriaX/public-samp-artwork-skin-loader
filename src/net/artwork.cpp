#include "net/artwork.h"

#include "core/log.h"
#include "core/samp.h"
#include "game/render.h"
#include "game/rwmodel.h"
#include "net/nativeart.h"
#include "net/directdl.h"

#include <cstdint>
#include <vector>

// R3-1 already ships the dormant 0.3.DL model manager and native object renderer.
// The direct bridge activates that manager, supplies its cache, and mirrors model metadata here;
// skins still use the plugin's clump-swap renderer while objects stay on samp.dll's native path.

namespace artwork {
namespace {

struct Model {
    int32_t newId = 0;
    int32_t baseId = 0;
    uint32_t dffCrc = 0;
    uint32_t txdCrc = 0;
    bool object = false;     // R3-1 AddSimpleModel custom-object entry (negative model ID)
    bool ready = false;      // native R3 manager finished downloading + verifying both files
    rwmodel::Loaded loaded;  // RW clump+txd, loaded from the artwork cache once ready
};

std::vector<Model> g_models;              // custom skins + R3-1 custom objects mirrored from direct DL
const void* g_managerToken = nullptr;     // last-seen manager identity (changes on reconnect/gmx)
int g_lastModelCount = 0;

Model* Find(int32_t newId) {
    for (auto& m : g_models) if (m.newId == newId) return &m;
    return nullptr;
}

// forget every mirrored skin: put swapped base templates back first, then free our clumps
void ResetAll() {
    render::RestoreAll();
    for (auto& m : g_models) {
        rwmodel::Free(m.loaded);
    }
    g_models.clear();
    g_lastModelCount = 0;
}

// mirror one native-manager skin entry (add if new, refresh its ready flag)
void Sync(const nativeart::ManagerSkin& s, void*) {
    Model* m = Find(s.newId);
    if (!m) {
        g_models.push_back(Model{ s.newId, s.baseId, s.dffCrc, s.txdCrc, false, false, {} });
        m = &g_models.back();
        CS_LOGI("artwork: skin %d (base %d) dff=0x%08X txd=0x%08X mirrored from native manager",
                s.newId, s.baseId, s.dffCrc, s.txdCrc);
    }
    m->ready = s.ready;
}

void SyncDirectDL(const directdl::ManagerSkin& s, void*) {
    if (s.newId < 20000 || s.newId > 30000) return;
    Model* m = Find(s.newId);
    if (!m) {
        g_models.push_back(Model{ s.newId, s.baseId, s.dffCrc, s.txdCrc, false, false, {} });
        m = &g_models.back();
        CS_LOGI("artwork: direct 0.3DL skin %d (base %d) dff=0x%08X txd=0x%08X",
                s.newId, s.baseId, s.dffCrc, s.txdCrc);
    }
    m->ready = s.ready;
}

void SyncDirectObject(const directdl::ManagerSkin& s, void*) {
    if (s.newId > -1000 || s.newId < -30000) return;
    Model* m = Find(s.newId);
    if (!m) {
        g_models.push_back(Model{ s.newId, s.baseId, s.dffCrc, s.txdCrc, true, false, {} });
        m = &g_models.back();
        CS_LOGI("artwork: direct 0.3DL object %d (base %d) dff=0x%08X txd=0x%08X",
                s.newId, s.baseId, s.dffCrc, s.txdCrc);
    }
    m->object = true;
    m->ready = s.ready;
}

bool AllLoadedForAsset(uint32_t crcValue, bool isDff) {
    if (!crcValue) return false;
    bool found = false;
    for (const auto& m : g_models) {
        const uint32_t crcValueForModel = isDff ? m.dffCrc : m.txdCrc;
        if (crcValueForModel != crcValue) continue;
        found = true;
        if (!m.loaded.ok()) return false;
    }
    return found;
}

void ReleaseLoadedDirectAssets() {
    for (const auto& m : g_models) {
        if (!m.loaded.ok()) continue;
        if (AllLoadedForAsset(m.dffCrc, true)) directdl::ReleasePlaintext(m.dffCrc, true);
        if (AllLoadedForAsset(m.txdCrc, false)) directdl::ReleasePlaintext(m.txdCrc, false);
    }
}

} // namespace

void Init() {
    if (samp::GetRevision() == samp::Revision::R3_1)
        CS_LOGI("artwork: %s skin clump-swap active (direct 0.3DL artwork bridge)", samp::RevisionName());
    else
        CS_LOGI("artwork: skin clump-swap active (skins sourced from samp.dll's native manager + cache)");
}

// each frame: resync skins from the manager, then load the clump for any whose files are cached.
// render::Tick() then swaps those clumps onto peds
void Pump() {
    const bool direct = samp::GetRevision() == samp::Revision::R3_1;

    // reconnect/gmx: reset the mirror when the underlying artwork manager generation changes.
    const void* token = direct ? directdl::ManagerToken() : nativeart::ManagerToken();
    const int skinCount = direct ? directdl::ForEachSkin(nullptr, nullptr)
                                 : nativeart::ForEachSkin(nullptr, nullptr);
    const int objectCount = direct ? directdl::ForEachObject(nullptr, nullptr) : 0;
    const int modelCount = skinCount + objectCount;
    const bool reset = (token != g_managerToken) || (modelCount < g_lastModelCount);
    if (reset && (g_managerToken != nullptr || !g_models.empty())) ResetAll();
    g_managerToken = token;
    g_lastModelCount = modelCount;

    if (direct) {
        directdl::Pump();
        directdl::ForEachSkin(&SyncDirectDL, nullptr);
        directdl::ForEachObject(&SyncDirectObject, nullptr);
    } else {
        nativeart::ForEachSkin(&Sync, nullptr);
        nativeart::PumpProgress(); // drive samp.dll's download dialog (else it sticks at 0%)
    }

    for (auto& m : g_models) {
        if (!m.ready) continue;
        if (m.loaded.ok()) {
            // R3 AddSimpleModel uses a negative SA-MP model ID. Do not pass that
            // protocol ID into GTA CModelInfo::AddClumpModel directly. The R3
            // samp.dll mapping/creation path still needs to be resolved.
            continue;
        }
        char dffPath[512], txdPath[512];
        const bool dffOk = direct ? directdl::CachePath(m.dffCrc, true, dffPath, sizeof(dffPath))
                                  : nativeart::CachePath(m.dffCrc, true, dffPath, sizeof(dffPath));
        const bool txdOk = direct ? directdl::CachePath(m.txdCrc, false, txdPath, sizeof(txdPath))
                                  : nativeart::CachePath(m.txdCrc, false, txdPath, sizeof(txdPath));
        if (!dffOk || !txdOk) continue;
        m.loaded = rwmodel::LoadModel(dffPath, txdPath);
        if (m.loaded.ok()) {
            if (m.object && direct) {
                CS_LOGI("artwork: object %d RenderWare cache loaded; native R3 object renderer owns model registration (base %d)",
                        m.newId, m.baseId);
            } else {
                CS_LOGI("artwork: skin %d RenderWare-loaded (%s)", m.newId,
                        direct ? "direct 0.3DL cache" : "native cache");
            }
        } else
            CS_LOGW("artwork: %s %d RW-load incomplete (clump=%p txd=%p) - bad file?",
                    m.object ? "object" : "skin", m.newId, m.loaded.clump, m.loaded.txd);
    }

    if (direct) ReleaseLoadedDirectAssets();
}

bool GetReadyModel(int newId, ReadyModel& out) {
    for (auto& m : g_models) {
        if (m.newId == newId && m.loaded.ok()) {
            out.newId = m.newId;
            out.baseId = m.baseId;
            out.clump = m.loaded.clump;
            out.txd = m.loaded.txd;
            return true;
        }
    }
    return false;
}

void ForEachReady(ReadyVisitor visit, void* ctx) {
    if (!visit) return;
    for (auto& m : g_models) {
        if (!m.loaded.ok()) continue;
        ReadyModel rm;
        rm.newId = m.newId;
        rm.baseId = m.baseId;
        rm.clump = m.loaded.clump;
        rm.txd = m.loaded.txd;
        visit(rm, ctx);
    }
}

void OnCustomClumpLost(void* clump, bool reload) {
    for (auto& m : g_models) {
        if (m.loaded.clump != clump) continue;
        m.loaded.clump = nullptr; // streamer already destroyed it - don't re-free
        if (reload) {
            char dffPath[512];
            const bool direct = samp::GetRevision() == samp::Revision::R3_1;
            const bool pathOk = direct
                ? directdl::CachePath(m.dffCrc, true, dffPath, sizeof(dffPath))
                : nativeart::CachePath(m.dffCrc, true, dffPath, sizeof(dffPath));
            if (direct) directdl::EnsurePlaintext(m.dffCrc, true);
            if (pathOk && rwmodel::ReloadClump(m.loaded, dffPath))
                CS_LOGI("artwork: skin %d clump was unloaded by streamer -> reloaded %p",
                        m.newId, m.loaded.clump);
            else
                CS_LOGW("artwork: skin %d clump lost, reload failed (reverts to base until reconnect)",
                        m.newId);
        } else {
            CS_LOGI("artwork: skin %d clump abandoned (teardown)", m.newId);
        }
        return;
    }
}

} // namespace artwork