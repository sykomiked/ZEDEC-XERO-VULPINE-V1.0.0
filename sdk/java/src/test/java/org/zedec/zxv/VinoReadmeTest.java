// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
package org.zedec.zxv;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertTrue;

import java.nio.charset.StandardCharsets;
import java.time.Instant;
import org.junit.jupiter.api.Test;
import org.zedec.zxv.crypto.Hashes;
import org.zedec.zxv.crypto.MlDsa87;
import org.zedec.zxv.vino.PaymentRequest;
import org.zedec.zxv.vino.PlayAccount;
import org.zedec.zxv.vino.PlaySandbox;
import org.zedec.zxv.vino.Rail;
import org.zedec.zxv.vino.Receipt;

/** The two "Vino Embed (ABI 1.6)" examples of README.md, verbatim, plus checks of what they claim. */
class VinoReadmeTest {
    @Test
    void playLaneShop() {
        try (ZxvContext ctx = ZxvContext.create();
             Capability cap = ctx.rootCapability(Permission.VINO);
             PlaySandbox game = PlaySandbox.create(cap)) {
            int shop = game.openIssuer(1);            // issues vouchers
            int player = game.openHolder(1001);
            int stall = game.openHolder(2001);        // where the player spends
            game.back(shop, 100);                     // play units from the faucet: not money
            long v = game.issue(shop, player, 21, Rail.GHOST);   // 10*100 >= 18*21
            game.transfer(v, player, stall, Rail.GHOST);         // spent on its active rail
            game.redeem(v, stall);                    // retired; nothing is paid out
            PlayAccount s = game.account(shop);       // faces() == 0, backing() == 100
            game.check();

            assertEquals(0, s.faces());
            assertEquals(100, s.backing());
            assertEquals(0, game.account(stall).faces());
        }
    }

    @Test
    void livePaymentRequestReceiptAndOfflineVerify() {
        long now = Instant.now().getEpochSecond();
        byte[] merchant = Hashes.sha3_256("my-shop-public-key".getBytes(StandardCharsets.UTF_8));
        PaymentRequest req = PaymentRequest.live(merchant, Hashes.random(16), 1_250, now, now + 600, "Sword of Dawn");
        byte[] wire = req.encode();                   // 160 bytes for the wallet

        // --- wallet side (the user's ZXV wallet) ---
        Receipt receipt;
        try (ZxvContext wctx = ZxvContext.create();
             Capability sign = wctx.rootCapability(Permission.CRYPTO_SIGN);
             MlDsa87.KeyPair wallet = MlDsa87.with(sign).generateKeyPair()) {
            receipt = Receipt.sign(sign, wire, wallet.secretKey(), wallet.publicKey(), now + 5);
        }

        // --- app side, offline: no capability, no network ---
        boolean approved = receipt.verify(now + 6);   // true
        PaymentRequest what = receipt.paymentRequest();  // the request it approves

        assertTrue(approved);
        assertEquals(req, what);
        assertEquals(160, wire.length);
    }
}
