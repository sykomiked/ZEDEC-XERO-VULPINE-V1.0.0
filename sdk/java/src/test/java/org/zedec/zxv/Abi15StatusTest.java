// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
package org.zedec.zxv;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertInstanceOf;
import static org.junit.jupiter.api.Assertions.assertSame;
import static org.junit.jupiter.api.Assertions.assertTrue;

import java.util.Map;
import org.junit.jupiter.api.Test;
import org.zedec.zxv.cid.CidException;
import org.zedec.zxv.coin.NotADenominationException;
import org.zedec.zxv.freight.ErasureException;
import org.zedec.zxv.internal.Errors;
import org.zedec.zxv.paradox.OwnerApprovalRequiredException;
import org.zedec.zxv.paradox.StepLimitException;
import org.zedec.zxv.treaty.TreatyException;
import org.zedec.zxv.vhub.LimitException;
import org.zedec.zxv.vhub.PegException;
import org.zedec.zxv.web4.ConsentException;

/** ABI 1.5 statuses, features and capability bits. */
class Abi15StatusTest {
    @Test
    void statusVectors() {
        for (Object o : V15.l(Vectors.root().get("status_abi15"))) {
            Map<String, Object> s = V15.m(o);
            int code = V15.i(s.get("code"));
            assertEquals(s.get("name"), Errors.name(code));
            assertEquals(StatusClass.fromCode(V15.i(s.get("class"))), Errors.statusClass(code));
        }
    }

    @Test
    void everyNewStatusMapsToItsTypedException() {
        Object[][] want = {
            {ZxvStatus.E_DENOM, NotADenominationException.class, ZxvUsageException.class},
            {ZxvStatus.E_TREATY, TreatyException.class, ZxvUsageException.class},
            {ZxvStatus.E_CID, CidException.class, ZxvRefusedException.class},
            {ZxvStatus.E_CONSENT, ConsentException.class, ZxvRefusedException.class},
            {ZxvStatus.E_ERASURE, ErasureException.class, ZxvRefusedException.class},
            {ZxvStatus.E_PEG, PegException.class, ZxvRefusedException.class},
            {ZxvStatus.E_LIMIT, LimitException.class, ZxvRefusedException.class},
            {ZxvStatus.E_PARADOX_STEP, StepLimitException.class, ZxvRefusedException.class},
            {ZxvStatus.E_PARADOX_OWNER, OwnerApprovalRequiredException.class, ZxvRefusedException.class},
        };
        for (Object[] w : want) {
            int code = (int) w[0];
            RuntimeException e = Errors.raise(code, "probe", null);
            assertSame(w[1], e.getClass(), "status " + code);
            assertInstanceOf((Class<?>) w[2], e);
            ZxvException z = (ZxvException) e;
            assertEquals(code, z.status());
            assertEquals(Errors.name(code), z.statusName());
            assertEquals(Errors.statusClass(code), z.statusClass());
        }
        assertEquals(-32, ZxvStatus.E_DENOM);
        assertEquals(-40, ZxvStatus.E_PARADOX_OWNER);
    }

    @Test
    void featuresAndCapabilityBits() {
        assertTrue(Zxv.version().minor() >= 5);
        for (Feature2 f : Feature2.values()) assertTrue(Zxv.hasFeature(f), f + " is built in");
        assertEquals(1L << 12, Permission.AIPI.bit());
        assertEquals(1L << 13, Permission.MINT.bit());
        assertEquals(1L << 14, Permission.PARADOX.bit());
        assertEquals((1L << 15) - 1, Permission.mask(Permission.all()));
        try (ZxvContext ctx = ZxvContext.create(); Capability cap = V15.root(ctx)) {
            assertEquals(Permission.all(), cap.permissions(), "a 1.5 library keeps the new bits");
        }
    }
}
