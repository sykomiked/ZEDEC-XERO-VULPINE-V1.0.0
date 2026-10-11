// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
package org.zedec.zxv.sample;

import org.springframework.boot.SpringApplication;
import org.springframework.boot.autoconfigure.SpringBootApplication;

/**
 * A small REST service on the ZXV starter. Run it from
 * {@code sdk/frameworks/spring-zxv} with
 * {@code mvn -pl zxv-spring-boot-sample spring-boot:run}.
 */
@SpringBootApplication
public class SampleApplication {
    /** Not instantiated by hand; Spring creates it. */
    protected SampleApplication() {
    }

    /**
     * Starts the service.
     *
     * @param args command-line arguments
     */
    public static void main(String[] args) {
        SpringApplication.run(SampleApplication.class, args);
    }
}
