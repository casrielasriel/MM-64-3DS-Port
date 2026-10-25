#!/usr/bin/env python3

import argparse
from pathlib import Path

from gen_native_audio import replace


def adapt(path):
    source = path.read_text()
    start = source.index("s32 func_80847190(PlayState* play, Player* this, s32 arg2) {")
    end = source.index("\nvoid func_8084748C(", start)
    original = source[start:end]
    function = replace(original, "    s16 var_s0;", """    s16 var_s0;
    f32 gyroPitch = 0.0f;
    f32 gyroYaw = 0.0f;
    s32 gyroActive = (this == GET_PLAYER(play)) && Port3ds_ReadGyro(
        (this->unk_AA5 == PLAYER_UNKAA5_1) &&
        (play->activeCamId == CAM_ID_MAIN) &&
        (Play_GetCamera(play, CAM_ID_MAIN)->mode == CAM_MODE_FIRSTPERSON) &&
        (play->pauseCtx.state == PAUSE_STATE_OFF) && (play->csCtx.state == CS_STATE_IDLE) &&
        (play->msgCtx.msgMode == MSGMODE_NONE) && (this->csAction == PLAYER_CSACTION_NONE),
        &gyroPitch, &gyroYaw);""")
    function = replace(function, "!func_8082EF20(this) && !arg2)",
                       "!func_8082EF20(this) && !arg2 && !gyroActive)")
    function = replace(function, "this->actor.focus.rot.x += temp3;",
                       "this->actor.focus.rot.x = CLAMP(this->actor.focus.rot.x + temp3 + (s32)gyroPitch, -0x36B0, 0x36B0);")
    function = replace(function, """        var_s0 += temp3;

        this->actor.focus.rot.y = CLAMP(var_s0, -0x4AAA, 0x4AAA) + this->actor.shape.rot.y;""",
                       """        this->actor.focus.rot.y =
            CLAMP(var_s0 + temp3 + (s32)gyroYaw, -0x4AAA, 0x4AAA) + this->actor.shape.rot.y;""")
    source = source[:start] + function + source[end:]
    source = replace(source, """    if (func_801240DC(this)) {
        return;
    }

    if (this->transformation == PLAYER_FORM_HUMAN) {""", """    if (func_801240DC(this)) {
        return;
    }

    if (this == GET_PLAYER(play) && gPortTouchOcarina > 0) {
        gPortTouchOcarina = 0;
        if (INV_CONTENT(ITEM_OCARINA_OF_TIME) == ITEM_OCARINA_OF_TIME &&
            this->transformation != PLAYER_FORM_FIERCE_DEITY &&
            !(this->stateFlags1 & PLAYER_STATE1_800000) && !IS_PAUSED(&play->pauseCtx) &&
            !Play_InCsMode(play) && play->msgCtx.msgMode == MSGMODE_NONE &&
            play->transitionTrigger == TRANS_TRIGGER_OFF && play->transitionMode == TRANS_MODE_OFF &&
            !play->actorCtx.isOverrideInputOn && play->bButtonAmmoPlusOne < 2 &&
            play->interfaceCtx.minigameState == MINIGAME_STATE_NONE &&
            play->interfaceCtx.restrictions.tradeItems == 0) {
            this->heldItemButton = EQUIP_SLOT_C_LEFT;
            Player_UseItem(play, this, ITEM_OCARINA_OF_TIME);
            return;
        }
    }

    if (this->transformation == PLAYER_FORM_HUMAN) {""")
    return '#include "port_input.h"\n#include "port_minimap.h"\n\n' + source


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("source", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    args.output.write_text(adapt(args.source))


if __name__ == "__main__":
    main()
