/* Host-side test of version comparison, release parsing and zip unpacking:
 *   make -f tests/Makefile.host build/host_test && ./build/host_test [releases.json] */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include "miniz.h"
#include "3dsupdate_internal.h"

static int failures;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); failures++; } } while (0)

static int file_is(const char *path, const char *want)
{
    char got[64] = "";
    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    fread(got, 1, sizeof got - 1, f);
    fclose(f);
    return !strcmp(got, want);
}

static void make_zip(const char *zip, const char *const *entries)
{
    mz_zip_archive zw;
    memset(&zw, 0, sizeof zw);
    CHECK(mz_zip_writer_init_file(&zw, zip, 0));
    for (; *entries; entries += 2)
        mz_zip_writer_add_mem(&zw, entries[0], entries[1], strlen(entries[1]), MZ_DEFAULT_COMPRESSION);
    mz_zip_writer_finalize_archive(&zw);
    mz_zip_writer_end(&zw);
}

int main(int argc, char **argv)
{
    CHECK(CTRUPDATE_CompareVersions("v1.10", "1.9.2") > 0);
    CHECK(CTRUPDATE_CompareVersions("v1.0", "1.0.0") == 0);
    CHECK(CTRUPDATE_CompareVersions("1.0", "v1.0.1") < 0);
    CHECK(CTRUPDATE_CompareVersions("v2.0-beta", "2.0") == 0);

    char tag[48];
    const char *json = "[{\"tag_name\":\"latest\",\"prerelease\":false},"
                       "{\"tag_name\":\"v1.1.0\",\"draft\":false,\"prerelease\":false,\"body\":\"x \\\"tag_name\\\": \\\"v9\\\"\"},"
                       "{\"tag_name\":\"v2.0.0\",\"prerelease\":true},"
                       "{\"tag_name\":\"v1.0.0\",\"prerelease\":false}]";
    CHECK(ctrupdate_newest_tag(json, tag, sizeof tag) == 0);
    CHECK(!strcmp(tag, "v1.1.0"));
    CHECK(ctrupdate_newest_tag("[]", tag, sizeof tag) < 0);

    /* A release for every console: a .3dsx copy gets the 3DS zip, an installed
     * one the CIA; the Wii and Wii U files never win. */
    char asset[256];
    const char *rel =
        "[{\"tag_name\":\"v1.3\",\"assets\":["
        "{\"browser_download_url\":\"https://x/quatric/App-Wii/releases/download/v1.3/App-WUP.zip\"},"
        "{\"browser_download_url\":\"https://x/quatric/App-Wii/releases/download/v1.3/App-Wii.zip\"},"
        "{\"browser_download_url\":\"https://x/quatric/App-Wii/releases/download/v1.3/App-3DS.cia\"},"
        "{\"browser_download_url\":\"https://x/quatric/App-Wii/releases/download/v1.3/App.3dsx\"},"
        "{\"browser_download_url\":\"https://x/quatric/App-Wii/releases/download/v1.3/App-3DS.zip\"},"
        "{\"browser_download_url\":\"https://x/quatric/App-Wii/releases/download/v1.3/App.wuhb\"}],"
        "\"body\":\"\\\"browser_download_url\\\":\\\"https://evil/x-3ds.zip\\\"\"},"
        "{\"tag_name\":\"v1.2\",\"assets\":[{\"browser_download_url\":\"https://x/App-Wii.zip\"},"
        "{\"browser_download_url\":\"https://x/App.3dsx\"}]},"
        "{\"tag_name\":\"v1.0\",\"assets\":[{\"browser_download_url\":\"https://x/App.zip\"}]},"
        "{\"tag_name\":\"v0.9\",\"assets\":[{\"browser_download_url\":\"https://x/App-Wii.zip\"},"
        "{\"browser_download_url\":\"https://x/App-WiiU.zip\"},{\"browser_download_url\":\"https://x/App.dol\"}]}]";
    CHECK(ctrupdate_release_asset(rel, "v1.3", 0, asset, sizeof asset) == 0 && strstr(asset, "/App-3DS.zip"));
    CHECK(ctrupdate_release_asset(rel, "v1.3", 1, asset, sizeof asset) == 0 && strstr(asset, "/App-3DS.cia"));
    CHECK(ctrupdate_release_asset(rel, "v1.2", 0, asset, sizeof asset) == 0 && !strcmp(asset, "https://x/App.3dsx"));
    CHECK(ctrupdate_release_asset(rel, "v1.2", 1, asset, sizeof asset) < 0);
    CHECK(ctrupdate_release_asset(rel, "v1.0", 0, asset, sizeof asset) == 0 && !strcmp(asset, "https://x/App.zip"));
    CHECK(ctrupdate_release_asset(rel, "v0.9", 0, asset, sizeof asset) < 0);
    CHECK(ctrupdate_release_asset(rel, "v2", 0, asset, sizeof asset) < 0);

    /* Unpack: only 3ds/<Name>/ lands in the app folder, nothing escapes it. */
    const char *zip = "build/test.zip";
    static const char *const entries[] = {
        "README.md", "readme",
        "3ds/MyApp/MyApp.3dsx", "new 3dsx",
        "3ds/MyApp/data/level1.bin", "level",
        "3ds/MyApp/../../escape.txt", "bad",
        NULL,
    };
    make_zip(zip, entries);
    char name[64], err[128];
    CHECK(ctrupdate_zip_app_name(zip, name, sizeof name) == 0 && !strcmp(name, "MyApp"));
    system("rm -rf build/sd && mkdir -p build/sd/3ds/MyApp && echo old > build/sd/3ds/MyApp/MyApp.3dsx"
           " && echo keep > build/sd/3ds/MyApp/settings.txt");
    CHECK(ctrupdate_extract(zip, "build/sd/3ds/MyApp", "MyApp.3dsx", NULL, NULL, err, sizeof err) == 0);
    CHECK(file_is("build/sd/3ds/MyApp/MyApp.3dsx", "new 3dsx"));
    struct stat st;
    CHECK(stat("build/sd/3ds/MyApp/data/level1.bin", &st) == 0);
    CHECK(file_is("build/sd/3ds/MyApp/settings.txt", "keep\n"));
    CHECK(stat("build/sd/3ds/MyApp/README.md", &st) != 0);
    CHECK(stat("build/sd/escape.txt", &st) != 0 && stat("build/escape.txt", &st) != 0);
    CHECK(stat("build/sd/3ds/MyApp/MyApp.3dsx.wutmp", &st) != 0);
    CHECK(ctrupdate_extract("build/missing.zip", "build/sd/3ds/MyApp", NULL, NULL, NULL, err, sizeof err) < 0);

    /* A copy kept straight in sdmc:/3ds under another name is updated under that name. */
    system("rm -rf build/sd && mkdir -p build/sd/3ds && echo old > build/sd/3ds/Mine.3dsx");
    CHECK(ctrupdate_extract(zip, "build/sd/3ds", "Mine.3dsx", NULL, NULL, err, sizeof err) == 0);
    CHECK(file_is("build/sd/3ds/Mine.3dsx", "new 3dsx"));
    CHECK(stat("build/sd/3ds/MyApp.3dsx", &st) != 0);

    if (argc > 1) {
        FILE *f = fopen(argv[1], "rb");
        static char buf[1 << 20];
        size_t n = fread(buf, 1, sizeof buf - 1, f);
        buf[n] = 0;
        fclose(f);
        printf("%s -> %s\n", argv[1], ctrupdate_newest_tag(buf, tag, sizeof tag) ? "(none)" : tag);
        if (!ctrupdate_newest_tag(buf, tag, sizeof tag)) {
            if (!ctrupdate_release_asset(buf, tag, 0, asset, sizeof asset)) printf(".3dsx asset: %s\n", asset);
            if (!ctrupdate_release_asset(buf, tag, 1, asset, sizeof asset)) printf("CIA asset: %s\n", asset);
        }
    }
    printf(failures ? "%d failures\n" : "ok\n", failures);
    return failures != 0;
}
