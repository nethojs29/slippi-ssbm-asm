// Rotation Lobby Major Scene — scene lifecycle (mjFunction)
// Compiled to RotationLobby.dat via mjFunction symbol.
//
// Exports: minor_scene (MinorScene array), major_load, major_exit
//
// This major scene owns one minor scene (the lobby UI).
// Scene flow:
//   CSS → Scene_SetNextMajor(ROTATION_LOBBY_MAJOR_ID) → major_load()
//   → minor 0 (lobby UI via mnFunction in same .dat)
//   → SceneDecide → Scene_SetNextMajor(8) + Scene_ExitMajor() → back to Slippi Online

#include "major.h"

// ---------------------------------------------------------------------------
// Static shared data — lives for the lifetime of the major scene.
// Passed to minor scene via load_data/unload_data pointers.
// ---------------------------------------------------------------------------
static SharedMinorData sharedData;

// ---------------------------------------------------------------------------
// MSRB helpers
// ---------------------------------------------------------------------------
static void load_msrb(u8 *buf)
{
    buf[0] = CMD_GET_MATCH_STATE;
    FN_EXITransferBuffer(buf, 1, CONST_ExiWrite);
    FN_EXITransferBuffer(buf, MSRB_TOTAL_SIZE, CONST_ExiRead);
}

// ---------------------------------------------------------------------------
// ScenePrep — called before the minor scene's .dat is loaded
// Override the MinorSceneDesc filename so m-ex loads our mnFunction from
// the same RotationLobby.dat file.
// ---------------------------------------------------------------------------
void ScenePrep(MinorScene *minor)
{
    // Invalidate character preload cache (same as GamePrep)
    // This prevents crashes when characters change between games
    void (*invalidate_preload_cache)(void) = (void (*)(void))0x800174bc;
    invalidate_preload_cache();

    // Find the MinorSceneDesc for our minor_kind (CommonMinorID 0x20)
    // and override its filename pointer to "RotationLobby.dat".
    // The m-ex loader at 0x801a40c8 reads filename from MinorSceneDesc+0x10.
    MinorSceneDesc *desc = Scene_GetMinorSceneDesc();
    if (desc) {
        desc->file_name = "RotationLobby.dat";
    }
}

// ---------------------------------------------------------------------------
// SceneDecide — called when the minor scene's minor_think calls Scene_ExitMinor()
// Decides where to go next: back to Slippi Online major scene (8).
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

    // Connected and ready — set the return-to-VS flag in r13 so that
    // Slippi Online's major load skips CSS and goes to Splash → VS.
    // The flag is at OFST_R13_ROTATION_RETURN_TO_VS, written from ASM.
    // We write it via inline assembly since we need r13 access.
    //
    // Actually, we can't easily write to r13 offsets from MexTK C.
    // Instead, we use a simpler approach: write a flag byte to a known
    // static address. The ASM side will check this.
    //
    // For now, transition back to major 8. The ASM-side CSSScenePrep
    // or MajorSceneLoad will check the return-to-VS r13 flag (set by
    // VSSceneDecide or CSSSceneDecide before entering this major scene).
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
    for (int i = 0; i < ROT_MAX_WAITING; i++)
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

    for (int i = 0; i < ROT_MAX_WAITING; i++)
        sharedData.waiting_ports[i] = sharedData.msrb[OFST_ROT_QUEUE_START + i];

    sharedData.is_active_player =
        (sharedData.local_port == sharedData.active_ports[0] ||
         sharedData.local_port == sharedData.active_ports[1]);

    sharedData.is_spectator = sharedData.msrb[OFST_IS_SPECTATOR];

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
// Layout must match the MinorScene struct (stride 0x18):
//   0x0:  minor_id (s8)
//   0x1:  heap_kind (u8)
//   0x2:  [padding to 0x4]
//   0x4:  minor_prep (ptr)
//   0x8:  minor_decide (ptr)
//   0xC:  minor_kind (u8)
//   0xD:  [padding to 0x10]
//   0x10: load_data (ptr)
//   0x14: unload_data (ptr)
//
// minor_kind = 0x20 (MNRKIND_CLSCSPLSH = Classic Mode Splash) so that
// m-ex's "Load Minor Scene File" hook fires and loads the mnFunction
// from the .dat file specified in MinorSceneDesc's file_name.
// ---------------------------------------------------------------------------
MinorScene minor_scene[] = {
    {
        .minor_id = 0,                     // ROT_MINOR_LOBBY
        .heap_kind = SCENEHEAPKIND_UNK3,   // 3 = persistent heaps
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
