// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
package org.zedec.zxv;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertFalse;
import static org.junit.jupiter.api.Assertions.assertThrows;
import static org.junit.jupiter.api.Assertions.assertTrue;

import org.junit.jupiter.api.Test;
import org.zedec.zxv.aipi.AipiNode;
import org.zedec.zxv.coin.Octane;
import org.zedec.zxv.coin.TokenWallet;
import org.zedec.zxv.freight.FreightJoin;
import org.zedec.zxv.freight.OpStreamWriter;
import org.zedec.zxv.paradox.ParadoxBook;
import org.zedec.zxv.paradox.ParadoxGraph;
import org.zedec.zxv.paradox.ParadoxHold;
import org.zedec.zxv.paradox.ParadoxPolicy;
import org.zedec.zxv.treaty.Treaty;
import org.zedec.zxv.vhub.VHub;
import org.zedec.zxv.web4.AgentDirectory;
import org.zedec.zxv.web4.AgentKey;
import org.zedec.zxv.web4.ConsentLog;
import org.zedec.zxv.web4.ToolManifest;

/** ABI 1.5 handle lifetimes, the poisoned-context path and capability refusals. */
class Abi15LifecycleTest {
    @Test
    void useAfterCloseIsRefused() {
        try (ZxvContext ctx = ZxvContext.create(); Capability cap = V15.root(ctx)) {
            Treaty t = Treaty.create();
            AipiNode node = AipiNode.create(cap, 1 << 16, 8, null);
            TokenWallet w = TokenWallet.create(cap, 1, Octane.PLAIN);
            VHub hub = VHub.create(cap, null);
            FreightJoin join = FreightJoin.create(100);
            OpStreamWriter wr = OpStreamWriter.create(1, null, 64);
            AgentKey key = AgentKey.create(cap, null, "a", null, 1);
            AgentDirectory dir = AgentDirectory.create(1);
            ToolManifest m = ToolManifest.create(1, 0, 10);
            ConsentLog log = ConsentLog.create(1);
            ParadoxBook book = ParadoxBook.create(cap, 4, 4);
            ParadoxPolicy pol = ParadoxPolicy.create(cap, new long[] {0}, new long[] {1}, new long[] {0}, 0);
            ParadoxHold hold = ParadoxHold.create(cap, 2, 1);
            ParadoxGraph graph = ParadoxGraph.create(cap, 1);
            AutoCloseable[] all = {t, node, w, hub, join, wr, key, dir, m, log, book, pol, hold, graph};
            Runnable[] uses = {t::name, node::info, w::info, () -> hub.currency(0), join::status, () -> wr.copy(1, 1),
                    key::card, dir::count, m::info, () -> log.prune(0), book::registryCount, pol::values, hold::info,
                    graph::info};
            for (int i = 0; i < all.length; i++) {
                try {
                    all[i].close();
                    all[i].close(); // idempotent
                } catch (Exception e) {
                    throw new AssertionError(e);
                }
                Runnable use = uses[i];
                IllegalStateException e = assertThrows(IllegalStateException.class, use::run, all[i].getClass().getName());
                assertTrue(e.getMessage().contains("closed"), e.getMessage());
            }
            assertTrue(t.isClosed() && node.isClosed() && graph.isClosed());
        }
    }

    @Test
    void closingTheContextClosesItsHandles() {
        ZxvContext ctx = ZxvContext.create();
        Capability cap = V15.root(ctx);
        VHub hub = VHub.create(cap, null);
        AgentKey key = AgentKey.create(cap, null, "a", null, 1);
        ctx.close();
        assertTrue(hub.isClosed());
        assertTrue(key.isClosed());
        assertThrows(IllegalStateException.class, hub::check);
    }

    @Test
    void poisonedContextFailsClosed() {
        try (ZxvContext ctx = ZxvContext.create(); Capability cap = V15.root(ctx);
                VHub hub = VHub.create(cap, null); TokenWallet w = TokenWallet.create(cap, 1, Octane.PLAIN)) {
            w.mint(10);
            ctx.halt("test: poisoned context");
            ZxvFatalError e = assertThrows(ZxvFatalError.class, () -> hub.currency(0));
            assertEquals(ZxvStatus.E_POISONED, e.status());
            assertTrue(ctx.isPoisoned());
            // Once marked dead, every handle of the context refuses before reaching C.
            assertThrows(ZxvFatalError.class, w::info);
            assertThrows(ZxvFatalError.class, () -> w.mint(1));
            assertThrows(ZxvFatalError.class, () -> TokenWallet.create(cap, 2, Octane.PLAIN));
            assertFalse(hub.isClosed(), "a poisoned handle can still be closed");
        }
    }

    @Test
    void capabilityRefusals() {
        try (ZxvContext ctx = ZxvContext.create(); Capability root = V15.root(ctx);
                Capability noAipi = V15.without(root, Permission.AIPI);
                Capability noMint = V15.without(root, Permission.MINT);
                Capability noParadox = V15.without(root, Permission.PARADOX)) {
            CapabilityDeniedException a = assertThrows(CapabilityDeniedException.class,
                    () -> AipiNode.create(noAipi, 1 << 16, 8, null));
            assertEquals(ZxvStatus.E_CAP, a.status());
            assertThrows(CapabilityDeniedException.class, () -> TokenWallet.create(noMint, 1, Octane.PLAIN));
            assertThrows(CapabilityDeniedException.class, () -> ParadoxBook.create(noParadox, 4, 4));
            assertThrows(CapabilityDeniedException.class, () -> ParadoxHold.create(noParadox, 2, 1));
            assertThrows(CapabilityDeniedException.class, () -> ParadoxGraph.create(noParadox, 1));
            assertThrows(CapabilityDeniedException.class,
                    () -> ParadoxPolicy.create(noParadox, new long[] {0}, new long[] {1}, new long[] {0}, 0));
            // A handle made with a full capability keeps working; the narrowed ones still do their own work.
            try (TokenWallet w = TokenWallet.create(noAipi, 1, Octane.PLAIN)) {
                w.mint(5);
                assertEquals(5, w.value());
            }
            // A capability of another context is refused.
            try (ZxvContext other = ZxvContext.create(); Capability foreign = V15.root(other);
                    TokenWallet mine = TokenWallet.create(root, 1, Octane.PLAIN);
                    TokenWallet theirs = TokenWallet.create(foreign, 1, Octane.PLAIN)) {
                mine.mint(10);
                assertEquals(ZxvStatus.E_CAP, assertThrows(CapabilityDeniedException.class,
                        () -> mine.transfer(theirs, 1)).status());
            }
        }
    }
}
