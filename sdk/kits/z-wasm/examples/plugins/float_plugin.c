/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* float_plugin.c — a plugin that uses floating point, so Z-Wasm REFUSES it
 * at load time with a FLOAT_OP (or FLOAT_TYPE) reason naming the
 * instruction, the function and the byte offset. It exists to show that
 * message; a real plugin uses zxv.rat_* instead (see rational_plugin.c). */
#include "zxv_plugin.h"

ZXV_EXPORT("average_percent") int32_t average_percent(int64_t total, int64_t count) {
    double avg = (double) total / (double) count; /* f64.convert_i64_s, f64.div */
    return (int32_t) (avg * 100.0);               /* f64.mul, i32.trunc_f64_s  */
}
