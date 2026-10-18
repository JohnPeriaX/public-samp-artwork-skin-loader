# English technical guide

## What this project is

This is a Windows x86 ASI for GTA San Andreas and SA-MP 0.3.7. It is built around two specific clients, R3-1 and R5-1, and the artwork protocol used by open.mp.

The project is not a generic patcher for every SA-MP build. Old clients have different function layouts and different assumptions about artwork packets. The plugin detects the loaded client first and only installs the hooks that belong to that build.

## The short version

There are two different implementations here.

**R3-1 uses a direct 0.3DL bridge.** The plugin receives the artwork RPCs, stores the model manifest, downloads and verifies the files, and adapts the custom-skin packets so the ordinary R3-1 player code can continue to run. The renderer then swaps the loaded RenderWare clump for the base skin model.

**R5-1 uses the native artwork manager already inside samp.dll.** The R5 client contains most of the 0.3DL machinery, including the model manager and native object path. The missing part is the download worker, so this project supplies that piece and lets the native manager consume the finished cache files. Custom skins still use the project's clump-swap renderer.

## Why the project is limited to R3-1 and R5-1

The public tree is intentionally focused on two client revisions whose code paths can be kept separate and explained from the available evidence.

R3-1 has a direct 0.3DL bridge and a verified runtime skin test. R5-1 follows the native artwork-manager path already present in the client, with the missing download/cache step supplied by this project. Keeping those paths isolated makes the revision gate, packet adaptation, artwork loading, and RenderWare integration easier to audit.

Unsupported revision-specific experiments are not kept in the public implementation. This avoids leaving stale hooks, mappings, or build paths that could be mistaken for supported behavior.

The result is deliberately simple: the public repository advertises R3-1 and R5-1 only, and each revision's runtime status is stated separately rather than inferred from source presence alone.

## R3-1 implementation

### 1. Revision detection

`src/core/samp.cpp` reads the PE headers of `samp.dll`. R5-1 is recognized from its known image size and timestamp; R3-1 is recognized from its verified entry point.

This gives every later subsystem a simple question: "Which exact client am I running against?"

### 2. 0.3DL connection identity

`src/net/masquerade.cpp` handles the protocol-version identity used during connection setup. The goal is to make the server expose the artwork path to the supported client.

This is kept separate from packet parsing. If a connection identity problem occurs, it can be debugged without changing the model parser.

### 3. Direct artwork RPCs

`src/net/directdl.cpp` registers the R3-1 artwork messages used by this implementation. The important RPCs are the artwork virtual-world state, model manifest, model URL/inline data, and download completion.

The model manifest becomes a local table containing:

For the current R3 extension, the two model classes are handled by the native R3 0.3.DL manager. Custom skins use `type=1`; simple objects use the native object class and custom IDs from `-1000` through `-30000`. IDA confirms that R3's dormant manager already routes negative IDs through `CObjectPool::Create`, so the plugin now supplies the native cache and activates those handlers instead of indexing GTA's `CModelInfo` table with a negative ID. The object path is still not a runtime PASS until a live custom-object test succeeds.

- custom model ID
- base model ID
- DFF CRC
- TXD CRC
- file sizes
- virtual-world state

Before a file is downloaded, the cache is checked. When a URL is received, the download worker writes to a `.part` path and verifies the CRC. Only after that succeeds is the file renamed to its final cache name.

### 4. Skin payload adaptation

`src/net/dlcompat.cpp` is the boundary between the 0.3DL skin packet and the older R3-1 skin handlers.

The 0.3DL payload has an extra custom-skin field. R3-1's normal handler expects a shorter packet. The hook captures the custom ID, remembers it for the player, moves the base skin into the location expected by the old handler, and reduces the bit count before calling the original function.

The same principle is used for the world-player and spawn-info messages: remove the 0.3DL custom-skin field while preserving the rest of the R3-1 structure.

The code is deliberately strict about payload length. Short or malformed messages are allowed to pass through rather than being rewritten using incomplete data.

### 5. RenderWare clump swapping

`src/game/render.cpp` connects the network state to the actual visible player.

For each ready custom model, it finds the GTA model-info slot for the base skin and remembers the original RenderWare clump. If that custom model is currently assigned to a player, the custom clump is installed in the base slot.

When the model is no longer needed, the original clump is restored. If GTA's streamer has already unloaded the custom clump, the renderer drops the stale pointer instead of writing it back into a newly loaded model.

The renderer also hooks the R3-1 spawn path so a custom model ID can be translated to the correct base model ID before SA-MP constructs the ped.

The current R3 extension now uses the stock R3 0.3.DL object pipeline. The plugin downloads verified DFF/TXD files into the native manager's cache, forwards RPC 179/183/185 to the validated R3 handlers, and mirrors type-2 object entries for diagnostics and cache-backed metadata. GTA `CModelInfo::AddClumpModel` is not used for negative SA-MP object IDs. It remains **in progress** until a live `AddSimpleModel` + `CreateObject` test succeeds.

## R5-1 implementation

The R5 path is intentionally separate from R3-1.

`src/net/nativeart.cpp` registers R5's native artwork handlers and wraps the URL handler. The native R5 manager already understands the artwork model list and can render custom objects. The missing worker is supplied by this project.

The download flow is:

```text
0.3DL ModelUrl
      |
      v
nativeart.cpp
      |
      v
model/download.cpp
      |
      v
CRC32 verification
      |
      v
<CRC>.part
      |
      v
<CRC>.dff / <CRC>.txd
      |
      v
R5 native artwork manager
```

That is why the R5 code does not recreate the whole object renderer. It fills the missing link and leaves the native manager in charge of the rest.

## Download worker

`src/model/download.cpp` keeps HTTP I/O away from the game thread.

A small worker queue owns the network operation. The worker downloads into a temporary file, checks the CRC32, and publishes a result. The main thread later commits the file and updates game-side state.

The worker uses the artwork endpoint's expected user agent:

```cpp
InternetOpenA("SAMP/0.3", INTERNET_OPEN_TYPE_PRECONFIG, nullptr, nullptr, 0);
```

This is not decorative. The server-side artwork path expects the SA-MP user agent, so a normal browser-like user agent is not a reliable substitute.

## Network safety details

A recurring theme in this project is not trusting a packet merely because it has the right RPC ID.

The code checks bit counts before reading fields. It avoids sending the final download-complete RPC while the model manifest is still incomplete. It suppresses duplicate download requests and keeps a request marked pending until the cache commit has actually succeeded.

These small rules matter because old game clients are not forgiving when a hook consumes a malformed packet or when a half-written model becomes visible to the native loader.

## What was actually tested

R3-1 has real visual evidence from the local artwork test harness:

1. `/skin1` produced custom skin `20001`.
2. `/skin2` produced custom skin `20002`.
3. `/normal` returned to the normal CJ model.

The client logs also showed R3-1 detection, the artwork RPC bridge, RenderWare loading for both custom models, and the expected skin transitions.

R5-1 has source/build preservation but is not called a runtime PASS in this documentation. That distinction is intentional.

## Latest PC Release build verification

The current source tree was rebuilt successfully with Visual Studio 2022 Community MSBuild:

- Configuration: `Release | Win32`
- MSBuild: `17.14.60`
- MSVC: `14.44.35207`
- Result: **0 warnings, 0 errors**
- Output: `build/bin/Release/custommodels.asi`
- SHA-256: `A65E526E58DED5432D9B4CFCDF37C559CF457A925329C6E4C2C59108885041D0`

This confirms the current source/build path only. It does **not** promote the R3 custom-object work to runtime PASS and does not claim R5 runtime PASS.

## Credits

This project owes a specific debt of understanding to **Ricardo Fernandes (`ricardoofnl`)** and the public repository [`customskin-0.3.7-R5`](https://github.com/ricardoofnl/customskin-0.3.7-R5). His R5 implementation documents a very useful architectural fact: R5 already contains a native 0.3DL artwork client, while the download worker is the part that needs to be supplied.

That project was read as a reference for the R5 direction. This repository is not presented as a mirror of it. The R3-1 direct RPC bridge, its payload rewrites, the RenderWare integration, the revision gates, and the debugging history are specific to this project.

Additional references:

- [openmultiplayer/open.mp](https://github.com/openmultiplayer/open.mp)
- [spmn/samp-client-compat](https://github.com/spmn/samp-client-compat)
- [spmn/urmem](https://github.com/spmn/urmem)
- [BlastHackNet/SAMP-API](https://github.com/BlastHackNet/SAMP-API)
- [DK22Pac/plugin-sdk](https://github.com/DK22Pac/plugin-sdk)
- [AmyrAhmady/sampvoice](https://github.com/AmyrAhmady/sampvoice)

See `README.md` for the project overview and the complete reference list.
