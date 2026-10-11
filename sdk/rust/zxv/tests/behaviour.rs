// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
//! Behaviour of the safe API against the real library: fail-closed,
//! capabilities, budgets (sync and async), phase deferral, the engine,
//! ledger and messages.

mod common;

use std::future::IntoFuture;
use std::sync::atomic::{AtomicBool, AtomicU32, Ordering};
use std::sync::Arc;
use std::time::Duration;

use common::*;
use zxv::*;

fn assert_send_sync<T: Send + Sync>() {}
fn assert_send<T: Send>() {}

#[test]
fn thread_safety_markers() {
    assert_send_sync::<Context>();
    assert_send_sync::<Capability<'static>>();
    assert_send_sync::<Model>();
    assert_send_sync::<Budgets<'static>>();
    assert_send_sync::<PhaseClock<'static>>();
    assert_send_sync::<Swarm<'static>>();
    assert_send_sync::<Attestations<'static>>();
    assert_send::<Session<'static>>();
    assert_send::<iso20022::Message<'static>>();
}

#[test]
fn fatal_path_poisons_context_and_handles() {
    let ctx = Context::new().unwrap();
    let cap = ctx.root_capability(7, None, Caps::ALL).unwrap();
    ctx.enable_currency_with_minor_cap(&cap, "NGN", 2).unwrap();
    let ledger = Ledger::new(&cap);
    let bank = ledger
        .open_account("bank", "NGN", AccountKind::Issuer)
        .unwrap();
    let alice = ledger
        .open_account("alice", "NGN", AccountKind::Holder)
        .unwrap();
    ledger.post(bank, alice, 500, "NGN", "r1").unwrap();

    ctx.halt("test halt").unwrap();
    assert!(ctx.is_poisoned());
    let e = ledger.post(bank, alice, 1, "NGN", "r2").unwrap_err();
    assert!(e.is_fatal(), "{e}");
    assert_eq!(e.class(), StatusClass::Fatal);
    assert_eq!(e.name(), "ZXV_E_POISONED");
    // Every later call fails fast, on every handle of the context.
    assert!(ledger.balance(alice).unwrap_err().is_fatal());
    assert!(cap.attenuate(Caps::LEDGER_READ).unwrap_err().is_fatal());
    assert!(ctx.enable_currency_cap(&cap, "USD").unwrap_err().is_fatal());
    assert!(Budgets::new(&cap).err().unwrap().is_fatal());
    // Pure functions keep working.
    assert_eq!(Rational::new(1, 2).unwrap().to_string(), "0.5");
}

#[test]
fn fatal_from_c_poisons_rust_handle() {
    // A FATAL reported by C (not by a halt through this SDK) also poisons
    // the Rust handle.
    let ctx = Context::new().unwrap();
    let cap = ctx.root_capability(7, None, Caps::ALL).unwrap();
    // SAFETY: valid context pointer; halting is allowed at any time.
    unsafe { zxv::sys::zxv_ctx_halt(ctx.as_raw(), std::ptr::null()) };
    let e = cap.check(Caps::LEDGER_READ).unwrap_err();
    assert!(e.is_fatal());
    assert!(ctx.is_poisoned());
    // Now refused before reaching C, with the same class.
    assert!(ctx.enable_currency_cap(&cap, "USD").unwrap_err().is_fatal());
}

#[test]
fn abort_policy_terminates_the_process() {
    if std::env::var_os("ZXV_ABORT_CHILD").is_some() {
        let ctx = Context::with_fatal_policy(FatalPolicy::Abort).unwrap();
        let cap = ctx.root_capability(1, None, Caps::ALL).unwrap();
        ctx.halt("abort test").unwrap();
        let _ = cap.check(Caps::LEDGER_READ);
        unreachable!("FatalPolicy::Abort must not return");
    }
    let out = std::process::Command::new(std::env::current_exe().unwrap())
        .args([
            "--exact",
            "abort_policy_terminates_the_process",
            "--nocapture",
            "--test-threads=1",
        ])
        .env("ZXV_ABORT_CHILD", "1")
        .output()
        .unwrap();
    assert!(!out.status.success());
    #[cfg(unix)]
    {
        use std::os::unix::process::ExitStatusExt;
        assert_eq!(out.status.signal(), Some(6), "SIGABRT");
    }
    assert!(String::from_utf8_lossy(&out.stderr).contains("FatalPolicy::Abort"));
}

#[test]
fn capability_denial_attenuation_and_revocation() {
    let ctx = Context::new().unwrap();
    let root = ctx.root_capability(7, Some(&[9u8; 21]), Caps::ALL).unwrap();
    let ro = root.attenuate(Caps::LEDGER_READ).unwrap();
    ro.check(Caps::LEDGER_READ).unwrap();
    let e = ro.check(Caps::LEDGER_POST).unwrap_err();
    assert!(e.is_capability_denied());
    assert_eq!(e.class(), StatusClass::Refused);
    // Attenuation never widens.
    let wider = ro.attenuate(Caps::ALL).unwrap();
    assert!(wider.check(Caps::LEDGER_POST).is_err());
    drop(wider);

    // BOTH attestation blocks and says so.
    root.set_attestation(Truth::Both).unwrap();
    let e = root.check(Caps::LEDGER_READ).unwrap_err();
    assert!(e.detail().contains("contradictory"), "{}", e.detail());
    root.set_attestation(Truth::True).unwrap();
    let info = root.info().unwrap();
    assert_eq!(info.process_id, 7);
    assert_eq!(info.caps, Caps::ALL);
    assert_eq!(info.attestation, Truth::True);

    ctx.enable_currency_with_minor_cap(&root, "NGN", 2).unwrap();
    let ledger = Ledger::new(&root);
    let a = ledger
        .open_account("issuer", "NGN", AccountKind::Issuer)
        .unwrap();
    let b = ledger
        .open_account("alice", "NGN", AccountKind::Holder)
        .unwrap();
    ledger.post(a, b, 500, "NGN", "r1").unwrap();
    let ro_ledger = Ledger::new(&ro);
    assert!(ro_ledger
        .post(a, b, 1, "NGN", "r2")
        .unwrap_err()
        .is_capability_denied());
    assert!(ro_ledger
        .open_account("x", "NGN", AccountKind::Holder)
        .unwrap_err()
        .is_capability_denied());
    assert_eq!(ro_ledger.balance(b).unwrap().equity, 500);

    root.revoke().unwrap();
    assert!(
        ro.check(Caps::LEDGER_READ)
            .unwrap_err()
            .is_capability_denied(),
        "revoking the parent revokes the child"
    );
    assert!(ro.info().unwrap().revoked);
    // Unknown bits are a usage error.
    let e = ctx
        .root_capability(1, None, Caps::from_bits(1 << 40))
        .unwrap_err();
    assert_eq!(e.class(), StatusClass::Usage);
}

#[test]
fn ledger_and_currencies() {
    let ctx = Context::new().unwrap();
    let cap = ctx.root_capability(1, None, Caps::ALL).unwrap();
    ctx.enable_currency_cap(&cap, "NGN").unwrap();
    ctx.register_private_currency_cap(&cap, "VFV", 2).unwrap();
    assert_eq!(ctx.currency("NGN").unwrap().minor_units, 2);
    assert_ne!(
        ctx.currency("VFV").unwrap().flags & iso4217::flags::PRIVATE,
        0
    );
    assert_eq!(
        ctx.enabled_currencies().unwrap(),
        vec!["NGN".to_string(), "VFV".to_string()]
    );
    let l = Ledger::new(&cap);
    let bank = l.open_account("bank", "NGN", AccountKind::Issuer).unwrap();
    let alice = l.open_account("alice", "NGN", AccountKind::Holder).unwrap();
    let bob = l.open_account("bob", "NGN", AccountKind::Holder).unwrap();
    l.post(bank, alice, 10_000, "NGN", "E1").unwrap();
    l.post(alice, bob, 2_500, "NGN", "E2").unwrap();
    let e = l.post(bob, alice, 1_000_000, "NGN", "E3").unwrap_err();
    assert_eq!(e.refused_kind(), Some(RefusedKind::InsufficientFunds));
    assert_eq!(
        l.post(alice, bob, 1, "NGN", "E2").unwrap_err().name(),
        "ZXV_E_DUPLICATE"
    );
    assert_eq!(
        l.balance(alice).unwrap(),
        Balance {
            debit: 10_000,
            credit: 2_500,
            equity: 7_500
        }
    );
    l.check().unwrap();
    let info = l.account(alice).unwrap();
    assert_eq!(
        (info.name.as_str(), info.currency.as_str(), info.kind),
        ("alice", "NGN", AccountKind::Holder)
    );
    assert_eq!(l.account_count().unwrap(), 3);
    let j = l.journal(alice).unwrap();
    assert_eq!(j.len(), 2);
    assert_eq!(
        (j[0].amount, j[0].counterparty, j[0].reference.as_str()),
        (10_000, bank, "E1")
    );
    assert_eq!((j[1].amount, j[1].counterparty), (-2_500, bob));
}

#[test]
fn iso_reference_data_and_messages() {
    let ngn = iso4217::by_alpha("NGN").unwrap();
    assert_eq!((ngn.numeric, ngn.minor_units), (566, Some(2)));
    assert_eq!(iso4217::by_numeric(566).unwrap().alpha, "NGN");
    assert_eq!(
        iso4217::by_alpha("VFV").unwrap_err().name(),
        "ZXV_E_NOT_FOUND"
    );
    assert_eq!(
        iso4217::by_numeric(777).unwrap_err().name(),
        "ZXV_E_NOT_FOUND"
    );
    assert_eq!(iso4217::name("NGN").unwrap(), "Naira");
    assert!(iso4217::all().unwrap().len() > 100);
    assert!(!iso4217::published().unwrap().is_empty());
    assert!(!iso4217::caveat(777).unwrap().is_empty());
    assert!(iso4217::is_au_member("NG").unwrap());
    assert_eq!(iso4217::Rail::Credit.numeric(), 777);
    assert!(iso4217::CardNetwork::Dragon
        .check_digit_valid("4111111111111111")
        .unwrap());

    let ctx = Context::new().unwrap();
    let cap = ctx.root_capability(1, None, Caps::LEDGER_READ).unwrap();
    let mut m = cap.new_message(iso20022::MessageKind::Pacs008).unwrap();
    use iso20022::TextField::*;
    m.set_text(MsgId, "MSG-1")
        .unwrap()
        .set_text(CreationDateTime, "2026-10-11T12:00:00Z")
        .unwrap()
        .set_text(DebtorName, "Ada & Co")
        .unwrap()
        .set_text(DebtorAccount, "NG001")
        .unwrap()
        .set_text(CreditorName, "Bola")
        .unwrap()
        .set_text(CreditorAccount, "NG002")
        .unwrap()
        .set_text(EndToEndId, "E2E-1")
        .unwrap()
        .set_text(Currency, "NGN")
        .unwrap()
        .set_amount(iso20022::AmountField::Settlement, 12_550, 2)
        .unwrap();
    let xml = m.render().unwrap();
    assert!(xml.contains("125.50"), "{xml}");
    assert!(xml.contains("Ada &amp; Co"));
    assert!(
        cap.new_message(iso20022::MessageKind::Pacs002)
            .err()
            .unwrap()
            .refused_kind()
            == Some(RefusedKind::NotImplemented)
    );
}

#[test]
fn attestations_hold_contradictions() {
    let ctx = Context::new().unwrap();
    let cap = ctx.root_capability(1, None, Caps::ALL).unwrap();
    let reg = Attestations::new(&cap).unwrap();
    let i = reg
        .attest("model.gguf", 6, Truth::True, "loader", &[0xAB; 32])
        .unwrap();
    assert!(reg.gate_clear().unwrap());
    reg.evidence(i, true).unwrap();
    reg.evidence(i, false).unwrap();
    assert_eq!(
        reg.state(i).unwrap(),
        AttestationState {
            state: Truth::Both,
            positive: 2,
            negative: 1
        }
    );
    assert!(!reg.gate_clear().unwrap());
    assert_eq!(reg.find("model.gguf").unwrap(), i);
    // Creating a registry needs ATTEST (ABI 1.2), and so does attesting.
    let ro = cap.attenuate(Caps::LEDGER_READ).unwrap();
    assert!(Attestations::new(&ro).err().unwrap().is_capability_denied());
    let at = cap.attenuate(Caps::ATTEST).unwrap();
    let reg2 = Attestations::new(&at).unwrap();
    at.set_attestation(Truth::Neither).unwrap();
    assert!(reg2
        .attest("x", 0, Truth::True, "a", &[])
        .unwrap_err()
        .is_capability_denied());
}

#[test]
fn budget_exhaustion_is_refused_and_invalid_is_usage() {
    let ctx = Context::new().unwrap();
    let cap = ctx.root_capability(1, None, Caps::ALL).unwrap();
    let budgets = Budgets::new(&cap).unwrap();
    let denied = Arc::new(AtomicU32::new(0));
    let d2 = denied.clone();
    budgets.on_exhausted(move |_| {
        d2.fetch_add(1, Ordering::SeqCst);
    });
    let b = budgets
        .create("tokens", Resource::Tokens, Rational::from(10), true)
        .unwrap();
    b.consume(Rational::new(7, 2).unwrap()).unwrap();
    b.consume(Rational::new(13, 2).unwrap()).unwrap();
    let e = b.consume(Rational::new(1, 3).unwrap()).unwrap_err();
    assert!(e.is_budget_exhausted(), "{e}");
    assert_eq!(e.class(), StatusClass::Refused);
    assert_eq!(denied.load(Ordering::SeqCst), 1);
    let e = b.consume(Rational::from(-1)).unwrap_err();
    assert_eq!(
        (e.class(), e.name()),
        (StatusClass::Usage, "ZXV_E_RMAG_INVALID")
    );
    let s = b.status().unwrap();
    assert_eq!(
        (s.remaining, s.used, s.epoch),
        (Rational::ZERO, Rational::from(10), 0)
    );
    b.release(Rational::new(1, 2).unwrap()).unwrap();
    let s = b.status().unwrap();
    assert_eq!((s.remaining, s.epoch), (Rational::new(1, 2).unwrap(), 1));
    // Denied capability.
    let ro = cap.attenuate(Caps::LEDGER_READ).unwrap();
    assert!(Budgets::new(&ro).err().unwrap().is_capability_denied());
    let bc = cap.attenuate(Caps::BUDGET).unwrap();
    let nb = Budgets::new(&bc).unwrap();
    bc.revoke().unwrap();
    assert!(nb
        .create("x", Resource::Energy, Rational::ONE, true)
        .unwrap_err()
        .is_capability_denied());
}

#[test]
fn consume_async_waits_for_release_futures_executor() {
    let ctx = Context::new().unwrap();
    let cap = ctx.root_capability(1, None, Caps::ALL).unwrap();
    let budgets = Budgets::new(&cap).unwrap();
    let b = budgets
        .create("compute", Resource::ComputeCycles, Rational::from(5), true)
        .unwrap();
    b.consume(Rational::from(5)).unwrap();
    std::thread::scope(|s| {
        let waiter = s.spawn(|| futures::executor::block_on(b.consume_async(Rational::from(3))));
        std::thread::sleep(Duration::from_millis(50));
        assert!(!waiter.is_finished(), "must wait while exhausted");
        b.release(Rational::from(1)).unwrap(); // not enough yet: retries and waits again
        std::thread::sleep(Duration::from_millis(50));
        assert!(!waiter.is_finished());
        b.release(Rational::from(2)).unwrap();
        waiter.join().unwrap().unwrap();
    });
    assert_eq!(b.status().unwrap().used, Rational::from(5));
    // INVALID fails immediately.
    let e = futures::executor::block_on(b.consume_async(Rational::from(-2))).unwrap_err();
    assert_eq!(e.name(), "ZXV_E_RMAG_INVALID");
}

#[tokio::test(flavor = "current_thread")]
async fn consume_async_on_tokio_with_timeout_and_set_limit() {
    let ctx = Context::new().unwrap();
    let cap = ctx.root_capability(1, None, Caps::ALL).unwrap();
    let budgets = Budgets::new(&cap).unwrap();
    let b = budgets
        .create("bw", Resource::Bandwidth, Rational::from(1), true)
        .unwrap();
    b.consume(Rational::ONE).unwrap();
    // A timeout cancels the wait by dropping the future.
    let r = tokio::time::timeout(Duration::from_millis(30), b.consume_async(Rational::ONE)).await;
    assert!(r.is_err(), "timed out while exhausted");
    // Raising the limit replenishes.
    let fut = b.consume_async(Rational::ONE);
    let raise = async {
        tokio::time::sleep(Duration::from_millis(10)).await;
        b.set_limit(Rational::from(2)).unwrap();
    };
    let (r, ()) = tokio::join!(fut, raise);
    r.unwrap();
    assert_eq!(b.status().unwrap().remaining, Rational::ZERO);
}

#[test]
fn phase_deferral_manual_driver() {
    let ctx = Context::new().unwrap();
    let cap = ctx.root_capability(1, None, Caps::ALL).unwrap();
    let clock = PhaseClock::new(&cap).unwrap();
    let ev = clock.defer(PHASE_CYCLE).unwrap();
    assert_eq!(clock.next_due().unwrap(), Some(PHASE_CYCLE));
    assert_eq!(
        clock.advance(PHASE_CYCLE - 1).unwrap(),
        Advanced {
            now: PHASE_CYCLE - 1,
            cleared: 0
        }
    );
    assert_eq!(ev.state().unwrap(), EventState::Pending);
    std::thread::scope(|s| {
        let w = s.spawn(|| futures::executor::block_on(ev.cleared()));
        std::thread::sleep(Duration::from_millis(30));
        assert!(!w.is_finished(), "S0 one tick early");
        assert_eq!(
            clock.advance(1).unwrap(),
            Advanced {
                now: 27_720,
                cleared: 1
            }
        );
        assert_eq!(w.join().unwrap().unwrap(), EventState::Cleared);
    });
    assert_eq!(clock.next_due().unwrap(), None);
    // Cancellation resolves the future too.
    let ev2 = clock.after(10).unwrap();
    ev2.cancel().unwrap();
    assert_eq!(
        futures::executor::block_on(ev2.into_future()).unwrap(),
        EventState::Cancelled
    );
    assert_eq!(
        ev2.cancel().unwrap_err().refused_kind(),
        Some(RefusedKind::State)
    );
    // A due tick in the past is a usage error.
    assert_eq!(clock.defer(1).unwrap_err().class(), StatusClass::Usage);

    // Admission.
    let s0 = StepFlags {
        s0_pending: true,
        ..Default::default()
    };
    assert_eq!(clock.admit(6, 1, s0).unwrap(), Decision::Defer);
    clock.set_health(6, Health::Healthy).unwrap();
    let healthy = StepFlags {
        requires_health: true,
        ..Default::default()
    };
    assert!(matches!(
        clock.admit(6, 2, healthy).unwrap(),
        Decision::Admit(_)
    ));
    let nophase = cap.attenuate(Caps::LEDGER_READ).unwrap();
    assert!(PhaseClock::new(&nophase)
        .err()
        .unwrap()
        .is_capability_denied());
    let ph = cap.attenuate(Caps::PHASE).unwrap();
    let c2 = PhaseClock::new(&ph).unwrap();
    ph.revoke().unwrap();
    assert!(c2.advance(1).unwrap_err().is_capability_denied());
}

#[test]
fn phase_realtime_driver() {
    let ctx = Context::new().unwrap();
    let cap = ctx.root_capability(1, None, Caps::ALL).unwrap();
    let clock = PhaseClock::new(&cap).unwrap();
    let stop = AtomicBool::new(false);
    let ev = clock.defer(50).unwrap();
    std::thread::scope(|s| {
        let driver = s.spawn(|| clock.run_realtime(1_000, &stop));
        let r = futures::executor::block_on(ev.cleared());
        stop.store(true, Ordering::Release);
        driver.join().unwrap().unwrap();
        assert_eq!(r.unwrap(), EventState::Cleared);
    });
    assert!(clock.now().unwrap() >= 50);
}

#[test]
fn engine_callback_stream_budget_and_lora() {
    if !features().contains(Features::TENSOR) {
        return;
    }
    let v = vectors();
    let greedy: Vec<i32> = i64s(&v["generate"]["greedy"]["tokens"])
        .into_iter()
        .map(|t| t as i32)
        .collect();
    let dir = fixtures();
    let ctx = Context::new().unwrap();
    let cap = ctx.root_capability(1, None, Caps::ALL).unwrap();
    let noai = cap.attenuate(Caps::LEDGER_READ).unwrap();
    let mp = dir.join("tiny-qwen2.gguf");
    let tp = dir.join("tiny-tokenizer.gguf");
    assert!(Model::open(&noai, &mp, Some(&tp))
        .unwrap_err()
        .is_capability_denied());
    let e = Model::open(&cap, "/nonexistent.gguf", None).unwrap_err();
    assert_eq!((e.class(), e.name()), (StatusClass::System, "ZXV_E_IO"));
    let model = Model::open(&cap, &mp, Some(&tp)).unwrap();
    assert_eq!(model.info(ModelInfo::VocabSize).unwrap(), 1200);
    let prompt = std::fs::read_to_string(dir.join("prompt.txt")).unwrap();

    // Model from memory gives the same tokens.
    let mem = Model::open_memory(
        &cap,
        &std::fs::read(&mp).unwrap(),
        Some(&std::fs::read(&tp).unwrap()),
    )
    .unwrap();
    let mut s = mem.new_session(&cap, 128, KvCache::Q16).unwrap();
    assert_eq!(
        s.generate(&prompt, &GenerateOptions::greedy(16))
            .unwrap()
            .tokens,
        greedy
    );
    drop(s);

    // The budget set must outlive any session bound to it (the borrow
    // checker enforces this), so it is created first.
    let budgets = Budgets::new(&cap).unwrap();
    let mut s = model.new_session(&cap, 128, KvCache::Q16).unwrap();
    // Callback stop.
    let mut seen = Vec::new();
    let g = s
        .generate_with(&prompt, &GenerateOptions::greedy(16), |t| {
            seen.push(t.id);
            seen.len() < 3
        })
        .unwrap();
    assert_eq!(g.stop, StopReason::Callback);
    assert_eq!(seen, greedy[..3]);
    assert_eq!(g.tokens, greedy[..3]);

    // Streaming iterator.
    s.reset().unwrap();
    let (ids, g) = s
        .stream(&prompt, &GenerateOptions::greedy(16), |tokens| {
            tokens.map(|t| t.id).collect::<Vec<_>>()
        })
        .unwrap();
    assert_eq!(ids, greedy);
    assert_eq!(g.stop, StopReason::MaxTokens);
    // Dropping the stream early stops generation.
    s.reset().unwrap();
    let (first, g) = s
        .stream(&prompt, &GenerateOptions::greedy(16), |mut t| {
            t.next().map(|t| t.id)
        })
        .unwrap();
    assert_eq!(first, Some(greedy[0]));
    assert!(g.tokens.len() < 16);
    assert_eq!(g.stop, StopReason::Callback);

    // A panic in the callback resumes on the caller.
    s.reset().unwrap();
    let r = std::panic::catch_unwind(std::panic::AssertUnwindSafe(|| {
        let _ = s.generate_with(&prompt, &GenerateOptions::greedy(4), |_| panic!("boom"));
    }));
    assert!(r.is_err());

    // Budget binding: 8 tokens of budget stops generation early; then the
    // prompt itself is refused up front.
    let n_prompt = model.tokenize(&prompt, true).unwrap().len() as i32;
    let b = budgets
        .create("gen", Resource::Tokens, Rational::from(n_prompt + 8), true)
        .unwrap();
    s.reset().unwrap();
    s.bind_budget(b).unwrap();
    let g = s.generate(&prompt, &GenerateOptions::greedy(16)).unwrap();
    assert_eq!((g.stop, g.tokens.len()), (StopReason::Budget, 8));
    s.reset().unwrap();
    assert!(s
        .generate(&prompt, &GenerateOptions::greedy(16))
        .unwrap_err()
        .is_budget_exhausted());
    s.unbind_budget().unwrap();
    s.reset().unwrap();
    assert_eq!(
        s.generate(&prompt, &GenerateOptions::greedy(16))
            .unwrap()
            .tokens,
        greedy
    );
    assert!(s.position().unwrap() > 0);

    // Generation across threads: the model is shared, sessions move.
    std::thread::scope(|sc| {
        for _ in 0..2 {
            sc.spawn(|| {
                let mut s = model.new_session(&cap, 128, KvCache::Q8).unwrap();
                s.generate(&prompt, &GenerateOptions::greedy(4)).unwrap();
            });
        }
    });

    // LoRA is honestly not implemented.
    assert!(!features().contains(Features::LORA));
    let e = model.load_lora(&cap, "x", Rational::ONE).unwrap_err();
    assert_eq!(e.refused_kind(), Some(RefusedKind::NotImplemented));
    // Generation needs AI_INFERENCE.
    let mut s2 = model.new_session(&noai, 128, KvCache::Q16).unwrap();
    assert!(s2
        .generate(&prompt, &GenerateOptions::greedy(1))
        .unwrap_err()
        .is_capability_denied());
}

#[test]
fn secrets_and_signing_need_capabilities() {
    let ctx = Context::new().unwrap();
    let cap = ctx.root_capability(1, None, Caps::ALL).unwrap();
    let kem_only = cap.attenuate(Caps::CRYPTO_KEM).unwrap();
    let ed = Ed25519::new(&kem_only);
    let sk = SecretKey::from_bytes(&[7u8; 32]);
    assert!(ed.sign(&sk, b"x").unwrap_err().is_capability_denied());
    assert_eq!(format!("{sk:?}"), "SecretKey(32 bytes, redacted)");
    let (pk, sk) = Ed25519::new(&cap).keygen().unwrap();
    let sig = Ed25519::new(&cap).sign(&sk, b"hello").unwrap();
    Ed25519::new(&cap).verify(&pk, b"hello", &sig).unwrap();
    assert_eq!(
        MlDsa87::new(&cap)
            .verify(&[0; 3], b"", b"", &[0; 3])
            .unwrap_err()
            .class(),
        StatusClass::Usage
    );
    let mut a = [0u8; 32];
    let mut b = [0u8; 32];
    random(&mut a).unwrap();
    random(&mut b).unwrap();
    assert_ne!(a, b);
}

#[test]
fn rational_and_truth_semantics() {
    let a: Rational = "0.1".parse().unwrap();
    let b: Rational = "0.2".parse().unwrap();
    assert_eq!(a.checked_add(b).unwrap(), "3/10".parse().unwrap());
    let max = Rational::from_integer(i64::MAX).unwrap();
    assert_eq!(
        max.checked_mul(Rational::from(2)).unwrap_err().name(),
        "ZXV_E_RANGE"
    );
    assert_eq!(
        Rational::ONE
            .checked_div(Rational::ZERO)
            .unwrap_err()
            .name(),
        "ZXV_E_ARG"
    );
    assert!(Rational::from_integer(i64::MIN).is_err());
    assert!(Rational::new(1, 3).unwrap() > "0.333333".parse().unwrap());
    assert_eq!(Rational::new(6, -4).unwrap(), Rational::new(-3, 2).unwrap());
    assert_eq!(format!("{:>6}", Rational::new(1, 3).unwrap()), "   1/3");
    assert!(!Truth::Both.to_bool(false, false));
    assert!(Truth::True.require_true().is_ok());
    assert!(Truth::Both.require_true().is_err());
    assert!(!Truth::Neither.is_certain());
}

#[test]
fn cap_forms_need_the_right_bit() {
    let ctx = Context::new().unwrap();
    let root = ctx.root_capability(1, None, Caps::ALL).unwrap();
    let post = root.attenuate(Caps::LEDGER_POST).unwrap();
    let read = root.attenuate(Caps::LEDGER_READ).unwrap();

    // LEDGER_POST: currencies and opening accounts.
    ctx.enable_currency_cap(&post, "NGN").unwrap();
    ctx.enable_currency_with_minor_cap(&post, "GHS", 2).unwrap();
    ctx.register_private_currency_cap(&post, "VFV", 2).unwrap();
    assert_eq!(ctx.enabled_currencies().unwrap(), ["NGN", "GHS", "VFV"]);
    assert!(ctx
        .enable_currency_cap(&read, "KES")
        .unwrap_err()
        .is_capability_denied());
    assert!(ctx
        .enable_currency_with_minor_cap(&read, "KES", 2)
        .unwrap_err()
        .is_capability_denied());
    assert!(ctx
        .register_private_currency_cap(&read, "VFX", 2)
        .unwrap_err()
        .is_capability_denied());
    let writer = Ledger::new(&post);
    let reader = Ledger::new(&read);
    let bank = writer
        .open_account("bank", "NGN", AccountKind::Issuer)
        .unwrap();
    let ada = writer
        .open_account("ada", "NGN", AccountKind::Holder)
        .unwrap();
    assert!(reader
        .open_account("x", "NGN", AccountKind::Holder)
        .unwrap_err()
        .is_capability_denied());
    writer.post(bank, ada, 7, "NGN", "R-1").unwrap();

    // LEDGER_READ: account metadata, journals and messages.
    assert_eq!(reader.account_count().unwrap(), 2);
    assert_eq!(reader.account(ada).unwrap().name, "ada");
    assert_eq!(reader.journal(ada).unwrap()[0].reference, "R-1");
    assert!(writer.account_count().unwrap_err().is_capability_denied());
    assert!(writer.account(ada).unwrap_err().is_capability_denied());
    assert!(writer.journal(ada).unwrap_err().is_capability_denied());
    assert!(post
        .new_message(iso20022::MessageKind::Pacs008)
        .err()
        .unwrap()
        .is_capability_denied());
    let mut m = read.new_message(iso20022::MessageKind::Pacs008).unwrap();
    m.set_text(iso20022::TextField::MsgId, "M-1").unwrap();
    // The message re-checks its capability on every call.
    read.set_attestation(Truth::Neither).unwrap();
    assert!(m
        .set_text(iso20022::TextField::MsgId, "M-2")
        .err()
        .unwrap()
        .is_capability_denied());
    read.set_attestation(Truth::True).unwrap();

    // Handle creation: BUDGET, PHASE, ATTEST, SWARM.
    assert!(Budgets::new(&read).err().unwrap().is_capability_denied());
    assert!(PhaseClock::new(&read).err().unwrap().is_capability_denied());
    assert!(Attestations::new(&read)
        .err()
        .unwrap()
        .is_capability_denied());
    assert!(Swarm::new(&read, 2, 10)
        .err()
        .unwrap()
        .is_capability_denied());
    let b = root.attenuate(Caps::BUDGET).unwrap();
    let p = root.attenuate(Caps::PHASE).unwrap();
    let t = root.attenuate(Caps::ATTEST).unwrap();
    let w = root.attenuate(Caps::SWARM).unwrap();
    Budgets::new(&b).unwrap();
    PhaseClock::new(&p).unwrap();
    Attestations::new(&t).unwrap();
    Swarm::new(&w, 2, 10).unwrap();
}

#[test]
fn cap_from_another_context_is_refused() {
    let ctx = Context::new().unwrap();
    let other = Context::new().unwrap();
    let foreign = other.root_capability(1, None, Caps::ALL).unwrap();
    for e in [
        ctx.enable_currency_cap(&foreign, "NGN").unwrap_err(),
        ctx.enable_currency_with_minor_cap(&foreign, "NGN", 2)
            .unwrap_err(),
        ctx.register_private_currency_cap(&foreign, "VFV", 2)
            .unwrap_err(),
    ] {
        assert!(e.is_capability_denied(), "{e}");
        assert_eq!(e.name(), "ZXV_E_CAP");
        assert!(e.detail().contains("different context"), "{}", e.detail());
    }
    assert!(ctx.enabled_currencies().unwrap().is_empty());
    assert!(
        other.enabled_currencies().unwrap().is_empty(),
        "nothing leaked into the cap's own context"
    );
}

#[test]
#[allow(deprecated)]
fn deprecated_no_cap_forms_still_work() {
    let ctx = Context::new().unwrap();
    ctx.enable_currency("NGN").unwrap();
    ctx.enable_currency_with_minor("GHS", 2).unwrap();
    ctx.register_private_currency("VFV", 2).unwrap();
    assert_eq!(ctx.enabled_currencies().unwrap(), ["NGN", "GHS", "VFV"]);
    let m = ctx.new_message(iso20022::MessageKind::Pacs008).unwrap();
    drop(m);
}
