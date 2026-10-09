/* test_pterm.c — P-TERM Terminal Engine Tests (W6)
 *
 * Tests for virtual console multiplexer, command execution,
 * input handling, history, and phase-aware prompt.
 *
 * Author: 36N9 Genetics, LLC
 * License: SEL-3.3 (kernel component)
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "pterm.h"

static int tests_run = 0;
static int tests_passed = 0;
static int tests_failed = 0;

#define TEST(name) static void name(void)
#define RUN(name) do { \
    tests_run++; \
    printf("  [TEST] %s ... ", #name); \
    name(); \
    tests_passed++; \
    printf("PASS\n"); \
} while(0)

#define ASSERT(cond, msg) do { \
    if (!(cond)) { \
        printf("FAIL: %s\n", msg); \
        tests_failed++; \
        return; \
    } \
} while(0)

#define PASS() return

/* ===== Init Tests ===== */

TEST(init_test) {
    pterm_t t;
    pterm_init(&t);
    ASSERT(t.initialized, "initialized flag set");
    ASSERT(t.num_consoles == 0, "no consoles");
    ASSERT(t.active_console == 0, "active console is 0");
    ASSERT(t.num_commands == PTERM_CMD_MAX, "commands registered by init");
    ASSERT(t.history_count == 0, "no history");
    ASSERT(t.phase_state == 1, "default phase is TRUE");
    PASS();
}

/* ===== Console Tests ===== */

TEST(console_create_test) {
    pterm_t t;
    pterm_init(&t);
    int32_t idx = pterm_console_create(&t, "ph:1> ");
    ASSERT(idx >= 0, "console created");
    ASSERT(t.num_consoles == 1, "1 console");
    ASSERT(t.consoles[0].active, "console is active");
    ASSERT(strcmp(t.consoles[0].prompt, "ph:1> ") == 0, "prompt matches");
    ASSERT(t.consoles[0].cursor_x == 0, "cursor x is 0");
    ASSERT(t.consoles[0].cursor_y == 0, "cursor y is 0");
    ASSERT(t.consoles[0].input_len == 0, "input len is 0");
    PASS();
}

TEST(console_switch_test) {
    pterm_t t;
    pterm_init(&t);
    int32_t c0 = pterm_console_create(&t, "a> ");
    int32_t c1 = pterm_console_create(&t, "b> ");
    ASSERT(c0 >= 0 && c1 >= 0, "two consoles created");
    ASSERT(t.active_console == 0, "active is 0");

    int ret = pterm_switch_console(&t, 1);
    ASSERT(ret == 0, "switch succeeds");
    ASSERT(t.active_console == 1, "active is now 1");
    PASS();
}

TEST(console_switch_invalid_test) {
    pterm_t t;
    pterm_init(&t);
    pterm_console_create(&t, "a> ");
    int ret = pterm_switch_console(&t, 99);
    ASSERT(ret == -1, "invalid switch fails");
    ASSERT(t.active_console == 0, "active unchanged");
    PASS();
}

TEST(max_consoles_test) {
    pterm_t t;
    pterm_init(&t);
    for (int i = 0; i < PTERM_MAX_CONSOLES; i++) {
        ASSERT(pterm_console_create(&t, "> ") >= 0, "console created");
    }
    ASSERT(pterm_console_create(&t, "> ") == -1, "exceeding max fails");
    PASS();
}

/* ===== Input Tests ===== */

TEST(input_char_test) {
    pterm_t t;
    pterm_init(&t);
    pterm_console_create(&t, "> ");
    pterm_input_char(&t, 'h');
    pterm_input_char(&t, 'i');
    ASSERT(t.consoles[0].input_len == 2, "input len is 2");
    ASSERT(t.consoles[0].input_buf[0] == 'h', "input[0] is h");
    ASSERT(t.consoles[0].input_buf[1] == 'i', "input[1] is i");
    PASS();
}

TEST(input_string_test) {
    pterm_t t;
    pterm_init(&t);
    pterm_console_create(&t, "> ");
    int ret = pterm_input_string(&t, "hello");
    ASSERT(ret == 0, "input_string succeeds");
    ASSERT(t.consoles[0].input_len == 5, "input len is 5");
    ASSERT(t.consoles[0].input_buf[0] == 'h', "input[0] is h");
    ASSERT(t.consoles[0].input_buf[4] == 'o', "input[4] is o");
    PASS();
}

TEST(input_newline_executes_test) {
    pterm_t t;
    pterm_init(&t);
    pterm_console_create(&t, "> ");
    pterm_register_commands(&t);
    pterm_input_string(&t, "echo hello");
    pterm_input_char(&t, '\n');
    /* After newline, command should be executed and input cleared */
    ASSERT(t.consoles[0].input_len == 0, "input cleared after newline");
    PASS();
}

/* ===== Command Tests ===== */

TEST(register_commands_test) {
    pterm_t t;
    pterm_init(&t);
    pterm_register_commands(&t);
    ASSERT(t.num_commands == PTERM_CMD_MAX, "all commands registered");

    const pterm_command_t *cmd = pterm_find_command(&t, "help");
    ASSERT(cmd != NULL, "help command found");
    ASSERT(cmd->id == PTERM_CMD_HELP, "help command id matches");

    cmd = pterm_find_command(&t, "clear");
    ASSERT(cmd != NULL, "clear command found");
    ASSERT(cmd->id == PTERM_CMD_CLEAR, "clear command id matches");
    PASS();
}

TEST(find_nonexistent_command_test) {
    pterm_t t;
    pterm_init(&t);
    pterm_register_commands(&t);
    ASSERT(pterm_find_command(&t, "nonexistent") == NULL, "nonexistent not found");
    PASS();
}

TEST(execute_echo_test) {
    pterm_t t;
    pterm_init(&t);
    pterm_console_create(&t, "> ");
    pterm_register_commands(&t);
    int ret = pterm_execute_command(&t, "echo test123");
    ASSERT(ret == 0, "echo command succeeds");
    PASS();
}

TEST(execute_clear_test) {
    pterm_t t;
    pterm_init(&t);
    pterm_console_create(&t, "> ");
    pterm_register_commands(&t);
    pterm_write(&t, "some text");
    int ret = pterm_execute_command(&t, "clear");
    ASSERT(ret == 0, "clear command succeeds");
    PASS();
}

TEST(execute_help_test) {
    pterm_t t;
    pterm_init(&t);
    pterm_console_create(&t, "> ");
    pterm_register_commands(&t);
    int ret = pterm_execute_command(&t, "help");
    ASSERT(ret == 0, "help command succeeds");
    PASS();
}

TEST(execute_unknown_test) {
    pterm_t t;
    pterm_init(&t);
    pterm_console_create(&t, "> ");
    pterm_register_commands(&t);
    int ret = pterm_execute_command(&t, "nonexistent_cmd");
    ASSERT(ret != 0, "unknown command fails");
    PASS();
}

/* ===== Write/Render Tests ===== */

TEST(write_test) {
    pterm_t t;
    pterm_init(&t);
    pterm_console_create(&t, "> ");
    pterm_write(&t, "Hello World");
    /* Text should appear on the screen buffer */
    ASSERT(t.consoles[0].screen[0][0] == 'H', "screen[0][0] is H");
    ASSERT(t.consoles[0].screen[0][4] == 'o', "screen[0][4] is o");
    ASSERT(t.consoles[0].cursor_x == 11, "cursor advanced");
    PASS();
}

TEST(write_attr_test) {
    pterm_t t;
    pterm_init(&t);
    pterm_console_create(&t, "> ");
    pterm_write_attr(&t, "Colored", PTERM_ATTR_GREEN);
    ASSERT(t.consoles[0].screen[0][0] == 'C', "screen[0][0] is C");
    ASSERT(t.consoles[0].attr[0][0] == PTERM_ATTR_GREEN, "attr is green");
    PASS();
}

TEST(newline_test) {
    pterm_t t;
    pterm_init(&t);
    pterm_console_create(&t, "> ");
    pterm_write(&t, "Line 1");
    pterm_newline(&t);
    pterm_write(&t, "Line 2");
    ASSERT(t.consoles[0].screen[0][0] == 'L', "line 0 has L");
    ASSERT(t.consoles[0].screen[1][0] == 'L', "line 1 has L");
    ASSERT(t.consoles[0].cursor_y == 1, "cursor on line 1");
    PASS();
}

TEST(clear_test) {
    pterm_t t;
    pterm_init(&t);
    pterm_console_create(&t, "> ");
    pterm_write(&t, "text to clear");
    pterm_clear(&t);
    ASSERT(t.consoles[0].cursor_x == 0, "cursor x is 0");
    ASSERT(t.consoles[0].cursor_y == 0, "cursor y is 0");
    PASS();
}

/* ===== Phase Tests ===== */

TEST(set_phase_test) {
    pterm_t t;
    pterm_init(&t);
    pterm_set_phase(&t, 2);
    ASSERT(t.phase_state == 2, "phase is 2");
    pterm_set_phase(&t, 0);
    ASSERT(t.phase_state == 0, "phase is 0");
    PASS();
}

TEST(phase_prompt_test) {
    const char *p0 = pterm_phase_prompt(0);
    const char *p1 = pterm_phase_prompt(1);
    ASSERT(p0 != NULL, "phase 0 prompt not null");
    ASSERT(p1 != NULL, "phase 1 prompt not null");
    ASSERT(strcmp(p0, p1) != 0, "different phases have different prompts");
    PASS();
}

/* ===== History Tests ===== */

TEST(history_add_test) {
    pterm_t t;
    pterm_init(&t);
    pterm_history_add(&t, "ls");
    pterm_history_add(&t, "cat file");
    ASSERT(t.history_count == 2, "2 history entries");
    PASS();
}

TEST(history_prev_test) {
    pterm_t t;
    pterm_init(&t);
    pterm_history_add(&t, "cmd1");
    pterm_history_add(&t, "cmd2");
    const char *prev = pterm_history_prev(&t);
    ASSERT(prev != NULL, "history prev not null");
    ASSERT(strcmp(prev, "cmd2") == 0, "most recent is cmd2");
    PASS();
}

TEST(history_max_test) {
    pterm_t t;
    pterm_init(&t);
    for (int i = 0; i < PTERM_MAX_HISTORY + 5; i++) {
        pterm_history_add(&t, "cmd");
    }
    ASSERT(t.history_count == PTERM_MAX_HISTORY, "history capped at max");
    PASS();
}

/* ===== Main ===== */

int main(void) {
    printf("\n=== ZXV P-TERM (W6) Tests ===\n\n");

    RUN(init_test);
    RUN(console_create_test);
    RUN(console_switch_test);
    RUN(console_switch_invalid_test);
    RUN(max_consoles_test);
    RUN(input_char_test);
    RUN(input_string_test);
    RUN(input_newline_executes_test);
    RUN(register_commands_test);
    RUN(find_nonexistent_command_test);
    RUN(execute_echo_test);
    RUN(execute_clear_test);
    RUN(execute_help_test);
    RUN(execute_unknown_test);
    RUN(write_test);
    RUN(write_attr_test);
    RUN(newline_test);
    RUN(clear_test);
    RUN(set_phase_test);
    RUN(phase_prompt_test);
    RUN(history_add_test);
    RUN(history_prev_test);
    RUN(history_max_test);

    printf("\n=== Results: %d/%d passed, %d failed ===\n",
           tests_passed, tests_run, tests_failed);
    if (tests_failed == 0) {
        printf("ALL TESTS PASSED\n");
    }
    return tests_failed > 0 ? 1 : 0;
}
