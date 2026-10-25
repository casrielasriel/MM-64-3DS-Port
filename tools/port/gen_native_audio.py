#!/usr/bin/env python3
"""Generate ARM audio compatibility sources without modifying the decompilation."""

import argparse
from pathlib import Path


def replace(source, old, new, count=1):
    if source.count(old) != count:
        raise ValueError(f"expected {count} occurrences of {old!r}")
    return source.replace(old, new)


def adapt(path):
    source = path.read_text()
    if path.name == "load.c":
        signature = "void AudioLoad_RelocateFont(s32 fontId, SoundFontData* fontDataStartAddr, SampleBankRelocInfo* sampleBankReloc) {"
        source = replace(source, signature, signature + "\n    PortAudio_NormalizeFont(fontId, fontDataStartAddr);")
        source = replace(source, "for (j = sizeof(gAudioCtx); j >= 0; j--)",
                         "for (j = sizeof(gAudioCtx) - 1; j >= 0; j--)")
    elif path.name == "thread.c":
        source = replace(source, "cmd->opArgs = opArgs;", """cmd->op = opArgs >> 24;
    cmd->arg0 = opArgs >> 16;
    cmd->arg1 = opArgs >> 8;
    cmd->arg2 = opArgs;""")
        source = replace(source, "u32 uData = data << 0x18;", "u32 uData = (u8)data;")
        source = replace(source, "u32 uData = data << 0x10;", "u32 uData = data;")
        source = replace(source, "channel->stereoData.asByte = cmd->asUbyte;",
                         "channel->stereoData = PortAudio_DecodeStereo(cmd->asUbyte);")
        signature = "s32 func_80193C5C(void) {"
        source = replace(source, signature, signature + """
    if (gAudioHeapResetState != AUDIO_HEAP_RESET_STATE_NONE && MQ_IS_EMPTY(gAudioCtx.audioResetQueueP)) {
        Port3ds_PumpAudio();
    }""")
    elif path.name == "seqplayer.c":
        source = replace(source,
                         "*(u16*)(seqPlayer->seqData + (u32)(cmdArgU16 + scriptState->value * 2))",
                         "PortAudio_ReadU16(seqPlayer->seqData + (u32)(cmdArgU16 + scriptState->value * 2))")
        source = replace(source, "((u16*)(channel->dynTable))[scriptState->value]",
                         "PortAudio_ReadU16((u8*)channel->dynTable + scriptState->value * 2)")
        source = replace(source, "temp = *new_var;", "temp = PortAudio_ReadU16(new_var);")
        source = replace(source, "layer->stereoData.asByte = AudioScript_ScriptReadU8(state);",
                         "layer->stereoData = PortAudio_DecodeStereo(AudioScript_ScriptReadU8(state));")
        source = replace(source, "channel->stereoData.asByte = cmd & 0x7F;",
                         "channel->stereoData = PortAudio_DecodeStereo(cmd & 0x7F);")
        source = replace(source, "layer->unk_0A.asByte &= (cmdArg16 ^ 0xFFFF);",
                         "layer->unk_0A.asByte &= (PortAudio_ReverseFlags(cmdArg16) ^ 0xFFFF);")
        source = replace(source, "AudioHeap_LoadFilter(channel->filter, lowBits, cmd);",
                         "PortAudio_LoadFilter(channel->filter, lowBits, cmd);")
    elif path.name == "playback.c":
        source = replace(source, "(u32)playbackState->parentLayer < 0x7FFFFFFF",
                         "(u32)playbackState->parentLayer < AUDIO_RELOCATED_ADDRESS_START")
        source = replace(source, """for (i = 0; i < 8; i++) {
                    attrs->filterBuf[i] = attrs->filter[i];
                }""", "PortAudio_CopyFilter(attrs->filterBuf, attrs->filter);")
    elif path.name == "effects.c":
        source = replace(source, "adsr->envelope = envelope;",
                         "adsr->envelope = envelope;\n    adsr->pad18[0] = PortAudio_IsSequenceData(envelope);")
        for field in ("delay", "arg"):
            old = f"adsr->envelope[adsr->envelopeIndex].{field}"
            source = replace(source, old,
                             f"(adsr->pad18[0] ? (s16)PortAudio_ReadU16(&{old}) : {old})",
                             count=1 if field == "delay" else 2)
    elif path.name == "code_8019AF00.c":
        source = replace(source, "channel->stereoData.asByte = sSfxChannelState[seqScriptValIn].stereoBits;",
                         "channel->stereoData = PortAudio_DecodeStereo(sSfxChannelState[seqScriptValIn].stereoBits);")
    else:
        raise ValueError(f"unsupported input: {path}")
    return '#include "port_audio.h"\n\n' + source


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    args.output.write_text(adapt(args.source))


if __name__ == "__main__":
    main()
