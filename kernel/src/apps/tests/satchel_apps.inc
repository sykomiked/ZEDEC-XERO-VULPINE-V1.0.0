/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* Satchel's model against zxvd's real dispatcher: files.* on the scratch
 * Hoard, and fab.put, fab.send and fab.receive on zxvd's real fabrication
 * group with its store open in a folder of the scratch Hoard's own. */
#include "satchel_model.h"

static bool sa_write(const char *rel, const void *p, uint32_t n)
{
    uint64_t size;
    return zxf_write(rel, (const uint8_t *) p, n, ZXF_W_REPLACE, false, &size) == 0;
}

/* the whole of a file of the Hoard into buf; its length, or -1 */
static int64_t sa_read(const char *rel, uint8_t *buf, uint32_t cap)
{
    uint64_t off = 0, size = 0;
    uint32_t got = 0;
    do {
        if (zxf_read(rel, off, buf + off, cap - (uint32_t) off, false, &got, &size) != 0) return -1;
        off += got;
    } while (got && off < size && off < cap);
    return off == size ? (int64_t) off : -1;
}

APPS_TEST(satchel)
{
    static satchel_t S;
    static uint8_t wasm[20000], bundle[40000], back[40000];
    static char head[SATCHEL_HEAD_MAX + 1], want[SATCHEL_HEAD_MAX + 1], root[512], dir[600];
    static const char MANIFEST[] = "zxapp 1\nid demo\n";
    static const char ICON[] = "<svg xmlns=\"http://www.w3.org/2000/svg\"/>\n";
    static const char README[] = "A demo package.\n";
    for (uint32_t i = 0; i < sizeof wasm; i++) wasm[i] = (uint8_t) (i * 31u + 7u);

    /* zxvd's fabrication group, its store in the scratch Hoard */
    CHECK(zxf_root(root, sizeof root) == 0, "satchel: the scratch Hoard");
    snprintf(dir, sizeof dir, "%s/satchel-fab", root);
    CHECK(zxvd_rpc_add_group(zxvd_fab_group()) == 0 && zxvd_rpc_enable("fab") == 0 &&
              zxvd_fab_open(dir) == 0,
          "satchel: fabrication on and its store open");
    satchel_init(&S, &LINK);

    /* SM1: a package from a folder; its subfolders, bundles and recipes left out */
    CHECK(zxf_mkdir("satchel") == 0 && zxf_mkdir("satchel/pkg") == 0 &&
              zxf_mkdir("satchel/pkg/sub") == 0 && zxf_mkdir("satchel/out") == 0 &&
              sa_write("satchel/pkg/app.zxapp", MANIFEST, sizeof MANIFEST - 1u) &&
              sa_write("satchel/pkg/run.wasm", wasm, sizeof wasm) &&
              sa_write("satchel/pkg/icon.svg", ICON, sizeof ICON - 1u) &&
              sa_write("satchel/pkg/README.md", README, sizeof README - 1u) &&
              sa_write("satchel/pkg/old.zxpkg", "x", 1) && sa_write("satchel/pkg/old.fab", "x", 1),
          "satchel: the package's files");
    int before = sent;
    CHECK(satchel_build(&S, "satchel/out/demo.zxpkg") == ZXM_E_STATE &&
              satchel_sign(&S, "satchel/out/demo.fab", 1) == ZXM_E_STATE &&
              satchel_open(&S, "satchel/pkg", "bad name") == ZXM_E_ARG &&
              satchel_open(&S, "satchel/pkg", "") == ZXM_E_ARG &&
              satchel_open(&S, NULL, "demo") == ZXM_E_ARG && sent == before,
          "satchel: nothing open, a bad name, no folder: nothing asked");
    int rc = satchel_open(&S, "satchel/pkg", "demo");
    int64_t total =
        (int64_t) (sizeof MANIFEST - 1u + sizeof wasm + sizeof ICON - 1u + sizeof README - 1u);
    CHECK(rc == ZXM_OK && S.open && S.pkg.nfiles == 4 && S.pkg.total == total &&
              S.pkg.largest == (int64_t) sizeof wasm && !strcmp(S.pkg.file[0].name, "README.md") &&
              !strcmp(S.pkg.file[1].name, "app.zxapp") && !strcmp(S.pkg.file[3].name, "run.wasm"),
          "satchel: opened, four files in files.list's order: %d %u %s", rc, S.pkg.nfiles,
          LINK.err);
    CHECK(S.pkg.count[SATCHEL_K_MANIFEST] == 1 && S.pkg.count[SATCHEL_K_CODE] == 1 &&
              S.pkg.count[SATCHEL_K_IMAGE] == 1 && S.pkg.count[SATCHEL_K_TEXT] == 1 &&
              S.pkg.count[SATCHEL_K_OTHER] == 0 && S.pkg.file[3].kind == SATCHEL_K_CODE,
          "satchel: one file of each kind (witness.kind_count)");
    {
        int64_t sum = 0;
        for (uint32_t i = 0; i < S.pkg.nfiles; i++) sum += S.pkg.file[i].share;
        CHECK(S.pkg.file[3].share == (int64_t) sizeof wasm * 10000 / total && sum <= 10000 &&
                  sum > 9996,
              "satchel: each file's share (banker.share), floored: %lld", (long long) sum);
    }

    /* SM2: the index, exactly */
    int32_t h = satchel_index(&S.pkg, head, sizeof head);
    int k = snprintf(want, sizeof want,
                     "name demo\nfiles 4\nbytes %lld\nfile %u 4 README.md\nfile %u 1 "
                     "app.zxapp\nfile %u 3 icon.svg\nfile %u 2 run.wasm\nend\n",
                     (long long) total, (unsigned) (sizeof README - 1u),
                     (unsigned) (sizeof MANIFEST - 1u), (unsigned) (sizeof ICON - 1u),
                     (unsigned) sizeof wasm);
    CHECK(h == (int32_t) SATCHEL_FIRST + k && !strncmp(head, "zxpkg 1 ", 8) &&
              strtol(head + 8, NULL, 10) == h && head[16] == '\n' && !strcmp(head + 17, want) &&
              satchel_index(&S.pkg, want, SATCHEL_FIRST + 10u) == -1,
          "satchel: the index (SM2): %d %s", h, head);

    /* SM1 refusals: the package open stays */
    CHECK(zxf_mkdir("satchel/bad") == 0 && sa_write("satchel/bad/a b.txt", "x", 1) &&
              satchel_open(&S, "satchel/bad", "bad") == ZXM_E_ARG && S.fault == SATCHEL_F_NAME &&
              S.pkg.nfiles == 4 && !strcmp(S.pkg.name, "demo"),
          "satchel: a name with a space refused, the package open stays");
    CHECK(
        zxf_mkdir("satchel/empty") == 0 && sa_write("satchel/empty/a.txt", "x", 1) &&
            sa_write("satchel/empty/b.txt", "", 0) &&
            satchel_open(&S, "satchel/empty", "empty") == ZXM_E_ARG && S.fault == SATCHEL_F_EMPTY &&
            satchel_open(&S, "satchel/pkg/sub", "none") == ZXM_E_ARG && S.fault == SATCHEL_F_LIMITS,
        "satchel: a file of 0 bytes, and a folder of no files, refused");
    {
        char p[64];
        CHECK(zxf_mkdir("satchel/many") == 0, "satchel: a folder for 65 files");
        for (uint32_t i = 0; i < 65; i++) {
            snprintf(p, sizeof p, "satchel/many/f%02u.txt", i);
            sa_write(p, "x", 1);
        }
        CHECK(satchel_open(&S, "satchel/many", "many") == ZXM_E_ARG && S.fault == SATCHEL_F_LIMITS,
              "satchel: 65 files refused (SR1)");
        snprintf(p, sizeof p, "satchel/many/f64.txt");
        CHECK(zxf_rename(p, "satchel/f64.txt") == 0 &&
                  satchel_open(&S, "satchel/many", "many") == ZXM_OK && S.pkg.nfiles == 64 &&
                  S.pkg.total == 64 && S.pkg.file[0].share == 156,
              "satchel: 64 files taken: %s", LINK.err);
    }
    CHECK(zxf_mkdir("satchel/long") == 0 &&
              sa_write("satchel/long/"
                       "abcdefghijabcdefghijabcdefghijabcdefghijabcdefghijabcdefghij12345.txt",
                       "x", 1) &&
              satchel_open(&S, "satchel/long", "long") == ZXM_E_ARG && S.fault == SATCHEL_F_NAME,
          "satchel: a name past 64 bytes refused");

    /* SM2: the bundle */
    CHECK(satchel_open(&S, "satchel/pkg", "demo") == ZXM_OK, "satchel: the demo again");
    before = sent;
    CHECK(satchel_build(&S, "satchel/out/demo.zip") == ZXM_E_ARG &&
              satchel_build(&S, "") == ZXM_E_ARG && sent == before,
          "satchel: a bundle that is no .zxpkg: nothing asked");
    rc = satchel_build(&S, "satchel/out/demo.zxpkg");
    CHECK(rc == ZXM_OK && S.built && S.bundle_len == h + total && !strcmp(S.root, root) &&
              !strncmp(S.pkg.file[0].cid, "bafk", 4) &&
              strcmp(S.pkg.file[0].cid, S.pkg.file[3].cid),
          "satchel: built, one block per file: %d %s", rc, LINK.err);
    {
        int64_t n = sa_read("satchel/out/demo.zxpkg", bundle, sizeof bundle);
        uint32_t o = (uint32_t) h;
        memcpy(back, head, (size_t) h);
        memcpy(back + o, README, sizeof README - 1u), o += (uint32_t) sizeof README - 1u;
        memcpy(back + o, MANIFEST, sizeof MANIFEST - 1u), o += (uint32_t) sizeof MANIFEST - 1u;
        memcpy(back + o, ICON, sizeof ICON - 1u), o += (uint32_t) sizeof ICON - 1u;
        memcpy(back + o, wasm, sizeof wasm), o += (uint32_t) sizeof wasm;
        CHECK(n == (int64_t) o && !memcmp(bundle, back, o),
              "satchel: the bundle is the index, then every file, byte for byte (%lld)",
              (long long) n);
    }
    CHECK(satchel_build(&S, "satchel/out/demo.zxpkg") == ZXM_E_STATE &&
              sa_read("satchel/out/demo.zxpkg", bundle, sizeof bundle) == S.bundle_len,
          "satchel: never over a bundle that is there");
    CHECK(satchel_inspect(&S, "satchel/out/demo.zxpkg") == ZXM_OK && S.got.nfiles == 4 &&
              S.got.total == total && S.got.head_len == h && !strcmp(S.got.name, "demo") &&
              !strcmp(S.got.file[3].name, "run.wasm") && S.got.file[3].kind == SATCHEL_K_CODE,
          "satchel: its index read back (SM4): %s", LINK.err);

    /* SM3: signed */
    before = sent;
    CHECK(satchel_sign(&S, "satchel/out/demo.zxpkg", 1) == ZXM_E_ARG &&
              satchel_sign(&S, "satchel/out/demo.fab", -1) == ZXM_E_ARG &&
              satchel_sign(&S, "satchel/out/demo.fab", INT64_C(1000000000000000)) == ZXM_E_ARG &&
              sent == before && S.seq == 0,
          "satchel: a recipe that is no .fab, a time that is no time: nothing asked");
    rc = satchel_sign(&S, "satchel/out/demo.fab", INT64_C(1760000000000));
    CHECK(rc == ZXM_OK && S.is_signed && S.seq == 1 && S.stamp == INT64_C(1760000000000) &&
              !strncmp(S.cid, "bafk", 4) && S.recipe_len > 0 &&
              !strcmp(S.recipe, "satchel/out/demo.fab"),
          "satchel: the public recipe, signed by zxvd, record 1: %d %s", rc, LINK.err);
    char sender[80] = "";
    zxm_begin(&LINK, "fab.status");
    CHECK(zxm_call(&LINK) == ZXM_OK &&
              zxm_get_str(&LINK, LINK.result, "sign_id", sender, sizeof sender) == ZXM_OK &&
              strlen(sender) == SATCHEL_ID_HEX,
          "satchel: this zxvd's signing id: %s", sender);

    /* SM4: installed only with the owner's word, from a trusted sender */
    const char *ok_list[1] = {sender};
    const char *other[1] = {"0000000000000000000000000000000000000000000000000000000000000000"};
    const char *bad[1] = {"xyz"};
    CHECK(zxf_mkdir("satchel/inst") == 0, "satchel: a folder to install into");
    before = sent;
    CHECK(satchel_install(&S, "satchel/out/demo.fab", "satchel/inst/demo.zxpkg", ok_list, 1) ==
                  ZXM_E_STATE &&
              sent == before && !S.installed,
          "satchel: no word from the owner: nothing asked, nothing installed");
    satchel_confirm(&S, true);
    CHECK(satchel_install(&S, "satchel/out/demo.fab", "satchel/inst/demo.zxpkg", bad, 1) ==
                  ZXM_E_ARG &&
              satchel_install(&S, "satchel/out/demo.fab", "satchel/inst/demo.zxpkg", ok_list, 0) ==
                  ZXM_E_ARG &&
              satchel_install(&S, "satchel/out/demo.fab", "satchel/inst/demo.txt", ok_list, 1) ==
                  ZXM_E_ARG &&
              sent == before && S.confirmed,
          "satchel: a bad sender id, no trust list, no .zxpkg: nothing asked, the word kept");
    rc = satchel_install(&S, "satchel/out/demo.fab", "satchel/inst/demo.zxpkg", ok_list, 1);
    CHECK(rc == ZXM_OK && S.installed && !S.confirmed && !strcmp(S.sender, sender) &&
              S.got.nfiles == 4 && S.got.total == total &&
              !strcmp(S.installed_at, "satchel/inst/demo.zxpkg"),
          "satchel: installed from the trusted sender, the word spent: %d %s", rc, LINK.err);
    CHECK(sa_read("satchel/inst/demo.zxpkg", back, sizeof back) == S.bundle_len &&
              !memcmp(back, bundle, (size_t) S.bundle_len),
          "satchel: the installed bundle is the one signed, byte for byte");
    before = sent;
    CHECK(satchel_install(&S, "satchel/out/demo.fab", "satchel/inst/two.zxpkg", ok_list, 1) ==
                  ZXM_E_STATE &&
              sent == before,
          "satchel: the word is never carried over to a second install");
    satchel_confirm(&S, true);
    CHECK(satchel_install(&S, "satchel/out/demo.fab", "satchel/inst/demo.zxpkg", ok_list, 1) ==
                  ZXM_E_STATE &&
              !S.confirmed && !S.installed,
          "satchel: never over an installed bundle; the word spent all the same");
    satchel_confirm(&S, true);
    rc = satchel_install(&S, "satchel/out/demo.fab", "satchel/inst/other.zxpkg", other, 1);
    CHECK(rc == ZXM_E_REMOTE && LINK.err_code == -32085 && !S.installed &&
              sa_read("satchel/inst/other.zxpkg", back, sizeof back) == -1,
          "satchel: a sender not on the list is refused by zxvd, nothing written: %s", LINK.err);
    satchel_confirm(&S, true);
    satchel_confirm(&S, false);
    CHECK(satchel_install(&S, "satchel/out/demo.fab", "satchel/inst/three.zxpkg", ok_list, 1) ==
              ZXM_E_STATE,
          "satchel: the owner took the word back");

    /* SM4: an index that is not SM2's */
    CHECK(sa_write("satchel/out/note.zxpkg", "hello, this is no bundle", 24) &&
              satchel_inspect(&S, "satchel/out/note.zxpkg") == ZXM_E_ARG &&
              S.fault == SATCHEL_F_INDEX && S.got.nfiles == 4,
          "satchel: no index: refused, the last one kept");
    memcpy(back, bundle, (size_t) S.bundle_len);
    back[h - 17] = '9'; /* "file 20000 2 run.wasm" says 20009: no longer the sum, nor the bundle */
    CHECK(sa_write("satchel/out/bent.zxpkg", back, (uint32_t) S.bundle_len) &&
              satchel_inspect(&S, "satchel/out/bent.zxpkg") == ZXM_E_ARG &&
              S.fault == SATCHEL_F_INDEX,
          "satchel: an index that does not add up: refused");
    memcpy(back, bundle, (size_t) S.bundle_len);
    back[h - 15] = '3'; /* run.wasm called an image */
    CHECK(sa_write("satchel/out/kind.zxpkg", back, (uint32_t) S.bundle_len) &&
              satchel_inspect(&S, "satchel/out/kind.zxpkg") == ZXM_E_ARG &&
              S.fault == SATCHEL_F_INDEX && satchel_inspect(&S, "x.txt") == ZXM_E_ARG,
          "satchel: a kind its name does not have, a path that is no bundle: refused");
    zxvd_fab_close();
}
