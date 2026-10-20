/* Shared between the library's source files; not part of the public API. */
#ifndef CTRUPDATE_INTERNAL_H
#define CTRUPDATE_INTERNAL_H

#include <stddef.h>
#include "3dsupdate.h"

/* Receives body bytes as they arrive; return <0 to abort the transfer. */
typedef int (*ctrupdate_sink)(void *ctx, const unsigned char *data, size_t len);

/* HTTPS GET, following up to 5 redirects. on_length (may be NULL) is told the
 * body size (-1 if unknown) before the first sink call. Returns 0 when the
 * whole body of a 200 reply was delivered. */
int ctrupdate_http_get(const char *url, const char *accept, ctrupdate_sink sink, void (*on_length)(void *, long long),
                        void *ctx, volatile int *cancel);

/* Highest numbered non-prerelease tag in a /releases reply. */
int ctrupdate_newest_tag(const char *json, char *out, size_t out_len);

/* Download URL of the asset to install for release `tag`. For an installed
 * title (cia set): a .cia. Otherwise a 3DS zip, else a .3dsx, else a zip that
 * names no other console. */
int ctrupdate_release_asset(const char *json, const char *tag, int cia, char *out, size_t out_len);

/* Unpack a zip into app_dir. Entries under ".../3ds/<Name>/" land in app_dir;
 * anything outside that folder is skipped (a zip without a 3ds/ folder is
 * unpacked as is). When exe_name is set (e.g. "Renamed.3dsx") and the folder
 * holds exactly one file with that extension, it is written under that name,
 * so a renamed copy is updated in place. Every file is written to a temporary
 * name first and only renamed over the old one once all of them were written,
 * so a failure leaves the installed app untouched. progress gets bytes written
 * so far and the total. Returns 0 on success; err gets a short reason. */
int ctrupdate_extract(const char *zip_path, const char *app_dir, const char *exe_name,
                       void (*progress)(void *, unsigned long long, unsigned long long), void *ctx, char *err,
                       size_t err_len);

/* The "3ds/<Name>" folder name inside a zip, or "" if none. */
int ctrupdate_zip_app_name(const char *zip_path, char *out, size_t out_len);

/* Move src over dst: rename, or copy when the old file cannot be removed.
 * Returns 0 on success. */
int ctrupdate_replace(const char *src, const char *dst);

/* ── Platform layer (ctrupdate_sys.c; tests/host_sys.c on a desktop) ───── */

/* Run fn on a new detached thread. Returns 0 when it started. */
int ctrupdate_sys_thread(void *(*fn)(void *));
unsigned long long ctrupdate_sys_ms(void);
void ctrupdate_sys_sleep_ms(int ms);
/* Connect to the network if needed. Returns 0 when it is usable. */
int ctrupdate_sys_net_up(volatile int *cancel);
/* Release whatever ctrupdate_sys_net_up took for the library itself. */
void ctrupdate_sys_net_down(void);
/* mbedTLS entropy source. */
int ctrupdate_sys_entropy(void *data, unsigned char *out, size_t len, size_t *olen);
/* Let go of path if the system holds it open. Returns 0 when it did
 * something worth retrying for. */
int ctrupdate_sys_release_file(const char *path);
/* The SD card's root with a trailing slash: "sdmc:/". */
const char *ctrupdate_sys_sd_root(void);
/* 1 when running as an installed title rather than a .3dsx. */
int ctrupdate_sys_is_cia(void);
/* Install a CIA as it streams in: begin, write the bytes in order, then end
 * with ok=1 to commit it or ok=0 to throw it away. Each returns 0 on success. */
int ctrupdate_sys_cia_begin(void);
int ctrupdate_sys_cia_write(const unsigned char *data, size_t len);
int ctrupdate_sys_cia_end(int ok);

#endif
