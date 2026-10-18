#include <open.mp>

#define MODEL_SKIN_1 20001
#define MODEL_SKIN_2 20002
#define MODEL_OBJECT -2000

#define SPAWN_X 1685.9645
#define SPAWN_Y -2329.8933
#define SPAWN_Z 13.5469
#define SPAWN_A 180.0

#define OBJECT_X (SPAWN_X + 8.0)
#define OBJECT_Y (SPAWN_Y - 3.0)
#define OBJECT_Z SPAWN_Z
#define OBJECT_STEP 1.0

#define DIALOG_MAIN 1000
#define DIALOG_OBJECT 1001
#define DIALOG_INFO 1002

new gObject = INVALID_OBJECT_ID;

stock CreateTestObject()
{
    if (gObject != INVALID_OBJECT_ID)
    {
        DestroyObject(gObject);
        gObject = INVALID_OBJECT_ID;
    }

    gObject = CreateObject(MODEL_OBJECT, OBJECT_X, OBJECT_Y, OBJECT_Z, 0.0, 0.0, 0.0);
    return gObject;
}

stock ObjectExists()
{
    return gObject != INVALID_OBJECT_ID;
}

stock ShowMainDialog(playerid)
{
    ShowPlayerDialog(playerid, DIALOG_MAIN, DIALOG_STYLE_LIST,
        "CustomSkin R3-1 Test",
        "Skin 1 - Model 20001\nSkin 2 - Model 20002\nNormal CJ - Model 0\nObject Control - Model -2000",
        "Select", "Close");
    return 1;
}

stock ShowObjectDialog(playerid)
{
    ShowPlayerDialog(playerid, DIALOG_OBJECT, DIALOG_STYLE_LIST,
        "Object Control",
        "Create object\nMove object to player\nMove up\nMove down\nMove forward\nMove back\nMove left\nMove right\nShow position\nReset position\nRemove object",
        "Select", "Back");
    return 1;
}

stock ShowObjectPosition(playerid)
{
    new Float:x, Float:y, Float:z;
    new message[144];

    if (!ObjectExists())
    {
        ShowPlayerDialog(playerid, DIALOG_INFO, DIALOG_STYLE_MSGBOX,
            "Object Info",
            "Object does not exist.\nCreate it first from Object Control.",
            "OK", "");
        return 1;
    }

    GetObjectPos(gObject, x, y, z);
    format(message, sizeof(message), "Object ID: %d\nModel: %d\nBase model: 19379\nX: %.3f\nY: %.3f\nZ: %.3f", gObject, MODEL_OBJECT, x, y, z);
    ShowPlayerDialog(playerid, DIALOG_INFO, DIALOG_STYLE_MSGBOX,
        "Object Position",
        message,
        "OK", "");
    return 1;
}

stock ObjectNotFound(playerid)
{
    ShowPlayerDialog(playerid, DIALOG_INFO, DIALOG_STYLE_MSGBOX,
        "Object Control",
        "Object does not exist.\nUse Create object first.",
        "OK", "");
    return 1;
}

stock MoveObjectVertical(playerid, up)
{
    new Float:x, Float:y, Float:z;

    if (!ObjectExists())
        return ObjectNotFound(playerid);

    GetObjectPos(gObject, x, y, z);
    if (up)
        z += OBJECT_STEP;
    else
        z -= OBJECT_STEP;

    SetObjectPos(gObject, x, y, z);
    return ShowObjectPosition(playerid);
}

stock MoveObjectForward(playerid, isForward)
{
    new Float:x, Float:y, Float:z;
    new Float:angle;

    if (!ObjectExists())
        return ObjectNotFound(playerid);

    GetObjectPos(gObject, x, y, z);
    GetPlayerFacingAngle(playerid, angle);

    if (isForward)
    {
        x += floatsin(-angle, degrees) * OBJECT_STEP;
        y += floatcos(-angle, degrees) * OBJECT_STEP;
    }
    else
    {
        x -= floatsin(-angle, degrees) * OBJECT_STEP;
        y -= floatcos(-angle, degrees) * OBJECT_STEP;
    }

    SetObjectPos(gObject, x, y, z);
    return ShowObjectPosition(playerid);
}

stock MoveObjectSide(playerid, left)
{
    new Float:x, Float:y, Float:z;

    if (!ObjectExists())
        return ObjectNotFound(playerid);

    GetObjectPos(gObject, x, y, z);
    if (left)
        x -= OBJECT_STEP;
    else
        x += OBJECT_STEP;

    SetObjectPos(gObject, x, y, z);
    return ShowObjectPosition(playerid);
}

public OnGameModeInit()
{
    SetGameModeText("CustomSkin R3-1");

    print("========================================");
    print(" CustomSkin R3-1 Test");
    print(" Models loaded from models/artconfig.txt");
    print("========================================");

    AddPlayerClass(0, SPAWN_X, SPAWN_Y, SPAWN_Z, SPAWN_A, WEAPON_FIST, 0, WEAPON_FIST, 0, WEAPON_FIST, 0);
    AddPlayerClass(MODEL_SKIN_1, SPAWN_X, SPAWN_Y, SPAWN_Z, SPAWN_A, WEAPON_FIST, 0, WEAPON_FIST, 0, WEAPON_FIST, 0);
    AddPlayerClass(MODEL_SKIN_2, SPAWN_X, SPAWN_Y, SPAWN_Z, SPAWN_A, WEAPON_FIST, 0, WEAPON_FIST, 0, WEAPON_FIST, 0);

    CreateTestObject();

    printf("[Object] model %d base 19379 id %d at %.3f %.3f %.3f", MODEL_OBJECT, gObject, OBJECT_X, OBJECT_Y, OBJECT_Z);
    return 1;
}

public OnPlayerConnect(playerid)
{
    SendClientMessage(playerid, 0xFFFFFFFF, "CustomSkin R3-1 test loaded.");
    SendClientMessage(playerid, 0xFFFFFFFF, "Use /test to open the interactive test menu.");
    return 1;
}

public OnPlayerSpawn(playerid)
{
    SetPlayerPos(playerid, SPAWN_X, SPAWN_Y, SPAWN_Z);
    SetPlayerFacingAngle(playerid, SPAWN_A);
    ShowMainDialog(playerid);
    return 1;
}

public OnPlayerCommandText(playerid, cmdtext[])
{
    if (!strcmp(cmdtext, "/test", true))
    {
        ShowMainDialog(playerid);
        return 1;
    }

    return 0;
}

public OnDialogResponse(playerid, dialogid, response, listitem, inputtext[])
{
    if (dialogid == DIALOG_MAIN)
    {
        if (!response)
            return 1;

        switch (listitem)
        {
            case 0:
            {
                SetPlayerSkin(playerid, MODEL_SKIN_1);
                ShowPlayerDialog(playerid, DIALOG_INFO, DIALOG_STYLE_MSGBOX,
                    "Skin Test", "Applied custom skin model 20001.", "OK", "");
            }
            case 1:
            {
                SetPlayerSkin(playerid, MODEL_SKIN_2);
                ShowPlayerDialog(playerid, DIALOG_INFO, DIALOG_STYLE_MSGBOX,
                    "Skin Test", "Applied custom skin model 20002.", "OK", "");
            }
            case 2:
            {
                SetPlayerSkin(playerid, 0);
                ShowPlayerDialog(playerid, DIALOG_INFO, DIALOG_STYLE_MSGBOX,
                    "Skin Test", "Restored normal CJ model 0.", "OK", "");
            }
            case 3:
            {
                ShowObjectDialog(playerid);
            }
        }
        return 1;
    }

    if (dialogid == DIALOG_OBJECT)
    {
        if (!response)
        {
            ShowMainDialog(playerid);
            return 1;
        }

        switch (listitem)
        {
            case 0:
            {
                CreateTestObject();
                ShowObjectPosition(playerid);
            }
            case 1:
            {
                new Float:x, Float:y, Float:z;

                if (!ObjectExists())
                    return ObjectNotFound(playerid);

                GetPlayerPos(playerid, x, y, z);
                SetObjectPos(gObject, x, y, z);
                ShowObjectPosition(playerid);
            }
            case 2:
            {
                MoveObjectVertical(playerid, true);
            }
            case 3:
            {
                MoveObjectVertical(playerid, false);
            }
            case 4:
            {
                MoveObjectForward(playerid, true);
            }
            case 5:
            {
                MoveObjectForward(playerid, false);
            }
            case 6:
            {
                MoveObjectSide(playerid, true);
            }
            case 7:
            {
                MoveObjectSide(playerid, false);
            }
            case 8:
            {
                ShowObjectPosition(playerid);
            }
            case 9:
            {
                if (!ObjectExists())
                    return ObjectNotFound(playerid);

                SetObjectPos(gObject, OBJECT_X, OBJECT_Y, OBJECT_Z);
                SetObjectRot(gObject, 0.0, 0.0, 0.0);
                ShowObjectPosition(playerid);
            }
            case 10:
            {
                if (!ObjectExists())
                    return ObjectNotFound(playerid);

                DestroyObject(gObject);
                gObject = INVALID_OBJECT_ID;
                ShowPlayerDialog(playerid, DIALOG_INFO, DIALOG_STYLE_MSGBOX,
                    "Object Control", "Object removed.", "OK", "");
            }
        }
        return 1;
    }

    if (dialogid == DIALOG_INFO)
    {
        if (!response)
            return 1;

        ShowMainDialog(playerid);
        return 1;
    }

    return 0;
}
