// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
//! Vino Embed (ABI 1.6): a play-lane shop, then a live payment request,
//! a wallet receipt and an offline check.
//!
//! Run: `cargo run --example vino`

use zxv::vino::{
    self, Lane, PaymentRequest, PlayAccountKind, Rail, RequestFlags, VinoPlay, VoucherState,
};
use zxv::{Caps, Context, MlDsa87, RefusedKind};

fn main() -> zxv::Result<()> {
    let ctx = Context::new()?;

    // ----- PLAY lane: play units are not money and never become live value.
    let app = ctx.root_capability(1, None, Caps::VINO)?;
    let play = VinoPlay::new(&app)?;
    let issuer = play.open(100, PlayAccountKind::Issuer)?;
    let player = play.open(200, PlayAccountKind::Holder)?;
    let shop = play.open(300, PlayAccountKind::Holder)?;
    play.back(issuer, 18)?; // 10*18 >= 18*10: room for faces up to 10
    let refused = play.issue(issuer, player, 13, Rail::Bearer).unwrap_err();
    assert_eq!(refused.refused_kind(), Some(RefusedKind::Coverage));
    let coin = play.issue(issuer, player, 8, Rail::Bearer)?;
    play.transfer(coin, player, shop, Rail::Bearer)?; // the player spends it
    play.redeem(coin, shop)?; // retired; nothing is paid out
    assert_eq!(play.voucher(coin)?.state, VoucherState::Redeemed);
    println!(
        "play: voucher {coin} redeemed, issuer {:?}",
        play.account(issuer)?
    );

    // ----- LIVE lane: the app builds a request ...
    let req = PaymentRequest {
        lane: Lane::Live,
        flags: RequestFlags::NONE,
        merchant: [1; 32],
        nonce: [2; 16],
        amount: 1_234_567, // VFV minor units
        created_at: 1_700_000_000,
        expires_at: 1_700_000_600,
        memo: "Sword of Dawn".into(),
    };
    let (bytes, id) = req.encode()?;

    // ... the user's wallet (never the app) approves it with its own key ...
    let wallet = ctx.root_capability(2, None, Caps::CRYPTO_SIGN)?;
    let (wallet_pk, wallet_sk) = MlDsa87::new(&wallet).keygen()?;
    let sig = vino::sign_receipt(&wallet, &bytes, &wallet_sk, &wallet_pk, 1_700_000_100)?;

    // ... and the app checks the receipt offline, with no capability.
    vino::verify_receipt(&bytes, &wallet_pk, 1_700_000_100, &sig, 1_700_000_200)?;
    let late = vino::verify_receipt(&bytes, &wallet_pk, 1_700_000_100, &sig, 1_700_000_601);
    assert_eq!(late.unwrap_err().refused_kind(), Some(RefusedKind::Expired));
    println!(
        "live: receipt for request {} verified",
        &zxv::hex(&id)[..16]
    );
    Ok(())
}
