#include <3ds.h>
#include <string.h>

#define PORT_AUDIO_RATE   32000
#define PORT_AUDIO_NBUFS  6 /* (4 before the Old 3DS margin below: room for one more frame queued) */
#define PORT_AUDIO_MAXSAMPLES 1600 /* per submit; engine frames are ~544-736 */

static bool sNdspOk = false;
static ndspWaveBuf sWaveBufs[PORT_AUDIO_NBUFS];
static s16* sWaveData[PORT_AUDIO_NBUFS];
static int sNextBuf = 0;
u32 gPortPerfAudioUnderruns, gPortPerfAudioDrops; /* perf report: the DSP ran dry before a buffer came / a buffer dropped */
static int sAudioStarted;


void Port3ds_AudioInit(void) {
    extern void PortDbg(const char*);
    if (R_FAILED(ndspInit())) {
        PortDbg("[audio] ndspInit FAILED - audio disabled");
        return;
    }
    PortDbg("[audio] ndspInit OK - ndsp up");
    ndspSetOutputMode(NDSP_OUTPUT_STEREO);
    ndspChnReset(0);
    ndspChnSetInterp(0, NDSP_INTERP_LINEAR);
    ndspChnSetRate(0, (float)PORT_AUDIO_RATE);
    ndspChnSetFormat(0, NDSP_FORMAT_STEREO_PCM16);

    for (int i = 0; i < PORT_AUDIO_NBUFS; i++) {
        sWaveData[i] = (s16*)linearAlloc(PORT_AUDIO_MAXSAMPLES * 2 * sizeof(s16));
        if (sWaveData[i] == NULL) {
            PortDbg("[audio] no linear memory for DSP buffers - audio disabled");
            while (i > 0) {
                i--;
                linearFree(sWaveData[i]);
            }
            ndspExit();
            return;
        }
        memset(&sWaveBufs[i], 0, sizeof(ndspWaveBuf));
        sWaveBufs[i].status = NDSP_WBUF_DONE;
    }

    sNdspOk = true;
}

int Port3ds_AudioReady(void) {
    return sNdspOk ? 1 : 0;
}

void Port3ds_AudioSubmit(const s16* samples, int nsamples) {
    if (!sNdspOk || samples == NULL || nsamples <= 0) {
        return;
    }
    if (nsamples > PORT_AUDIO_MAXSAMPLES) {
        nsamples = PORT_AUDIO_MAXSAMPLES;
    }
    ndspWaveBuf* wb = &sWaveBufs[sNextBuf];
    if (wb->status != NDSP_WBUF_DONE && wb->status != NDSP_WBUF_FREE) {
        gPortPerfAudioDrops++;
        return; /* all buffers in flight; drop */
    }
    {
        int i, live = 0;
        for (i = 0; i < PORT_AUDIO_NBUFS; i++) {
            live |= sWaveBufs[i].status == NDSP_WBUF_QUEUED || sWaveBufs[i].status == NDSP_WBUF_PLAYING;
        }
        gPortPerfAudioUnderruns += sAudioStarted && !live; /* a gap: the DSP had nothing left to play */
        sAudioStarted = 1;
    }
    memcpy(sWaveData[sNextBuf], samples, (size_t)nsamples * 2 * sizeof(s16));
    memset(wb, 0, sizeof(ndspWaveBuf));
    wb->data_vaddr = sWaveData[sNextBuf];
    wb->nsamples = (u32)nsamples;
    {
        extern void Port3ds_CacheFlush(const void* p, u32 size); /* gfx_3ds.c: the kernel, not the DSP service */
        Port3ds_CacheFlush(sWaveData[sNextBuf], (u32)((size_t)nsamples * 2 * sizeof(s16)));
    }
    ndspChnWaveBufAdd(0, wb);
    sNextBuf = (sNextBuf + 1) % PORT_AUDIO_NBUFS;
}

/* PORT (2026-10-05): samples the engine is told are NOT queued (osAiGetLength), so it keeps that much more queued.
 * The engine aims at its frame plus 128 samples (4 ms) still queued when the next frame arrives - enough for the N64,
 * whose audio task runs on time. On an Old 3DS the mixer shares the system core (7.5-10 ms per task in big scenes,
 * hardware v58) and the game's update can hold the next task back: a frame more than 4 ms late found the DSP empty
 * and played a gap (crackling, user report). With one more frame queued (~16 ms more latency) it tolerates ~20 ms.
 * -1 = by model (Old 3DS mixer on the system core: 512, else 0); settings audio_margin=<samples>. */
int gPortAudioMargin = -1;

/* Stereo frames queued on ndsp and not yet played (the N64 osAiGetLength equivalent). */
int Port3ds_AudioQueuedFrames(void) {
    int i, queued = 0;
    if (!sNdspOk) {
        return 0;
    }
    for (i = 0; i < PORT_AUDIO_NBUFS; i++) {
        if (sWaveBufs[i].status == NDSP_WBUF_QUEUED || sWaveBufs[i].status == NDSP_WBUF_PLAYING) {
            queued += (int)sWaveBufs[i].nsamples;
        }
    }
    if (queued > 0) {
        queued -= (int)ndspChnGetSamplePos(0); /* position within the buffer now playing */
    }
    {
        extern int gPortAudioCoreNow;
        int margin = gPortAudioMargin >= 0 ? gPortAudioMargin : gPortAudioCoreNow == 1 ? 512 : 0;
        queued -= margin;
    }
    return queued > 0 ? queued : 0;
}

/* PORT (2026-09-29): the audio "RSP". On the N64 the RSP runs each audio task while the CPU builds the next
 * one (AudioMgr_HandleRetrace dispatches task N, runs AudioThread_Update for N+1, then waits for N-1); the
 * engine's DMA buffer lifetimes assume that overlap. Here the C microcode (PortAudio_RunTask) runs on a
 * worker thread on another core - core 2 on New 3DS, the system core 1 (with an app CPU-time allowance) on
 * Old 3DS - so it no longer eats the main thread's frame time. Dispatching a task first waits for the
 * previous one, so tasks still run in order. If no worker can be created, tasks run synchronously. */
/* OSTask (ultra64.h, which clashes with <3ds.h>) copied as opaque bytes; sched_shim.c checks the size */
#define PORT_OSTASK_SIZE 64
typedef struct { u8 bytes[PORT_OSTASK_SIZE]; } PortOSTask;
extern void PortAudio_RunTask(void* task);

static Thread sAudioWorker;
static LightEvent sAudioJobStart, sAudioJobDone;

static PortOSTask sAudioJob __attribute__((aligned(8)));
static volatile bool sAudioJobBusy;
static int sAudioAsync = -1; /* -1 = not tried yet */

u64 gPortPerfAudioUcode, gPortPerfAudioWait; /* ticks: microcode (worker core), main thread waiting for it */
u32 gPortPerfAudioTasks;

static void Port3ds_AudioWorkerMain(void* arg) {
    (void)arg;
    for (;;) {
        u64 t0;
        LightEvent_Wait(&sAudioJobStart);
        t0 = svcGetSystemTick();
        PortAudio_RunTask(&sAudioJob);
        gPortPerfAudioUcode += svcGetSystemTick() - t0;
        gPortPerfAudioTasks++;
        sAudioJobBusy = false;
        LightEvent_Signal(&sAudioJobDone);
    }
}

int gPortAudioCore = -1; /* -1: by model (New 3DS core 2, Old 3DS core 1) */
unsigned gPortAudioCore1Limit; /* the system core share granted (perf report), 0 when not on core 1 */
int gPortAudioCoreNow = -1;    /* the core running the current tasks (perf report) */
/* audio_share_ab (3ds_main.c): a new system-core share between perf reports */
void Port3ds_AudioSetShare(int percent) {
    if (gPortAudioCore1Limit != 0 && R_SUCCEEDED(APT_SetAppCpuTimeLimit((u32)percent))) {
        gPortAudioCore1Limit = (unsigned)percent;
    }
}

/* Old 3DS: the audio microcode needs a task every retrace, 2-4 ms each at 268 MHz (hardware v49 on core 2). The
 * system core gives an application only the share it asked for with APT_SetAppCpuTimeLimit, up to the exheader's
 * limit (port/oot.rsf MaxCpu, 89%). Measured at Old 3DS speed (PORT 2026-10-04):
 *   30% (v52; the default exheader limit refused more): 12-18 ms per task, the game waited for it - low frame rate,
 *       crackling audio;
 *   80% (v54): 5-6.7 ms per task, but every request to the system services that live on that core (the DSP's cache
 *       flush per audio pump, the GPU's) got slower - the audio engine on the game thread went from ~3 to 10-14 ms
 *       per update, on a New 3DS too, where the mixer was not even on core 1 (the share was reserved for perf_ab).
 * So: never on a New 3DS (core 2 is free), and a middle share on an Old 3DS until hardware decides
 * (settings audio_share=N, audio_share_ab=1 cycles 30/55/80 for a measurement). */
int gPortAudioShare = 55;
static void Port3ds_AudioAskSystemCore(void) {
    u32 want = (u32)(gPortAudioShare < 10 ? 10 : gPortAudioShare > 89 ? 89 : gPortAudioShare);
    u32 limits[4] = { want, 55, 30, 0 }, granted = 0;
    int i;
    for (i = 0; i < 3; i++) {
        if (R_SUCCEEDED(APT_SetAppCpuTimeLimit(limits[i]))) {
            break;
        }
    }
    APT_GetAppCpuTimeLimit(&granted);
    gPortAudioCore1Limit = granted;
    {
        extern void PortDbgX(const char* label, unsigned val);
        PortDbgX("[audio] system core share for the mixer (%)", granted);
    }
}
static void Port3ds_AudioWorkerStart(void) {
    extern void PortDbg(const char*);
    bool n3ds = false;
    s32 prio = 0x30;
    int core;

    sAudioAsync = 0;
    LightEvent_Init(&sAudioJobStart, RESET_ONESHOT);
    LightEvent_Init(&sAudioJobDone, RESET_ONESHOT);
    APT_CheckNew3DS(&n3ds);
    svcGetThreadPriority(&prio, CUR_THREAD_HANDLE);
    core = n3ds ? 2 : 1;
    if (gPortAudioCore >= 0) {
        core = gPortAudioCore;
    }
    if (core == 1) {
        Port3ds_AudioAskSystemCore();
    }
    sAudioWorker = threadCreate(Port3ds_AudioWorkerMain, NULL, 16 * 1024, prio > 0x18 ? prio - 1 : prio, core, true);
    if (sAudioWorker == NULL && n3ds) {
        APT_SetAppCpuTimeLimit(30);
        core = 1;
        sAudioWorker = threadCreate(Port3ds_AudioWorkerMain, NULL, 16 * 1024, prio > 0x18 ? prio - 1 : prio, core, true);
    }
    gPortAudioCoreNow = sAudioWorker != NULL ? core : 0;
    if (sAudioWorker != NULL) {
        sAudioAsync = 1;
        PortDbg(core == 2 ? "[audio] microcode worker on core 2" : core == 1 ? "[audio] microcode worker on core 1" :
                                                                             "[audio] microcode worker on core 0");
    } else {
        PortDbg("[audio] no microcode worker thread - running audio tasks synchronously");
    }
}

/* wait until the previous audio task has finished */
void Port3ds_AudioTaskWait(void) {
    u64 t0 = svcGetSystemTick();
    while (sAudioJobBusy) {
        LightEvent_Wait(&sAudioJobDone);
    }
    gPortPerfAudioWait += svcGetSystemTick() - t0;
}

/* run (or start) one audio task; returns once it may be treated as complete by the scheduler */
void Port3ds_AudioTaskRun(void* task) {
    if (sAudioAsync < 0) {
        Port3ds_AudioWorkerStart();
    }
    if (sAudioAsync == 0) {
        u64 t0 = svcGetSystemTick();
        PortAudio_RunTask(task);
        gPortPerfAudioUcode += svcGetSystemTick() - t0;
        gPortPerfAudioTasks++;
        return;
    }
    Port3ds_AudioTaskWait();
    memcpy(&sAudioJob, task, sizeof(sAudioJob)); /* the command list stays valid: the engine double-buffers it */
    sAudioJobBusy = true;
    LightEvent_Signal(&sAudioJobStart);
}
