// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
//! ABI 1.6: the status_abi16 and vino sections of vectors.json (refusals
//! included), plus error mapping, the voucher buffer path, capability
//! refusals, borrow-checked lifetimes and a poisoned context.

mod common;

use serde_json::Value;
use zxv::vino::{
    self, Lane, PaymentRequest, PlayAccount, PlayAccountKind, Rail, RequestFlags, SpendMeter,
    VinoPlay, VoucherInfo, VoucherState,
};
use zxv::{sys, Caps, Context, Error, Features2, Hashes, MlDsa87, RefusedKind, StatusClass};

// ----- JSON helpers (64-bit numbers are decimal strings) -----------------

fn ji(v: &Value) -> i64 {
    match v {
        Value::Number(n) => n.as_i64().unwrap(),
        Value::String(s) => s.parse().unwrap(),
        _ => panic!("not a number: {v}"),
    }
}

fn ju(v: &Value) -> u64 {
    match v {
        Value::Number(n) => n.as_u64().unwrap(),
        Value::String(s) => s.parse().unwrap(),
        _ => panic!("not a number: {v}"),
    }
}

fn js(v: &Value) -> &str {
    v.as_str().unwrap_or("")
}

fn ja(v: &Value) -> &Vec<Value> {
    v.as_array().expect("array")
}

fn hx(v: &Value) -> Vec<u8> {
    zxv::hex_decode(js(v))
}

fn st<T>(r: &Result<T, Error>) -> &'static str {
    match r {
        Ok(_) => "ZXV_OK",
        Err(e) => zxv::status_name(e.status()),
    }
}

fn want<T: std::fmt::Debug>(label: &str, r: &Result<T, Error>, w: &Value) {
    assert_eq!(st(r), js(w), "{label}: {r:?}");
}

fn vino_vec() -> Value {
    common::vectors()["vino"].clone()
}

/// One request row of the vectors, through the raw lane (lanes 0 and 3 are
/// not expressible as `Lane`).
fn encode_row(m: &Value) -> Result<(vino::RequestRecord, vino::RequestId), Error> {
    let merchant: [u8; 32] = hx(&m["merchant"]).try_into().unwrap();
    let nonce: [u8; 16] = hx(&m["nonce"]).try_into().unwrap();
    match Lane::from_raw(ji(&m["lane"]) as i32) {
        Some(lane) => PaymentRequest {
            lane,
            flags: RequestFlags::from_bits(ji(&m["flags"]) as u32),
            merchant,
            nonce,
            amount: ju(&m["amount"]),
            created_at: ju(&m["created_at"]),
            expires_at: ju(&m["expires_at"]),
            memo: js(&m["memo"]).to_string(),
        }
        .encode(),
        None => vino::encode_raw(
            ji(&m["lane"]) as i32,
            ji(&m["flags"]) as u32,
            &merchant,
            &nonce,
            ju(&m["amount"]),
            ju(&m["created_at"]),
            ju(&m["expires_at"]),
            js(&m["memo"]),
        ),
    }
}

fn record(v: &Value) -> vino::RequestRecord {
    hx(v).try_into().unwrap()
}

// ===== statuses, caps, features ===========================================

#[test]
fn statuses_caps_features() {
    let (_, minor, _) = zxv::version();
    assert!(minor >= 6);
    let v = common::vectors();
    let rows = ja(&v["status_abi16"]);
    assert_eq!(rows.len(), 5);
    let kinds = [
        (sys::ZXV_E_RAIL, RefusedKind::Rail),
        (sys::ZXV_E_COVERAGE, RefusedKind::Coverage),
        (sys::ZXV_E_EXPIRED, RefusedKind::Expired),
        (sys::ZXV_E_CHANCE, RefusedKind::Chance),
    ];
    for r in rows {
        let code = ji(&r["code"]) as i32;
        assert_eq!(zxv::status_name(code), js(&r["name"]));
        let e = Error::new(code, "detail");
        assert_eq!((e.status(), e.name()), (code, js(&r["name"])));
        match ji(&r["class"]) {
            1 => {
                assert_eq!(StatusClass::of(code), StatusClass::Usage);
                assert!(matches!(e, Error::Usage(_)), "{e}");
                assert_eq!(code, sys::ZXV_E_LANE);
            }
            2 => {
                assert_eq!(StatusClass::of(code), StatusClass::Refused);
                let k = kinds.iter().find(|(c, _)| *c == code).unwrap().1;
                assert_eq!(e.refused_kind(), Some(k), "{e}");
            }
            c => panic!("class {c}"),
        }
    }
    assert_eq!(Caps::VINO.bits(), 1 << 15);
    assert_eq!(Caps::ALL_1_6.bits(), (1 << 16) - 1);
    assert!(Caps::ALL_1_6.contains(Caps::ALL_1_5 | Caps::VINO));
    assert!(!Caps::ALL_1_5.contains(Caps::VINO));
    assert_eq!(
        format!("{:?}", Caps::PARADOX | Caps::VINO),
        "Caps(PARADOX | VINO)"
    );
    assert_eq!(Features2::VINO.bits(), 0x100);
    assert!(zxv::features2().contains(Features2::VINO));
    let ctx = Context::new().unwrap();
    let c = ctx.root_capability(1, None, Caps::ALL_1_6).unwrap();
    c.check(Caps::VINO).unwrap();
    assert_eq!(
        (vino::REQUEST_BYTES, vino::RECEIPT_MSG_BYTES, vino::MEMO_MAX),
        (160, 88, 64)
    );
    assert_eq!(vino::AMOUNT_MAX, 1 << 59);
    assert_eq!((vino::MAX_ACCOUNTS, vino::MAX_VOUCHERS), (256, 4096));
    assert_eq!(
        (Rail::Bearer.to_string(), Lane::Live.to_string()),
        ("BEARER".into(), "LIVE".into())
    );
}

// ===== requests ===========================================================

#[test]
fn request_vectors() {
    let v = vino_vec();
    let rows = ja(&v["requests"]);
    assert!(rows.len() >= 10);
    for (i, m) in rows.iter().enumerate() {
        let r = encode_row(m);
        want(&format!("encode {i}"), &r, &m["status"]);
        let Ok((bytes, id)) = r else { continue };
        assert_eq!(bytes.to_vec(), hx(&m["bytes"]), "bytes {i}");
        assert_eq!(id.to_vec(), hx(&m["id"]), "id {i}");
        let (back, id2) = PaymentRequest::decode(&bytes).unwrap();
        assert_eq!(id2, id);
        assert_eq!(back.encode().unwrap().0, bytes, "round trip {i}");
        assert_eq!(back.id().unwrap(), id);
        assert_eq!(back.memo, js(&m["memo"]));
        assert_eq!(back.amount, ju(&m["amount"]));
        assert_eq!(back.lane.raw(), ji(&m["lane"]) as i32);
    }
    let base = record(&rows[0]["bytes"]);
    for (i, m) in ja(&v["decode"]).iter().enumerate() {
        let mut b = base;
        b[ji(&m["offset"]) as usize] = ji(&m["value"]) as u8;
        want(
            &format!("decode {i}"),
            &PaymentRequest::decode(&b),
            &m["status"],
        );
    }
    // Class mapping of the request refusals.
    let (req, _) = PaymentRequest::decode(&base).unwrap();
    let mut chance = req.clone();
    chance.flags = RequestFlags::CHANCE;
    let e = chance.encode().unwrap_err();
    assert_eq!(e.refused_kind(), Some(RefusedKind::Chance), "{e}");
    chance.lane = Lane::Play; // a chance outcome is allowed on the play lane
    chance.encode().unwrap();
    let e = vino::encode_raw(3, 0, &req.merchant, &req.nonce, 1, 1, 2, "").unwrap_err();
    assert!(
        matches!(e, Error::Usage(_)) && e.status() == sys::ZXV_E_LANE,
        "{e}"
    );
    // A NUL in the memo is refused rather than silently cutting the memo.
    let mut nul = req;
    nul.memo = "Sword\0of Dawn".into();
    assert_eq!(nul.encode().unwrap_err().status(), sys::ZXV_E_FORMAT);
}

// ===== receipts ===========================================================

#[test]
fn receipt_vectors() {
    let v = vino_vec();
    let rv = &v["receipt"];
    let req0 = &ja(&v["requests"])[0];
    let rec = record(&req0["bytes"]);
    let id: vino::RequestId = hx(&req0["id"]).try_into().unwrap();
    let ctx = Context::new().unwrap();
    let cap = ctx.root_capability(1, None, Caps::ALL_1_6).unwrap();
    let dsa = MlDsa87::new(&cap);
    let seed: [u8; 32] = hx(&rv["seed"]).try_into().unwrap();
    let (pk, sk) = dsa.keygen_from_seed(&seed).unwrap();
    assert_eq!(Hashes::sha3_256(&pk).to_vec(), hx(&rv["pk_sha3_256"]));
    let at = ju(&rv["approved_at"]);
    let msg = vino::receipt_message(&id, &pk, at).unwrap();
    assert_eq!(msg.to_vec(), hx(&rv["msg"]));
    let sig = dsa.sign_deterministic(&sk, &msg, &[]).unwrap();
    assert_eq!(sig, hx(&rv["sig"]));
    for (i, c) in ja(&rv["checks"]).iter().enumerate() {
        let mut s = sig.clone();
        let flip = ji(&c["flip_sig"]);
        if flip >= 0 {
            s[flip as usize] ^= 1;
        }
        let r = vino::verify_receipt(&rec, &pk, ju(&c["approved_at"]), &s, ju(&c["now"]));
        want(&format!("check {i}"), &r, &c["status"]);
    }
    // The wallet side signs (hedged); the app verifies offline.
    let wsig = vino::sign_receipt(&cap, &rec, &sk, &pk, at).unwrap();
    vino::verify_receipt(&rec, &pk, at, &wsig, at + 100).unwrap();
    dsa.verify(&pk, &msg, &[], &wsig).unwrap();
    let e = vino::sign_receipt(&cap, &rec, &sk, &pk, 1_700_000_601).unwrap_err();
    assert_eq!(e.refused_kind(), Some(RefusedKind::Expired), "{e}");
    // Another wallet, or a changed request byte, does not verify.
    let (pk2, _) = dsa.keygen_from_seed(&[0x32; 32]).unwrap();
    let e = vino::verify_receipt(&rec, &pk2, at, &wsig, at).unwrap_err();
    assert_eq!(e.refused_kind(), Some(RefusedKind::VerificationFailed));
    let mut other = rec;
    other[56] ^= 1;
    let e = vino::verify_receipt(&other, &pk, at, &wsig, at).unwrap_err();
    assert_eq!(e.refused_kind(), Some(RefusedKind::VerificationFailed));
    let mut bad = rec;
    bad[0] = b'Y';
    assert_eq!(
        vino::verify_receipt(&bad, &pk, at, &wsig, at)
            .unwrap_err()
            .status(),
        sys::ZXV_E_FORMAT
    );
    // Signing needs CRYPTO_SIGN; wrong lengths are usage errors.
    let nosign = cap
        .attenuate(Caps::from_bits(
            Caps::ALL_1_6.bits() & !Caps::CRYPTO_SIGN.bits(),
        ))
        .unwrap();
    assert!(vino::sign_receipt(&nosign, &rec, &sk, &pk, at)
        .unwrap_err()
        .is_capability_denied());
    assert_eq!(
        vino::receipt_message(&id, &pk[..10], at)
            .unwrap_err()
            .status(),
        sys::ZXV_E_ARG
    );
    assert_eq!(
        vino::verify_receipt(&rec, &pk, at, &wsig[..100], at)
            .unwrap_err()
            .status(),
        sys::ZXV_E_ARG
    );
}

// ===== play lane ==========================================================

#[test]
fn play_vectors() {
    let pv = vino_vec()["play"].clone();
    let ctx = Context::new().unwrap();
    let cap = ctx.root_capability(1, None, Caps::ALL_1_6).unwrap();
    let play = VinoPlay::new(&cap).unwrap();
    let rail = |m: &Value| Rail::from_raw(ji(&m["rail"]) as i32).expect("vector rail");
    for (i, m) in ja(&pv["steps"]).iter().enumerate() {
        let label = format!("{} step {i}", js(&m["op"]));
        let r: Result<(), Error> = match js(&m["op"]) {
            "open" => {
                let owner = ji(&m["owner"]) as u32;
                let kind = match ji(&m["is_issuer"]) {
                    0 => Some(PlayAccountKind::Holder),
                    1 => Some(PlayAccountKind::Issuer),
                    _ => None,
                };
                match kind {
                    Some(k) => play.open(owner, k).map(|a| {
                        assert_eq!(i64::from(a), ji(&m["account"]), "{label}");
                    }),
                    // Not expressible as PlayAccountKind: check the C
                    // refusal through the raw handle.
                    None => {
                        let mut a = 0u32;
                        // SAFETY: live cap and sandbox; out pointer valid.
                        let rc = unsafe {
                            sys::zxv_vino_play_open(
                                cap.as_raw(),
                                play.as_raw(),
                                owner,
                                ji(&m["is_issuer"]) as i32,
                                &mut a,
                            )
                        };
                        if rc == 0 {
                            Ok(())
                        } else {
                            Err(Error::new(rc, ""))
                        }
                    }
                }
            }
            "back" => play.back(ji(&m["issuer"]) as u32, ju(&m["amount"])),
            "unback" => play.unback(ji(&m["issuer"]) as u32, ju(&m["amount"])),
            "issue" => play
                .issue(
                    ji(&m["issuer"]) as u32,
                    ji(&m["holder"]) as u32,
                    ju(&m["face"]),
                    rail(m),
                )
                .map(|id| assert_eq!(id, ju(&m["id"]), "{label}")),
            "transfer" => play.transfer(
                ju(&m["voucher"]),
                ji(&m["from"]) as u32,
                ji(&m["to"]) as u32,
                rail(m),
            ),
            "set_rail" => play.set_rail(ju(&m["voucher"]), ji(&m["holder"]) as u32, rail(m)),
            "redeem" => play.redeem(ju(&m["voucher"]), ji(&m["holder"]) as u32),
            op => panic!("unknown op {op}"),
        };
        want(&label, &r, &m["status"]);
    }
    for m in ja(&pv["accounts"]) {
        let n = ji(&m["account"]) as u32;
        let want_acct = PlayAccount {
            owner: ji(&m["owner"]) as u32,
            kind: if ji(&m["is_issuer"]) == 1 {
                PlayAccountKind::Issuer
            } else {
                PlayAccountKind::Holder
            },
            backing: ju(&m["backing"]),
            faces: ju(&m["faces"]),
            count: ji(&m["count"]) as u32,
        };
        assert_eq!(play.account(n).unwrap(), want_acct, "account {n}");
        let ids: Vec<u64> = ja(&m["vouchers"]).iter().map(ju).collect();
        assert_eq!(play.vouchers(n).unwrap(), ids, "vouchers of {n}");
    }
    for m in ja(&pv["vouchers"]) {
        let want_v = VoucherInfo {
            face: ju(&m["face"]),
            issuer: ji(&m["issuer"]) as u32,
            holder: ji(&m["holder"]) as u32,
            rail: rail(m),
            state: if ji(&m["state"]) == 2 {
                VoucherState::Redeemed
            } else {
                VoucherState::Active
            },
        };
        assert_eq!(play.voucher(ju(&m["id"])).unwrap(), want_v);
    }
    play.check().unwrap();
    assert_eq!(play.account(99).unwrap_err().status(), sys::ZXV_E_NOT_FOUND);
    assert_eq!(
        play.vouchers(99).unwrap_err().status(),
        sys::ZXV_E_NOT_FOUND
    );
}

/// The two-call buffer path, past its first guess (8 ids) and back to zero.
#[test]
fn voucher_buffer_growth() {
    let ctx = Context::new().unwrap();
    let cap = ctx.root_capability(1, None, Caps::VINO).unwrap();
    let play = VinoPlay::new(&cap).unwrap();
    let iss = play.open(1, PlayAccountKind::Issuer).unwrap();
    let h = play.open(2, PlayAccountKind::Holder).unwrap();
    play.back(iss, 10_000).unwrap();
    let ids: Vec<u64> = (0..37)
        .map(|_| play.issue(iss, h, 1, Rail::Ghost).unwrap())
        .collect();
    assert_eq!(play.vouchers(iss).unwrap(), ids);
    assert_eq!(play.vouchers(h).unwrap(), ids);
    let a = play.account(iss).unwrap();
    assert_eq!((a.count, a.faces, a.backing), (37, 37, 10_000));
    for id in &ids {
        play.redeem(*id, h).unwrap();
    }
    assert!(play.vouchers(h).unwrap().is_empty());
    // Redeeming retired the vouchers and paid nothing: backing unchanged.
    let a = play.account(iss).unwrap();
    assert_eq!((a.count, a.faces, a.backing), (0, 0, 10_000));
    play.check().unwrap();
}

#[test]
fn error_mapping() {
    let ctx = Context::new().unwrap();
    let cap = ctx.root_capability(1, None, Caps::VINO).unwrap();
    let play = VinoPlay::new(&cap).unwrap();
    let iss = play.open(1, PlayAccountKind::Issuer).unwrap();
    let h1 = play.open(2, PlayAccountKind::Holder).unwrap();
    let h2 = play.open(3, PlayAccountKind::Holder).unwrap();
    play.back(iss, 1).unwrap();
    let e = play.issue(iss, h1, 1, Rail::Bearer).unwrap_err();
    assert_eq!(e.refused_kind(), Some(RefusedKind::Coverage));
    assert_eq!(e.name(), "ZXV_E_COVERAGE");
    assert!(!e.detail().is_empty());
    play.back(iss, 1).unwrap();
    let id = play.issue(iss, h1, 1, Rail::Bearer).unwrap();
    let e = play.transfer(id, h1, h2, Rail::Ghost).unwrap_err();
    assert_eq!(e.refused_kind(), Some(RefusedKind::Rail), "{e}");
    let e = play.set_rail(id, h1, Rail::Bearer).unwrap_err();
    assert_eq!(e.refused_kind(), Some(RefusedKind::Rail), "{e}");
    play.set_rail(id, h1, Rail::Ghost).unwrap();
    play.transfer(id, h1, h2, Rail::Ghost).unwrap();
    let v = play.voucher(id).unwrap();
    assert_eq!(
        (v.holder, v.rail, v.state),
        (h2, Rail::Ghost, VoucherState::Active)
    );
    let e = play.issue(iss, h1, 4, Rail::Bearer).unwrap_err();
    assert!(
        matches!(e, Error::Usage(_)) && e.status() == sys::ZXV_E_DENOM,
        "{e}"
    );
    let e = play.unback(iss, 1000).unwrap_err();
    assert_eq!(e.refused_kind(), Some(RefusedKind::InsufficientFunds));
    play.redeem(id, h2).unwrap();
    let e = play.redeem(id, h2).unwrap_err();
    assert_eq!(e.refused_kind(), Some(RefusedKind::State));
}

// ===== meter ==============================================================

#[test]
fn meter_vectors() {
    let mv = vino_vec()["meter"].clone();
    let ctx = Context::new().unwrap();
    let cap = ctx.root_capability(1, None, Caps::VINO).unwrap();
    let m = SpendMeter::new(&cap, ju(&mv["daily_cap"])).unwrap();
    for (i, s) in ja(&mv["steps"]).iter().enumerate() {
        let (r, left) = m.admit_with_left(ju(&s["day"]), ju(&s["amount"]));
        want(&format!("meter step {i}"), &r, &s["status"]);
        match s.get("left") {
            Some(l) => assert_eq!(left, Some(ju(l)), "meter step {i}"),
            None => assert_eq!(left, None, "meter step {i}"),
        }
    }
    assert_eq!(m.admit(100, 1).unwrap(), ju(&mv["daily_cap"]) - 1);
    let e = m.admit(100, ju(&mv["daily_cap"])).unwrap_err();
    assert_eq!(e.refused_kind(), Some(RefusedKind::Limit));
    for bad in [0, vino::AMOUNT_MAX + 1] {
        assert_eq!(
            SpendMeter::new(&cap, bad).unwrap_err().status(),
            sys::ZXV_E_RANGE
        );
    }
}

// ===== behaviour: caps, lifetimes, poisoned context =======================

#[test]
fn capability_refusals() {
    let ctx = Context::new().unwrap();
    let root = ctx.root_capability(1, None, Caps::ALL_1_6).unwrap();
    let no_vino = root
        .attenuate(Caps::from_bits(Caps::ALL_1_6.bits() & !Caps::VINO.bits()))
        .unwrap();
    assert!(VinoPlay::new(&no_vino).unwrap_err().is_capability_denied());
    assert!(SpendMeter::new(&no_vino, 10)
        .unwrap_err()
        .is_capability_denied());
    // A sandbox keeps the capability it was made with: revoke it and every
    // call is refused, the buffer path included.
    let sub = root.attenuate(Caps::ALL_1_6).unwrap();
    let play = VinoPlay::new(&sub).unwrap();
    let meter = SpendMeter::new(&sub, 10).unwrap();
    sub.revoke().unwrap();
    assert!(play
        .open(1, PlayAccountKind::Issuer)
        .unwrap_err()
        .is_capability_denied());
    assert!(play.vouchers(0).unwrap_err().is_capability_denied());
    assert!(meter.admit(1, 1).unwrap_err().is_capability_denied());
}

/// Handles release deterministically on drop; use after release cannot
/// compile (see the `compile_fail` doctest in `vino`). Here: many
/// sandboxes come and go, and each one is independent.
#[test]
fn handle_lifetimes() {
    let ctx = Context::new().unwrap();
    let cap = ctx.root_capability(1, None, Caps::VINO).unwrap();
    for round in 0..50u32 {
        let play = VinoPlay::new(&cap).unwrap();
        assert_eq!(play.open(round, PlayAccountKind::Issuer).unwrap(), 0);
        let meter = SpendMeter::new(&cap, 5).unwrap();
        assert_eq!(meter.admit(1, 5).unwrap(), 0);
        drop(meter);
        drop(play);
    }
    // Send + Sync: one sandbox shared across threads.
    let play = VinoPlay::new(&cap).unwrap();
    let iss = play.open(1, PlayAccountKind::Issuer).unwrap();
    let h = play.open(2, PlayAccountKind::Holder).unwrap();
    play.back(iss, 1000).unwrap();
    std::thread::scope(|s| {
        for _ in 0..4 {
            s.spawn(|| {
                for _ in 0..25 {
                    play.issue(iss, h, 1, Rail::Bearer).unwrap();
                }
            });
        }
    });
    assert_eq!(play.vouchers(h).unwrap().len(), 100);
    play.check().unwrap();
}

#[test]
fn poisoned_context() {
    let ctx = Context::new().unwrap();
    let cap = ctx.root_capability(1, None, Caps::ALL_1_6).unwrap();
    let play = VinoPlay::new(&cap).unwrap();
    let meter = SpendMeter::new(&cap, 10).unwrap();
    let iss = play.open(1, PlayAccountKind::Issuer).unwrap();
    ctx.halt("test halt").unwrap();
    let errs = [
        play.back(iss, 5).unwrap_err(),
        play.account(iss).unwrap_err(),
        play.vouchers(iss).unwrap_err(),
        play.check().unwrap_err(),
        meter.admit(1, 1).unwrap_err(),
        VinoPlay::new(&cap).unwrap_err(),
    ];
    for e in errs {
        assert!(e.is_fatal(), "{e}");
        assert_eq!(e.status(), sys::ZXV_E_POISONED, "{e}");
    }
    // Pure functions need no context and keep working.
    vino::receipt_message(&[0; 32], &[0; zxv::sizes::MLDSA87_PK], 1).unwrap();
}
