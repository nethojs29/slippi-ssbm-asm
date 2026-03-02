// Rotation Lobby Minor Scene — MINIMAL TEST
// Black screen + 3 second timer, no archive loads.
// Tests that major→minor flow works before adding UI.

#include "RotationLobby.h"

static int frame_count = 0;

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

void minor_load(void *load_data)
{
    frame_count = 0;

    // Minimal camera — no archive, no text
    GOBJ *cam_gobj = GObj_Create(2, 3, 128);
    COBJ *cam_cobj = COBJ_Alloc();
    GObj_AddObject(cam_gobj, 1, cam_cobj);
    GOBJ_InitCamera(cam_gobj, CObjThink, 0);
    CObj_SetOrtho(cam_cobj, 0.0f, 480.0f, 0.0f, 640.0f);
    CObj_SetViewport(cam_cobj, 0.0f, 640.0f, 0.0f, 480.0f);
    CObj_SetScissor(cam_cobj, 0, 480, 0, 640);
    cam_gobj->cobj_links = (1 << 0) | (1 << 1) | (1 << 2) | (1 << 3) | (1 << 4);
}

void minor_think(void)
{
    frame_count++;
    if (frame_count >= 180)  // 3 seconds
        Scene_ExitMinor();
}

void minor_exit(void *unload_data)
{
}
