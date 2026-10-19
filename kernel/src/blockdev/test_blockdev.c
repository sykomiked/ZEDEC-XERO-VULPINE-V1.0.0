/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* test_blockdev.c — host test for the generic block-device record.
 *
 * Covers blockdev_init() (every field it sets, the 40-character model copy,
 * its NUL terminator and that nothing past model[40] is touched) and the two
 * inline dispatchers in include/blockdev.h (NULL device, absent device and a
 * missing callback each return -1 without calling a driver; otherwise the
 * driver sees the exact device, LBA and buffer and its return value comes
 * back unchanged). A recording fake driver stands in for ATA/virtio-blk.
 */
#include <stdio.h>
#include <string.h>
#include <stddef.h>
#include "blockdev.h"

static int failures = 0;
static int checks = 0;
#define CHECK(c, m)                                                                                \
    do {                                                                                           \
        checks++;                                                                                  \
        if (!(c)) {                                                                                \
            printf("[FAIL] %s (line %d)\n", m, __LINE__);                                          \
            failures++;                                                                            \
        } else                                                                                     \
            printf("[PASS] %s\n", m);                                                              \
    } while (0)

/* ---- a recording fake driver ---- */
static int g_reads, g_writes;
static block_device_t *g_dev_seen;
static uint32_t g_lba_seen;
static const void *g_buf_seen;
static int g_ret = 0;

static int fake_read(block_device_t *dev, uint32_t lba, uint8_t *buf)
{
    g_reads++;
    g_dev_seen = dev;
    g_lba_seen = lba;
    g_buf_seen = buf;
    buf[0] = (uint8_t) (lba & 0xFFu);
    return g_ret;
}

static int fake_write(block_device_t *dev, uint32_t lba, const uint8_t *buf)
{
    g_writes++;
    g_dev_seen = dev;
    g_lba_seen = lba;
    g_buf_seen = buf;
    return g_ret;
}

static void reset_calls(void)
{
    g_reads = g_writes = 0;
    g_dev_seen = NULL;
    g_lba_seen = 0;
    g_buf_seen = NULL;
}

static void test_init_fields(void)
{
    block_device_t d;
    memset(&d, 0xA5, sizeof d);
    blockdev_init(&d, fake_read, fake_write, 123456u, "QEMU HARDDISK");
    CHECK(d.present == true, "init: present set");
    CHECK(d.total_sectors == 123456u, "init: total_sectors stored");
    CHECK(d.read_sector == fake_read, "init: read callback stored");
    CHECK(d.write_sector == fake_write, "init: write callback stored");
    CHECK(d.driver_data == NULL, "init: driver_data cleared");
    CHECK(strcmp(d.model, "QEMU HARDDISK") == 0, "init: model copied");
    CHECK(d.model[13] == 0, "init: model NUL-terminated right after the text");
    CHECK((unsigned char) d.model[14] == 0xA5u, "init: nothing written past the terminator");

    /* empty model: just the terminator */
    memset(&d, 0xA5, sizeof d);
    blockdev_init(&d, NULL, NULL, 0u, "");
    CHECK(d.model[0] == 0, "init: empty model is an empty string");
    CHECK((unsigned char) d.model[1] == 0xA5u, "init: empty model writes one byte");
    CHECK(d.total_sectors == 0u && d.read_sector == NULL && d.write_sector == NULL,
          "init: zero sectors / NULL callbacks stored as given");
    CHECK(d.present == true, "init: present even with no callbacks");
}

static void test_init_model_bounds(void)
{
    /* exactly 40 characters: all copied, terminator at [40] */
    const char *m40 = "0123456789ABCDEFGHIJ0123456789abcdefghij";
    block_device_t d;
    memset(&d, 0xA5, sizeof d);
    blockdev_init(&d, fake_read, fake_write, 1u, m40);
    CHECK(memcmp(d.model, m40, 40) == 0, "model: 40 chars copied");
    CHECK(d.model[40] == 0, "model: terminator at [40]");

    /* 50 characters: truncated to 40, and model[40] is the terminator, not 'K' */
    const char *m50 = "0123456789ABCDEFGHIJ0123456789abcdefghijKLMNOPQRST";
    unsigned char *raw = (unsigned char *) &d;
    memset(&d, 0xA5, sizeof d);
    blockdev_init(&d, fake_read, fake_write, 1u, m50);
    CHECK(memcmp(d.model, m50, 40) == 0, "model: first 40 of 50 chars copied");
    CHECK(d.model[39] == 'j', "model: [39] is the 40th char");
    CHECK(d.model[40] == 0, "model: truncated at 40");
    CHECK(strlen(d.model) == 40u, "model: strlen 40 after truncation");
    /* the byte right after model[] (struct padding before driver_data) is untouched */
    size_t after = offsetof(block_device_t, model) + sizeof d.model;
    if (after < offsetof(block_device_t, driver_data))
        CHECK(raw[after] == 0xA5u, "model: nothing written past model[40]");
    CHECK(d.driver_data == NULL, "model: driver_data still cleared after a long model");
}

static void test_dispatch(void)
{
    uint8_t buf[BLOCKDEV_SECTOR_SIZE];
    block_device_t d;
    blockdev_init(&d, fake_read, fake_write, 64u, "fake");

    reset_calls();
    g_ret = 0;
    CHECK(blockdev_read(&d, 0x12345678u, buf) == 0, "read: returns driver value 0");
    CHECK(g_reads == 1 && g_writes == 0, "read: exactly one read callback");
    CHECK(g_dev_seen == &d && g_lba_seen == 0x12345678u && g_buf_seen == buf,
          "read: device, lba and buffer passed through");
    CHECK(buf[0] == 0x78u, "read: driver filled the caller's buffer");

    reset_calls();
    g_ret = 512;
    CHECK(blockdev_write(&d, 7u, buf) == 512, "write: returns driver value 512");
    CHECK(g_writes == 1 && g_reads == 0, "write: exactly one write callback");
    CHECK(g_dev_seen == &d && g_lba_seen == 7u && g_buf_seen == buf,
          "write: device, lba and buffer passed through");

    reset_calls();
    g_ret = -5;
    CHECK(blockdev_read(&d, 1u, buf) == -5, "read: driver error passes through");
    CHECK(blockdev_write(&d, 1u, buf) == -5, "write: driver error passes through");

    /* refusals: never reach a driver */
    reset_calls();
    CHECK(blockdev_read(NULL, 0u, buf) == -1, "read: NULL device refused");
    CHECK(blockdev_write(NULL, 0u, buf) == -1, "write: NULL device refused");
    d.present = false;
    CHECK(blockdev_read(&d, 0u, buf) == -1, "read: absent device refused");
    CHECK(blockdev_write(&d, 0u, buf) == -1, "write: absent device refused");
    d.present = true;
    d.read_sector = NULL;
    CHECK(blockdev_read(&d, 0u, buf) == -1, "read: missing read callback refused");
    CHECK(blockdev_write(&d, 0u, buf) == -5, "write: still works without read callback");
    g_writes = 0;
    d.read_sector = fake_read;
    d.write_sector = NULL;
    CHECK(blockdev_write(&d, 0u, buf) == -1, "write: missing write callback refused");
    CHECK(g_reads == 0 && g_writes == 0, "refusals: no driver callback ran");
}

int main(void)
{
    test_init_fields();
    test_init_model_bounds();
    test_dispatch();
    printf("test_blockdev: %d/%d checks passed\n", checks - failures, checks);
    return failures ? 1 : 0;
}
