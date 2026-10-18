#include "net/directdl.h"

#include "core/log.h"
#include "core/samp.h"
#include "model/cache.h"
#include "model/crc32.h"
#include "model/download.h"
#include "model/securecache.h"
#include "net/artwork.h"
#include "net/raknet.h"

#include <windows.h>
#include <raknet/rakclient.h>
#include <urmem.hpp>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <mutex>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace directdl {
namespace {

std::unordered_map<uint16_t, uint32_t> g_playerCustom;
uint32_t g_localCustom = 0;
uint64_t g_generation = 0;
uint32_t g_expectedModels = 0;
std::unordered_set<uint64_t> g_expectedFiles;
std::unordered_set<uint64_t> g_completedFiles;
bool g_finishSent = false;
std::mutex g_stateMutex;

// R3-1 native "Updating models.." dialog updater and its singleton pointer.
// IDA: sub_1006A570, global dword_1026E8A0.
constexpr uintptr_t kNativeDownloadUiUpdate = 0x06A570;
constexpr uintptr_t kNativeDownloadClient = 0x26E8A0;

uint64_t FileKey(uint32_t crcValue, bool isDff) {
    return (static_cast<uint64_t>(crcValue) << 1) | (isDff ? 1u : 0u);
}

// R3-1 already contains the dormant 0.3DL model manager and its object renderer.
// Keep the plugin's network/download bridge, but let samp.dll own model registration,
// readiness, negative object IDs, and CObjectPool::Create routing.
constexpr uintptr_t kNativeManager = 0x26E8E0;
constexpr uintptr_t kNativeModelRequest = 0x11B60;
constexpr uintptr_t kNativeModelUrl = 0x11D60;
// R3-1 RPC 185 is sub_1000E8F0 (native artwork finalize/unfreeze). 0x0EC30 is ScrSetWorldTime.
constexpr uintptr_t kNativeDownloadCompleted = 0x0E8F0;
constexpr uintptr_t kMgrArray = 0;
constexpr uintptr_t kMgrCount = 4;
constexpr uintptr_t kMgrCacheDir = 9;
constexpr uintptr_t kEntDffReady = 0;
constexpr uintptr_t kEntTxdReady = 1;
constexpr uintptr_t kEntType = 7;
constexpr uintptr_t kEntVirtualWorld = 8;
constexpr uintptr_t kEntBaseId = 12;
constexpr uintptr_t kEntNewId = 16;
constexpr uintptr_t kEntDffCrc = 70;
constexpr uintptr_t kEntTxdCrc = 74;
constexpr uintptr_t kEntDffSize = 78;
constexpr uintptr_t kEntTxdSize = 82;

void* NativeManager() {
    return *reinterpret_cast<void**>(samp::Addr(kNativeManager));
}

template <typename T>
T Rd(uintptr_t p) { return *reinterpret_cast<volatile T*>(p); }

bool NativeCachePath(uint32_t crcValue, bool isDff, std::string& out) {
    void* mgr = NativeManager();
    if (!mgr) return false;
    const char* dir = reinterpret_cast<const char*>(reinterpret_cast<uintptr_t>(mgr) + kMgrCacheDir);
    if (!dir[0]) return false;
    char path[512] = {0};
    std::snprintf(path, sizeof(path), "%s/0x%X.%s", dir, crcValue, isDff ? "dff" : "txd");
    out = path;
    return true;
}

void ForwardNativeModelRequest(RPCParameters* p) {
    if (!p) return;
    reinterpret_cast<net::RpcHandler>(samp::Addr(kNativeModelRequest))(p);
}

void ForwardNativeModelUrl(RPCParameters* p) {
    if (!p) return;
    reinterpret_cast<net::RpcHandler>(samp::Addr(kNativeModelUrl))(p);
}

void ForwardNativeDownloadCompleted(RPCParameters* p) {
    if (!p) return;
    reinterpret_cast<net::RpcHandler>(samp::Addr(kNativeDownloadCompleted))(p);
}

void OnVirtualWorld(RPCParameters* p) {
    if (!p || !p->input || p->numberOfBitsOfData < 32) return;
    int32_t world = 0;
    std::memcpy(&world, p->input, sizeof(world));
    ++g_generation;
    CS_LOGI("directdl: server announced virtual world %d for artwork", world);
    // The native R3 manager stores the artwork world in each model entry and performs
    // its own readiness/FinishDownload state machine. Keep RPC 48 only as a generation
    // boundary here; do not duplicate its model/download bookkeeping.
}

uint32_t NativeExpectedSize(uint32_t crcValue, bool isDff) {
    void* mgr = NativeManager();
    if (!mgr) return 0;
    const uintptr_t m = reinterpret_cast<uintptr_t>(mgr);
    const uint32_t count = Rd<uint32_t>(m + kMgrCount);
    void** arr = reinterpret_cast<void**>(Rd<uintptr_t>(m + kMgrArray));
    if (!arr) return 0;
    for (uint32_t i = 0; i < count; ++i) {
        uintptr_t e = reinterpret_cast<uintptr_t>(arr[i]);
        if (!e || Rd<uint32_t>(e + (isDff ? kEntDffCrc : kEntTxdCrc)) != crcValue) continue;
        return Rd<uint32_t>(e + (isDff ? kEntDffSize : kEntTxdSize));
    }
    return 0;
}

void MarkNativeReady(uint32_t crcValue, bool isDff) {
    void* mgr = NativeManager();
    if (!mgr) return;
    const uintptr_t m = reinterpret_cast<uintptr_t>(mgr);
    const uint32_t count = Rd<uint32_t>(m + kMgrCount);
    void** arr = reinterpret_cast<void**>(Rd<uintptr_t>(m + kMgrArray));
    if (!arr) return;
    for (uint32_t i = 0; i < count; ++i) {
        uintptr_t e = reinterpret_cast<uintptr_t>(arr[i]);
        if (!e || Rd<uint32_t>(e + (isDff ? kEntDffCrc : kEntTxdCrc)) != crcValue) continue;
        *reinterpret_cast<volatile uint8_t*>(e + (isDff ? kEntDffReady : kEntTxdReady)) = 1;
        CS_LOGI("directdl: native R3 entry ready flag set 0x%08X %s", crcValue, isDff ? "DFF" : "TXD");
        return;
    }
    CS_LOGW("directdl: native R3 entry not found for completed 0x%08X %s", crcValue, isDff ? "DFF" : "TXD");
}

void NotifyNativeDownloadUi(uint32_t crcValue, bool isDff, uint32_t bytes) {
    void* client = *reinterpret_cast<void**>(samp::Addr(kNativeDownloadClient));
    if (!client) return;
    // Native R3's real worker calls this on completion with state=4. Doing the same
    // from our downloader keeps the stock "Updating models.." dialog in sync even
    // while its original HTTP worker is waiting on our unused .temp files.
    urmem::call_function<urmem::calling_convention::thiscall, char>(
        samp::Addr(kNativeDownloadUiUpdate), client,
        isDff ? 1 : 2, static_cast<int>(crcValue), 4,
        static_cast<int>(bytes), 0, -1, -1);
}

bool CompleteNativeFile(uint32_t crcValue, bool isDff, uint32_t bytes) {
    const uint64_t key = FileKey(crcValue, isDff);
    {
        std::lock_guard<std::mutex> lk(g_stateMutex);
        if (g_completedFiles.find(key) != g_completedFiles.end()) return true;
    }

    std::string finalPath;
    if (!NativeCachePath(crcValue, isDff, finalPath)) return false;
    if (!securecache::Materialize(crcValue, isDff, finalPath)) {
        CS_LOGE("directdl: protected asset could not be materialized 0x%08X %s",
                crcValue, isDff ? "DFF" : "TXD");
        return false;
    }

    MarkNativeReady(crcValue, isDff);
    NotifyNativeDownloadUi(crcValue, isDff, bytes);
    {
        std::lock_guard<std::mutex> lk(g_stateMutex);
        g_completedFiles.insert(key);
    }
    CS_LOGI("directdl: native R3 completion signalled 0x%08X %s (%u bytes)",
            crcValue, isDff ? "DFF" : "TXD", bytes);
    return true;
}

void TrySendFinishDownload() {
    size_t expected = 0;
    bool send = false;
    {
        std::lock_guard<std::mutex> lk(g_stateMutex);
        if (!g_finishSent && g_expectedModels != 0 && !g_expectedFiles.empty() &&
            g_completedFiles.size() >= g_expectedFiles.size()) {
            g_finishSent = true;
            expected = g_expectedFiles.size();
            send = true;
        }
    }
    if (!send) return;

    const bool ok = net::SendEmptyRPC(184);
    if (!ok) {
        std::lock_guard<std::mutex> lk(g_stateMutex);
        g_finishSent = false;
    }
    CS_LOGI("directdl: all %zu native-cache files verified; sent FinishDownload RPC 184 ok=%d",
            expected, static_cast<int>(ok));
}

void OnWorkerResult(const dl::Result& r) {
    if (!r.ok) {
        CS_LOGE("directdl: download/CRC failed 0x%08X %s", r.crc, r.isDff ? "DFF" : "TXD");
        return;
    }
    if (CompleteNativeFile(r.crc, r.isDff, r.bytes))
        TrySendFinishDownload();
}

void OnModelRequest(RPCParameters* p) {
    if (!p || !p->input || p->numberOfBitsOfData < 39u * 8u) return;
    const unsigned char* d = p->input;
    uint32_t modelCount = 0;
    uint32_t dffCrc = 0;
    uint32_t txdCrc = 0;
    std::memcpy(&modelCount, d + 4, sizeof(modelCount));
    std::memcpy(&dffCrc, d + 21, sizeof(dffCrc));
    std::memcpy(&txdCrc, d + 25, sizeof(txdCrc));

    {
        std::lock_guard<std::mutex> lk(g_stateMutex);
        if (g_expectedModels == 0 || g_expectedModels != modelCount) {
            g_expectedModels = modelCount;
            g_expectedFiles.clear();
            g_completedFiles.clear();
            g_finishSent = false;
            CS_LOGI("directdl: new R3 artwork set count=%u", modelCount);
        }
        if (dffCrc) g_expectedFiles.insert(FileKey(dffCrc, true));
        if (txdCrc) g_expectedFiles.insert(FileKey(txdCrc, false));
    }

    ++g_generation;
    CS_LOGI("directdl: forwarding ModelRequest RPC 179 to native R3 artwork manager");
    ForwardNativeModelRequest(p);
}

bool StoreInline(uint32_t crcValue, bool isDff, const unsigned char* data, uint32_t length) {
    if (!data || !length || crc::Buffer(data, length) != crcValue) {
        CS_LOGE("directdl: inline artwork CRC mismatch 0x%08X", crcValue);
        return false;
    }
    std::vector<unsigned char> bytes(data, data + length);
    const bool stored = securecache::Store(crcValue, isDff, bytes);
    SecureZeroMemory(bytes.data(), bytes.size());
    if (!stored) return false;
    CS_LOGI("directdl: stored inline protected %s 0x%08X (%u bytes)",
            isDff ? "DFF" : "TXD", crcValue, length);
    return CompleteNativeFile(crcValue, isDff, length);
}

void OnModelUrl(RPCParameters* p) {
    if (!p || !p->input || p->numberOfBitsOfData < 8) return;
    const unsigned bytes = BITS_TO_BYTES(p->numberOfBitsOfData);
    const unsigned char* d = p->input;
    const uint8_t arg = d[0];

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
        std::string finalPath;
        if (!NativeCachePath(crcValue, isDff, finalPath)) {
            CS_LOGE("directdl: native R3 artwork cache unavailable for 0x%08X", crcValue);
            return;
        }
        // Protected cache is the durable store. Only materialize a plaintext native
        // cache file for the short window in which samp.dll/RenderWare consumes it.
        const std::string protectedPath = securecache::PathFor(crcValue, isDff);
        if (GetFileAttributesA(protectedPath.c_str()) != INVALID_FILE_ATTRIBUTES) {
            const uint32_t bytesReady = NativeExpectedSize(crcValue, isDff);
            if (CompleteNativeFile(crcValue, isDff, bytesReady))
                TrySendFinishDownload();
            CS_LOGI("directdl: protected cache hit 0x%08X %s -> native staging",
                    crcValue, isDff ? "DFF" : "TXD");
        } else {
            dl::Enqueue(url, finalPath, crcValue, isDff);
            CS_LOGI("directdl: protected fetch %s 0x%08X url=\"%s\"",
                    isDff ? "DFF" : "TXD", crcValue, url);
        }
    } else if (arg == 1) {
        if (bytes < 10) return;
        const uint8_t fileType = d[1];
        uint32_t crcValue = 0, length = 0;
        std::memcpy(&crcValue, d + 2, sizeof(crcValue));
        std::memcpy(&length, d + 6, sizeof(length));
        if (fileType != 1 && fileType != 2) return;
        if (length == 0 || length > bytes - 10) return;
        const bool isDff = fileType == 1;
        if (StoreInline(crcValue, isDff, d + 10, length))
            TrySendFinishDownload();
    }

    // Keep the native state machine alive after our download shim.
    ForwardNativeModelUrl(p);
}

void OnDownloadCompleted(RPCParameters* p) {
    ++g_generation;
    CS_LOGI("directdl: forwarding DownloadCompleted RPC 185 to native R3 artwork manager");
    ForwardNativeDownloadCompleted(p);
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
    dl::SetCompletionHook(&OnWorkerResult);
    CS_LOGI("directdl: %s direct 0.3DL artwork RPC bridge installed (48/179/183/185)",
            samp::RevisionName());
    return true;
}

void Pump() {
    // The native R3 download dialog blocks the normal CNetGame::Process heartbeat while
    // it is waiting. The worker completion hook therefore performs the real completion
    // path. Pump remains as a harmless fallback for cache hits/results drained later.
    for (const auto& r : dl::Drain()) {
        if (!r.ok) {
            CS_LOGE("directdl: download/CRC failed 0x%08X %s", r.crc, r.isDff ? "DFF" : "TXD");
            continue;
        }
        CompleteNativeFile(r.crc, r.isDff, r.bytes);
    }
    TrySendFinishDownload();
}

int ForEachSkin(SkinVisitor visit, void* ctx) {
    void* mgr = NativeManager();
    if (!mgr) return 0;
    const uintptr_t m = reinterpret_cast<uintptr_t>(mgr);
    const uint32_t count = Rd<uint32_t>(m + kMgrCount);
    void** arr = reinterpret_cast<void**>(Rd<uintptr_t>(m + kMgrArray));
    if (!arr) return 0;
    int skins = 0;
    for (uint32_t i = 0; i < count; ++i) {
        uintptr_t e = reinterpret_cast<uintptr_t>(arr[i]);
        if (!e || Rd<uint8_t>(e + kEntType) != 1) continue;
        ManagerSkin s;
        s.newId = Rd<int32_t>(e + kEntNewId);
        s.baseId = Rd<int32_t>(e + kEntBaseId);
        s.dffCrc = Rd<uint32_t>(e + kEntDffCrc);
        s.txdCrc = Rd<uint32_t>(e + kEntTxdCrc);
        s.dffSize = Rd<uint32_t>(e + kEntDffSize);
        s.txdSize = Rd<uint32_t>(e + kEntTxdSize);
        s.virtualWorld = Rd<int32_t>(e + kEntVirtualWorld);
        s.timeOn = Rd<uint8_t>(e + 86);
        s.timeOff = Rd<uint8_t>(e + 87);
        s.ready = Rd<uint8_t>(e + kEntDffReady) && Rd<uint8_t>(e + kEntTxdReady);
        ++skins;
        if (visit) visit(s, ctx);
    }
    return skins;
}

int ForEachObject(ObjectVisitor visit, void* ctx) {
    void* mgr = NativeManager();
    if (!mgr) return 0;
    const uintptr_t m = reinterpret_cast<uintptr_t>(mgr);
    const uint32_t count = Rd<uint32_t>(m + kMgrCount);
    void** arr = reinterpret_cast<void**>(Rd<uintptr_t>(m + kMgrArray));
    if (!arr) return 0;
    int objects = 0;
    for (uint32_t i = 0; i < count; ++i) {
        uintptr_t e = reinterpret_cast<uintptr_t>(arr[i]);
        if (!e || Rd<uint8_t>(e + kEntType) != 2) continue;
        ManagerSkin s;
        s.newId = Rd<int32_t>(e + kEntNewId);
        s.baseId = Rd<int32_t>(e + kEntBaseId);
        s.dffCrc = Rd<uint32_t>(e + kEntDffCrc);
        s.txdCrc = Rd<uint32_t>(e + kEntTxdCrc);
        s.dffSize = Rd<uint32_t>(e + kEntDffSize);
        s.txdSize = Rd<uint32_t>(e + kEntTxdSize);
        s.virtualWorld = Rd<int32_t>(e + kEntVirtualWorld);
        s.timeOn = Rd<uint8_t>(e + 86);
        s.timeOff = Rd<uint8_t>(e + 87);
        s.ready = Rd<uint8_t>(e + kEntDffReady) && Rd<uint8_t>(e + kEntTxdReady);
        ++objects;
        if (visit) visit(s, ctx);
    }
    return objects;
}

bool CachePath(uint32_t crcValue, bool isDff, char* out, unsigned n) {
    if (!out || n == 0 || !crcValue) return false;
    std::string p;
    if (!NativeCachePath(crcValue, isDff, p) || p.size() + 1 > n) return false;
    std::memcpy(out, p.c_str(), p.size() + 1);
    return true;
}

bool EnsurePlaintext(uint32_t crcValue, bool isDff) {
    std::string path;
    if (!NativeCachePath(crcValue, isDff, path)) return false;
    if (GetFileAttributesA(path.c_str()) != INVALID_FILE_ATTRIBUTES) return true;
    return securecache::Materialize(crcValue, isDff, path);
}

void ReleasePlaintext(uint32_t crcValue, bool isDff) {
    std::string path;
    if (NativeCachePath(crcValue, isDff, path))
        securecache::ReleasePlaintext(path);
}

const void* ManagerToken() { return NativeManager() ? NativeManager() : &g_generation; }

bool GetBaseModel(int32_t newId, int32_t& baseId) {
    void* mgr = NativeManager();
    if (!mgr) return false;
    const uintptr_t m = reinterpret_cast<uintptr_t>(mgr);
    const uint32_t count = Rd<uint32_t>(m + kMgrCount);
    void** arr = reinterpret_cast<void**>(Rd<uintptr_t>(m + kMgrArray));
    if (!arr) return false;
    for (uint32_t i = 0; i < count; ++i) {
        uintptr_t e = reinterpret_cast<uintptr_t>(arr[i]);
        if (!e || Rd<int32_t>(e + kEntNewId) != newId || Rd<uint8_t>(e + kEntType) != 1) continue;
        baseId = Rd<int32_t>(e + kEntBaseId);
        return true;
    }
    return false;
}

bool GetCustomModelForBase(int32_t baseId, int32_t& customId) {
    void* mgr = NativeManager();
    if (!mgr) return false;
    const uintptr_t m = reinterpret_cast<uintptr_t>(mgr);
    const uint32_t count = Rd<uint32_t>(m + kMgrCount);
    void** arr = reinterpret_cast<void**>(Rd<uintptr_t>(m + kMgrArray));
    if (!arr) return false;
    for (uint32_t i = 0; i < count; ++i) {
        uintptr_t e = reinterpret_cast<uintptr_t>(arr[i]);
        if (!e || Rd<uint8_t>(e + kEntType) != 1 || Rd<int32_t>(e + kEntBaseId) != baseId) continue;
        customId = Rd<int32_t>(e + kEntNewId);
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
