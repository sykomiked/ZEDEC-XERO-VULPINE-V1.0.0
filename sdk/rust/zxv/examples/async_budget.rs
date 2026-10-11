// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
//! Async budgets and phase ticks on tokio (any executor works).
//!
//! Run: `cargo run --example async_budget`

use std::time::Duration;

use zxv::{Budgets, Caps, Context, PhaseClock, Rational, Resource, PHASE_CYCLE};

#[tokio::main(flavor = "current_thread")]
async fn main() -> zxv::Result<()> {
    let ctx = Context::new()?;
    let cap = ctx.root_capability(1, None, Caps::BUDGET | Caps::PHASE)?;

    let budgets = Budgets::new(&cap)?;
    budgets.on_exhausted(|id| println!("budget {id} exhausted; waiting"));
    let compute = budgets.create("compute", Resource::ComputeCycles, Rational::from(10), true)?;
    compute.consume(Rational::from(10))?;

    let worker = async {
        compute.consume_async(Rational::from(4)).await?;
        println!("worker got 4 cycles");
        Ok::<(), zxv::Error>(())
    };
    let releaser = async {
        tokio::time::sleep(Duration::from_millis(20)).await;
        compute.release(Rational::from(5))?;
        println!("released 5 cycles");
        Ok::<(), zxv::Error>(())
    };
    let (a, b) = tokio::join!(worker, releaser);
    a?;
    b?;

    let clock = PhaseClock::new(&cap)?;
    let event = clock.defer(PHASE_CYCLE)?;
    let advancer = async {
        for _ in 0..4 {
            tokio::time::sleep(Duration::from_millis(5)).await;
            let step = clock.advance(PHASE_CYCLE / 4)?;
            println!("tick {} ({} cleared)", step.now, step.cleared);
        }
        Ok::<(), zxv::Error>(())
    };
    let (state, adv) = tokio::join!(event.cleared(), advancer);
    adv?;
    println!("deferred event: {:?} at tick {}", state?, clock.now()?);
    Ok(())
}
