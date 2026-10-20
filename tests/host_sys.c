/* The platform layer on a desktop, for the host tests: POSIX threads and
 * time, the network is assumed up, and entropy comes from /dev/urandom. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <pthread.h>
#include <unistd.h>
#include <mbedtls/platform_time.h>
#include "3dsupdate_internal.h"

int ctrupdate_sys_thread(void *(*fn)(void *))
{
    pthread_t t;
    if (pthread_create(&t, NULL, fn, NULL) != 0) return -1;
    pthread_detach(t);
    return 0;
}

unsigned long long ctrupdate_sys_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (unsigned long long)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

void ctrupdate_sys_sleep_ms(int ms) { usleep(ms * 1000); }

mbedtls_ms_time_t mbedtls_ms_time(void) { return (mbedtls_ms_time_t)ctrupdate_sys_ms(); }

int ctrupdate_sys_net_up(volatile int *cancel) { return *cancel ? -1 : 0; }
void ctrupdate_sys_net_down(void) {}

int ctrupdate_sys_entropy(void *data, unsigned char *out, size_t len, size_t *olen)
{
    (void)data;
    FILE *f = fopen("/dev/urandom", "rb");
    size_t n = f ? fread(out, 1, len, f) : 0;
    if (f) fclose(f);
    *olen = n;
    return n == len ? 0 : -1;
}

int ctrupdate_sys_release_file(const char *path) { (void)path; return -1; }

/* Desktop stand-in for the SD card: build/sd. */
const char *ctrupdate_sys_sd_root(void) { return "build/sd/"; }

/* HOST_CIA=1 pretends to be an installed title; the "installed" CIA is
 * written to build/sd/installed.cia only once it is complete. */
int ctrupdate_sys_is_cia(void) { return getenv("HOST_CIA") != NULL; }

static FILE *cia;

int ctrupdate_sys_cia_begin(void)
{
    cia = fopen("build/sd/installing.cia", "wb");
    return cia ? 0 : -1;
}

int ctrupdate_sys_cia_write(const unsigned char *data, size_t len)
{
    return fwrite(data, 1, len, cia) == len ? 0 : -1;
}

int ctrupdate_sys_cia_end(int ok)
{
    int closed = fclose(cia) == 0;
    cia = NULL;
    if (ok && closed && rename("build/sd/installing.cia", "build/sd/installed.cia") == 0) return 0;
    remove("build/sd/installing.cia");
    return -1;
}
