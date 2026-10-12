// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
//
// The README's two Vino Embed (ABI 1.6) examples, kept compiling and running:
// a play-lane shop, and a live payment request -> wallet receipt -> offline
// check.
//   node dist/examples/vino.js

import { Cap, MlDsa87, PaymentRequest, Receipt, VinoLane, VinoPlay, VinoRail, Zxv } from '../src/index.js';

const zxv = await Zxv.load();
using ctx = zxv.createContext();

// ---- 1. A play-lane shop. Play units are not money and never become live value.
{
  using play = VinoPlay.create(ctx.rootCapability({ caps: Cap.VINO }));
  const shop = play.openIssuer(1);                  // owner ids are yours to choose
  const ada = play.openHolder(2);
  const ben = play.openHolder(3);
  play.back(shop, 18n);                             // play backing: 18 covers 10 outstanding (1.8x)
  const v = play.issue(shop, ada, 8n, VinoRail.BEARER); // a face-8 voucher, a Fibonacci face
  play.transfer(v, ada, ben, VinoRail.BEARER);      // Ada spends it on its active rail
  play.redeem(v, ben);                              // back to the shop: retired, nothing paid out
  console.log(play.voucher(v).state, play.account(shop).faces); // 2 (REDEEMED), 0n outstanding
}

// ---- 2. Live: the app builds a request, the user's wallet signs a receipt, the app checks it offline.
const request = new PaymentRequest({
  lane: VinoLane.LIVE,
  merchant: new Uint8Array(32).fill(7),              // e.g. SHA-256 of the shop's key
  nonce: crypto.getRandomValues(new Uint8Array(16)),
  amount: 1250n,                                     // VFV minor units, exact
  createdAt: 1700000000n,
  expiresAt: 1700000600n,
  memo: 'Sword of Dawn',
});
const { bytes, id } = request.encode(zxv);          // 160 bytes for the wallet; id = SHA-256(bytes)

// Wallet side (in the user's ZXV wallet, never in the app): the key stays there.
const walletCap = ctx.rootCapability({ caps: Cap.CRYPTO_SIGN });
const wallet = MlDsa87.generate(walletCap);
const receipt = Receipt.sign(walletCap, bytes, wallet.secretKey, wallet.publicKey, 1700000100n);
wallet.secretKey.close();                           // wiped

// App side: offline, no capability. Throws VerificationFailedError or RequestExpiredError if not valid.
receipt.verify(zxv, 1700000200n);
console.log(Buffer.from(id).toString('hex').slice(0, 16), receipt.isValid(zxv, 1700000601n)); // ... false (expired)
