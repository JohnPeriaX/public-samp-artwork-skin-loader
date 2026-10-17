// customskin-037 - asi entry point
#include <windows.h>

#include "core/log.h"
#include "core/samp.h"
#include "game/render.h"
#include "net/artwork.h"
#include "net/dlcompat.h"
#include "net/masquerade.h"
#include "net/nativeart.h"
#include "net/directdl.h"
#include "net/raknet.h"

namespace {

// wait up to timeoutMs for a module; also our "is this a sa-mp session?" check (samp.dll never
// loads in single player)
HMODULE WaitForModule(const char* name, DWORD timeoutMs) {
    const DWORD step = 100;
    DWORD waited = 0;
    for (;;) {
        if (HMODULE h = GetModuleHandleA(name)) return h;
        if (waited >= timeoutMs) return nullptr;
        Sleep(step);
        waited += step;
    }
}

DWORD WINAPI MainThread(LPVOID) {
    cs::LogInit();
    CS_LOGI("customskin-037 loading (built %s %s)", __DATE__, __TIME__);

    if (!WaitForModule("samp.dll", 60000)) {
        CS_LOGW("samp.dll not present after 60s; not a SA-MP session, exiting");
        return 0;
    }
    Sleep(500); // let samp.dll finish its own startup after the module maps in

    if (!samp::Init()) {
        CS_LOGE("failed to resolve samp.dll base");
        return 0;
    }
    samp::DumpFingerprint();
    CS_LOGI("samp.dll resolved @ 0x%08X", static_cast<unsigned>(samp::Base()));

    const auto revision = samp::GetRevision();

    if (net::Init()) {
        if (revision == samp::Revision::R5_1) {
            nativeart::Init();
            artwork::Init();
            if (masq::Apply() && dlcompat::Init() && render::Init())
                CS_LOGI("R5 native objects + skin clump-swap ready");
            else
                CS_LOGW("R5 artwork path initialization incomplete");
        } else if (revision == samp::Revision::R3_1) {
            if (!masq::Apply()) {
                CS_LOGE("R3-1 DL masquerade failed; artwork initialization remains disabled");
            } else if (!directdl::Init()) {
                CS_LOGE("R3-1 direct 0.3DL artwork RPC bridge initialization failed");
            } else if (!dlcompat::Init()) {
                CS_LOGE("R3-1 0.3DL packet compatibility hook initialization failed");
            } else {
                artwork::Init();
                if (render::Init())
                    CS_LOGI("R3-1 direct 0.3DL custom-skin artwork + clump-swap path ready");
                else
                    CS_LOGE("R3-1 renderer initialization failed; artwork downloads remain active but skins are not swapped");
            }
        } else {
            CS_LOGI("%s network compatibility ready; artwork path is intentionally isolated from this revision",
                    samp::RevisionName());
        }
    } else {
        CS_LOGE("net init failed");
    }

    // announce ourselves in chat once it exists; poll up to ~120s
    for (int i = 0; i < 1200; ++i) {
        if (samp::DebugChat("customskin loaded")) {
            break;
        }
        Sleep(100);
    }

    return 0;
}

} // namespace

BOOL APIENTRY DllMain(HINSTANCE hinst, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(hinst);
        CreateThread(nullptr, 0, MainThread, nullptr, 0, nullptr);
    }
    return TRUE;
}
