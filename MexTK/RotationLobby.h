// Rotation Lobby Scene — shared header
// Used by both RotationLobby_Major.c (scene lifecycle) and RotationLobby_Minor.c (lobby UI)

#ifndef ROTATION_LOBBY_H
#define ROTATION_LOBBY_H

#include "../../m-ex/MexTK/include/mex.h"

// ---------------------------------------------------------------------------
// EXI transfer
// ---------------------------------------------------------------------------
typedef void (*EXITransferFn)(void *buf, int len, int mode);
#define ExiSlippi_Transfer ((EXITransferFn)0x800055F0)
#define EXI_WRITE 1
#define EXI_READ  0

// ---------------------------------------------------------------------------
// MSRB field offsets (must match Online.s)
// ---------------------------------------------------------------------------
#define MSRB_TOTAL_SIZE          985

#define OFST_CONNECTION_STATE    0
#define OFST_LOCAL_PLAYER_INDEX  3
#define OFST_P1_NAME             54
#define MSRB_NAME_SIZE           31
#define OFST_ROT_PLAYER_COUNT    972
#define OFST_ROT_ACTIVE_P1       973
#define OFST_ROT_ACTIVE_P2       974
#define OFST_ROT_QUEUE_START     975
#define OFST_ROT_GAMES_PLAYED    983
#define OFST_ROT_LAST_WINNER     984

#define MM_STATE_CONNECTION_SUCCESS 4

// ---------------------------------------------------------------------------
// Rotation constants
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

    // Character selection results (written by minor, read by major decide)
    u8 selected_char;
    u8 selected_color;
    int local_ready;
    int opponent_ready;

    // Full MSRB buffer (for name lookups etc)
    u8 msrb[MSRB_TOTAL_SIZE];
} SharedMinorData;

// Minor scene prep/decide (defined in major, referenced in minor_scene table)
void ScenePrep(MinorScene *minor);
void SceneDecide(MinorScene *minor);

#endif // ROTATION_LOBBY_H
