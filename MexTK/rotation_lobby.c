// Rotation Lobby Scene - MexTK source
// Compiled to RotationLobby.dat via:
//   MexTK.exe -ff -i "rotation_lobby.c" -s mnFunction -dat "RotationLobby.dat" -ow
//
// Exports minor_think, minor_load, minor_exit via mnFunction symbol.
// Loaded by RotationLobbyScenePrep in main.asm.

#include "../../m-ex/MexTK/include/mex.h"

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
// Traced byte-by-byte from the .set chain in Online.s
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
// Layout constants (text canvas coordinate space)
// Canvas is roughly 30 units wide x 22 units tall
// ---------------------------------------------------------------------------

// Top bar
#define TOP_Y           0.8
#define TOP_LABEL_X     1.0
#define TOP_TIMER_X     12.5
#define TOP_COUNT_X     22.0

// Main panel (left 70%)
#define PANEL_LEFT      1.0
#define PANEL_TOP       3.0
#define PANEL_WIDTH     19.0

// Matchup area
#define MATCH_HEADER_Y  3.5
#define P1_NAME_X       2.0
#define P1_NAME_Y       5.5
#define P1_CHAR_Y       7.0
#define P1_READY_Y      8.2
#define VS_X            9.5
#define VS_Y            6.0
#define P2_NAME_X       13.5
#define P2_NAME_Y       5.5
#define P2_CHAR_Y       7.0
#define P2_READY_Y      8.2

// Sidebar (right 30%)
#define SIDE_LEFT        21.5
#define SIDE_TOP         3.0
#define SIDE_WIDTH       8.0
#define SIDE_HEADER_Y    3.5
#define SIDE_FIRST_Y     5.0
#define SIDE_LINE_H      1.3

// Bottom bar
#define BOT_Y            19.5
#define BOT_GAME_X       1.5
#define BOT_PROMPT_X     8.0

// Timer
#define TIMER_TOTAL_FRAMES  1800  // 30 seconds at 60fps
#define SPECTATOR_WAIT      180   // 3 seconds

// ---------------------------------------------------------------------------
// Character name table (external char ID -> display name)
// ---------------------------------------------------------------------------
static const char *char_names[] = {
    "Captain Falcon",  // 0x00
    "DK",              // 0x01
    "Fox",             // 0x02
    "Mr. Game & Watch",// 0x03
    "Kirby",           // 0x04
    "Bowser",          // 0x05
    "Link",            // 0x06
    "Luigi",           // 0x07
    "Mario",           // 0x08
    "Marth",           // 0x09
    "Mewtwo",          // 0x0A
    "Ness",            // 0x0B
    "Peach",           // 0x0C
    "Pikachu",         // 0x0D
    "Ice Climbers",    // 0x0E
    "Jigglypuff",      // 0x0F
    "Samus",           // 0x10
    "Yoshi",           // 0x11
    "Zelda",           // 0x12
    "Sheik",           // 0x13
    "Falco",           // 0x14
    "Young Link",      // 0x15
    "Dr. Mario",       // 0x16
    "Roy",             // 0x17
    "Pichu",           // 0x18
    "Ganondorf",       // 0x19
};
#define NUM_CHARACTERS 26

// ---------------------------------------------------------------------------
// Max supported players
// ---------------------------------------------------------------------------
#define ROT_MAX_PLAYERS  10
#define ROT_MAX_WAITING  8

// ---------------------------------------------------------------------------
// Lobby state
// ---------------------------------------------------------------------------
typedef struct {
    int frame_count;
    int local_ready;
    int opponent_ready;
    int is_active_player;
    u8 local_port;
    u8 active_ports[2];
    u8 waiting_ports[ROT_MAX_WAITING];
    u8 player_count;
    u8 games_played;
    u8 last_winner;
    u8 selected_char;
    u8 selected_color;
    u8 opp_char;
    u8 opp_color;
    int wait_count;

    // Text canvas
    int canvas_id;

    // Top bar
    Text *lobby_name_text;
    Text *timer_text;
    Text *player_count_text;

    // Match panel
    Text *match_header_text;
    Text *p1_text;
    Text *p2_text;
    Text *p1_char_text;
    Text *p2_char_text;
    Text *p1_ready_text;
    Text *p2_ready_text;
    Text *vs_text;

    // Sidebar
    Text *side_header_text;
    Text *queue_texts[ROT_MAX_WAITING];

    // Bottom bar
    Text *game_text;
    Text *prompt_text;

    // Background panels (Text objects with viewport_color for BG)
    Text *bg_top;
    Text *bg_panel;
    Text *bg_side;
    Text *bg_bottom;

    // MSRB buffer
    u8 *msrb;
} LobbyState;

static LobbyState *state = 0;

// ---------------------------------------------------------------------------
// MSRB helpers
// ---------------------------------------------------------------------------

static u8 *load_msrb(u8 *buf)
{
    buf[0] = CMD_GET_MATCH_STATE;
    FN_EXITransferBuffer(buf, 1, CONST_ExiWrite);
    FN_EXITransferBuffer(buf, MSRB_TOTAL_SIZE, CONST_ExiRead);
    return buf;
}

static char *get_player_name(u8 *msrb, u8 port)
{
    if (port > 3) return "???";
    return (char *)(msrb + OFST_P1_NAME + port * MSRB_NAME_SIZE);
}

static char *get_connect_code(u8 *msrb, u8 port)
{
    if (port > 3) return "";
    return (char *)(msrb + OFST_P1_CONNECT_CODE + port * MSRB_CONNECT_CODE_SIZE);
}

// ---------------------------------------------------------------------------
// EXI communication for character sync
// ---------------------------------------------------------------------------

static void send_char_selection(u8 char_id, u8 color_id)
{
    u8 buf[6];
    buf[0] = CMD_GP_COMPLETE_STEP;
    buf[1] = 0;          // step_idx = 0 (character pick)
    buf[2] = char_id;
    buf[3] = color_id;
    buf[4] = 0;          // stage_selections[0] (unused)
    buf[5] = 0;          // stage_selections[1] (unused)
    FN_EXITransferBuffer(buf, 6, CONST_ExiWrite);
}

static int fetch_opponent_selection(u8 *char_id, u8 *color_id)
{
    u8 cmd[2];
    cmd[0] = CMD_GP_FETCH_STEP;
    cmd[1] = 0;          // step_idx = 0
    FN_EXITransferBuffer(cmd, 2, CONST_ExiWrite);

    u8 resp[6];
    FN_EXITransferBuffer(resp, 6, CONST_ExiRead);

    if (resp[0]) {       // is_found
        *char_id = resp[2];
        *color_id = resp[3];
    }
    return resp[0];
}

// ---------------------------------------------------------------------------
// UI helpers: create a Text object used as a background panel
// viewport_color with non-zero alpha renders a colored rect behind the text
// ---------------------------------------------------------------------------
static Text *create_bg_panel(int canvas_id, float x, float y,
                              float w, float h, GXColor *color)
{
    Text *t = Text_CreateText2(0, canvas_id, x, y, 0.0, w, h);
    Text_AddSubtext(t, 0.0, 0.0, " "); // Need at least one subtext for rendering
    t->viewport_color = *color;
    t->use_aspect = 1;  // Fill to the aspect bounds
    // Make the text itself invisible
    GXColor clear = {0, 0, 0, 0};
    Text_SetColor(t, 0, &clear);
    return t;
}

// ---------------------------------------------------------------------------
// minor_load — called when the scene is entered
// ---------------------------------------------------------------------------
void minor_load(void *data)
{
    state = HSD_MemAlloc(sizeof(LobbyState));
    memset(state, 0, sizeof(LobbyState));

    for (int i = 0; i < ROT_MAX_WAITING; i++)
        state->waiting_ports[i] = 0xFF;

    // Load MSRB
    state->msrb = HSD_MemAlloc(MSRB_TOTAL_SIZE);
    load_msrb(state->msrb);

    // Parse rotation state
    state->local_port = state->msrb[OFST_LOCAL_PLAYER_INDEX];
    state->player_count = state->msrb[OFST_ROT_PLAYER_COUNT];
    state->active_ports[0] = state->msrb[OFST_ROT_ACTIVE_P1];
    state->active_ports[1] = state->msrb[OFST_ROT_ACTIVE_P2];
    state->games_played = state->msrb[OFST_ROT_GAMES_PLAYED];
    state->last_winner = state->msrb[OFST_ROT_LAST_WINNER];

    state->wait_count = 0;
    for (int i = 0; i < ROT_MAX_WAITING; i++)
    {
        u8 port = state->msrb[OFST_ROT_QUEUE_START + i];
        state->waiting_ports[i] = port;
        if (port != 0xFF)
            state->wait_count++;
    }

    state->is_active_player =
        (state->local_port == state->active_ports[0] ||
         state->local_port == state->active_ports[1]);

    // Default character
    state->selected_char = 0x02; // Fox
    state->selected_color = 0;

    // --- Colors ---
    GXColor white   = {0xFF, 0xFF, 0xFF, 0xFF};
    GXColor green   = {0x21, 0xBA, 0x45, 0xFF};
    GXColor gray    = {0x99, 0x99, 0x99, 0xFF};
    GXColor yellow  = {0xFF, 0xD7, 0x00, 0xFF};
    GXColor red     = {0xDB, 0x28, 0x28, 0xFF};
    GXColor cyan    = {0x00, 0xCC, 0xCC, 0xFF};

    // Panel background colors (semi-transparent)
    GXColor bg_dark   = {0x10, 0x10, 0x18, 0xD0};
    GXColor bg_panel  = {0x18, 0x18, 0x28, 0xC0};
    GXColor bg_side   = {0x14, 0x14, 0x22, 0xC0};
    GXColor bg_bottom = {0x10, 0x10, 0x18, 0xA0};

    // --- Create canvas ---
    state->canvas_id = Text_CreateCanvas(0, 0, 0, 13, 80, 8, 0, 0);

    // --- Background panels ---
    // Top bar background
    state->bg_top = create_bg_panel(state->canvas_id,
        0.0, 0.0, 30.0, 2.2, &bg_dark);

    // Main matchup panel background
    state->bg_panel = create_bg_panel(state->canvas_id,
        PANEL_LEFT - 0.5, PANEL_TOP - 0.5, PANEL_WIDTH + 1.0, 7.5, &bg_panel);

    // Sidebar background
    if (state->wait_count > 0)
    {
        float side_h = 2.5 + state->wait_count * SIDE_LINE_H;
        state->bg_side = create_bg_panel(state->canvas_id,
            SIDE_LEFT - 0.5, SIDE_TOP - 0.5, SIDE_WIDTH + 1.0, side_h, &bg_side);
    }

    // Bottom bar background
    state->bg_bottom = create_bg_panel(state->canvas_id,
        0.0, BOT_Y - 0.5, 30.0, 2.5, &bg_bottom);

    // =====================================================================
    // TOP BAR: Lobby name | Timer | Player count
    // =====================================================================

    // Lobby name = local player's connect code
    state->lobby_name_text = Text_CreateText2(0, state->canvas_id,
        TOP_LABEL_X, TOP_Y, 0.0, 10.0, 1.5);
    char *code = get_connect_code(state->msrb, state->local_port);
    Text_AddSubtext(state->lobby_name_text, 0.0, 0.0, "%s", code);
    Text_SetColor(state->lobby_name_text, 0, &cyan);
    Text_SetScale(state->lobby_name_text, 0, 0.9, 0.9);

    // Timer
    state->timer_text = Text_CreateText2(0, state->canvas_id,
        TOP_TIMER_X, TOP_Y, 0.0, 6.0, 1.5);
    Text_AddSubtext(state->timer_text, 0.0, 0.0, "0:30");
    Text_SetColor(state->timer_text, 0, &white);
    Text_SetScale(state->timer_text, 0, 1.1, 1.1);

    // Player count
    state->player_count_text = Text_CreateText2(0, state->canvas_id,
        TOP_COUNT_X, TOP_Y, 0.0, 8.0, 1.5);
    Text_AddSubtext(state->player_count_text, 0.0, 0.0,
        "%d Players", state->player_count);
    Text_SetColor(state->player_count_text, 0, &gray);
    Text_SetScale(state->player_count_text, 0, 0.8, 0.8);

    // =====================================================================
    // MATCH PANEL: P1 name + char   VS   P2 name + char
    // =====================================================================

    // "PLAYERS IN MATCH" header
    state->match_header_text = Text_CreateText2(0, state->canvas_id,
        PANEL_LEFT + 3.0, MATCH_HEADER_Y, 0.0, 16.0, 1.5);
    Text_AddSubtext(state->match_header_text, 0.0, 0.0, "PLAYERS IN MATCH");
    Text_SetColor(state->match_header_text, 0, &green);
    Text_SetScale(state->match_header_text, 0, 0.9, 0.9);

    // Player 1 name
    char *p1_name = get_player_name(state->msrb, state->active_ports[0]);
    state->p1_text = Text_CreateText2(0, state->canvas_id,
        P1_NAME_X, P1_NAME_Y, 0.0, 7.0, 1.5);
    Text_AddSubtext(state->p1_text, 0.0, 0.0, "%s", p1_name);
    Text_SetColor(state->p1_text, 0, &white);
    Text_SetScale(state->p1_text, 0, 1.0, 1.0);

    // Player 1 character
    state->p1_char_text = Text_CreateText2(0, state->canvas_id,
        P1_NAME_X, P1_CHAR_Y, 0.0, 7.0, 1.5);
    Text_AddSubtext(state->p1_char_text, 0.0, 0.0,
        "%s", (char *)char_names[state->selected_char % NUM_CHARACTERS]);
    Text_SetColor(state->p1_char_text, 0, &green);
    Text_SetScale(state->p1_char_text, 0, 0.7, 0.7);

    // Player 1 ready indicator
    state->p1_ready_text = Text_CreateText2(0, state->canvas_id,
        P1_NAME_X, P1_READY_Y, 0.0, 7.0, 1.5);
    if (state->is_active_player &&
        state->local_port == state->active_ports[0])
    {
        Text_AddSubtext(state->p1_ready_text, 0.0, 0.0, "Picking...");
        Text_SetColor(state->p1_ready_text, 0, &yellow);
    }
    else
    {
        Text_AddSubtext(state->p1_ready_text, 0.0, 0.0, "...");
        Text_SetColor(state->p1_ready_text, 0, &gray);
    }
    Text_SetScale(state->p1_ready_text, 0, 0.55, 0.55);

    // "VS" text
    state->vs_text = Text_CreateText2(0, state->canvas_id,
        VS_X, VS_Y, 0.0, 3.0, 2.0);
    Text_AddSubtext(state->vs_text, 0.0, 0.0, "VS");
    Text_SetColor(state->vs_text, 0, &red);
    Text_SetScale(state->vs_text, 0, 1.5, 1.5);

    // Player 2 name
    char *p2_name = get_player_name(state->msrb, state->active_ports[1]);
    state->p2_text = Text_CreateText2(0, state->canvas_id,
        P2_NAME_X, P2_NAME_Y, 0.0, 7.0, 1.5);
    Text_AddSubtext(state->p2_text, 0.0, 0.0, "%s", p2_name);
    Text_SetColor(state->p2_text, 0, &white);
    Text_SetScale(state->p2_text, 0, 1.0, 1.0);

    // Player 2 character (unknown until fetched)
    state->p2_char_text = Text_CreateText2(0, state->canvas_id,
        P2_NAME_X, P2_CHAR_Y, 0.0, 7.0, 1.5);
    Text_AddSubtext(state->p2_char_text, 0.0, 0.0, "...");
    Text_SetColor(state->p2_char_text, 0, &green);
    Text_SetScale(state->p2_char_text, 0, 0.7, 0.7);

    // Player 2 ready indicator
    state->p2_ready_text = Text_CreateText2(0, state->canvas_id,
        P2_NAME_X, P2_READY_Y, 0.0, 7.0, 1.5);
    if (state->is_active_player &&
        state->local_port == state->active_ports[1])
    {
        Text_AddSubtext(state->p2_ready_text, 0.0, 0.0, "Picking...");
        Text_SetColor(state->p2_ready_text, 0, &yellow);
    }
    else
    {
        Text_AddSubtext(state->p2_ready_text, 0.0, 0.0, "...");
        Text_SetColor(state->p2_ready_text, 0, &gray);
    }
    Text_SetScale(state->p2_ready_text, 0, 0.55, 0.55);

    // =====================================================================
    // SIDEBAR: Waiting area
    // =====================================================================

    if (state->wait_count > 0)
    {
        // "WAITING AREA" header
        state->side_header_text = Text_CreateText2(0, state->canvas_id,
            SIDE_LEFT, SIDE_HEADER_Y, 0.0, SIDE_WIDTH, 1.5);
        Text_AddSubtext(state->side_header_text, 0.0, 0.0, "WAITING AREA");
        Text_SetColor(state->side_header_text, 0, &yellow);
        Text_SetScale(state->side_header_text, 0, 0.7, 0.7);

        // Queue entries
        float y = SIDE_FIRST_Y;
        for (int i = 0; i < state->wait_count; i++)
        {
            u8 port = state->waiting_ports[i];
            if (port == 0xFF) continue;

            char *name = get_player_name(state->msrb, port);
            const char *prefix = (i == 0) ? "Next" : "  %d.";

            state->queue_texts[i] = Text_CreateText2(0, state->canvas_id,
                SIDE_LEFT, y, 0.0, SIDE_WIDTH, 1.2);

            if (i == 0)
            {
                Text_AddSubtext(state->queue_texts[i], 0.0, 0.0,
                    "Next: %s", name);
                Text_SetColor(state->queue_texts[i], 0, &yellow);
            }
            else
            {
                Text_AddSubtext(state->queue_texts[i], 0.0, 0.0,
                    "  %d. %s", i + 1, name);
                Text_SetColor(state->queue_texts[i], 0, &gray);
            }
            Text_SetScale(state->queue_texts[i], 0, 0.6, 0.6);

            y += SIDE_LINE_H;
        }
    }

    // =====================================================================
    // BOTTOM BAR: Game count + Controls prompt
    // =====================================================================

    // Game count
    state->game_text = Text_CreateText2(0, state->canvas_id,
        BOT_GAME_X, BOT_Y, 0.0, 8.0, 1.5);
    Text_AddSubtext(state->game_text, 0.0, 0.0,
        "Game %d", state->games_played + 1);
    Text_SetColor(state->game_text, 0, &gray);
    Text_SetScale(state->game_text, 0, 0.7, 0.7);

    // Prompt text
    state->prompt_text = Text_CreateText2(0, state->canvas_id,
        BOT_PROMPT_X, BOT_Y, 0.0, 20.0, 1.5);

    if (state->is_active_player)
    {
        Text_AddSubtext(state->prompt_text, 0.0, 0.0,
            "D-Pad: Change Char    A: Confirm");
        Text_SetColor(state->prompt_text, 0, &white);
    }
    else
    {
        Text_AddSubtext(state->prompt_text, 0.0, 0.0,
            "Spectating - waiting for match");
        Text_SetColor(state->prompt_text, 0, &gray);
        state->local_ready = 1;
    }
    Text_SetScale(state->prompt_text, 0, 0.6, 0.6);
}

// ---------------------------------------------------------------------------
// minor_think — called every frame
// ---------------------------------------------------------------------------
void minor_think(void)
{
    if (!state) return;
    state->frame_count++;

    // --- Update timer display ---
    {
        int remaining;
        if (state->is_active_player)
            remaining = TIMER_TOTAL_FRAMES - state->frame_count;
        else
            remaining = SPECTATOR_WAIT - state->frame_count;

        if (remaining < 0) remaining = 0;

        int secs = remaining / 60;
        int tenths = (remaining % 60) * 10 / 60;

        // Change color when low
        if (state->is_active_player)
        {
            if (secs <= 5)
            {
                GXColor red = {0xDB, 0x28, 0x28, 0xFF};
                Text_SetText(state->timer_text, 0, "0:%02d", secs);
                Text_SetColor(state->timer_text, 0, &red);
            }
            else if (secs <= 10)
            {
                GXColor yellow = {0xFF, 0xD7, 0x00, 0xFF};
                Text_SetText(state->timer_text, 0, "0:%02d", secs);
                Text_SetColor(state->timer_text, 0, &yellow);
            }
            else
            {
                Text_SetText(state->timer_text, 0, "0:%02d", secs);
            }
        }
        else
        {
            Text_SetText(state->timer_text, 0, "0:%02d", secs);
        }
    }

    // --- Active player input ---
    if (state->is_active_player && !state->local_ready)
    {
        // Determine which side we are (P1 = left, P2 = right)
        int is_p1 = (state->local_port == state->active_ports[0]);
        Text *my_char_text = is_p1 ? state->p1_char_text : state->p2_char_text;
        Text *my_ready_text = is_p1 ? state->p1_ready_text : state->p2_ready_text;

        HSD_Pad *pad = PadGet(state->local_port, PADGET_ENGINE);

        // D-pad left/right: cycle character
        if (pad->down & HSD_BUTTON_DPAD_RIGHT)
        {
            state->selected_char = (state->selected_char + 1) % NUM_CHARACTERS;
            Text_SetText(my_char_text, 0,
                "%s", (char *)char_names[state->selected_char]);
        }
        if (pad->down & HSD_BUTTON_DPAD_LEFT)
        {
            state->selected_char =
                (state->selected_char + NUM_CHARACTERS - 1) % NUM_CHARACTERS;
            Text_SetText(my_char_text, 0,
                "%s", (char *)char_names[state->selected_char]);
        }

        // D-pad up/down: cycle color
        if (pad->down & HSD_BUTTON_DPAD_UP)
        {
            state->selected_color = (state->selected_color + 1) % 6;
            // Show color index next to char name
            Text_SetText(my_char_text, 0, "%s [%d]",
                (char *)char_names[state->selected_char],
                state->selected_color + 1);
        }
        if (pad->down & HSD_BUTTON_DPAD_DOWN)
        {
            state->selected_color = (state->selected_color + 5) % 6;
            Text_SetText(my_char_text, 0, "%s [%d]",
                (char *)char_names[state->selected_char],
                state->selected_color + 1);
        }

        // A button: confirm selection
        if (pad->down & HSD_BUTTON_A)
        {
            state->local_ready = 1;
            send_char_selection(state->selected_char, state->selected_color);

            // Update ready indicator
            GXColor green = {0x21, 0xBA, 0x45, 0xFF};
            Text_SetText(my_ready_text, 0, "READY");
            Text_SetColor(my_ready_text, 0, &green);

            // Update prompt
            Text_SetText(state->prompt_text, 0, "Waiting for opponent...");
            GXColor gray = {0x99, 0x99, 0x99, 0xFF};
            Text_SetColor(state->prompt_text, 0, &gray);
        }
    }

    // --- Poll for opponent's selection ---
    if (state->is_active_player && !state->opponent_ready)
    {
        if (state->frame_count % 10 == 0)
        {
            u8 opp_char, opp_color;
            if (fetch_opponent_selection(&opp_char, &opp_color))
            {
                state->opponent_ready = 1;
                state->opp_char = opp_char;
                state->opp_color = opp_color;

                // Determine opponent side
                int local_is_p1 =
                    (state->local_port == state->active_ports[0]);
                Text *opp_char_text = local_is_p1 ?
                    state->p2_char_text : state->p1_char_text;
                Text *opp_ready_text = local_is_p1 ?
                    state->p2_ready_text : state->p1_ready_text;

                if (opp_char < NUM_CHARACTERS)
                {
                    Text_SetText(opp_char_text, 0,
                        "%s", (char *)char_names[opp_char]);
                }

                GXColor green = {0x21, 0xBA, 0x45, 0xFF};
                Text_SetText(opp_ready_text, 0, "READY");
                Text_SetColor(opp_ready_text, 0, &green);
            }
        }
    }

    // --- Check if we should advance ---
    int should_advance = 0;

    if (state->is_active_player)
    {
        if (state->local_ready && state->opponent_ready)
            should_advance = 1;

        if (state->frame_count >= TIMER_TOTAL_FRAMES)
        {
            // Auto-confirm if not ready yet
            if (!state->local_ready)
                send_char_selection(state->selected_char, state->selected_color);
            should_advance = 1;
        }
    }
    else
    {
        if (state->frame_count >= SPECTATOR_WAIT)
            should_advance = 1;
    }

    if (should_advance)
    {
        Scene_ExitMinor();
    }
}

// ---------------------------------------------------------------------------
// minor_exit — called when leaving the scene
// ---------------------------------------------------------------------------
void minor_exit(void *data)
{
    if (!state) return;

    // Destroy all text objects
    if (state->lobby_name_text)   Text_Destroy(state->lobby_name_text);
    if (state->timer_text)        Text_Destroy(state->timer_text);
    if (state->player_count_text) Text_Destroy(state->player_count_text);
    if (state->match_header_text) Text_Destroy(state->match_header_text);
    if (state->p1_text)           Text_Destroy(state->p1_text);
    if (state->p2_text)           Text_Destroy(state->p2_text);
    if (state->p1_char_text)      Text_Destroy(state->p1_char_text);
    if (state->p2_char_text)      Text_Destroy(state->p2_char_text);
    if (state->p1_ready_text)     Text_Destroy(state->p1_ready_text);
    if (state->p2_ready_text)     Text_Destroy(state->p2_ready_text);
    if (state->vs_text)           Text_Destroy(state->vs_text);
    if (state->side_header_text)  Text_Destroy(state->side_header_text);
    if (state->prompt_text)       Text_Destroy(state->prompt_text);
    if (state->game_text)         Text_Destroy(state->game_text);

    // Background panels
    if (state->bg_top)    Text_Destroy(state->bg_top);
    if (state->bg_panel)  Text_Destroy(state->bg_panel);
    if (state->bg_side)   Text_Destroy(state->bg_side);
    if (state->bg_bottom) Text_Destroy(state->bg_bottom);

    for (int i = 0; i < ROT_MAX_WAITING; i++)
    {
        if (state->queue_texts[i])
            Text_Destroy(state->queue_texts[i]);
    }

    if (state->msrb)
        HSD_Free(state->msrb);

    HSD_Free(state);
    state = 0;
}
