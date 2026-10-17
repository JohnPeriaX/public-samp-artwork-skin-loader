// samp.dll access layer for sa-mp 0.3.7-R5-1
#pragma once

#include <cstdint>

namespace samp {

enum class Revision : uint8_t {
    Unknown,
    R3_1,
    R5_1,
};

// Build-specific RVAs for the supported SA-MP 0.3.7 R3-1 and R5-1 clients.
namespace r3_1 {
constexpr uintptr_t kEntryPoint = 0x0CC4D0;
constexpr uintptr_t kRefNetGame = 0x26E8DC;
constexpr uintptr_t kCNetGame_GetRakClient = 0x1A40;
constexpr uintptr_t kCNetGame_Packet_ConnectionSucceeded = 0x0AA20;
constexpr uintptr_t kCPlayerPool_Process = 0x13470;
constexpr uintptr_t kCRemotePlayer_Spawn = 0x16A90;
constexpr uintptr_t kCLocalPlayer_Spawn = 0x03AD0;

// R3-1 0.3DL RPC handlers whose wire payloads gain a u32 CustomSkin field.
constexpr uintptr_t kScrWorldPlayerAdd = 0x107D0; // RPC 32
constexpr uintptr_t kScrSetPlayerSkin = 0x18AA0;  // RPC 153
constexpr uintptr_t kScrSetSpawnInfo = 0x17870;   // RPC 68

// R3-1's Packet_ConnectionSucceeded contains exactly two 0x0FD9 (4057) uses:
// challenge-response XOR at RVA 0x0AB02 and advertised version write at RVA 0x0AB79.
// Patch only the low immediate byte D9 -> DE (4062) after exact byte guards.
constexpr uintptr_t kDlChallengeImmLowByte = 0x0AB04;
constexpr uintptr_t kDlVersionImmLowByte = 0x0AB7D;
}

// rva constants for 0.3.7-R5-1
namespace r5 {

// global pointers-to-object (the value at base+rva is itself a pointer)
constexpr uintptr_t kRefChat = 0x26EB80;    // CChat**
constexpr uintptr_t kRefNetGame = 0x26EB94; // CNetGame**

// pe fingerprint of the validated 0.3.7-R5-1 samp.dll; build guard before applying patches
constexpr uint32_t kImageSize = 0x0027E000;
constexpr uint32_t kTimeDateStamp = 0x6372C39E;

// CChat methods (thiscall)
constexpr uintptr_t kCChat_AddMessage = 0x68170; // (this, D3DCOLOR color, const char* text)
constexpr uintptr_t kCChat_AddEntry = 0x67BE0;   // (this, int type, text, prefix, textColor, prefixColor)

// CNetGame methods (thiscall)
constexpr uintptr_t kCNetGame_GetRakClient = 0xBBC0;              // > RakClientInterface*
constexpr uintptr_t kCNetGame_Process = 0xB5B0;                  // per-frame; our main-thread tick
constexpr uintptr_t kCNetGame_Connect = 0x8940;                  // anchor
constexpr uintptr_t kCNetGame_Packet_ConnectionSucceeded = 0xAD80; // anchor (auth/token path)

// ScrSetPlayerSkin(RPCParameters*), __cdecl. dl adds a u32 CustomSkin that misaligns the 0.3.7 parser
constexpr uintptr_t kScrSetPlayerSkin = 0x19190;

// dormant native 0.3dl artwork rpc handlers; registering them activates the native pipeline.
// void __cdecl(RPCParameters*)
constexpr uintptr_t kNativeModelRequest = 0x11EB0;      // RPC179 s->c: register model, build manager
constexpr uintptr_t kNativeModelUrl = 0x120B0;          // RPC183 s->c: url / inline file bytes
constexpr uintptr_t kNativeDownloadCompleted = 0xEC30;  // RPC185 s->c: finalize + unfreeze
constexpr uintptr_t kNativeModelReset = 0xEBA0;         // s->c: reset/clear manager (rpc id TBD)

// native "Updating models.." download dialog; we call the updater ourselves to unstick it from 0%
constexpr uintptr_t kDownloadClient = 0x26EB58;         // CHTTPClient* (dialog + per-file slots)
constexpr uintptr_t kDownloadUiUpdate = 0x6ACE0;        // __thiscall(client,fileType,crc,state,bytes,size,i64): state 4=done

// dl masquerade patch sites: flip the version low byte 0xD9 (4057) -> 0xDE (4062) at each
constexpr uintptr_t kChallengeImmLowByte = 0xAE64; // 0xD9 -> 0xDE
constexpr uintptr_t kVersionImmLowByte = 0xAECE;   // 0xD9 -> 0xDE

} // namespace r5

// resolve samp.dll base and read its fingerprint. false if not loaded yet; safe to call repeatedly
bool Init();

// Detected exact client revision. Unknown means the entry point is not in the verified registry.
Revision GetRevision();
const char* RevisionName();
bool IsSupportedNetworkRevision();

// samp.dll load base, or 0 before a successful Init()
uintptr_t Base();

// rva -> absolute address in samp.dll
inline uintptr_t Addr(uintptr_t rva) { return Base() + rva; }

// pe fingerprint of the loaded samp.dll (for verifying the build is really R5-1)
uint32_t ImageSize();
uint32_t TimeDateStamp();

// true if the loaded samp.dll matches the validated R5-1 fingerprint (patches refuse to apply otherwise)
bool IsR5Build();

// log the fingerprint + bytes at a few anchors, to confirm the offset table matches this samp.dll
void DumpFingerprint();

// print a white line to sa-mp chat; false (file log only) if the chat object doesn't exist yet
bool DebugChat(const char* fmt, ...);

} // namespace samp
