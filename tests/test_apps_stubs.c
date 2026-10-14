/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* test_apps_stubs.c — link stubs for host testing of kernel/src/apps/apps.c
 *
 * apps.c's launcher dispatches APP_EXCHANGE / APP_BANK / APP_CUSTOM to the
 * derivatives, assurance and treaty apps, whose sources
 * (derivatives_app.c, assurance_app.c, treaty_app.c) do not compile
 * (docs/AUDIT_REPORT.md). test_apps.c never opens those three apps, so
 * these stubs exist only to satisfy the linker; reaching any stub here means
 * the test exercised code that has no real implementation, and it aborts.
 */
#include <stdio.h>
#include <stdlib.h>

#include "../kernel/src/apps/apps.h"

static void stub_hit(const char *fn) {
    fprintf(stderr, "FAIL: unimplemented app entry point %s reached\n", fn);
    abort();
}

void derivatives_init(derivatives_app_t *a, uint32_t w, vino_ledger_t *v, vena_runtime_t *e) { (void)a; (void)w; (void)v; (void)e; stub_hit(__func__); }
void derivatives_handle_key(derivatives_app_t *a, char c) { (void)a; (void)c; stub_hit(__func__); }
void derivatives_handle_special(derivatives_app_t *a, uint8_t s) { (void)a; (void)s; stub_hit(__func__); }
void derivatives_render(derivatives_app_t *a, gui_desktop_t *g) { (void)a; (void)g; stub_hit(__func__); }
void derivatives_tick(derivatives_app_t *a) { (void)a; stub_hit(__func__); }

void assurance_init(assurance_app_t *a, uint32_t w, vino_ledger_t *v, vena_runtime_t *e) { (void)a; (void)w; (void)v; (void)e; stub_hit(__func__); }
void assurance_handle_key(assurance_app_t *a, char c) { (void)a; (void)c; stub_hit(__func__); }
void assurance_handle_special(assurance_app_t *a, uint8_t s) { (void)a; (void)s; stub_hit(__func__); }
void assurance_render(assurance_app_t *a, gui_desktop_t *g) { (void)a; (void)g; stub_hit(__func__); }
void assurance_tick(assurance_app_t *a) { (void)a; stub_hit(__func__); }

void treaty_init(treaty_app_t *a, uint32_t w, vino_ledger_t *v, vena_runtime_t *e) { (void)a; (void)w; (void)v; (void)e; stub_hit(__func__); }
void treaty_handle_key(treaty_app_t *a, char c) { (void)a; (void)c; stub_hit(__func__); }
void treaty_handle_special(treaty_app_t *a, uint8_t s) { (void)a; (void)s; stub_hit(__func__); }
void treaty_render(treaty_app_t *a, gui_desktop_t *g) { (void)a; (void)g; stub_hit(__func__); }
void treaty_tick(treaty_app_t *a) { (void)a; stub_hit(__func__); }

/* Directory operations: src/apps/vfs_stub.c (the in-repo single-directory
 * VFS stub this test links for open/read/write/close) has none, and
 * test_apps.c only drives the shell/file manager with no VFS attached. */
int32_t vfs_list_dir(vfs_state_t *v, const char *p, vfs_node_t *o, uint32_t m) { (void)v; (void)p; (void)o; (void)m; stub_hit(__func__); return -1; }
int32_t vfs_chdir(vfs_state_t *v, const char *p) { (void)v; (void)p; stub_hit(__func__); return -1; }
const char *vfs_get_cwd(vfs_state_t *v) { (void)v; stub_hit(__func__); return "/"; }
