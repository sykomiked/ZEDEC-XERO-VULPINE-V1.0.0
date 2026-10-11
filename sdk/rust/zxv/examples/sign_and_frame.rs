// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
//! Post-quantum signing of an agent message, key exchange, and a UBH-168
//! wire frame.
//!
//! Run: `cargo run --example sign_and_frame`

use zxv::{hex, Caps, Context, MlDsa87, MlKem1024, Ubh168Frame};

fn main() -> zxv::Result<()> {
    let ctx = Context::new()?;
    let cap = ctx.root_capability(1, None, Caps::CRYPTO_SIGN | Caps::CRYPTO_KEM)?;

    let dsa = MlDsa87::new(&cap);
    let (pk, sk) = dsa.keygen()?;
    let msg = b"agent 7: settle cycle 42";
    let sig = dsa.sign(&sk, msg, b"zxv-agent")?;
    dsa.verify(&pk, msg, b"zxv-agent", &sig)?;
    println!("ML-DSA-87 signature verified ({} bytes)", sig.len());

    let kem = MlKem1024::new(&cap);
    let (ek, dk) = kem.keygen()?;
    let (ct, ours) = kem.encapsulate(&ek)?;
    let theirs = kem.decapsulate(&dk, &ct)?;
    println!("ML-KEM-1024 shared secret agrees: {}", ours.ct_eq(&theirs));

    let tag = Ubh168Frame::tag(3, 2, 1)?;
    let frame = Ubh168Frame::pack(
        tag,
        [0x0102_0304, 0x0506_0708, 0x090A_0B0C],
        [0x1112_1314, 0x1516_1718],
    );
    println!(
        "frame {} resolves {:?}",
        hex(frame.as_bytes()),
        frame.resolve()?
    );
    Ok(())
}
