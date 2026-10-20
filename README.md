# lib3dsupdate

Checks GitHub for a newer release of a 3DS homebrew app and can install it in
place, built on libctru. Works for both ways a 3DS app runs: a `.3dsx` from
the Homebrew Launcher, and an installed CIA. The 3DS counterpart of
[libwiiupdate](https://github.com/quatric/libwiiupdate) and
[libwiiuupdate](https://github.com/quatric/libwiiuupdate).

- Runs on its own thread: brings up the network, asks the GitHub releases API
  over HTTPS and compares the newest release tag with the running version
- Ships its own small mbedTLS (TLS 1.2, certificate chain and host name
  verified against the bundled roots; dates are not checked since many
  console clocks are wrong). Its symbols are renamed inside the archive, so
  apps that link their own mbedTLS are unaffected
- Picks the highest numbered, non-prerelease tag, so rolling tags such as
  `latest` are ignored
- Failures are quiet: no network or no reply just means no notice
- Updates in place:
  - a `.3dsx` gets the release's 3DS zip (streamed to the SD card), whose
    `3ds/<Name>/` folder is unpacked over the app's folder. Every file is
    written under a temporary name first, so a failed download or unpack
    leaves the installed copy untouched
  - an installed title gets the release's `.cia` streamed straight into the
    system's installer, which only commits it once all of it arrived

## Build

Requires devkitPro with devkitARM and libctru.

```sh
export DEVKITPRO=/opt/devkitpro DEVKITARM=/opt/devkitpro/devkitARM
make            # produces lib/lib3dsupdate.a
make install    # copies into $DEVKITPRO/portlibs/3ds
```

## Usage

Start the check at launch and show a popup when it finds something:

```c
#include <3dsupdate.h>

int main(int argc, char **argv)
{
    ...
    CTRUPDATE_SetAppPath(argc > 0 ? argv[0] : NULL);
    CTRUPDATE_Start("quatric", "MyApp", APP_VERSION);

    while (aptMainLoop()) {
        hidScanInput();
        u32 down = hidKeysDown();
        ctrupdate_popup pop;
        int st = CTRUPDATE_Popup(down, &pop);
        if (st == CTRUPDATE_POPUP_EXIT) break;   /* installed: leave the app */
        else if (st == CTRUPDATE_POPUP_OPEN) {
            down = 0;                            /* the popup has the controls */
            draw_box(pop.title, pop.text, pop.detail, pop.hint);
            if (pop.progress >= 0) draw_progress(pop.progress);
        }
        ...
    }

    CTRUPDATE_Cancel();
    CTRUPDATE_Wait(5000);
```

`CTRUPDATE_Popup` takes `KEY_*` bits (`hidKeysDown()`). A updates (or closes
the popup when there is nothing to install), B closes it or cancels the
download.

Apps that draw their own popup can drive it with `CTRUPDATE_TakeNotice()`
(true once, when a newer release was found), `CTRUPDATE_Message()`
(`"Version v1.3.0 is available (you have v1.2.0)."`), `CTRUPDATE_Install()`,
`CTRUPDATE_InstallState()`, `CTRUPDATE_Progress()` and
`CTRUPDATE_InstallError()`.

### Sharing the network

The 3DS's `soc` service can only be started once per process: a second
`socInit` fails, and `socExit` shuts it down for everyone. So the library
starts `soc` only when the app has not, and stops it again as soon as its
check (or download) is done. An app with its own `socInit` should call
`CTRUPDATE_Wait()` first, so the two do not overlap:

```c
if (CTRUPDATE_State() == CTRUPDATE_CHECKING) CTRUPDATE_Wait(20000);
socInit(buffer, size);
```

If the app already has `soc` running, the library simply uses it.

### Installing

`CTRUPDATE_IsInstalledTitle()` tells the two cases apart: not started
through the Homebrew Launcher, and the running program is a title installed
on the SD card.

An installed title takes the release's `.cia`. The app needs the `am:net`
service in its RSF (`ServiceAccessControl`), which the usual homebrew RSF
templates already list.

A `.3dsx` takes, by file name:

1. a zip named for the 3DS (`3ds` in the name)
2. a `.3dsx`
3. any other zip, unless its name is for another console (`Wii`, `WUP`,
   `Switch`)

so one release can carry the Wii, Wii U and 3DS builds side by side. The
target folder is that of `argv[0]` as given to `CTRUPDATE_SetAppPath` (the
Homebrew Launcher passes e.g. `sdmc:/3ds/MyApp/MyApp.3dsx`), and the zip's
`.3dsx` is written under the running file's name, so a copy that was renamed
or kept straight in `sdmc:/3ds/` is updated in place rather than duplicated.
Without a path on the SD card (3dslink, some loaders), the update goes to
`sdmc:/3ds/<Name>/`. Files in the app folder that the zip does not contain,
such as saved settings, are left alone.

### Other calls

* `CTRUPDATE_State()`: `CHECKING`, `UP_TO_DATE`, `AVAILABLE`, `FAILED` or `CANCELLED`.
* `CTRUPDATE_Wait(ms)`: block until the check (or install) is done.
* `CTRUPDATE_Cancel()`: drop the result, or stop a download.
* `CTRUPDATE_Latest()`, `CTRUPDATE_Current()`, `CTRUPDATE_Url()`, `CTRUPDATE_CompareVersions(a, b)`.
* `CTRUPDATE_InstallDir()`: the folder the update went to (`""` for a CIA).

### Using it from an app

Add it as a git submodule, build it before the app and link it:

```sh
git submodule add https://github.com/quatric/lib3dsupdate.git lib3dsupdate
```

```make
LIBS    := -l3dsupdate $(LIBS)        # ahead of -lctru
LIBDIRS += $(CURDIR)/lib3dsupdate     # include/ and lib/

# after the $(BUILD) rule, so it stays the default goal
$(BUILD): lib3dsupdate-lib
.PHONY: lib3dsupdate-lib
lib3dsupdate-lib:
	@$(MAKE) --no-print-directory -C lib3dsupdate
```

The library builds from its own sources (mbedTLS and miniz are vendored), so
CI needs nothing beyond devkitARM with libctru and `submodules: true` on
checkout.

See `example/main.c` for a complete program.

## Tests

`tests/host_test.c` checks version comparison, release and asset parsing and
zip unpacking on a desktop; `build/host_check` runs the whole network path
against plain sockets for an end-to-end run against GitHub, and with an app
path (on `build/sd`, the stand-in SD card) also downloads and installs the
update there. `HOST_CIA=1` makes it act as an installed title, writing the
release's CIA to `build/sd/installed.cia`:

```sh
make -f tests/Makefile.host
./build/host_test
./build/host_check quatric PictoChat-Client 1.0.0
./build/host_check quatric PictoChat-Client 1.0.0 sdmc:/3ds/PictoChat/PictoChat.3dsx
HOST_CIA=1 ./build/host_check quatric PictoChat-Client 1.0.0 x
```

The example also runs in the Azahar emulator, which passes sockets through
to the host: as a bare `.3dsx` it takes the zip path, and once installed as
a CIA, the CIA path.

## Root certificates

`source/3dsupdate_certs.h` is generated from the Mozilla CA bundle:

```sh
curl -sSLf -o cacert.pem https://curl.se/ca/cacert.pem
python3 scripts/gen_certs.py cacert.pem > source/3dsupdate_certs.h
```

## License

AGPL-3.0, see `LICENSE`. mbedTLS in `vendor/mbedtls` is Apache-2.0 (or
GPL-2.0-or-later), see `vendor/mbedtls/LICENSE`. miniz in `vendor/miniz` is
MIT, see `vendor/miniz/LICENSE`.
