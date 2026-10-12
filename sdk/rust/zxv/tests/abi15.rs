// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
//! ABI 1.5: the treaty, cid/aipi, coin, fee, vhub, freight, web4 and
//! paradox sections of vectors.json (refusals included), plus capability
//! refusals, handle lifetimes, buffer growth, error mapping and a poisoned
//! context.

mod common;

use std::collections::HashMap;

use serde_json::Value;
use zxv::aipi::{self, AipiClaim, AipiFunction, AipiIntent, Cid, CidBase};
use zxv::coin::{self, Octane, TokenHolding, TokenWallet};
use zxv::fee;
use zxv::freight::{
    self, FreightGen, FreightHeader, FreightHeaderFields, FreightJoin, FreightOpsWriter,
};
use zxv::paradox::{
    self, ParadoxBook, ParadoxClass, ParadoxContainment, ParadoxEvidence, ParadoxGraph,
    ParadoxHold, ParadoxKind, ParadoxLog, ParadoxNodeState, ParadoxOutcome, ParadoxPolicy,
};
use zxv::treaty::{self, Treaty, TreatyFault, TreatyOp};
use zxv::vhub::{self, VHub};
use zxv::web4::{
    self, ConsentLog, ConsentNeed, ConsentRequest, W4Agent, W4Directory, W4Manifest, W4Message,
    W4MsgKind, W4Tool,
};
use zxv::{
    sys, AipiNode, Capability, Caps, Context, Error, Features2, Hashes, Hexagram, MlDsa87,
    RefusedKind, StatusClass,
};

// ----- JSON helpers (64-bit numbers are decimal strings) -----------------

fn ji(v: &Value) -> i64 {
    match v {
        Value::Number(n) => n.as_i64().unwrap_or_else(|| n.as_f64().unwrap() as i64),
        Value::String(s) => s.parse().unwrap(),
        Value::Null => 0,
        _ => panic!("not a number: {v}"),
    }
}

fn ju(v: &Value) -> u64 {
    match v {
        Value::Number(n) => n.as_u64().unwrap(),
        Value::String(s) => s.parse().unwrap(),
        Value::Null => 0,
        _ => panic!("not a number: {v}"),
    }
}

fn js(v: &Value) -> &str {
    v.as_str().unwrap_or("")
}

fn jb(v: &Value) -> bool {
    v.as_bool().unwrap_or(false)
}

fn ja(v: &Value) -> &Vec<Value> {
    v.as_array().expect("array")
}

fn hx(v: &Value) -> Vec<u8> {
    zxv::hex_decode(js(v))
}

fn a32(v: &Value) -> [u8; 32] {
    hx(v).try_into().unwrap()
}

fn i64s(v: &Value) -> Vec<i64> {
    v.as_array()
        .map(|a| a.iter().map(ji).collect())
        .unwrap_or_default()
}

/// The status name of a result ("ZXV_OK" for Ok).
fn st<T>(r: &Result<T, Error>) -> &'static str {
    match r {
        Ok(_) => "ZXV_OK",
        Err(e) => zxv::status_name(e.status()),
    }
}

fn want<T: std::fmt::Debug>(label: &str, r: &Result<T, Error>, w: &Value) {
    assert_eq!(st(r), js(w), "{label}: {r:?}");
}

fn ramp(n: usize) -> Vec<u8> {
    (0..n).map(|i| i as u8).collect()
}

fn sha3(b: &[u8]) -> Vec<u8> {
    Hashes::sha3_256(b).to_vec()
}

fn tag(v: &Value) -> Hexagram {
    Hexagram::from_tag(ji(v) as u8).unwrap()
}

fn class(m: &Value) -> ParadoxClass {
    ParadoxClass {
        kind: ParadoxKind::from_raw(ji(&m["kind"]) as i32),
        tag: tag(&m["tag"]),
    }
}

fn section(v: &Value, name: &str) -> Value {
    v[name].clone()
}

// ===== statuses, caps, features ===========================================

#[test]
fn statuses_caps_features() {
    let (_, minor, _) = zxv::version();
    assert!(minor >= 5);
    let v = common::vectors();
    let rows = ja(&v["status_abi15"]);
    assert_eq!(rows.len(), 9);
    let kinds = [
        (sys::ZXV_E_CID, RefusedKind::Cid),
        (sys::ZXV_E_CONSENT, RefusedKind::Consent),
        (sys::ZXV_E_ERASURE, RefusedKind::Erasure),
        (sys::ZXV_E_PEG, RefusedKind::Peg),
        (sys::ZXV_E_LIMIT, RefusedKind::Limit),
        (sys::ZXV_E_PARADOX_STEP, RefusedKind::ParadoxStep),
        (sys::ZXV_E_PARADOX_OWNER, RefusedKind::ParadoxOwner),
    ];
    for r in rows {
        let code = ji(&r["code"]) as i32;
        assert_eq!(zxv::status_name(code), js(&r["name"]));
        let e = Error::new(code, "detail");
        assert_eq!(e.status(), code);
        match ji(&r["class"]) {
            1 => {
                assert_eq!(StatusClass::of(code), StatusClass::Usage);
                assert!(matches!(e, Error::Usage(_)), "{e}");
            }
            2 => {
                assert_eq!(StatusClass::of(code), StatusClass::Refused);
                let k = kinds.iter().find(|(c, _)| *c == code).unwrap().1;
                assert_eq!(e.refused_kind(), Some(k));
            }
            c => panic!("class {c}"),
        }
    }
    assert_eq!(Caps::AIPI.bits(), 1 << 12);
    assert_eq!(Caps::MINT.bits(), 1 << 13);
    assert_eq!(Caps::PARADOX.bits(), 1 << 14);
    assert_eq!(Caps::ALL_1_5.bits(), (1 << 15) - 1);
    assert!(Caps::ALL_1_5.contains(Caps::ALL_1_4 | Caps::AIPI | Caps::MINT | Caps::PARADOX));
    assert!(!Caps::ALL_1_4.contains(Caps::AIPI));
    assert_eq!(
        format!("{:?}", Caps::AIPI | Caps::PARADOX),
        "Caps(AIPI | PARADOX)"
    );
    let f2 = zxv::features2();
    for f in [
        Features2::TREATY,
        Features2::AIPI,
        Features2::COIN,
        Features2::FEE,
        Features2::VHUB,
        Features2::FREIGHT,
        Features2::WEB4,
        Features2::PARADOX,
    ] {
        assert!(f2.contains(f), "{f:?} missing from {f2:?}");
    }
    let ctx = Context::new().unwrap();
    let c = ctx.root_capability(1, None, Caps::ALL_1_5).unwrap();
    c.check(Caps::AIPI | Caps::MINT | Caps::PARADOX).unwrap();
    let e = ctx
        .root_capability(1, None, Caps::from_bits(1 << 15))
        .unwrap_err();
    assert_eq!(e.status(), sys::ZXV_E_ARG);
}

// ===== treaty ==============================================================

#[test]
fn treaty_vectors() {
    let v = section(&common::vectors(), "treaty");
    assert_eq!(ji(&v["rec_len"]) as usize, treaty::REC_LEN);
    assert_eq!(ji(&v["result_len"]) as usize, treaty::RESULT_LEN);
    for (k, name) in v["op_names"].as_object().unwrap() {
        let op = match k.as_str() {
            "1" => TreatyOp::Net,
            "2" => TreatyOp::Setl,
            _ => TreatyOp::None,
        };
        assert_eq!(op.to_string(), js(name));
    }
    let mut rec0 = Vec::new();
    for (i, m) in ja(&v["sutra"]).iter().enumerate() {
        let tr = Treaty::parse_sutra(js(&m["sutra"])).map_err(Error::from);
        want(&format!("sutra {i}"), &tr, &m["status"]);
        let tr = tr.unwrap();
        let rec = tr.record().unwrap();
        assert_eq!(rec, hx(&m["record"]), "sutra {i} record");
        if i == 0 {
            rec0 = rec.clone();
        }
        let u = Treaty::parse_record(&rec).unwrap();
        assert_eq!(tr.equal(&u).unwrap(), jb(&m["record_parses_equal"]));
        assert_eq!(u.sutra().unwrap(), js(&m["reemit"]));
        assert_eq!(tr.name().unwrap(), js(&m["name"]));
        assert_eq!(tr.asset().unwrap(), js(&m["asset"]));
        let (op, np, no) = tr.header().unwrap();
        let want_op = match ji(&m["op"]) {
            1 => TreatyOp::Net,
            2 => TreatyOp::Setl,
            _ => TreatyOp::None,
        };
        assert_eq!(
            (op, i64::from(np), i64::from(no)),
            (want_op, ji(&m["party_count"]), ji(&m["owe_count"]))
        );
        let parties = tr.parties().unwrap();
        for (p, pm) in ja(&m["parties"]).iter().enumerate() {
            let w = ja(&pm["weight"]);
            let g = &parties[p];
            assert_eq!(g.id, js(&pm["id"]));
            assert_eq!((g.deposit, g.claim), (ji(&pm["deposit"]), ji(&pm["claim"])));
            assert_eq!(
                (i64::from(g.weight_num), i64::from(g.weight_den)),
                (ji(&w[0]), ji(&w[1]))
            );
        }
        let owes = tr.owes().unwrap();
        for (o, om) in ja(&m["owes"]).iter().enumerate() {
            assert_eq!(i64::from(owes[o].from), ji(&om["from"]));
            assert_eq!(i64::from(owes[o].to), ji(&om["to"]));
            assert_eq!(owes[o].amount, ji(&om["amount"]));
        }
        tr.validate().unwrap();
    }
    for (i, m) in ja(&v["reject"]).iter().enumerate() {
        let e = Treaty::parse_sutra(js(&m["sutra"])).unwrap_err();
        assert_eq!(
            zxv::status_name(e.error.status()),
            js(&m["status"]),
            "reject {i}"
        );
        assert_eq!(i64::from(e.fault.raw()), ji(&m["fault"]), "reject {i}: {e}");
        assert_eq!(i64::from(e.offset), ji(&m["offset"]), "reject {i}");
        assert_eq!(e.error.class(), StatusClass::Usage);
    }
    for (i, m) in ja(&v["reject_record"]).iter().enumerate() {
        let mut rec = rec0.clone();
        rec[ji(&m["patch_offset"]) as usize] = hx(&m["patch_byte"])[0];
        let e = Treaty::parse_record(&rec).unwrap_err();
        assert_eq!(
            zxv::status_name(e.error.status()),
            js(&m["status"]),
            "reject_record {i}"
        );
        assert_eq!(
            i64::from(e.fault.raw()),
            ji(&m["fault"]),
            "reject_record {i}"
        );
        assert_eq!(i64::from(e.offset), ji(&m["offset"]), "reject_record {i}");
    }
    for (i, m) in ja(&v["pic"]).iter().enumerate() {
        let (a, b) = (ji(&m["a"]), ji(&m["b"]));
        let r = match js(&m["op"]) {
            "add" => treaty::pic_add(a, b).map(|o| (o, 0)),
            "sub" => treaty::pic_sub(a, b).map(|o| (o, 0)),
            "mul" => treaty::pic_mul(a, b).map(|o| (o, 0)),
            _ => treaty::pic_div(a, b),
        };
        want(&format!("pic {i}"), &r, &m["status"]);
        if let Ok((out, rem)) = r {
            assert_eq!(out, ji(&m["out"]), "pic {i}");
            if !m["rem"].is_null() {
                assert_eq!(rem, ji(&m["rem"]), "pic {i}");
            }
        }
    }
    for (i, m) in ja(&v["run"]).iter().enumerate() {
        let tr = Treaty::parse_sutra(js(&m["sutra"])).unwrap();
        let r = tr.run().map_err(Error::from);
        want(&format!("run {i}"), &r, &m["status"]);
        let Ok(res) = r else { continue };
        let (np, nl, fees) = res.summary().unwrap();
        assert_eq!(
            (i64::from(np), i64::from(nl), fees),
            (ji(&m["parties"]), ji(&m["legs"]), ji(&m["fees"]))
        );
        let ps = res.parties().unwrap();
        for (p, pm) in ja(&m["party"]).iter().enumerate() {
            assert_eq!(
                (ps[p].net, ps[p].balance, ps[p].dividend),
                (ji(&pm["net"]), ji(&pm["balance"]), ji(&pm["dividend"])),
                "run {i} party {p}"
            );
        }
        let legs = res.legs().unwrap();
        for (l, lm) in ja(&m["leg"]).iter().enumerate() {
            assert_eq!(
                (
                    i64::from(legs[l].from),
                    i64::from(legs[l].to),
                    legs[l].amount
                ),
                (ji(&lm["from"]), ji(&lm["to"]), ji(&lm["amount"]))
            );
        }
        assert_eq!(
            res.record().unwrap(),
            hx(&m["result_record"]),
            "run {i} record"
        );
    }
    // An empty treaty fails validation with the NAME fault, and its emits
    // carry the same fault.
    let empty = Treaty::new().unwrap();
    assert_eq!(empty.validate().unwrap_err().fault, TreatyFault::Name);
    assert_eq!(empty.sutra().unwrap_err().fault, TreatyFault::Name);
}

// ===== cid and aipi ========================================================

fn check_text(label: &str, c: &Cid, base: CidBase, w: &Value) {
    let r = c.text(base);
    let w = js(w);
    if w.starts_with("ZXV_") {
        assert_eq!(st(&r), w, "{label}");
    } else {
        assert_eq!(r.unwrap(), w, "{label}");
    }
}

fn check_info(label: &str, c: &Cid, m: &Value) {
    let i = c.info().unwrap();
    assert_eq!(i64::from(i.version), ji(&m["version"]), "{label}");
    assert_eq!(i64::from(i.codec), ji(&m["codec"]), "{label}");
    assert_eq!(i64::from(i.hash), ji(&m["hash"]), "{label}");
    assert_eq!(i.digest, hx(&m["digest"]), "{label}");
}

#[test]
fn cid_vectors() {
    let v = section(&common::vectors(), "cid");
    for (i, m) in ja(&v["compute"]).iter().enumerate() {
        let r = Cid::compute(ji(&m["codec"]) as u32, js(&m["utf8"]).as_bytes());
        want(&format!("compute {i}"), &r, &m["status"]);
        let c = r.unwrap();
        assert_eq!(c.as_bytes(), hx(&m["cid"]).as_slice());
        check_text("compute default", &c, CidBase::Default, &m["text_default"]);
        check_text("compute 58btc", &c, CidBase::Base58Btc, &m["text_58btc"]);
        check_info("compute", &c, m);
        c.verify(js(&m["utf8"]).as_bytes()).unwrap();
    }
    for (i, m) in ja(&v["unixfs"]).iter().enumerate() {
        let data = if jb(&m["ramp"]) {
            ramp(ji(&m["len"]) as usize)
        } else {
            js(&m["utf8"]).as_bytes().to_vec()
        };
        let r = Cid::unixfs(&data);
        want(&format!("unixfs {i}"), &r, &m["status"]);
        let c = r.unwrap();
        assert_eq!(c.as_bytes(), hx(&m["cid"]).as_slice(), "unixfs {i}");
        check_text("unixfs text", &c, CidBase::Default, &m["text_default"]);
        if !m["codec"].is_null() {
            assert_eq!(i64::from(c.info().unwrap().codec), ji(&m["codec"]));
        }
    }
    for (i, m) in ja(&v["text"]).iter().enumerate() {
        let r = Cid::parse(js(&m["text"]));
        want(&format!("text {i}"), &r, &m["status"]);
        let c = r.unwrap();
        assert_eq!(c.as_bytes(), hx(&m["cid"]).as_slice());
        check_text("text default", &c, CidBase::Default, &m["text_default"]);
        check_text("text 58btc", &c, CidBase::Base58Btc, &m["text_58btc"]);
        check_info("text", &c, m);
        let v1 = c.to_v1().unwrap();
        assert_eq!(v1.as_bytes(), hx(&m["v1"]).as_slice());
        check_text("text v1", &v1, CidBase::Default, &m["v1_text"]);
        assert_eq!(c.provider_key().unwrap().to_vec(), hx(&m["provider_key"]));
        assert_eq!(
            c,
            js(&m["text"]).parse::<Cid>().unwrap(),
            "Cid compares by value"
        );
    }
    for (i, m) in ja(&v["text_reject"]).iter().enumerate() {
        let r = Cid::parse(js(&m["text"]));
        want(&format!("text_reject {i}"), &r, &m["status"]);
        if js(&m["status"]) == "ZXV_E_CID" {
            assert_eq!(r.unwrap_err().refused_kind(), Some(RefusedKind::Cid));
        }
    }
    for (i, m) in ja(&v["verify"]).iter().enumerate() {
        let r = Cid::from_bytes(&hx(&m["cid"])).and_then(|c| c.verify(js(&m["utf8"]).as_bytes()));
        want(&format!("verify {i}"), &r, &m["status"]);
    }
}

#[test]
fn aipi_vectors() {
    let v = section(&common::vectors(), "aipi");
    let module = Cid::from_bytes(&hx(&v["module_cid"])).unwrap();
    let contract = Cid::from_bytes(&hx(&v["contract_cid"])).unwrap();
    assert_eq!(
        Cid::compute(aipi::CODEC_RAW, js(&v["module_utf8"]).as_bytes()).unwrap(),
        module
    );
    let mut fn0 = Cid::default();
    for (i, m) in ja(&v["fn"]).iter().enumerate() {
        let f = AipiFunction {
            intent: AipiIntent::from_raw(ji(&m["intent"]) as u32).unwrap(),
            verify: aipi::AipiVerify::from_raw(ji(&m["verify"]) as u32).unwrap(),
            privacy: aipi::AipiPrivacy::from_raw(ji(&m["privacy"]) as u32).unwrap(),
            determinism: aipi::AipiDeterminism::from_raw(ji(&m["determinism"]) as u32).unwrap(),
            quorum_n: ji(&m["quorum_n"]) as u32,
            quorum_k: ji(&m["quorum_k"]) as u32,
            fuel_max: ju(&m["fuel_max"]),
            module,
            contract: jb(&m["contract"]).then_some(contract),
            entry: js(&m["entry"]).to_string(),
            checker: js(&m["checker"]).to_string(),
            certifier: js(&m["certifier"]).to_string(),
        };
        let r = f.encode();
        want(&format!("fn {i}"), &r, &m["status"]);
        let Ok(rec) = r else { continue };
        assert_eq!(rec, hx(&m["record"]), "fn {i}");
        assert_eq!(AipiFunction::decode(&rec).unwrap(), f, "fn {i} round trip");
        let c = AipiFunction::cid_of(&rec).unwrap();
        assert_eq!(c.as_bytes(), hx(&m["fn_cid"]).as_slice());
        check_text("fn text", &c, CidBase::Base58Btc, &m["fn_cid_text"]);
        if i == 0 {
            fn0 = c;
        }
    }
    let mut call1 = Cid::default();
    for (i, m) in ja(&v["call"]).iter().enumerate() {
        if js(&m["fn"]) == "module_cid" {
            want(
                "call with a module cid",
                &aipi::encode_call(&module, &[]),
                &m["status"],
            );
            continue;
        }
        let input = js(&m["input_utf8"]).as_bytes();
        let rec = aipi::encode_call(&fn0, input).unwrap();
        assert_eq!(rec, hx(&m["record"]), "call {i}");
        assert_eq!(aipi::decode_call(&rec).unwrap(), (fn0, input.to_vec()));
        let c = aipi::call_cid(&fn0, input).unwrap();
        assert_eq!(c.as_bytes(), hx(&m["call_cid"]).as_slice());
        check_text("call text", &c, CidBase::Base58Btc, &m["call_cid_text"]);
        if i == 1 {
            call1 = c;
        }
    }
    for (i, m) in ja(&v["claim"]).iter().enumerate() {
        let cl = AipiClaim {
            call: call1,
            executor: a32(&m["executor"]),
            output: js(&m["output_utf8"]).as_bytes().to_vec(),
            cert: js(&m["cert_utf8"]).as_bytes().to_vec(),
        };
        let rec = cl.encode().unwrap();
        assert_eq!(rec, hx(&m["record"]), "claim {i}");
        assert_eq!(AipiClaim::decode(&rec).unwrap(), cl);
    }
}

fn env15() -> Context {
    Context::new().unwrap()
}

#[test]
fn aipi_node() {
    let ctx = env15();
    let cap = ctx.root_capability(1, None, Caps::ALL_1_5).unwrap();
    let node = AipiNode::new(&cap, aipi::STORE_MIN, 64, Some(&[9; 32])).unwrap();
    let data = b"zxv module bytes";
    let c = node.put(data, false).unwrap();
    assert_eq!(c, Cid::compute(aipi::CODEC_RAW, data).unwrap());
    assert_eq!(node.get(&c).unwrap(), data);
    assert_eq!(node.put(data, true).unwrap_err().status(), sys::ZXV_E_STATE);
    let missing = Cid::compute(aipi::CODEC_RAW, b"absent").unwrap();
    assert_eq!(
        node.get(&missing).unwrap_err().status(),
        sys::ZXV_E_NOT_FOUND
    );
    let f = AipiFunction {
        intent: AipiIntent::Fn,
        verify: aipi::AipiVerify::Checker,
        privacy: aipi::AipiPrivacy::Public,
        determinism: aipi::AipiDeterminism::Deterministic,
        quorum_n: 0,
        quorum_k: 0,
        fuel_max: 10,
        module: c,
        contract: None,
        entry: "main".into(),
        checker: "check".into(),
        certifier: String::new(),
    };
    let rec = f.encode().unwrap();
    let fc = node.publish_function(&rec).unwrap();
    assert_eq!(node.load_function(&fc).unwrap(), rec);
    assert_eq!(node.plan(&fc, &[], false).unwrap(), aipi::AipiRoute::Local);
    let info = node.info().unwrap();
    assert!(info.blocks >= 2 && info.gets_verified >= 1, "{info:?}");
}

// ===== coin ================================================================

fn holding(v: &Value) -> TokenHolding {
    let mut h = TokenHolding::default();
    for p in ja(v) {
        let pair = ja(p);
        h.0[ji(&pair[0]) as usize] = ju(&pair[1]);
    }
    h
}

#[test]
fn coin_vectors() {
    let v = section(&common::vectors(), "coin");
    assert_eq!(i64::from(coin::COIN_RUNGS), ji(&v["rungs"]));
    assert_eq!(coin::COIN_TOP_FACE, ju(&v["top_face"]));
    for (i, m) in ja(&v["face"]).iter().enumerate() {
        let r = coin::coin_face(ji(&m["rung"]) as u32);
        want(&format!("face {i}"), &r, &m["status"]);
        if let Ok(f) = r {
            assert_eq!(f, ju(&m["face"]));
        }
    }
    for (i, m) in ja(&v["rung_of"]).iter().enumerate() {
        let r = coin::coin_rung(ju(&m["face"]));
        want(&format!("rung_of {i}"), &r, &m["status"]);
        match r {
            Ok(x) => assert_eq!(i64::from(x), ji(&m["rung"])),
            Err(e) if e.status() == sys::ZXV_E_DENOM => assert!(matches!(e, Error::Usage(_))),
            Err(_) => {}
        }
    }
    for (i, m) in ja(&v["split"]).iter().enumerate() {
        let r = coin::coin_split_count(ju(&m["amount"]));
        want(&format!("split count {i}"), &r, &m["status"]);
        let Ok(n) = r else { continue };
        assert_eq!(n, ju(&m["count"]));
        if !m["rungs"].is_null() {
            let rungs: Vec<i64> = coin::coin_split(ju(&m["amount"]))
                .unwrap()
                .into_iter()
                .map(i64::from)
                .collect();
            assert_eq!(rungs, i64s(&m["rungs"]), "split {i}");
        }
    }
    for (i, m) in ja(&v["token_face"]).iter().enumerate() {
        let r = coin::token_face(ji(&m["tier"]) as u32);
        want(&format!("token_face {i}"), &r, &m["status"]);
        if let Ok((f, s)) = r {
            assert_eq!((f, s), (ju(&m["face"]), jb(&m["super"])));
        }
    }
    for (i, m) in ja(&v["token_tier"]).iter().enumerate() {
        let r = coin::token_tier(ju(&m["face"]));
        want(&format!("token_tier {i}"), &r, &m["status"]);
        if let Ok(t) = r {
            assert_eq!(i64::from(t), ji(&m["tier"]));
        }
    }
    for (i, m) in ja(&v["token_value"]).iter().enumerate() {
        let r = holding(&m["counts"]).value();
        want(&format!("token_value {i}"), &r, &m["status"]);
        if let Ok(x) = r {
            assert_eq!(x, (ju(&m["value"]), ju(&m["pieces"])));
        }
    }
    for (i, m) in ja(&v["token_canonical"]).iter().enumerate() {
        let r = TokenHolding::canonical(ju(&m["value"]), Octane(ji(&m["octane"]) as u32));
        want(&format!("token_canonical {i}"), &r, &m["status"]);
        let Ok(h) = r else { continue };
        assert_eq!(h, holding(&m["counts"]), "token_canonical {i}");
        assert_eq!(h.value().unwrap().1, ju(&m["pieces"]));
    }
    for (i, m) in ja(&v["token_is_canonical"]).iter().enumerate() {
        let r = holding(&m["counts"]).is_canonical(Octane(ji(&m["octane"]) as u32));
        want(&format!("token_is_canonical {i}"), &r, &m["status"]);
        if let Ok(c) = r {
            assert_eq!(c, jb(&m["canonical"]));
        }
    }
    for (i, m) in ja(&v["token_compress"]).iter().enumerate() {
        let mut h = holding(&m["counts"]);
        let r = h.compress(Octane(ji(&m["octane"]) as u32));
        want(&format!("token_compress {i}"), &r, &m["status"]);
        assert_eq!(h, holding(&m["out"]), "token_compress {i}");
    }
    for (i, m) in ja(&v["token_fuse_split"]).iter().enumerate() {
        let mut h = holding(&m["counts"]);
        let tier = ji(&m["tier"]) as u32;
        let r = if js(&m["op"]) == "fuse" {
            h.fuse(Octane(ji(&m["octane"]) as u32), tier)
        } else {
            h.split(tier)
        };
        want(&format!("token_fuse_split {i}"), &r, &m["status"]);
        assert_eq!(h, holding(&m["out"]), "token_fuse_split {i}");
    }
    for (i, m) in ja(&v["token_code"]).iter().enumerate() {
        let r = coin::token_code_bits(ju(&m["value"]));
        want(&format!("token_code {i}"), &r, &m["status"]);
        let Ok(bits) = r else { continue };
        assert_eq!(i64::from(bits), ji(&m["bits"]));
        assert_eq!(js(&m["code"]).len(), bits as usize);
        let (b, nbits) = coin::encode_token_codes(&[ju(&m["value"])]).unwrap();
        assert_eq!(
            (nbits, b.clone()),
            (bits, hx(&m["bytes"])),
            "token_code {i}"
        );
        assert_eq!(
            coin::get_token_code(&b, 0).unwrap(),
            (ju(&m["value"]), bits)
        );
    }
    let s = &v["token_code_stream"];
    let vals: Vec<u64> = ja(&s["values"]).iter().map(ju).collect();
    let (b, bits) = coin::encode_token_codes(&vals).unwrap();
    assert_eq!(
        (i64::from(bits), b.clone()),
        (ji(&s["bits"]), hx(&s["bytes"]))
    );
    let back: Vec<u64> = coin::token_codes(&b, vals.len())
        .collect::<zxv::Result<_>>()
        .unwrap();
    assert_eq!(back, vals);
    for (i, m) in ja(&v["token_code_get_reject"]).iter().enumerate() {
        let r = coin::get_token_code(&hx(&m["bytes"]), ji(&m["bit"]) as u32);
        want(&format!("token_code_get_reject {i}"), &r, &m["status"]);
    }
}

#[test]
fn token_wallet() {
    let ctx = env15();
    let cap = ctx.root_capability(1, None, Caps::ALL_1_5).unwrap();
    let a = TokenWallet::new(&cap, 1, Octane::MIDGRADE).unwrap();
    let b = TokenWallet::new(&cap, 2, Octane::PLAIN).unwrap();
    a.mint(100).unwrap();
    a.transfer(&b, 30).unwrap();
    a.spend(20).unwrap();
    assert_eq!(
        b.spend(31).unwrap_err().refused_kind(),
        Some(RefusedKind::InsufficientFunds)
    );
    let (ia, ib) = (a.info().unwrap(), b.info().unwrap());
    assert_eq!(
        (ia.value, ib.value, ib.pieces, ia.octane),
        (50, 30, 30, Octane::MIDGRADE)
    );
    assert!(a.counts().unwrap().is_canonical(Octane::MIDGRADE).unwrap());
    // Changing octane re-expresses the same value: no premium.
    a.set_octane(Octane::PLAIN).unwrap();
    let ia = a.info().unwrap();
    assert_eq!((ia.value, ia.pieces), (50, 50));
}

// ===== fee =================================================================

#[test]
fn fee_vectors() {
    let v = section(&common::vectors(), "fee");
    assert_eq!((fee::FEE_NUM, fee::FEE_DEN), (ju(&v["num"]), ju(&v["den"])));
    for (i, m) in ja(&v["assure"]).iter().enumerate() {
        assert_eq!(
            fee::assurance_fee(ju(&m["gross"])).unwrap(),
            ju(&m["fee"]),
            "assure {i}"
        );
    }
    for (i, m) in ja(&v["carry"]).iter().enumerate() {
        let mut rem = ju(&m["rem_in"]);
        for (k, s) in ja(&m["steps"]).iter().enumerate() {
            let r = fee::assurance_fee_carry(rem, ju(&s["gross"]));
            want(&format!("carry {i}/{k}"), &r, &s["status"]);
            let (f, r) = r.unwrap();
            assert_eq!((f, r), (ju(&s["fee"]), ju(&s["rem"])), "carry {i}/{k}");
            rem = r;
        }
    }
    for (i, m) in ja(&v["carry_reject"]).iter().enumerate() {
        let r = fee::assurance_fee_carry(ju(&m["rem_in"]), ju(&m["gross"]));
        want(&format!("carry_reject {i}"), &r, &m["status"]);
    }
    let buckets = [
        fee::FeeBucket::ReserveFloor,
        fee::FeeBucket::VBillDividend,
        fee::FeeBucket::InfraBounty,
        fee::FeeBucket::RegenCapital,
    ];
    for (i, n) in ja(&v["buckets"]).iter().enumerate() {
        assert_eq!(buckets[i].name(), js(n));
    }
    for (i, m) in ja(&v["split"]).iter().enumerate() {
        let s = fee::split_fee(ju(&m["fee"])).unwrap();
        let parts: Vec<u64> = ja(&m["parts"]).iter().map(ju).collect();
        assert_eq!(s.0.to_vec(), parts, "split {i}");
        assert_eq!(s.total(), ju(&m["fee"]));
        assert_eq!(s.get(fee::FeeBucket::ReserveFloor), parts[0]);
    }
    let p = fee::fee_policy_default().unwrap();
    assert_eq!(p.percent, [50, 25, 15, 10]);
    assert!(!p.allow_below);
    assert_eq!(p.commons_cap, fee::FeeRate { num: 8, den: 21 });
    assert_eq!(p.multiplier, fee::FeeRate { num: 1, den: 1 });
}

// ===== vhub ================================================================

#[test]
fn vhub_vectors() {
    let v = section(&common::vectors(), "vhub");
    for (i, m) in ja(&v["peg_msg"]).iter().enumerate() {
        let msg = vhub::peg_message(
            ji(&m["slot"]) as u8,
            ju(&m["num"]),
            ju(&m["den"]),
            ju(&m["epoch"]),
        )
        .unwrap();
        assert_eq!(msg.to_vec(), hx(&m["msg"]), "peg_msg {i}");
    }
    let sc = &v["scenario"];
    let ctx = env15();
    let cap = ctx.root_capability(1, None, Caps::ALL_1_5).unwrap();
    let dsa = MlDsa87::new(&cap);
    let mut pks = Vec::new();
    let mut sks = Vec::new();
    for (k, m) in ja(&sc["keys"]).iter().enumerate() {
        let (pk, sk) = dsa.keygen_from_seed(&a32(&m["seed"])).unwrap();
        assert_eq!(sha3(&pk), hx(&m["pk_sha3_256"]), "key {k}");
        pks.push(pk);
        sks.push(sk);
    }
    let hub = VHub::new(&cap, Some(&a32(&sc["hub_seed"]))).unwrap();
    for (i, m) in ja(&sc["currencies"]).iter().enumerate() {
        let slot = ji(&m["slot"]) as u8;
        let r = if m["issuer_key"].is_null() {
            hub.add_currency(slot, js(&m["code"]), 2, &pks[0], 0, 0)
        } else {
            hub.add_currency(
                slot,
                js(&m["code"]),
                ji(&m["minor_units"]) as u8,
                &pks[ji(&m["issuer_key"]) as usize],
                ju(&m["pool_funding"]),
                ju(&m["vbill_backing"]),
            )
        };
        want(&format!("currency {i}"), &r, &m["status"]);
        if r.is_ok() {
            let c = hub.currency(slot).unwrap();
            assert_eq!(
                (
                    i64::from(c.issuer_account),
                    i64::from(c.pool_account),
                    i64::from(c.float_account)
                ),
                (
                    ji(&m["issuer_account"]),
                    ji(&m["pool_account"]),
                    ji(&m["vfloat_account"])
                )
            );
        }
    }
    for (i, m) in ja(&sc["pegs"]).iter().enumerate() {
        let slot = ji(&m["slot"]) as u8;
        let msg = vhub::peg_message(slot, ju(&m["num"]), ju(&m["den"]), ju(&m["epoch"])).unwrap();
        let sig = dsa
            .sign_deterministic(&sks[ji(&m["signer"]) as usize], &msg, &[])
            .unwrap();
        assert_eq!(sha3(&sig), hx(&m["sig_sha3_256"]), "peg {i} signature");
        let r = hub.set_peg(slot, ju(&m["num"]), ju(&m["den"]), ju(&m["epoch"]), &sig);
        want(&format!("peg {i}"), &r, &m["status"]);
    }
    for (i, m) in ja(&sc["accounts"]).iter().enumerate() {
        let a = hub
            .open_account(ji(&m["slot"]) as u8, ji(&m["owner"]) as u32)
            .unwrap();
        assert_eq!(i64::from(a), ji(&m["account"]), "account {i}");
        if !m["issued"].is_null() {
            hub.issue(a, ju(&m["issued"])).unwrap();
        }
    }
    for (i, m) in ja(&sc["quotes"]).iter().enumerate() {
        let r = hub.quote(ji(&m["from"]) as u8, ji(&m["to"]) as u8, ju(&m["want"]));
        want(&format!("quote {i}"), &r, &m["status"]);
        if let Ok(q) = r {
            assert_eq!(
                (q.amount, q.vbills, q.received),
                (ju(&m["amount"]), ju(&m["vbills"]), ju(&m["received"]))
            );
        }
    }
    for (i, m) in ja(&sc["swaps"]).iter().enumerate() {
        if m["add_limit"].is_object() {
            let al = &m["add_limit"];
            hub.add_limit(ji(&al["slot"]) as u8, ju(&al["limit"]))
                .unwrap();
        }
        let r = hub.swap(
            ji(&m["from"]) as u8,
            ji(&m["to"]) as u8,
            ji(&m["sender"]) as u32,
            ji(&m["receiver"]) as u32,
            ju(&m["amount"]),
            ju(&m["tick"]),
        );
        want(&format!("swap {i}"), &r, &m["status"]);
        match js(&m["status"]) {
            "ZXV_E_LIMIT" => assert_eq!(r.unwrap_err().refused_kind(), Some(RefusedKind::Limit)),
            "ZXV_E_PEG" => assert_eq!(r.unwrap_err().refused_kind(), Some(RefusedKind::Peg)),
            "ZXV_OK" => {
                let s = r.unwrap();
                assert_eq!(
                    (s.vbills, s.received),
                    (ju(&m["vbills"]), ju(&m["received"]))
                );
            }
            _ => {}
        }
    }
    let r = hub.check();
    want("check", &r, &sc["check"]);
    assert_eq!(r.unwrap(), ju(&sc["postings"]));
    let bals = ja(&sc["balances"]);
    for m in bals {
        let b = hub.balance(ji(&m["account"]) as u32).unwrap();
        assert_eq!(
            (b.debit, b.credit, b.equity),
            (ju(&m["debit"]), ju(&m["credit"]), ji(&m["equity"]))
        );
    }
    assert!(hub.balance(bals.len() as u32).is_err());
}

// ===== freight =============================================================

#[test]
fn freight_vectors() {
    let v = section(&common::vectors(), "freight");
    for (i, m) in ja(&v["header_serialize"]).iter().enumerate() {
        let mut f = FreightHeaderFields {
            version: 0,
            k: ji(&m["k"]) as u8,
            flags: ji(&m["flags"]) as u8,
            payload_len: ji(&m["payload_len"]) as u16,
            seq: ji(&m["seq"]) as u16,
            seq_count: ji(&m["seq_count"]) as u16,
            freight_id: ju(&m["freight_id"]),
            expand_limit: ju(&m["expand_limit"]),
            payload_sha256: a32(&m["payload_sha256"]),
        };
        let r = f.serialize();
        want(&format!("header_serialize {i}"), &r, &m["status"]);
        let Ok(h) = r else { continue };
        assert_eq!(h.0.to_vec(), hx(&m["header"]), "header {i}");
        f.version = freight::VERSION;
        assert_eq!(h.fields().unwrap(), f);
        h.check().unwrap();
    }
    for (i, m) in ja(&v["codec"]).iter().enumerate() {
        let payload = hx(&m["payload"]);
        let h =
            FreightHeader::new(ju(&m["freight_id"]), ji(&m["k"]) as u8, 0, 0, &payload).unwrap();
        assert_eq!(h.0.to_vec(), hx(&m["header"]), "codec {i} header");
        let packets = freight::encode(&h, &payload).unwrap();
        assert_eq!(packets, hx(&m["packets"]), "codec {i} packets");
        for (d, dm) in ja(&m["decode"]).iter().enumerate() {
            let mut sel = Vec::new();
            for r in ja(&dm["rows"]) {
                let row = ji(r) as usize;
                sel.extend_from_slice(&packets[row * 21..row * 21 + 21]);
            }
            if dm["flip"].is_object() {
                let fl = &dm["flip"];
                sel[ji(&fl["index_in_rows"]) as usize * 21 + ji(&fl["byte"]) as usize] ^=
                    hx(&fl["xor"])[0];
            }
            let r = freight::decode(&h, &sel);
            want(&format!("decode {i}/{d}"), &r, &dm["status"]);
            if jb(&dm["matches_payload"]) {
                assert_eq!(r.as_ref().unwrap(), &payload);
            }
            if js(&dm["status"]) == "ZXV_E_ERASURE" {
                assert_eq!(r.unwrap_err().refused_kind(), Some(RefusedKind::Erasure));
            }
        }
    }
    for (i, m) in ja(&v["header_init_reject"]).iter().enumerate() {
        let r = FreightHeader::new(
            0,
            ji(&m["k"]) as u8,
            ji(&m["flags"]) as u8,
            ju(&m["expand_limit"]),
            &vec![0u8; ji(&m["len"]) as usize],
        );
        want(&format!("header_init_reject {i}"), &r, &m["status"]);
    }
    for (i, m) in ja(&v["set_count"]).iter().enumerate() {
        let r = freight::set_count(ju(&m["len"]), ji(&m["k"]) as u8);
        want(&format!("set_count {i}"), &r, &m["status"]);
        if let Ok(n) = r {
            assert_eq!(i64::from(n), ji(&m["count"]));
        }
    }
    let sp = &v["split"];
    let data = ramp(ji(&sp["len"]) as usize);
    let k = ji(&sp["k"]) as u8;
    let mut join = FreightJoin::new(data.len() as u64).unwrap();
    for (j, m) in ja(&sp["members"]).iter().enumerate().rev() {
        let r = freight::split(ju(&sp["freight_id"]), k, 0, 0, &data, ji(&m["seq"]) as u32);
        want(&format!("split {j}"), &r, &m["status"]);
        let Ok((h, packets)) = r else { continue };
        assert_eq!(h.0.to_vec(), hx(&m["header"]), "split {j} header");
        assert_eq!(
            sha3(&packets),
            hx(&m["packets_sha3_256"]),
            "split {j} packets"
        );
        join.add(&h, &packets[21..]).unwrap(); // row 0 lost: recovered
        join.add(&h, &packets).unwrap(); // a member again is a no-op
    }
    let s = join.status().unwrap();
    assert!(s.complete && s.total == data.len() as u64, "{s:?}");
    assert_eq!(join.result().unwrap(), data);
    for (i, m) in ja(&v["ops_encode"]).iter().enumerate() {
        let input = js(&m["utf8"]).as_bytes();
        let r = freight::ops_encode(input, jb(&m["with_sha"]));
        want(&format!("ops_encode {i}"), &r, &m["status"]);
        let ops = r.unwrap();
        assert_eq!(ops, hx(&m["ops"]), "ops_encode {i}");
        assert_eq!(
            freight::ops_bound(input.len() as u64, jb(&m["with_sha"])).unwrap(),
            ju(&m["bound"])
        );
        let out = freight::ops_expand(&ops, freight::OPS_MAX);
        want(&format!("ops_encode expand {i}"), &out, &m["expand_status"]);
        assert_eq!(out.unwrap(), input);
    }
    for (i, m) in ja(&v["ops_writer"]).iter().enumerate() {
        let total = ju(&m["declared_total"]);
        let mut w = FreightOpsWriter::new(total, None, 4096).unwrap();
        let (mut cid, mut resolves_to) = (Vec::new(), Vec::new());
        for (o, om) in ja(&m["ops_list"]).iter().enumerate() {
            let r = match js(&om["op"]) {
                "literal" => w.put_literal(js(&om["utf8"]).as_bytes()),
                "copy" => w.put_copy(ju(&om["distance"]), ju(&om["len"])),
                "seed" => w.put_seed(
                    FreightGen::from_raw(ji(&om["gen"]) as u8),
                    &hx(&om["params"]),
                    ju(&om["len"]),
                ),
                "cidref" => {
                    cid = hx(&om["cid"]);
                    resolves_to = js(&om["resolves_to_utf8"]).as_bytes().to_vec();
                    w.put_cidref(&cid, ju(&om["len"]))
                }
                op => panic!("op {op}"),
            };
            if om["status"].is_null() {
                r.unwrap();
            } else {
                want(&format!("ops_writer {i}/{o}"), &r, &om["status"]);
            }
        }
        let r = w.finish();
        if m["finish_status"].is_null() {
            assert!(r.is_ok());
        } else {
            want(&format!("ops_writer finish {i}"), &r, &m["finish_status"]);
        }
        let ops = r.unwrap_or_default();
        drop(w);
        assert_eq!(ops, hx(&m["ops"]), "ops_writer {i}");
        if !m["expand_status"].is_null() {
            let out = freight::ops_expand(&ops, total);
            want(&format!("ops_writer expand {i}"), &out, &m["expand_status"]);
            assert_eq!(out.unwrap_or_default(), hx(&m["expanded"]));
            let (t, has) = freight::ops_peek(&ops).unwrap();
            assert_eq!((t, has), (ju(&m["peek_total"]), jb(&m["peek_has_sha"])));
            want(
                "expand limit too small",
                &freight::ops_expand(&ops, total - 1),
                &m["expand_limit_too_small"],
            );
            want(
                "expand truncated",
                &freight::ops_expand(&ops[..ops.len() - 1], total),
                &m["expand_truncated"],
            );
        }
        if !m["expand_without_resolver"].is_null() {
            want(
                "expand without resolver",
                &freight::ops_expand(&ops, total),
                &m["expand_without_resolver"],
            );
            let out = freight::ops_expand_with(&ops, total, |c, out| {
                if c != cid.as_slice() || out.len() != resolves_to.len() {
                    return false;
                }
                out.copy_from_slice(&resolves_to);
                true
            })
            .unwrap();
            assert_eq!(
                String::from_utf8(out).unwrap(),
                js(&m["expanded_with_resolver_utf8"])
            );
            // A resolver that fails or panics is "not resolved", never a crash.
            let e = freight::ops_expand_with(&ops, total, |_, _| false).unwrap_err();
            assert_eq!(e.status(), sys::ZXV_E_NOT_FOUND);
            let e = freight::ops_expand_with(&ops, total, |_, _| panic!("boom")).unwrap_err();
            assert_eq!(e.status(), sys::ZXV_E_INTERNAL);
        }
    }
    for (i, m) in ja(&v["gen_run"]).iter().enumerate() {
        let r = freight::gen_run(
            FreightGen::from_raw(ji(&m["gen"]) as u8),
            &hx(&m["params"]),
            ji(&m["len"]) as usize,
        );
        want(&format!("gen_run {i}"), &r, &m["status"]);
        if let Ok(out) = r {
            assert_eq!(out, hx(&m["out"]), "gen_run {i}");
        }
    }
}

// ===== web4 ================================================================

fn signed_body(b: &[u8]) -> &[u8] {
    &b[..b.len() - web4::SIG_BYTES]
}

#[test]
fn web4_vectors() {
    let v = section(&common::vectors(), "web4");
    assert_eq!(ji(&v["sig_bytes"]) as usize, web4::SIG_BYTES);
    let ctx = env15();
    let cap = ctx.root_capability(1, None, Caps::ALL_1_5).unwrap();
    let mut agents: Vec<W4Agent<'_>> = Vec::new();
    let mut idx: HashMap<String, usize> = HashMap::new();
    let mut infos = HashMap::new();
    for m in ja(&v["agents"]) {
        let seed = [hx(&m["seed_byte"])[0]; 32];
        let name = js(&m["name"]).to_string();
        let node = (!m["node"].is_null()).then(|| &agents[idx[js(&m["node"])]]);
        let r = W4Agent::new(&cap, Some(&seed), &name, node, ju(&m["now_ms"]));
        want(&format!("agent {name}"), &r, &m["status"]);
        let a = r.unwrap();
        let info = a.info().unwrap();
        assert_eq!(info.agent_id.to_vec(), hx(&m["agent_id"]), "{name}");
        assert_eq!(info.peer_id.to_vec(), hx(&m["peer_id"]), "{name}");
        assert_eq!(sha3(&info.public_key), hx(&m["pk_sha3_256"]), "{name}");
        let mut card = a.card().unwrap();
        assert_eq!(card.len() as i64, ji(&m["card_len"]));
        assert_eq!(
            sha3(signed_body(&card)),
            hx(&m["card_signed_body_sha3_256"])
        );
        let ci = web4::verify_card(&card);
        want(&format!("card verify {name}"), &ci, &m["card_verify"]);
        let ci = ci.unwrap();
        assert_eq!(
            (ci.name.as_str(), ci.agent_id, ci.peer_id, ci.bound_ms),
            (name.as_str(), info.agent_id, info.peer_id, ju(&m["now_ms"]))
        );
        *card.last_mut().unwrap() ^= 1;
        want(
            &format!("card flipped {name}"),
            &web4::verify_card(&card),
            &m["card_verify_flipped_last_byte"],
        );
        idx.insert(name.clone(), agents.len());
        infos.insert(name, info);
        agents.push(a);
    }
    // The directory holds verified cards.
    let mut dir = W4Directory::new(4).unwrap();
    for a in &agents {
        dir.add(&a.card().unwrap()).unwrap();
    }
    assert_eq!(dir.count().unwrap(), 4);
    assert_eq!(
        dir.find(&infos["bob"].agent_id).unwrap(),
        agents[idx["bob"]].card().unwrap()
    );
    assert_eq!(
        dir.find(&[0; 32]).unwrap_err().status(),
        sys::ZXV_E_NOT_FOUND
    );

    let ad = &v["action_digest"];
    let (from, to) = (a32(&ad["from"]), a32(&ad["to"]));
    let mut dig1 = [0u8; 32];
    for (i, m) in ja(&ad["cases"]).iter().enumerate() {
        let payload = if m["payload_ramp_len"].is_null() {
            js(&m["payload_utf8"]).as_bytes().to_vec()
        } else {
            ramp(ji(&m["payload_ramp_len"]) as usize)
        };
        let r = web4::action_digest(js(&m["tool"]), &from, &to, ju(&m["req_id"]), &payload);
        want(&format!("action digest {i}"), &r, &m["status"]);
        if let Ok(d) = r {
            assert_eq!(d.to_vec(), hx(&m["digest"]), "action digest {i}");
            if i == 1 {
                dig1 = d;
            }
        }
    }

    let mf = &v["manifest"];
    let mut man = W4Manifest::new(
        ji(&mf["version"]) as u32,
        ju(&mf["issued_ms"]),
        ju(&mf["expires_ms"]),
    )
    .unwrap();
    let mut tools = Vec::new();
    for (i, m) in ja(&mf["tools"]).iter().enumerate() {
        let t = W4Tool {
            name: js(&m["name"]).into(),
            price_vfv: ju(&m["price_vfv"]),
            rate_per_min: ji(&m["rate_per_min"]) as u32,
            burst: ji(&m["burst"]) as u32,
            flags: ji(&m["flags"]) as u32,
            max_input: ji(&m["max_input"]) as u32,
        };
        want(&format!("tool {i}"), &man.add_tool(&t), &m["status"]);
        tools.push(t);
    }
    let dup = W4Tool {
        name: "pay".into(),
        price_vfv: 1,
        rate_per_min: 1,
        burst: 1,
        flags: 0,
        max_input: 0,
    };
    want("add duplicate", &man.add_tool(&dup), &mf["add_duplicate"]);
    want("encode unsigned", &man.encode(), &mf["encode_unsigned"]);
    man.sign(&agents[idx[js(&mf["signer"])]]).unwrap();
    let enc = man.encode().unwrap();
    assert_eq!(enc.len() as i64, ji(&mf["encoding_len"]));
    assert_eq!(
        signed_body(&enc),
        hx(&mf["encoding_signed_body"]).as_slice()
    );
    assert_eq!(man.to_json().unwrap(), js(&mf["json"]));
    let dec = W4Manifest::decode(&enc);
    want("manifest decode", &dec, &mf["decode"]);
    let dec = dec.unwrap();
    let got: Vec<W4Tool> = dec.tools().unwrap().collect::<zxv::Result<_>>().unwrap();
    assert_eq!(got, tools);
    assert_eq!(dec.find("stream.feed").unwrap(), 2);
    let mi = dec.info().unwrap();
    assert!(
        mi.signed && mi.agent_id == infos["alice"].agent_id && mi.tool_count == 3,
        "{mi:?}"
    );
    for (i, m) in ja(&mf["verify"]).iter().enumerate() {
        let r = dec.verify(&infos[js(&m["key"])].public_key, ju(&m["now_ms"]));
        want(&format!("manifest verify {i}"), &r, &m["status"]);
    }

    let cs = &v["consent"];
    let nonce: [u8; 16] = hx(&cs["nonce"]).try_into().unwrap();
    let human = &agents[idx[js(&cs["human"])]];
    let mut req = ConsentRequest {
        agent_id: infos[js(&cs["agent"])].agent_id,
        action: dig1,
        scope: ji(&cs["scope"]) as u32,
        max_vfv: ju(&cs["max_vfv"]),
        issued_ms: ju(&cs["issued_ms"]),
        expires_ms: ju(&cs["expires_ms"]),
        nonce: Some(nonce),
    };
    let tok = web4::issue_consent(&cap, human, &req);
    want("consent issue", &tok, &cs["status"]);
    let tok = tok.unwrap();
    assert_eq!(tok.0.len() as i64, ji(&cs["token_len"]));
    assert_eq!(signed_body(&tok.0), hx(&cs["token_signed_body"]).as_slice());
    let ti = tok.info().unwrap();
    assert_eq!(
        (ti.human_id, ti.nonce, ti.max_vfv),
        (infos["human"].agent_id, nonce, req.max_vfv)
    );
    let mut nonce2 = nonce;
    nonce2[0] ^= 0xFF;
    req.nonce = Some(nonce2);
    let tok2 = web4::issue_consent(&cap, human, &req).unwrap();
    let mut log = ConsentLog::new(16).unwrap();
    let checks = ja(&cs["check"]);
    for (i, m) in checks.iter().enumerate() {
        let mut tk = &tok;
        if i == checks.len() - 1 {
            tk = &tok2;
            log.revoke(&tok2, ju(&m["now_ms"])).unwrap();
        }
        let need = ConsentNeed {
            agent_id: infos[js(&m["agent"])].agent_id,
            action: dig1,
            need: ji(&m["need"]) as u32,
            amount: ju(&m["amount"]),
            now_ms: ju(&m["now_ms"]),
        };
        let r = log.check(Some(tk), &infos["human"].public_key, &need);
        want(
            &format!("consent check {}", js(&m["what"])),
            &r,
            &m["status"],
        );
        if let Err(e) = r {
            assert_eq!(e.refused_kind(), Some(RefusedKind::Consent));
        }
    }
    let none = ConsentNeed {
        agent_id: [0; 32],
        action: dig1,
        need: 1,
        amount: 0,
        now_ms: 1,
    };
    assert_eq!(
        log.check(None, &infos["human"].public_key, &none)
            .unwrap_err()
            .refused_kind(),
        Some(RefusedKind::Consent)
    );
    assert_eq!(log.prune(ju(&cs["expires_ms"])).unwrap(), 0);

    let mut request = Vec::new();
    for (i, m) in ja(&v["envelopes"]).iter().enumerate() {
        let recv = infos[js(&m["to"])].clone();
        let sender_pk = infos[js(&m["sender"])].public_key.clone();
        let sender_id = infos[js(&m["sender"])].agent_id;
        let kind = W4MsgKind::from_raw(ji(&m["kind"]) as i32);
        let payload = js(&m["payload_utf8"]).as_bytes();
        let sender = &mut agents[idx[js(&m["sender"])]];
        let r = if js(&m["op"]) == "seal" {
            let msg = W4Message {
                kind,
                to: recv.agent_id,
                req_id: ju(&m["req_id"]),
                chunk: ji(&m["chunk"]) as u32,
                tool: js(&m["tool"]),
                flags: ji(&m["flags"]) as u32,
                payload,
                consent: None,
            };
            sender.seal(&msg, ju(&m["now_ms"]))
        } else {
            sender.reply(
                &request,
                kind,
                ji(&m["chunk"]) as u32,
                ji(&m["flags"]) as u32,
                payload,
                ju(&m["now_ms"]),
            )
        };
        want(&format!("envelope {i}"), &r, &m["status"]);
        let env = r.unwrap();
        if js(&m["op"]) == "seal" {
            request = env.clone();
        }
        assert_eq!(env.len() as i64, ji(&m["env_len"]), "envelope {i}");
        assert_eq!(
            signed_body(&env),
            hx(&m["env_signed_body"]).as_slice(),
            "envelope {i}"
        );
        want(
            "envelope verify",
            &web4::verify_envelope(&env, &sender_pk),
            &m["verify_sender"],
        );
        if !m["verify_other"].is_null() {
            want(
                "envelope verify other",
                &web4::verify_envelope(&env, &recv.public_key),
                &m["verify_other"],
            );
        }
        let d = web4::decode_envelope(&env).unwrap();
        assert_eq!(
            (d.kind, d.to, d.from),
            (kind, recv.agent_id, sender_id),
            "envelope {i}"
        );
        assert_eq!(web4::envelope_payload(&env).unwrap(), payload);
        assert_eq!(
            web4::envelope_consent(&env).unwrap_err().status(),
            sys::ZXV_E_NOT_FOUND
        );
    }
    // A token attached by sealing comes back out.
    let bob = infos["bob"].agent_id;
    let alice = &mut agents[idx["alice"]];
    let msg = W4Message {
        kind: W4MsgKind::Request,
        to: bob,
        req_id: 9,
        chunk: 0,
        tool: "pay",
        flags: 0,
        payload: b"amount=1",
        consent: Some(&tok),
    };
    let env = alice.seal(&msg, 1_700_000_001_000).unwrap();
    assert_eq!(web4::envelope_consent(&env).unwrap(), tok);
    assert_ne!(
        web4::decode_envelope(&env).unwrap().flags & web4::FLAG_CONSENT,
        0
    );
    // Debug never prints a key.
    let dbg = format!("{alice:?}");
    assert!(dbg.len() < 100, "{dbg}");
}

// ===== paradox =============================================================

fn heads(b: &ParadoxBook<'_>, w: &Value) {
    for (which, name) in [
        (ParadoxLog::Ops, "ops"),
        (ParadoxLog::Frontier, "frontier"),
        (ParadoxLog::Quarantine, "quarantine"),
    ] {
        let m = &w[name];
        assert_eq!(
            i64::from(b.log_count(which).unwrap()),
            ji(&m["count"]),
            "log {name}"
        );
        assert_eq!(
            b.log_head(which).unwrap().to_vec(),
            hx(&m["head"]),
            "log {name}"
        );
    }
}

#[test]
fn paradox_vectors() {
    let v = section(&common::vectors(), "paradox");
    for (i, m) in ja(&v["signature"]).iter().enumerate() {
        assert_eq!(
            paradox::signature(js(&m["utf8"]).as_bytes())
                .unwrap()
                .to_vec(),
            hx(&m["sig"]),
            "signature {i}"
        );
    }
    for (i, n) in ja(&v["kind_names"]).iter().enumerate() {
        assert_eq!(ParadoxKind::from_raw(i as i32).name(), js(n));
    }
    for (i, n) in ja(&v["outcome_names"]).iter().enumerate() {
        assert_eq!(ParadoxOutcome::from_raw(i as i32).name(), js(n));
    }
    for (i, n) in ja(&v["node_state_names"]).iter().enumerate() {
        assert_eq!(ParadoxNodeState::from_raw(i as i32).name(), js(n));
    }
    for (t, k) in ja(&v["kind_of_tag"]).iter().enumerate() {
        let got = ParadoxKind::of_tag(Hexagram::from_tag(t as u8).unwrap()).unwrap();
        assert_eq!(i64::from(got.raw()), ji(k), "kind_of_tag {t}");
    }
    for (k, row) in ja(&v["action"]).iter().enumerate() {
        for (t, d) in ja(row).iter().enumerate() {
            let c = ParadoxClass {
                kind: ParadoxKind::from_raw(k as i32),
                tag: Hexagram::from_tag(t as u8).unwrap(),
            };
            let r = c.action();
            if ji(d) == -1 {
                assert_eq!(r.unwrap_err().status(), sys::ZXV_E_ARG, "action[{k}][{t}]");
            } else {
                assert_eq!(i64::from(r.unwrap().raw()), ji(d), "action[{k}][{t}]");
            }
        }
    }
    for (o, a) in ja(&v["outcome_action"]).iter().enumerate() {
        let got = ParadoxOutcome::from_raw(o as i32).action().unwrap();
        assert_eq!(i64::from(got.raw()), ji(a), "outcome_action {o}");
    }

    let ctx = env15();
    let cap = ctx.root_capability(1, None, Caps::ALL_1_5).unwrap();
    let b = ParadoxBook::new(&cap, 64, 16).unwrap();
    heads(&b, &v["heads_fresh"]);
    let sig_l = paradox::signature(b"loop:A->B->A").unwrap();
    for (i, m) in ja(&v["classify"]).iter().enumerate() {
        if m["registry_add"].is_object() {
            let ra = &m["registry_add"];
            let r = b.registry_add(
                &sig_l,
                ParadoxKind::from_raw(ji(&ra["kind"]) as i32),
                ji(&ra["reason"]) as u8,
                ji(&ra["flags"]) as u8,
                jb(&ra["owner_ok"]),
            );
            want("registry_add", &r, &ra["status"]);
            assert_eq!(i64::from(r.unwrap_or(0)), ji(&ra["index"]));
            continue;
        }
        let ev = m["evidence"].as_array().map(|_| {
            let e = i64s(&m["evidence"]);
            ParadoxEvidence {
                pos0: e[0] as u32,
                neg0: e[1] as u32,
                pos1: e[2] as u32,
                neg1: e[3] as u32,
                stale: jb(&m["stale"]),
            }
        });
        let sig = (!m["sig"].is_null()).then_some(&sig_l);
        let r = b.classify(tag(&m["tag"]), ev.as_ref(), sig);
        want(&format!("classify {i}"), &r, &m["status"]);
        let c = r.unwrap();
        assert_eq!(
            i64::from(c.class.kind.raw()),
            ji(&m["kind"]),
            "classify {i}"
        );
        assert_eq!(
            i64::from(c.class.tag.tag()),
            ji(&m["out_tag"]),
            "classify {i}"
        );
        assert_eq!(i64::from(c.reg_index), ji(&m["reg_index"]), "classify {i}");
        if sig.is_none() {
            assert_eq!(
                paradox::classify(tag(&m["tag"]), ev.as_ref())
                    .unwrap()
                    .class,
                c.class
            );
        }
    }
    let sig_m = paradox::signature(b"money:drain").unwrap();
    for (i, m) in ja(&v["registry"]).iter().enumerate() {
        let r = b.registry_add(
            &sig_m,
            ParadoxKind::from_raw(ji(&m["kind"]) as i32),
            2,
            ji(&m["flags"]) as u8,
            jb(&m["owner_ok"]),
        );
        want(&format!("registry {i}"), &r, &m["status"]);
        if !m["index"].is_null() {
            assert_eq!(i64::from(*r.as_ref().unwrap()), ji(&m["index"]));
        }
        if js(&m["status"]) == "ZXV_E_PARADOX_OWNER" {
            assert_eq!(
                r.unwrap_err().refused_kind(),
                Some(RefusedKind::ParadoxOwner)
            );
        }
    }
    let e = b.registry_at(1).unwrap();
    assert_eq!(
        (e.sig, e.src, e.flags),
        (sig_m, paradox::SRC_SEED, paradox::FLAG_MONEY)
    );
    assert_eq!(b.registry_find(&sig_l).unwrap(), 0);
    assert_eq!(
        b.registry().unwrap().count() as u32,
        b.registry_count().unwrap()
    );
    heads(&b, &v["heads_after_classify"]);

    let cs = &v["constructive"];
    let (lo, hi, init) = (i64s(&cs["lo"]), i64s(&cs["hi"]), i64s(&cs["initial"]));
    let mut plain = ParadoxPolicy::new(&cap, &lo, &hi, &init, 0).unwrap();
    let mut money = ParadoxPolicy::new(&cap, &lo, &hi, &init, paradox::FLAG_MONEY).unwrap();
    for (i, m) in ja(&cs["steps"]).iter().enumerate() {
        let p = if js(&m["policy"]) == "money" {
            &mut money
        } else {
            &mut plain
        };
        let synth = (!m["synth"].is_null()).then(|| i64s(&m["synth"]));
        let r = b.constructive(p, class(m), synth.as_deref(), ji(&m["tension"]) as u32);
        want(&format!("constructive {i}"), &r, &m["status"]);
        if !m["outcome"].is_null() {
            assert_eq!(
                i64::from(r.unwrap().raw()),
                ji(&m["outcome"]),
                "constructive {i}"
            );
        }
        let vals = p.values().unwrap();
        assert_eq!(vals.values, i64s(&m["values"]), "constructive {i}");
        assert_eq!(vals.prev, i64s(&m["prev"]), "constructive {i}");
        assert_eq!(
            (vals.armed, i64::from(vals.tension_at)),
            (jb(&m["armed"]), ji(&m["tension_at"]))
        );
        if !m["proposal"].is_null() {
            assert_eq!(
                p.proposal(synth.as_deref().unwrap()).unwrap(),
                hx(&m["proposal"])
            );
        }
    }
    assert_eq!(
        money.bounds().unwrap(),
        (lo.clone(), hi.clone(), paradox::FLAG_MONEY)
    );

    for (i, m) in ja(&v["destructive"]).iter().enumerate() {
        let trace: Vec<u64> = m["trace"]
            .as_array()
            .map(|a| a.iter().map(ju).collect())
            .unwrap_or_default();
        let mut state = js(&m["state_utf8"]).as_bytes().to_vec();
        let r = b.destructive(
            class(m),
            ParadoxContainment {
                steps: ji(&m["steps"]) as u32,
                step_limit: ji(&m["step_limit"]) as u32,
                spent: ji(&m["spent"]) as u32,
                budget: ji(&m["budget"]) as u32,
                trace: &trace,
                invariant_ok: jb(&m["invariant_ok"]),
                state: &mut state,
                checkpoint: js(&m["checkpoint_utf8"]).as_bytes(),
                pattern: js(&m["pattern_utf8"]).as_bytes(),
                submitter: ji(&m["submitter"]) as u32,
            },
        );
        want(&format!("destructive {i}"), &r, &m["status"]);
        let (o, mut proof) = r.unwrap();
        assert_eq!(i64::from(o.raw()), ji(&m["outcome"]), "destructive {i}");
        assert_eq!(state, hx(&m["state_after"]), "destructive {i}");
        if !m["repair_proof"].is_null() {
            assert_eq!(proof.0.to_vec(), hx(&m["repair_proof"]));
            want("repair verify", &proof.verify(), &m["repair_verify"]);
            assert_eq!(proof.signature(), sig_l);
            assert_eq!(proof.submitter(), 7);
            assert_ne!(proof.trips() & paradox::TRIP_STEPS, 0);
            proof.0[0] ^= 1;
            want(
                "repair verify flipped",
                &proof.verify(),
                &m["repair_verify_flipped_byte0"],
            );
        }
    }

    let nt = &v["neutral"];
    let mut hold =
        ParadoxHold::new(&cap, ji(&nt["capacity"]) as u32, ji(&nt["decisive"]) as u32).unwrap();
    let br = ja(&nt["branches"]);
    let (b0, b1) = (a32(&br[0]), a32(&br[1]));
    hold.add(&b0).unwrap();
    hold.add(&b1).unwrap();
    hold.evidence(&b0, true).unwrap();
    hold.evidence(&b1, true).unwrap();
    for (i, m) in ja(&nt["steps"]).iter().enumerate() {
        if !m["before"].is_null() {
            continue;
        }
        for _ in 0..ji(&m["add_for_branch0"]) {
            hold.evidence(&b0, true).unwrap();
        }
        let pick = u32::try_from(ji(&m["owner_pick"])).ok();
        let r = b.neutral(&mut hold, class(m), pick);
        want(&format!("neutral {i}"), &r, &m["status"]);
        let (o, settled) = r.unwrap();
        assert_eq!(i64::from(o.raw()), ji(&m["outcome"]), "neutral {i}");
        assert_eq!(
            settled.map_or(-1, i64::from),
            ji(&m["settled"]),
            "neutral {i}"
        );
    }
    let brs: Vec<_> = hold
        .branches()
        .unwrap()
        .collect::<zxv::Result<_>>()
        .unwrap();
    assert!(
        brs.len() == 2 && brs[0].id == b0 && brs[0].pos == 3,
        "{brs:?}"
    );

    let sp = &v["special"];
    let (sg, subj) = (a32(&sp["sig"]), a32(&sp["subject"]));
    let r = b.special(class(sp), &sg, &subj);
    want("special", &r, &sp["status"]);
    let (o, rec) = r.unwrap();
    let rec = rec.unwrap_or(u32::MAX);
    assert_eq!(
        (i64::from(o.raw()), i64::from(rec)),
        (ji(&sp["outcome"]), ji(&sp["rec"]))
    );
    let mut drafts = HashMap::new();
    for name in ["draft", "draft_money"] {
        let m = &sp[name];
        let r = b.draft(
            rec,
            ParadoxKind::from_raw(ji(&m["kind"]) as i32),
            ji(&m["reason"]) as u8,
            ji(&m["flags"]) as u8,
        );
        want(name, &r, &m["status"]);
        let d = r.unwrap();
        assert_eq!(d.0.to_vec(), hx(&m["bytes"]), "{name}");
        assert_eq!((d.rec(), d.signature()), (rec, sg));
        drafts.insert(name, d);
    }
    for (i, m) in ja(&sp["review"]).iter().enumerate() {
        let r = b.review(
            &drafts[js(&m["draft"])],
            jb(&m["owner_ok"]),
            jb(&m["consensus_ok"]),
        );
        want(&format!("review {i}"), &r, &m["status"]);
        assert_eq!(i64::from(r.unwrap().raw()), ji(&m["outcome"]), "review {i}");
    }
    assert_eq!(
        i64::from(b.registry_count().unwrap()),
        ji(&sp["registry_count_after"])
    );

    let id = |x: &Value| [js(x).as_bytes()[0]; 32];
    for (gi, gm) in ja(&v["graphs"]).iter().enumerate() {
        let mut g = ParadoxGraph::new(&cap, ji(&gm["step_limit"]) as u32).unwrap();
        for m in ja(&gm["nodes"]) {
            let r = g.add_node(
                &id(&m["id"]),
                class(m),
                ji(&m["mag"]) as u32,
                ji(&m["dir"]) as i32,
            );
            want("add node", &r, &m["status"]);
        }
        for m in ja(&gm["deps"]) {
            want(
                "add dep",
                &g.add_dep(&id(&m["id"]), &id(&m["dep"])),
                &m["status"],
            );
        }
        for (pi, m) in ja(&gm["passes"]).iter().enumerate() {
            let r = b.graph_pass(&mut g);
            want(&format!("graph {gi} pass {pi}"), &r, &m["status"]);
            match &r {
                Ok(o) => assert_eq!(i64::from(o.raw()), ji(&m["outcome"])),
                Err(e) => assert_eq!(e.refused_kind(), Some(RefusedKind::ParadoxStep)),
            }
            let info = g.info().unwrap();
            assert_eq!(i64::from(info.pass), ji(&m["pass"]));
            assert_eq!(i64::from(info.total_mag), ji(&m["total_mag"]));
            assert_eq!(i64::from(info.total_dir), ji(&m["total_dir"]));
            assert_eq!(info.escalated, jb(&m["escalated"]));
            assert_eq!(info.chain.to_vec(), hx(&m["chain"]));
            for (name, s) in m["states"].as_object().unwrap() {
                let st = g.state(&[name.as_bytes()[0]; 32]).unwrap();
                assert_eq!(i64::from(st.raw()), ji(s), "graph {gi} node {name}");
            }
        }
        let nodes: Vec<_> = g.nodes().unwrap().collect::<zxv::Result<_>>().unwrap();
        assert!(
            nodes.windows(2).all(|w| w[0].id < w[1].id),
            "nodes not in id order"
        );
    }
    heads(&b, &v["heads_final"]);
    for (w, (which, name)) in [
        (ParadoxLog::Ops, "ops"),
        (ParadoxLog::Frontier, "frontier"),
        (ParadoxLog::Quarantine, "quarantine"),
    ]
    .into_iter()
    .enumerate()
    {
        let recs: Vec<Vec<u8>> = b
            .log_records(which)
            .unwrap()
            .collect::<zxv::Result<_>>()
            .unwrap();
        let want_recs: Vec<Vec<u8>> = ja(&v["logs"][name]).iter().map(hx).collect();
        assert_eq!(recs, want_recs, "log {name}");
        want(
            &format!("log verify {name}"),
            &b.log_verify(which),
            &ja(&v["log_verify"])[w],
        );
    }
}

// ===== behaviour: capabilities, lifetimes, buffers, fail closed ============

fn denied<T: std::fmt::Debug>(what: &str, r: zxv::Result<T>) {
    let e = r.unwrap_err();
    assert!(
        e.is_capability_denied() && e.class() == StatusClass::Refused,
        "{what}: {e}"
    );
}

#[test]
fn capability_refusals() {
    let ctx = env15();
    let root = ctx.root_capability(1, None, Caps::ALL_1_5).unwrap();
    let without = |c: Caps| {
        root.attenuate(Caps::from_bits(Caps::ALL_1_5.bits() & !c.bits()))
            .unwrap()
    };
    let c = without(Caps::AIPI);
    denied("AIPI", AipiNode::new(&c, aipi::STORE_MIN, 4, None));
    let c = without(Caps::MINT);
    denied("MINT", TokenWallet::new(&c, 1, Octane::PLAIN));
    let c = without(Caps::PARADOX);
    denied("PARADOX book", ParadoxBook::new(&c, 4, 4));
    denied("PARADOX hold", ParadoxHold::new(&c, 4, 1));
    denied("PARADOX graph", ParadoxGraph::new(&c, 1));
    denied(
        "PARADOX policy",
        ParadoxPolicy::new(&c, &[0], &[1], &[0], 0),
    );
    let c = without(Caps::CRYPTO_SIGN);
    denied("CRYPTO_SIGN", W4Agent::new(&c, None, "a", None, 1));
    let c = without(Caps::LEDGER_POST);
    denied("LEDGER_POST", VHub::new(&c, None));

    // Operators on a book use the book's capability: once it is revoked,
    // every write is refused.
    let sub = root.attenuate(Caps::ALL_1_5).unwrap();
    let b = ParadoxBook::new(&sub, 4, 4).unwrap();
    sub.revoke().unwrap();
    denied(
        "revoked",
        b.registry_add(&[1; 32], ParadoxKind::Destructive, 0, 0, false),
    );

    // Consent needs ATTEST, and a cap of the human key's context.
    let human = W4Agent::new(&root, Some(&[4; 32]), "human", None, 1).unwrap();
    let no_attest = without(Caps::ATTEST);
    let req = ConsentRequest {
        agent_id: [0; 32],
        action: [0; 32],
        scope: web4::CONSENT_MONEY,
        max_vfv: 0,
        issued_ms: 1,
        expires_ms: 2,
        nonce: None,
    };
    denied(
        "consent without ATTEST",
        web4::issue_consent(&no_attest, &human, &req),
    );
    let ctx2 = env15();
    let other = ctx2.root_capability(1, None, Caps::ALL_1_5).unwrap();
    assert_eq!(
        web4::issue_consent(&other, &human, &req)
            .unwrap_err()
            .status(),
        sys::ZXV_E_ARG
    );
    let ob = ParadoxBook::new(&other, 4, 4).unwrap();
    let mut g = ParadoxGraph::new(&root, 1).unwrap();
    assert_eq!(ob.graph_pass(&mut g).unwrap_err().status(), sys::ZXV_E_ARG);
    let mut hold = ParadoxHold::new(&root, 2, 1).unwrap();
    let neutral = ParadoxClass {
        kind: ParadoxKind::Neutral,
        tag: Hexagram::from_tag(0).unwrap(),
    };
    assert_eq!(
        ob.neutral(&mut hold, neutral, None).unwrap_err().status(),
        sys::ZXV_E_ARG
    );
    // An agent bound to a node of another context is refused too.
    assert_eq!(
        W4Agent::new(&other, None, "x", Some(&human), 1)
            .unwrap_err()
            .status(),
        sys::ZXV_E_ARG
    );
}

fn assert_send<T: Send>() {}
fn assert_sync<T: Sync>() {}

#[test]
fn handle_lifetimes() {
    // Thread-safe in C: Send + Sync. The others: Send only (checked to be
    // !Sync by the compile_fail doctests in the crate docs).
    assert_send::<Treaty>();
    assert_sync::<Treaty>();
    assert_sync::<zxv::TreatyResult>();
    assert_sync::<AipiNode<'static>>();
    assert_sync::<TokenWallet<'static>>();
    assert_sync::<VHub<'static>>();
    assert_sync::<ParadoxBook<'static>>();
    assert_send::<FreightJoin>();
    assert_send::<FreightOpsWriter>();
    assert_send::<W4Agent<'static>>();
    assert_send::<W4Directory>();
    assert_send::<W4Manifest>();
    assert_send::<ConsentLog>();
    assert_send::<ParadoxPolicy<'static>>();
    assert_send::<ParadoxHold<'static>>();
    assert_send::<ParadoxGraph<'static>>();

    // A treaty result is independent of its treaty: drop either first.
    let tr = Treaty::parse_sutra(
        "TREATY T USD NET\nPARTY A 10 0 1/2\nPARTY B 10 0 1/2\nOWE A B 5\nEND\n",
    )
    .unwrap();
    let res = tr.run().unwrap();
    drop(tr);
    assert_eq!(res.legs().unwrap().len(), 1);
    drop(res);

    // Many handles of one capability, dropped in any order, then the cap
    // and the context.
    let ctx = env15();
    let cap = ctx.root_capability(1, None, Caps::ALL_1_5).unwrap();
    for _ in 0..50 {
        let a = W4Agent::new(&cap, None, "agent", None, 1).unwrap();
        let b = ParadoxBook::new(&cap, 4, 4).unwrap();
        let h = ParadoxHold::new(&cap, 2, 1).unwrap();
        let hub = VHub::new(&cap, None).unwrap();
        let w = TokenWallet::new(&cap, 1, Octane::PLAIN).unwrap();
        let n = AipiNode::new(&cap, aipi::STORE_MIN, 4, None).unwrap();
        drop(b);
        drop(a);
        drop(w);
        drop(h);
        drop(n);
        drop(hub);
    }
    drop(cap);
    drop(ctx);

    // A context's handles stay usable from another thread.
    let ctx = env15();
    let cap = ctx.root_capability(1, None, Caps::ALL_1_5).unwrap();
    let book = ParadoxBook::new(&cap, 64, 4).unwrap();
    std::thread::scope(|s| {
        for t in 0..4u8 {
            let book = &book;
            s.spawn(move || {
                let c = book
                    .classify(Hexagram::from_tag(t).unwrap(), None, None)
                    .unwrap();
                assert!(c.reg_index < 0);
            });
        }
    });
}

#[test]
fn buffer_growth() {
    // Random-looking bytes encode LARGER than themselves: the first buffer
    // is too small and the SDK grows it to the exact length.
    let input: Vec<u8> = (0..70_000u32)
        .map(|i| ((i.wrapping_mul(2_654_435_761) >> 13) as u8) ^ ((i >> 8) as u8))
        .collect();
    let ops = freight::ops_encode(&input, true).unwrap();
    assert!(ops.len() > input.len() / 2 + 64, "growth not exercised");
    assert_eq!(
        freight::ops_expand(&ops, input.len() as u64).unwrap(),
        input
    );
    // A join result larger than the first buffer.
    let data = ramp(5000);
    let mut j = FreightJoin::new(data.len() as u64).unwrap();
    for s in 0..freight::set_count(data.len() as u64, 168).unwrap() {
        let (h, p) = freight::split(1, 168, 0, 0, &data, s).unwrap();
        j.add(&h, &p).unwrap();
    }
    assert_eq!(j.result().unwrap(), data);
    // A paradox proposal of 64 values.
    let ctx = env15();
    let cap = ctx.root_capability(1, None, Caps::ALL_1_5).unwrap();
    let (lo, hi) = (vec![0i64; 64], vec![100i64; 64]);
    let p = ParadoxPolicy::new(&cap, &lo, &hi, &lo, 0).unwrap();
    assert_eq!(p.proposal(&hi).unwrap().len(), paradox::proposal_bytes(64));
    // A long treaty Sutra text and a long JSON manifest.
    let mut m = W4Manifest::new(1, 1, 2).unwrap();
    for i in 0..web4::MAX_TOOLS {
        let t = W4Tool {
            name: format!("tool.number-{i}"),
            price_vfv: 1,
            rate_per_min: 1,
            burst: 1,
            flags: 0,
            max_input: 1,
        };
        m.add_tool(&t).unwrap();
    }
    assert!(m.to_json().unwrap().len() > 64);
}

#[test]
fn poisoned_context() {
    let ctx = env15();
    let cap = ctx.root_capability(1, None, Caps::ALL_1_5).unwrap();
    let b = ParadoxBook::new(&cap, 4, 4).unwrap();
    let mut g = ParadoxGraph::new(&cap, 1).unwrap();
    let mut a = W4Agent::new(&cap, Some(&[7; 32]), "a", None, 1).unwrap();
    let w = TokenWallet::new(&cap, 1, Octane::PLAIN).unwrap();
    ctx.halt("test halt").unwrap();
    let none = ParadoxClass {
        kind: ParadoxKind::None,
        tag: Hexagram::from_tag(0).unwrap(),
    };
    let msg = W4Message {
        kind: W4MsgKind::Request,
        to: [0; 32],
        req_id: 0,
        chunk: 0,
        tool: "pay",
        flags: 0,
        payload: &[],
        consent: None,
    };
    let errs: Vec<(&str, Error)> = vec![
        ("book read", b.log_count(ParadoxLog::Ops).unwrap_err()),
        (
            "book write",
            b.registry_add(&[1; 32], ParadoxKind::Neutral, 0, 0, false)
                .unwrap_err(),
        ),
        ("graph local", g.add_node(&[1; 32], none, 0, 0).unwrap_err()),
        ("agent card", a.card().unwrap_err()),
        ("agent seal", a.seal(&msg, 1).unwrap_err()),
        ("wallet info", w.info().unwrap_err()),
        ("new book", ParadoxBook::new(&cap, 4, 4).unwrap_err()),
    ];
    for (what, e) in errs {
        assert!(
            e.is_fatal() && e.status() == sys::ZXV_E_POISONED,
            "{what}: {e}"
        );
    }
    // Pure functions need no context and keep working.
    assert_eq!(fee::assurance_fee(1125).unwrap(), 1);
    assert!(Cid::compute(aipi::CODEC_RAW, b"x").is_ok());
}

#[allow(dead_code)]
fn _uses(_: &Capability<'_>) {}
