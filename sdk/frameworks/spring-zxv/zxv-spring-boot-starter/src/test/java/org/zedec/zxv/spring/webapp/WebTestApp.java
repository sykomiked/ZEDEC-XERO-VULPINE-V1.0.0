// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
package org.zedec.zxv.spring.webapp;

import org.springframework.beans.factory.annotation.Qualifier;
import org.springframework.boot.autoconfigure.SpringBootApplication;
import org.springframework.http.HttpStatus;
import org.springframework.http.ResponseEntity;
import org.springframework.http.converter.HttpMessageNotReadableException;
import org.springframework.web.bind.annotation.ExceptionHandler;
import org.springframework.web.bind.annotation.GetMapping;
import org.springframework.web.bind.annotation.PostMapping;
import org.springframework.web.bind.annotation.RequestBody;
import org.springframework.web.bind.annotation.RequestParam;
import org.springframework.web.bind.annotation.RestController;
import org.springframework.web.bind.annotation.RestControllerAdvice;
import org.zedec.zxv.Capability;
import org.zedec.zxv.CapabilityDeniedException;
import org.zedec.zxv.Permission;
import org.zedec.zxv.Rational;
import org.zedec.zxv.ZxvContext;

/** A web application on the starter, with the kind of catch-all error handler real services have. */
@SpringBootApplication
public class WebTestApp {
    /** The request body. */
    public record Amount(Rational value) {}

    /** Endpoints. */
    @RestController
    public static class Endpoints {
        private final ZxvContext context;
        private final Capability teller;

        public Endpoints(ZxvContext context, @Qualifier("teller") Capability teller) {
            this.context = context;
            this.teller = teller;
        }

        @PostMapping("/double")
        public Amount twice(@RequestBody Amount a) {
            return new Amount(a.value().add(a.value()));
        }

        @GetMapping("/sum")
        public Amount sum(@RequestParam Rational a, @RequestParam Rational b) {
            return new Amount(a.add(b));
        }

        @GetMapping("/sign")
        public String sign() {
            teller.require(Permission.CRYPTO_SIGN);
            return "signed";
        }

        @GetMapping("/currencies")
        public Object currencies() {
            return context.enabledCurrencies();
        }

        @PostMapping("/halt")
        public void halt() {
            context.halt("test");
        }
    }

    /** Swallows everything it is allowed to see and answers 200. */
    @RestControllerAdvice
    public static class CatchAll {
        @ExceptionHandler(CapabilityDeniedException.class)
        public ResponseEntity<String> denied(CapabilityDeniedException e) {
            return ResponseEntity.status(HttpStatus.FORBIDDEN).body(e.statusName());
        }

        @ExceptionHandler(HttpMessageNotReadableException.class)
        public ResponseEntity<String> unreadable(HttpMessageNotReadableException e) {
            return ResponseEntity.badRequest().body("unreadable");
        }

        @ExceptionHandler(Throwable.class)
        public ResponseEntity<String> anything(Throwable t) {
            return ResponseEntity.ok("swallowed");
        }
    }
}
