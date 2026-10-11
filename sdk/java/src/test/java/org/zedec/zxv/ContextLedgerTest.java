// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
package org.zedec.zxv;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertFalse;
import static org.junit.jupiter.api.Assertions.assertThrows;
import static org.junit.jupiter.api.Assertions.assertTrue;

import java.nio.file.Path;
import java.util.List;
import java.util.Set;
import java.util.concurrent.TimeUnit;
import org.junit.jupiter.api.Test;
import org.zedec.zxv.ledger.Account;
import org.zedec.zxv.ledger.AccountKind;
import org.zedec.zxv.ledger.Balance;
import org.zedec.zxv.ledger.Iso4217;
import org.zedec.zxv.ledger.JournalEntry;
import org.zedec.zxv.ledger.Ledger;
import org.zedec.zxv.trust.Attestations;
import org.zedec.zxv.trust.Subject;

/** Contexts, capabilities, the ledger, attestations and the fatal path. */
class ContextLedgerTest {
    @Test
    void ledgerThroughCapabilities() {
        try (ZxvContext ctx = ZxvContext.create(); Capability root = ctx.rootCapability(Permission.all())) {
            ctx.enableCurrency(root, "NGN", 2);
            ctx.registerPrivateCurrency(root, "VFV", 2);
            assertEquals(List.of("NGN", "VFV"), ctx.enabledCurrencies());
            assertTrue(ctx.currency("VFV").orElseThrow().isPrivate());
            assertTrue(ctx.currency("GHS").isEmpty());

            Ledger ledger = Ledger.of(root);
            Account bank = ledger.openAccount("issuer", "NGN", AccountKind.ISSUER);
            Account ada = ledger.openAccount("ada", "NGN", AccountKind.HOLDER);
            Account bob = ledger.openAccount("bob", "NGN", AccountKind.HOLDER);
            long e1 = ledger.post(bank, ada, 500, "NGN", "r1");
            assertEquals(1, e1);
            ledger.post(ada, bob, 200, "NGN", "r2");
            assertEquals(new Balance(500, 200, 300), ledger.balance(ada));
            assertEquals(new Balance(0, 500, -500), ledger.balance(bank));
            List<JournalEntry> j = ledger.journal(ada);
            assertEquals(2, j.size());
            assertEquals(new JournalEntry(e1, 500, bank.id(), "r1"), j.get(0));
            assertEquals(-200, j.get(1).amountMinor());
            assertEquals(ada, ledger.account(ada.id()));
            assertEquals(3, ledger.accounts().size());
            ledger.check();

            InsufficientFundsException funds = assertThrows(InsufficientFundsException.class,
                    () -> ledger.post(bob, ada, 201, "NGN", "r3"));
            assertEquals(-6, funds.status());
            ZxvUsageException dup = assertThrows(ZxvUsageException.class, () -> ledger.post(bank, ada, 1, "NGN", "r1"));
            assertEquals("ZXV_E_DUPLICATE", dup.statusName());
            assertFalse(dup.detail().isEmpty(), "detail text comes from zxv_last_error");

            try (Capability readOnly = root.attenuate(Permission.LEDGER_READ)) {
                Ledger ro = Ledger.of(readOnly);
                assertEquals(300, ro.balance(ada).equity());
                assertThrows(CapabilityDeniedException.class, () -> ro.post(bank, ada, 1, "NGN", "r4"));
                assertEquals(Set.of(Permission.LEDGER_READ), readOnly.permissions());
                try (Capability wider = readOnly.attenuate(Permission.all())) {
                    assertFalse(wider.allows(Permission.LEDGER_POST), "attenuation never widens");
                }
                root.revoke();
                assertFalse(readOnly.allows(Permission.LEDGER_READ), "revoking the parent revokes the child");
                assertTrue(readOnly.info().revoked());
            }
        }
    }

    @Test
    void contradictoryAttestationBlocks() {
        try (ZxvContext ctx = ZxvContext.create(); Capability cap = ctx.rootCapability(Permission.all())) {
            cap.setAttestation(Truth.BOTH);
            CapabilityDeniedException e = assertThrows(CapabilityDeniedException.class, () -> cap.require(Permission.LEDGER_READ));
            assertTrue(e.detail().contains("contradictory"), e.detail());
            assertEquals(Truth.BOTH, cap.info().attestation());
            cap.setAttestation(Truth.TRUE);
            cap.require(Permission.LEDGER_READ);
        }
    }

    @Test
    void attestationRegistry() {
        try (ZxvContext ctx = ZxvContext.create(); Capability cap = ctx.rootCapability(Permission.ATTEST);
             Attestations reg = Attestations.create(cap)) {
            int i = reg.attest("model.gguf", Subject.MODEL_PACK, Truth.TRUE, "loader", new byte[32]);
            assertTrue(reg.isGateClear());
            reg.evidence(i, true);
            reg.evidence(i, false);
            assertEquals(new Attestations.State(Truth.BOTH, 2, 1), reg.state(i));
            assertFalse(reg.isGateClear(), "BOTH blocks the actuator gate");
            assertEquals(i, reg.find("model.gguf").orElseThrow());
            assertTrue(reg.find("nope").isEmpty());
            try (Capability noAttest = cap.attenuate()) {
                assertThrows(CapabilityDeniedException.class,
                        () -> Attestations.create(noAttest).attest("x", Subject.KEY, Truth.TRUE, "y", null));
            }
        }
    }

    @Test
    void iso4217() {
        Iso4217.Currency ngn = Iso4217.byAlpha("NGN").orElseThrow();
        assertEquals(566, ngn.numeric());
        assertEquals(2, ngn.minorUnits());
        assertEquals("NGN", Iso4217.byNumeric(566).orElseThrow().alpha());
        assertTrue(Iso4217.byNumeric(777).isEmpty(), "the Vino rails are not ISO 4217");
        assertTrue(Iso4217.byAlpha("VFV").isEmpty());
        assertEquals("Naira", Iso4217.name("NGN"));
        assertTrue(Iso4217.codes().contains("XOF"));
        assertFalse(Iso4217.published().isEmpty());
    }

    @Test
    void fatalPathThroughHalt() {
        ZxvContext ctx = ZxvContext.create();
        Capability cap = ctx.rootCapability(Permission.all());
        ctx.enableCurrency(cap, "NGN", 2);
        Ledger ledger = Ledger.of(cap);
        Account a = ledger.openAccount("a", "NGN", AccountKind.ISSUER);
        ctx.halt("test halt");
        assertTrue(ctx.isPoisoned());
        ZxvFatalError e = assertThrows(ZxvFatalError.class, () -> ledger.balance(a));
        assertEquals(-18, e.status());
        assertEquals("ZXV_E_POISONED", e.statusName());
        assertFalse(Exception.class.isAssignableFrom(ZxvFatalError.class), "not an Exception: catch (Exception) lets it through");
        // The SDK marks the handle dead: every later call fails the same way, without retrying in C.
        assertThrows(ZxvFatalError.class, () -> ledger.post(a, a, 1, "NGN", "again"));
        assertThrows(ZxvFatalError.class, () -> cap.attenuate(Permission.LEDGER_READ));
        assertThrows(ZxvFatalError.class, () -> ctx.enableCurrency(cap, "GHS"));
        assertTrue(ctx.isPoisoned());
        ctx.close(); // a halted context can still be closed
        assertThrows(IllegalStateException.class, () -> cap.permissions(), "closing the context closed its capabilities");
    }

    @Test
    void fatalPolicyAbortHaltsTheJvm() throws Exception {
        Path java = Path.of(System.getProperty("java.home"), "bin", "java");
        Process p = new ProcessBuilder(java.toString(), "--enable-native-access=ALL-UNNAMED",
                "-Dzxv.library=" + System.getProperty("zxv.library"),
                "-cp", System.getProperty("java.class.path"), AbortProbe.class.getName())
                .redirectErrorStream(true).start();
        assertTrue(p.waitFor(60, TimeUnit.SECONDS));
        String out = new String(p.getInputStream().readAllBytes());
        assertEquals(70, p.exitValue(), out);
        assertTrue(out.contains("ZXV FATAL"), out);
        assertFalse(out.contains("UNREACHABLE"), out);
    }
}
