// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
//! Streams tokens from a GGUF model under a token budget.
//!
//! Run: `cargo run --example generate -- [model.gguf [tokenizer.gguf]]`
//! (defaults to the conformance fixtures).

use std::io::Write;
use std::path::PathBuf;

use zxv::{Budgets, Caps, Context, GenerateOptions, KvCache, Model, Rational, Resource};

fn main() -> zxv::Result<()> {
    let fixtures = PathBuf::from(env!("CARGO_MANIFEST_DIR")).join("../../conformance/fixtures");
    let mut args = std::env::args().skip(1);
    let model_path = args
        .next()
        .map(PathBuf::from)
        .unwrap_or_else(|| fixtures.join("tiny-qwen2.gguf"));
    let tok_path = args
        .next()
        .map(PathBuf::from)
        .or_else(|| Some(fixtures.join("tiny-tokenizer.gguf")));

    let ctx = Context::new()?;
    let cap = ctx.root_capability(1, None, Caps::AI_INFERENCE | Caps::BUDGET)?;
    let model = Model::open(&cap, &model_path, tok_path.as_deref())?;
    let budgets = Budgets::new(&cap)?;
    let tokens = budgets.create("tokens", Resource::Tokens, Rational::from(64), true)?;

    let mut session = model.new_session(&cap, 256, KvCache::Q16)?;
    session.bind_budget(tokens)?;
    let opts = GenerateOptions::greedy(32)
        .temperature(Rational::new(7, 10)?)
        .top_k(40)
        .top_p(Rational::new(95, 100)?)
        .seed(42);
    let (_, result) = session.stream("Hello, ZXV.", &opts, |stream| {
        for t in stream {
            print!("{}", String::from_utf8_lossy(&t.piece));
            let _ = std::io::stdout().flush();
        }
    })?;
    println!(
        "\n[{} tokens, stop: {:?}, budget used: {}]",
        result.tokens.len(),
        result.stop,
        tokens.status()?.used
    );
    Ok(())
}
