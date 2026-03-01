// Rotation Lobby Minor Scene — lobby UI (mnFunction)
// 3D rendering + JOBJ components from GameSetup_gui.dat
//
// Receives SharedMinorData from major scene via load_data pointer.
// MSRB is already fetched by major_load — no EXI transfer needed here.

#include "RotationLobby.h"

// ---------------------------------------------------------------------------
// GUI_GameSetup — layout of GameSetup_gui.dat archive
// ---------------------------------------------------------------------------
typedef struct {
    JOBJSet **jobjs;
    COBJDesc **cobjs;
    void **lights;
    void **fog;
} GUI_GameSetup;

// JOBJ indices within GameSetup_gui.dat
// Index 0 (Background) crashes — external file refs. Don't use.
#define GUI_JOBJ_Panels      1
#define GUI_JOBJ_StockIcon   10

// Sheik char ID for stock icon frame adjustment
#define CKIND_SHEIK 0x13

#define DEVTEXT_LIST_HEAD (*(void**)((char*)R13 + (-0x4884)))
#define LOBBY_DURATION 600

// ---------------------------------------------------------------------------
// State
// ---------------------------------------------------------------------------
static int frame_count = 0;
static HSD_Archive *gui_archive = 0;
static GUI_GameSetup *gui_assets = 0;
static DevText *devtext = 0;
static void *devtext_alloc = 0;
static SharedMinorData *shared = 0;

// JOBJ components
static GOBJ *p1_icon_gobj = 0;
static JOBJ *p1_icon_jobj = 0;
static JOBJSet *stock_icon_set = 0;

static GOBJ *p2_icon_gobj = 0;
static JOBJ *p2_icon_jobj = 0;

void CObjThink(GOBJ *gobj);

// ---------------------------------------------------------------------------
// Helper: get player name from MSRB by port index (0-3)
// ---------------------------------------------------------------------------
static char *get_name(int port)
{
    if (port > 3) return "---";
    return (char *)&shared->msrb[OFST_P1_NAME + port * MSRB_NAME_SIZE];
}

// ---------------------------------------------------------------------------
// Helper: set stock icon to a character + color
// ---------------------------------------------------------------------------
static void StockIcon_SetFrame(JOBJ *root_jobj, JOBJSet *set, u8 charId, u8 charColor)
{
    u32 adjId = charId;
    if (charId == CKIND_SHEIK) adjId = 29;
    else if (charId > CKIND_SHEIK) adjId--;

    JOBJ_AddSetAnim(root_jobj, set, 0);
    JOBJ_ReqAnimAll(root_jobj, adjId + (30 * charColor));
    JOBJ_AnimAll(root_jobj);
    JOBJ_RemoveAnimAll(root_jobj);
}

// ---------------------------------------------------------------------------
// minor_load
// ---------------------------------------------------------------------------
void minor_load(void *load_data)
{
    frame_count = 0;
    shared = (SharedMinorData *)load_data;

    // 3D rendering setup — camera, fog, lights from GameSetup_gui.dat
    gui_archive = Archive_LoadFile("GameSetup_gui.dat");
    gui_assets = Archive_GetPublicAddress(gui_archive, "ScGamTour_scene_data");

    GOBJ *cam_gobj = GObj_Create(2, 3, 128);
    COBJ *cam_cobj = COBJ_LoadDesc(gui_assets->cobjs[0]);
    GObj_AddObject(cam_gobj, 1, cam_cobj);
    GOBJ_InitCamera(cam_gobj, CObjThink, 0);
    cam_gobj->cobj_links = (1 << 0) | (1 << 1) | (1 << 2) | (1 << 3) | (1 << 4);

    GOBJ *fog_gobj = GObj_Create(14, 2, 0);
    HSD_Fog *fog = Fog_LoadDesc(gui_assets->fog[0]);
    GObj_AddObject(fog_gobj, 4, fog);
    GObj_AddGXLink(fog_gobj, GXLink_Fog, 0, 128);

    GOBJ *light_gobj = GObj_Create(3, 4, 128);
    LOBJ *lobj = LObj_CreateAll(gui_assets->lights);
    GObj_AddObject(light_gobj, 2, lobj);
    GObj_AddGXLink(light_gobj, GXLink_LObj, 0, 128);

    // Panels — dark background panels from GameSetup_gui.dat
    JOBJ_LoadSet(0, gui_assets->jobjs[GUI_JOBJ_Panels], 0, 0, 3, 1, 1, GObj_Anim);

    // StockIcon — P1 (left side)
    stock_icon_set = gui_assets->jobjs[GUI_JOBJ_StockIcon];
    p1_icon_gobj = JOBJ_LoadSet(0, stock_icon_set, 0, 0, 3, 1, 0, 0);
    p1_icon_jobj = p1_icon_gobj->hsd_object;
    p1_icon_jobj->trans.X = -5.0f;
    p1_icon_jobj->trans.Y = 3.0f;
    p1_icon_jobj->trans.Z = 0.0f;
    JOBJ_SetMtxDirtySub(p1_icon_jobj);
    StockIcon_SetFrame(p1_icon_jobj, stock_icon_set, 0, 0);  // Fox default

    // StockIcon — P2 (right side)
    p2_icon_gobj = JOBJ_LoadSet(0, stock_icon_set, 0, 0, 3, 1, 0, 0);
    p2_icon_jobj = p2_icon_gobj->hsd_object;
    p2_icon_jobj->trans.X = 5.0f;
    p2_icon_jobj->trans.Y = 3.0f;
    p2_icon_jobj->trans.Z = 0.0f;
    JOBJ_SetMtxDirtySub(p2_icon_jobj);
    StockIcon_SetFrame(p2_icon_jobj, stock_icon_set, 9, 0);  // Marth default

    // DevelopText — 39 cols x 14 rows (for text overlay)
    devtext_alloc = HSD_MemAlloc(0x2000);
    devtext = DevelopText_CreateDataTable(12, 0, 0, 39, 14, devtext_alloc);
    DevelopText_Activate(DEVTEXT_LIST_HEAD, devtext);
    *(u8*)((char*)devtext + 0x26) = 0;
    DevelopText_HideBG(devtext);
}

// ---------------------------------------------------------------------------
// CObjThink
// ---------------------------------------------------------------------------
void CObjThink(GOBJ *gobj)
{
    COBJ *cobj = gobj->hsd_object;
    if (!CObj_SetCurrent(cobj))
        return;
    CObj_SetEraseColor(0, 0, 0, 255);
    CObj_EraseScreen(cobj, 1, 0, 1);
    CObj_RenderGXLinks(gobj, 7);
    CObj_EndCurrent();
}

// ---------------------------------------------------------------------------
// minor_think
// ---------------------------------------------------------------------------
void minor_think(void)
{
    frame_count++;

    int secs_left = (LOBBY_DURATION - frame_count) / 60;
    if (secs_left < 0) secs_left = 0;

    DevelopText_EraseAllText(devtext);

    // Row 0: Title + timer
    DevelopText_ResetCursorXY(devtext, 0, 0);
    DevelopText_AddString(devtext, "       ROTATION LOBBY");
    DevelopText_ResetCursorXY(devtext, 35, 0);
    DevelopText_AddString(devtext, ":%02d", secs_left);

    // Row 2: Match header
    DevelopText_ResetCursorXY(devtext, 0, 2);
    DevelopText_AddString(devtext, "  Game %d", shared->games_played + 1);

    // Row 4: NOW PLAYING
    DevelopText_ResetCursorXY(devtext, 0, 4);
    DevelopText_AddString(devtext, "  NOW PLAYING:");

    // Row 5: P1 vs P2
    DevelopText_ResetCursorXY(devtext, 0, 5);
    DevelopText_AddString(devtext, "    %s  vs  %s",
        get_name(shared->active_ports[0]), get_name(shared->active_ports[1]));

    // Row 7: Last winner
    if (shared->last_winner != 0xFF) {
        DevelopText_ResetCursorXY(devtext, 0, 7);
        DevelopText_AddString(devtext, "  Last Winner: %s",
            get_name(shared->last_winner));
    }

    // Row 9: Waiting queue
    DevelopText_ResetCursorXY(devtext, 0, 9);
    DevelopText_AddString(devtext, "  NEXT UP:");

    int row = 10;
    int i;
    for (i = 0; i < ROT_MAX_WAITING; i++) {
        u8 port = shared->waiting_ports[i];
        if (port == 0xFF) break;
        if (row >= 13) break;
        DevelopText_ResetCursorXY(devtext, 0, row);
        if (port == shared->local_port)
            DevelopText_AddString(devtext, "    %d. %s  <-- YOU", i + 1, get_name(port));
        else
            DevelopText_AddString(devtext, "    %d. %s", i + 1, get_name(port));
        row++;
    }

    // If local player is active, show that
    if (shared->local_port == shared->active_ports[0] ||
        shared->local_port == shared->active_ports[1]) {
        DevelopText_ResetCursorXY(devtext, 0, 13);
        DevelopText_AddString(devtext, "  YOU ARE PLAYING NEXT!");
    }

    if (frame_count >= LOBBY_DURATION)
        Scene_ExitMinor();
}

// ---------------------------------------------------------------------------
// minor_exit
// ---------------------------------------------------------------------------
void minor_exit(void *unload_data)
{
    if (devtext) {
        DevelopText_HideText(devtext);
        void **list_head = (void**)((char*)R13 + (-0x4884));
        *list_head = 0;
        devtext = 0;
        devtext_alloc = 0;
    }
    p1_icon_gobj = 0;
    p1_icon_jobj = 0;
    p2_icon_gobj = 0;
    p2_icon_jobj = 0;
    stock_icon_set = 0;
    gui_assets = 0;
    if (gui_archive) {
        Archive_Free(gui_archive);
        gui_archive = 0;
    }
    shared = 0;
    frame_count = 0;
}
