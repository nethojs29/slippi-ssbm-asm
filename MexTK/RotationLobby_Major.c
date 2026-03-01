// Rotation Lobby Major Scene — scene lifecycle (mjFunction)
// Compiled to RotationLobby.dat via mjFunction symbol.
//
// Exports: minor_scene (MinorScene array), major_load, major_exit
//
// This major scene owns one minor scene (the lobby UI).
// Scene flow:
//   VS ends → Scene_SetNextMajor(13) → major_load()
//   → minor 0 (lobby UI via mnFunction in same .dat)
//   → SceneDecide → Scene_SetNextMajor(8) + Scene_ExitMajor() → back to Slippi Online

#include "RotationLobby.h"

// ---------------------------------------------------------------------------
// Static shared data — lives for the lifetime of the major scene.
// Passed to minor scene via load_data/unload_data pointers.
// ---------------------------------------------------------------------------
static SharedMinorData sharedData;

// ---------------------------------------------------------------------------
// MSRB helper
// ---------------------------------------------------------------------------
static void load_msrb(u8 *buf)
{
    buf[0] = 0xB3;  // CMD_GET_MATCH_STATE
    ExiSlippi_Transfer(buf, 1, EXI_WRITE);
    ExiSlippi_Transfer(buf, MSRB_TOTAL_SIZE, EXI_READ);
}

// ---------------------------------------------------------------------------
// ScenePrep — minor_prep callback for the lobby minor scene.
// Invalidates character preload cache, then scans MinorSceneDesc table
// for CommonMinorID 0x20 and overwrites its file_name to "RotationLobby.dat"
// so the m-ex "Load Minor Scene File" hook loads our .dat.
// ---------------------------------------------------------------------------
void ScenePrep(MinorScene *minor)
{
    // Invalidate character preload cache (same as GamePrep)
    void (*invalidate_preload_cache)(void) = (void (*)(void))0x800174bc;
    invalidate_preload_cache();

    // Scan MinorSceneDesc table for id == 0x20 (stride 0x14)
    MinorSceneDesc *desc = Scene_GetMinorSceneDesc();
    while (desc->id != 0x20 && desc->id != -1)
        desc = (MinorSceneDesc *)((char *)desc + 0x14);
    if (desc->id == 0x20)
        desc->file_name = "RotationLobby.dat";
}

// ---------------------------------------------------------------------------
// SceneDecide — called when the minor scene's minor_think calls Scene_ExitMinor()
// Transitions back to Slippi Online major scene (8).
// ---------------------------------------------------------------------------
void SceneDecide(MinorScene *minor)
{
    // Check connection state from shared data's MSRB
    u8 conn_state = sharedData.msrb[OFST_CONNECTION_STATE];

    if (conn_state != MM_STATE_CONNECTION_SUCCESS) {
        // Disconnected — go back to Slippi Online CSS
        Scene_SetNextMajor(8);
        Scene_ExitMajor();
        return;
    }

    // Connected and ready — transition back to major 8 (Slippi Online).
    // CSS will auto-ready and advance through splash to VS.
    Scene_SetNextMajor(8);
    Scene_ExitMajor();
}

// ---------------------------------------------------------------------------
// major_load — called when entering this major scene
// Fetches MSRB and populates SharedMinorData for the minor scene.
// ---------------------------------------------------------------------------
void major_load(void)
{
    // Zero the shared data
    memset(&sharedData, 0, sizeof(SharedMinorData));

    // Initialize waiting ports to 0xFF (unused)
    int i;
    for (i = 0; i < ROT_MAX_WAITING; i++)
        sharedData.waiting_ports[i] = 0xFF;

    // Load MSRB
    load_msrb(sharedData.msrb);

    // Parse rotation state from MSRB into shared data
    sharedData.local_port = sharedData.msrb[OFST_LOCAL_PLAYER_INDEX];
    sharedData.player_count = sharedData.msrb[OFST_ROT_PLAYER_COUNT];
    sharedData.active_ports[0] = sharedData.msrb[OFST_ROT_ACTIVE_P1];
    sharedData.active_ports[1] = sharedData.msrb[OFST_ROT_ACTIVE_P2];
    sharedData.games_played = sharedData.msrb[OFST_ROT_GAMES_PLAYED];
    sharedData.last_winner = sharedData.msrb[OFST_ROT_LAST_WINNER];

    for (i = 0; i < ROT_MAX_WAITING; i++)
        sharedData.waiting_ports[i] = sharedData.msrb[OFST_ROT_QUEUE_START + i];

    sharedData.is_active_player =
        (sharedData.local_port == sharedData.active_ports[0] ||
         sharedData.local_port == sharedData.active_ports[1]);

    // Default character (Fox)
    sharedData.selected_char = 0x02;
    sharedData.selected_color = 0;
}

// ---------------------------------------------------------------------------
// major_exit — called when leaving this major scene
// ---------------------------------------------------------------------------
void major_exit(void)
{
    // Nothing to clean up — SharedMinorData is static
}

// ---------------------------------------------------------------------------
// Minor scene table — single entry for the lobby UI
//
// minor_kind = 0x20 (MNRKIND_CLSCSPLSH = Classic Mode Splash) so that
// m-ex's "Load Minor Scene File" hook fires and loads the mnFunction
// from the .dat file specified in MinorSceneDesc's file_name.
// ---------------------------------------------------------------------------
MinorScene minor_scene[] = {
    {
        .minor_id = 0,
        .heap_kind = 3,                    // SCENEHEAPKIND_UNK3 = persistent heaps
        .minor_prep = (void *)ScenePrep,
        .minor_decide = (void *)SceneDecide,
        .minor_kind = 0x20,                // Classic Mode Splash (triggers m-ex .dat load)
        .load_data = (void *)&sharedData,
        .unload_data = (void *)&sharedData,
    },
    {
        .minor_id = -1,  // Terminator
    },
};
