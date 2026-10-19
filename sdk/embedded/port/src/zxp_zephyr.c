/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zxp_zephyr.c — the port on Zephyr (built by zephyr-zxv/, Kconfig ZXV).
 *
 * k_mutex in the libzxv object, a spinlock for the short critical
 * sections, k_uptime_ticks for time, a dedicated k_heap of
 * CONFIG_ZXV_HEAP_SIZE bytes for every libzxv allocation, k_panic on a
 * fatal error. Entropy follows CONFIG_ZXV_ENTROPY_* (see Kconfig): the
 * default is NONE, so zxp_init() fails closed until the application or the
 * Kconfig choice provides a source. */
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
#if defined(CONFIG_ZXV_ENTROPY_CSPRNG) || defined(CONFIG_ZXV_ENTROPY_INSECURE_TEST)
#    include <zephyr/random/random.h>
#endif
#include <zephyr/init.h>

#include "zxp.h"
#include "zxp_os.h"
#include "zxp_zephyr.h"

BUILD_ASSERT(sizeof(struct k_mutex) <= sizeof(((zxp_mutex_t *) 0)->os),
             "raise ZXP_MUTEX_WORDS for this struct k_mutex");

static struct k_mutex *native(zxp_mutex_t *m)
{
    return (struct k_mutex *) (void *) &m->os;
}

int zxp_os_mutex_create(zxp_mutex_t *m)
{
    return k_mutex_init(native(m));
}

void zxp_os_mutex_delete(zxp_mutex_t *m)
{
    (void) m;
}

int zxp_os_mutex_lock(zxp_mutex_t *m)
{
    return k_mutex_lock(native(m), K_FOREVER);
}

int zxp_os_mutex_unlock(zxp_mutex_t *m)
{
    return k_mutex_unlock(native(m));
}

static struct k_spinlock g_lock;
static k_spinlock_key_t g_key;

void zxp_os_crit_enter(void)
{
    g_key = k_spin_lock(&g_lock);
}

void zxp_os_crit_exit(void)
{
    k_spin_unlock(&g_lock, g_key);
}

void zxp_os_sleep_tick(void)
{
    k_sleep(K_TICKS(1));
}

uint64_t zxp_os_monotonic_us(void)
{
    return k_ticks_to_us_floor64((uint64_t) k_uptime_ticks());
}

void *zxp_os_task_self(void)
{
    return (void *) k_current_get();
}

K_HEAP_DEFINE(zxp_heap, CONFIG_ZXV_HEAP_SIZE);

void *zxp_os_alloc(void *user, size_t n)
{
    (void) user;
    return k_heap_aligned_alloc(&zxp_heap, 8, n, K_NO_WAIT);
}

void zxp_os_free(void *user, void *p)
{
    (void) user;
    k_heap_free(&zxp_heap, p);
}

void zxp_os_halt(const char *why)
{
    printk("zxp: fatal: %s\n", why);
    k_panic();
    for (;;) {
    }
}

#if defined(CONFIG_ZXV_ENTROPY_CSPRNG)
int zxp_zephyr_entropy_csrand(void *user, uint8_t *buf, size_t len)
{
    (void) user;
    return sys_csrand_get(buf, len) == 0 ? 0 : -1;
}
#endif

#if defined(CONFIG_ZXV_ENTROPY_INSECURE_TEST)
/* NOT A SECURE SOURCE. Emulators and CI only (TEST_RANDOM_GENERATOR). */
static int insecure_test_entropy(void *user, uint8_t *buf, size_t len)
{
    (void) user;
    sys_rand_get(buf, len);
    return 0;
}
#endif

#if defined(CONFIG_ZXV_AUTO_INIT)
static int zxp_zephyr_auto_init(void)
{
#    if defined(CONFIG_ZXV_ENTROPY_CSPRNG)
    (void) zxp_set_entropy(zxp_zephyr_entropy_csrand, NULL);
#    elif defined(CONFIG_ZXV_ENTROPY_INSECURE_TEST)
    printk("zxp: WARNING: INSECURE test entropy (CONFIG_ZXV_ENTROPY_INSECURE_TEST); "
           "never ship this configuration\n");
    (void) zxp_set_entropy(insecure_test_entropy, NULL);
#    endif
    int rc = zxp_init();
    if (rc != ZXP_OK)
        printk("zxp: init refused (%s): ZXV contexts will not open until an entropy source "
               "is configured\n",
               zxp_status_name(rc));
    return 0; /* boot continues; ZXV stays closed */
}
SYS_INIT(zxp_zephyr_auto_init, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);
#endif
