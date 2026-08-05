#include <stdio.h>
#include <string.h>
#include "apps.h"

static int failures = 0;
#define CHECK(cond, msg) do { \
    if (cond) printf("PASS: %s\n", msg); \
    else { printf("FAIL: %s\n", msg); failures++; } \
} while (0)

static vfs_state_t vfs;
static oseq_state_t oseq;

static void type_string(notes_app_t *a, const char *s) {
    while (*s) { notes_handle_key(a, *s); s++; }
}

int main(void) {
    vfs_init(&vfs);
    oseq_init(&oseq);
    oseq.current_cycle = 42;

    notes_app_t notes;
    notes_init(&notes, 0, &vfs, &oseq);
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
    CHECK(strcmp(notes.entries[2].text, "Test note") == 0, "backspace-corrected entry saved correctly ('Tst' -> backspace x2 -> 'T' + 'est note' = 'Test note')");

    printf("\n=== Persistence: fresh notes_app_t loads what was saved ===\n");
    {
        notes_app_t reloaded;
        notes_init(&reloaded, 0, &vfs, &oseq);
        CHECK(reloaded.num_entries == 3, "reloaded app has all 3 previously-saved entries");
        CHECK(strcmp(reloaded.entries[0].text, "Buy milk") == 0, "entry 0 round-trips correctly");
        CHECK(reloaded.entries[0].created_cycle == 42, "entry 0's cycle round-trips correctly");
        CHECK(strcmp(reloaded.entries[1].text, "Call Bob") == 0, "entry 1 round-trips correctly");
        CHECK(strcmp(reloaded.entries[2].text, "Test note") == 0, "entry 2 round-trips correctly");
    }

    printf("\n=== Deletion ===\n");
    notes.selected = 1; /* "Call Bob" */
    notes_handle_special(&notes, 0x53); /* Delete key */
    CHECK(notes.num_entries == 2, "1 entry removed via Delete key");
    CHECK(strcmp(notes.entries[0].text, "Buy milk") == 0, "entry 0 unaffected by deleting entry 1");
    CHECK(strcmp(notes.entries[1].text, "Test note") == 0, "entry that was at index 2 shifted down to index 1");

    {
        notes_app_t reloaded2;
        notes_init(&reloaded2, 0, &vfs, &oseq);
        CHECK(reloaded2.num_entries == 2, "deletion was persisted: reload shows only 2 entries");
    }

    if (failures == 0) printf("\n=== ALL NOTES APP TESTS PASSED ===\n");
    else printf("\n=== %d FAILURE(S) ===\n", failures);
    return failures;
}
