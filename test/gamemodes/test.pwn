// customskin-037 local R3-1 skin test gamemode (open.mp)
//
// Use REAL ped/skin artwork already installed under test/models:
//   skin A: id 20001, base 1  <- models/people001.dff + models/people001.txd
//   skin B: id 20002, base 2  <- models/people002.dff + models/people002.txd
// The test is intentionally SKIN ONLY. No custom objects are registered or spawned.
// Their model files are served by the local open.mp artwork webserver, so the R3-1 client
// receives RPC 179, requests RPC 181/182, downloads RPC 183 files, verifies CRC, and
// loads the real ped clump/TXD before SetPlayerSkin applies the custom model.
//
// The harness keeps the normal class-selection screen active so << / >> can choose
// normal, custom 20001, or custom 20002. After spawn, /skin1 and /skin2 can re-apply either model.
//
// Both registered models use their real paired DFF/TXD files already present under models/.
// The two /skin commands exercise separate base model IDs and separate artwork cache entries.
//
// compile to gamemodes/test.amx (see test/readme.md) with a recent omp-stdlib

#include <open.mp>

#define SKIN_A_ID   20001
#define SKIN_A_BASE 1
#define SKIN_B_ID   20002
#define SKIN_B_BASE 2

// normal spawn skin (cj). deliberately NOT a custom base id (1/2) so it never picks up a
// swapped template
#define DEFAULT_SKIN 0

// ls international airport (terminal apron, on the ground, facing the doors)
#define SPAWN_X 1685.9645
#define SPAWN_Y -2329.8933
#define SPAWN_Z 13.5469
#define SPAWN_A 180.0

public OnGameModeInit()
{
    print("[test][boot] OnGameModeInit: begin customskin R3-1 diagnostic harness");
    printf("[test][config] skinA=%d baseA=%d skinB=%d baseB=%d default=%d spawn=(%.4f, %.4f, %.4f, %.2f)",
        SKIN_A_ID, SKIN_A_BASE, SKIN_B_ID, SKIN_B_BASE, DEFAULT_SKIN,
        SPAWN_X, SPAWN_Y, SPAWN_Z, SPAWN_A);
    SetGameModeText("customskin-037 test");

    // register both custom skins. open.mp sends rpc 179 (ModelRequest) to dl clients
    printf("[test][models] AddCharModel request custom=%d base=%d dff=people001.dff txd=people001.txd", SKIN_A_ID, SKIN_A_BASE);
    if (AddCharModel(SKIN_A_BASE, SKIN_A_ID, "people001.dff", "people001.txd"))
        print("[test][models] AddCharModel custom=20001 base=1 REAL people001 OK");
    else
        print("[test][models][ERROR] AddCharModel custom=20001 base=1 FAILED - check models/people001.dff + people001.txd");

    printf("[test][models] AddCharModel request custom=%d base=%d dff=people002.dff txd=people002.txd", SKIN_B_ID, SKIN_B_BASE);
    if (AddCharModel(SKIN_B_BASE, SKIN_B_ID, "people002.dff", "people002.txd"))
        print("[test][models] AddCharModel custom=20002 base=2 REAL people002 OK");
    else
        print("[test][models][ERROR] AddCharModel custom=20002 base=2 FAILED - check models/people002.dff + people002.txd");

    // Keep the normal SA-MP class-selection screen active. Do not auto-call SpawnPlayer
    // from OnPlayerConnect, otherwise the client skips the << / >> class-selection UI.
    AddPlayerClass(DEFAULT_SKIN, SPAWN_X, SPAWN_Y, SPAWN_Z, SPAWN_A,
        WEAPON_FIST, 0, WEAPON_FIST, 0, WEAPON_FIST, 0);
    AddPlayerClass(SKIN_A_ID, SPAWN_X, SPAWN_Y, SPAWN_Z, SPAWN_A,
        WEAPON_FIST, 0, WEAPON_FIST, 0, WEAPON_FIST, 0);
    AddPlayerClass(SKIN_B_ID, SPAWN_X, SPAWN_Y, SPAWN_Z, SPAWN_A,
        WEAPON_FIST, 0, WEAPON_FIST, 0, WEAPON_FIST, 0);
    printf("[test][class] AddPlayerClass slots=0:%d, 1:%d, 2:%d spawn=(%.4f, %.4f, %.4f) angle=%.2f",
        DEFAULT_SKIN, SKIN_A_ID, SKIN_B_ID, SPAWN_X, SPAWN_Y, SPAWN_Z, SPAWN_A);
    print("[test][boot] OnGameModeInit: complete");
    return 1;
}

public OnPlayerConnect(playerid)
{
    new playerName[MAX_PLAYER_NAME + 1];
    GetPlayerName(playerid, playerName, sizeof(playerName));
    printf("[test][connect] OnPlayerConnect player=%d name=%s", playerid, playerName);
    SendClientMessage(playerid, 0xFFFFFFFF,
        "customskin-037 R3-1 test: real people001/people002 skins 20001/20002");
    print("[test][connect] leaving player in normal class-selection flow; no SpawnPlayer() here");
    return 1;
}

public OnPlayerRequestClass(playerid, classid)
{
    printf("[test][class] OnPlayerRequestClass player=%d class=%d current_skin=%d", playerid, classid, GetPlayerSkin(playerid));
    SetPlayerPos(playerid, SPAWN_X, SPAWN_Y, SPAWN_Z);
    SetPlayerFacingAngle(playerid, SPAWN_A);
    // camera in front of the player (facing angle 180 = looking toward -Y) for a preview
    SetPlayerCameraPos(playerid, SPAWN_X, SPAWN_Y - 3.0, SPAWN_Z + 0.7);
    SetPlayerCameraLookAt(playerid, SPAWN_X, SPAWN_Y, SPAWN_Z);
    return 1;
}

public OnPlayerCommandText(playerid, cmdtext[])
{
    new playerName[MAX_PLAYER_NAME + 1];
    GetPlayerName(playerid, playerName, sizeof(playerName));
    printf("[test][command] received player=%d name=%s command=%s current_skin=%d",
        playerid, playerName, cmdtext, GetPlayerSkin(playerid));
    // Test either real custom skin on demand. The bases differ (1 vs 2), so the per-base swap
    // renders each registered model.
    if (strcmp(cmdtext, "/skin1", true) == 0 || strcmp(cmdtext, "/a", true) == 0)
    {
        printf("[test][skin][before] cmd=%s player=%d skin=%d target_custom=%d target_base=%d",
            cmdtext, playerid, GetPlayerSkin(playerid), SKIN_A_ID, SKIN_A_BASE);
        SetPlayerSkin(playerid, SKIN_A_ID);
        printf("[test][skin][after] cmd=%s player=%d reported_skin=%d requested_custom=%d base=%d",
            cmdtext, playerid, GetPlayerSkin(playerid), SKIN_A_ID, SKIN_A_BASE);
        SendClientMessage(playerid, 0xFFFFFFFF, "switched to custom skin 20001 (base 1)");
        return 1;
    }
    if (strcmp(cmdtext, "/skin2", true) == 0 || strcmp(cmdtext, "/b", true) == 0)
    {
        printf("[test][skin][before] cmd=%s player=%d skin=%d target_custom=%d target_base=%d",
            cmdtext, playerid, GetPlayerSkin(playerid), SKIN_B_ID, SKIN_B_BASE);
        SetPlayerSkin(playerid, SKIN_B_ID);
        printf("[test][skin][after] cmd=%s player=%d reported_skin=%d requested_custom=%d base=%d",
            cmdtext, playerid, GetPlayerSkin(playerid), SKIN_B_ID, SKIN_B_BASE);
        SendClientMessage(playerid, 0xFFFFFFFF, "switched to custom skin 20002 (base 2)");
        return 1;
    }
    if (strcmp(cmdtext, "/normal", true) == 0)
    {
        printf("[test][skin][before] cmd=/normal player=%d skin=%d target_default=%d",
            playerid, GetPlayerSkin(playerid), DEFAULT_SKIN);
        SetPlayerSkin(playerid, DEFAULT_SKIN);
        printf("[test][skin][after] cmd=/normal player=%d reported_skin=%d target_default=%d",
            playerid, GetPlayerSkin(playerid), DEFAULT_SKIN);
        SendClientMessage(playerid, 0xFFFFFFFF, "back to the normal skin");
        return 1;
    }
    printf("[test][command][unhandled] player=%d command=%s", playerid, cmdtext);
    return 0;
}

// open.mp artwork callbacks - handy server-side visibility during testing
public OnPlayerRequestDownload(playerid, DOWNLOAD_REQUEST:type, crc)
{
    new playerName[MAX_PLAYER_NAME + 1];
    GetPlayerName(playerid, playerName, sizeof(playerName));
    printf("[test][download][request] player=%d name=%s type=%d crc=0x%08x",
        playerid, playerName, _:type, crc);
    return 1; // allow the default webserver download
}

public OnPlayerFinishedDownloading(playerid, virtualworld)
{
    new playerName[MAX_PLAYER_NAME + 1];
    GetPlayerName(playerid, playerName, sizeof(playerName));
    printf("[test][download][finished] player=%d name=%s virtualworld=%d current_skin=%d",
        playerid, playerName, virtualworld, GetPlayerSkin(playerid));
    return 1;
}

public OnPlayerSpawn(playerid)
{
    new playerName[MAX_PLAYER_NAME + 1];
    GetPlayerName(playerid, playerName, sizeof(playerName));
    printf("[test][spawn][begin] player=%d name=%s selected_skin=%d", playerid, playerName, GetPlayerSkin(playerid));

    // The selected AddPlayerClass skin is authoritative here. Do NOT overwrite it with
    // SKIN_A_ID, otherwise the << / >> class selection would be immediately discarded.
    SetPlayerPos(playerid, SPAWN_X, SPAWN_Y, SPAWN_Z);
    SetPlayerFacingAngle(playerid, SPAWN_A);

    // OnPlayerRequestClass uses a preview camera. Explicitly restore normal gameplay camera
    // and control after spawning so the player can look around and walk immediately.
    SetCameraBehindPlayer(playerid);
    TogglePlayerControllable(playerid, true);

    SendClientMessage(playerid, 0xFFFFFFFF,
        "spawned - use << / >> before spawn to choose normal / custom 20001 / custom 20002");
    printf("[test][spawn][complete] player=%d selected_skin=%d pos=(%.4f, %.4f, %.4f) angle=%.2f camera=behind_player controllable=1",
        playerid, GetPlayerSkin(playerid), SPAWN_X, SPAWN_Y, SPAWN_Z, SPAWN_A);
    return 1;
}
