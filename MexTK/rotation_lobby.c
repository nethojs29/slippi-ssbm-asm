// Rotation Lobby Scene - MexTK source
// Uses Melee native Text system + GameSetup_gui.dat camera/lights/JOBJs
// Active players select characters via CSIcon portraits + CharPickerDialog grid

#include "../../m-ex/MexTK/include/mex.h"

// EXI transfer
typedef void (*EXITransferFn)(void *buf, int len, int mode);
#define ExiSlippi_Transfer ((EXITransferFn)0x800055F0)
#define EXI_WRITE 1
#define EXI_READ  0

// EXI commands
#define CMD_GP_COMPLETE_STEP      0xC0
#define CMD_GP_FETCH_STEP         0xC1
#define CMD_SET_SELECTIONS        0xB5
#define CMD_CLEANUP_CONNECTIONS   0xBA

// Z-hold disconnect (2 seconds = 120 frames at 60fps)
#define Z_HOLD_DISCONNECT_FRAMES  120

// MSRB offsets (must match Online.s)
#define MSRB_TOTAL_SIZE          1003
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
#define OFST_ROT_LOBBY_CODE      985
#define MSRB_LOBBY_CODE_SIZE     18

// Match block within MSRB: player charId/charColor
#define OFST_MATCH_BLOCK         606
#define MATCH_PLAYER_STRIDE      0x24
#define MATCH_CHAR_ID_OFF        0x60
#define MATCH_CHAR_COLOR_OFF     0x63

// GUI_GameSetup layout
typedef struct {
    JOBJSet **jobjs;
    COBJDesc **cobjs;
    void **lights;
    void **fog;
} GUI_GameSetup;

// JOBJ indices in GameSetup_gui.dat
#define GUI_JOBJ_Background      0
#define GUI_JOBJ_CSIcon          4
#define GUI_JOBJ_CharDialog      9
#define GUI_JOBJ_StockIcon       10
#define GUI_JOBJ_TurnIndicator   11

#ifndef M_PI
#define M_PI 3.14159265358979323846f
#endif

// Sound effect IDs (from CommonSound enum)
#define SFX_BACK    0
#define SFX_ACCEPT  1
#define SFX_NEXT    2

// EXI structs (packed, must match SlippiExiTypes.h)
typedef struct {
    u8 command;
    u8 step_idx;
    u8 char_selection;
    u8 char_color_selection;
    u8 stage_selections[2];
} GpCompleteStepQuery;

typedef struct {
    u8 command;
    u8 step_idx;
} GpFetchStepQuery;

typedef struct {
    u8 is_found;
    u8 is_skip;
    u8 char_selection;
    u8 char_color_selection;
    u8 stage_selections[2];
} GpFetchStepResponse;

typedef struct {
    u8 command;
    u8 team_id;
    u8 char_id;
    u8 char_color_id;
    u8 char_option;
    u16 stage_id;
    u8 stage_option;
    u8 online_mode;
} SetSelectionsQuery;

// =========================================================================
// CSIcon material enum — indices into GameSetup_gui.dat anim set 1
// =========================================================================
typedef enum {
    CSIcon_Mat_Empty,       // 0
    CSIcon_Mat_Question,    // 1
    CSIcon_Mat_Battlefield, // 2
    CSIcon_Mat_Yoshis,      // 3
    CSIcon_Mat_Dreamland,   // 4
    CSIcon_Mat_FD,          // 5
    CSIcon_Mat_Fountain,    // 6
    CSIcon_Mat_Pokemon,     // 7
    CSIcon_Mat_Falcon,      // 8
    CSIcon_Mat_DK,          // 9
    CSIcon_Mat_Fox,         // 10
    CSIcon_Mat_GW,          // 11
    CSIcon_Mat_Kirby,       // 12
    CSIcon_Mat_Bowser,      // 13
    CSIcon_Mat_Link,        // 14
    CSIcon_Mat_Luigi,       // 15
    CSIcon_Mat_Mario,       // 16
    CSIcon_Mat_Marth,       // 17
    CSIcon_Mat_Mewtwo,      // 18
    CSIcon_Mat_Ness,        // 19
    CSIcon_Mat_Peach,       // 20
    CSIcon_Mat_Pikachu,     // 21
    CSIcon_Mat_ICs,         // 22
    CSIcon_Mat_Puff,        // 23
    CSIcon_Mat_Samus,       // 24
    CSIcon_Mat_Yoshi,       // 25
    CSIcon_Mat_Zelda,       // 26
    CSIcon_Mat_Sheik,       // 27
    CSIcon_Mat_Falco,       // 28
    CSIcon_Mat_YLink,       // 29
    CSIcon_Mat_Doc,         // 30
    CSIcon_Mat_Roy,         // 31
    CSIcon_Mat_Pichu,       // 32
    CSIcon_Mat_Ganon,       // 33
} CSIcon_Material;

// CSIcon select states — anim set indices for visual feedback
typedef enum {
    CSIcon_SS_NotSelected,  // 0
    CSIcon_SS_Hover,        // 1
    CSIcon_SS_Disabled,     // 2
    CSIcon_SS_Blink,        // 3
} CSIcon_SelectState;

// =========================================================================
// StockIcon — small colored character stock icon
// =========================================================================
typedef struct {
    u8 is_visible;
    u8 char_id;
    u8 color_id;
} StockIconState;

typedef struct StockIcon {
    GOBJ *gobj;
    JOBJ *root_jobj;
    JOBJSet *jobj_set;
    StockIconState state;
} StockIcon;

static void StockIcon_SetIconInternal(StockIcon *si, u8 charId, u8 charColor)
{
    si->state.char_id = charId;
    si->state.color_id = charColor;

    u32 adjCharId = charId;
    if (charId == CKIND_SHEIK) {
        adjCharId = 29;
    } else if (charId > CKIND_SHEIK) {
        adjCharId--;
    }

    JOBJ_AddSetAnim(si->root_jobj, si->jobj_set, 0);
    JOBJ_ReqAnimAll(si->root_jobj, adjCharId + (30 * charColor));
    JOBJ_AnimAll(si->root_jobj);
    JOBJ_RemoveAnimAll(si->root_jobj);
}

static StockIcon *StockIcon_Init(GUI_GameSetup *gui)
{
    StockIcon *si = calloc(sizeof(StockIcon));
    si->jobj_set = gui->jobjs[GUI_JOBJ_StockIcon];
    si->gobj = JOBJ_LoadSet(0, si->jobj_set, 0, 0, 3, 1, 0, 0);
    si->root_jobj = si->gobj->hsd_object;
    StockIcon_SetIconInternal(si, 0, 0);
    return si;
}

static void StockIcon_SetIcon(StockIcon *si, u8 charId, u8 charColor)
{
    if (charId == si->state.char_id && charColor == si->state.color_id)
        return;
    StockIcon_SetIconInternal(si, charId, charColor);
}

// =========================================================================
// CSIcon — character portrait tile with stock icon overlay
// =========================================================================
typedef struct {
    CSIcon_Material material;
    CSIcon_SelectState select_state;
    u8 is_visible;
    u8 is_stock_icon_visible;
} CSIconState;

typedef struct CSIcon {
    GOBJ *gobj;
    JOBJ *root_jobj;
    JOBJSet *jobj_set;
    JOBJ *stock_bg_jobj;
    StockIcon *stock_icon;
    CSIconState state;
} CSIcon;

static void CSIcon_SetSelectStateInternal(CSIcon *icon, CSIcon_SelectState state)
{
    JOBJ_RemoveAnimAll(icon->root_jobj);

    float alpha = 1;
    if (state == CSIcon_SS_Disabled)
        alpha = 0.3f;

    HSD_Material *bg_mat = icon->root_jobj->child->dobj->mobj->mat;
    HSD_Material *fg_mat = icon->root_jobj->child->dobj->next->mobj->mat;
    HSD_Material *stock_border_mat = icon->stock_bg_jobj->dobj->next->mobj->mat;

    bg_mat->alpha = alpha;
    fg_mat->alpha = alpha;
    bg_mat->diffuse = (GXColor){128, 128, 128, 255};
    stock_border_mat->diffuse = (GXColor){128, 128, 128, 255};

    if (state == CSIcon_SS_Blink) {
        JOBJ_AddSetAnim(icon->root_jobj, icon->jobj_set, 0);
        JOBJ_ReqAnimAll(icon->root_jobj, 0);
    } else if (state == CSIcon_SS_Hover) {
        JOBJ_AddSetAnim(icon->root_jobj, icon->jobj_set, 2);
        JOBJ_ReqAnimAll(icon->root_jobj, 0);
    }

    icon->state.select_state = state;
}

static void CSIcon_SetStockIconVisInternal(CSIcon *icon, u8 is_visible)
{
    icon->state.is_stock_icon_visible = is_visible;
    if (is_visible && icon->state.is_visible) {
        icon->stock_bg_jobj->flags &= ~JOBJ_HIDDEN;
        icon->stock_icon->root_jobj->child->flags &= ~JOBJ_HIDDEN;
    } else {
        icon->stock_bg_jobj->flags |= JOBJ_HIDDEN;
        icon->stock_icon->root_jobj->child->flags |= JOBJ_HIDDEN;
    }
}

static void CSIcon_SetVisInternal(CSIcon *icon, u8 is_visible)
{
    JOBJ *jobj = icon->root_jobj->child;
    icon->state.is_visible = is_visible;
    if (is_visible)
        jobj->flags &= ~JOBJ_HIDDEN;
    else
        jobj->flags |= JOBJ_HIDDEN;
    CSIcon_SetStockIconVisInternal(icon, icon->state.is_stock_icon_visible);
}

static CSIcon *CSIcon_Init(GUI_GameSetup *gui)
{
    CSIcon *icon = calloc(sizeof(CSIcon));
    icon->jobj_set = gui->jobjs[GUI_JOBJ_CSIcon];
    icon->gobj = JOBJ_LoadSet(0, icon->jobj_set, 0, 0, 3, 1, 0, GObj_Anim);
    icon->root_jobj = icon->gobj->hsd_object;
    icon->stock_bg_jobj = icon->root_jobj->child->sibling->child;

    icon->stock_icon = StockIcon_Init(gui);
    JOBJ_AddChild(icon->root_jobj->child->sibling, icon->stock_icon->root_jobj);

    icon->state.is_stock_icon_visible = 0;
    CSIcon_SetSelectStateInternal(icon, CSIcon_SS_NotSelected);
    CSIcon_SetVisInternal(icon, 1);

    return icon;
}

static void CSIcon_SetMaterial(CSIcon *icon, CSIcon_Material matIdx)
{
    JOBJ_AddSetAnim(icon->root_jobj, icon->jobj_set, 1);
    JOBJ_ReqAnimAll(icon->root_jobj, matIdx);
    JOBJ_AnimAll(icon->root_jobj);
    JOBJ_RemoveAnimAll(icon->root_jobj);
    icon->state.material = matIdx;
}

static CSIcon_Material CSIcon_CharToMat(int charId)
{
    switch (charId) {
    case CKIND_FALCON:      return CSIcon_Mat_Falcon;
    case CKIND_DK:          return CSIcon_Mat_DK;
    case CKIND_FOX:         return CSIcon_Mat_Fox;
    case CKIND_GAW:         return CSIcon_Mat_GW;
    case CKIND_KIRBY:       return CSIcon_Mat_Kirby;
    case CKIND_BOWSER:      return CSIcon_Mat_Bowser;
    case CKIND_LINK:        return CSIcon_Mat_Link;
    case CKIND_LUIGI:       return CSIcon_Mat_Luigi;
    case CKIND_MARIO:       return CSIcon_Mat_Mario;
    case CKIND_MARTH:       return CSIcon_Mat_Marth;
    case CKIND_MEWTWO:      return CSIcon_Mat_Mewtwo;
    case CKIND_NESS:        return CSIcon_Mat_Ness;
    case CKIND_PEACH:       return CSIcon_Mat_Peach;
    case CKIND_PIKACHU:     return CSIcon_Mat_Pikachu;
    case CKIND_ICECLIMBERS: return CSIcon_Mat_ICs;
    case CKIND_JIGGLYPUFF:  return CSIcon_Mat_Puff;
    case CKIND_SAMUS:       return CSIcon_Mat_Samus;
    case CKIND_YOSHI:       return CSIcon_Mat_Yoshi;
    case CKIND_ZELDA:       return CSIcon_Mat_Zelda;
    case CKIND_SHEIK:       return CSIcon_Mat_Sheik;
    case CKIND_FALCO:       return CSIcon_Mat_Falco;
    case CKIND_YOUNGLINK:   return CSIcon_Mat_YLink;
    case CKIND_DRMARIO:     return CSIcon_Mat_Doc;
    case CKIND_ROY:         return CSIcon_Mat_Roy;
    case CKIND_PICHU:       return CSIcon_Mat_Pichu;
    case CKIND_GANONDORF:   return CSIcon_Mat_Ganon;
    }
    return CSIcon_Mat_Empty;
}

static void CSIcon_SetPos(CSIcon *icon, Vec3 p)
{
    icon->root_jobj->trans = p;
}

static void CSIcon_SetSelectState(CSIcon *icon, CSIcon_SelectState state)
{
    if (icon->state.select_state == state) return;
    CSIcon_SetSelectStateInternal(icon, state);
}

static void CSIcon_SetStockIconVisibility(CSIcon *icon, u8 is_visible)
{
    if (icon->state.is_stock_icon_visible == is_visible) return;
    CSIcon_SetStockIconVisInternal(icon, is_visible);
}

// =========================================================================
// TurnIndicator — animated arrow showing who is picking
// =========================================================================
typedef enum {
    TI_STATIC,     // 0 — resting pose
    TI_ANIM_GRAY,  // 1 — animated, gray
    TI_ANIM_YEL,   // 2 — animated, yellow (active picking)
} TI_DisplayState;

typedef enum {
    TI_DIR_LEFT,
    TI_DIR_RIGHT,
} TI_Direction;

typedef struct TurnIndicator {
    GOBJ *gobj;
    JOBJ *root_jobj;
    JOBJSet *jobj_set;
    TI_DisplayState display_state;
} TurnIndicator;

static void TI_SetDisplayStateInternal(TurnIndicator *ti, TI_DisplayState ds)
{
    JOBJ_RemoveAnimAll(ti->root_jobj);

    HSD_Material *big_mat = ti->root_jobj->child->dobj->mobj->mat;
    HSD_Material *small_mat = ti->root_jobj->child->sibling->dobj->mobj->mat;

    big_mat->diffuse = (GXColor){128, 128, 128, 255};
    small_mat->diffuse = (GXColor){128, 128, 128, 255};

    if (ds == TI_ANIM_YEL) {
        big_mat->diffuse = (GXColor){254, 202, 52, 255};
        small_mat->diffuse = (GXColor){254, 202, 52, 255};
        JOBJ_AddSetAnim(ti->root_jobj, ti->jobj_set, 0);
        JOBJ_ReqAnimAll(ti->root_jobj, 0);
    } else if (ds == TI_ANIM_GRAY) {
        JOBJ_AddSetAnim(ti->root_jobj, ti->jobj_set, 0);
        JOBJ_ReqAnimAll(ti->root_jobj, 0);
    } else {
        // STATIC: snap to frame 0 then stop
        JOBJ_AddSetAnim(ti->root_jobj, ti->jobj_set, 0);
        JOBJ_ReqAnimAll(ti->root_jobj, 0);
        JOBJ_AnimAll(ti->root_jobj);
        JOBJ_RemoveAnimAll(ti->root_jobj);
    }

    ti->display_state = ds;
}

static TurnIndicator *TI_Init(GUI_GameSetup *gui, TI_Direction dir)
{
    TurnIndicator *ti = calloc(sizeof(TurnIndicator));
    ti->jobj_set = gui->jobjs[GUI_JOBJ_TurnIndicator];
    ti->gobj = JOBJ_LoadSet(0, ti->jobj_set, 0, 0, 3, 1, 1, GObj_Anim);
    ti->root_jobj = ti->gobj->hsd_object;
    ti->root_jobj->rot.Z = (dir == TI_DIR_LEFT) ? M_PI : 0;
    TI_SetDisplayStateInternal(ti, TI_STATIC);
    return ti;
}

static void TI_SetDisplayState(TurnIndicator *ti, TI_DisplayState ds)
{
    if (ti->display_state == ds) return;
    TI_SetDisplayStateInternal(ti, ds);
}

static void TI_SetPos(TurnIndicator *ti, Vec3 p)
{
    ti->root_jobj->trans = p;
    JOBJ_SetMtxDirtySub(ti->root_jobj);
}

// =========================================================================
// Character color utilities
// =========================================================================
#define NUM_PLAYABLE_CHARS  26
#define CKIND_RANDOM        26   // Index of Random in char picker grid
#define CPD_LAST_INDEX      26

static u8 GetMaxColors(u8 charId)
{
    switch (charId) {
    case CKIND_FALCON:
    case CKIND_KIRBY:
    case CKIND_YOSHI:
        return 6;
    case CKIND_DK:
    case CKIND_LINK:
    case CKIND_MARIO:
    case CKIND_MARTH:
    case CKIND_PEACH:
    case CKIND_JIGGLYPUFF:
    case CKIND_SAMUS:
    case CKIND_ZELDA:
    case CKIND_SHEIK:
    case CKIND_YOUNGLINK:
    case CKIND_DRMARIO:
    case CKIND_ROY:
    case CKIND_GANONDORF:
        return 5;
    case CKIND_FOX:
    case CKIND_GAW:
    case CKIND_BOWSER:
    case CKIND_LUIGI:
    case CKIND_MEWTWO:
    case CKIND_NESS:
    case CKIND_PIKACHU:
    case CKIND_ICECLIMBERS:
    case CKIND_FALCO:
    case CKIND_PICHU:
        return 4;
    default:
        return 1;
    }
}

static u8 GetNextColor(u8 charId, u8 colorId, int incr)
{
    if (charId >= NUM_PLAYABLE_CHARS) return 0;
    u8 max = GetMaxColors(charId);
    if (incr == 0) return colorId < max ? colorId : 0;
    int next = (int)colorId + incr;
    if (next < 0) next = max - 1;
    if (next >= max) next = 0;
    return (u8)next;
}

// =========================================================================
// CharPickerDialog — 4x7 grid of all 26 characters + Random
// =========================================================================
typedef struct {
    Vec3 pos;
    u8 is_open;
    u8 char_selection_idx;
    u8 char_color_idx;
    u32 open_frame_count;
} CPDState;

typedef struct CharPickerDialog {
    GOBJ *gobj;
    JOBJ *root_jobj;
    JOBJSet *jobj_set;
    CSIcon *char_icons[CPD_LAST_INDEX + 1]; // 27 icons (26 chars + Random)
    CPDState state;
    void (*on_close)(struct CharPickerDialog *, u8);
    u8 (*get_next_color)(u8, u8, int);
} CharPickerDialog;

// Forward declarations
static void CPD_CloseDialog(CharPickerDialog *cpd);

static void CPD_SetPos(CharPickerDialog *cpd, Vec3 pos)
{
    cpd->root_jobj->trans = pos;
    JOBJ_SetMtxDirtySub(cpd->root_jobj);
}

static void CPD_InputsThink(GOBJ *gobj)
{
    CharPickerDialog *cpd = gobj->userdata;
    if (!cpd->state.is_open) return;

    cpd->state.open_frame_count++;
    if (cpd->state.open_frame_count == 1) return; // skip first frame

    u8 port = R13_U8(-0x5108);
    u64 scrollInputs = Pad_GetRapidHeld(port);
    u64 downInputs = Pad_GetDown(port);

    if (downInputs & HSD_BUTTON_A) {
        SFX_PlayCommon(SFX_ACCEPT);
        if (cpd->state.char_selection_idx == CKIND_RANDOM) {
            cpd->state.char_selection_idx = HSD_Randi(CPD_LAST_INDEX);
            cpd->state.char_color_idx = cpd->get_next_color(cpd->state.char_selection_idx, 0, 0);
        }
        CPD_CloseDialog(cpd);
        cpd->on_close(cpd, 1);
        return;
    } else if (downInputs & HSD_BUTTON_B) {
        SFX_PlayCommon(SFX_BACK);
        CPD_CloseDialog(cpd);
        cpd->on_close(cpd, 0);
        return;
    }

    if (downInputs & HSD_BUTTON_X) {
        cpd->state.char_color_idx = cpd->get_next_color(cpd->state.char_selection_idx, cpd->state.char_color_idx, 1);
        SFX_PlayCommon(SFX_NEXT);
    } else if (downInputs & HSD_BUTTON_Y) {
        cpd->state.char_color_idx = cpd->get_next_color(cpd->state.char_selection_idx, cpd->state.char_color_idx, -1);
        SFX_PlayCommon(SFX_NEXT);
    }

    // Grid navigation (4 rows x 7 cols, index 0-26, 26=Random)
    if (scrollInputs & (HSD_BUTTON_RIGHT | HSD_BUTTON_DPAD_RIGHT)) {
        if (cpd->state.char_selection_idx >= CKIND_RANDOM)
            cpd->state.char_selection_idx = CKIND_YOUNGLINK;
        else if ((cpd->state.char_selection_idx + 1) % 7 == 0)
            cpd->state.char_selection_idx -= 6;
        else
            cpd->state.char_selection_idx++;
        cpd->state.char_color_idx = cpd->get_next_color(cpd->state.char_selection_idx, 0, 0);
        SFX_PlayCommon(SFX_NEXT);
    } else if (scrollInputs & (HSD_BUTTON_LEFT | HSD_BUTTON_DPAD_LEFT)) {
        if (cpd->state.char_selection_idx == CKIND_YOUNGLINK)
            cpd->state.char_selection_idx = CKIND_RANDOM;
        else if (cpd->state.char_selection_idx % 7 == 0)
            cpd->state.char_selection_idx += 6;
        else
            cpd->state.char_selection_idx--;
        cpd->state.char_color_idx = cpd->get_next_color(cpd->state.char_selection_idx, 0, 0);
        SFX_PlayCommon(SFX_NEXT);
    } else if (scrollInputs & (HSD_BUTTON_DOWN | HSD_BUTTON_DPAD_DOWN)) {
        if (cpd->state.char_selection_idx % 7 >= 6 && cpd->state.char_selection_idx > CKIND_SHEIK)
            cpd->state.char_selection_idx -= 14;
        else if (cpd->state.char_selection_idx >= CKIND_YOUNGLINK)
            cpd->state.char_selection_idx -= 21;
        else
            cpd->state.char_selection_idx += 7;
        cpd->state.char_color_idx = cpd->get_next_color(cpd->state.char_selection_idx, 0, 0);
        SFX_PlayCommon(SFX_NEXT);
    } else if (scrollInputs & (HSD_BUTTON_UP | HSD_BUTTON_DPAD_UP)) {
        if (cpd->state.char_selection_idx % 7 >= 5 && cpd->state.char_selection_idx <= CKIND_LINK)
            cpd->state.char_selection_idx += 21;
        else if (cpd->state.char_selection_idx <= CKIND_LINK)
            cpd->state.char_selection_idx += 21;
        else
            cpd->state.char_selection_idx -= 7;
        cpd->state.char_color_idx = cpd->get_next_color(cpd->state.char_selection_idx, 0, 0);
        SFX_PlayCommon(SFX_NEXT);
    }

    // Update icon highlight states
    int i;
    for (i = CKIND_FALCON; i <= CKIND_RANDOM; i++) {
        CSIcon_SelectState ss = CSIcon_SS_NotSelected;
        u8 colorId = 0;
        if (i == cpd->state.char_selection_idx) {
            ss = CSIcon_SS_Hover;
            colorId = cpd->state.char_color_idx;
        }
        if (i < CKIND_RANDOM) {
            CSIcon_SetStockIconVisibility(cpd->char_icons[i], i == cpd->state.char_selection_idx);
            StockIcon_SetIcon(cpd->char_icons[i]->stock_icon, i, colorId);
        }
        CSIcon_SetSelectState(cpd->char_icons[i], ss);
    }
}

static CharPickerDialog *CPD_Init(GUI_GameSetup *gui, void *on_close, void *get_next_color)
{
    CharPickerDialog *cpd = calloc(sizeof(CharPickerDialog));

    cpd->jobj_set = gui->jobjs[GUI_JOBJ_CharDialog];
    cpd->gobj = JOBJ_LoadSet(0, cpd->jobj_set, 0, 0, 3, 1, 0, GObj_Anim);
    cpd->root_jobj = cpd->gobj->hsd_object;
    cpd->on_close = on_close;
    cpd->get_next_color = get_next_color;

    // Attach 27 CSIcons to the dialog's joint tree
    JOBJ *cur_joint = cpd->root_jobj->child->child->sibling->child;
    int i;
    for (i = CKIND_FALCON; i <= CKIND_RANDOM; i++) {
        cpd->char_icons[i] = CSIcon_Init(gui);
        int matIdx = i < CKIND_RANDOM ? CSIcon_CharToMat(i) : CSIcon_Mat_Question;
        CSIcon_SetMaterial(cpd->char_icons[i], matIdx);
        JOBJ_AddChild(cur_joint, cpd->char_icons[i]->root_jobj);
        cur_joint = cur_joint->sibling;
    }

    // Input handler GOBJ
    GOBJ *input_gobj = GObj_Create(4, 0, 128);
    GObj_AddUserData(input_gobj, 4, HSD_Free, cpd);
    GObj_AddProc(input_gobj, CPD_InputsThink, 0);

    // Start closed (offscreen)
    cpd->state.pos = (Vec3){0, 0, 0};
    CPD_CloseDialog(cpd);

    return cpd;
}

static void CPD_OpenDialog(CharPickerDialog *cpd, u8 start_char_idx, u8 start_char_color)
{
    cpd->state.is_open = 1;
    cpd->state.char_selection_idx = start_char_idx;
    cpd->state.char_color_idx = start_char_color;
    cpd->state.open_frame_count = 0;
    CPD_SetPos(cpd, cpd->state.pos);
}

static void CPD_CloseDialog(CharPickerDialog *cpd)
{
    cpd->state.is_open = 0;
    CPD_SetPos(cpd, (Vec3){0, 1000, 0});
}

static void CPD_SetDialogPos(CharPickerDialog *cpd, Vec3 pos)
{
    cpd->state.pos = pos;
    if (!cpd->state.is_open) return;
    CPD_SetPos(cpd, pos);
}

// =========================================================================
// Scene constants
// =========================================================================
#define LOBBY_DURATION  1800

// Layout constants (Text coordinate space)
#define HDR_Y         -45.0f
#define MATCH_X       -21.0f
#define MATCH_CX      -7.0f
#define QUEUE_X       9.0f
#define QUEUE_CX      13.0f
#define BODY_TOP      -40.0f

#define SCALE_TITLE   0.07f
#define SCALE_HEADER  0.06f
#define SCALE_BODY    0.05f
#define SCALE_SMALL   0.04f
#define ROW_SPACING   3.0f

// CSIcon positions (3D JOBJ coordinate space)
#define P1_ICON_X     -6.5f
#define P1_ICON_Y     7.5f
#define P2_ICON_X     -6.5f
#define P2_ICON_Y     -3.5f

// TurnIndicator positions (next to CSIcons, pointing inward)
#define P1_TI_X       (P1_ICON_X + 5.0f)
#define P1_TI_Y       P1_ICON_Y
#define P2_TI_X       (P2_ICON_X + 5.0f)
#define P2_TI_Y       P2_ICON_Y

// CharPickerDialog position
#define CPD_POS_X       3.0f
#define CPD_POS_Y       0.0f

// =========================================================================
// Scene state
// =========================================================================
static int frame_count = 0;
static HSD_Archive *gui_archive = 0;
static GUI_GameSetup *gui_assets = 0;
static Text *text = 0;
static u8 *msrb = 0;

// Parsed from MSRB
static u8 local_port;
static u8 active_p1;
static u8 active_p2;
static u8 player_count;
static u8 games_played;
static u8 last_winner;

// Character selection state
static u8 local_char_id = 0;
static u8 local_char_color = 0;
static u8 local_confirmed = 0;
static u8 opp_confirmed = 0;
static u8 opp_char_id = 0;
static u8 opp_char_color = 0;
static u8 is_active_player = 0;
static u8 dialog_open = 0;
static int z_hold_frames = 0;

// EXI buffers
static GpCompleteStepQuery *complete_query = 0;
static GpFetchStepQuery *fetch_query = 0;
static GpFetchStepResponse *fetch_resp = 0;

// Visual components
static CSIcon *p1_icon = 0;
static CSIcon *p2_icon = 0;
static CharPickerDialog *char_picker = 0;
static TurnIndicator *p1_indicator = 0;
static TurnIndicator *p2_indicator = 0;

// Text subtext indices
static int st_title = -1;
static int st_timer = -1;
static int st_p1_label = -1;
static int st_p1_name = -1;
static int st_vs = -1;
static int st_p2_label = -1;
static int st_p2_name = -1;
static int st_status = -1;
static int st_queue_hdr = -1;
static int st_queue[8];

void CObjThink(GOBJ *gobj);

// Colors
static GXColor color_white  = {255, 255, 255, 255};
static GXColor color_yellow = {241, 200, 50, 255};
static GXColor color_green  = {100, 220, 100, 255};
static GXColor color_gray   = {160, 160, 160, 255};

static char *get_name(int port)
{
    if (port > 3) return "---";
    return (char *)&msrb[OFST_P1_NAME + port * MSRB_NAME_SIZE];
}

static char *get_lobby_code(void)
{
    return (char *)&msrb[OFST_ROT_LOBBY_CODE];
}

static void send_selection(void)
{
    complete_query->command = CMD_GP_COMPLETE_STEP;
    complete_query->step_idx = 0;
    complete_query->char_selection = local_char_id;
    complete_query->char_color_selection = local_char_color;
    complete_query->stage_selections[0] = 0;
    complete_query->stage_selections[1] = 0;
    ExiSlippi_Transfer(complete_query, sizeof(GpCompleteStepQuery), EXI_WRITE);
}

static void poll_opponent(void)
{
    fetch_query->command = CMD_GP_FETCH_STEP;
    fetch_query->step_idx = 0;
    ExiSlippi_Transfer(fetch_query, sizeof(GpFetchStepQuery), EXI_WRITE);
    ExiSlippi_Transfer(fetch_resp, sizeof(GpFetchStepResponse), EXI_READ);

    if (fetch_resp->is_found) {
        opp_confirmed = 1;
        opp_char_id = fetch_resp->char_selection;
        opp_char_color = fetch_resp->char_color_selection;
    }
}

static void apply_selections(void)
{
    SetSelectionsQuery *ssq = calloc(sizeof(SetSelectionsQuery));
    ssq->command = CMD_SET_SELECTIONS;
    ssq->team_id = 0;
    ssq->char_id = local_char_id;
    ssq->char_color_id = local_char_color;
    ssq->char_option = 1;
    ssq->stage_id = 0;
    ssq->stage_option = 1;
    ssq->online_mode = 0;
    ExiSlippi_Transfer(ssq, sizeof(SetSelectionsQuery), EXI_WRITE);
}

// Update a CSIcon to show a character + stock icon
static void update_icon(CSIcon *icon, u8 charId, u8 colorId)
{
    CSIcon_SetMaterial(icon, CSIcon_CharToMat(charId));
    CSIcon_SetStockIconVisibility(icon, 1);
    StockIcon_SetIcon(icon->stock_icon, charId, colorId);
}

// =========================================================================
// CharPickerDialog close callback
// =========================================================================
static void on_picker_close(CharPickerDialog *cpd, u8 is_selection)
{
    dialog_open = 0;

    if (!is_selection) return; // B pressed, cancelled

    // Apply the selection from the dialog
    local_char_id = cpd->state.char_selection_idx;
    local_char_color = cpd->state.char_color_idx;

    // Update the local player's icon
    CSIcon *my_icon = (local_port == active_p1) ? p1_icon : p2_icon;
    update_icon(my_icon, local_char_id, local_char_color);
}

// GetNextColor wrapper matching CharPickerDialog callback signature
static u8 get_next_color_cb(u8 charId, u8 colorId, int incr)
{
    return GetNextColor(charId, colorId, incr);
}

// =========================================================================
// minor_load
// =========================================================================
void minor_load(void *data)
{
    int i;
    frame_count = 0;
    local_confirmed = 0;
    opp_confirmed = 0;
    dialog_open = 0;
    z_hold_frames = 0;
    opp_char_id = 0;
    opp_char_color = 0;

    // Fetch MSRB
    msrb = calloc(MSRB_TOTAL_SIZE);
    msrb[0] = 0xB3;
    ExiSlippi_Transfer(msrb, 1, EXI_WRITE);
    ExiSlippi_Transfer(msrb, MSRB_TOTAL_SIZE, EXI_READ);

    local_port   = msrb[OFST_LOCAL_PLAYER_INDEX];

    // Read last-used character from the match block
    local_char_id = msrb[OFST_MATCH_BLOCK + MATCH_CHAR_ID_OFF + local_port * MATCH_PLAYER_STRIDE];
    local_char_color = msrb[OFST_MATCH_BLOCK + MATCH_CHAR_COLOR_OFF + local_port * MATCH_PLAYER_STRIDE];
    if (local_char_id >= NUM_PLAYABLE_CHARS) {
        local_char_id = CKIND_MARTH;
        local_char_color = 0;
    }
    player_count = msrb[OFST_ROT_PLAYER_COUNT];
    active_p1    = msrb[OFST_ROT_ACTIVE_P1];
    active_p2    = msrb[OFST_ROT_ACTIVE_P2];
    games_played = msrb[OFST_ROT_GAMES_PLAYED];
    last_winner  = msrb[OFST_ROT_LAST_WINNER];

    is_active_player = (local_port == active_p1 || local_port == active_p2);

    // EXI buffers
    complete_query = calloc(sizeof(GpCompleteStepQuery));
    fetch_query = calloc(sizeof(GpFetchStepQuery));
    fetch_resp = calloc(sizeof(GpFetchStepResponse));

    // 3D setup from GameSetup_gui.dat
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

    // Background + panels
    JOBJ_LoadSet(0, gui_assets->jobjs[GUI_JOBJ_Background], 0, 0, 3, 1, 1, GObj_Anim);
    GOBJ *panels_gobj = JOBJ_LoadSet(0, gui_assets->jobjs[1], 0, 0, 3, 1, 1, GObj_Anim);
    JOBJ *panels = panels_gobj->hsd_object;
    panels->scale.X = 1.0f;
    panels->scale.Y = 1.9f;
    panels->trans.Y = -2.0f;
    JOBJ_SetMtxDirtySub(panels);

    // --- CSIcon setup ---
    // P1 icon (winner/left player)
    p1_icon = CSIcon_Init(gui_assets);
    CSIcon_SetPos(p1_icon, (Vec3){P1_ICON_X, P1_ICON_Y, 0});
    update_icon(p1_icon, local_char_id, local_char_color);
    if (!is_active_player || local_port != active_p1) {
        // Show question mark for unknown opponent
        CSIcon_SetMaterial(p1_icon, CSIcon_Mat_Question);
        CSIcon_SetStockIconVisibility(p1_icon, 0);
    }

    // P2 icon (challenger/right player)
    p2_icon = CSIcon_Init(gui_assets);
    CSIcon_SetPos(p2_icon, (Vec3){P2_ICON_X, P2_ICON_Y, 0});
    update_icon(p2_icon, local_char_id, local_char_color);
    if (!is_active_player || local_port != active_p2) {
        CSIcon_SetMaterial(p2_icon, CSIcon_Mat_Question);
        CSIcon_SetStockIconVisibility(p2_icon, 0);
    }

    // TurnIndicators (arrows next to CSIcons)
    p1_indicator = TI_Init(gui_assets, TI_DIR_LEFT);
    TI_SetPos(p1_indicator, (Vec3){P1_TI_X, P1_TI_Y, 0});
    p2_indicator = TI_Init(gui_assets, TI_DIR_LEFT);
    TI_SetPos(p2_indicator, (Vec3){P2_TI_X, P2_TI_Y, 0});

    // Start indicators: active picking = yellow animated, spectator = static
    if (is_active_player) {
        TI_SetDisplayState(p1_indicator, TI_ANIM_YEL);
        TI_SetDisplayState(p2_indicator, TI_ANIM_YEL);
    }

    // CharPickerDialog for active players
    if (is_active_player) {
        char_picker = CPD_Init(gui_assets, on_picker_close, get_next_color_cb);
        CPD_SetDialogPos(char_picker, (Vec3){CPD_POS_X, CPD_POS_Y, 0});
    } else {
        char_picker = 0;
    }

    // --- Text setup ---
    text = Text_CreateText(0, 0);
    text->kerning = 1;
    text->align = 0;
    text->trans.Z = 0.0f;
    text->scale.X = 0.01f;
    text->scale.Y = 0.01f;

    // === HEADER ROW ===
    st_title = Text_AddSubtext(text, MATCH_X, HDR_Y, "LOBBY %s", get_lobby_code());
    Text_SetScale(text, st_title, SCALE_TITLE, SCALE_TITLE);
    Text_SetColor(text, st_title, &color_white);

    st_timer = Text_AddSubtext(text, QUEUE_CX, HDR_Y, "0:10");
    Text_SetScale(text, st_timer, SCALE_TITLE, SCALE_TITLE);
    Text_SetColor(text, st_timer, &color_white);

    // === LEFT COLUMN — MATCH ===
    float my = BODY_TOP;

    // Player 1 (winner) — shifted right to center with CHALLENGER
    st_p1_label = Text_AddSubtext(text, MATCH_X + 1.0f, my, "WINNER");
    Text_SetScale(text, st_p1_label, SCALE_SMALL, SCALE_SMALL);
    Text_SetColor(text, st_p1_label, &color_yellow);

    my += 2.0f;
    st_p1_name = Text_AddSubtext(text, MATCH_X + 1.0f, my, "");
    Text_SetScale(text, st_p1_name, SCALE_BODY, SCALE_BODY);
    Text_SetColor(text, st_p1_name, &color_white);

    // (CSIcon replaces the character text line — skip space for it)
    my += 5.5f;

    // VS (centered between player names)
    st_vs = Text_AddSubtext(text, MATCH_X + 2.0f, my - 1.0f, "VS");
    Text_SetScale(text, st_vs, SCALE_TITLE, SCALE_TITLE);
    Text_SetColor(text, st_vs, &color_gray);

    // Player 2 (challenger)
    my += 3.5f;
    st_p2_label = Text_AddSubtext(text, MATCH_X, my, "CHALLENGER");
    Text_SetScale(text, st_p2_label, SCALE_SMALL, SCALE_SMALL);
    Text_SetColor(text, st_p2_label, &color_green);

    my += 2.0f;
    st_p2_name = Text_AddSubtext(text, MATCH_X, my, "");
    Text_SetScale(text, st_p2_name, SCALE_BODY, SCALE_BODY);
    Text_SetColor(text, st_p2_name, &color_white);

    // (CSIcon replaces the character text line — skip space for it)
    my += 7.0f;

    // Status line
    st_status = Text_AddSubtext(text, MATCH_X, my, "");
    Text_SetScale(text, st_status, SCALE_SMALL, SCALE_SMALL);
    Text_SetColor(text, st_status, &color_gray);

    // === RIGHT COLUMN — QUEUE ===
    st_queue_hdr = Text_AddSubtext(text, QUEUE_X, BODY_TOP, "QUEUE");
    Text_SetScale(text, st_queue_hdr, SCALE_HEADER, SCALE_HEADER);
    Text_SetColor(text, st_queue_hdr, &color_white);

    for (i = 0; i < 8; i++) {
        float y = BODY_TOP + 4.0f + i * ROW_SPACING;
        st_queue[i] = Text_AddSubtext(text, QUEUE_X, y, "");
        Text_SetScale(text, st_queue[i], SCALE_SMALL, SCALE_SMALL);
        Text_SetColor(text, st_queue[i], &color_gray);
    }

    // === Populate initial content ===
    Text_SetText(text, st_p1_name, get_name(active_p1));
    Text_SetText(text, st_p2_name, get_name(active_p2));

    if (is_active_player) {
        Text_SetText(text, st_status, "A: pick character  START: confirm");
    } else {
        Text_SetText(text, st_status, "Spectating...");
    }

    // Build queue list
    int slot = 0;
    for (i = 0; i < 8 && slot < 8; i++) {
        u8 port = msrb[OFST_ROT_QUEUE_START + i];
        if (port == 0xFF) break;
        Text_SetText(text, st_queue[slot], "%d. %s", slot + 1, get_name(port));
        if (port == local_port)
            Text_SetColor(text, st_queue[slot], &color_white);
        slot++;
    }

    if (slot == 0) {
        Text_SetText(text, st_queue[0], "No one waiting");
    }
}

// =========================================================================
// CObjThink
// =========================================================================
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

// =========================================================================
// minor_think
// =========================================================================
void minor_think(void)
{
    frame_count++;

    // Timer
    int secs_left = (LOBBY_DURATION - frame_count) / 60;
    if (secs_left < 0) secs_left = 0;
    Text_SetText(text, st_timer, "%d:%02d", secs_left / 60, secs_left % 60);

    // Z-hold disconnect (any player can disconnect)
    if (local_port < 4) {
        u64 held = Pad_GetHeld(local_port);
        if (held & HSD_TRIGGER_Z) {
            z_hold_frames++;
            int remain = Z_HOLD_DISCONNECT_FRAMES - z_hold_frames;
            if (remain > 0) {
                Text_SetText(text, st_status, "Hold Z to leave... %d", (remain / 60) + 1);
            }
            if (z_hold_frames >= Z_HOLD_DISCONNECT_FRAMES) {
                // Send cleanup command to disconnect
                u8 *cmd = calloc(1);
                *cmd = CMD_CLEANUP_CONNECTIONS;
                ExiSlippi_Transfer(cmd, 1, EXI_WRITE);
                HSD_Free(cmd);
                Scene_ExitMinor();
                return;
            }
        } else {
            if (z_hold_frames > 0) {
                z_hold_frames = 0;
                // Restore status text
                if (is_active_player) {
                    if (local_confirmed)
                        Text_SetText(text, st_status, "Confirmed! Waiting...");
                    else
                        Text_SetText(text, st_status, "A: pick character  START: confirm");
                } else {
                    Text_SetText(text, st_status, "Spectating...");
                }
            }
        }
    }

    // Handle input for active players only
    if (is_active_player && !local_confirmed && local_port < 4) {
        u64 down = Pad_GetDown(local_port);

        // Don't handle scene-level input while dialog is open
        if (!dialog_open) {
            // A: open character picker dialog
            if (down & HSD_BUTTON_A) {
                dialog_open = 1;
                CPD_OpenDialog(char_picker, local_char_id, local_char_color);
            }

            // START: confirm current selection
            if (down & HSD_BUTTON_START) {
                local_confirmed = 1;
                send_selection();
                SFX_PlayCommon(SFX_ACCEPT);

                // Set confirmed icon to blink, indicator to static
                CSIcon *my_icon = (local_port == active_p1) ? p1_icon : p2_icon;
                TurnIndicator *my_ti = (local_port == active_p1) ? p1_indicator : p2_indicator;
                CSIcon_SetSelectState(my_icon, CSIcon_SS_Blink);
                TI_SetDisplayState(my_ti, TI_STATIC);

                Text_SetText(text, st_status, "Confirmed! Waiting...");
            }
        }
    }

    // Poll for opponent's selection every frame
    if (is_active_player && !opp_confirmed) {
        poll_opponent();
        if (opp_confirmed) {
            // Update opponent's CSIcon and indicator
            CSIcon *opp_icon = (local_port == active_p1) ? p2_icon : p1_icon;
            TurnIndicator *opp_ti = (local_port == active_p1) ? p2_indicator : p1_indicator;
            update_icon(opp_icon, opp_char_id, opp_char_color);
            CSIcon_SetSelectState(opp_icon, CSIcon_SS_Blink);
            TI_SetDisplayState(opp_ti, TI_STATIC);
        }
    }

    // Both confirmed or timer expired: apply and exit
    if (frame_count >= LOBBY_DURATION || (local_confirmed && opp_confirmed)) {
        if (is_active_player) {
            apply_selections();
        }
        Scene_ExitMinor();
    }
}

// =========================================================================
// minor_exit
// =========================================================================
void minor_exit(void *data)
{
    if (text) {
        Text_Destroy(text);
        text = 0;
    }
    if (complete_query) { HSD_Free(complete_query); complete_query = 0; }
    if (fetch_query) { HSD_Free(fetch_query); fetch_query = 0; }
    if (fetch_resp) { HSD_Free(fetch_resp); fetch_resp = 0; }

    // CSIcon and CharPickerDialog don't implement Free — they live on the
    // scene heap and are cleaned up when the archive is freed.
    p1_icon = 0;
    p2_icon = 0;
    char_picker = 0;
    p1_indicator = 0;
    p2_indicator = 0;
    dialog_open = 0;

    gui_assets = 0;
    if (gui_archive) {
        Archive_Free(gui_archive);
        gui_archive = 0;
    }
    if (msrb) {
        HSD_Free(msrb);
        msrb = 0;
    }
    frame_count = 0;
}
