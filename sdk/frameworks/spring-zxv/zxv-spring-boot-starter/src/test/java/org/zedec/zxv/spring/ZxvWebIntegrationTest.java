// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
package org.zedec.zxv.spring;

import static org.assertj.core.api.Assertions.assertThat;

import org.junit.jupiter.api.MethodOrderer;
import org.junit.jupiter.api.Order;
import org.junit.jupiter.api.Test;
import org.junit.jupiter.api.TestMethodOrder;
import org.springframework.beans.factory.annotation.Autowired;
import org.springframework.boot.test.context.SpringBootTest;
import org.springframework.boot.test.web.client.TestRestTemplate;
import org.springframework.http.HttpEntity;
import org.springframework.http.HttpHeaders;
import org.springframework.http.HttpStatus;
import org.springframework.http.MediaType;
import org.springframework.http.ResponseEntity;
import org.springframework.test.annotation.DirtiesContext;
import org.zedec.zxv.spring.webapp.WebTestApp;

/**
 * A real servlet container: JSON exactness, capability checks over HTTP,
 * and the fatal path (500 despite a catch-all handler, health and liveness DOWN).
 */
@SpringBootTest(classes = WebTestApp.class, webEnvironment = SpringBootTest.WebEnvironment.RANDOM_PORT, properties = {
    "zxv.currencies=NGN",
    "zxv.capabilities.teller.permissions=LEDGER_READ,LEDGER_POST",
    "management.endpoints.web.exposure.include=health",
    "management.endpoint.health.show-details=always",
    "management.endpoint.health.probes.enabled=true",
    "management.endpoint.health.group.readiness.include=readinessState,zxv",
})
@DirtiesContext
@TestMethodOrder(MethodOrderer.OrderAnnotation.class)
class ZxvWebIntegrationTest {
    @Autowired
    private TestRestTemplate http;

    private ResponseEntity<String> post(String path, String json) {
        HttpHeaders h = new HttpHeaders();
        h.setContentType(MediaType.APPLICATION_JSON);
        return http.postForEntity(path, new HttpEntity<>(json, h), String.class);
    }

    @Test
    @Order(1)
    void exactJsonInAndOut() {
        ResponseEntity<String> r = post("/double", "{\"value\":\"0.1\"}");
        assertThat(r.getStatusCode()).isEqualTo(HttpStatus.OK);
        assertThat(r.getBody()).isEqualTo("{\"value\":\"0.2\"}");
        assertThat(post("/double", "{\"value\":\"1/3\"}").getBody()).isEqualTo("{\"value\":\"2/3\"}");
    }

    @Test
    @Order(2)
    void jsonFloatIsABadRequest() {
        ResponseEntity<String> r = post("/double", "{\"value\":0.1}");
        assertThat(r.getStatusCode()).isEqualTo(HttpStatus.BAD_REQUEST);
        assertThat(post("/double", "{\"value\":1e-1}").getStatusCode()).isEqualTo(HttpStatus.BAD_REQUEST);
        assertThat(post("/double", "{\"value\":\"0.1e0\"}").getStatusCode()).isEqualTo(HttpStatus.BAD_REQUEST);
    }

    @Test
    @Order(3)
    void queryParametersParseExactly() {
        assertThat(http.getForObject("/sum?a=0.1&b=0.2", String.class)).isEqualTo("{\"value\":\"0.3\"}");
    }

    @Test
    @Order(4)
    void capabilityChecksReachTheClient() {
        ResponseEntity<String> r = http.getForEntity("/sign", String.class);
        assertThat(r.getStatusCode()).isEqualTo(HttpStatus.FORBIDDEN);
        assertThat(r.getBody()).isEqualTo("ZXV_E_CAP");
    }

    @Test
    @Order(5)
    void healthyBeforeHalt() {
        ResponseEntity<String> r = http.getForEntity("/actuator/health", String.class);
        assertThat(r.getStatusCode()).isEqualTo(HttpStatus.OK);
        assertThat(r.getBody()).contains("\"zxv\":{\"status\":\"UP\"");
        assertThat(http.getForEntity("/actuator/health/liveness", String.class).getStatusCode()).isEqualTo(HttpStatus.OK);
    }

    @Test
    @Order(6)
    void fatalIsNeverSwallowed() {
        assertThat(http.postForEntity("/halt", null, String.class).getStatusCode()).isEqualTo(HttpStatus.OK);

        // The next call raises ZxvFatalError. The app's @ExceptionHandler(Throwable)
        // would answer 200 "swallowed"; the starter rethrows it first.
        ResponseEntity<String> r = http.getForEntity("/currencies", String.class);
        assertThat(r.getStatusCode()).isEqualTo(HttpStatus.INTERNAL_SERVER_ERROR);
        assertThat(r.getBody()).doesNotContain("swallowed").doesNotContain("ZXV_E_POISONED").doesNotContain("test");

        ResponseEntity<String> health = http.getForEntity("/actuator/health", String.class);
        assertThat(health.getStatusCode()).isEqualTo(HttpStatus.SERVICE_UNAVAILABLE);
        assertThat(health.getBody()).contains("\"zxv\":{\"status\":\"DOWN\"").contains("poisoned");
        assertThat(http.getForEntity("/actuator/health/liveness", String.class).getStatusCode())
                .isEqualTo(HttpStatus.SERVICE_UNAVAILABLE);
        assertThat(http.getForEntity("/actuator/health/readiness", String.class).getStatusCode())
                .isEqualTo(HttpStatus.SERVICE_UNAVAILABLE);

        // Exact arithmetic needs no context and keeps working.
        assertThat(http.getForObject("/sum?a=1&b=2", String.class)).isEqualTo("{\"value\":\"3\"}");
    }
}
