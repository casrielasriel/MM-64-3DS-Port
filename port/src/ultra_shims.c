#include <string.h>
/*
 * ultra_shims.c — libultra replacement layer for the OoT port.
 * Single-threaded synchronous model: the platform frame loop owns execution;
 * N64 threads are recorded but never scheduled. Message queues are real.
 */
#include "ultra64.h"
/* freestanding bring-up: libc via implicit decls (32-bit: int==ptr) */
extern int fprintf();
extern void* stderr;
struct PortTimespec { long tv_sec; long tv_nsec; };
extern int clock_gettime(int clk, struct PortTimespec* ts);
#define PORT_CLOCK_MONOTONIC 1
extern void* memset();

/* ------------------------------------------------------------------ */
/* Message queues — faithful ring-buffer semantics, minus blocking.    */
/* In a single-threaded world a blocking recv on an empty queue would  */
/* deadlock; we log it so bring-up shows us each spot that needs the   */
/* frame loop to feed it. */

void osCreateMesgQueue(OSMesgQueue* mq, OSMesg* msg, s32 count) {
    mq->validCount = 0;
    mq->first = 0;
    mq->msgCount = count;
    mq->msg = msg;
}

s32 osSendMesg(OSMesgQueue* mq, OSMesg msg, s32 flag) {
    if (mq == NULL) { return -1; }
    if (mq->validCount >= mq->msgCount) {
        (void)flag;
        return -1; /* BLOCK defused: single-threaded bring-up */
    }
    mq->msg[(mq->first + mq->validCount) % mq->msgCount] = msg;
    mq->validCount++;
    return 0;
}

s32 osJamMesg(OSMesgQueue* mq, OSMesg msg, s32 flag) {
    if (mq == NULL) { return -1; }
    if (mq->validCount >= mq->msgCount) {
        (void)flag;
        return -1; /* BLOCK defused: single-threaded bring-up */
    }
    mq->first = (mq->first + mq->msgCount - 1) % mq->msgCount;
    mq->msg[mq->first] = msg;
    mq->validCount++;
    return 0;
}

s32 osRecvMesg(OSMesgQueue* mq, OSMesg* msg, s32 flag) {
    if (mq == NULL) { return -1; }
    if (mq->validCount == 0) {
        (void)flag;
        return -1; /* BLOCK defused: single-threaded bring-up */
    }
    if (msg != NULL) {
        *msg = mq->msg[mq->first];
    }
    mq->first = (mq->first + 1) % mq->msgCount;
    mq->validCount--;
    return 0;
}

/* ------------------------------------------------------------------ */
/* Threads — recorded, never run. The frame loop calls what matters.   */

static OSThread* sThreads[32];
static int sThreadCount = 0;

void osCreateThread(OSThread* t, OSId id, void* entry, void* arg, void* sp, OSPri pri) { /* (MM prototype) */
    (void)sp;
    t->id = id;
    t->priority = pri;
    if (sThreadCount < 32) {
        sThreads[sThreadCount++] = t;
    }
    fprintf(stderr, "[shim] thread %d created (entry %p) — not scheduled\n", (int)id, (void*)entry);
}

void osStartThread(OSThread* t) { (void)t; }
void osStopThread(OSThread* t) { (void)t; }
void osDestroyThread(OSThread* t) { (void)t; }
void osYieldThread(void) {}
OSId osGetThreadId(OSThread* t) { return (t != NULL) ? t->id : 0; }
OSPri osGetThreadPri(OSThread* t) { return (t != NULL) ? t->priority : 0; }
void osSetThreadPri(OSThread* t, OSPri pri) { if (t != NULL) { t->priority = pri; } }

/* ------------------------------------------------------------------ */
/* Events, interrupts, timers                                          */

static OSMesgQueue* sEventQueues[OS_NUM_EVENTS];
static OSMesg sEventMesgs[OS_NUM_EVENTS];

void osSetEventMesg(OSEvent e, OSMesgQueue* mq, OSMesg msg) {
    if (e < OS_NUM_EVENTS) {
        sEventQueues[e] = mq;
        sEventMesgs[e] = msg;
    }
}

/* The platform loop calls this to deliver VI retrace etc. */
void PortShim_RaiseEvent(OSEvent e) {
    if (e < OS_NUM_EVENTS && sEventQueues[e] != NULL) {
        osSendMesg(sEventQueues[e], sEventMesgs[e], OS_MESG_NOBLOCK);
    }
}

OSIntMask osSetIntMask(OSIntMask mask) { (void)mask; return OS_IM_ALL; }
u32 osGetIntMask(void) { return OS_IM_ALL; }

#ifdef __3DS__
extern u64 svcGetSystemTick(void);
/* OS_CLOCK_RATE ticks (46.875 MHz counter on N64) from the 268.111856 MHz system tick: tick * 46875000 / 268111856
 * as a 32.32 fixed-point product (clock_gettime's 64-bit software divisions were measurable on the Old 3DS) */
static u64 TicksNow(void) {
    const u64 k = 750905219ULL; /* 46875000 / 268111856 * 2^32 */
    u64 t = svcGetSystemTick();
    return (t >> 32) * k + (((t & 0xFFFFFFFFULL) * k) >> 32);
}
#else
static u64 TicksNow(void) {
    struct PortTimespec ts;
    clock_gettime(PORT_CLOCK_MONOTONIC, &ts);
    /* OS_CLOCK_RATE ticks (46.875 MHz counter on N64) */
    return (u64)ts.tv_sec * 46875000ULL + (u64)ts.tv_nsec * 46875ULL / 1000000ULL;
}
#endif

OSTime osGetTime(void) { return TicksNow(); }
void osSetTime(OSTime t) { (void)t; }
u32 osGetCount(void) { return (u32)TicksNow(); }

int osSetTimer(OSTimer* timer, OSTime countdown, OSTime interval, OSMesgQueue* mq, OSMesg msg) {
    (void)timer; (void)countdown; (void)interval; (void)mq; (void)msg;
    return 0; /* frame loop will deliver time-based messages */
}
int osStopTimer(OSTimer* timer) { (void)timer; return 0; }

/* ------------------------------------------------------------------ */
/* Cache / TLB / misc CPU — meaningless on the host                    */

void osInvalDCache(void* addr, size_t size) { (void)addr; (void)size; }
void osInvalICache(void* addr, size_t size) { (void)addr; (void)size; }
void osWritebackDCache(void* addr, s32 size) { (void)addr; (void)size; }
void osWritebackDCacheAll(void) {}
u32 osVirtualToPhysical(void* addr) { return (u32)(uintptr_t)addr; }
void osMapTLBRdb(void) {}
void osUnmapTLBAll(void) {}
s32 osAfterPreNMI(void) { return 0; }
void __osInitialize_common(void) {} /* (MM: osInitialize() is a macro for it) */
u32 osGetMemSize(void) { return 8 * 1024 * 1024; }

/* ------------------------------------------------------------------ */
/* VI — frame presentation is the platform layer's job                 */

static void* sNextFb = NULL;
static void* sCurrentFb = NULL;

void osViSetMode(OSViMode* mode) { (void)mode; }
void osViSetSpecialFeatures(u32 func) { (void)func; }
void osViSetXScale(f32 v) { (void)v; }
void osViSetYScale(f32 v) { (void)v; }
void osViExtendVStart(u32 v) { (void)v; }
void osViBlack(u8 active) { (void)active; }
static u32 sSwapCount = 0;
void osViSwapBuffer(void* fb) {
    sNextFb = fb;
    sSwapCount++;
    if ((sSwapCount % 60) == 1) {
        fprintf(stderr, "[vi] frame %d (fb %p)\n", sSwapCount, fb);
    }
}
void* osViGetNextFramebuffer(void) { return sNextFb; }
void* osViGetCurrentFramebuffer(void) { return (sCurrentFb != NULL) ? sCurrentFb : sNextFb; }
void osViSetEvent(OSMesgQueue* mq, OSMesg msg, u32 retraceCount) {
    (void)retraceCount;
    osSetEventMesg(OS_EVENT_VI, mq, msg);
}
void osCreateViManager(OSPri pri) { (void)pri; }

/* ------------------------------------------------------------------ */
/* Controller — one native controller on port 1                       */

s32 osContInit(OSMesgQueue* mq, u8* bitpattern, OSContStatus* status) {
    (void)mq;
    *bitpattern = 1; /* controller 1 present */
    osContGetQuery(status);
    return 0;
}
s32 osContStartReadData(OSMesgQueue* mq) { PortShim_RaiseEvent(OS_EVENT_SI); (void)mq; return 0; }

/* keyboard -> pad, implemented in pc_gfx.c */
extern unsigned short PortInput_GetPad(signed char* outX, signed char* outY);

void osContGetReadData(OSContPad* pad) {
    signed char sx = 0, sy = 0;
    s32 i;
    memset(pad, 0, 4 * sizeof(OSContPad));
    pad[0].button = PortInput_GetPad(&sx, &sy);
    pad[0].stick_x = sx;
    pad[0].stick_y = sy;
    for (i = 1; i < MAXCONTROLLERS; i++) {
        pad[i].errno = CHNL_ERR_NORESP >> 4;
    }
}
s32 osContStartQuery(OSMesgQueue* mq) { (void)mq; return 0; }
void osContGetQuery(OSContStatus* status) {
    s32 i;
    memset(status, 0, 4 * sizeof(OSContStatus));
    status[0].type = CONT_TYPE_NORMAL;
    for (i = 1; i < MAXCONTROLLERS; i++) {
        status[i].errno = CHNL_ERR_NORESP >> 4;
    }
}
s32 osContSetCh(u8 ch) { (void)ch; return 0; }

/* Pump the game's PadMgr from live 3DS buttons once per frame. Normally a
 * dedicated controller thread runs PadMgr_HandleRetrace on each VI retrace, but
 * in the synchronous model that thread never executes, so gPadMgr.inputs[] stays
 * zero and the game ignores input. MM also requires UpdateConnections before
 * UpdateInputs: an unregistered controller's readings are discarded.
 * Called from PortGfx_RunTask (3ds_main.c) after the hardware poll. */
#include "padmgr.h"
extern void PadMgr_UpdateConnections(void); /* global in padmgr.c, not in the header */
extern void PadMgr_UpdateInputs(void);
void Port3ds_PumpInput(void) {
    osContGetReadData(gPadMgr.pads); /* pads[0] <- live 3DS buttons via the shim above */
    osContGetQuery(gPadMgr.padStatus);
    PadMgr_UpdateConnections();
    PadMgr_UpdateInputs(); /* fills inputs[] incl. press/release edges */
}

/* Pump the audio driver once per frame. AudioMgr_ThreadEntry never runs
 * (osStartThread is a no-op), so AudioThread_Update is never called and no audio
 * RSP task is ever built or dispatched. Drive the per-retrace handler directly:
 * it forwards the previous frame's Acmd task to the scheduler (-> the C audio
 * microcode in audio_microcode.c) and builds the next one. Mirrors the PadMgr
 * pattern above. Called from PortGfx_RunTask (3ds_main.c). */
#include "audiomgr.h"
#include "regs.h"
#include "global.h" /* PORT: was OoT audio.h */
extern AudioMgr sAudioMgr;                         /* global instance in main.c */
static void Port3ds_PumpAudio_impl(void);
void Port3ds_PumpAudio(void) {
    extern int gPortMmAudio;
    if (!gPortMmAudio) {
        return;
    }
    {
    extern volatile unsigned char gPortProf;
    extern void Port3ds_ScanBetweenUpdates(void);
    unsigned char prev = gPortProf;
    Port3ds_ScanBetweenUpdates(); /* (3ds_main.c: taps between updates) */
    gPortProf = 10; /* PROF_AUDIO (port_prof.h) */
    Port3ds_PumpAudio_impl();
    gPortProf = prev;
    }
}
void PortAudio_Init(void) {
    Audio_Init();
    AudioLoad_SetDmaHandler(DmaMgr_AudioDmaHandler);
    Audio_InitSound();
    osSendMesg(&sAudioMgr.lockQueue, NULL, OS_MESG_NOBLOCK);
}

static void Port3ds_PumpAudio_impl(void) {
    extern void Port3ds_AudioTaskRun(void* task);
    extern void Port3ds_AudioTaskWait(void);
    extern u64 gPortPerfAudioMain;
    extern u64 svcGetSystemTick(void);
    AudioTask* previous = R_AUDIOMGR_DEBUG_LEVEL > 0 ? NULL : sAudioMgr.rspTask;
    AudioTask* next;
    u64 start = svcGetSystemTick();

    if (previous != NULL) {
        Port3ds_AudioTaskRun(&previous->task);
    }
    next = R_AUDIOMGR_DEBUG_LEVEL >= 2 ? NULL : AudioThread_Update();
    if (previous != NULL) {
        Port3ds_AudioTaskWait();
        if (previous->taskQueue != NULL) {
            osSendMesg(previous->taskQueue, NULL, OS_MESG_NOBLOCK);
        }
    }
    sAudioMgr.rspTask = next;
    gPortPerfAudioMain += svcGetSystemTick() - start;
}

/* Rumble + Controller Pak: absent hardware */
s32 osMotorInit(OSMesgQueue* mq, OSPfs* pfs, s32 channel) { (void)mq; (void)pfs; (void)channel; return 1; }
s32 __osMotorAccess(OSPfs* pfs, s32 flag) { (void)pfs; (void)flag; return 1; }

/* PORT (2026-09-29): touch panel state (3ds_main.c draws it; see port_minimap.h and
 * docs/3ds-touch-panel.md). The game side fills gPortMinimap / gPortHud*; the input side sets the
 * gPortTouch* one-shot requests that z_player.c consumes. */
#include "port_minimap.h"
#include "segment_symbols.h"
#include "sys_cmpdma.h"
PortMinimap gPortMinimap;
int gPortMinimapOnBottom = 1;
const unsigned char* gPortHudIconSeg;
unsigned int gPortHudSerial;
int gPortHudKeys = -1;
int gPortHudTatl;
int gPortHudTop = 1;
volatile int gPortTouchOcarina;
volatile int gPortTouchPage = -1;
static PortHudInfo sHudInfo;
static u8 sHudIcons[4 * ICON_ITEM_TEX_SIZE] __attribute__((aligned(8)));
static const u8* sHudIconSource;

void PortHud_Update(PlayState* play) {
    PortHudInfo* h = &sHudInfo;
    SavePlayerData* data = &gSaveContext.save.saveInfo.playerData;
    Player* player = GET_PLAYER(play);
    u8 previousItems[3];
    int i;
    memcpy(previousItems, h->cItem, sizeof(previousItems));
    memset(h, 0, sizeof(*h));
    h->valid = gSaveContext.gameMode == GAMEMODE_NORMAL;
    h->health = data->health;
    h->healthCapacity = data->healthCapacity;
    h->magic = data->magic;
    h->magicCapacity = data->isMagicAcquired ? (data->isDoubleMagicAcquired ? 96 : 48) : 0;
    h->rupees = data->rupees;
    h->keys = -1;
    switch (play->sceneId) {
        case SCENE_INISIE_N:
        case SCENE_INISIE_R:
        case SCENE_MITURIN:
        case SCENE_HAKUGIN:
        case SCENE_SEA:
            h->keys = DUNGEON_KEY_COUNT(gSaveContext.mapIndex);
            break;
    }
    h->ocarina = INV_CONTENT(ITEM_OCARINA_OF_TIME);
    h->mask = player->currentMask == PLAYER_MASK_NONE ? ITEM_NONE : Player_MaskIdToItemId(player->currentMask - 1);
    h->day = gSaveContext.save.day;
    h->minutes = ((u32)CURRENT_TIME * 1440) >> 16;
    for (i = 0; i < 3; i++) {
        int item = GET_CUR_FORM_BTN_ITEM(i + EQUIP_SLOT_C_LEFT);
        h->cItem[i] = item;
        if (item < ITEM_F0 && (item != previousItems[i] || sHudIconSource != play->interfaceCtx.iconItemSegment)) {
            memcpy(sHudIcons + (i + 1) * ICON_ITEM_TEX_SIZE,
                   play->interfaceCtx.iconItemSegment + (i + 1) * ICON_ITEM_TEX_SIZE, ICON_ITEM_TEX_SIZE);
        }
        h->cDisabled[i] = gSaveContext.buttonStatus[i + EQUIP_SLOT_C_LEFT] == BTN_DISABLED;
        h->cAmmo[i] = -1;
        if (item >= ITEM_BOW_FIRE && item <= ITEM_BOW_LIGHT) {
            item = ITEM_BOW;
        }
        switch (item) {
            case ITEM_DEKU_STICK:
            case ITEM_DEKU_NUT:
            case ITEM_BOMB:
            case ITEM_BOW:
            case ITEM_BOMBCHU:
            case ITEM_POWDER_KEG:
            case ITEM_MAGIC_BEANS:
                h->cAmmo[i] = AMMO(item);
                break;
            case ITEM_PICTOGRAPH_BOX:
                h->cAmmo[i] = CHECK_QUEST_ITEM(QUEST_PICTOGRAPH) != 0;
                break;
        }
    }
    sHudIconSource = play->interfaceCtx.iconItemSegment;
    gPortHudIconSeg = sHudIcons;
    gPortHudKeys = h->keys;
    gPortHudTatl = play->interfaceCtx.tatlCalling;
    gPortHudSerial++;
    gPortTouchPage = -1;
}

void Port_GetHudInfo(PortHudInfo* h) {
    *h = sHudInfo;
    if (gPortHudSerial == 0) {
        memset(h, 0, sizeof(*h));
        h->rupees = h->keys = -1;
        h->ocarina = ITEM_NONE;
        h->mask = ITEM_NONE;
    }
}

const unsigned char* Port_GetItemIcon(int itemId) {
    static u64 icons[2][ICON_ITEM_TEX_SIZE / sizeof(u64)];
    static int ids[2] = { -1, -1 };
    int slot = itemId == ITEM_OCARINA_OF_TIME ? 0 : 1;
    if (itemId < 0 || itemId > ITEM_MASK_GIANT) {
        return NULL;
    }
    if (ids[slot] != itemId) {
        CmpDma_LoadFile(SEGMENT_ROM_START(icon_item_static_yar), itemId, icons[slot], ICON_ITEM_TEX_SIZE);
        ids[slot] = itemId;
    }
    return (const unsigned char*)icons[slot];
}

void PortMap_Begin(int sceneId) {
    gPortMinimap.serial++;
    gPortMinimap.sceneId = sceneId;
    gPortMinimap.numLayers = gPortMinimap.numMarks = 0;
    gPortMinimap.compass = gPortMinimap.hasStart = 0;
}

void PortMap_AddMark(int type, int x, int y, int r, int g, int b) {
    int i = gPortMinimap.numMarks;
    if (i == PORT_MINIMAP_MAX_MARKS) {
        return;
    }
    gPortMinimap.markType[i] = type;
    gPortMinimap.markX[i] = x;
    gPortMinimap.markY[i] = y;
    gPortMinimap.markR[i] = r;
    gPortMinimap.markG[i] = g;
    gPortMinimap.markB[i] = b;
    gPortMinimap.numMarks++;
}
