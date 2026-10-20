/* The update popup's logic, shared by GUI apps: they only draw it. */
#include <stdio.h>
#include "3dsupdate.h"

/* KEY_A / KEY_B from <3ds/services/hid.h>, kept here so the popup logic
 * builds anywhere. */
#define BUTTON_A 0x0001
#define BUTTON_B 0x0002

static int open;

int CTRUPDATE_Popup(uint32_t down, ctrupdate_popup *p)
{
    if (CTRUPDATE_TakeNotice()) open = 1;
    if (!open) return CTRUPDATE_POPUP_CLOSED;

    int a = (down & BUTTON_A) != 0, b = (down & BUTTON_B) != 0;
    int result = CTRUPDATE_POPUP_OPEN;
    switch (CTRUPDATE_InstallState()) {
    case CTRUPDATE_INSTALL_IDLE:
        if (a && CTRUPDATE_CanInstall()) CTRUPDATE_Install();
        else if (a || b) open = 0;
        break;
    case CTRUPDATE_INSTALL_DOWNLOADING:
    case CTRUPDATE_INSTALL_EXTRACTING:
        if (b) CTRUPDATE_Cancel();
        break;
    case CTRUPDATE_INSTALL_DONE:
        if (a) result = CTRUPDATE_POPUP_EXIT;
        break;
    case CTRUPDATE_INSTALL_FAILED:
        if (a || b) open = 0;
        break;
    }
    if (!open) return CTRUPDATE_POPUP_CLOSED;

    /* Describe the state after this frame's input. */
    p->progress = -1;
    switch (CTRUPDATE_InstallState()) {
    case CTRUPDATE_INSTALL_IDLE:
        p->title = "Update available";
        snprintf(p->text, sizeof p->text, "%s", CTRUPDATE_Message());
        p->hint = CTRUPDATE_CanInstall() ? "A: Update now    B: Later" : "Press A to continue";
        p->detail = CTRUPDATE_CanInstall() ? "" : CTRUPDATE_Url();
        break;
    case CTRUPDATE_INSTALL_DOWNLOADING:
    case CTRUPDATE_INSTALL_EXTRACTING:
        p->title = "Updating...";
        snprintf(p->text, sizeof p->text, "%s %s",
                 CTRUPDATE_InstallState() == CTRUPDATE_INSTALL_DOWNLOADING ? "Downloading" : "Installing",
                 CTRUPDATE_Latest());
        p->hint = CTRUPDATE_InstallState() == CTRUPDATE_INSTALL_DOWNLOADING ? "B: Cancel" : "";
        p->detail = "";
        p->progress = CTRUPDATE_Progress();
        break;
    case CTRUPDATE_INSTALL_DONE:
        p->title = "Updated";
        snprintf(p->text, sizeof p->text, "Version %s is installed.", CTRUPDATE_Latest());
        p->hint = CTRUPDATE_IsInstalledTitle() ? "A: Back to the HOME Menu" : "A: Back to the Homebrew Launcher";
        p->detail = "";
        break;
    case CTRUPDATE_INSTALL_FAILED:
        p->title = "Update failed";
        snprintf(p->text, sizeof p->text, "Could not update: %s.", CTRUPDATE_InstallError());
        p->hint = "Press A to continue";
        p->detail = "";
        break;
    }
    return result;
}
