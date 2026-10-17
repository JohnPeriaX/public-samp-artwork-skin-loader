# Local R3-1 custom-skin test harness

This harness targets **SA-MP 0.3.7 R3-1** and exercises the custom artwork path using two real ped/skin
asset pairs already included in `test/models/`. It intentionally registers skins only, not custom objects.
The assistant does not start the server or game; run the steps below manually when ready.

## Files used

- `config.json`: open.mp artwork enabled; game/server port `7778`, artwork port `7778`, model root `models`.
- `gamemodes/test.pwn`: registers `AddCharModel(1, 20001, "people001.dff", "people001.txd")` and
  `AddCharModel(2, 20002, "people002.dff", "people002.txd")`.
- `models/`: an isolated folder containing only the four real DFF/TXD files. It deliberately has no
  `artconfig.txt`, so unrelated model definitions in `models/` are not picked up by this test.
- `gamemodes/test.amx`: compiled from the current `gamemodes/test.pwn`.

## Prerequisites

- Use the existing Windows open.mp test server in this folder (`omp-server.exe`) and its `components/`.
- The bundled compiler is `qawno/pawncc.exe`; `qawno/include` has the open.mp declarations needed by the script.
- The test client must be the exact SA-MP **0.3.7 R3-1 / 0.3DL-R3-1** binary used by the project, with the freshly built
  `build/bin/Release/custommodels.asi` copied/loaded through the user's existing local ASI loader as appropriate.

## Compile the test gamemode

Run from `test/`:

```powershell
.\qawno\pawncc.exe gamemodes/test.pwn -ogamemodes/test.amx -iinclude
```

The command must exit with code `0`, and `gamemodes/test.amx` must have a fresh timestamp.

## Manual runtime test

1. Close any previous instance of this local test server. Open a terminal with working directory set to this `test/` directory.
2. Start `omp-server.exe` manually. Confirm it reports the two real custom skins registered and that the artwork web server is active on the configured port.
3. Launch the local SA-MP **R3-1** test client with the current ASI and connect to `127.0.0.1:7778`.
4. On spawn, skin `20001` (`people001`) is applied automatically. Use `/skin1` and `/skin2` to re-apply either skin, and `/normal` to return to the default ped.
5. Stop the server/client manually after capturing logs. Do not use the World NextX production environment.

## Evidence required for R3-1 acceptance

Client `customskin.log` should show all of the following:

- The R3-1 fingerprint and guarded hook initialization succeeded.
- `ConnectionSucceeded` followed by RPC `179` manifest rows for IDs `20001` and `20002`.
- RPC `183` URL notifications for DFF and TXD, followed by successful download/CRC logs.
- `artwork: skin 20001 RenderWare-loaded (R3-1 cache)` with non-null `clump` and `txd` values; the equivalent for `20002` is also desirable.
- Render logs confirming the custom skin ID is assigned and its base-model template is swapped.

The server log should include `AddCharModel 20001` and `AddCharModel 20002`, requests for the four corresponding files, and `OnPlayerFinishedDownloading`. If the client disconnects before RPC 179 or RPC 183, include the exact timestamped server/client logs; that is a protocol/connection blocker, not proof that rendering failed.

## Keep the evidence honest

A successful compile or CRC match alone does not prove that a DFF/TXD parses as valid RenderWare or that a custom ped was visibly rendered. R3-1 is complete only after actual-model loading and assignment are supported by runtime logs. R3-1 and R5 remain out of scope until then.
