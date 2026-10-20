#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>
#include <errno.h>
#include <sys/stat.h>
#include "3dsupdate_internal.h"

#define MAX_BODY    (512 * 1024)
#define DOWNLOAD_SHARE 80 /* percent of the progress bar spent downloading */

static volatile ctrupdate_state state = CTRUPDATE_IDLE;
static volatile ctrupdate_install_state install_state = CTRUPDATE_INSTALL_IDLE;
static volatile int cancelled;
static volatile int noticed;
static volatile int running; /* a thread is alive, even after a cancel */
static volatile int progress;
static char owner[64], repo[96], current[48], latest[48], url[200], message[200];
static char asset[1024], app_path[256], install_dir[384], install_error[160];

static int start_thread(void *(*fn)(void *))
{
    running = 1;
    if (ctrupdate_sys_thread(fn) != 0) {
        running = 0;
        return -1;
    }
    return 0;
}

/* ── Check ───────────────────────────────────────────────────────────────── */

typedef struct {
    char *data;
    size_t len;
} membuf_t;

static int mem_sink(void *ctx, const unsigned char *data, size_t len)
{
    membuf_t *m = ctx;
    if (m->len + len > MAX_BODY) len = MAX_BODY - m->len; /* keep what fits */
    memcpy(m->data + m->len, data, len);
    m->len += len;
    return 0;
}

static void *check_thread(void *arg)
{
    (void)arg;
    ctrupdate_state result = CTRUPDATE_FAILED;
    char tag[sizeof latest];
    membuf_t body = { malloc(MAX_BODY + 1), 0 };

    if (body.data && ctrupdate_sys_net_up(&cancelled) == 0 && !cancelled) {
        char api[256];
        snprintf(api, sizeof api, "https://api.github.com/repos/%s/%s/releases?per_page=10", owner, repo);
        if (ctrupdate_http_get(api, "application/vnd.github+json", mem_sink, NULL, &body, &cancelled) == 0) {
            body.data[body.len] = 0;
            if (ctrupdate_newest_tag(body.data, tag, sizeof tag) == 0 && CTRUPDATE_CompareVersions(tag, current) > 0) {
                snprintf(latest, sizeof latest, "%s", tag);
                snprintf(message, sizeof message, "Version %s is available (you have %s%s).", latest,
                         isdigit((unsigned char)current[0]) ? "v" : "", current);
                if (ctrupdate_release_asset(body.data, tag, ctrupdate_sys_is_cia(), asset, sizeof asset) < 0) asset[0] = 0;
                result = CTRUPDATE_AVAILABLE;
            } else {
                result = CTRUPDATE_UP_TO_DATE;
            }
        }
    }
    ctrupdate_sys_net_down();
    free(body.data);
    state = cancelled ? CTRUPDATE_CANCELLED : result;
    running = 0;
    return NULL;
}

int CTRUPDATE_Start(const char *o, const char *r, const char *version)
{
    if (running) return -1;
    snprintf(owner, sizeof owner, "%s", o);
    snprintf(repo, sizeof repo, "%s", r);
    snprintf(current, sizeof current, "%s", version ? version : "0");
    snprintf(url, sizeof url, "github.com/%s/%s/releases", owner, repo);
    latest[0] = message[0] = asset[0] = 0;
    cancelled = noticed = 0;
    install_state = CTRUPDATE_INSTALL_IDLE;
    state = CTRUPDATE_CHECKING;
    if (start_thread(check_thread) < 0) {
        state = CTRUPDATE_FAILED;
        return -1;
    }
    return 0;
}

/* ── Install ─────────────────────────────────────────────────────────────── */

typedef struct {
    FILE *f;
    long long total, done;
} download_t;

static int file_sink(void *ctx, const unsigned char *data, size_t len)
{
    download_t *d = ctx;
    if (fwrite(data, 1, len, d->f) != len) return -1;
    d->done += len;
    if (d->total > 0) progress = (int)(d->done * DOWNLOAD_SHARE / d->total);
    return 0;
}

static void on_length(void *ctx, long long len) { ((download_t *)ctx)->total = len; }

static void on_extract(void *ctx, unsigned long long done, unsigned long long total)
{
    (void)ctx;
    if (total) progress = DOWNLOAD_SHARE + (int)(done * (100 - DOWNLOAD_SHARE) / total);
}

static int is_dir(const char *path)
{
    struct stat st;
    return stat(path, &st) == 0 && S_ISDIR(st.st_mode);
}

static int has_ext(const char *name, const char *ext)
{
    size_t n = strlen(name), e = strlen(ext);
    return n > e && !strcasecmp(name + n - e, ext);
}

static int fail(const char *why)
{
    snprintf(install_error, sizeof install_error, "%s", why);
    return -1;
}

static int cia_sink(void *ctx, const unsigned char *data, size_t len)
{
    download_t *d = ctx;
    if (ctrupdate_sys_cia_write(data, len) != 0) return -1;
    d->done += len;
    if (d->total > 0) progress = (int)(d->done * 99 / d->total);
    return 0;
}

/* An installed title: stream the release's .cia straight into the system's
 * installer, which only commits it once all of it arrived. */
static int install_cia(void)
{
    install_dir[0] = 0;
    if (ctrupdate_sys_cia_begin() != 0) return fail("cannot start installing");
    download_t d = { NULL, -1, 0 };
    int ok = ctrupdate_sys_net_up(&cancelled) == 0 &&
             ctrupdate_http_get(asset, "application/octet-stream", cia_sink, on_length, &d, &cancelled) == 0;
    ctrupdate_sys_net_down();
    if (!ok) {
        ctrupdate_sys_cia_end(0);
        return fail(cancelled ? "cancelled" : "download failed");
    }
    install_state = CTRUPDATE_INSTALL_EXTRACTING;
    if (ctrupdate_sys_cia_end(1) != 0) return fail("the system did not accept the CIA");
    progress = 100;
    return 0;
}

static int install(void)
{
    if (ctrupdate_sys_is_cia()) return install_cia();

    char *tmp = malloc(320);
    char root[256], exe[96] = "";
    int result = -1;
    if (!tmp) return fail("out of memory");

    if (app_path[0]) {
        /* The running app's own file: install into its folder. */
        snprintf(install_dir, sizeof install_dir, "%s", app_path);
        char *slash = strrchr(install_dir, '/');
        snprintf(exe, sizeof exe, "%s", slash + 1);
        *slash = 0;
        snprintf(root, sizeof root, "%s", install_dir);
    } else {
        install_dir[0] = 0;
        snprintf(root, sizeof root, "%s3ds", ctrupdate_sys_sd_root());
        mkdir(root, 0777);
    }
    if (!is_dir(root)) {
        free(tmp);
        return fail(install_dir[0] ? "cannot find the app's folder" : "no SD card found");
    }

    int is_zip = has_ext(asset, ".zip");
    snprintf(tmp, 320, "%s/3dsupdate-download.tmp", root);
    download_t d = { fopen(tmp, "wb"), -1, 0 };
    if (!d.f) {
        free(tmp);
        return fail("cannot write to the SD card");
    }
    install_state = CTRUPDATE_INSTALL_DOWNLOADING;
    int ok = ctrupdate_sys_net_up(&cancelled) == 0 &&
             ctrupdate_http_get(asset, "application/octet-stream", file_sink, on_length, &d, &cancelled) == 0;
    if (fclose(d.f) != 0) ok = 0;
    ctrupdate_sys_net_down();
    if (!ok) {
        fail(cancelled ? "cancelled" : "download failed");
        goto out;
    }
    progress = DOWNLOAD_SHARE;
    install_state = CTRUPDATE_INSTALL_EXTRACTING;

    if (!install_dir[0]) {
        char name[96];
        if (!is_zip || ctrupdate_zip_app_name(tmp, name, sizeof name) < 0) snprintf(name, sizeof name, "%s", repo);
        snprintf(install_dir, sizeof install_dir, "%s/%s", root, name);
    }

    if (is_zip) {
        if (ctrupdate_extract(tmp, install_dir, exe[0] ? exe : NULL, on_extract, NULL, install_error,
                               sizeof install_error) < 0)
            goto out;
    } else {
        /* A bare .3dsx replaces the running file. */
        const char *ext = ".3dsx";
        char target[512];
        mkdir(install_dir, 0777);
        if (exe[0] && has_ext(exe, ext)) snprintf(target, sizeof target, "%s/%s", install_dir, exe);
        else snprintf(target, sizeof target, "%s/%s%s", install_dir, repo, ext);
        if (ctrupdate_replace(tmp, target) != 0) {
            fail("could not replace the app");
            goto out;
        }
    }
    progress = 100;
    result = 0;

out:
    remove(tmp);
    free(tmp);
    return result;
}

static void *install_thread(void *arg)
{
    (void)arg;
    install_state = install() == 0 ? CTRUPDATE_INSTALL_DONE : CTRUPDATE_INSTALL_FAILED;
    running = 0;
    return NULL;
}

void CTRUPDATE_SetAppPath(const char *path)
{
    app_path[0] = 0;
    if (!path || !strchr(path, '/') || path[strlen(path) - 1] == '/') return;
    /* Only the SD card is ours to write: not 3dslink:/ or romfs:/. */
    const char *sd = ctrupdate_sys_sd_root();
    if (!strncmp(path, "sdmc:/", 6)) snprintf(app_path, sizeof app_path, "%s%s", sd, path + 6);
    else if (!strncmp(path, "sd:/", 4)) snprintf(app_path, sizeof app_path, "%s%s", sd, path + 4);
    else if (path[0] == '/') snprintf(app_path, sizeof app_path, "%s%s", sd, path + 1);
    else if (!strchr(path, ':')) snprintf(app_path, sizeof app_path, "%s%s", sd, path);
}

int CTRUPDATE_IsInstalledTitle(void) { return ctrupdate_sys_is_cia(); }

int CTRUPDATE_CanInstall(void) { return state == CTRUPDATE_AVAILABLE && asset[0]; }

int CTRUPDATE_Install(void)
{
    CTRUPDATE_Wait(2000); /* the check thread may still be returning */
    if (running || !CTRUPDATE_CanInstall()) return -1;
    if (install_state == CTRUPDATE_INSTALL_DOWNLOADING || install_state == CTRUPDATE_INSTALL_EXTRACTING) return -1;
    cancelled = 0;
    progress = 0;
    install_error[0] = 0;
    install_state = CTRUPDATE_INSTALL_DOWNLOADING;
    if (start_thread(install_thread) < 0) {
        install_state = CTRUPDATE_INSTALL_FAILED;
        fail("could not start");
        return -1;
    }
    return 0;
}

ctrupdate_install_state CTRUPDATE_InstallState(void) { return install_state; }
int CTRUPDATE_Progress(void) { return progress; }
const char *CTRUPDATE_InstallError(void) { return install_error; }
const char *CTRUPDATE_InstallDir(void) { return install_dir; }

/* ── Shared ──────────────────────────────────────────────────────────────── */

ctrupdate_state CTRUPDATE_State(void) { return state; }

ctrupdate_state CTRUPDATE_Wait(int timeout_ms)
{
    unsigned long long start = ctrupdate_sys_ms();
    while (running) { /* also covers a cancelled check whose thread is still winding down */
        if (timeout_ms >= 0 && ctrupdate_sys_ms() - start >= (unsigned long long)timeout_ms) break;
        ctrupdate_sys_sleep_ms(10);
    }
    return state;
}

void CTRUPDATE_Cancel(void)
{
    cancelled = 1;
    if (install_state == CTRUPDATE_INSTALL_IDLE &&
        (state == CTRUPDATE_CHECKING || state == CTRUPDATE_AVAILABLE))
        state = CTRUPDATE_CANCELLED;
}

int CTRUPDATE_TakeNotice(void)
{
    if (state != CTRUPDATE_AVAILABLE || noticed) return 0;
    noticed = 1;
    return 1;
}

const char *CTRUPDATE_Latest(void) { return latest; }
const char *CTRUPDATE_Current(void) { return current; }
const char *CTRUPDATE_Url(void) { return url; }
const char *CTRUPDATE_Message(void) { return message; }
