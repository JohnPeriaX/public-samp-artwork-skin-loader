# Custom Models for SA-MP 0.3.7 R3-1 and R5-1

A Windows x86 ASI plugin for **GTA: San Andreas + SA-MP 0.3.7** that makes open.mp custom artwork usable from the supported **R3-1** and **R5-1** clients.

This repository is deliberately revision-specific. It does not try to turn every 0.3.7 build into one universal binary. The two supported clients take different paths, so the code keeps those paths separate and checks the client build before installing hooks.

## Read the documentation

- **[English guide (EN.md)](EN.md)** - the detailed technical guide, debugging history, source walkthrough, and references.
- **[คู่มือภาษาไทย (TH.md)](TH.md)** - คู่มือภาษาไทยที่อธิบายการทำงาน เหตุผลที่เลือก R3-1/R5-1 และประสบการณ์จากการแก้ปัญหาจริง

## What is supported

| Client | Status | Artwork path |
|---|---|---|
| SA-MP 0.3.7 R3-1 | **PASS / runtime-tested** | Direct 0.3DL artwork RPC bridge + RenderWare skin clump swap |
| SA-MP 0.3.7 R5-1 | **Source/build path retained; runtime not claimed here** | Native artwork manager + download/cache bridge + skin clump swap |
| Other SA-MP builds | Unsupported | The plugin refuses to apply revision-specific hooks |

The R3-1 path has been visually tested with custom skins `20001` and `20002`, followed by a return to normal CJ. R5-1 remains intentionally conservative in the documentation: the source path is present and builds, but this README does not turn source/build evidence into a runtime PASS.

## How the two paths work

### R3-1: direct artwork bridge

R3-1 does not expose the same dormant native artwork path used by R5-1, so the plugin speaks the 0.3DL artwork messages directly.

`src/net/directdl.cpp` registers the artwork RPCs used by the direct path, keeps the model manifest, requests missing DFF/TXD files, accepts inline data, verifies CRC32, and commits completed downloads into the cache. `src/net/dlcompat.cpp` adapts the 0.3DL skin payloads back to the layouts expected by the R3-1 SA-MP handlers. `src/game/render.cpp` then maps a loaded custom clump onto the base skin model that SA-MP is already expecting.

The important idea is that the network layer does not ask the old client to understand a new skin layout by magic. It receives the richer 0.3DL payload, remembers the custom model ID, rewrites the packet to the R3-1 shape, and lets the existing player code continue doing its normal job.

### R5-1: native artwork manager with a missing downloader filled in

R5-1 is different. Its `samp.dll` already contains the 0.3DL artwork manager and object-rendering path, but the download worker is stubbed. `src/net/nativeart.cpp` wakes the native RPC handlers and supplies the missing file-fetch/cache step. The native manager can then see the completed files in its normal cache and continue through its own object pipeline.

Custom skins still need the project's RenderWare clump swap because the native R5 artwork path is not enough to make a custom skin replace the live player template by itself. The same `src/game/render.cpp` layer handles that part.

## Why the project is limited to R3-1 and R5-1

The public tree is intentionally focused on two client revisions whose code paths can be kept separate and explained from the available evidence.

R3-1 has a direct 0.3DL bridge and a verified runtime skin test. R5-1 follows the native artwork-manager path already present in the client, with the missing download/cache step supplied by this project. Keeping those paths isolated makes the revision gate, packet adaptation, artwork loading, and RenderWare integration easier to audit.

Unsupported revision-specific experiments are not kept in the public implementation. This avoids leaving stale hooks, mappings, or build paths that could be mistaken for supported behavior.

The result is deliberately simple: the public repository advertises R3-1 and R5-1 only, and each revision's runtime status is stated separately rather than inferred from source presence alone.

## Source code guide

The code is split by responsibility so the protocol and revision-specific hooks are easier to reason about.

### `src/core`

- `samp.cpp` / `samp.h` identify the loaded `samp.dll` build using PE information and the verified R3-1/R5-1 registry.
- `log.cpp` / `log.h` provide the diagnostic logging used while bringing the hooks up.

The revision gate is intentionally simple. The relevant logic is essentially:

```cpp
if (g_imageSize == r5::kImageSize && g_timeDateStamp == r5::kTimeDateStamp)
    g_revision = Revision::R5_1;
else if (g_entryPointRva == r3_1::kEntryPoint)
    g_revision = Revision::R3_1;
```

The point is not to guess a version from a friendly filename. Hooks only become available after the loaded binary matches a known build.

### `src/net/masquerade.cpp`

This is the connection-side compatibility layer. The plugin presents the client as 0.3DL where the server's artwork system requires that identity, including the expected protocol/version values used by the supported path.

The masquerade is deliberately kept separate from the artwork parser. That makes it possible to reason about "what the server thinks the client is" independently from "how the client handles an artwork packet".

### `src/net/dlcompat.cpp`

This is the revision bridge for player-skin RPCs.

For R3-1, a 0.3DL skin message contains a player ID, base skin, and custom skin. The native R3-1 handler expects the older 0.3.7 layout, so the hook records the custom ID and rewrites the payload before calling the original handler.

A simplified version of the real transformation is:

```cpp
const uint16_t playerId = *reinterpret_cast<uint16_t*>(d + 0);
const uint32_t skin = *reinterpret_cast<uint32_t*>(d + 2);
const uint32_t customSkin = *reinterpret_cast<uint32_t*>(d + 6);

*reinterpret_cast<uint32_t*>(d + 0) = playerId;
*reinterpret_cast<uint32_t*>(d + 4) = skin;
p->numberOfBitsOfData = 64;
```

The custom ID is kept in a small player-to-custom-model map. That map is what tells the renderer which loaded artwork should be worn by a particular player.

R5-1 uses a separate hook for `ScrSetPlayerSkin`. It performs the corresponding 0.3DL-to-0.3.7 payload adaptation without mixing R3-1 addresses or handler assumptions into the R5 code.

### `src/net/directdl.cpp`

This is the R3-1 artwork client implemented in the plugin.

It handles the artwork RPCs used by the direct path, including the virtual-world notification, model manifest, URL/inline artwork delivery, and download completion. It tracks each model's base ID, custom ID, DFF/TXD CRCs, sizes, and virtual-world state.

The download flow is intentionally defensive:

1. Receive the model description.
2. Check the cache.
3. Request or download missing DFF/TXD data.
4. Verify the CRC32.
5. Write to a `.part` file.
6. Rename the completed file into the cache.
7. Tell the artwork state machine that the required files are ready.

The project also guards against duplicate requests. A CRC/type key remains pending until the main thread has committed the `.part` file, which avoids a second RPC enqueueing the same transfer during the hand-off between the worker thread and the game thread.

### `src/net/nativeart.cpp`

This is the R5-1 bridge into the artwork manager already present inside `samp.dll`.

The important detail is that the R5 client already contains much more of the 0.3DL system than it initially appears to. The missing practical piece is the download worker. The plugin hooks the model URL handler, downloads the requested file, verifies it, and places it in the native cache using the filename the native manager expects.

The relevant idea is:

```cpp
dl::Enqueue(url, temp, crc, isDff);
// later: .part -> native cache filename
```

The native manager can then discover the finished file through its own readiness checks. Objects stay on the native artwork pipeline; skins are exposed to the renderer through the manager's model entries.

### `src/model`

`download.cpp` owns the asynchronous HTTP worker. It uses WinINet, sends the `SAMP/0.3` user agent expected by the artwork web path, writes the response to a temporary file, and verifies the requested CRC32 before reporting success.

The worker deliberately does not mutate game objects. It only communicates through a job queue and a completed-result queue. The main/game thread commits the cache file and updates artwork state.

The core request looks like this:

```cpp
HINTERNET hNet = InternetOpenA(
    "SAMP/0.3", INTERNET_OPEN_TYPE_PRECONFIG, nullptr, nullptr, 0);
```

That small detail turned out to matter: a downloader that works against an ordinary web server is not automatically compatible with the artwork endpoint.

### `src/game`

`render.cpp` is the final step that makes a downloaded custom skin visible.

The plugin does not replace every ped model globally. It remembers the original RenderWare clump for a base model, installs the custom clump while that custom skin is actually assigned, and restores the original clump when the custom state is removed.

The R3-1 spawn hooks also translate a custom model ID back to its base model ID before SA-MP builds the player object. The custom clump is already prepared, so the resulting ped uses the expected base slot while displaying the custom artwork.

The per-frame hook reconciles the artwork state and catches cases where GTA's streamer unloads a model. That prevents a one-time swap from becoming a stale pointer after the game reloads the model.

### `src/net/raknet.cpp` and `src/net/artwork.cpp`

These files provide the low-level network access and common artwork representation used by the supported revision paths. Keeping RakNet access in one place prevents each revision handler from inventing its own connection logic.

### `vendor`

The repository keeps the small third-party headers/utilities required by the plugin, including RakNet interfaces and urmem hooking support.

## Runtime test harness

The local test harness uses two custom skins:

- `20001` based on skin `1`
- `20002` based on skin `2`

The server-side test commands are:

- `/skin1`
- `/skin2`
- `/normal`

The R3-1 runtime sequence that was actually observed was custom `20001` -> custom `20002` -> normal CJ, with the corresponding server commands and client logs agreeing with the visual result.

R5-1 is not labelled runtime PASS here unless a fresh live test proves it.

## Build

The plugin is a 32-bit Windows ASI built with MSVC.

```sh
msbuild build/custommodels.vcxproj /p:Configuration=Release /p:Platform=Win32
```

Or with CMake:

```sh
cmake -B build/cmake -A Win32 -DCMAKE_BUILD_TYPE=Release
cmake --build build/cmake --config Release
```

The output is:

```text
build/bin/Release/custommodels.asi
```

## Requirements

- GTA: San Andreas 1.0 US
- SA-MP 0.3.7 R3-1 or R5-1
- A compatible ASI loader
- `custommodels.asi` next to `gta_sa.exe`
- An open.mp server with artwork enabled
- At least one custom model such as `AddCharModel` for the skin tests

## Protocol and implementation notes

The wire-format notes are kept in [`docs/protocol.md`](docs/protocol.md).

The code uses fixed addresses only after revision detection. This is necessary for old SA-MP binaries because a function address that is correct for one build is not automatically correct for another build.

If you are porting this project to another client revision, treat the existing R3-1 and R5-1 mappings as evidence for those exact builds, not as a template of interchangeable offsets.

## Credits and references

A special thank-you to **Ricardo Fernandes (`ricardoofnl`)** for the public R5 research and implementation that helped make the R5 direction much easier to understand. In particular, the repository [`ricardoofnl/customskin-0.3.7-R5`](https://github.com/ricardoofnl/customskin-0.3.7-R5) documents the useful observation that R5 already contains a native 0.3DL artwork manager while its download worker is effectively missing. That architecture was an important reference when structuring the R5 side of this project.

Credit here does **not** mean this repository is a copy of Ricardo's project. The R3-1 direct bridge, the revision detection, the R3-1 payload adapters, the renderer integration, the test harness, and the debugging decisions in this repository are maintained as this project's own work. Where an idea or protocol was used as a reference, the original project is linked explicitly.

Other references:

- [ricardoofnl/customskin-0.3.7-R5](https://github.com/ricardoofnl/customskin-0.3.7-R5) - R5 native artwork-manager research and implementation reference.
- [openmultiplayer/open.mp](https://github.com/openmultiplayer/open.mp) - custom-model protocol and server-side artwork implementation, especially `Shared/NetCode/custommodels.hpp` and the CustomModels server component.
- [spmn/samp-client-compat](https://github.com/spmn/samp-client-compat) - SA-MP/RakNet compatibility reference.
- [spmn/urmem](https://github.com/spmn/urmem) - x86 hook/trampoline support used by this project.
- [BlastHackNet/SAMP-API](https://github.com/BlastHackNet/SAMP-API) - SA-MP address and client-layout reference material.
- [DK22Pac/plugin-sdk](https://github.com/DK22Pac/plugin-sdk) - GTA San Andreas and RenderWare reference material.
- [AmyrAhmady/sampvoice](https://github.com/AmyrAhmady/sampvoice) - useful GTA/SA-MP class-layout reference material.

## Project philosophy

The project is intentionally small in one important way: when a client revision cannot be explained well enough to be trusted, it is better to leave that revision unsupported than to ship a hook that merely looks plausible in a debugger.

That is why R3-1 is documented from an actual runtime result, while R5-1 is kept as a separate implementation whose runtime status is stated honestly.
