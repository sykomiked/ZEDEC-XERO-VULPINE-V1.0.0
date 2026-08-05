/* chronicle_persist.c — durable Chronicle over ZXVFS. See chronicle_persist.h. */
#include "chronicle_persist.h"

#define CHRON_MAGIC    0x31524843u   /* 'CHR1' little-endian */
#define CHRON_FMT_VER  1u

/* ---- little-endian byte packing (a stable on-disk format, not memcpy) ---- */
static uint32_t put_u8(uint8_t *b, uint32_t at, uint8_t v)  { b[at] = v; return at + 1; }
static uint32_t put_u32(uint8_t *b, uint32_t at, uint32_t v) {
    for (int i = 0; i < 4; i++) b[at + i] = (uint8_t)(v >> (8 * i));
    return at + 4;
}
static uint32_t put_u64(uint8_t *b, uint32_t at, uint64_t v) {
    for (int i = 0; i < 8; i++) b[at + i] = (uint8_t)(v >> (8 * i));
    return at + 8;
}
static uint32_t put_hash(uint8_t *b, uint32_t at, const uint8_t *h) {
    for (uint32_t i = 0; i < CHRON_HASH_LEN; i++) b[at + i] = h[i];
    return at + CHRON_HASH_LEN;
}
static uint32_t get_u32(const uint8_t *b, uint32_t at, uint32_t *v) {
    *v = (uint32_t)b[at] | ((uint32_t)b[at+1] << 8) |
         ((uint32_t)b[at+2] << 16) | ((uint32_t)b[at+3] << 24);
    return at + 4;
}
static uint32_t get_u64(const uint8_t *b, uint32_t at, uint64_t *v) {
    uint64_t x = 0;
    for (int i = 0; i < 8; i++) x |= (uint64_t)b[at + i] << (8 * i);
    *v = x; return at + 8;
}
static uint32_t get_hash(const uint8_t *b, uint32_t at, uint8_t *h) {
    for (uint32_t i = 0; i < CHRON_HASH_LEN; i++) h[i] = b[at + i];
    return at + CHRON_HASH_LEN;
}

bool chronicle_save(const chronicle_t *c, zxvfs_t *fs, const char *name) {
    if (!c || !fs || !name) return false;
    if (c->n_entries > CHRON_PERSIST_MAX) return false;   /* would not fit one file */

    uint8_t buf[CHRON_PERSIST_HEADER + CHRON_PERSIST_MAX * CHRON_PERSIST_ENTRY];
    uint32_t at = 0;
    at = put_u32(buf, at, CHRON_MAGIC);
    at = put_u32(buf, at, CHRON_FMT_VER);
    at = put_u32(buf, at, c->n_entries);
    at = put_u64(buf, at, c->next_seq);
    at = put_hash(buf, at, c->head);
    for (uint32_t i = 0; i < c->n_entries; i++) {
        const chronicle_entry_t *e = &c->entry[i];
        at = put_u64(buf, at, e->seq);
        at = put_u8(buf, at, e->verdict);
        at = put_u8(buf, at, e->reason);
        at = put_hash(buf, at, e->event_digest);
        at = put_hash(buf, at, e->entry_hash);
    }
    /* one journaled, atomic write */
    return zxvfs_write(fs, name, buf, at) == 0;
}

bool chronicle_load(chronicle_t *c, zxvfs_t *fs, const char *name) {
    if (!c || !fs || !name) return false;
    chronicle_init(c);                                    /* start clean */

    uint8_t buf[CHRON_PERSIST_HEADER + CHRON_PERSIST_MAX * CHRON_PERSIST_ENTRY];
    int n = zxvfs_read(fs, name, buf, sizeof(buf));
    if (n < (int)CHRON_PERSIST_HEADER) return false;      /* missing/too short */

    uint32_t at = 0, magic = 0, ver = 0, ne = 0;
    at = get_u32(buf, at, &magic);
    at = get_u32(buf, at, &ver);
    if (magic != CHRON_MAGIC || ver != CHRON_FMT_VER) return false;
    at = get_u32(buf, at, &ne);
    if (ne > CHRON_MAX_ENTRIES || ne > CHRON_PERSIST_MAX) return false;
    /* the file must be exactly the size these fields imply */
    if ((uint32_t)n != CHRON_PERSIST_HEADER + ne * CHRON_PERSIST_ENTRY) return false;

    uint64_t next_seq = 0;
    at = get_u64(buf, at, &next_seq);
    at = get_hash(buf, at, c->head);
    for (uint32_t i = 0; i < ne; i++) {
        chronicle_entry_t *e = &c->entry[i];
        at = get_u64(buf, at, &e->seq);
        e->verdict = buf[at++];
        e->reason  = buf[at++];
        at = get_hash(buf, at, e->event_digest);
        at = get_hash(buf, at, e->entry_hash);
    }
    c->n_entries = ne;
    c->next_seq = next_seq;

    /* re-verify the whole chain end to end; refuse a corrupted/tampered image */
    if (!chronicle_verify(c)) { chronicle_init(c); return false; }
    return true;
}
