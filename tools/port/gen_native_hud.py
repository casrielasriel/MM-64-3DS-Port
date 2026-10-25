#!/usr/bin/env python3

import argparse
from pathlib import Path

from gen_native_audio import replace


def replace_function(source, start, end, old, new):
    first = source.index(start)
    last = source.index(end, first)
    return source[:first] + replace(source[first:last], old, new) + source[last:]


def adapt(path):
    source = path.read_text()
    if path.name == "z_parameter.c":
        source = replace(source, "void Interface_Draw(PlayState* play) {",
                         "void Interface_Draw(PlayState* play) {\n    PortHud_Update(play);")
        source = replace(source, "        LifeMeter_Draw(play);",
                         "        Gfx* portHudStart = OVERLAY_DISP;\n        LifeMeter_Draw(play);")
        source = replace(source, "        Magic_DrawMeter(play);", """        Magic_DrawMeter(play);
        if (!gPortHudTop) {
            OVERLAY_DISP = portHudStart;
        }""")
        source = replace(source, "        Interface_DrawItemButtons(play);",
                         "        portHudStart = OVERLAY_DISP;\n        Interface_DrawItemButtons(play);")
        source = replace(source, "        Interface_DrawAButton(play);", """        Interface_DrawAButton(play);
        if (!gPortHudTop) {
            OVERLAY_DISP = portHudStart;
        }""")
        source = replace(source, "                Interface_DrawClock(play);", """                portHudStart = OVERLAY_DISP;
                Interface_DrawClock(play);
                if (!gPortHudTop) {
                    OVERLAY_DISP = portHudStart;
                }""")
    elif path.name == "z_kaleido_setup.c":
        source = replace(source, "            pauseCtx->itemDescriptionOn = false;", """            if (gPortTouchPage >= PAUSE_ITEM && gPortTouchPage <= PAUSE_MASK) {
                pauseCtx->pageIndex = gPortTouchPage;
                gPortTouchPage = -1;
            }
            pauseCtx->itemDescriptionOn = false;""")
    elif path.name == "z_map_exp.c":
        source = replace(source, """    MapDisp_DrawMinimap(play, sPlayerInitPosX, sPlayerInitPosZ, sPlayerInitDir);""", """    Gfx* start = play->state.gfxCtx->overlay.p;
    PortMap_Begin(play->sceneId);
    MapDisp_DrawMinimap(play, sPlayerInitPosX, sPlayerInitPosZ, sPlayerInitDir);
    if (gPortMinimapOnBottom) {
        play->state.gfxCtx->overlay.p = start;
    }""")
    elif path.name == "z_map_disp.c":
        source = replace_function(source, "void MapDisp_DrawMinimapRoom(", "\ns32 MapDisp_IsDataRotated(",
                                  "    MapData_GetDrawType(mapDataRoom->mapId, &drawType);", """    MapData_GetDrawType(mapDataRoom->mapId, &drawType);
    if (gPortMinimap.numLayers < PORT_MINIMAP_MAX_LAYERS) {
        PortMinimapLayer* layer = &gPortMinimap.layers[gPortMinimap.numLayers++];
        layer->tex = texture;
        layer->x = x;
        layer->y = y;
        layer->w = texWidth;
        layer->h = texHeight;
        layer->fmt = drawType == MAPDATA_DRAW_1 ? PORT_MINIMAP_IA4 :
                     drawType == MAPDATA_DRAW_3 ? PORT_MINIMAP_I4_ALPHA : PORT_MINIMAP_I4;
        layer->flipX = (mapDataRoom->flags & MAP_DATA_ROOM_FLIP_X) != 0;
        layer->flipY = (mapDataRoom->flags & MAP_DATA_ROOM_FLIP_Y) == 0;
        layer->byteXor = MapData_MID_GetType(mapDataRoom->mapId) == MAPDATA_MID_GAMEPLAY_DANGEON_KEEP ? 7 : 0;
        layer->r = color.r;
        layer->g = color.g;
        layer->b = color.b;
        layer->a = play->interfaceCtx.minimapAlpha * intensity * color.a / 255.0f;
    }""")
        start = "void MapDisp_Minimap_DrawActorIcon("
        end = "\nvoid MapDisp_Minimap_DrawActors("
        source = replace_function(source, start, end, "            s16 compassRot;", """            s16 compassRot;
            if (actor == &player->actor) {
                gPortMinimap.compass = 1;
                gPortMinimap.playerX = posX;
                gPortMinimap.playerY = posY;
                gPortMinimap.playerYaw = actor->focus.rot.y + (MapDisp_IsDataRotated(play) ? 0x8000 : 0);
            } else {
                PortMap_AddMark(1, posX, posY, 0, 255, 0);
            }""")
        source = replace_function(source, start, end,
                                  "            gDPLoadTextureBlock_Runtime(OVERLAY_DISP++, gMapChestIconTex,", """            PortMap_AddMark(0, posX, posY, 255, 255, 255);
            gDPLoadTextureBlock_Runtime(OVERLAY_DISP++, gMapChestIconTex,""")
        source = replace_function(source, start, end,
                                  "            if (actor->flags & ACTOR_FLAG_MINIMAP_ICON_ENABLED) {", """            if (actor->flags & ACTOR_FLAG_MINIMAP_ICON_ENABLED) {
                PortMap_AddMark(1, posX, posY, sMinimapActorCategoryColors[actor->category].r,
                                sMinimapActorCategoryColors[actor->category].g,
                                sMinimapActorCategoryColors[actor->category].b);""")
        source = replace_function(source, "void MapDisp_Minimap_DrawDoorActor(", "\nvoid MapDisp_Minimap_DrawDoorActors(",
                                  "            if ((actor->category == ACTORCAT_DOOR) && MapDisp_CanDrawDoors(play)) {", """            if ((actor->category == ACTORCAT_DOOR) && MapDisp_CanDrawDoors(play)) {
                if (yDistAlpha > 0.0f && sMapDisp.swapAnimTimer < 20) {
                    PortMap_AddMark(2, posX, posY, 255, 255, 255);
                }""")
        source = replace_function(source, "void MapDisp_Minimap_DrawRedCompassIcon(", "\ns32 MapDisp_IsLocationRomaniRanchAltScene(",
                                  "        OPEN_DISPS(play->state.gfxCtx);", """        gPortMinimap.hasStart = 1;
        gPortMinimap.startX = posX;
        gPortMinimap.startY = posY;
        gPortMinimap.startYaw = 0x7FFF - rot * 1024 + (MapDisp_IsDataRotated(play) ? 0x8000 : 0);
        OPEN_DISPS(play->state.gfxCtx);""")
    else:
        raise ValueError(f"unsupported input: {path}")
    return '#include "port_minimap.h"\n\n' + source


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("source", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    args.output.write_text(adapt(args.source))


if __name__ == "__main__":
    main()
