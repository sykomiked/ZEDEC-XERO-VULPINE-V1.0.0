/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* notes_app_shim.h — the declarations kernel/src/apps/notes_app.c needs and
 * no header provides.
 *
 * notes_app.c uses notes_app_t, notes_entry_t, NOTES_MAX_ENTRIES,
 * NOTES_ENTRY_LEN and oseq_state_t through "apps.h", but apps.h declares
 * none of them, so the module does not compile on its own
 * (docs/AUDIT_REPORT.md). The host test force-includes this file
 * (gcc -include) into notes_app.c and test_notes.c so that the real
 * notes_app.c is what gets tested. The layout below is the minimal one
 * implied by every field notes_app.c touches; it belongs in apps.h.
 *
 * The sizes are chosen so a full list always fits notes_save()'s 4096-byte
 * buffer: 32 * (20 digits + '|' + 95 chars + '\n') = 3744 < 4096 - 128.
 */
#ifndef ZXV_TEST_NOTES_APP_SHIM_H
#define ZXV_TEST_NOTES_APP_SHIM_H

#include "apps.h"
#include "oseq_core.h"

#define NOTES_MAX_ENTRIES 32
#define NOTES_ENTRY_LEN   96

typedef struct notes_entry {
    uint64_t created_cycle;          /* OSEQ cycle the entry was committed on */
    char text[NOTES_ENTRY_LEN];
} notes_entry_t;

typedef struct notes_app {
    app_base_t base;
    vfs_state_t *vfs;
    oseq_state_t *oseq;
    char filename[64];
    notes_entry_t entries[NOTES_MAX_ENTRIES];
    uint32_t num_entries;
    uint32_t selected;
    char input_buf[NOTES_ENTRY_LEN];
    uint32_t input_len;
} notes_app_t;

void notes_init(notes_app_t *app, uint32_t gui_win, vfs_state_t *vfs, oseq_state_t *oseq);
void notes_handle_key(notes_app_t *app, char ch);
void notes_handle_special(notes_app_t *app, uint8_t scancode);
int32_t notes_save(notes_app_t *app);
int32_t notes_load(notes_app_t *app);
void notes_render(notes_app_t *app, gui_desktop_t *gui);

#endif
