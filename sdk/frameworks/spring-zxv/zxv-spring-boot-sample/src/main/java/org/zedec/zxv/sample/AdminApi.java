// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
package org.zedec.zxv.sample;

import org.springframework.boot.autoconfigure.condition.ConditionalOnProperty;
import org.springframework.http.ResponseEntity;
import org.springframework.web.bind.annotation.PostMapping;
import org.springframework.web.bind.annotation.RestController;
import org.zedec.zxv.ZxvContext;

/**
 * The operator kill switch, for demonstrating the fatal path. Off unless
 * {@code sample.halt-endpoint.enabled=true}, and unauthenticated: never
 * enable it outside a local demo.
 */
@RestController
@ConditionalOnProperty(name = "sample.halt-endpoint.enabled", havingValue = "true")
public class AdminApi {
    private final ZxvContext context;

    /**
     * Creates the controller.
     *
     * @param context the managed context
     */
    public AdminApi(ZxvContext context) {
        this.context = context;
    }

    /**
     * Halts the context. Every later ZXV call fails with ZxvFatalError,
     * health goes DOWN and liveness BROKEN.
     *
     * @return 202
     */
    @PostMapping("/admin/halt")
    public ResponseEntity<Void> halt() {
        context.halt("operator halt from the sample");
        return ResponseEntity.accepted().build();
    }
}
