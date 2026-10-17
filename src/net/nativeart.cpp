#include "net/nativeart.h"

#include "core/log.h"
#include "core/samp.h"
#include "model/download.h"
#include "net/raknet.h"

#include <windows.h>
#include <urmem.hpp>
#include <raknet/rakclient.h> // RPCParameters, BitStream, BITS_TO_BYTES

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <set>
#include <string>
#include <vector>

namespace nativeart {
namespace {

// r5's samp.dll ships the whole 0.3dl client (handlers, model manager, render) but its download
// worker is stubbed (nullsub), so it registers models and requests urls yet fetches nothing. we
// wake the dormant handlers and supply the missing fetch: download each url into the native cache
// as 0x<CRC>.dff/.txd, then its per-frame tick finds it via HasValid and renders

// native model manager global (r5 rva 0x26EB98), lazily built by the 179 handler
void* Manager() { return *reinterpret_cast<void**>(samp::Addr(0x26EB98)); }

template <typename T> T Rd(uintptr_t p) { return *reinterpret_cast<volatile T*>(p); }

// manager cache dir (field +9); false until the manager exists and the dir is set
bool CacheDir(char* out, size_t n) {
    void* mgr = Manager();
    if (!mgr) return false;
    const char* dir = reinterpret_cast<const char*>(reinterpret_cast<uintptr_t>(mgr) + 9);
    if (!dir[0]) return false;
    std::strncpy(out, dir, n - 1);
    out[n - 1] = '\0';
    return true;
}

// path the native HasValid checks: "<dir>/0x<CRC>.<ext>"
void FinalPath(char* out, size_t n, const char* dir, uint32_t crc, bool isDff) {
    _snprintf(out, n, "%s/0x%X.%s", dir, crc, isDff ? "dff" : "txd");
    out[n - 1] = '\0';
}

// native model manager layout, reverse-engineered from vendor/samp/samp.dll
constexpr uintptr_t kMgrArray = 0;  // void** array of model entries
constexpr uintptr_t kMgrCount = 4;  // u32 number of entries
constexpr uintptr_t kEntDffReady = 0;  // u8
constexpr uintptr_t kEntTxdReady = 1;  // u8
constexpr uintptr_t kEntType = 7;      // u8: 1=skin, 2=object
constexpr uintptr_t kEntBaseId = 12;   // i32
constexpr uintptr_t kEntNewId = 16;    // i32 (negative for objects)
constexpr uintptr_t kEntDffCrc = 70;   // u32
constexpr uintptr_t kEntTxdCrc = 74;   // u32
constexpr uintptr_t kEntDffSize = 78;  // u32
constexpr uintptr_t kEntTxdSize = 82;  // u32

// 183 download handler; case 6 is a url. we fetch it (native worker is stubbed) then forward so
// the native state machine still advances
void __cdecl OnModelUrl(RPCParameters* p) {
    if (p && p->input && p->numberOfBitsOfData >= 8) {
        BitStream bs(p->input, BITS_TO_BYTES(p->numberOfBitsOfData), false);
        uint8_t lead = 0;
        bs.Read(lead);
        if (lead == 6) {
            uint8_t fileType = 0, urlLen = 0;
            uint32_t crc = 0;
            char url[256] = {0};
            bs.Read(fileType);
            bs.Read(crc);
            bs.Read(urlLen);
            if (urlLen && urlLen < sizeof(url)) bs.Read(url, urlLen);
            const bool isDff = (fileType == 1);

            char dir[300];
            if (CacheDir(dir, sizeof(dir))) {
                char finalp[512];
                FinalPath(finalp, sizeof(finalp), dir, crc, isDff);
                std::string temp = std::string(finalp) + ".part";
                dl::Enqueue(url, temp, crc, isDff); // async fetch + crc verify; renamed on Pump
                CS_LOGI("nativeart: hybrid fetch %s 0x%08X url=\"%s\" -> %s",
                        isDff ? "dff" : "txd", crc, url, finalp);
            } else {
                CS_LOGW("nativeart: cache dir not ready; cannot fetch 0x%08X", crc);
            }
        }
    }
    // forward to native handler; its own worker downloads nothing (stub)
    reinterpret_cast<net::RpcHandler>(samp::Addr(samp::r5::kNativeModelUrl))(p);
}

// publish finished downloads into the native cache (rename .part -> final) so HasValid only ever
// sees a complete, crc-verified file
void PumpDownloads() {
    std::vector<dl::Result> results = dl::Drain();
    if (results.empty()) return;
    char dir[300];
    if (!CacheDir(dir, sizeof(dir))) return;
    for (const auto& r : results) {
        char finalp[512];
        FinalPath(finalp, sizeof(finalp), dir, r.crc, r.isDff);
        std::string temp = std::string(finalp) + ".part";
        if (r.ok) {
            MoveFileExA(temp.c_str(), finalp, MOVEFILE_REPLACE_EXISTING);
            CS_LOGI("nativeart: cached 0x%08X %s -> %s (native tick will pick it up)",
                    r.crc, r.isDff ? "dff" : "txd", finalp);
        } else {
            DeleteFileA(temp.c_str());
            CS_LOGE("nativeart: hybrid download/verify failed for 0x%08X", r.crc);
        }
    }
}

// diag: snapshot of native manager state, logged only when it changes
void LogDiag(char* last, size_t lastN) {
    void* mgr = Manager();
    if (!mgr) return;
    uintptr_t m = reinterpret_cast<uintptr_t>(mgr);
    uint32_t count = Rd<uint32_t>(m + 4);
    char buf[512];
    _snprintf(buf, sizeof(buf),
        "diag: count=%u expected=%u s531=%u f543=%u done545=%u busy4932=%u",
        count, Rd<uint32_t>(m + 539), Rd<uint8_t>(m + 531), Rd<uint8_t>(m + 543),
        Rd<uint8_t>(m + 545), Rd<uint8_t>(m + 4932));
    buf[sizeof(buf) - 1] = '\0';
    if (std::strcmp(buf, last) == 0) return;
    std::strncpy(last, buf, lastN - 1);
    CS_LOGI("%s", buf);
    void** arr = reinterpret_cast<void**>(Rd<uintptr_t>(m + 0));
    for (uint32_t i = 0; arr && i < count && i < 8; ++i) {
        uintptr_t e = reinterpret_cast<uintptr_t>(arr[i]);
        if (!e) continue;
        CS_LOGI("diag:  model[%u] type=%u newId=%d base=%d dffReady0=%u txdReady1=%u done6=%u",
                i, Rd<uint8_t>(e + 7), Rd<int32_t>(e + 16), Rd<int32_t>(e + 12),
                Rd<uint8_t>(e + 0), Rd<uint8_t>(e + 1), Rd<uint8_t>(e + 6));
    }
}

DWORD WINAPI PollThread(LPVOID) {
    char last[512] = {0};
    int tick = 0;
    for (;;) {
        PumpDownloads();
        if (++tick >= 4) { tick = 0; LogDiag(last, sizeof(last)); } // ~1s
        Sleep(250);
    }
}

} // namespace

bool Init() {
    if (!samp::IsR5Build()) {
        CS_LOGE("nativeart: not the validated R5 build; refusing to register native handlers");
        return false;
    }

    struct Entry { unsigned char id; uintptr_t rva; net::RpcHandler shim; const char* name; };
    const Entry handlers[] = {
        { 179, samp::r5::kNativeModelRequest,      nullptr,     "ModelRequest" },      // register model, build manager
        { 183, samp::r5::kNativeModelUrl,          &OnModelUrl, "ModelUrl/Download" }, // url -> we fetch (native worker stubbed)
        { 185, samp::r5::kNativeDownloadCompleted, nullptr,     "DownloadCompleted" }, // finalize, unfreeze
    };

    for (const auto& h : handlers) {
        net::RpcHandler fn = h.shim ? h.shim : reinterpret_cast<net::RpcHandler>(samp::Addr(h.rva));
        net::RegisterIncomingRPC(h.id, fn);
        CS_LOGI("nativeart: registered %s%s (RPC %u) @ 0x%08X",
                h.shim ? "shim->" : "native ", h.name, h.id, static_cast<unsigned>(samp::Addr(h.rva)));
    }

    CS_LOGI("nativeart: native 0.3DL artwork activated (hybrid: native registers+renders, we download)");

    if (HANDLE t = CreateThread(nullptr, 0, &PollThread, nullptr, 0, nullptr))
        CloseHandle(t);

    return true;
}

int ForEachSkin(SkinVisitor visit, void* ctx) {
    void* mgr = Manager();
    if (!mgr) return 0;
    uintptr_t m = reinterpret_cast<uintptr_t>(mgr);
    uint32_t count = Rd<uint32_t>(m + kMgrCount);
    void** arr = reinterpret_cast<void**>(Rd<uintptr_t>(m + kMgrArray));
    if (!arr) return 0;
    int skins = 0;
    for (uint32_t i = 0; i < count; ++i) {
        uintptr_t e = reinterpret_cast<uintptr_t>(arr[i]);
        if (!e || Rd<uint8_t>(e + kEntType) != 1) continue; // skins only (type 1)
        ++skins;
        if (!visit) continue;
        ManagerSkin s;
        s.newId = Rd<int32_t>(e + kEntNewId);
        s.baseId = Rd<int32_t>(e + kEntBaseId);
        s.dffCrc = Rd<uint32_t>(e + kEntDffCrc);
        s.txdCrc = Rd<uint32_t>(e + kEntTxdCrc);
        s.ready = Rd<uint8_t>(e + kEntDffReady) && Rd<uint8_t>(e + kEntTxdReady);
        visit(s, ctx);
    }
    return skins;
}

bool CachePath(uint32_t crc, bool isDff, char* out, unsigned n) {
    char dir[300];
    if (!CacheDir(dir, sizeof(dir))) return false;
    FinalPath(out, n, dir, crc, isDff);
    return true;
}

const void* ManagerToken() { return Manager(); }

namespace {

std::set<uint64_t> g_reported;      // (crc<<1)|isDff files already flagged done in the dialog
const void* g_progToken = nullptr;  // manager identity the reported-set belongs to

// tell samp.dll's own download dialog one file finished (state 4 -> 100% + ++completed count)
// fileType: 1=dff, 2=txd
void ReportDone(void* client, unsigned fileType, uint32_t crc, uint32_t size) {
    uint64_t key = (static_cast<uint64_t>(crc) << 1) | (fileType == 1 ? 1u : 0u);
    if (!g_reported.insert(key).second) return; // already reported
    // __thiscall(client, fileType, crc, state=4, bytes, size, i64=0); the i64 is two dword pushes
    // so the 0x1C-byte stack matches without 64-bit arg marshalling
    urmem::call_function<urmem::calling_convention::thiscall, char>(
        samp::Addr(samp::r5::kDownloadUiUpdate), client,
        static_cast<int>(fileType), static_cast<int>(crc), 4,
        static_cast<int>(size), static_cast<int>(size), 0, 0);
}

} // namespace

void PumpProgress() {
    void* client = *reinterpret_cast<void**>(samp::Addr(samp::r5::kDownloadClient));
    void* mgr = Manager();
    if (!client || !mgr) return;

    const void* token = mgr;
    if (token != g_progToken) { g_reported.clear(); g_progToken = token; } // reconnect/gmx

    uintptr_t m = reinterpret_cast<uintptr_t>(mgr);
    uint32_t count = Rd<uint32_t>(m + kMgrCount);
    void** arr = reinterpret_cast<void**>(Rd<uintptr_t>(m + kMgrArray));
    if (!arr) return;
    for (uint32_t i = 0; i < count; ++i) {
        uintptr_t e = reinterpret_cast<uintptr_t>(arr[i]);
        if (!e) continue;
        if (Rd<uint8_t>(e + kEntDffReady))
            ReportDone(client, 1, Rd<uint32_t>(e + kEntDffCrc), Rd<uint32_t>(e + kEntDffSize));
        if (Rd<uint8_t>(e + kEntTxdReady))
            ReportDone(client, 2, Rd<uint32_t>(e + kEntTxdCrc), Rd<uint32_t>(e + kEntTxdSize));
    }
}

} // namespace nativeart
