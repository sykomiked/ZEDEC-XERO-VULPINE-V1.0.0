/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zxv_thermal_host.h — this machine's temperature sensors, read when zxvd
 * runs hosted, for thermal.read (zxvd_power.h W9) and the apps behind it.
 * Every number comes from a sensor the OS exposes; nothing is estimated.
 *
 *   T1  SOURCES. Linux: every hwmon device's temp<N>_input (with its
 *       temp<N>_label and the device's name) under /sys/class/hwmon, and
 *       every thermal zone's temp (with its type) under /sys/class/thermal.
 *       macOS on Apple silicon: the HID event system's temperature services
 *       (IOHIDEventSystemClient, usage page 0xff00, usage 5); on Intel
 *       Macs: the SMC's sp78 temperature keys through IOKit. Windows and
 *       other hosts have no reader here and say so (T5). The macOS readers
 *       are compiled only on macOS: they are not compiled or tested in the
 *       Linux CI containers (docs/POWER.md, "Temperatures").
 *   T2  CLASSES. Each reading is cpu, gpu, case (board, chassis, chipset,
 *       ACPI zone) or other, from its label first and then its driver or
 *       zone type (zth_classify): coretemp, k10temp, k8temp, zenpower,
 *       x86_pkg_temp, cpu* and soc* are cpu; amdgpu, nouveau, radeon, i915,
 *       xe and gpu* are gpu; acpitz, nct*, it8*, w83*, f71*, pch_*, board*,
 *       dell_smm, thinkpad and asus* are case. A label naming the CPU
 *       (cpu, package, core, tctl, tdie, tccd, pacc, eacc, soc), the GPU
 *       (gpu) or the board (systin, auxtin, board, ambient, case, pch,
 *       chipset) wins over the driver; labels compare without case.
 *   T3  HOSTILE INPUT. A value file is read up to ZTH_VALUE_MAX bytes; a
 *       longer one is refused. A value is millidegrees Celsius written as
 *       an optional '-' and 1 to 9 decimal digits, then at most one
 *       newline; anything else is "not an integer". A value outside
 *       ZTH_MIN_MDEG..ZTH_MAX_MDEG (-50 C to +150 C) is "out of range". A
 *       value file that exists but cannot be read is "unreadable". Names
 *       and labels keep printable ASCII only (a byte outside 0x20..0x7e
 *       drops the whole name or label) and are cut at ZTH_NAME_MAX - 1
 *       bytes. Only directory entries named hwmon<digits>,
 *       thermal_zone<digits> and temp<digits>_input are followed, so a path
 *       never takes a name from the files it reads.
 *   T4  VERDICTS (kernel/src/lpres, four-valued). A reading that passes T3
 *       is TRUE; a refused one is FALSE, kept with its reason and no value.
 *       The report's state is the knowledge join (lpres_kjoin) over its
 *       readings: TRUE when every reading passed, FALSE when every one was
 *       refused, BOTH when some were refused, NEITHER when there was no
 *       reading at all (with the reason). The same holds per class.
 *   T5  NO SENSOR IS NOT ZERO. A host with no readable sensor reports
 *       NEITHER, an empty list and the reason; a class with no accepted
 *       reading has max_mdeg null. No value is ever made up.
 *   T6  BOUNDS. At most ZTH_MAX_DEVS hwmon devices, ZTH_MAX_DEVS thermal
 *       zones and ZTH_MAX_ENTRIES directory entries per device are looked
 *       at, and ZTH_MAX_READINGS readings kept; past that, readings are
 *       counted as dropped. Readings are sorted by path (digit runs compare
 *       as numbers), so the report does not depend on directory order.
 *   T7  zth_render_json writes the report as integer JSON:
 *       {"state", "host", "reason" (null unless NEITHER), "classes": {cpu,
 *       gpu, case, other: {"state", "n", "max_mdeg"}}, "readings": [{
 *       "class", "state", "mdeg" (null when refused), "source" (hwmon,
 *       thermal_zone, iohid or smc), "driver", "label", "path", "reason"
 *       (null when accepted)}], "rejected", "dropped"}.
 * Integers only: the macOS sensor API hands back a C double, which is
 * checked and turned into whole millidegrees at once (zxv_thermal_host.c).
 */
#ifndef ZXV_THERMAL_HOST_H
#define ZXV_THERMAL_HOST_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "lpres.h"

#define ZTH_MAX_READINGS 128u
#define ZTH_MAX_DEVS     64u
#define ZTH_MAX_ENTRIES  512u
#define ZTH_VALUE_MAX    32u /* bytes of a value file, newline included */
#define ZTH_NAME_MAX     40u
#define ZTH_PATH_MAX     160u
#define ZTH_ROOT_MAX     256u
#define ZTH_REASON_MAX   96u
#define ZTH_MIN_MDEG     (-50000)
#define ZTH_MAX_MDEG     150000

typedef enum { ZTH_CPU = 0, ZTH_GPU = 1, ZTH_CASE = 2, ZTH_OTHER = 3, ZTH_NCLASS = 4 } zth_class_t;

typedef enum {
    ZTH_SRC_HWMON = 0,
    ZTH_SRC_ZONE = 1,
    ZTH_SRC_IOHID = 2,
    ZTH_SRC_SMC = 3
} zth_source_t;

typedef struct {
    zth_class_t cls;
    zth_source_t src;
    lpres_state_t state; /* T4: TRUE accepted, FALSE refused */
    int32_t mdeg;        /* millidegrees C; 0 when refused */
    char driver[ZTH_NAME_MAX];
    char label[ZTH_NAME_MAX];
    char path[ZTH_PATH_MAX]; /* under the root, starting with '/' */
    const char *reason;      /* static text when refused, else NULL */
} zth_reading_t;

typedef struct {
    lpres_state_t state;
    uint32_t n;       /* accepted readings */
    int32_t max_mdeg; /* the highest accepted; meaningless when n == 0 */
} zth_class_sum_t;

typedef struct {
    lpres_state_t state;         /* T4 */
    const char *host;            /* "linux", "macos", "windows" or "other" */
    char reason[ZTH_REASON_MAX]; /* T5: why there is no reading ("" otherwise) */
    zth_reading_t r[ZTH_MAX_READINGS];
    uint32_t n;
    uint32_t rejected; /* readings kept as FALSE */
    uint32_t dropped;  /* T6: readings past ZTH_MAX_READINGS */
    zth_class_sum_t cls[ZTH_NCLASS];
} zth_report_t;

/* T1: root NULL or "" reads this machine; a directory reads a Linux-style
 * fixture under it (tests; any POSIX host). Always fills out; returns its
 * state. */
lpres_state_t zth_read(const char *root, zth_report_t *out);

/* T2: driver (or zone type) and label may be NULL or "". */
zth_class_t zth_classify(const char *driver, const char *label);
const char *zth_class_name(zth_class_t c);

/* T3: buf holds n bytes of a value file. true with *mdeg set when it is a
 * strict integer; the range check is separate (zth_in_range). */
bool zth_parse_mdeg(const char *buf, size_t n, int32_t *mdeg);
bool zth_in_range(int32_t mdeg);

/* T7: bytes written (no NUL counted), or -1 when cap is too small. */
int zth_render_json(const zth_report_t *rep, char *out, size_t cap);

#endif /* ZXV_THERMAL_HOST_H */
