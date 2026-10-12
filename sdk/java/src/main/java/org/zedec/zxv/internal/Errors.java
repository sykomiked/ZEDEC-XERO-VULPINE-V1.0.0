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
import org.zedec.zxv.cid.CidException;
import org.zedec.zxv.coin.NotADenominationException;
import org.zedec.zxv.freight.ErasureException;
import org.zedec.zxv.paradox.OwnerApprovalRequiredException;
import org.zedec.zxv.paradox.StepLimitException;
import org.zedec.zxv.treaty.TreatyException;
import org.zedec.zxv.vhub.LimitException;
import org.zedec.zxv.vhub.PegException;
import org.zedec.zxv.web4.ConsentException;

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
            case USAGE -> switch (status) {
                case -24 -> new FloatRejectedException(detail);
                case -32 -> new NotADenominationException(status, name, detail);
                case -33 -> new TreatyException(status, name, detail);
                default -> new ZxvUsageException(status, name, detail);
            };
            case REFUSED -> switch (status) {
                case -17 -> new CapabilityDeniedException(status, name, detail);
                case -19 -> new BudgetExhaustedException(status, name, detail);
                case -6 -> new InsufficientFundsException(status, name, detail);
                case -21 -> new VerificationFailedException(status, name, detail);
                case -3 -> new NotImplementedException(status, name, detail);
                // ABI 1.5
                case -34 -> new CidException(status, name, detail);
                case -35 -> new ConsentException(status, name, detail);
                case -36 -> new ErasureException(status, name, detail);
                case -37 -> new PegException(status, name, detail);
                case -38 -> new LimitException(status, name, detail);
                case -39 -> new StepLimitException(status, name, detail);
                case -40 -> new OwnerApprovalRequiredException(status, name, detail);
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
