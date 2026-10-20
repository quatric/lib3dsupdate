/* lib3dsupdate example: checks quatric/PictoChat-Client for a newer release
 * than 0.0.1 and offers to install it, on the console. */
#include <stdio.h>
#include <3ds.h>
#include <3dsupdate.h>

#define OWNER   "quatric"
#define REPO    "PictoChat-Client"
#define VERSION "0.0.1"

int main(int argc, char **argv)
{
    gfxInitDefault();
    consoleInit(GFX_TOP, NULL);
    printf("lib3dsupdate example: %s/%s, running %s\n", OWNER, REPO, VERSION);
    printf("Running as %s\n\n", CTRUPDATE_IsInstalledTitle() ? "an installed title" : "a .3dsx");
    CTRUPDATE_SetAppPath(argc > 0 ? argv[0] : NULL);
    CTRUPDATE_Start(OWNER, REPO, VERSION);

    ctrupdate_popup pop;
    int last_state = -1, last_install = -1, last_progress = -1, done = 0;
    while (aptMainLoop()) {
        hidScanInput();
        u32 down = hidKeysDown();
        if (down & KEY_START) break;

        if (!done && (int)CTRUPDATE_State() != last_state) {
            last_state = CTRUPDATE_State();
            static const char *names[] = { "idle", "checking", "up to date", "update available", "could not check",
                                           "cancelled" };
            printf("check: %s\n", names[last_state]);
        }
        int st = CTRUPDATE_Popup(down, &pop);
        if (st == CTRUPDATE_POPUP_OPEN &&
            ((int)CTRUPDATE_InstallState() != last_install || pop.progress / 10 != last_progress / 10)) {
            last_install = CTRUPDATE_InstallState();
            last_progress = pop.progress;
            printf("%s: %s %s\n", pop.title, pop.text, pop.detail);
            if (pop.progress >= 0) printf("  %d%%\n", pop.progress);
            if (pop.hint[0]) printf("  %s\n", pop.hint);
        }
        if (st == CTRUPDATE_POPUP_EXIT) break;
        if (st == CTRUPDATE_POPUP_CLOSED && last_state > 1 && !done) {
            done = 1;
            printf("\nPress START to exit.\n");
        }
        gspWaitForVBlank();
    }

    CTRUPDATE_Cancel();
    CTRUPDATE_Wait(5000);
    gfxExit();
    return 0;
}
