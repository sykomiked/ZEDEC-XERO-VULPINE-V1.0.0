// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
package org.zedec.zxv.sample;

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

/** The sample end to end, over HTTP, against the real libzxv. */
@SpringBootTest(webEnvironment = SpringBootTest.WebEnvironment.RANDOM_PORT,
        properties = "sample.halt-endpoint.enabled=true")
@DirtiesContext
@TestMethodOrder(MethodOrderer.OrderAnnotation.class)
class SampleApplicationTest {
    @Autowired
    private TestRestTemplate http;

    private ResponseEntity<String> post(String path, String json) {
        HttpHeaders h = new HttpHeaders();
        h.setContentType(MediaType.APPLICATION_JSON);
        return http.postForEntity(path, new HttpEntity<>(json, h), String.class);
    }

    @Test
    @Order(1)
    void quotesExactly() {
        ResponseEntity<String> r = post("/quote", "{\"price\":\"12.75\",\"quantity\":\"1/3\"}");
        assertThat(r.getStatusCode()).isEqualTo(HttpStatus.OK);
        assertThat(r.getBody()).isEqualTo("{\"total\":\"4.25\"}");
        assertThat(http.getForObject("/sum?a=0.1&b=0.2", String.class)).isEqualTo("{\"total\":\"0.3\"}");
    }

    @Test
    @Order(2)
    void refusesFloats() {
        assertThat(post("/quote", "{\"price\":12.75,\"quantity\":\"1\"}").getStatusCode())
                .isEqualTo(HttpStatus.BAD_REQUEST);
        assertThat(http.getForEntity("/sum?a=0.1&b=1e3", String.class).getStatusCode())
                .isEqualTo(HttpStatus.BAD_REQUEST);
        // Jackson would truncate 150.7 to 150 for a long; the starter turns that off.
        assertThat(post("/payments", "{\"amountMinor\":150.7,\"reference\":\"E2E-F\"}").getStatusCode())
                .isEqualTo(HttpStatus.BAD_REQUEST);
    }

    @Test
    @Order(3)
    void tellerPostsAuditorReads() {
        ResponseEntity<String> paid = post("/payments", "{\"amountMinor\":15000,\"reference\":\"E2E-1\"}");
        assertThat(paid.getStatusCode()).isEqualTo(HttpStatus.OK);
        assertThat(http.getForObject("/balance", String.class)).contains("\"equity\":15000");

        ResponseEntity<String> refund = post("/refunds", "{\"amountMinor\":100,\"reference\":\"E2E-2\"}");
        assertThat(refund.getStatusCode()).isEqualTo(HttpStatus.FORBIDDEN);
        assertThat(refund.getBody()).contains("\"code\":\"ZXV_E_CAP\"").doesNotContain("E2E-2");
    }

    @Test
    @Order(4)
    void healthUpWithBothCapabilities() {
        ResponseEntity<String> r = http.getForEntity("/actuator/health", String.class);
        assertThat(r.getStatusCode()).isEqualTo(HttpStatus.OK);
        assertThat(r.getBody()).contains("\"auditor\"").contains("\"teller\"").contains("LEDGER_POST");
        assertThat(http.getForEntity("/actuator/health/readiness", String.class).getStatusCode())
                .isEqualTo(HttpStatus.OK);
    }

    @Test
    @Order(5)
    void haltTakesTheServiceDown() {
        assertThat(http.postForEntity("/admin/halt", null, String.class).getStatusCode()).isEqualTo(HttpStatus.ACCEPTED);

        ResponseEntity<String> r = http.getForEntity("/balance", String.class);
        assertThat(r.getStatusCode()).isEqualTo(HttpStatus.INTERNAL_SERVER_ERROR);
        assertThat(r.getBody()).doesNotContain("Internal error"); // not the app's catch-all

        assertThat(http.getForEntity("/actuator/health", String.class).getStatusCode())
                .isEqualTo(HttpStatus.SERVICE_UNAVAILABLE);
        assertThat(http.getForEntity("/actuator/health/liveness", String.class).getStatusCode())
                .isEqualTo(HttpStatus.SERVICE_UNAVAILABLE);
        assertThat(http.getForEntity("/actuator/health/readiness", String.class).getStatusCode())
                .isEqualTo(HttpStatus.SERVICE_UNAVAILABLE);
    }
}
