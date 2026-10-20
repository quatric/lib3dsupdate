/* lib3dsupdate: check GitHub for a newer release of a 3DS homebrew app, and
 * optionally install it over the running copy, whether that is a .3dsx
 * started from the Homebrew Launcher or an installed CIA.
 *
 * The check runs on its own thread (network bring-up, HTTPS request to the
 * GitHub releases API, version comparison) so it never blocks the app:
 *
 *     CTRUPDATE_SetAppPath(argc > 0 ? argv[0] : NULL);
 *     CTRUPDATE_Start("quatric", "MyApp", "1.2.0");
 *     ...each frame...
 *     if (CTRUPDATE_TakeNotice()) show_popup(CTRUPDATE_Message());
 *
 * Failures are silent: no network or no reply just means no notice.
 *
 * Installing: CTRUPDATE_Install() and poll CTRUPDATE_InstallState() /
 * CTRUPDATE_Progress(). A .3dsx app gets the release's zip (or .3dsx)
 * unpacked over its folder on the SD card; an installed app gets the
 * release's .cia installed over itself. Once it reports
 * CTRUPDATE_INSTALL_DONE the app should exit.
 */
#ifndef CTRUPDATE_H
#define CTRUPDATE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    CTRUPDATE_IDLE = 0,     /* CTRUPDATE_Start not called yet */
    CTRUPDATE_CHECKING,     /* thread running */
    CTRUPDATE_UP_TO_DATE,   /* no release newer than the running version */
    CTRUPDATE_AVAILABLE,    /* CTRUPDATE_Latest() is newer */
    CTRUPDATE_FAILED,       /* no network, TLS or HTTP error, bad reply */
    CTRUPDATE_CANCELLED,    /* CTRUPDATE_Cancel was called */
} ctrupdate_state;

typedef enum {
    CTRUPDATE_INSTALL_IDLE = 0,
    CTRUPDATE_INSTALL_DOWNLOADING,
    CTRUPDATE_INSTALL_EXTRACTING,  /* unpacking the zip, or finishing the CIA install */
    CTRUPDATE_INSTALL_DONE,        /* the new version is in place */
    CTRUPDATE_INSTALL_FAILED,      /* see CTRUPDATE_InstallError(); the old copy is untouched */
} ctrupdate_install_state;

/* Start a background check of github.com/<owner>/<repo>/releases against
 * current_version (e.g. "1.2.0" or "v1.2.0"). Brings up the soc service for
 * the check if the app has not, and lets go of it afterwards. Returns 0 when
 * the thread started. Strings are copied. */
int CTRUPDATE_Start(const char *owner, const char *repo, const char *current_version);

ctrupdate_state CTRUPDATE_State(void);

/* Block until the check (or install) thread has finished, or timeout_ms
 * passes (<0 waits forever). Apps that call socInit themselves must wait
 * first: the 3DS allows only one socInit at a time. */
ctrupdate_state CTRUPDATE_Wait(int timeout_ms);

/* Stop caring about the result. The thread finishes in the background; its
 * result is discarded and the state becomes CTRUPDATE_CANCELLED. During an
 * install this stops the download. */
void CTRUPDATE_Cancel(void);

/* Returns 1 exactly once, the first time it is called after a newer release
 * was found. Meant for a GUI popup: poll it every frame. */
int CTRUPDATE_TakeNotice(void);

/* Newest release tag found, e.g. "v1.3.0" ("" until known). */
const char *CTRUPDATE_Latest(void);
/* The running version as passed to CTRUPDATE_Start. */
const char *CTRUPDATE_Current(void);
/* "github.com/<owner>/<repo>/releases" */
const char *CTRUPDATE_Url(void);
/* A short line for popups, e.g. "Version v1.3.0 is available (you have v1.2.0)." */
const char *CTRUPDATE_Message(void);

/* Tell the library where the running .3dsx lives: argv[0] from main
 * ("sdmc:/3ds/MyApp/MyApp.3dsx" from the Homebrew Launcher). Without it, a
 * .3dsx update goes to sdmc:/3ds/<folder named in the zip>/. Not needed for
 * an installed CIA. */
void CTRUPDATE_SetAppPath(const char *argv0);

/* 1 when the app runs as an installed title (CIA), 0 for a .3dsx. */
int CTRUPDATE_IsInstalledTitle(void);

/* 1 when an update was found and its release has an asset this copy can
 * install: a .cia for an installed title; a 3DS zip or a .3dsx otherwise. */
int CTRUPDATE_CanInstall(void);

/* Download and install the update on a background thread. CTRUPDATE_Cancel
 * stops the download; unpacking or finishing the CIA is not interrupted.
 * Returns 0 if started. */
int CTRUPDATE_Install(void);

ctrupdate_install_state CTRUPDATE_InstallState(void);
/* 0-100 over download and unpacking. */
int CTRUPDATE_Progress(void);
/* Why the install failed, e.g. "download failed" ("" otherwise). */
const char *CTRUPDATE_InstallError(void);
/* The folder the update was unpacked to ("" for a CIA). */
const char *CTRUPDATE_InstallDir(void);

/* Ready-made popup logic for GUI apps. Call once per frame with the KEY_*
 * bits pressed this frame (hidKeysDown()); while it returns OPEN, draw a box
 * from *popup and keep the buttons away from the app. A installs (or closes
 * the popup when there is nothing to install), B closes it or cancels the
 * download. EXIT means the update is installed and the user pressed A: leave
 * the app the way it normally exits. */
enum { CTRUPDATE_POPUP_CLOSED = 0, CTRUPDATE_POPUP_OPEN, CTRUPDATE_POPUP_EXIT };
typedef struct {
    const char *title;   /* "Update available", "Updating...", "Updated", "Update failed" */
    char text[160];      /* e.g. "Version v1.3.0 is available (you have v1.2.0)." */
    const char *detail;  /* a second line (the releases URL) or "" */
    const char *hint;    /* buttons, e.g. "A: Update now    B: Later", or "" */
    int progress;        /* 0-100 while updating (draw a bar), else -1 */
} ctrupdate_popup;
int CTRUPDATE_Popup(uint32_t keys_down, ctrupdate_popup *popup);

/* Compare two version strings numerically ("v1.10" > "1.9.2"). Leading text
 * before the first digit is skipped. Returns <0, 0 or >0. */
int CTRUPDATE_CompareVersions(const char *a, const char *b);

#ifdef __cplusplus
}
#endif

#endif
