// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
package org.zedec.zxv.trust;

/** What an attestation is about (the kernel's {@code lpres_subject_type_t}, codes 0..14). */
public enum Subject {
    /** Source code. */
    SOURCE,
    /** A language dialect. */
    DIALECT,
    /** A dependency. */
    DEPENDENCY,
    /** A runtime. */
    RUNTIME,
    /** A data set. */
    DATA,
    /** A symbol. */
    SYMBOL,
    /** A model pack (for example a GGUF file). */
    MODEL_PACK,
    /** Geometry. */
    GEOMETRY,
    /** A parameter. */
    PARAMETER,
    /** A sensor. */
    SENSOR,
    /** An actuator. */
    ACTUATOR,
    /** A calibration. */
    CALIBRATION,
    /** A format. */
    FORMAT,
    /** A key. */
    KEY,
    /** Firmware. */
    FIRMWARE;

    /** The kernel code. */
    public int code() {
        return ordinal();
    }
}
