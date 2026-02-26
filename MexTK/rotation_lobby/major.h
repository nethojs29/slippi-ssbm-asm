// Rotation Lobby Major Scene — shared header
// Used by both major.c (scene lifecycle) and minor.c (lobby UI)

#ifndef ROTATION_LOBBY_MAJOR_H
#define ROTATION_LOBBY_MAJOR_H

#include "../../../m-ex/MexTK/include/mex.h"

// ---------------------------------------------------------------------------
// Slippi EXI function pointers (not in melee.link, use hardcoded addresses)
// ---------------------------------------------------------------------------
typedef void (*EXITransferFn)(void *buf, int len, int dir);
#define FN_EXITransferBuffer ((EXITransferFn)0x800055f0)
#define CONST_ExiRead  0
#define CONST_ExiWrite 1

// Slippi EXI commands
#define CMD_GET_MATCH_STATE  0xB3
#define CMD_GP_COMPLETE_STEP 0xC0
#define CMD_GP_FETCH_STEP    0xC1

// ---------------------------------------------------------------------------
// MSRB field offsets (must match Online.s)
// ---------------------------------------------------------------------------
#define MSRB_TOTAL_SIZE          985

#define OFST_CONNECTION_STATE    0
#define OFST_LOCAL_PLAYER_INDEX  3
#define OFST_LOCAL_NAME          23
#define OFST_P1_NAME             54
#define MSRB_NAME_SIZE           31
#define OFST_P1_CONNECT_CODE     209
#define MSRB_CONNECT_CODE_SIZE   10
#define OFST_IS_SPECTATOR        971
#define OFST_ROT_PLAYER_COUNT    972
#define OFST_ROT_ACTIVE_P1       973
#define OFST_ROT_ACTIVE_P2       974
#define OFST_ROT_QUEUE_START     975
#define OFST_ROT_GAMES_PLAYED    983
#define OFST_ROT_LAST_WINNER     984

#define MM_STATE_CONNECTION_SUCCESS 4

// ---------------------------------------------------------------------------
// Rotation lobby minor scene IDs (within this major)
// ---------------------------------------------------------------------------
enum ROT_MINOR_KIND {
    ROT_MINOR_LOBBY = 0,
};

// ---------------------------------------------------------------------------
// Max supported players
// ---------------------------------------------------------------------------
#define ROT_MAX_PLAYERS  10
#define ROT_MAX_WAITING  8

// ---------------------------------------------------------------------------
// SharedMinorData — passed between major and minor via load_data/unload_data
// Populated by major_load from MSRB, consumed by minor_load/think/exit
// ---------------------------------------------------------------------------
typedef struct SharedMinorData {
    // Rotation state (populated by major_load from MSRB)
    u8 local_port;
    u8 player_count;
    u8 active_ports[2];
    u8 waiting_ports[ROT_MAX_WAITING];
    u8 games_played;
    u8 last_winner;
    u8 is_active_player;
    u8 is_spectator;

    // Character selection results (written by minor, read by major decide)
    u8 selected_char;
    u8 selected_color;
    int local_ready;
    int opponent_ready;

    // Full MSRB buffer (for name lookups etc)
    u8 msrb[MSRB_TOTAL_SIZE];
} SharedMinorData;

// Minor scene prep/decide (defined in major.c, referenced in minor_scene table)
void ScenePrep(MinorScene *minor);
void SceneDecide(MinorScene *minor);

#endif // ROTATION_LOBBY_MAJOR_H
