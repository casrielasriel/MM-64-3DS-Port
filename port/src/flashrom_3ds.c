#include <stdio.h>
#include <string.h>

#define u64 __u64
#define s64 __s64
#define u32 __u32
#define vu32 __vu32
#define vs32 __vs32
#define s32 __s32
#define u16 __u16
#define s16 __s16
#define u8 __u8
#define s8 __s8
#include <3ds/types.h>
#undef u64
#undef s64
#undef u32
#undef vu32
#undef vs32
#undef s32
#undef u16
#undef s16
#undef u8
#undef s8

#include "sys_flashrom.h"
#include "PR/os_internal_flash.h"
#include <3ds/thread.h>
#include <errno.h>

extern void PortDbg(const char* message);

#define FLASH_FILE "sdmc:/3ds/mm/flash.bin"
#define FLASH_TMP "sdmc:/3ds/mm/flash.tmp"
#define FLASH_BAK "sdmc:/3ds/mm/flash.bak"
#define FLASH_PAGE_COUNT (FLASH_SIZE / FLASH_BLOCK_SIZE)

static u8 sFlash[FLASH_SIZE];
static s32 sFlashInitialized;
static s32 sFlashHasPrimary;
static s32 sFlashResult;
static s32 sFlashPending;
static Thread sFlashThread;
static LightEvent sFlashDone;
/* The caller owns this buffer until SysFlashrom_AwaitResult completes. */
static const void* sFlashWriteAddr;
static u32 sFlashWritePage;
static u32 sFlashWriteCount;

enum {
    FLASH_LOAD_OK,
    FLASH_LOAD_MISSING,
    FLASH_LOAD_ERROR
};

static s32 FlashLoad(const char* path) {
    FILE* file;
    size_t size;
    s32 valid;

    file = fopen(path, "rb");
    if (file == NULL) {
        return errno == ENOENT ? FLASH_LOAD_MISSING : FLASH_LOAD_ERROR;
    }
    size = fread(sFlash, 1, sizeof(sFlash), file);
    valid = size == sizeof(sFlash) && fgetc(file) == EOF && !ferror(file);
    if (fclose(file) != 0 || !valid) {
        return FLASH_LOAD_ERROR;
    }
    return FLASH_LOAD_OK;
}

s32 SysFlashrom_InitFlash(void) {
    s32 primary;
    s32 backup;

    if (sFlashInitialized) {
        return 0;
    }

    primary = FlashLoad(FLASH_FILE);
    sFlashHasPrimary = primary == FLASH_LOAD_OK;
    if (!sFlashHasPrimary) {
        backup = FlashLoad(FLASH_BAK);
        if (backup == FLASH_LOAD_OK) {
            PortDbg("[flash] recovered flash.bak");
        } else if (primary == FLASH_LOAD_MISSING && backup == FLASH_LOAD_MISSING) {
            memset(sFlash, 0xFF, sizeof(sFlash));
        } else {
            PortDbg("[flash] failed to load flash.bin or flash.bak");
            return -1;
        }
    }

    sFlashInitialized = true;
    return 0;
}

s32 SysFlashrom_Read(void* addr, u32 pageNum, u32 pageCount) {
    if (!sFlashInitialized || pageNum > FLASH_PAGE_COUNT || pageCount > FLASH_PAGE_COUNT - pageNum) {
        return -1;
    }

    if (sFlashThread != NULL) {
        threadJoin(sFlashThread, U64_MAX);
    }
    memcpy(addr, sFlash + pageNum * FLASH_BLOCK_SIZE, pageCount * FLASH_BLOCK_SIZE);
    return 0;
}

static s32 FlashWrite(const void* addr, u32 pageNum, u32 pageCount) {
    FILE* file;
    u32 offset;
    u32 size;
    u32 tail;
    s32 written;

    if (!sFlashInitialized || pageNum > FLASH_PAGE_COUNT || pageCount > FLASH_PAGE_COUNT - pageNum) {
        return -1;
    }
    offset = pageNum * FLASH_BLOCK_SIZE;
    size = pageCount * FLASH_BLOCK_SIZE;
    if (size == 0 || (sFlashHasPrimary && memcmp(sFlash + offset, addr, size) == 0)) {
        return 0;
    }
    tail = sizeof(sFlash) - offset - size;

    file = fopen(FLASH_TMP, "wb");
    if (file == NULL) {
        PortDbg("[flash] failed to create flash.tmp");
        return -1;
    }
    written = fwrite(sFlash, 1, offset, file) == offset && fwrite(addr, 1, size, file) == size &&
              fwrite(sFlash + offset + size, 1, tail, file) == tail;
    if (fclose(file) != 0 || !written) {
        PortDbg("[flash] failed to write flash.tmp");
        remove(FLASH_TMP);
        return -1;
    }

    /* Keep the last complete image available while replacing the primary. */
    if (sFlashHasPrimary) {
        if (remove(FLASH_BAK) != 0 && errno != ENOENT) {
            PortDbg("[flash] failed to remove flash.bak");
            return -1;
        }
        if (rename(FLASH_FILE, FLASH_BAK) != 0) {
            PortDbg("[flash] failed to back up flash.bin");
            return -1;
        }
        sFlashHasPrimary = false;
    } else if (remove(FLASH_FILE) != 0 && errno != ENOENT) {
        PortDbg("[flash] failed to remove invalid flash.bin");
        return -1;
    }
    if (rename(FLASH_TMP, FLASH_FILE) != 0) {
        PortDbg("[flash] failed to replace flash.bin");
        return -1;
    }
    sFlashHasPrimary = true;
    memcpy(sFlash + offset, addr, size);
    return 0;
}

static void FlashWriteThread(void* arg) {
    (void)arg;
    sFlashResult = FlashWrite(sFlashWriteAddr, sFlashWritePage, sFlashWriteCount);
    LightEvent_Signal(&sFlashDone);
}

void SysFlashrom_WriteAsync(void* addr, u32 pageNum, u32 pageCount) {
    s32 priority = 0x30;

    if (sFlashPending) {
        SysFlashrom_AwaitResult();
    }
    sFlashWriteAddr = addr;
    sFlashWritePage = pageNum;
    sFlashWriteCount = pageCount;
    sFlashPending = true;
    LightEvent_Init(&sFlashDone, RESET_STICKY);
    svcGetThreadPriority(&priority, CUR_THREAD_HANDLE);
    sFlashThread = threadCreate(FlashWriteThread, NULL, 16 * 1024, priority, 0, false);
    if (sFlashThread == NULL) {
        PortDbg("[flash] failed to create save worker");
        sFlashResult = -1;
        LightEvent_Signal(&sFlashDone);
    }
}

s32 SysFlashrom_IsBusy(void) {
    /* MM polls for a full completion queue here, not for an active write. */
    return sFlashPending && LightEvent_TryWait(&sFlashDone);
}

s32 SysFlashrom_AwaitResult(void) {
    if (!sFlashPending) {
        return -1;
    }
    LightEvent_Wait(&sFlashDone);
    if (sFlashThread != NULL) {
        threadJoin(sFlashThread, U64_MAX);
        threadFree(sFlashThread);
        sFlashThread = NULL;
    }
    sFlashPending = false;
    if (sFlashResult != 0) {
        PortDbg("[flash] save request failed");
    }
    return sFlashResult;
}

void SysFlashrom_WriteSync(void* addr, u32 pageNum, u32 pageCount) {
    SysFlashrom_WriteAsync(addr, pageNum, pageCount);
    SysFlashrom_AwaitResult();
}

void PortFlashrom_WaitIdle(void) {
    if (sFlashPending) {
        SysFlashrom_AwaitResult();
    }
}
