#include "net/raknet.h"

#include "core/log.h"
#include "core/samp.h"

#include <windows.h>
#include <urmem.hpp>
#include <raknet/rakclient.h> // RakClientInterface, RPCParameters, BitStream, priorities

#include <utility>
#include <vector>

namespace net {
namespace {

urmem::hook g_connHook;
RakClientInterface* g_rak = nullptr;
std::vector<std::pair<uint8_t, RpcHandler>> g_wanted; // desired incoming handlers

bool IsReadable(const void* pointer, size_t length) {
    if (!pointer || length == 0) return false;
    uintptr_t start = reinterpret_cast<uintptr_t>(pointer);
    uintptr_t end = start + length;
    if (end < start) return false;
    while (start < end) {
        MEMORY_BASIC_INFORMATION mbi{};
        if (!VirtualQuery(reinterpret_cast<LPCVOID>(start), &mbi, sizeof(mbi)) ||
            mbi.State != MEM_COMMIT || (mbi.Protect & (PAGE_GUARD | PAGE_NOACCESS))) {
            return false;
        }
        const uintptr_t regionEnd = reinterpret_cast<uintptr_t>(mbi.BaseAddress) + mbi.RegionSize;
        if (regionEnd <= start) return false;
        start = (regionEnd < end) ? regionEnd : end;
    }
    return true;
}

bool IsExecutable(const void* pointer) {
    if (!pointer) return false;
    MEMORY_BASIC_INFORMATION mbi{};
    if (!VirtualQuery(pointer, &mbi, sizeof(mbi)) || mbi.State != MEM_COMMIT ||
        (mbi.Protect & (PAGE_GUARD | PAGE_NOACCESS))) return false;
    const DWORD protection = mbi.Protect & 0xFFU;
    return protection == PAGE_EXECUTE || protection == PAGE_EXECUTE_READ ||
           protection == PAGE_EXECUTE_READWRITE || protection == PAGE_EXECUTE_WRITECOPY;
}

// CNetGame::GetRakClient() via RefNetGame(). valid once a CNetGame exists
RakClientInterface* ResolveRak() {
    if (!samp::Base()) return nullptr;

    uintptr_t refNetGame = 0;
    uintptr_t getRakClient = 0;
    switch (samp::GetRevision()) {
    case samp::Revision::R3_1:
        refNetGame = samp::r3_1::kRefNetGame;
        getRakClient = samp::r3_1::kCNetGame_GetRakClient;
        break;
    case samp::Revision::R5_1:
        refNetGame = samp::r5::kRefNetGame;
        getRakClient = samp::r5::kCNetGame_GetRakClient;
        break;
    default:
        return nullptr;
    }

    const uintptr_t netGameSlot = samp::Addr(refNetGame);
    const uintptr_t getRakClientAddress = samp::Addr(getRakClient);
    if (!IsReadable(reinterpret_cast<const void*>(netGameSlot), sizeof(void*)) ||
        !IsExecutable(reinterpret_cast<const void*>(getRakClientAddress))) {
        return nullptr;
    }

    void* netgame = *reinterpret_cast<void**>(netGameSlot);
    if (!IsReadable(netgame, sizeof(void*))) return nullptr;

    RakClientInterface* rak = urmem::call_function<urmem::calling_convention::thiscall, RakClientInterface*>(
        getRakClientAddress, netgame);
    if (!IsReadable(rak, sizeof(void*))) return nullptr;

    void** vtable = *reinterpret_cast<void***>(rak);
    if (!IsReadable(vtable, 19 * sizeof(void*))) return nullptr;
    constexpr size_t kCheckedVtable[] = { 7, 8, 9, 18 };
    for (size_t index : kCheckedVtable) {
        if (!IsExecutable(vtable[index])) return nullptr;
    }
    return rak;
}

void ApplyRegistrations() {
    g_rak = ResolveRak();
    if (!g_rak) {
        CS_LOGW("rak: no RakClient available at ConnectionSucceeded");
        return;
    }
    for (auto& entry : g_wanted) {
        int uid = entry.first;
        g_rak->RegisterAsRemoteProcedureCall(&uid, entry.second);
    }
    CS_LOGI("rak: RakClient=%p, (re)registered %zu incoming RPC handler(s)",
            reinterpret_cast<void*>(g_rak), g_wanted.size());
}

// CNetGame::Packet_ConnectionSucceeded(this, packet*); __fastcall captures the x86 thiscall
void __fastcall Hooked_ConnectionSucceeded(void* self, void* /*edx*/, void* packet) {
    g_connHook.call<urmem::calling_convention::thiscall, void>(self, packet);
    CS_LOGI("rak: ConnectionSucceeded");
    ApplyRegistrations();
}

} // namespace

bool Init() {
    if (!samp::Base() || !samp::IsSupportedNetworkRevision()) {
        CS_LOGE("rak: unsupported or unresolved samp build");
        return false;
    }

    // CNetGame/RakClient is created later than samp.dll itself. Match the verified reference
    // transport's 30-second startup window so connection initialization is not lost to timing.
    for (int attempt = 0; attempt < 600 && !g_rak; ++attempt) {
        g_rak = ResolveRak();
        if (!g_rak) Sleep(50);
    }
    if (!g_rak) {
        CS_LOGE("rak: could not resolve RakClient for %s", samp::RevisionName());
        return false;
    }

    if (samp::GetRevision() == samp::Revision::R5_1) {
        g_connHook.install(
            samp::Addr(samp::r5::kCNetGame_Packet_ConnectionSucceeded),
            urmem::get_func_addr(&Hooked_ConnectionSucceeded),
            urmem::hook::type::jmp);
        CS_LOGI("rak: R5 ConnectionSucceeded hook installed @ 0x%08X",
                static_cast<unsigned>(samp::Addr(samp::r5::kCNetGame_Packet_ConnectionSucceeded)));
    } else {
        ApplyRegistrations();
        CS_LOGI("rak: %s direct RakClient path enabled", samp::RevisionName());
    }
    return true;
}

RakClientInterface* Rak() {
    if (!g_rak) g_rak = ResolveRak();
    return g_rak;
}

void RegisterIncomingRPC(uint8_t id, RpcHandler handler) {
    g_wanted.emplace_back(id, handler);
    if (RakClientInterface* rc = Rak()) { // already connected: apply now too
        int uid = id;
        rc->RegisterAsRemoteProcedureCall(&uid, handler);
        CS_LOGI("rak: registered incoming RPC %u (immediate)", id);
    }
}

bool SendRPC(uint8_t id, BitStream* payload) {
    RakClientInterface* rc = Rak();
    if (!rc) {
        CS_LOGW("rak: SendRPC %u but not connected", id);
        return false;
    }
    int uid = id;
    bool ok = rc->RPC(&uid, payload, HIGH_PRIORITY, RELIABLE_ORDERED, 0, false);
    CS_LOGI("rak: sent RPC %u ok=%d", id, static_cast<int>(ok));
    return ok;
}

bool SendEmptyRPC(uint8_t id) {
    // 0.3DL FinishedDownloading (RPC 184) carries one unused byte, conventionally 0x80.
    BitStream bs;
    uint8_t unused = 0x80;
    bs.Write(unused);
    return SendRPC(id, &bs);
}

} // namespace net
