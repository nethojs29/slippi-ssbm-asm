// Rotation Lobby Minor Scene — lobby UI (mnFunction)
// Compiled to RotationLobby.dat via mnFunction symbol.
//
// Exports: minor_think, minor_load, minor_exit
//
// Refactored from rotation_lobby.c — receives SharedMinorData from major
// scene instead of loading MSRB directly.

#include "major.h"

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
// Lobby UI state (allocated per-scene-load, freed on exit)
// ---------------------------------------------------------------------------
typedef struct {
    int frame_count;
    int local_ready;
    int opponent_ready;
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

    // Pointer to shared data from major scene
    SharedMinorData *shared;
} LobbyUIState;

static LobbyUIState *ui = 0;

// ---------------------------------------------------------------------------
// MSRB helpers (read from shared data's MSRB copy)
// ---------------------------------------------------------------------------
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
// ---------------------------------------------------------------------------
static Text *create_bg_panel(int canvas_id, float x, float y,
                              float w, float h, GXColor *color)
{
    Text *t = Text_CreateText2(0, canvas_id, x, y, 0.0, w, h);
    Text_AddSubtext(t, 0.0, 0.0, " ");
    t->viewport_color = *color;
    t->use_aspect = 1;
    GXColor clear = {0, 0, 0, 0};
    Text_SetColor(t, 0, &clear);
    return t;
}

// ---------------------------------------------------------------------------
// minor_load — called when the minor scene is entered
// Receives SharedMinorData from major scene via load_data pointer.
// ---------------------------------------------------------------------------
void minor_load(SharedMinorData *data)
{
    ui = HSD_MemAlloc(sizeof(LobbyUIState));
    memset(ui, 0, sizeof(LobbyUIState));
    ui->shared = data;

    // Count waiting players
    ui->wait_count = 0;
    for (int i = 0; i < ROT_MAX_WAITING; i++) {
        if (data->waiting_ports[i] != 0xFF)
            ui->wait_count++;
    }

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
    ui->canvas_id = Text_CreateCanvas(0, 0, 0, 13, 80, 8, 0, 0);

    // --- Background panels ---
    ui->bg_top = create_bg_panel(ui->canvas_id,
        0.0, 0.0, 30.0, 2.2, &bg_dark);

    ui->bg_panel = create_bg_panel(ui->canvas_id,
        PANEL_LEFT - 0.5, PANEL_TOP - 0.5, PANEL_WIDTH + 1.0, 7.5, &bg_panel);

    if (ui->wait_count > 0) {
        float side_h = 2.5 + ui->wait_count * SIDE_LINE_H;
        ui->bg_side = create_bg_panel(ui->canvas_id,
            SIDE_LEFT - 0.5, SIDE_TOP - 0.5, SIDE_WIDTH + 1.0, side_h, &bg_side);
    }

    ui->bg_bottom = create_bg_panel(ui->canvas_id,
        0.0, BOT_Y - 0.5, 30.0, 2.5, &bg_bottom);

    // =====================================================================
    // TOP BAR: Lobby name | Timer | Player count
    // =====================================================================

    // Lobby name = local player's connect code
    ui->lobby_name_text = Text_CreateText2(0, ui->canvas_id,
        TOP_LABEL_X, TOP_Y, 0.0, 10.0, 1.5);
    char *code = get_connect_code(data->msrb, data->local_port);
    Text_AddSubtext(ui->lobby_name_text, 0.0, 0.0, "%s", code);
    Text_SetColor(ui->lobby_name_text, 0, &cyan);
    Text_SetScale(ui->lobby_name_text, 0, 0.9, 0.9);

    // Timer
    ui->timer_text = Text_CreateText2(0, ui->canvas_id,
        TOP_TIMER_X, TOP_Y, 0.0, 6.0, 1.5);
    Text_AddSubtext(ui->timer_text, 0.0, 0.0, "0:30");
    Text_SetColor(ui->timer_text, 0, &white);
    Text_SetScale(ui->timer_text, 0, 1.1, 1.1);

    // Player count
    ui->player_count_text = Text_CreateText2(0, ui->canvas_id,
        TOP_COUNT_X, TOP_Y, 0.0, 8.0, 1.5);
    Text_AddSubtext(ui->player_count_text, 0.0, 0.0,
        "%d Players", data->player_count);
    Text_SetColor(ui->player_count_text, 0, &gray);
    Text_SetScale(ui->player_count_text, 0, 0.8, 0.8);

    // =====================================================================
    // MATCH PANEL: P1 name + char   VS   P2 name + char
    // =====================================================================

    // "PLAYERS IN MATCH" header
    ui->match_header_text = Text_CreateText2(0, ui->canvas_id,
        PANEL_LEFT + 3.0, MATCH_HEADER_Y, 0.0, 16.0, 1.5);
    Text_AddSubtext(ui->match_header_text, 0.0, 0.0, "PLAYERS IN MATCH");
    Text_SetColor(ui->match_header_text, 0, &green);
    Text_SetScale(ui->match_header_text, 0, 0.9, 0.9);

    // Player 1 name
    char *p1_name = get_player_name(data->msrb, data->active_ports[0]);
    ui->p1_text = Text_CreateText2(0, ui->canvas_id,
        P1_NAME_X, P1_NAME_Y, 0.0, 7.0, 1.5);
    Text_AddSubtext(ui->p1_text, 0.0, 0.0, "%s", p1_name);
    Text_SetColor(ui->p1_text, 0, &white);
    Text_SetScale(ui->p1_text, 0, 1.0, 1.0);

    // Player 1 character
    ui->p1_char_text = Text_CreateText2(0, ui->canvas_id,
        P1_NAME_X, P1_CHAR_Y, 0.0, 7.0, 1.5);
    Text_AddSubtext(ui->p1_char_text, 0.0, 0.0,
        "%s", (char *)char_names[data->selected_char % NUM_CHARACTERS]);
    Text_SetColor(ui->p1_char_text, 0, &green);
    Text_SetScale(ui->p1_char_text, 0, 0.7, 0.7);

    // Player 1 ready indicator
    ui->p1_ready_text = Text_CreateText2(0, ui->canvas_id,
        P1_NAME_X, P1_READY_Y, 0.0, 7.0, 1.5);
    if (data->is_active_player &&
        data->local_port == data->active_ports[0])
    {
        Text_AddSubtext(ui->p1_ready_text, 0.0, 0.0, "Picking...");
        Text_SetColor(ui->p1_ready_text, 0, &yellow);
    }
    else
    {
        Text_AddSubtext(ui->p1_ready_text, 0.0, 0.0, "...");
        Text_SetColor(ui->p1_ready_text, 0, &gray);
    }
    Text_SetScale(ui->p1_ready_text, 0, 0.55, 0.55);

    // "VS" text
    ui->vs_text = Text_CreateText2(0, ui->canvas_id,
        VS_X, VS_Y, 0.0, 3.0, 2.0);
    Text_AddSubtext(ui->vs_text, 0.0, 0.0, "VS");
    Text_SetColor(ui->vs_text, 0, &red);
    Text_SetScale(ui->vs_text, 0, 1.5, 1.5);

    // Player 2 name
    char *p2_name = get_player_name(data->msrb, data->active_ports[1]);
    ui->p2_text = Text_CreateText2(0, ui->canvas_id,
        P2_NAME_X, P2_NAME_Y, 0.0, 7.0, 1.5);
    Text_AddSubtext(ui->p2_text, 0.0, 0.0, "%s", p2_name);
    Text_SetColor(ui->p2_text, 0, &white);
    Text_SetScale(ui->p2_text, 0, 1.0, 1.0);

    // Player 2 character (unknown until fetched)
    ui->p2_char_text = Text_CreateText2(0, ui->canvas_id,
        P2_NAME_X, P2_CHAR_Y, 0.0, 7.0, 1.5);
    Text_AddSubtext(ui->p2_char_text, 0.0, 0.0, "...");
    Text_SetColor(ui->p2_char_text, 0, &green);
    Text_SetScale(ui->p2_char_text, 0, 0.7, 0.7);

    // Player 2 ready indicator
    ui->p2_ready_text = Text_CreateText2(0, ui->canvas_id,
        P2_NAME_X, P2_READY_Y, 0.0, 7.0, 1.5);
    if (data->is_active_player &&
        data->local_port == data->active_ports[1])
    {
        Text_AddSubtext(ui->p2_ready_text, 0.0, 0.0, "Picking...");
        Text_SetColor(ui->p2_ready_text, 0, &yellow);
    }
    else
    {
        Text_AddSubtext(ui->p2_ready_text, 0.0, 0.0, "...");
        Text_SetColor(ui->p2_ready_text, 0, &gray);
    }
    Text_SetScale(ui->p2_ready_text, 0, 0.55, 0.55);

    // =====================================================================
    // SIDEBAR: Waiting area
    // =====================================================================

    if (ui->wait_count > 0)
    {
        ui->side_header_text = Text_CreateText2(0, ui->canvas_id,
            SIDE_LEFT, SIDE_HEADER_Y, 0.0, SIDE_WIDTH, 1.5);
        Text_AddSubtext(ui->side_header_text, 0.0, 0.0, "WAITING AREA");
        Text_SetColor(ui->side_header_text, 0, &yellow);
        Text_SetScale(ui->side_header_text, 0, 0.7, 0.7);

        float y = SIDE_FIRST_Y;
        for (int i = 0; i < ui->wait_count; i++)
        {
            u8 port = data->waiting_ports[i];
            if (port == 0xFF) continue;

            char *name = get_player_name(data->msrb, port);

            ui->queue_texts[i] = Text_CreateText2(0, ui->canvas_id,
                SIDE_LEFT, y, 0.0, SIDE_WIDTH, 1.2);

            if (i == 0)
            {
                Text_AddSubtext(ui->queue_texts[i], 0.0, 0.0,
                    "Next: %s", name);
                Text_SetColor(ui->queue_texts[i], 0, &yellow);
            }
            else
            {
                Text_AddSubtext(ui->queue_texts[i], 0.0, 0.0,
                    "  %d. %s", i + 1, name);
                Text_SetColor(ui->queue_texts[i], 0, &gray);
            }
            Text_SetScale(ui->queue_texts[i], 0, 0.6, 0.6);

            y += SIDE_LINE_H;
        }
    }

    // =====================================================================
    // BOTTOM BAR: Game count + Controls prompt
    // =====================================================================

    ui->game_text = Text_CreateText2(0, ui->canvas_id,
        BOT_GAME_X, BOT_Y, 0.0, 8.0, 1.5);
    Text_AddSubtext(ui->game_text, 0.0, 0.0,
        "Game %d", data->games_played + 1);
    Text_SetColor(ui->game_text, 0, &gray);
    Text_SetScale(ui->game_text, 0, 0.7, 0.7);

    ui->prompt_text = Text_CreateText2(0, ui->canvas_id,
        BOT_PROMPT_X, BOT_Y, 0.0, 20.0, 1.5);

    if (data->is_active_player)
    {
        Text_AddSubtext(ui->prompt_text, 0.0, 0.0,
            "D-Pad: Change Char    A: Confirm");
        Text_SetColor(ui->prompt_text, 0, &white);
    }
    else
    {
        Text_AddSubtext(ui->prompt_text, 0.0, 0.0,
            "Spectating - waiting for match");
        Text_SetColor(ui->prompt_text, 0, &gray);
        ui->local_ready = 1;
    }
    Text_SetScale(ui->prompt_text, 0, 0.6, 0.6);
}

// ---------------------------------------------------------------------------
// minor_think — called every frame
// ---------------------------------------------------------------------------
void minor_think(void)
{
    if (!ui) return;
    SharedMinorData *data = ui->shared;
    ui->frame_count++;

    // --- Update timer display ---
    {
        int remaining;
        if (data->is_active_player)
            remaining = TIMER_TOTAL_FRAMES - ui->frame_count;
        else
            remaining = SPECTATOR_WAIT - ui->frame_count;

        if (remaining < 0) remaining = 0;

        int secs = remaining / 60;

        if (data->is_active_player)
        {
            if (secs <= 5)
            {
                GXColor red = {0xDB, 0x28, 0x28, 0xFF};
                Text_SetText(ui->timer_text, 0, "0:%02d", secs);
                Text_SetColor(ui->timer_text, 0, &red);
            }
            else if (secs <= 10)
            {
                GXColor yellow = {0xFF, 0xD7, 0x00, 0xFF};
                Text_SetText(ui->timer_text, 0, "0:%02d", secs);
                Text_SetColor(ui->timer_text, 0, &yellow);
            }
            else
            {
                Text_SetText(ui->timer_text, 0, "0:%02d", secs);
            }
        }
        else
        {
            Text_SetText(ui->timer_text, 0, "0:%02d", secs);
        }
    }

    // --- Active player input ---
    if (data->is_active_player && !ui->local_ready)
    {
        int is_p1 = (data->local_port == data->active_ports[0]);
        Text *my_char_text = is_p1 ? ui->p1_char_text : ui->p2_char_text;
        Text *my_ready_text = is_p1 ? ui->p1_ready_text : ui->p2_ready_text;

        HSD_Pad *pad = PadGet(data->local_port, PADGET_ENGINE);

        // D-pad left/right: cycle character
        if (pad->down & HSD_BUTTON_DPAD_RIGHT)
        {
            data->selected_char = (data->selected_char + 1) % NUM_CHARACTERS;
            Text_SetText(my_char_text, 0,
                "%s", (char *)char_names[data->selected_char]);
        }
        if (pad->down & HSD_BUTTON_DPAD_LEFT)
        {
            data->selected_char =
                (data->selected_char + NUM_CHARACTERS - 1) % NUM_CHARACTERS;
            Text_SetText(my_char_text, 0,
                "%s", (char *)char_names[data->selected_char]);
        }

        // D-pad up/down: cycle color
        if (pad->down & HSD_BUTTON_DPAD_UP)
        {
            data->selected_color = (data->selected_color + 1) % 6;
            Text_SetText(my_char_text, 0, "%s [%d]",
                (char *)char_names[data->selected_char],
                data->selected_color + 1);
        }
        if (pad->down & HSD_BUTTON_DPAD_DOWN)
        {
            data->selected_color = (data->selected_color + 5) % 6;
            Text_SetText(my_char_text, 0, "%s [%d]",
                (char *)char_names[data->selected_char],
                data->selected_color + 1);
        }

        // A button: confirm selection
        if (pad->down & HSD_BUTTON_A)
        {
            ui->local_ready = 1;
            data->local_ready = 1;
            send_char_selection(data->selected_char, data->selected_color);

            GXColor green = {0x21, 0xBA, 0x45, 0xFF};
            Text_SetText(my_ready_text, 0, "READY");
            Text_SetColor(my_ready_text, 0, &green);

            Text_SetText(ui->prompt_text, 0, "Waiting for opponent...");
            GXColor gray = {0x99, 0x99, 0x99, 0xFF};
            Text_SetColor(ui->prompt_text, 0, &gray);
        }
    }

    // --- Poll for opponent's selection ---
    if (data->is_active_player && !ui->opponent_ready)
    {
        if (ui->frame_count % 10 == 0)
        {
            u8 opp_char, opp_color;
            if (fetch_opponent_selection(&opp_char, &opp_color))
            {
                ui->opponent_ready = 1;
                data->opponent_ready = 1;
                ui->opp_char = opp_char;
                ui->opp_color = opp_color;

                int local_is_p1 =
                    (data->local_port == data->active_ports[0]);
                Text *opp_char_text = local_is_p1 ?
                    ui->p2_char_text : ui->p1_char_text;
                Text *opp_ready_text = local_is_p1 ?
                    ui->p2_ready_text : ui->p1_ready_text;

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

    if (data->is_active_player)
    {
        if (ui->local_ready && ui->opponent_ready)
            should_advance = 1;

        if (ui->frame_count >= TIMER_TOTAL_FRAMES)
        {
            if (!ui->local_ready)
                send_char_selection(data->selected_char, data->selected_color);
            should_advance = 1;
        }
    }
    else
    {
        if (ui->frame_count >= SPECTATOR_WAIT)
            should_advance = 1;
    }

    if (should_advance)
    {
        Scene_ExitMinor();
    }
}

// ---------------------------------------------------------------------------
// minor_exit — called when leaving the minor scene
// ---------------------------------------------------------------------------
void minor_exit(SharedMinorData *data)
{
    if (!ui) return;

    // Destroy all text objects
    if (ui->lobby_name_text)   Text_Destroy(ui->lobby_name_text);
    if (ui->timer_text)        Text_Destroy(ui->timer_text);
    if (ui->player_count_text) Text_Destroy(ui->player_count_text);
    if (ui->match_header_text) Text_Destroy(ui->match_header_text);
    if (ui->p1_text)           Text_Destroy(ui->p1_text);
    if (ui->p2_text)           Text_Destroy(ui->p2_text);
    if (ui->p1_char_text)      Text_Destroy(ui->p1_char_text);
    if (ui->p2_char_text)      Text_Destroy(ui->p2_char_text);
    if (ui->p1_ready_text)     Text_Destroy(ui->p1_ready_text);
    if (ui->p2_ready_text)     Text_Destroy(ui->p2_ready_text);
    if (ui->vs_text)           Text_Destroy(ui->vs_text);
    if (ui->side_header_text)  Text_Destroy(ui->side_header_text);
    if (ui->prompt_text)       Text_Destroy(ui->prompt_text);
    if (ui->game_text)         Text_Destroy(ui->game_text);

    // Background panels
    if (ui->bg_top)    Text_Destroy(ui->bg_top);
    if (ui->bg_panel)  Text_Destroy(ui->bg_panel);
    if (ui->bg_side)   Text_Destroy(ui->bg_side);
    if (ui->bg_bottom) Text_Destroy(ui->bg_bottom);

    for (int i = 0; i < ROT_MAX_WAITING; i++)
    {
        if (ui->queue_texts[i])
            Text_Destroy(ui->queue_texts[i]);
    }

    HSD_Free(ui);
    ui = 0;
}
