#include <stdio.h>
#include <string.h>
#include "event.h"

static int failures = 0;
#define CHECK(cond, msg) do { \
    if (cond) printf("PASS: %s\n", msg); \
    else { printf("FAIL: %s\n", msg); failures++; } \
} while (0)

int main(void) {
    printf("=== Basic push/poll ===\n");
    event_queue_t q;
    event_queue_init(&q);
    CHECK(event_queue_pending(&q) == 0, "fresh queue is empty");

    event_push(&q, EVENT_CLICK, 1, 2, 10, 20, 0);
    event_push(&q, EVENT_KEY_DOWN, 1, 0, 0, 0, 'x');
    CHECK(event_queue_pending(&q) == 2, "2 events pending after 2 pushes");

    gui_event_t ev;
    CHECK(event_poll(&q, &ev) == true, "poll succeeds on non-empty queue");
    CHECK(ev.type == EVENT_CLICK && ev.window_id == 1 && ev.widget_id == 2 && ev.x == 10 && ev.y == 20,
          "first polled event matches first pushed event (FIFO order)");
    CHECK(ev.seq == 1, "first event has seq 1 (monotonic ordinal starting at 1)");

    CHECK(event_poll(&q, &ev) == true, "second poll succeeds");
    CHECK(ev.type == EVENT_KEY_DOWN && ev.key == 'x', "second polled event matches second pushed event");
    CHECK(ev.seq == 2, "second event has seq 2 (monotonically increasing)");

    CHECK(event_poll(&q, &ev) == false, "poll on now-empty queue returns false");
    CHECK(event_queue_pending(&q) == 0, "queue is empty again");

    printf("\n=== Overflow behavior: drops oldest, keeps most recent ===\n");
    event_queue_init(&q);
    for (int i = 0; i < EVENT_QUEUE_SIZE + 5; i++) {
        event_push(&q, EVENT_CLICK, 0, (uint32_t)i, 0, 0, 0);
    }
    CHECK(event_queue_pending(&q) == EVENT_QUEUE_SIZE, "queue caps at EVENT_QUEUE_SIZE, doesn't grow unbounded");
    event_poll(&q, &ev);
    CHECK(ev.widget_id == 5, "oldest 5 events were dropped -- first remaining event is widget_id 5, not 0");

    printf("\n=== Naming ===\n");
    CHECK(strcmp(event_type_name(EVENT_CLICK), "EVENT_CLICK") == 0, "EVENT_CLICK name correct");
    CHECK(strcmp(event_type_name((event_type_t)999), "Unknown") == 0, "out-of-range type name is Unknown");

    if (failures == 0) printf("\n=== ALL EVENT QUEUE TESTS PASSED ===\n");
    else printf("\n=== %d FAILURE(S) ===\n", failures);
    return failures;
}
