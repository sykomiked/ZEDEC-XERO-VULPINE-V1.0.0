// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
//! ABI 1.4: hardware emulation and hardware updates, mirroring
//! sdk/libzxv/test/test_zxv_abi14.c.

use std::sync::{Arc, Mutex};

use zxv::hw::{self, class};
use zxv::{
    sys, Caps, Context, Error, Features, HwDevice, HwEventKind, HwImageKind, HwImageSpec, HwKind,
    HwMatrix, HwPairing, HwPolicy, HwPower, HwProfile, HwProviderFlags, HwRegion, HwRegionState,
    HwRelease, HwResourceState, RefusedKind, StatusClass, Truth,
};

fn status<T: std::fmt::Debug>(r: zxv::Result<T>) -> i32 {
    r.expect_err("expected an error").status()
}

#[test]
fn version_features_statuses() {
    let (major, minor, _) = zxv::version();
    assert!(major == 1 && minor >= 4);
    assert!(zxv::features().contains(Features::HW));
    assert_eq!(
        zxv::status_name(sys::ZXV_E_QUARANTINED),
        "ZXV_E_QUARANTINED"
    );
    assert_eq!(zxv::status_name(sys::ZXV_E_POLICY), "ZXV_E_POLICY");
    assert_eq!(StatusClass::of(sys::ZXV_E_DISPUTE), StatusClass::Refused);
    assert_eq!(StatusClass::of(sys::ZXV_E_TARGET), StatusClass::Usage);
    for (s, k) in [
        (sys::ZXV_E_NOPROVIDER, RefusedKind::NoProvider),
        (sys::ZXV_E_DISPUTE, RefusedKind::Dispute),
        (sys::ZXV_E_QUARANTINED, RefusedKind::Quarantined),
        (sys::ZXV_E_ROLLBACK, RefusedKind::Rollback),
        (sys::ZXV_E_BLOCKED, RefusedKind::Blocked),
        (sys::ZXV_E_POLICY, RefusedKind::Policy),
    ] {
        assert_eq!(Error::new(s, "").refused_kind(), Some(k));
    }
    assert!(matches!(Error::new(sys::ZXV_E_TARGET, ""), Error::Usage(_)));
    assert_eq!(Caps::ALL_1_4.bits(), (1 << 12) - 1);
    assert!(Caps::ALL_1_4.contains(Caps::HW_MATRIX | Caps::HW_RECONF | Caps::ALL));
    assert!(!Caps::ALL.contains(Caps::HW_MATRIX));
    let ctx = Context::new().unwrap();
    assert!(ctx
        .root_capability(1, None, Caps::from_bits(sys::ZXV_CAP_ALL_1_4 + 1))
        .is_err());
}

#[test]
fn profiles() {
    let host = HwProfile::probe_host().unwrap();
    let r1 = HwProfile::reference().unwrap();
    assert_eq!(r1.node_count().unwrap(), 16);
    assert!(host.node_count().unwrap() >= 2);
    assert_eq!(
        host.cap(2, hw::cap::WORD_BITS).unwrap(),
        (std::mem::size_of::<usize>() * 8) as u64
    );
    assert!(host.cap(2, hw::cap::COUNT).unwrap() >= 1);

    let m = HwProfile::match_host(&host, &r1).unwrap();
    assert!(m.native >= 1 && m.native + m.emulated == 14, "{m:?}");
    let p = HwProfile::pair(&host, &r1, 1).unwrap();
    assert_eq!((p.need_id, p.pairing), (2, HwPairing::Group));
    let p = HwProfile::pair(&host, &r1, 2).unwrap();
    assert_eq!(
        (p.need_id, p.pairing),
        (3, HwPairing::Native { host_id: 2 })
    );
    assert!(matches!(
        HwProfile::pair(&host, &r1, 7).unwrap().pairing,
        HwPairing::Emulate { .. }
    ));
    assert_eq!(
        status(HwProfile::pair(&host, &r1, 99)),
        sys::ZXV_E_NOT_FOUND
    );

    assert_eq!(
        r1.cap(6, hw::cap::CONTRACT_ID).unwrap(),
        u64::from(hw::FN_TENSOR)
    );
    let pres = HwProfile::present(&host, &r1).unwrap();
    assert_eq!(
        pres.cap(3, hw::cap::KIND).unwrap(),
        HwKind::Hard.raw() as u64
    );
    assert_eq!(
        pres.cap(8, hw::cap::KIND).unwrap(),
        HwKind::Soft.raw() as u64
    );
    assert_eq!(status(pres.cap(1, hw::cap::KIND)), sys::ZXV_E_NOT_FOUND);

    // Canonical round trip and digest.
    let enc = r1.encode().unwrap();
    let copy = HwProfile::decode(&enc).unwrap();
    assert_eq!(copy.digest().unwrap(), r1.digest().unwrap());
    assert_eq!(copy.nodes().unwrap(), r1.nodes().unwrap());
    assert_eq!(
        status(HwProfile::decode(&enc[..enc.len() - 1])),
        sys::ZXV_E_FORMAT
    );

    // Build one: a machine built to R1 runs the ternary unit natively.
    let mut mine = HwProfile::new().unwrap();
    mine.add_node(1, hw::ROOT, class::MACHINE).unwrap();
    mine.add_node(2, 1, class::TERNARY_UNIT).unwrap();
    mine.set_cap(2, hw::cap::RADIX, 3).unwrap();
    assert_eq!(status(mine.add_node(2, 1, class::CACHE)), sys::ZXV_E_ARG);
    assert_eq!(
        status(mine.set_cap(9, hw::cap::RADIX, 3)),
        sys::ZXV_E_NOT_FOUND
    );
    mine.add_node(3, 1, class::PRIVATE | 1).unwrap();
    assert_eq!(
        HwProfile::pair(&mine, &r1, 7).unwrap().pairing,
        HwPairing::Native { host_id: 2 }
    );
    let d1 = mine.digest().unwrap();
    mine.set_ext(3, &[0xA5, 0x00, 0x5A]).unwrap();
    assert_ne!(
        mine.digest().unwrap(),
        d1,
        "extension is part of the identity"
    );
    assert_eq!(mine.ext(3).unwrap(), vec![0xA5, 0x00, 0x5A]);
    assert_eq!(mine.ext(2).unwrap(), Vec::<u8>::new());
    assert_eq!(
        status(mine.set_ext(3, &[0u8; hw::EXT_MAX + 1])),
        sys::ZXV_E_RANGE
    );
    let n = mine.node(2).unwrap();
    assert_eq!((n.id, n.parent, n.class), (3, 1, class::PRIVATE | 1));
}

#[test]
fn standard_engines_and_placement() {
    let ctx = Context::new().unwrap();
    let cap = ctx.root_capability(1, None, Caps::ALL_1_4).unwrap();
    let nohw = cap.attenuate(Caps::ALL).unwrap();
    assert!(HwMatrix::new(&nohw).unwrap_err().is_capability_denied());
    let mx = HwMatrix::new(&cap).unwrap();

    let d = mx.call(hw::FN_SHA256, 0, b"abc").unwrap();
    assert_eq!(
        zxv::hex(&d),
        "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"
    );
    let mut mac_in = vec![4u8, 0];
    mac_in.extend_from_slice(b"Jefe");
    mac_in.extend_from_slice(b"what do ya want for nothing?");
    let mac = mx.call(hw::FN_MAC, 0, &mac_in).unwrap();
    assert_eq!(
        zxv::hex(&mac),
        "5bdcc146bf60754e6a042426089575c75a003f089d2739839dec58b964ec3843"
    );
    let mut g = [0u8; 16];
    g[0] = 12;
    g[8] = 18;
    assert_eq!(mx.call(hw::FN_GCD, 0, &g).unwrap(), 6u64.to_le_bytes());
    let mut small = [0u8; 8];
    assert!(mx.call_into(hw::FN_SHA256, 0, b"abc", &mut small).is_err());

    // Discovery.
    let all = mx.providers().unwrap();
    assert!(all.len() >= 5);
    for p in &all {
        assert_eq!(
            (p.kind, p.proof, p.contract),
            (HwKind::Soft, Truth::True, 1)
        );
        assert!(!p.flags.contains(HwProviderFlags::FAULT));
    }
    let mac_pv = mx.find_provider(hw::FN_MAC).unwrap().expect("MAC listed");
    assert_eq!(
        status(mx.provider_info(all.len() as u32)),
        sys::ZXV_E_NOT_FOUND
    );
    let fi = mx.function_info(hw::FN_MAC).unwrap();
    assert!(fi.contract == 1 && fi.policy.contains(HwPolicy::KEY_BEARING) && fi.vectors >= 1);

    // Key-bearing: a cheaper REMOTE implementation never serves it.
    let hits = Arc::new(Mutex::new(0u32));
    let h2 = hits.clone();
    mx.add_resource(7).unwrap();
    let prem = mx
        .add_provider(hw::FN_MAC, 7, HwKind::Remote, 1, move |_, _, _| {
            *h2.lock().unwrap() += 1;
            Err(Error::new(sys::ZXV_E_STATE, "must never serve"))
        })
        .unwrap();
    let _ = mx.attest(prem).unwrap();
    assert_eq!(mx.call(hw::FN_MAC, 0, &mac_in).unwrap(), mac);
    assert_eq!(mx.bound(hw::FN_MAC).unwrap(), mac_pv);
    assert!(mx
        .provider_info(prem)
        .unwrap()
        .flags
        .contains(HwProviderFlags::POLICY));
    let e = mx.set_policy(hw::FN_MAC, HwPolicy::NONE, 0, 0).unwrap_err();
    assert_eq!(e.refused_kind(), Some(RefusedKind::Policy));
    assert_eq!(status(mx.memo(hw::FN_MAC, 16)), sys::ZXV_E_POLICY);
    assert_eq!(
        status(mx.set_policy(hw::FN_MAC, HwPolicy::from_bits(0x80), 0, 0)),
        sys::ZXV_E_ARG
    );

    // A power budget: undeclared traits never qualify, declared ones do.
    mx.set_policy(hw::FN_GCD, HwPolicy::NONE, 0, 500).unwrap();
    assert_eq!(status(mx.call(hw::FN_GCD, 0, &g)), sys::ZXV_E_NOPROVIDER);
    let gcd_pv = mx.find_provider(hw::FN_GCD).unwrap().unwrap();
    mx.set_traits(gcd_pv, 2000, 300, 4).unwrap();
    assert_eq!(
        status(mx.set_traits(gcd_pv, 2000, 300, 16)),
        sys::ZXV_E_RANGE
    );
    mx.call(hw::FN_GCD, 0, &g).unwrap();
    let gi = mx.provider_info(gcd_pv).unwrap();
    assert_eq!((gi.throughput_class, gi.power), (4, HwPower::W1));
    mx.set_policy(hw::FN_GCD, HwPolicy::LOCAL_ONLY, 1000, 0)
        .unwrap();
    assert_eq!(status(mx.call(hw::FN_GCD, 0, &g)), sys::ZXV_E_NOPROVIDER);
    mx.set_policy(hw::FN_GCD, HwPolicy::NONE, 0, 0).unwrap();
}

fn addk(k: u8, bad: Arc<Mutex<u8>>) -> impl Fn(u32, &[u8], &mut [u8]) -> zxv::Result<usize> {
    move |_, input, out| {
        let b = *bad.lock().unwrap();
        if b == 2 {
            return Err(Error::new(sys::ZXV_E_IO, "device error"));
        }
        if input.len() > out.len() {
            return Err(Error::new(sys::ZXV_E_BUFFER, ""));
        }
        for (o, i) in out.iter_mut().zip(input) {
            *o = i.wrapping_add(k).wrapping_add(u8::from(b == 1 && *i == 9));
        }
        Ok(input.len())
    }
}

#[test]
fn matrix_proof_failover_shadow_memo_contract() {
    let ctx = Context::new().unwrap();
    let cap = ctx.root_capability(1, None, Caps::ALL_1_4).unwrap();
    let mx = HwMatrix::new(&cap).unwrap();
    mx.add_function(0x100, Some("add8")).unwrap();
    assert_eq!(
        status(mx.add_function(0x100, Some("again"))),
        sys::ZXV_E_DUPLICATE
    );
    mx.add_resource(1).unwrap();
    mx.add_resource(2).unwrap();
    let (input, ex8) = ([0u8, 1, 9, 200], [8u8, 9, 17, 208]);
    mx.add_vector(0x100, 0, &input, &ex8).unwrap();

    let (fx_bad, sw_bad) = (Arc::new(Mutex::new(0u8)), Arc::new(Mutex::new(0u8)));
    let pfx = mx
        .add_provider(0x100, 2, HwKind::Hard, 1, addk(8, fx_bad.clone()))
        .unwrap();
    let psw = mx
        .add_provider(0x100, 1, HwKind::Soft, 10, addk(8, sw_bad.clone()))
        .unwrap();
    let panicky = mx
        .add_provider(0x100, 1, HwKind::Soft, 30, |_, _, _| panic!("boom"))
        .unwrap();
    assert_eq!(status(mx.call(0x100, 0, &input)), sys::ZXV_E_NOPROVIDER);
    assert_eq!(mx.attest(pfx).unwrap(), Truth::True);
    assert_eq!(mx.attest(psw).unwrap(), Truth::True);
    // A panic is caught at the boundary and fails the proof.
    assert_eq!(mx.attest(panicky).unwrap(), Truth::False);
    assert_eq!(mx.call(0x100, 0, &input).unwrap(), ex8);
    assert_eq!(mx.bound(0x100).unwrap(), pfx);

    mx.set_resource(2, HwResourceState::Faulted).unwrap();
    assert_eq!(mx.call(0x100, 0, &input).unwrap(), ex8);
    assert_eq!(mx.bound(0x100).unwrap(), psw, "failover to the emulator");
    mx.set_resource(2, HwResourceState::Ok).unwrap();

    *fx_bad.lock().unwrap() = 2;
    assert_eq!(status(mx.call(0x100, 0, &input)), sys::ZXV_E_IO);
    *fx_bad.lock().unwrap() = 0;

    let (out, checked) = mx.shadow(0x100, 0, &input).unwrap();
    assert!(checked && out == ex8);
    *sw_bad.lock().unwrap() = 1;
    let e = mx.shadow(0x100, 0, &input).unwrap_err();
    assert_eq!(e.refused_kind(), Some(RefusedKind::Dispute));
    assert_eq!(status(mx.call(0x100, 0, &input)), sys::ZXV_E_NOPROVIDER);
    assert_eq!(mx.attest(pfx).unwrap(), Truth::True);
    assert_eq!(mx.attest(psw).unwrap(), Truth::False);

    assert_eq!(status(mx.memo(0x100, 6)), sys::ZXV_E_ARG);
    mx.memo(0x100, 16).unwrap();
    let calls0 = mx.provider_info(pfx).unwrap().calls;
    mx.call(0x100, 0, &input).unwrap();
    assert_eq!(mx.call(0x100, 0, &input).unwrap(), ex8);
    assert_eq!(mx.provider_info(pfx).unwrap().calls, calls0 + 1);
    assert_eq!(mx.memo(0x100, 16).unwrap(), (1, 1));

    let fi = mx.function_info(0x100).unwrap();
    assert_eq!((fi.contract, fi.policy, fi.vectors), (0, HwPolicy::NONE, 1));
    mx.set_contract(0x100, 2).unwrap();
    assert_eq!(status(mx.set_contract(0x100, 2)), sys::ZXV_E_ROLLBACK);
    assert_eq!(status(mx.call(0x100, 0, &input)), sys::ZXV_E_NOPROVIDER);
    mx.add_vector(0x100, 0, &input, &ex8).unwrap();
    assert_eq!(mx.attest(pfx).unwrap(), Truth::True);
    let pi = mx.provider_info(pfx).unwrap();
    assert_eq!((pi.proof, pi.contract), (Truth::True, 2));
    assert!(mx
        .provider_info(psw)
        .unwrap()
        .flags
        .contains(HwProviderFlags::UNPROVEN));
    mx.enable(pfx, false).unwrap();
    assert!(mx
        .provider_info(pfx)
        .unwrap()
        .flags
        .contains(HwProviderFlags::DISABLED));
}

/// A simulated two-slot device whose active slot is a byte lookup table.
#[derive(Default)]
struct Sim {
    slot: [Vec<u8>; 2],
    active: Option<usize>,
}

struct Device<'m> {
    sim: Arc<Mutex<Sim>>,
    oracle: &'m HwMatrix<'m>,
}

impl HwDevice for Device<'_> {
    fn load(&self, slot: u32, payload: &[u8]) -> zxv::Result<()> {
        if slot > 1 || payload.len() > 256 {
            return Err(Error::new(sys::ZXV_E_RANGE, "slot"));
        }
        self.sim.lock().unwrap().slot[slot as usize] = payload.to_vec();
        Ok(())
    }

    fn readback(&self, slot: u32) -> zxv::Result<[u8; 32]> {
        let bytes = self.sim.lock().unwrap().slot[slot as usize].clone();
        let d = self.oracle.call(hw::FN_SHA256, 0, &bytes)?;
        Ok(d.try_into().expect("32 bytes"))
    }

    fn activate(&self, slot: u32) -> zxv::Result<()> {
        self.sim.lock().unwrap().active = Some(slot as usize);
        Ok(())
    }
}

#[test]
fn packages_regions_and_contracts() {
    let ctx = Context::new().unwrap();
    let cap = ctx.root_capability(1, None, Caps::ALL_1_4).unwrap();
    let nohw = cap.attenuate(Caps::ALL).unwrap();
    let rel = HwRelease::new(&cap);
    let key = rel.keygen().unwrap();
    assert_eq!(key.public().len(), hw::PK_BYTES);
    assert_eq!(key.mldsa_secret().len(), hw::MLDSA_SK_BYTES);
    assert!(!format!("{key:?}").contains("0x"));
    let oracle = HwMatrix::new(&cap).unwrap();

    let mx = HwMatrix::new(&cap).unwrap();
    mx.add_function(0x200, Some("inc")).unwrap();
    mx.add_resource(1).unwrap();
    mx.add_resource(2).unwrap();
    let vin: Vec<u8> = (0..=255u8).collect();
    let inc: Vec<u8> = vin.iter().map(|b| b.wrapping_add(1)).collect();
    let mut wrong = inc.clone();
    wrong[77] = 0;
    mx.add_vector(0x200, 0, &vin, &inc).unwrap();
    let psoft = mx
        .add_provider(0x200, 1, HwKind::Soft, 50, addk(1, Arc::new(Mutex::new(0))))
        .unwrap();
    let sim = Arc::new(Mutex::new(Sim::default()));
    let s2 = sim.clone();
    let preg = mx
        .add_provider(0x200, 2, HwKind::Fabric, 1, move |_, input, out| {
            let s = s2.lock().unwrap();
            let Some(a) = s.active else {
                return Err(Error::new(sys::ZXV_E_STATE, "empty"));
            };
            if input.len() > out.len() || s.slot[a].len() < 256 {
                return Err(Error::new(sys::ZXV_E_STATE, ""));
            }
            for (o, i) in out.iter_mut().zip(input) {
                *o = s.slot[a][*i as usize];
            }
            Ok(input.len())
        })
        .unwrap();
    assert_eq!(mx.attest(psoft).unwrap(), Truth::True);

    let dev = || Device {
        sim: sim.clone(),
        oracle: &oracle,
    };
    assert!(
        HwRegion::new(&nohw, Some((&mx, preg)), 1, 0xD0, None, dev(), 12)
            .unwrap_err()
            .is_capability_denied()
    );
    assert_eq!(
        status(HwRegion::new(
            &cap,
            Some((&mx, psoft)),
            1,
            0xD0,
            None,
            dev(),
            12
        )),
        sys::ZXV_E_ARG
    );
    let rg = HwRegion::new(&cap, Some((&mx, preg)), 1, 0xD0, None, dev(), 12).unwrap();
    rg.trust_key(key.public()).unwrap();
    let events = Arc::new(Mutex::new(Vec::new()));
    let ev2 = events.clone();
    rg.on_event(move |e| ev2.lock().unwrap().push(e));

    // Image build 1 of 3: the good update.
    let spec = HwImageSpec {
        kind: HwImageKind::Partial,
        target_class: 0xD0,
        target_digest: None,
        fn_id: 0x200,
        version: 1,
        svn: 1,
    };
    let good = rel.build(&spec, &inc, &key).unwrap();
    assert_eq!(good.len(), HwRelease::package_len(256));

    // One of the two signatures broken (a flipped SLH-DSA byte): refused,
    // recorded.
    let mut img = good.clone();
    *img.last_mut().unwrap() ^= 1;
    assert_eq!(
        rg.stage(&img).unwrap_err().refused_kind(),
        Some(RefusedKind::VerificationFailed)
    );
    // Wrong hardware (the class is checked before the signature).
    let mut img = good.clone();
    img[12..16].copy_from_slice(&0xD1u32.to_le_bytes());
    assert_eq!(status(rg.stage(&img)), sys::ZXV_E_TARGET);

    rg.stage(&good).unwrap();
    assert_eq!(status(rg.commit()), sys::ZXV_E_STATE);
    rg.trial(5).unwrap();
    assert_eq!(
        mx.bound(0x200).unwrap(),
        preg,
        "the reconfigured region serves"
    );
    rg.commit().unwrap();
    let info = rg.info().unwrap();
    assert_eq!(
        (info.state, info.version, info.floor),
        (HwRegionState::Active, 1, 1)
    );
    {
        let ev = events.lock().unwrap();
        let kinds: Vec<HwEventKind> = ev.iter().map(|e| e.kind).collect();
        assert_eq!(
            kinds,
            vec![
                HwEventKind::Rejected,
                HwEventKind::Staged,
                HwEventKind::Tested,
                HwEventKind::Activated
            ]
        );
        assert!(ev.iter().all(|e| e.region_id == 1));
        assert_eq!(ev[3].version, 1);
    }

    // Replay: refused, recorded.
    assert_eq!(
        rg.stage(&good).unwrap_err().refused_kind(),
        Some(RefusedKind::Rollback)
    );

    // Image build 2 of 3: signed, but a wrong gate. Rolled back to v1,
    // quarantined, and blocked even under a new version number.
    let bad = rel
        .build(&HwImageSpec { version: 2, ..spec }, &wrong, &key)
        .unwrap();
    rg.stage(&bad).unwrap();
    assert_eq!(
        rg.trial(5).unwrap_err().refused_kind(),
        Some(RefusedKind::Quarantined)
    );
    let info = rg.info().unwrap();
    assert_eq!((info.state, info.version), (HwRegionState::Active, 1));
    assert_eq!(mx.bound(0x200).unwrap(), preg, "v1 back in service");
    let mut img = bad.clone();
    img[20..24].copy_from_slice(&3u32.to_le_bytes());
    assert_eq!(
        rg.stage(&img).unwrap_err().refused_kind(),
        Some(RefusedKind::Blocked)
    );

    // The audit log: one event per record, the last one a quarantine.
    let log = rg.log().unwrap();
    let entries = rg.log_entries().unwrap();
    assert_eq!(entries.len() as u32, log.count);
    assert_eq!(events.lock().unwrap().len() as u32, log.count);
    let last = entries.last().unwrap();
    assert_eq!(
        (last.event, last.id, last.version),
        (HwEventKind::Quarantined, 1, 2)
    );
    assert_eq!(last.hash, log.head);
    assert_eq!(last.tag.polarity(), zxv::HexPolarity::Forbidden);
    let act = entries
        .iter()
        .find(|e| e.event == HwEventKind::Activated)
        .unwrap();
    assert!(act.version == 1 && act.tag.polarity() != zxv::HexPolarity::Forbidden);
    assert_eq!(status(rg.log_entry(log.count)), sys::ZXV_E_NOT_FOUND);

    // Image build 3 of 3: a newer golden-vector contract for "inc".
    let mut pay = Vec::new();
    pay.extend_from_slice(&0u32.to_le_bytes()); // op
    pay.extend_from_slice(&2u32.to_le_bytes()); // input length
    pay.extend_from_slice(&[5, 250]);
    pay.extend_from_slice(&2u32.to_le_bytes()); // output length
    pay.extend_from_slice(&[6, 251]);
    let cspec = HwImageSpec {
        kind: HwImageKind::Vectors,
        target_class: 0,
        target_digest: None,
        fn_id: 0x200,
        version: 7,
        svn: 3,
    };
    let pkg = rel.build(&cspec, &pay, &key).unwrap();
    // No key trusted yet.
    assert_eq!(status(mx.apply_contract(&pkg)), sys::ZXV_E_VERIFY);
    assert_eq!(status(mx.trust_key(&key.public()[1..])), sys::ZXV_E_ARG);
    mx.trust_key(key.public()).unwrap();
    assert_eq!(status(mx.apply_contract(&good)), sys::ZXV_E_FORMAT);
    assert_eq!(status(rg.stage(&pkg)), sys::ZXV_E_FORMAT);
    assert_eq!(mx.apply_contract(&pkg).unwrap(), 2);
    let fi = mx.function_info(0x200).unwrap();
    assert_eq!((fi.contract, fi.vectors), (7, 1));
    assert_eq!(status(mx.apply_contract(&pkg)), sys::ZXV_E_ROLLBACK);
    let ml = mx.log().unwrap();
    assert!(ml.count >= 1);
    let rec = mx
        .log_entries()
        .unwrap()
        .into_iter()
        .find(|e| e.event == HwEventKind::Contract)
        .expect("contract record");
    assert_eq!((rec.id, rec.version), (0x200, 7));

    // Fill the region's log (12 records) with refusals; the next refusal
    // halts the region and poisons the context: fail closed.
    let mut tampered = good.clone();
    *tampered.last_mut().unwrap() ^= 1;
    let left = 12 - rg.log().unwrap().count;
    for _ in 0..left {
        assert_eq!(status(rg.stage(&tampered)), sys::ZXV_E_VERIFY);
    }
    assert!(rg.stage(&tampered).unwrap_err().is_fatal());
    assert!(ctx.is_poisoned());
    assert!(mx.call(0x200, 0, &vin[..4]).unwrap_err().is_fatal());
}
