// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
//! Checks the SDK against sdk/conformance/vectors.json, the answer key
//! shared by every ZXV SDK.

mod common;

use common::*;
use zxv::*;

#[test]
fn abi_version_matches() {
    let v = vectors();
    let abi = i64s(&v["abi"]);
    let (major, minor, _) = version();
    assert_eq!(major as i64, abi[0]);
    assert!(minor >= 2);
}

#[test]
fn status_names_and_classes() {
    for s in vectors()["status"].as_array().unwrap() {
        let code = s["code"].as_i64().unwrap() as i32;
        assert_eq!(status_name(code), s["name"].as_str().unwrap());
        let class = match s["class"].as_i64().unwrap() {
            0 => StatusClass::Ok,
            1 => StatusClass::Usage,
            2 => StatusClass::Refused,
            3 => StatusClass::Fatal,
            _ => StatusClass::System,
        };
        assert_eq!(StatusClass::of(code), class, "{}", status_name(code));
        if code != 0 {
            assert_eq!(Error::new(code, "").class(), class);
        }
    }
}

#[test]
fn rational_ops() {
    for case in vectors()["rational_ops"].as_array().unwrap() {
        let (a, b) = (rat(&case["a"]), rat(&case["b"]));
        assert_eq!(a.checked_add(b).unwrap(), rat(&case["add"]));
        assert_eq!(a.checked_sub(b).unwrap(), rat(&case["sub"]));
        assert_eq!(a.checked_mul(b).unwrap(), rat(&case["mul"]));
        assert_eq!(a.checked_div(b).unwrap(), rat(&case["div"]));
        let cmp = case["cmp"].as_i64().unwrap();
        assert_eq!(a.cmp(&b) as i64, cmp);
    }
}

#[test]
fn rational_parse_and_format() {
    for case in vectors()["rational_parse"].as_array().unwrap() {
        let text = case["text"].as_str().unwrap();
        let status = case["status"].as_i64().unwrap() as i32;
        match text.parse::<Rational>() {
            Ok(r) => {
                assert_eq!(status, 0, "{text} should fail");
                assert_eq!(r, rat(&case["value"]), "{text}");
                assert_eq!(r.to_string(), case["format"].as_str().unwrap());
            }
            Err(e) => {
                assert_eq!(e.status(), status, "{text}: {e}");
                assert_eq!(e.class(), StatusClass::Usage);
            }
        }
    }
}

#[test]
fn ieee754_boundary() {
    for case in vectors()["ieee754"].as_array().unwrap() {
        let bits = u64::from_str_radix(case["bits"].as_str().unwrap(), 16).unwrap();
        let status = case["status"].as_i64().unwrap() as i32;
        let r = Rational::from_f64_exact(f64::from_bits(bits));
        match r {
            Ok(r) => {
                assert_eq!(status, 0);
                assert_eq!(r, rat(&case["value"]));
            }
            Err(e) => assert_eq!(e.status(), status),
        }
    }
    // 0.1 is not 1/10.
    let tenth = Rational::from_f64_exact(0.1).unwrap();
    assert_ne!(tenth, Rational::new(1, 10).unwrap());
    assert_eq!(
        Rational::from_f64_exact(f64::NAN).unwrap_err().name(),
        "ZXV_E_FLOAT"
    );
}

#[test]
fn q16_conversion() {
    for case in vectors()["q16"].as_array().unwrap() {
        let p = i64s(&case["value"]);
        let r = Rational::new(p[0], p[1]).unwrap();
        let status = case["status"].as_i64().unwrap() as i32;
        match r.to_q16() {
            Ok(q) => {
                assert_eq!(status, 0);
                assert_eq!(q as i64, case["q16"].as_i64().unwrap());
            }
            Err(e) => assert_eq!(e.status(), status),
        }
    }
}

#[test]
fn lpres_tables() {
    let v = vectors();
    let t = &v["lpres"];
    for (i, a) in Truth::ALL.iter().enumerate() {
        assert_eq!(a.name(), t["names"][i].as_str().unwrap());
        assert_eq!(a.not().raw() as i64, t["not"][i].as_i64().unwrap());
        for (j, b) in Truth::ALL.iter().enumerate() {
            assert_eq!(
                a.and(*b).raw() as i64,
                t["and"][i][j].as_i64().unwrap(),
                "{a} and {b}"
            );
            assert_eq!(
                a.or(*b).raw() as i64,
                t["or"][i][j].as_i64().unwrap(),
                "{a} or {b}"
            );
        }
    }
    assert_eq!(Truth::from_evidence(2, 1), Truth::Both);
    assert_eq!(Truth::from_evidence(0, 0), Truth::Neither);
}

#[test]
fn ubh168_frames_byte_for_byte() {
    let v = vectors();
    for case in v["ubh168"].as_array().unwrap() {
        let g = |k: &str| case[k].as_u64().unwrap() as u32;
        let tag = Ubh168Frame::tag(g("trunk"), g("shell"), g("sync")).unwrap();
        assert_eq!(tag as u64, case["tag"].as_u64().unwrap());
        let sp: Vec<u32> = case["s_plus"]
            .as_array()
            .unwrap()
            .iter()
            .map(|x| x.as_u64().unwrap() as u32)
            .collect();
        let sm: Vec<u32> = case["s_minus"]
            .as_array()
            .unwrap()
            .iter()
            .map(|x| x.as_u64().unwrap() as u32)
            .collect();
        let sp: [u32; 3] = sp.try_into().unwrap();
        let sm: [u32; 2] = sm.try_into().unwrap();
        let pure = Ubh168Frame::pack(tag, sp, sm);
        let native = Ubh168Frame::pack_native(tag, sp, sm).unwrap();
        assert_eq!(
            pure.as_bytes().to_vec(),
            unhex(&case["frame"]),
            "pure Rust packing"
        );
        assert_eq!(pure, native, "pure Rust == libzxv");
        assert_eq!(pure.unpack(), (tag, sp, sm));
        assert_eq!(
            pure.resolve().unwrap().raw() as u64,
            case["truth"].as_u64().unwrap()
        );
    }
    for (trunk, frame) in v["ubh168_idle"].as_array().unwrap().iter().enumerate() {
        let idle = Ubh168Frame::idle(trunk as u32).unwrap();
        assert_eq!(idle.as_bytes().to_vec(), unhex(frame));
    }
    assert_eq!(
        Ubh168Frame::idle(0).unwrap().resolve().unwrap(),
        WireTruth::Neutral
    );
    assert!(Ubh168Frame::tag(10, 0, 0).is_err());
}

#[test]
fn hashes() {
    for case in vectors()["hash"].as_array().unwrap() {
        let m = case["utf8"].as_str().unwrap().as_bytes();
        assert_eq!(Hashes::keccak256(m).to_vec(), unhex(&case["keccak256"]));
        assert_eq!(Hashes::sha3_256(m).to_vec(), unhex(&case["sha3_256"]));
        assert_eq!(Hashes::shake256(m, 64), unhex(&case["shake256_64"]));
    }
}

#[test]
fn ed25519_vector() {
    let v = vectors();
    let e = &v["ed25519"];
    let ctx = Context::new().unwrap();
    let cap = ctx.root_capability(1, None, Caps::ALL).unwrap();
    let ed = Ed25519::new(&cap);
    let sk = SecretKey::from_bytes(&unhex(&e["sk"]));
    let pk = ed.public_key(&sk).unwrap();
    assert_eq!(pk.to_vec(), unhex(&e["pk"]));
    let msg = e["msg_utf8"].as_str().unwrap().as_bytes();
    let sig = ed.sign(&sk, msg).unwrap();
    assert_eq!(sig.to_vec(), unhex(&e["sig"]));
    ed.verify(&pk, msg, &sig).unwrap();
    let mut bad = sig;
    bad[0] ^= 1;
    let err = ed.verify(&pk, msg, &bad).unwrap_err();
    assert_eq!(err.refused_kind(), Some(RefusedKind::VerificationFailed));
}

#[test]
fn mldsa87_vector() {
    let v = vectors();
    let d = &v["mldsa87"];
    let ctx = Context::new().unwrap();
    let cap = ctx.root_capability(1, None, Caps::ALL).unwrap();
    let dsa = MlDsa87::new(&cap);
    let (pk, sk) = dsa.keygen_from_seed(&arr32(&d["seed"])).unwrap();
    assert_eq!(Hashes::sha3_256(&pk).to_vec(), unhex(&d["pk_sha3_256"]));
    assert_eq!(
        Hashes::sha3_256(&sk.export_bytes_unsafe()).to_vec(),
        unhex(&d["sk_sha3_256"])
    );
    let msg = d["msg_utf8"].as_str().unwrap().as_bytes();
    let c = d["ctx_utf8"].as_str().unwrap().as_bytes();
    let sig = dsa.sign_deterministic(&sk, msg, c).unwrap();
    assert_eq!(
        Hashes::sha3_256(&sig).to_vec(),
        unhex(&d["sig_deterministic_sha3_256"])
    );
    dsa.verify(&pk, msg, c, &sig).unwrap();
    let hedged = dsa.sign(&sk, msg, c).unwrap();
    dsa.verify(&pk, msg, c, &hedged).unwrap();
    assert!(
        dsa.verify(&pk, msg, b"zxw", &sig)
            .unwrap_err()
            .refused_kind()
            == Some(RefusedKind::VerificationFailed)
    );
}

#[test]
fn mlkem1024_vector() {
    let v = vectors();
    let k = &v["mlkem1024"];
    let ctx = Context::new().unwrap();
    let cap = ctx.root_capability(1, None, Caps::ALL).unwrap();
    let kem = MlKem1024::new(&cap);
    let (ek, dk) = kem.keygen_derand(&arr32(&k["d"]), &arr32(&k["z"])).unwrap();
    assert_eq!(Hashes::sha3_256(&ek).to_vec(), unhex(&k["ek_sha3_256"]));
    let (ct, ss) = kem.encapsulate_derand(&ek, &arr32(&k["m"])).unwrap();
    assert_eq!(Hashes::sha3_256(&ct).to_vec(), unhex(&k["ct_sha3_256"]));
    assert_eq!(ss.export_bytes_unsafe(), unhex(&k["ss"]));
    let ss2 = kem.decapsulate(&dk, &ct).unwrap();
    assert!(ss.ct_eq(&ss2));
    // Random key pair round trip.
    let (ek, dk) = kem.keygen().unwrap();
    let (ct, a) = kem.encapsulate(&ek).unwrap();
    assert!(a.ct_eq(&kem.decapsulate(&dk, &ct).unwrap()));
    let mut bad = ek.clone();
    bad[..32].fill(0xFF);
    assert_eq!(kem.encapsulate(&bad).unwrap_err().name(), "ZXV_E_FORMAT");
}

#[test]
fn swarm_allotments() {
    let v = vectors();
    let s = &v["swarm"];
    let ctx = Context::new().unwrap();
    let cap = ctx.root_capability(1, None, Caps::ALL).unwrap();
    let swarm = Swarm::new(
        &cap,
        s["levels"].as_u64().unwrap() as u32,
        s["tokens_per_cycle"].as_u64().unwrap(),
    )
    .unwrap();
    for m in s["models"].as_array().unwrap() {
        swarm
            .register(m[0].as_u64().unwrap() as u32, m[1].as_u64().unwrap() as u32)
            .unwrap();
    }
    let ch = |x: &serde_json::Value| char::from_u32(x.as_u64().unwrap() as u32).unwrap();
    swarm
        .set_mood(Feeling::new(
            ch(&s["mood"][0]),
            s["mood"][1].as_u64().unwrap() as u32,
        ))
        .unwrap();
    for f in s["feelings"].as_array().unwrap() {
        swarm
            .set_feeling(
                f[0].as_u64().unwrap() as u32,
                Feeling::new(ch(&f[1]), f[2].as_u64().unwrap() as u32),
            )
            .unwrap();
    }
    swarm.begin_cycle().unwrap();
    let mut total = 0;
    for a in s["allotments"].as_array().unwrap() {
        let al = swarm.allotment(a[0].as_u64().unwrap() as u32).unwrap();
        assert_eq!(al.allotted, a[1].as_u64().unwrap());
        assert_eq!(al.imaginary, a[2].as_u64().unwrap());
        total += al.allotted;
    }
    assert_eq!(total, s["tokens_per_cycle"].as_u64().unwrap());
    for (level, c) in s["level_capacity"].as_array().unwrap().iter().enumerate() {
        assert_eq!(
            Swarm::level_capacity(level as u32).unwrap() as u64,
            c.as_u64().unwrap()
        );
    }
    // Over-asking grants the rest and is not an error.
    let al = swarm.allotment(100).unwrap();
    let g = swarm.consume(100, 10).unwrap();
    assert!(!g.is_partial());
    let g = swarm.consume(100, al.allotted).unwrap();
    assert!(g.is_partial());
    assert_eq!(g.granted, al.allotted - 10);
    swarm.end_cycle().unwrap();
    // A full level is refused with Capacity.
    let e = swarm.register(999, 0).unwrap_err();
    assert_eq!(e.refused_kind(), Some(RefusedKind::Capacity));
    assert_eq!(
        swarm
            .set_mood(Feeling::new('\u{1F600}', 1))
            .unwrap_err()
            .name(),
        "ZXV_E_NOT_FOUND"
    );
    Swarm::emotion_profile('\u{1F914}').unwrap();
}

#[test]
fn evm_state_encoding() {
    let v = vectors();
    let e = &v["evm_state"];
    let ctx = Context::new().unwrap();
    let cap = ctx.root_capability(1, None, Caps::ALL).unwrap();
    let ch = StateChannel::new(&cap);
    let parties: Vec<Address> = e["addresses"]
        .as_array()
        .unwrap()
        .iter()
        .map(|a| unhex(a).try_into().unwrap())
        .collect();
    let state = ch
        .encode_state(
            &arr32(&e["channel_id"]),
            e["nonce"].as_u64().unwrap(),
            &parties,
            &i64s(&e["balances"]),
        )
        .unwrap();
    assert_eq!(state.encoding, unhex(&e["encoding"]));
    assert_eq!(state.digest.to_vec(), unhex(&e["digest"]));
    assert_eq!(Hashes::keccak256(&state.encoding), state.digest);

    // The same from ledger accounts.
    ctx.enable_currency_with_minor_cap(&cap, "USD", 2).unwrap();
    let ledger = Ledger::new(&cap);
    let a = ledger
        .open_account("a", "USD", AccountKind::Issuer)
        .unwrap();
    let b = ledger
        .open_account("b", "USD", AccountKind::Holder)
        .unwrap();
    ledger.post(a, b, 250, "USD", "x").unwrap();
    let id = [0u8; 32];
    let addr: Vec<Address> = vec![[1u8; 20], [2u8; 20]];
    let from_ledger = ch.ledger_state(&id, 1, "USD", &[a, b], &addr).unwrap();
    let explicit = ch.encode_state(&id, 1, &addr, &[-250, 250]).unwrap();
    assert_eq!(from_ledger, explicit);
    assert_eq!(from_ledger.encoding.len(), 10 * 32);
}

#[test]
fn generation_matches_reference_tokens() {
    if !features().contains(Features::TENSOR) {
        eprintln!("skip: engine not built");
        return;
    }
    let v = vectors();
    let g = &v["generate"];
    let dir = fixtures();
    let ctx = Context::new().unwrap();
    let cap = ctx.root_capability(1, None, Caps::ALL).unwrap();
    let model = Model::open(
        &cap,
        dir.join(g["model"].as_str().unwrap()),
        Some(&dir.join(g["tokenizer"].as_str().unwrap())),
    )
    .unwrap();
    let prompt = std::fs::read_to_string(dir.join(g["prompt"].as_str().unwrap())).unwrap();
    let ids: Vec<i64> = model
        .tokenize(&prompt, true)
        .unwrap()
        .into_iter()
        .map(i64::from)
        .collect();
    assert_eq!(ids, i64s(&g["prompt_tokens"]));
    assert_eq!(
        model
            .detokenize(&model.tokenize(&prompt, true).unwrap())
            .unwrap(),
        prompt
    );

    let mut s = model
        .new_session(&cap, g["n_ctx"].as_u64().unwrap() as u32, KvCache::Q16)
        .unwrap();
    let greedy = &g["greedy"];
    let out = s
        .generate(
            &prompt,
            &GenerateOptions::greedy(greedy["max_tokens"].as_u64().unwrap() as u32),
        )
        .unwrap();
    assert_eq!(
        out.tokens.iter().map(|&t| t as i64).collect::<Vec<_>>(),
        i64s(&greedy["tokens"])
    );
    assert_eq!(out.stop, StopReason::MaxTokens);
    assert_eq!(out.prompt_tokens as usize, ids.len());

    let sm = &g["sampled"];
    let q = |k: &str| Rational::new(sm[k].as_i64().unwrap(), 65536).unwrap();
    let opts = GenerateOptions::greedy(sm["max_tokens"].as_u64().unwrap() as u32)
        .temperature(q("temperature_q16"))
        .top_k(sm["top_k"].as_u64().unwrap() as u32)
        .top_p(q("top_p_q16"))
        .repeat_penalty(
            q("repeat_penalty_q16"),
            sm["repeat_last_n"].as_u64().unwrap() as u32,
        )
        .seed(sm["seed"].as_u64().unwrap());
    // The exact decimal forms map to the same Q16 values.
    assert_eq!(
        Rational::new(7, 10).unwrap().to_q16().unwrap() as i64,
        sm["temperature_q16"].as_i64().unwrap()
    );
    s.reset().unwrap();
    let out = s.generate(&prompt, &opts).unwrap();
    assert_eq!(
        out.tokens.iter().map(|&t| t as i64).collect::<Vec<_>>(),
        i64s(&sm["tokens"])
    );
}
