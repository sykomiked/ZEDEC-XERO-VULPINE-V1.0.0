// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
//! ABI 1.3: the 64-state hexagram tag and quarantine log against
//! vectors.json "hex".

mod common;

use serde_json::Value;
use zxv::{
    Caps, Context, Error, Features, HexAction, HexBase, HexPhase, HexPolarity, Hexagram,
    QuarantineLog, Truth,
};

fn hx() -> Value {
    common::vectors()["hex"].clone()
}

fn u(v: &Value) -> u64 {
    v.as_u64().unwrap()
}

fn names<T: std::fmt::Debug>(all: &[T]) -> Vec<String> {
    all.iter()
        .map(|x| format!("{x:?}").to_lowercase())
        .collect()
}

fn strs(v: &Value) -> Vec<String> {
    v.as_array()
        .unwrap()
        .iter()
        .map(|x| x.as_str().unwrap().to_owned())
        .collect()
}

#[test]
fn tags_match_vectors() {
    let hx = hx();
    assert!(zxv::features().contains(Features::HEX));
    assert_eq!(names(HexBase::ALL), strs(&hx["base_names"]));
    assert_eq!(names(HexPolarity::ALL), strs(&hx["polarity_names"]));
    assert_eq!(names(HexPhase::ALL), strs(&hx["phase_names"]));
    assert_eq!(names(HexAction::ALL), strs(&hx["action_names"]));
    for a in HexAction::ALL {
        assert_eq!(a.name(), format!("{a:?}").to_lowercase());
    }
    for v in hx["tags"].as_array().unwrap() {
        let h = Hexagram::from_tag(u(&v["tag"]) as u8).unwrap();
        assert_eq!(h.name(), v["name"].as_str().unwrap());
        assert_eq!(u64::from(h.base().raw()), u(&v["base"]));
        assert_eq!(u64::from(h.polarity().raw()), u(&v["polarity"]));
        assert_eq!(u64::from(h.phase().raw()), u(&v["phase"]));
        assert_eq!(u64::from(h.action().raw()), u(&v["action"]));
        assert_eq!(h, Hexagram::new(h.base(), h.polarity(), h.phase()));
    }
    for p in HexPhase::ALL {
        assert_eq!(
            u64::from(p.next().raw()),
            u(&hx["phase_next"][p.raw() as usize])
        );
    }
    assert_eq!(Hexagram::all().count() as u32, Hexagram::STATES);
}

#[test]
fn logic_tables() {
    let hx = hx();
    for a in Hexagram::all() {
        let i = a.tag() as usize;
        assert_eq!(u64::from((!a).tag()), u(&hx["not"][i]));
        for b in Hexagram::all() {
            let j = b.tag() as usize;
            assert_eq!(u64::from((a & b).tag()), u(&hx["and"][i][j]));
            assert_eq!(u64::from(a.or(b).tag()), u(&hx["or"][i][j]));
        }
    }
}

#[test]
fn step_and_with_state() {
    let hx = hx();
    for h in Hexagram::all() {
        let i = h.tag() as usize;
        for &p in HexPhase::ALL {
            let want = hx["step"][i][p.raw() as usize].as_i64().unwrap();
            match h.step(p) {
                Ok(n) => assert_eq!(i64::from(n.tag()), want),
                Err(e) => {
                    assert!(want < 0);
                    assert!(matches!(e, Error::Refused(..)));
                    assert_eq!(e.name(), "ZXV_E_STATE");
                }
            }
        }
        for &b in HexBase::ALL {
            for &pol in HexPolarity::ALL {
                let want = hx["set"][i][(b.raw() * 4 + pol.raw()) as usize]
                    .as_i64()
                    .unwrap();
                match h.with_state(b, pol) {
                    Ok(n) => assert_eq!(i64::from(n.tag()), want),
                    Err(e) => assert!(want < 0 && matches!(e, Error::Refused(..))),
                }
            }
        }
        assert_eq!(
            h.forbid(),
            Hexagram::new(h.base(), HexPolarity::Forbidden, h.phase())
        );
        assert_eq!(h.advance().phase(), h.phase().next());
    }
}

#[test]
fn polarity_and_truth_bridge() {
    let hx = hx();
    for c in hx["polarity"].as_array().unwrap() {
        let p = Hexagram::polarity_from_evidence(
            u(&c["pos0"]) as u32,
            u(&c["neg0"]) as u32,
            u(&c["pos1"]) as u32,
            u(&c["neg1"]) as u32,
            c["stale"].as_bool().unwrap(),
        );
        assert_eq!(u64::from(p.raw()), u(&c["polarity"]));
    }
    for t in Truth::ALL {
        let h = Hexagram::from_truth(t, HexPolarity::Neutral, HexPhase::Potential);
        assert_eq!(
            u64::from(h.base().raw()),
            u(&hx["lp_to_base"][t.raw() as usize])
        );
        assert_eq!(h.to_truth(), t);
    }
    for &b in HexBase::ALL {
        let h = Hexagram::new(b, HexPolarity::Neutral, HexPhase::Potential);
        assert_eq!(
            h.to_truth().raw() as u64,
            u(&hx["base_to_lp"][b.raw() as usize])
        );
    }
    // The numbering differs on purpose: Truth::True is 1, HexBase::True is 3.
    assert_eq!(Truth::True.raw(), 1);
    assert_eq!(HexBase::True.raw(), 3);
    assert_eq!(
        Hexagram::from_truth(Truth::True, HexPolarity::Neutral, HexPhase::Potential).base(),
        HexBase::True
    );
}

#[test]
fn refusals_and_value_semantics() {
    for bad in [64u8, 100, 255] {
        let e = Hexagram::from_tag(bad).unwrap_err();
        assert!(matches!(e, Error::Usage(_)));
        assert!(Hexagram::try_from(bad).is_err());
        assert!(HexBase::from_raw(bad).is_err());
    }
    let h = Hexagram::new(HexBase::True, HexPolarity::Negentropic, HexPhase::Manifest);
    assert_eq!(h.tag(), 63);
    assert_eq!(
        format!("{h:?}"),
        "Hexagram(true/negentropic/manifest, tag=63)"
    );
    assert_eq!(h.to_string(), "true/negentropic/manifest");
    let mut f = Hexagram::new(HexBase::True, HexPolarity::Forbidden, HexPhase::Potential);
    assert_eq!(f.action(), HexAction::Rollback);
    for _ in 0..4 {
        f = f.advance();
        assert!(f.is_forbidden());
    }
    assert!((f & h).is_forbidden() && (f | h).is_forbidden() && (!f).is_forbidden());
    assert!(f.with_state(HexBase::True, HexPolarity::Neutral).is_err());
    assert_eq!(
        f.with_state(HexBase::False, HexPolarity::Forbidden)
            .unwrap()
            .base(),
        HexBase::False
    );
}

#[test]
fn quarantine_chain_matches_vectors() {
    let q = hx()["qlog"].clone();
    let ctx = Context::new().unwrap();
    let cap = ctx.root_capability(1, None, Caps::ALL).unwrap();
    let log = QuarantineLog::new(&cap, 3).unwrap();
    assert_eq!(log.count().unwrap(), 0);
    assert_eq!(log.head().unwrap(), [0u8; 32]);
    let mut prev = [0u8; 32];
    for r in q["records"].as_array().unwrap() {
        let subject = common::arr32(&r["subject"]);
        let tag = Hexagram::from_tag(u(&r["tag"]) as u8).unwrap();
        let seq = log
            .append(
                tag,
                u(&r["reason"]) as u8,
                u(&r["submitter"]) as u32,
                &subject,
            )
            .unwrap();
        assert_eq!(seq, u(&r["seq"]));
        let rec = log.record(seq as u32).unwrap();
        assert_eq!(rec.hash, common::arr32(&r["hash"]));
        assert_eq!(rec.prev, prev);
        assert_eq!(rec.subject, subject);
        assert_eq!(rec.tag, tag);
        assert_eq!(u64::from(rec.reason), u(&r["reason"]));
        assert_eq!(u64::from(rec.submitter), u(&r["submitter"]));
        prev = rec.hash;
    }
    assert_eq!(log.head().unwrap(), common::arr32(&q["head"]));
    assert_eq!(
        log.records()
            .unwrap()
            .iter()
            .map(|r| r.seq)
            .collect::<Vec<_>>(),
        [0, 1, 2]
    );
    log.verify().unwrap();
    let e = log
        .append(Hexagram::from_tag(0x3B).unwrap(), 0, 0, &[0; 32])
        .unwrap_err();
    assert_eq!(e.name(), "ZXV_E_CAPACITY");
    assert!(matches!(e, Error::Refused(..)));
}

#[test]
fn quarantine_refusals() {
    let forb = Hexagram::new(HexBase::Both, HexPolarity::Forbidden, HexPhase::Emerging);
    let ctx = Context::new().unwrap();
    let cap = ctx.root_capability(1, None, Caps::ALL).unwrap();
    let no_attest = cap
        .attenuate(Caps::from_bits(Caps::ALL.bits() & !Caps::ATTEST.bits()))
        .unwrap();
    let e = QuarantineLog::new(&no_attest, 4).unwrap_err();
    assert_eq!(e.name(), "ZXV_E_CAP");
    assert!(matches!(
        QuarantineLog::new(&cap, 0).unwrap_err(),
        Error::Usage(_)
    ));
    assert!(matches!(
        QuarantineLog::new(&cap, QuarantineLog::MAX_RECORDS + 1).unwrap_err(),
        Error::Usage(_)
    ));
    let log = QuarantineLog::new(&cap, 4).unwrap();
    let ok = Hexagram::new(HexBase::True, HexPolarity::Neutral, HexPhase::Emerging);
    assert!(matches!(
        log.append(ok, 1, 1, &[0; 32]).unwrap_err(),
        Error::Usage(_)
    ));
    assert_eq!(log.append(forb, 9, u32::MAX, &[0; 32]).unwrap(), 0);
    assert_eq!(log.record(0).unwrap().submitter, u32::MAX);
    ctx.halt("test").unwrap();
    assert!(log.count().unwrap_err().is_fatal());
}
