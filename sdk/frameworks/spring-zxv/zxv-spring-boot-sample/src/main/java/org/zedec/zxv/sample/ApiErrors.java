// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
package org.zedec.zxv.sample;

import org.springframework.http.HttpStatus;
import org.springframework.http.ProblemDetail;
import org.springframework.web.bind.annotation.ExceptionHandler;
import org.springframework.web.bind.annotation.RestControllerAdvice;
import org.springframework.web.servlet.mvc.method.annotation.ResponseEntityExceptionHandler;
import org.zedec.zxv.CapabilityDeniedException;
import org.zedec.zxv.InsufficientFundsException;
import org.zedec.zxv.ZxvException;
import org.zedec.zxv.ZxvRefusedException;
import org.zedec.zxv.ZxvUsageException;

/**
 * Maps ZXV refusals to RFC 9457 problem details by status class. Only the
 * stable status name goes to the client, never {@code detail()} (libzxv's
 * diagnostic text can name accounts and references).
 *
 * <p>Spring MVC's own exceptions (an unreadable body, such as a JSON float
 * where a {@code Rational} belongs) keep their standard 4xx answers through
 * {@link ResponseEntityExceptionHandler}. It also has a catch-all {@code Exception} handler, as many services do.
 * That handler never sees a {@code ZxvFatalError}: the starter's
 * {@code ZxvFatalExceptionResolver} runs first and rethrows it.
 */
@RestControllerAdvice
public class ApiErrors extends ResponseEntityExceptionHandler {
    /** Creates the advice. */
    public ApiErrors() {
    }

    /**
     * A capability check failed.
     *
     * @param e the refusal
     * @return 403
     */
    @ExceptionHandler(CapabilityDeniedException.class)
    public ProblemDetail denied(CapabilityDeniedException e) {
        return problem(HttpStatus.FORBIDDEN, "Capability denied", e);
    }

    /**
     * Not enough funds.
     *
     * @param e the refusal
     * @return 422
     */
    @ExceptionHandler(InsufficientFundsException.class)
    public ProblemDetail funds(InsufficientFundsException e) {
        return problem(HttpStatus.UNPROCESSABLE_ENTITY, "Insufficient funds", e);
    }

    /**
     * Any other refusal.
     *
     * @param e the refusal
     * @return 409
     */
    @ExceptionHandler(ZxvRefusedException.class)
    public ProblemDetail refused(ZxvRefusedException e) {
        return problem(HttpStatus.CONFLICT, "Refused", e);
    }

    /**
     * A caller bug (bad format, out of range).
     *
     * @param e the error
     * @return 400
     */
    @ExceptionHandler(ZxvUsageException.class)
    public ProblemDetail usage(ZxvUsageException e) {
        return problem(HttpStatus.BAD_REQUEST, "Invalid request", e);
    }

    /**
     * Everything else: a generic 500 without internals.
     *
     * @param e the exception
     * @return 500
     */
    @ExceptionHandler(Exception.class)
    public ProblemDetail other(Exception e) {
        return ProblemDetail.forStatusAndDetail(HttpStatus.INTERNAL_SERVER_ERROR, "Internal error");
    }

    private static ProblemDetail problem(HttpStatus status, String title, ZxvException e) {
        ProblemDetail p = ProblemDetail.forStatus(status);
        p.setTitle(title);
        p.setProperty("code", e.statusName());
        p.setProperty("class", e.statusClass().name());
        return p;
    }
}
