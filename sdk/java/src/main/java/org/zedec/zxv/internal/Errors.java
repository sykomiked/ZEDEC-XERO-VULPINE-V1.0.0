// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
package org.zedec.zxv.internal;

import java.lang.foreign.MemorySegment;
import org.zedec.zxv.BudgetExhaustedException;
import org.zedec.zxv.CapabilityDeniedException;
import org.zedec.zxv.FatalPolicy;
import org.zedec.zxv.FloatRejectedException;
import org.zedec.zxv.InsufficientFundsException;
import org.zedec.zxv.NotImplementedException;
import org.zedec.zxv.StatusClass;
import org.zedec.zxv.VerificationFailedException;
import org.zedec.zxv.ZxvContext;
import org.zedec.zxv.ZxvException;
import org.zedec.zxv.ZxvFatalError;
import org.zedec.zxv.ZxvRefusedException;
import org.zedec.zxv.ZxvSystemException;
import org.zedec.zxv.ZxvUsageException;

/** Maps status codes to the SDK's exception family (SDK_DESIGN section 5). Internal. */
public final class Errors {
    private Errors() {}

    /** The static symbolic name of a status ("ZXV_E_FUNDS"). */
    public static String name(int status) {
        try {
            MemorySegment p = (MemorySegment) Native.status_name.invokeExact(status);
            return p.reinterpret(256).getString(0);
        } catch (Throwable t) {
            return "ZXV_E_UNKNOWN";
        }
    }

    /** The class of a status. */
    public static StatusClass statusClass(int status) {
        return StatusClass.fromCode(Ffi.call(Native.status_class, status));
    }

    /**
     * Builds the exception for a negative status. A FATAL status marks
     * {@code ctx} dead, applies its {@link FatalPolicy}, and is thrown here
     * as {@link ZxvFatalError}; other classes are returned for the caller to throw.
     */
    public static RuntimeException raise(int status, String detail, ZxvContext ctx) {
        String name = name(status);
        StatusClass cls = statusClass(status);
        if (cls == StatusClass.FATAL) {
            throw fatal(status, name, detail, ctx);
        }
        return switch (cls) {
            case USAGE -> status == -24 ? new FloatRejectedException(detail) : new ZxvUsageException(status, name, detail);
            case REFUSED -> switch (status) {
                case -17 -> new CapabilityDeniedException(status, name, detail);
                case -19 -> new BudgetExhaustedException(status, name, detail);
                case -6 -> new InsufficientFundsException(status, name, detail);
                case -21 -> new VerificationFailedException(status, name, detail);
                case -3 -> new NotImplementedException(status, name, detail);
                default -> new ZxvRefusedException(status, name, detail);
            };
            default -> new ZxvSystemException(status, name, detail);
        };
    }

    /** Marks the context dead and produces (or, under ABORT, enacts) the terminal error. */
    public static ZxvFatalError fatal(int status, String name, String detail, ZxvContext ctx) {
        FatalPolicy policy = FatalPolicy.THROW;
        if (ctx != null) {
            Access.core().markDead(ctx, name + ": " + detail);
            policy = ctx.fatalPolicy();
        }
        ZxvFatalError err = new ZxvFatalError(status, name, detail);
        if (policy == FatalPolicy.ABORT) {
            System.err.println("ZXV FATAL (FatalPolicy.ABORT): " + name + ": " + detail);
            System.err.flush();
            Runtime.getRuntime().halt(70);
        }
        return err;
    }

    /** A USAGE exception raised by the SDK itself (argument checks before any native call). */
    public static ZxvUsageException usage(int status, String detail) {
        return new ZxvUsageException(status, name(status), detail);
    }
}
