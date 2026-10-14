/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* test_notes.c — host test for notes_app.c against the in-repo VFS stub
 * (vfs_stub.c). apps.h does not declare the notes types, so the build
 * force-includes tests/notes_app_shim.h (see that file). */
#include <stdio.h>
#include <string.h>
#include "apps.h"

static int failures = 0;
static int checks = 0;
#define CHECK(cond, msg)                                                                           \
    do {                                                                                           \
        checks++;                                                                                  \
        if (cond)                                                                                  \
            printf("PASS: %s\n", msg);                                                             \
        else {                                                                                     \
            printf("FAIL: %s\n", msg);                                                             \
            failures++;                                                                            \
        }                                                                                          \
    } while (0)

static vfs_state_t vfs;
static oseq_state_t oseq;

static void type_string(notes_app_t *a, const char *s)
{
    while (*s) {
        notes_handle_key(a, *s);
        s++;
    }
}

int main(void)
{
    vfs_init(&vfs);
    oseq_init(&oseq);
    oseq.current_cycle = 42;

    static notes_app_t notes;
    notes_init(&notes, 0, &vfs, &oseq);
    CHECK(notes.base.active && strcmp(notes.base.name, "Notes") == 0, "app initialized as 'Notes'");
    CHECK(notes.num_entries == 0, "fresh notes app has no entries (no prior file)");

    printf("\n=== Adding entries ===\n");
    type_string(&notes, "Buy milk");
    notes_handle_key(&notes, '\n');
    CHECK(notes.num_entries == 1, "1 entry after typing + Enter");
    CHECK(strcmp(notes.entries[0].text, "Buy milk") == 0, "entry text is correct");
    CHECK(notes.entries[0].created_cycle == 42, "entry tagged with the current OSEQ cycle (42)");
    CHECK(notes.input_len == 0, "input buffer cleared after commit");

    oseq.current_cycle = 100;
    type_string(&notes, "Call Bob");
    notes_handle_key(&notes, '\n');
    CHECK(notes.num_entries == 2, "2 entries after second commit");
    CHECK(notes.entries[1].created_cycle == 100, "second entry tagged with updated cycle (100)");

    printf("\n=== Backspace editing ===\n");
    type_string(&notes, "Tst");
    notes_handle_key(&notes, '\b');
    notes_handle_key(&notes, '\b');
    type_string(&notes, "est note");
    notes_handle_key(&notes, '\n');
    CHECK(strcmp(notes.entries[2].text, "Test note") == 0,
          "backspace-corrected entry saved correctly ('Tst' -> backspace x2 -> 'T' + 'est note' = "
          "'Test note')");

    printf("\n=== Persistence: fresh notes_app_t loads what was saved ===\n");
    {
        static notes_app_t reloaded;
        notes_init(&reloaded, 0, &vfs, &oseq);
        CHECK(reloaded.num_entries == 3, "reloaded app has all 3 previously-saved entries");
        CHECK(strcmp(reloaded.entries[0].text, "Buy milk") == 0, "entry 0 round-trips correctly");
        CHECK(reloaded.entries[0].created_cycle == 42, "entry 0's cycle round-trips correctly");
        CHECK(strcmp(reloaded.entries[1].text, "Call Bob") == 0, "entry 1 round-trips correctly");
        CHECK(strcmp(reloaded.entries[2].text, "Test note") == 0, "entry 2 round-trips correctly");
    }

    printf("\n=== Deletion ===\n");
    notes.selected = 1;                 /* "Call Bob" */
    notes_handle_special(&notes, 0x53); /* Delete key */
    CHECK(notes.num_entries == 2, "1 entry removed via Delete key");
    CHECK(strcmp(notes.entries[0].text, "Buy milk") == 0, "entry 0 unaffected by deleting entry 1");
    CHECK(strcmp(notes.entries[1].text, "Test note") == 0,
          "entry that was at index 2 shifted down to index 1");

    {
        static notes_app_t reloaded2;
        notes_init(&reloaded2, 0, &vfs, &oseq);
        CHECK(reloaded2.num_entries == 2, "deletion was persisted: reload shows only 2 entries");
    }

    printf("\n=== Selection bounds ===\n");
    notes.selected = 0;
    notes_handle_special(&notes, 0x48); /* Up at the top stays at 0 */
    CHECK(notes.selected == 0, "Up at the first entry stays at 0");
    notes_handle_special(&notes, 0x50);
    notes_handle_special(&notes, 0x50); /* Down past the end clamps */
    CHECK(notes.selected == 1, "Down clamps at the last entry");
    notes_handle_special(&notes, 0x53);
    CHECK(notes.num_entries == 1 && notes.selected == 0,
          "deleting the last entry moves the selection onto the new last entry");

    printf("\n=== Input limits ===\n");
    notes_handle_key(&notes, '\n');
    CHECK(notes.num_entries == 1, "Enter on empty input commits nothing");
    for (int i = 0; i < NOTES_ENTRY_LEN + 10; i++) notes_handle_key(&notes, 'x');
    CHECK(notes.input_len == NOTES_ENTRY_LEN - 1, "input is capped at NOTES_ENTRY_LEN - 1");
    notes_handle_key(&notes, '\n');
    CHECK(notes.num_entries == 2 && strlen(notes.entries[1].text) == NOTES_ENTRY_LEN - 1,
          "a full-length entry is stored intact");
    while (notes.num_entries < NOTES_MAX_ENTRIES) {
        notes_handle_key(&notes, 'y');
        notes_handle_key(&notes, '\n');
    }
    notes_handle_key(&notes, 'z');
    notes_handle_key(&notes, '\n');
    CHECK(notes.num_entries == NOTES_MAX_ENTRIES, "no commit past NOTES_MAX_ENTRIES");
    {
        static notes_app_t full;
        notes_init(&full, 0, &vfs, &oseq);
        CHECK(full.num_entries == NOTES_MAX_ENTRIES, "a full list round-trips through the file");
        CHECK(strcmp(full.entries[1].text, notes.entries[1].text) == 0,
              "the full-length entry round-trips");
    }

    printf("\n=== Malformed file lines are skipped ===\n");
    {
        static const char file[] = "7|ok one\ngarbage line\n|no cycle\n9|ok two";
        int32_t fd = vfs_open(&vfs, "notes.txt");
        CHECK(fd >= 0 && vfs_write(&vfs, fd, file, sizeof(file) - 1) == (int32_t)(sizeof(file) - 1),
              "wrote a hand-made notes file");
        vfs_close(&vfs, fd);
        static notes_app_t parsed;
        notes_init(&parsed, 0, &vfs, &oseq);
        CHECK(parsed.num_entries == 2, "two well-formed lines loaded, two malformed skipped");
        CHECK(parsed.entries[0].created_cycle == 7 && strcmp(parsed.entries[0].text, "ok one") == 0,
              "first good line parsed");
        CHECK(parsed.entries[1].created_cycle == 9 && strcmp(parsed.entries[1].text, "ok two") == 0,
              "last line without a trailing newline parsed");
    }

    printf("\n=== No VFS / no OSEQ ===\n");
    {
        static notes_app_t bare;
        notes_init(&bare, 0, NULL, NULL);
        CHECK(bare.num_entries == 0, "no VFS: starts empty");
        notes_handle_key(&bare, 'a');
        notes_handle_key(&bare, '\n');
        CHECK(bare.num_entries == 1 && bare.entries[0].created_cycle == 0,
              "no OSEQ: entry tagged with cycle 0");
        CHECK(notes_save(&bare) == -1, "no VFS: save reports failure");
    }

    printf("\n%d checks\n", checks);
    if (failures == 0)
        printf("\n=== ALL NOTES APP TESTS PASSED ===\n");
    else
        printf("\n=== %d FAILURE(S) ===\n", failures);
    return failures ? 1 : 0;
}
