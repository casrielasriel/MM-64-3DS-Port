#ifndef PORT_AUDIO_H
#define PORT_AUDIO_H

#include "global.h"

#undef AUDIO_RELOCATED_ADDRESS_START
#define AUDIO_RELOCATED_ADDRESS_START 0x00100000u

void PortAudio_NormalizeFont(s32 fontId, void* data);
s32 PortAudio_IsSequenceData(const void* data);
void PortAudio_CopyFilter(s16* dest, const s16* source);
void PortAudio_LoadFilter(s16* dest, s32 lowPass, s32 highPass);
void Port3ds_PumpAudio(void);

static inline u16 PortAudio_ReadU16(const void* data) {
    const u8* bytes = data;
    return ((u16)bytes[0] << 8) | bytes[1];
}

static inline StereoData PortAudio_DecodeStereo(u8 bits) {
    StereoData stereo;
    stereo.asByte = 0;
    stereo.type = (bits >> 4) & 3;
    stereo.strongRight = (bits >> 3) & 1;
    stereo.strongLeft = (bits >> 2) & 1;
    stereo.strongReverbRight = (bits >> 1) & 1;
    stereo.strongReverbLeft = bits & 1;
    return stereo;
}

static inline u16 PortAudio_ReverseFlags(u16 flags) {
    flags = ((flags & 0x5555) << 1) | ((flags >> 1) & 0x5555);
    flags = ((flags & 0x3333) << 2) | ((flags >> 2) & 0x3333);
    flags = ((flags & 0x0F0F) << 4) | ((flags >> 4) & 0x0F0F);
    return (flags << 8) | (flags >> 8);
}

#endif
