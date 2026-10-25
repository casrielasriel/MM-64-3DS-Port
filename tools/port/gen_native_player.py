#!/usr/bin/env python3

import argparse
from pathlib import Path

from gen_native_audio import replace


def adapt(path):
    source = path.read_text()
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
    return '#include "port_minimap.h"\n\n' + source


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("source", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    args.output.write_text(adapt(args.source))


if __name__ == "__main__":
    main()
