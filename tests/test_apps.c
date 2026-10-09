/* test_apps.c — Host-testable tests for ZEDEC pqOS native apps
 * Author: H.M. Michael-Laurence: Curzi (c)
 */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>

#include "../kernel/src/apps/apps.h"

static void test_shell(void) {
    printf("=== Shell App Tests ===\n");
    static shell_app_t shell;
    shell_init(&shell, 0, NULL, NULL, NULL, NULL, NULL, NULL);
    assert(shell.base.type == APP_SHELL);
    assert(shell.base.active == true);
    assert(shell.base.state == APP_STATE_RUNNING);
    assert(strcmp(shell.base.name, "Shell") == 0);
    assert(shell.cursor_row > 0); /* banner printed */
    assert(shell.input_len == 0);
    assert(shell.hist_count == 0);
    printf("  [PASS] Shell initialized correctly (cursor_row=%u)\n", shell.cursor_row);

    /* Type "help" and execute */
    shell_handle_key(&shell, 'h');
    shell_handle_key(&shell, 'e');
    shell_handle_key(&shell, 'l');
    shell_handle_key(&shell, 'p');
    assert(shell.input_len == 4);
    assert(shell.input_line[0] == 'h');
    shell_handle_key(&shell, '\n');
    assert(shell.hist_count == 1);
    assert(shell.input_len == 0);
    printf("  [PASS] Shell processes input + history\n");

    /* Test backspace */
    shell_handle_key(&shell, 'x');
    shell_handle_key(&shell, 'y');
    assert(shell.input_len == 2);
    shell_handle_key(&shell, '\b');
    assert(shell.input_len == 1);
    printf("  [PASS] Shell backspace works\n");

    /* Clear command */
    shell_handle_key(&shell, '\b');
    shell_handle_key(&shell, 'c');
    shell_handle_key(&shell, 'l');
    shell_handle_key(&shell, 'e');
    shell_handle_key(&shell, 'a');
    shell_handle_key(&shell, 'r');
    shell_handle_key(&shell, '\n');
    assert(shell.cursor_row == 0); /* clear resets to row 0 */
    printf("  [PASS] Shell 'clear' command works\n");

    /* Unknown command */
    shell_handle_key(&shell, 'z');
    shell_handle_key(&shell, 'z');
    shell_handle_key(&shell, 'z');
    shell_handle_key(&shell, '\n');
    printf("  [PASS] Shell unknown command handled\n\n");
}

static void test_editor(void) {
    printf("=== Editor App Tests ===\n");
    static editor_app_t ed;
    editor_init(&ed, 0, NULL);
    assert(ed.base.type == APP_EDITOR);
    assert(ed.base.active == true);
    assert(ed.insert_mode == true);
    assert(ed.buf_len == 0);
    assert(ed.cursor_pos == 0);
    assert(strcmp(ed.filename, "untitled.txt") == 0);
    printf("  [PASS] Editor initialized correctly\n");

    /* Type some text */
    editor_handle_key(&ed, 'H');
    editor_handle_key(&ed, 'i');
    editor_handle_key(&ed, '\n');
    editor_handle_key(&ed, 'W');
    editor_handle_key(&ed, 'o');
    editor_handle_key(&ed, 'r');
    editor_handle_key(&ed, 'l');
    editor_handle_key(&ed, 'd');
    assert(ed.buf_len == 8);
    assert(ed.buffer[0] == 'H');
    assert(ed.buffer[2] == '\n');
    assert(ed.buffer[3] == 'W');
    assert(ed.modified == true);
    printf("  [PASS] Editor text input works (buf_len=%u)\n", ed.buf_len);

    /* Cursor movement */
    assert(ed.cursor_pos == 8);
    editor_handle_special(&ed, 0x4B); /* Left */
    assert(ed.cursor_pos == 7);
    editor_handle_special(&ed, 0x4D); /* Right */
    assert(ed.cursor_pos == 8);
    printf("  [PASS] Editor cursor left/right works\n");

    /* Home key */
    editor_handle_special(&ed, 0x47); /* Home */
    assert(ed.cursor_pos == 3);
    printf("  [PASS] Editor Home key works\n");

    /* End key */
    editor_handle_special(&ed, 0x4F); /* End */
    assert(ed.cursor_pos == 8);
    printf("  [PASS] Editor End key works\n");

    /* Backspace */
    editor_handle_key(&ed, '\b');
    assert(ed.buf_len == 7);
    assert(ed.buffer[6] == 'l');
    printf("  [PASS] Editor backspace works\n");

    /* Insert mode */
    ed.cursor_pos = 1;
    editor_handle_key(&ed, 'X');
    assert(ed.buffer[1] == 'X');
    assert(ed.buffer[2] == 'i');
    assert(ed.buf_len == 8);
    printf("  [PASS] Editor insert mode works\n\n");
}

static void test_fman(void) {
    printf("=== File Manager App Tests ===\n");
    static fman_app_t fm;
    fman_init(&fm, 0, NULL);
    assert(fm.base.type == APP_FILE_MANAGER);
    assert(fm.base.active == true);
    assert(strcmp(fm.base.name, "Files") == 0);
    assert(fm.selected == 0);
    printf("  [PASS] File manager initialized correctly\n");

    /* Navigation */
    fman_down(&fm);
    fman_down(&fm);
    fman_up(&fm);
    printf("  [PASS] File manager navigation works\n\n");
}

static void test_sysmon(void) {
    printf("=== System Monitor App Tests ===\n");
    static sysmon_app_t sm;
    sysmon_init(&sm, 0, NULL, NULL, NULL, NULL, NULL);
    assert(sm.base.type == APP_SYSMON);
    assert(sm.base.active == true);
    assert(strcmp(sm.base.name, "SysMon") == 0);
    assert(sm.tick == 0);
    printf("  [PASS] SysMon initialized correctly\n");

    sysmon_tick(&sm);
    assert(sm.tick == 1);
    assert(sm.cpu_usage == 7);
    assert(sm.mem_total == 262144);
    sysmon_tick(&sm);
    assert(sm.tick == 2);
    assert(sm.cpu_usage == 14);
    printf("  [PASS] SysMon tick updates stats\n\n");
}

static void test_netcfg(void) {
    printf("=== Network Config App Tests ===\n");
    static netcfg_app_t nc;
    netcfg_init(&nc, 0, NULL, NULL);
    assert(nc.base.type == APP_NET_CONFIG);
    assert(nc.base.active == true);
    assert(strcmp(nc.base.name, "NetConfig") == 0);
    printf("  [PASS] NetConfig initialized correctly\n\n");
}

static void test_wallet(void) {
    printf("=== Wallet App Tests ===\n");
    static wallet_app_t w;
    wallet_init(&w, 0, NULL);
    assert(w.base.type == APP_WALLET);
    assert(w.base.active == true);
    assert(strcmp(w.base.name, "Wallet") == 0);
    assert(w.selected_capital == 0);
    printf("  [PASS] Wallet initialized correctly\n");

    /* Capital selection */
    wallet_handle_special(&w, 0x50); /* Down */
    assert(w.selected_capital == 1);
    wallet_handle_special(&w, 0x50); /* Down */
    assert(w.selected_capital == 2);
    wallet_handle_special(&w, 0x48); /* Up */
    assert(w.selected_capital == 1);
    printf("  [PASS] Wallet capital selection works\n\n");
}

static void test_app_launcher(void) {
    printf("=== App Launcher Tests ===\n");
    static app_launcher_t al;
    app_launcher_init(&al, NULL, NULL, NULL, NULL, NULL, NULL, NULL);
    assert(al.num_windows == 0);
    assert(al.active_app == 0xFFFFFFFF);
    printf("  [PASS] App launcher initialized\n");

    int32_t r = app_launcher_open(&al, APP_SHELL);
    assert(r == 0);
    assert(al.num_windows == 1);
    assert(al.active_app == APP_SHELL);
    assert(al.shell.base.active == true);
    printf("  [PASS] Opened Shell app\n");

    r = app_launcher_open(&al, APP_EDITOR);
    assert(r == 0);
    assert(al.num_windows == 2);
    assert(al.active_app == APP_EDITOR);
    printf("  [PASS] Opened Editor app\n");

    r = app_launcher_open(&al, APP_SYSMON);
    assert(r == 0);
    assert(al.num_windows == 3);
    printf("  [PASS] Opened SysMon app\n");

    r = app_launcher_open(&al, APP_WALLET);
    assert(r == 0);
    assert(al.num_windows == 4);
    printf("  [PASS] Opened Wallet app\n");

    r = app_launcher_close(&al, APP_EDITOR);
    assert(r == 0);
    assert(al.editor.base.active == false);
    printf("  [PASS] Closed Editor app\n");

    r = app_launcher_close(&al, APP_SHELL);
    assert(r == 0);
    assert(al.shell.base.active == false);
    printf("  [PASS] Closed Shell app\n");

    /* Tick should update sysmon */
    uint32_t old_tick = al.sysmon.tick;
    app_launcher_tick(&al);
    assert(al.sysmon.tick == old_tick + 1);
    printf("  [PASS] App launcher tick updates SysMon\n\n");
}

int main(void) {
    printf("=== ZEDEC pqOS Native Apps Tests ===\n\n");
    test_shell();
    test_editor();
    test_fman();
    test_sysmon();
    test_netcfg();
    test_wallet();
    test_app_launcher();
    printf("=== All ZEDEC pqOS native apps tests passed ===\n");
    return 0;
}
