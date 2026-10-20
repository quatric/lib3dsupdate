/* End-to-end check against the real GitHub API from a desktop, using the
 * library's own TLS configuration and root certificates:
 *   make -f tests/Makefile.host build/host_check
 *   ./build/host_check quatric PictoChat-Client 1.0.0
 * With a fourth argument (argv[0] of a .3dsx, on the stand-in SD card build/sd)
 * it also installs the update there:
 *   ./build/host_check quatric PictoChat-Client 1.0.0 sdmc:/3ds/PictoChat/PictoChat.3dsx
 * With HOST_CIA=1 it acts as an installed title and "installs" the release's
 * CIA to build/sd/installed.cia (pass any fourth argument). */
#include <stdio.h>
#include <unistd.h>
#include "3dsupdate.h"

int main(int argc, char **argv)
{
    if (argc < 4) { fprintf(stderr, "usage: %s owner repo version [app_path]\n", argv[0]); return 2; }
    if (argc > 4) CTRUPDATE_SetAppPath(argv[4]);
    CTRUPDATE_Start(argv[1], argv[2], argv[3]);
    ctrupdate_state st = CTRUPDATE_Wait(30000);
    static const char *names[] = { "idle", "checking", "up to date", "available", "failed", "cancelled" };
    printf("%s: %s latest=%s installable=%d\n%s\n", CTRUPDATE_Url(), names[st], CTRUPDATE_Latest(),
           CTRUPDATE_CanInstall(), CTRUPDATE_Message());
    if (argc > 4 && CTRUPDATE_CanInstall()) {
        if (CTRUPDATE_Install() < 0) { printf("install did not start\n"); return 1; }
        int last = -1;
        ctrupdate_install_state ist;
        while ((ist = CTRUPDATE_InstallState()) == CTRUPDATE_INSTALL_DOWNLOADING || ist == CTRUPDATE_INSTALL_EXTRACTING) {
            if (CTRUPDATE_Progress() / 10 != last) printf("  %d%%\n", (last = CTRUPDATE_Progress() / 10) * 10);
            usleep(50000);
        }
        CTRUPDATE_Wait(5000);
        printf("install: %s %s -> %s\n", ist == CTRUPDATE_INSTALL_DONE ? "done" : "failed", CTRUPDATE_InstallError(),
               CTRUPDATE_InstallDir());
        return ist != CTRUPDATE_INSTALL_DONE;
    }
    return st == CTRUPDATE_FAILED;
}
