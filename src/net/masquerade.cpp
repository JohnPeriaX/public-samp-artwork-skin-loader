#include "net/masquerade.h"

#include "core/log.h"
#include "core/samp.h"

#include <urmem.hpp>

#include <memory>
#include <vector>

namespace masq {
namespace {

std::vector<std::unique_ptr<urmem::patch>> g_patches;
bool g_enabled = false;

// patch one code byte at rva, only if it still holds expected (guards a wrong build or double-patch)
bool PatchByte(uintptr_t rva, uint8_t expected, uint8_t value) {
    auto addr = samp::Addr(rva);
    uint8_t cur = *reinterpret_cast<volatile uint8_t*>(addr);
    if (cur == value) {
        CS_LOGW("masq: rva 0x%06X already 0x%02X; leaving as-is", static_cast<unsigned>(rva), value);
        return true;
    }
    if (cur != expected) {
        CS_LOGE("masq: rva 0x%06X = 0x%02X, expected 0x%02X; refusing to patch",
                static_cast<unsigned>(rva), cur, expected);
        return false;
    }
    g_patches.emplace_back(std::make_unique<urmem::patch>(addr, urmem::bytearray_t { value }));
    CS_LOGI("masq: rva 0x%06X  0x%02X -> 0x%02X", static_cast<unsigned>(rva), expected, value);
    return true;
}

} // namespace

bool Apply() {
    if (g_enabled) return true;

    if (samp::GetRevision() == samp::Revision::R3_1) {
        // R3-1 Packet_ConnectionSucceeded has two exact 4057 (0x0FD9) uses:
        //   RVA 0x0AB02: xor edx, 0x0FD9h   (immediate low byte is RVA 0x0AB04)
        //   RVA 0x0AB79: store 0x0FD9h      (immediate low byte is RVA 0x0AB7D)
        // The surrounding assembly shows the first value is derived from the server challenge and
        // the second is written into the connection BitStream as the advertised client version.
        // Patch only D9 -> DE, with exact guards, so an unexpected R3-1 binary fails closed.
        if (!PatchByte(samp::r3_1::kDlChallengeImmLowByte, 0xD9, 0xDE)
            || !PatchByte(samp::r3_1::kDlVersionImmLowByte, 0xD9, 0xDE)) {
            g_patches.clear();
            CS_LOGE("masq: R3-1 DL masquerade failed; rolled back");
            return false;
        }
        g_enabled = true;
        CS_LOGI("masq: R3-1 DL masquerade ENABLED - 4057 -> 4062 (0.3DL)");
        return true;
    }

    if (!samp::IsR5Build()) {
        CS_LOGE("masq: unsupported revision; refusing DL masquerade");
        return false;
    }
    // 4057 -> 4062: flip the low byte 0xD9 -> 0xDE at the challenge xor and the version store
    if (!PatchByte(samp::r5::kChallengeImmLowByte, 0xD9, 0xDE)
        || !PatchByte(samp::r5::kVersionImmLowByte, 0xD9, 0xDE)) {
        g_patches.clear(); // rollback: urmem::patch dtor restores original bytes
        CS_LOGE("masq: apply failed; rolled back");
        return false;
    }
    g_enabled = true;
    CS_LOGI("masq: DL masquerade ENABLED - client now identifies as 0.3DL (4062)");
    return true;
}

bool Enabled() { return g_enabled; }

} // namespace masq
