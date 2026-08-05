/* event.h — Ordinal-tagged GUI event queue.
 * Every event carries a monotonically increasing ordinal (seq),
 * matching the codebase's OSEQ philosophy: event ORDER is a first-
 * class, exact property, not an incidental side effect of timing.
 *
 * Author: H.M. Michael-Laurence: Curzi (c)
 */
#ifndef ZXV_EVENT_H
#define ZXV_EVENT_H

#include <stdint.h>
#include <stdbool.h>
#include "../kernel/include/m5_types.h"

typedef enum {
    EVENT_NONE = 0,
    EVENT_CLICK,
    EVENT_HOVER_ENTER,
    EVENT_HOVER_EXIT,
    EVENT_KEY_DOWN,
    EVENT_WINDOW_CLOSE,
    EVENT_WINDOW_FOCUS,
} event_type_t;

#define EVENT_QUEUE_SIZE 64

typedef struct gui_event {
    ordinal_t seq;
    event_type_t type;
    uint32_t window_id;
    uint32_t widget_id;
    int32_t x, y;
    char key;
} gui_event_t;

typedef struct event_queue {
    gui_event_t events[EVENT_QUEUE_SIZE];
    uint32_t head;
    uint32_t tail;
    uint32_t count;
    ordinal_t next_seq;
} event_queue_t;

void event_queue_init(event_queue_t *q);
/* Pushes an event. If the queue is full, drops the OLDEST event to
 * make room (a live GUI queue should prefer recent input over stale
 * backlog) rather than silently discarding the new one. */
bool event_push(event_queue_t *q, event_type_t type, uint32_t window_id,
                 uint32_t widget_id, int32_t x, int32_t y, char key);
bool event_poll(event_queue_t *q, gui_event_t *out); /* pops oldest; false if empty */
uint32_t event_queue_pending(const event_queue_t *q);
const char *event_type_name(event_type_t t);

#endif
