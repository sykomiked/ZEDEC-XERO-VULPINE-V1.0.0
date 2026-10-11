// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
//! Shared test helpers: the conformance vectors and fixtures.
#![allow(dead_code)]

use std::path::PathBuf;

use serde_json::Value;

pub fn conformance_dir() -> PathBuf {
    PathBuf::from(env!("CARGO_MANIFEST_DIR")).join("../../conformance")
}

pub fn fixtures() -> PathBuf {
    conformance_dir().join("fixtures")
}

pub fn vectors() -> Value {
    let text =
        std::fs::read_to_string(conformance_dir().join("vectors.json")).expect("vectors.json");
    serde_json::from_str(&text).expect("valid vectors.json")
}

pub fn unhex(s: &Value) -> Vec<u8> {
    zxv::hex_decode(s.as_str().unwrap())
}

pub fn arr32(s: &Value) -> [u8; 32] {
    unhex(s).try_into().unwrap()
}

pub fn i64s(v: &Value) -> Vec<i64> {
    v.as_array()
        .unwrap()
        .iter()
        .map(|x| x.as_i64().unwrap())
        .collect()
}

pub fn rat(v: &Value) -> zxv::Rational {
    let p = i64s(v);
    zxv::Rational::new(p[0], p[1]).unwrap()
}
