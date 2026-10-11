// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
package org.zedec.zxv.phase;

/** Health of a phase, as reported to the coordinator. */
public enum Health {
    /** Not reported. */
    UNKNOWN,
    /** Healthy. */
    HEALTHY,
    /** Degraded. */
    DEGRADED,
    /** Unhealthy. */
    UNHEALTHY
}
