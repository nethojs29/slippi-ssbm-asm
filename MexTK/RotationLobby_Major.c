// Rotation Lobby Major Scene — MINIMAL TEST
// Empty major_load, ScenePrep just sets filename, SceneDecide goes back to major 8.

#include "RotationLobby.h"

static SharedMinorData sharedData;

// ---------------------------------------------------------------------------
// ScenePrep — just set the .dat filename, nothing else
// ---------------------------------------------------------------------------
void ScenePrep(MinorScene *minor)
{
    // Empty for testing — is this where the freeze happens?
}

// ---------------------------------------------------------------------------
// SceneDecide — always go back to major 8
// ---------------------------------------------------------------------------
void SceneDecide(MinorScene *minor)
{
    Scene_SetNextMajor(8);
    Scene_ExitMajor();
}

// ---------------------------------------------------------------------------
// major_load — empty, just test the flow
// ---------------------------------------------------------------------------
void major_load(void)
{
}

// ---------------------------------------------------------------------------
// major_exit
// ---------------------------------------------------------------------------
void major_exit(void)
{
}

// ---------------------------------------------------------------------------
// Minor scene table
// ---------------------------------------------------------------------------
MinorScene minor_scene[] = {
    {
        .minor_id = 0,
        .heap_kind = 3,
        .minor_prep = (void *)ScenePrep,
        .minor_decide = (void *)SceneDecide,
        .minor_kind = 0x20,
        .load_data = (void *)&sharedData,
        .unload_data = (void *)&sharedData,
    },
    {
        .minor_id = 255,
    },
};
