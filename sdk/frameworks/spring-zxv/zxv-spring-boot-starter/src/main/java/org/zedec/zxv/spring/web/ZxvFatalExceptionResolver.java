// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
package org.zedec.zxv.spring.web;

import jakarta.servlet.http.HttpServletRequest;
import jakarta.servlet.http.HttpServletResponse;
import org.springframework.core.Ordered;
import org.springframework.web.servlet.HandlerExceptionResolver;
import org.springframework.web.servlet.ModelAndView;
import org.zedec.zxv.ZxvFatalError;
import org.zedec.zxv.spring.ZxvFatalHandler;

/**
 * Keeps Spring MVC from turning a {@link ZxvFatalError} into an ordinary
 * response. Spring wraps an {@link Error} from a handler in a
 * {@code ServletException}, and an application's
 * {@code @ExceptionHandler(Exception.class)} would then match it and answer
 * as if nothing had happened. This resolver runs before every
 * {@code @ExceptionHandler} (order {@code HIGHEST_PRECEDENCE + 1}, right
 * after Boot's {@code DefaultErrorAttributes} records the error), trips the
 * {@link ZxvFatalHandler}, and rethrows the error so it leaves the
 * {@code DispatcherServlet}: the container answers 500 through Boot's error
 * page, which shows no exception text by default.
 */
public final class ZxvFatalExceptionResolver implements HandlerExceptionResolver, Ordered {
    private final ZxvFatalHandler handler;

    /**
     * Creates the resolver.
     *
     * @param handler trips liveness and readiness
     */
    public ZxvFatalExceptionResolver(ZxvFatalHandler handler) {
        this.handler = handler;
    }

    @Override
    public ModelAndView resolveException(HttpServletRequest request, HttpServletResponse response, Object h,
            Exception ex) {
        ZxvFatalError fatal = handler.inspect(ex).orElse(null);
        if (fatal != null) {
            throw fatal;
        }
        return null;
    }

    @Override
    public int getOrder() {
        return Ordered.HIGHEST_PRECEDENCE + 1;
    }
}
