// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
package org.zedec.zxv;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertThrows;
import static org.junit.jupiter.api.Assertions.assertTrue;

import java.util.List;
import org.junit.jupiter.api.Test;
import org.junit.jupiter.api.function.Executable;
import org.zedec.zxv.budget.Budgets;
import org.zedec.zxv.ledger.Account;
import org.zedec.zxv.ledger.AccountKind;
import org.zedec.zxv.ledger.Ledger;
import org.zedec.zxv.phase.PhaseClock;
import org.zedec.zxv.swarm.Swarm;
import org.zedec.zxv.trust.Attestations;

/** ABI 1.2: every gated call goes through a capability (the zxv_*_cap forms). */
class CapFormsTest {
    private static void denied(Executable call) {
        CapabilityDeniedException e = assertThrows(CapabilityDeniedException.class, call);
        assertEquals(-17, e.status());
        assertEquals("ZXV_E_CAP", e.statusName());
    }

    @Test
    void capFormsNeedTheRightBit() {
        try (ZxvContext ctx = ZxvContext.create();
             Capability root = ctx.rootCapability(Permission.all());
             Capability post = root.attenuate(Permission.LEDGER_POST);
             Capability read = root.attenuate(Permission.LEDGER_READ)) {
            // LEDGER_POST: currencies and opening accounts
            ctx.enableCurrency(post, "NGN");
            ctx.enableCurrency(post, "GHS", 2);
            ctx.registerPrivateCurrency(post, "VFV", 2);
            assertEquals(List.of("NGN", "GHS", "VFV"), ctx.enabledCurrencies());
            denied(() -> ctx.enableCurrency(read, "KES"));
            denied(() -> ctx.enableCurrency(read, "KES", 2));
            denied(() -> ctx.registerPrivateCurrency(read, "VFX", 2));
            denied(() -> Ledger.of(read).openAccount("x", "NGN", AccountKind.HOLDER));

            Ledger writer = Ledger.of(post);
            Account bank = writer.openAccount("bank", "NGN", AccountKind.ISSUER);
            Account ada = writer.openAccount("ada", "NGN", AccountKind.HOLDER);
            writer.post(bank, ada, 7, "NGN", "R-1");

            // LEDGER_READ: account metadata and journals
            Ledger reader = Ledger.of(read);
            assertEquals(List.of(bank, ada), reader.accounts());
            assertEquals(ada, reader.account(ada.id()));
            assertEquals("R-1", reader.journal(ada).get(0).reference());
            denied(writer::accounts);
            denied(() -> writer.account(ada.id()));
            denied(() -> writer.journal(ada));

            // handle creation: BUDGET, PHASE, ATTEST, SWARM
            denied(() -> Budgets.create(read));
            denied(() -> PhaseClock.create(read));
            denied(() -> Attestations.create(read));
            denied(() -> Swarm.create(read, 2, 10));
            try (Capability b = root.attenuate(Permission.BUDGET);
                 Capability p = root.attenuate(Permission.PHASE);
                 Capability t = root.attenuate(Permission.ATTEST);
                 Capability w = root.attenuate(Permission.SWARM)) {
                Budgets.create(b).close();
                PhaseClock.create(p).close();
                Attestations.create(t).close();
                Swarm.create(w, 2, 10).close();
            }
        }
    }

    @Test
    void capFromAnotherContextIsRefused() {
        try (ZxvContext ctx = ZxvContext.create();
             ZxvContext other = ZxvContext.create();
             Capability foreign = other.rootCapability(Permission.all())) {
            denied(() -> ctx.enableCurrency(foreign, "NGN"));
            denied(() -> ctx.enableCurrency(foreign, "NGN", 2));
            denied(() -> ctx.registerPrivateCurrency(foreign, "VFV", 2));
            CapabilityDeniedException e = assertThrows(CapabilityDeniedException.class,
                    () -> ctx.enableCurrency(foreign, "NGN"));
            assertTrue(e.detail().contains("different context"));
            assertTrue(ctx.enabledCurrencies().isEmpty());
            assertTrue(other.enabledCurrencies().isEmpty(), "nothing leaked into the cap's own context");
        }
    }

    @Test
    @SuppressWarnings("deprecation")
    void deprecatedFormsStillWork() {
        try (ZxvContext ctx = ZxvContext.create()) {
            ctx.enableCurrency("NGN");
            ctx.enableCurrency("GHS", 2);
            ctx.registerPrivateCurrency("VFV", 2);
            assertEquals(List.of("NGN", "GHS", "VFV"), ctx.enabledCurrencies());
        }
    }
}
