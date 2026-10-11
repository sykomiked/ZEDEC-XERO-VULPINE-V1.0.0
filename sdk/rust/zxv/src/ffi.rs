// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
//! Small helpers around the C conventions (R4 two-call buffers, C strings).

use std::ffi::{c_char, CString};

use zxv_sys as sys;

use crate::error::{Error, Result};

/// Runs an R4 two-call text function and returns the text.
///
/// `f(buf, cap, out_len)` must follow R4: `ZXV_E_BUFFER` with the needed
/// length when the buffer is too small.
pub(crate) fn read_string(
    mut f: impl FnMut(*mut c_char, usize, *mut usize) -> i32,
) -> Result<String> {
    let mut buf: Vec<u8> = vec![0; 64];
    loop {
        let mut len = 0usize;
        let rc = f(buf.as_mut_ptr().cast(), buf.len(), &mut len);
        if rc == sys::ZXV_OK {
            buf.truncate(len);
            return String::from_utf8(buf).map_err(|_| {
                Error::with_detail(sys::ZXV_E_UTF8, "library returned invalid UTF-8")
            });
        }
        if rc == sys::ZXV_E_BUFFER && len + 1 > buf.len() {
            buf = vec![0; len + 1];
            continue;
        }
        return Err(Error::from_status(rc));
    }
}

/// Converts a Rust string to a C string, refusing interior NUL bytes.
pub(crate) fn cstr(s: &str) -> Result<CString> {
    CString::new(s).map_err(|_| Error::with_detail(sys::ZXV_E_ARG, "string contains a NUL byte"))
}

/// Parses a hex string (test and example helper; not public API).
#[doc(hidden)]
pub fn hex_decode(s: &str) -> Vec<u8> {
    (0..s.len())
        .step_by(2)
        .map(|i| u8::from_str_radix(&s[i..i + 2], 16).unwrap())
        .collect()
}

/// Lower-case hex of bytes.
pub fn hex(bytes: &[u8]) -> String {
    use std::fmt::Write;
    bytes
        .iter()
        .fold(String::with_capacity(bytes.len() * 2), |mut s, b| {
            let _ = write!(s, "{b:02x}");
            s
        })
}
