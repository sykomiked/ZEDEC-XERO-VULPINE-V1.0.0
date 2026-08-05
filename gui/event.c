#include "event.h"

void event_queue_init(event_queue_t *q) {
    q->head = 0;
    q->tail = 0;
    q->count = 0;
    q->next_seq = 1;
}

bool event_push(event_queue_t *q, event_type_t type, uint32_t window_id, uint32_t widget_id, int32_t x, int32_t y, char key) {
    if (q->count >= EVENT_QUEUE_SIZE) {
        q->head = (q->head + 1) % EVENT_QUEUE_SIZE;
        q->count--;
    }
    gui_event_t *event = &q->events[q->tail];
    event->seq = q->next_seq++;
    event->type = type;
    event->window_id = window_id;
    event->widget_id = widget_id;
    event->x = x;
    event->y = y;
    event->key = key;
    q->tail = (q->tail + 1) % EVENT_QUEUE_SIZE;
    q->count++;
    return true;
}

bool event_poll(event_queue_t *q, gui_event_t *out) {
    if (q->count == 0) {
        return false;
    }
    *out = q->events[q->head];
    q->head = (q->head + 1) % EVENT_QUEUE_SIZE;
    q->count--;
    return true;
}

uint32_t event_queue_pending(const event_queue_t *q) {
    return q->count;
}

const char *event_type_name(event_type_t type) {
    static const char *names[] = {
        "EVENT_NONE",
        "EVENT_CLICK",
        "EVENT_HOVER_ENTER",
        "EVENT_HOVER_EXIT",
        "EVENT_KEY_DOWN",
        "EVENT_WINDOW_CLOSE",
        "EVENT_WINDOW_FOCUS"
    };
    if (type < EVENT_NONE || type >= sizeof(names) / sizeof(names[0])) {
        return "Unknown";
    }
    return names[type];
}