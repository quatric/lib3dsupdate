/* The 3DS side: threads, time, the soc service, entropy, and installing a
 * CIA through the AM service. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <malloc.h>
#include <unistd.h>
#include <3ds.h>
#include <mbedtls/platform_time.h>
#include "3dsupdate_internal.h"

#define STACK_SIZE      (128 * 1024)
#define SOC_BUFFER_SIZE 0x100000

/* ── Threads and time ────────────────────────────────────────────────────── */

typedef struct {
    void *(*fn)(void *);
} start_t;

static void thread_main(void *arg)
{
    start_t s = *(start_t *)arg;
    free(arg);
    s.fn(NULL);
}

int ctrupdate_sys_thread(void *(*fn)(void *))
{
    /* Just below the caller's priority, on the app's core: it runs whenever the
     * frame loop waits for the next VBlank, and while it waits on the network. */
    start_t *s = malloc(sizeof *s);
    if (!s) return -1;
    s->fn = fn;
    s32 prio = 0x30;
    svcGetThreadPriority(&prio, CUR_THREAD_HANDLE);
    if (!threadCreate(thread_main, s, STACK_SIZE, prio + 1 > 0x3F ? 0x3F : prio + 1, -2, true)) {
        free(s);
        return -1;
    }
    return 0;
}

unsigned long long ctrupdate_sys_ms(void) { return osGetTime(); }
void ctrupdate_sys_sleep_ms(int ms) { svcSleepThread((s64)ms * 1000000); }

mbedtls_ms_time_t mbedtls_ms_time(void) { return (mbedtls_ms_time_t)osGetTime(); }

/* ── Network ─────────────────────────────────────────────────────────────── */

/* socInit works once per process: a second call fails (-1) without touching
 * its buffer, and socExit always shuts the service down. So the library only
 * starts soc when the app has not, and stops it again when it is done. */
static u32 *soc_buffer;

int ctrupdate_sys_net_up(volatile int *cancel)
{
    if (*cancel) return -1;
    if (!soc_buffer) {
        u32 *buf = memalign(0x1000, SOC_BUFFER_SIZE);
        if (!buf) return -1;
        Result r = socInit(buf, SOC_BUFFER_SIZE);
        if (R_SUCCEEDED(r)) {
            soc_buffer = buf; /* ours until ctrupdate_sys_net_down */
        } else {
            free(buf);
            if (r != -1) return -1; /* -1: the app has it up already */
        }
    }
    u32 wifi = 0;
    if (R_SUCCEEDED(acInit())) {
        ACU_GetWifiStatus(&wifi);
        acExit();
    }
    return wifi && gethostid() ? 0 : -1;
}

void ctrupdate_sys_net_down(void)
{
    if (!soc_buffer) return;
    socExit();
    free(soc_buffer);
    soc_buffer = NULL;
}

/* ── Entropy ─────────────────────────────────────────────────────────────── */

/* The hardware RNG behind the ps service, or timing jitter when the app may
 * not use ps. */
int ctrupdate_sys_entropy(void *data, unsigned char *out, size_t len, size_t *olen)
{
    (void)data;
    if (R_SUCCEEDED(psInit())) {
        Result r = PS_GenerateRandomBytes(out, len);
        psExit();
        if (R_SUCCEEDED(r)) {
            *olen = len;
            return 0;
        }
    }
    static volatile u32 scratch[256];
    for (size_t i = 0; i < len; i++) {
        u32 acc = 0;
        for (int k = 0; k < 16; k++) {
            u64 t0 = svcGetSystemTick();
            for (int n = 0; n < (int)(t0 & 31) + 8; n++) scratch[(t0 + n * 37) & 255] += (u32)t0 ^ n;
            u64 t1 = svcGetSystemTick();
            acc = (acc << 3 | acc >> 29) ^ (u32)(t1 - t0) ^ (u32)t1;
        }
        out[i] = (unsigned char)(acc ^ acc >> 8 ^ acc >> 16 ^ acc >> 24);
    }
    *olen = len;
    return 0;
}

/* ── Files ───────────────────────────────────────────────────────────────── */

int ctrupdate_sys_release_file(const char *path)
{
    (void)path; /* a running .3dsx is loaded into memory; nothing holds it */
    return -1;
}

const char *ctrupdate_sys_sd_root(void) { return "sdmc:/"; }

/* ── CIA ─────────────────────────────────────────────────────────────────── */

/* An installed title: not started through the Homebrew Launcher, and the
 * running program is a title installed on the SD card (a .3dsx that an
 * emulator or another loader starts directly is neither). Asked once. */
int ctrupdate_sys_is_cia(void)
{
    static int known = -1;
    if (known >= 0) return known;
    known = 0;
    u64 id = 0;
    if (envIsHomebrew() || R_FAILED(APT_GetProgramID(&id)) || !id) return known;
    if (R_SUCCEEDED(amInit())) {
        AM_TitleInfo info;
        known = R_SUCCEEDED(AM_GetTitleInfo(MEDIATYPE_SD, 1, &id, &info));
        amExit();
    }
    return known;
}

static Handle cia;
static u64 cia_offset;

int ctrupdate_sys_cia_begin(void)
{
    if (R_FAILED(amInit())) return -1;
    cia_offset = 0;
    if (R_FAILED(AM_StartCiaInstall(MEDIATYPE_SD, &cia))) {
        amExit();
        return -1;
    }
    return 0;
}

int ctrupdate_sys_cia_write(const unsigned char *data, size_t len)
{
    u32 written = 0;
    if (R_FAILED(FSFILE_Write(cia, &written, cia_offset, data, (u32)len, 0)) || written != len) return -1;
    cia_offset += len;
    return 0;
}

int ctrupdate_sys_cia_end(int ok)
{
    Result r = ok ? AM_FinishCiaInstall(cia) : AM_CancelCIAInstall(cia);
    cia = 0;
    amExit();
    return ok && R_SUCCEEDED(r) ? 0 : -1;
}
