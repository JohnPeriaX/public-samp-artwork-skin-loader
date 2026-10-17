#include "net/directdl.h"

#include "core/log.h"
#include "core/samp.h"
#include "model/cache.h"
#include "model/crc32.h"
#include "model/download.h"
#include "net/artwork.h"
#include "net/raknet.h"

#include <windows.h>
#include <raknet/rakclient.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace directdl {
namespace {

std::vector<ManagerSkin> g_models;
std::unordered_map<uint32_t, bool> g_crcIsDff;
std::unordered_set<uint64_t> g_requested;
std::unordered_map<uint16_t, uint32_t> g_playerCustom;
uint32_t g_localCustom = 0;
uint32_t g_expectedCount = 0;
int32_t g_virtualWorld = 0;
uint64_t g_generation = 0;
bool g_finishSent = false;
bool g_serverCompleted = false;

bool Active(const ManagerSkin& m) {
    return m.virtualWorld == -1 || m.virtualWorld == g_virtualWorld;
}

ManagerSkin* Find(int32_t newId) {
    for (auto& m : g_models)
        if (m.newId == newId) return &m;
    return nullptr;
}

ManagerSkin* FindByCrc(uint32_t crcValue) {
    for (auto& m : g_models) {
        if (m.dffCrc == crcValue || m.txdCrc == crcValue) return &m;
    }
    return nullptr;
}

void RequestFile(uint32_t crcValue, bool isDff) {
    if (!crcValue || cache::HasValid(crcValue, isDff)) return;
    const uint64_t key = (static_cast<uint64_t>(crcValue) << 1) | (isDff ? 1u : 0u);
    if (!g_requested.insert(key).second) return;

    bool sent = false;
    if (isDff) {
        BitStream bs;
        bs.Write(crcValue);
        sent = net::SendRPC(artwork::rpc::kRequestDFF, &bs);
    } else {
        BitStream bs;
        bs.Write(crcValue);
        sent = net::SendRPC(artwork::rpc::kRequestTXD, &bs);
    }
    if (!sent) {
        g_requested.erase(key);
        return;
    }
    CS_LOGI("directdl: requested %s 0x%08X", isDff ? "DFF" : "TXD", crcValue);
}

void RequestMissing() {
    for (const auto& m : g_models) {
        if (!Active(m)) continue;
        RequestFile(m.dffCrc, true);
        RequestFile(m.txdCrc, false);
    }
}

bool AllActiveReady() {
    bool sawActive = false;
    for (const auto& m : g_models) {
        if (!Active(m)) continue;
        sawActive = true;
        if (!cache::HasValid(m.dffCrc, true) || !cache::HasValid(m.txdCrc, false))
            return false;
    }
    return sawActive || g_expectedCount == 0;
}

void MaybeFinish() {
    if (g_finishSent || !net::Rak()) return;
    // Do not send RPC 184 before the server has published any artwork models.
    // An empty state previously looked ready and could prevent the RPC 179 model manifest
    // from arriving, leaving custom IDs unmapped even when cache files already exist.
    if (g_expectedCount == 0 || g_models.empty()) return;
    if (g_models.size() < g_expectedCount) return;
    if (!AllActiveReady()) return;
    if (net::SendEmptyRPC(artwork::rpc::kFinishDownload)) {
        g_finishSent = true;
        CS_LOGI("directdl: all active artwork files are ready; sent FinishDownload (RPC 184), world=%d",
                g_virtualWorld);
    }
}

void OnVirtualWorld(RPCParameters* p) {
    if (!p || !p->input || p->numberOfBitsOfData < 32) return;
    int32_t world = 0;
    std::memcpy(&world, p->input, sizeof(world));
    if (world == g_virtualWorld) {
        RequestMissing();
        MaybeFinish();
        return;
    }
    g_virtualWorld = world;
    g_finishSent = false;
    g_serverCompleted = false;
    ++g_generation;
    CS_LOGI("directdl: server announced virtual world %d for artwork", g_virtualWorld);
    RequestMissing();
    MaybeFinish();
}

void OnModelRequest(RPCParameters* p) {
    // ModelRequest carries 39 bytes: 2x u32, 1x u8, 8x u32, 2x u8.
    // Reject shorter payloads before the sequential BitStream reads below.
    if (!p || !p->input || p->numberOfBitsOfData < 39u * 8u) return;
    BitStream bs(p->input, BITS_TO_BYTES(p->numberOfBitsOfData), false);
    uint32_t index = 0, count = 0;
    uint8_t type = 0, timeOn = 0, timeOff = 0;
    int32_t world = 0, baseId = 0, newId = 0;
    uint32_t dffCrc = 0, txdCrc = 0, dffSize = 0, txdSize = 0;
    if (!bs.Read(index) || !bs.Read(count) || !bs.Read(type) || !bs.Read(world) ||
        !bs.Read(baseId) || !bs.Read(newId) || !bs.Read(dffCrc) || !bs.Read(txdCrc) ||
        !bs.Read(dffSize) || !bs.Read(txdSize) || !bs.Read(timeOn) || !bs.Read(timeOff)) {
        CS_LOGW("directdl: malformed ModelRequest RPC 179");
        return;
    }

    if (index == 0) {
        g_models.clear();
        g_crcIsDff.clear();
        g_requested.clear();
        g_playerCustom.clear();
        g_localCustom = 0;
        g_expectedCount = count;
        g_finishSent = false;
        g_serverCompleted = false;
        ++g_generation;
    } else if (count > g_expectedCount) {
        g_expectedCount = count;
    }

    ManagerSkin m;
    m.newId = newId;
    m.baseId = baseId;
    m.dffCrc = dffCrc;
    m.txdCrc = txdCrc;
    m.dffSize = dffSize;
    m.txdSize = txdSize;
    m.virtualWorld = world;
    m.timeOn = timeOn;
    m.timeOff = timeOff;

    if (ManagerSkin* old = Find(newId)) {
        *old = m;
    } else {
        g_models.push_back(m);
    }
    g_crcIsDff[dffCrc] = true;
    g_crcIsDff[txdCrc] = false;

    CS_LOGI("directdl: ModelRequest[%u/%u] type=%u world=%d base=%d new=%d dff=0x%08X(%u) txd=0x%08X(%u)",
            index + 1, count, type, world, baseId, newId, dffCrc, dffSize, txdCrc, txdSize);

    RequestMissing();
    MaybeFinish();
}

bool StoreInline(uint32_t crcValue, bool isDff, const unsigned char* data, uint32_t length) {
    if (!data || !length || crc::Buffer(data, length) != crcValue) {
        CS_LOGE("directdl: inline artwork CRC mismatch 0x%08X", crcValue);
        return false;
    }
    const std::string finalPath = cache::PathFor(crcValue, isDff);
    const std::string partPath = finalPath + ".part";
    FILE* f = std::fopen(partPath.c_str(), "wb");
    if (!f) return false;
    const size_t written = std::fwrite(data, 1, length, f);
    std::fclose(f);
    if (written != length) {
        DeleteFileA(partPath.c_str());
        return false;
    }
    if (!MoveFileExA(partPath.c_str(), finalPath.c_str(), MOVEFILE_REPLACE_EXISTING)) {
        DeleteFileA(partPath.c_str());
        return false;
    }
    const uint64_t key = (static_cast<uint64_t>(crcValue) << 1) | (isDff ? 1u : 0u);
    g_requested.erase(key);
    CS_LOGI("directdl: stored inline %s 0x%08X (%u bytes)", isDff ? "DFF" : "TXD", crcValue, length);
    return true;
}

void OnModelUrl(RPCParameters* p) {
    if (!p || !p->input || p->numberOfBitsOfData < 8) return;
    const unsigned bytes = BITS_TO_BYTES(p->numberOfBitsOfData);
    const unsigned char* d = p->input;
    uint8_t arg = d[0];
    if (arg == 6) {
        if (bytes < 7) return;
        const uint8_t fileType = d[1];
        uint32_t crcValue = 0;
        std::memcpy(&crcValue, d + 2, sizeof(crcValue));
        const uint8_t urlLen = d[6];
        if (fileType != 1 && fileType != 2) return;
        if (bytes < 7u + urlLen || urlLen == 0 || urlLen >= 256) return;
        char url[256] = {0};
        std::memcpy(url, d + 7, urlLen);
        const bool isDff = fileType == 1;
        if (cache::HasValid(crcValue, isDff)) return;
        const uint64_t key = (static_cast<uint64_t>(crcValue) << 1) | (isDff ? 1u : 0u);
        if (!g_requested.insert(key).second) return;
        const std::string finalPath = cache::PathFor(crcValue, isDff);
        const std::string partPath = finalPath + ".part";
        dl::Enqueue(url, partPath, crcValue, isDff);
        CS_LOGI("directdl: ModelUrl RPC 183 -> %s 0x%08X url=\"%s\"", isDff ? "DFF" : "TXD", crcValue, url);
        return;
    }

    if (arg == 1) {
        if (bytes < 10) return;
        const uint8_t fileType = d[1];
        uint32_t crcValue = 0, length = 0;
        std::memcpy(&crcValue, d + 2, sizeof(crcValue));
        std::memcpy(&length, d + 6, sizeof(length));
        if (fileType != 1 && fileType != 2) return;
        if (length == 0 || length > bytes - 10) return;
        StoreInline(crcValue, fileType == 1, d + 10, length);
    }
}

void OnDownloadCompleted(RPCParameters*) {
    g_serverCompleted = true;
    CS_LOGI("directdl: server sent DownloadCompleted (RPC 185)");
}

} // namespace

bool Init() {
    const auto revision = samp::GetRevision();
    if (revision != samp::Revision::R3_1) {
        CS_LOGE("directdl: refusing to initialize outside SA-MP 0.3.7 R3-1");
        return false;
    }
    net::RegisterIncomingRPC(48, &OnVirtualWorld);
    net::RegisterIncomingRPC(179, &OnModelRequest);
    net::RegisterIncomingRPC(183, &OnModelUrl);
    net::RegisterIncomingRPC(185, &OnDownloadCompleted);
    CS_LOGI("directdl: %s direct 0.3DL artwork RPC bridge installed (48/179/183/185)",
            samp::RevisionName());
    return true;
}

void Pump() {
    for (const auto& r : dl::Drain()) {
        const std::string finalPath = cache::PathFor(r.crc, r.isDff);
        const std::string partPath = finalPath + ".part";
        if (r.ok) {
            const BOOL committed = MoveFileExA(partPath.c_str(), finalPath.c_str(), MOVEFILE_REPLACE_EXISTING);
            if (!committed) {
                const DWORD error = GetLastError();
                DeleteFileA(partPath.c_str());
                // Release the key after cleanup so a later RPC can retry the failed commit.
                dl::Release(r.crc, r.isDff);
                g_requested.erase((static_cast<uint64_t>(r.crc) << 1) | (r.isDff ? 1u : 0u));
                CS_LOGE("directdl: cache commit failed 0x%08X %s err=%lu", r.crc, r.isDff ? "DFF" : "TXD", error);
                continue;
            }
            // Release only after the .part -> final cache commit succeeds.
            // This closes the duplicate-download race between worker completion and Pump().
            dl::Release(r.crc, r.isDff);
            g_requested.erase((static_cast<uint64_t>(r.crc) << 1) | (r.isDff ? 1u : 0u));
            CS_LOGI("directdl: download verified and committed 0x%08X %s", r.crc, r.isDff ? "DFF" : "TXD");
        } else {
            DeleteFileA(partPath.c_str());
            dl::Release(r.crc, r.isDff);
            g_requested.erase((static_cast<uint64_t>(r.crc) << 1) | (r.isDff ? 1u : 0u));
            CS_LOGE("directdl: download/CRC failed 0x%08X %s", r.crc, r.isDff ? "DFF" : "TXD");
        }
    }
    MaybeFinish();
}

int ForEachSkin(SkinVisitor visit, void* ctx) {
    int count = 0;
    for (auto& m : g_models) {
        if (m.newId < 20000 || m.newId > 30000) continue;
        m.ready = cache::HasValid(m.dffCrc, true) && cache::HasValid(m.txdCrc, false);
        ++count;
        if (visit) visit(m, ctx);
    }
    return count;
}

bool CachePath(uint32_t crcValue, bool isDff, char* out, unsigned n) {
    if (!out || n == 0 || !crcValue) return false;
    const std::string p = cache::PathFor(crcValue, isDff);
    if (p.size() + 1 > n) return false;
    std::memcpy(out, p.c_str(), p.size() + 1);
    return true;
}

const void* ManagerToken() { return &g_generation; }

bool GetBaseModel(int32_t newId, int32_t& baseId) {
    if (ManagerSkin* m = Find(newId)) {
        if (m->newId >= 20000 && m->newId <= 30000) {
            baseId = m->baseId;
            return true;
        }
    }
    return false;
}

bool GetCustomModelForBase(int32_t baseId, int32_t& customId) {
    for (const auto& m : g_models) {
        if (m.newId < 20000 || m.newId > 30000) continue;
        if (m.baseId != baseId) continue;
        customId = m.newId;
        return true;
    }
    return false;
}

void SetPlayerCustom(uint16_t playerId, uint32_t customId) {
    if (customId == 0) g_playerCustom.erase(playerId);
    else g_playerCustom[playerId] = customId;
}

void ClearPlayerCustom(uint16_t playerId) {
    g_playerCustom.erase(playerId);
}

void SetLocalCustom(uint32_t customId) {
    g_localCustom = customId;
}

bool IsCustomAssigned(uint32_t customId) {
    if (g_localCustom == customId) return true;
    for (const auto& kv : g_playerCustom)
        if (kv.second == customId) return true;
    return false;
}

} // namespace directdl
