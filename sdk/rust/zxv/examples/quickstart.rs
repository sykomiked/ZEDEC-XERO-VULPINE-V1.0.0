// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
//! Quickstart: a context, a capability, exact money and four-valued logic.
//!
//! Run: `cargo run --example quickstart`

use zxv::{AccountKind, Caps, Context, Ledger, Rational, Truth};

fn main() -> zxv::Result<()> {
    let (major, minor, patch) = zxv::version();
    println!("libzxv ABI {major}.{minor}.{patch}");

    let ctx = Context::new()?;
    let root = ctx.root_capability(1, None, Caps::ALL)?;
    ctx.enable_currency_cap(&root, "NGN")?;

    // A reader that can never post.
    let reader = root.attenuate(Caps::LEDGER_READ)?;

    let ledger = Ledger::new(&root);
    let bank = ledger.open_account("bank", "NGN", AccountKind::Issuer)?;
    let alice = ledger.open_account("alice", "NGN", AccountKind::Holder)?;
    ledger.post(bank, alice, 12_550, "NGN", "E2E-0001")?;
    println!(
        "alice holds {} kobo",
        Ledger::new(&reader).balance(alice)?.equity
    );

    match Ledger::new(&reader).post(alice, bank, 1, "NGN", "E2E-0002") {
        Err(e) if e.is_capability_denied() => println!("reader cannot post: {}", e.name()),
        other => panic!("unexpected: {other:?}"),
    }

    // Exact arithmetic: 0.1 + 0.2 is 0.3, and a float shows its true value.
    let sum = "0.1".parse::<Rational>()?.checked_add("0.2".parse()?)?;
    println!("0.1 + 0.2 = {sum}");
    println!(
        "the double 0.1 is exactly {}",
        Rational::from_f64_exact(0.1)?
    );

    // Contradictions are held, not collapsed.
    let verdict = Truth::from_evidence(2, 1);
    println!(
        "evidence 2 for, 1 against: {verdict} (certain? {})",
        verdict.is_certain()
    );
    Ok(())
}
