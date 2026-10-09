/* test_ipc.c — IPC message passing test (host-side)
 *
 * Tests the IPC send/receive logic using the portable scheduler
 * from el0_sched.c. Verifies message delivery, queue overflow,
 * blocking receive, and wake-on-message behavior.
 *
 * Author: H.M. Michael-Laurence: Curzi (c)
 */
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>

/* Pull in the portable scheduler and IPC definitions */
#include "../../kernel/arch/arm64/el0_userspace.h"

/* Stub the ARM64-specific functions for host testing */
void proc_pt_init(void) {}
void proc_pt_free(uint64_t *l0_table) { (void)l0_table; }
void proc_switch_address_space(user_proc_t *p) { (void)p; }
void proc_restore_el0(cpu_context_t *ctx) { (void)ctx; }
void proc_enter_el0(user_proc_t *p) { (void)p; }
void proc_save_context(proc_scheduler_t *ps, cpu_context_t *ctx) { (void)ps; (void)ctx; }
void proc_configure_tcr_el1(void) {}
void proc_setup_ttbr1(void) {}
bool proc_map_page(user_proc_t *p, uint64_t va, uint64_t pa, bool w, bool x) {
    (void)p; (void)va; (void)pa; (void)w; (void)x; return true;
}
uint64_t *proc_alloc_page_table(void) { return 0; }

/* proc_create stub for host testing */
int32_t proc_create(proc_scheduler_t *ps, const char *name,
                     void (*entry_point)(void), uint32_t priority) {
    if (!ps || ps->num_procs >= MAX_USER_PROCS) return -1;
    uint32_t slot = 0;
    for (uint32_t i = 0; i < MAX_USER_PROCS; i++) {
        if (ps->procs[i].state == PROC_UNUSED) { slot = i; break; }
    }
    user_proc_t *proc = &ps->procs[slot];
    proc->pid = ps->next_pid++;
    int i;
    for (i = 0; i < 31 && name[i]; i++) proc->name[i] = name[i];
    proc->name[i] = 0;
    proc->state = PROC_READY;
    proc->priority = priority;
    proc->cpu_time_ticks = 0;
    proc->quantum_ticks = 10;
    proc->quantum_default = 10;
    proc->exit_code = 0;
    proc->syscall_result = 0;
    proc->ipc_queue.head = 0;
    proc->ipc_queue.tail = 0;
    proc->ipc_queue.count = 0;
    proc->ipc_queue.wait_sender = 0;
    proc->blocked_on_pid = 0;
    proc->sleep_until_tick = 0;
    proc->l0_table = 0;
    for (i = 0; i < 31; i++) proc->ctx.x[i] = 0;
    proc->ctx.sp = 0;
    proc->ctx.pc = (uint64_t)entry_point;
    proc->ctx.pstate = 0;
    ps->num_procs++;
    return (int32_t)proc->pid;
}

/* proc_sched_tick stub */
void proc_sched_tick(proc_scheduler_t *ps, cpu_context_t *ctx) {
    (void)ctx;
    if (!ps) return;
    ps->ticks++;
    proc_wake_eligible(ps);
}

static int tests_run = 0;
static int tests_passed = 0;

#define TEST(name) do { tests_run++; printf("  [TEST] %s... ", name); } while(0)
#define PASS() do { tests_passed++; printf("PASS\n"); } while(0)
#define FAIL(msg) do { printf("FAIL: %s\n", msg); return; } while(0)

/* ---- IPC functions from el0_userspace.c (copied for host testing) ---- */

int32_t ipc_send(proc_scheduler_t *ps, uint32_t dest_pid,
                  const uint8_t *payload, uint32_t length) {
    if (!ps || !payload || length > IPC_MAX_PAYLOAD) return -1;
    user_proc_t *dest = proc_get(ps, dest_pid);
    if (!dest) return -1;
    ipc_queue_t *q = &dest->ipc_queue;
    if (q->count >= IPC_QUEUE_DEPTH) return -1;
    ipc_message_t *msg = &q->messages[q->head];
    msg->sender_pid = proc_current(ps) ? proc_current(ps)->pid : 0;
    msg->length = length;
    for (uint32_t i = 0; i < length; i++)
        msg->payload[i] = payload[i];
    q->head = (q->head + 1) % IPC_QUEUE_DEPTH;
    q->count++;
    if (dest->state == PROC_BLOCKED &&
        (dest->blocked_on_pid == 0 || dest->blocked_on_pid == msg->sender_pid)) {
        dest->state = PROC_READY;
        dest->blocked_on_pid = 0;
    }
    return 0;
}

int32_t ipc_recv(proc_scheduler_t *ps, uint32_t sender_pid,
                  uint8_t *buffer, uint32_t max_len) {
    if (!ps || !buffer) return -1;
    user_proc_t *curr = proc_current(ps);
    if (!curr) return -1;
    ipc_queue_t *q = &curr->ipc_queue;
    if (q->count == 0) {
        curr->state = PROC_BLOCKED;
        curr->blocked_on_pid = sender_pid;
        return 0;
    }
    uint32_t idx = q->tail;
    for (uint32_t i = 0; i < q->count; i++) {
        ipc_message_t *msg = &q->messages[idx];
        if (sender_pid == 0 || msg->sender_pid == sender_pid) {
            uint32_t copy_len = msg->length;
            if (copy_len > max_len) copy_len = max_len;
            for (uint32_t j = 0; j < copy_len; j++)
                buffer[j] = msg->payload[j];
            uint32_t read = idx;
            while (read != q->head) {
                uint32_t next = (read + 1) % IPC_QUEUE_DEPTH;
                if (next == q->head) break;
                q->messages[read] = q->messages[next];
                read = next;
            }
            q->head = (q->head - 1 + IPC_QUEUE_DEPTH) % IPC_QUEUE_DEPTH;
            q->count--;
            return (int32_t)copy_len;
        }
        idx = (idx + 1) % IPC_QUEUE_DEPTH;
    }
    curr->state = PROC_BLOCKED;
    curr->blocked_on_pid = sender_pid;
    return 0;
}

void proc_wake_eligible(proc_scheduler_t *ps) {
    if (!ps) return;
    for (uint32_t i = 0; i < MAX_USER_PROCS; i++) {
        user_proc_t *p = &ps->procs[i];
        if (p->state == PROC_SLEEPING && ps->ticks >= p->sleep_until_tick) {
            p->state = PROC_READY;
        }
    }
}

/* ---- Tests ---- */

void test_send_recv_basic(void) {
    TEST("basic send/receive");
    proc_scheduler_t ps;
    proc_sched_init(&ps);

    int32_t pid_a = proc_create(&ps, "sender", (void*)0x1000, 1);
    int32_t pid_b = proc_create(&ps, "receiver", (void*)0x2000, 1);
    if (pid_a < 0 || pid_b < 0) FAIL("proc_create failed");

    /* Make A the current process */
    ps.procs[0].state = PROC_RUNNING;
    ps.current_pid = 0;

    /* Send message from A to B */
    const char *msg = "Hello IPC!";
    int32_t ret = ipc_send(&ps, (uint32_t)pid_b, (const uint8_t *)msg, 10);
    if (ret != 0) FAIL("ipc_send failed");

    /* Switch to B */
    ps.procs[0].state = PROC_READY;
    ps.procs[1].state = PROC_RUNNING;
    ps.current_pid = 1;

    /* Receive message */
    uint8_t buf[256];
    ret = ipc_recv(&ps, (uint32_t)pid_a, buf, 256);
    if (ret != 10) FAIL("ipc_recv returned wrong length");
    if (memcmp(buf, msg, 10) != 0) FAIL("message content mismatch");

    PASS();
}

void test_send_queue_full(void) {
    TEST("queue overflow rejection");
    proc_scheduler_t ps;
    proc_sched_init(&ps);

    int32_t pid_a = proc_create(&ps, "sender", (void*)0x1000, 1);
    int32_t pid_b = proc_create(&ps, "receiver", (void*)0x2000, 1);

    ps.procs[0].state = PROC_RUNNING;
    ps.current_pid = 0;

    /* Fill the queue */
    uint8_t data[16] = {0};
    for (int i = 0; i < IPC_QUEUE_DEPTH; i++) {
        int32_t ret = ipc_send(&ps, (uint32_t)pid_b, data, 16);
        if (ret != 0) FAIL("ipc_send failed before queue full");
    }

    /* One more should fail */
    int32_t ret = ipc_send(&ps, (uint32_t)pid_b, data, 16);
    if (ret != -1) FAIL("ipc_send should have returned -1 for full queue");

    PASS();
}

void test_recv_blocks_when_empty(void) {
    TEST("blocking receive on empty queue");
    proc_scheduler_t ps;
    proc_sched_init(&ps);

    int32_t pid_a = proc_create(&ps, "sender", (void*)0x1000, 1);
    int32_t pid_b = proc_create(&ps, "receiver", (void*)0x2000, 1);

    /* B is current, tries to receive from empty queue */
    ps.procs[1].state = PROC_RUNNING;
    ps.current_pid = 1;

    uint8_t buf[256];
    int32_t ret = ipc_recv(&ps, 0, buf, 256);
    if (ret != 0) FAIL("ipc_recv should return 0 when blocking");

    /* B should now be BLOCKED */
    if (ps.procs[1].state != PROC_BLOCKED) FAIL("receiver should be BLOCKED");

    PASS();
}

void test_send_wakes_blocked_receiver(void) {
    TEST("send wakes blocked receiver");
    proc_scheduler_t ps;
    proc_sched_init(&ps);

    int32_t pid_a = proc_create(&ps, "sender", (void*)0x1000, 1);
    int32_t pid_b = proc_create(&ps, "receiver", (void*)0x2000, 1);

    /* B tries to receive, blocks */
    ps.procs[1].state = PROC_RUNNING;
    ps.current_pid = 1;
    uint8_t buf[256];
    ipc_recv(&ps, 0, buf, 256);

    if (ps.procs[1].state != PROC_BLOCKED) FAIL("B should be blocked");

    /* A sends a message */
    ps.procs[0].state = PROC_RUNNING;
    ps.current_pid = 0;
    const char *msg = "Wake up!";
    ipc_send(&ps, (uint32_t)pid_b, (const uint8_t *)msg, 8);

    /* B should now be READY */
    if (ps.procs[1].state != PROC_READY) FAIL("B should be READY after send");

    PASS();
}

void test_sleep_wake(void) {
    TEST("sleep and wake on timer");
    proc_scheduler_t ps;
    proc_sched_init(&ps);

    int32_t pid = proc_create(&ps, "sleeper", (void*)0x1000, 1);

    ps.procs[0].state = PROC_RUNNING;
    ps.current_pid = 0;

    /* Set process to sleep for 50 ticks */
    ps.procs[0].state = PROC_SLEEPING;
    ps.procs[0].sleep_until_tick = 50;

    /* Tick 30 times — should still be sleeping */
    for (int i = 0; i < 30; i++) {
        cpu_context_t ctx = {0};
        proc_sched_tick(&ps, &ctx);
    }
    if (ps.procs[0].state != PROC_SLEEPING) FAIL("should still be sleeping at tick 30");

    /* Tick to 50 — should wake */
    for (int i = 0; i < 20; i++) {
        cpu_context_t ctx = {0};
        proc_sched_tick(&ps, &ctx);
    }
    if (ps.procs[0].state != PROC_READY) FAIL("should be READY at tick 50");

    PASS();
}

void test_multiple_messages(void) {
    TEST("multiple messages in order");
    proc_scheduler_t ps;
    proc_sched_init(&ps);

    int32_t pid_a = proc_create(&ps, "sender", (void*)0x1000, 1);
    int32_t pid_b = proc_create(&ps, "receiver", (void*)0x2000, 1);

    /* Send 3 messages */
    ps.procs[0].state = PROC_RUNNING;
    ps.current_pid = 0;
    ipc_send(&ps, (uint32_t)pid_b, (const uint8_t *)"MSG1", 4);
    ipc_send(&ps, (uint32_t)pid_b, (const uint8_t *)"MSG2", 4);
    ipc_send(&ps, (uint32_t)pid_b, (const uint8_t *)"MSG3", 4);

    /* Receive them */
    ps.procs[0].state = PROC_READY;
    ps.procs[1].state = PROC_RUNNING;
    ps.current_pid = 1;

    uint8_t buf[256];
    int32_t r1 = ipc_recv(&ps, (uint32_t)pid_a, buf, 256);
    if (r1 != 4 || memcmp(buf, "MSG1", 4) != 0) FAIL("first message wrong");

    int32_t r2 = ipc_recv(&ps, (uint32_t)pid_a, buf, 256);
    if (r2 != 4 || memcmp(buf, "MSG2", 4) != 0) FAIL("second message wrong");

    int32_t r3 = ipc_recv(&ps, (uint32_t)pid_a, buf, 256);
    if (r3 != 4 || memcmp(buf, "MSG3", 4) != 0) FAIL("third message wrong");

    PASS();
}

int main(void) {
    printf("=== IPC Message Passing Tests ===\n\n");

    test_send_recv_basic();
    test_send_queue_full();
    test_recv_blocks_when_empty();
    test_send_wakes_blocked_receiver();
    test_sleep_wake();
    test_multiple_messages();

    printf("\n=== Results: %d/%d tests passed ===\n", tests_passed, tests_run);
    return tests_passed == tests_run ? 0 : 1;
}
