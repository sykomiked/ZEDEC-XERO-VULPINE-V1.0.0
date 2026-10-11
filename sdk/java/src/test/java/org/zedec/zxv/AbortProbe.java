// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
package org.zedec.zxv;

/** Run in a child JVM by ContextLedgerTest: FatalPolicy.ABORT must halt the process with status 70. */
public final class AbortProbe {
    private AbortProbe() {}

    public static void main(String[] args) {
        ZxvContext ctx = ZxvContext.create(FatalPolicy.ABORT);
        Capability cap = ctx.rootCapability(Permission.all());
        ctx.halt("abort probe");
        try {
            cap.require(Permission.LEDGER_READ);
        } catch (Throwable t) {
            System.out.println("UNREACHABLE: caught " + t);
        } finally {
            System.out.println("UNREACHABLE: finally ran");
        }
        System.exit(0);
    }
}
