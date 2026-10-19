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

/// Runs an "R4 for bytes" call (no NUL is added): first into a buffer of
/// `hint` bytes, then, on `ZXV_E_BUFFER`, into one of exactly the length the
/// library asked for.
pub(crate) fn read_bytes(
    hint: usize,
    mut f: impl FnMut(*mut u8, usize, *mut usize) -> i32,
) -> Result<Vec<u8>> {
    let mut buf: Vec<u8> = vec![0; hint.max(1)];
    for _ in 0..3 {
        let mut len = 0usize;
        let rc = f(buf.as_mut_ptr(), buf.len(), &mut len);
        if rc == sys::ZXV_OK {
            buf.truncate(len);
            return Ok(buf);
        }
        if rc == sys::ZXV_E_BUFFER && len > buf.len() {
            buf = vec![0; len];
            continue;
        }
        return Err(Error::from_status(rc));
    }
    Err(Error::with_detail(
        sys::ZXV_E_INTERNAL,
        "the library kept asking for a larger buffer",
    ))
}

/// A pointer to `b`, or NULL when it is empty (C takes NULL with length 0).
pub(crate) fn opt_ptr(b: &[u8]) -> *const u8 {
    if b.is_empty() {
        std::ptr::null()
    } else {
        b.as_ptr()
    }
}

/// Refuses a slice whose length is not `n` (`ZXV_E_ARG`).
pub(crate) fn need_len(b: &[u8], n: usize, what: &str) -> Result<()> {
    if b.len() != n {
        return Err(Error::with_detail(
            sys::ZXV_E_ARG,
            format!("{what} must be {n} bytes, got {}", b.len()),
        ));
    }
    Ok(())
}

/// Copies a NUL-terminated name out of a fixed C buffer.
pub(crate) fn c_name(buf: &[std::ffi::c_char]) -> String {
    let bytes: Vec<u8> = buf
        .iter()
        .take_while(|&&c| c != 0)
        .map(|&c| c as u8)
        .collect();
    String::from_utf8_lossy(&bytes).into_owned()
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

/// A call on a handle of `cap`'s context that takes no capability: guard
/// the context, run, map the status (poisoning on FATAL).
pub(crate) fn local(cap: &crate::Capability<'_>, f: impl FnOnce() -> i32) -> Result<()> {
    let ctx = cap.context();
    ctx.guard()?;
    ctx.check(f())
}

/// [`local`] for a body that maps its own statuses (an R4 read, say).
pub(crate) fn local_with<T>(
    cap: &crate::Capability<'_>,
    f: impl FnOnce() -> Result<T>,
) -> Result<T> {
    let ctx = cap.context();
    ctx.guard()?;
    f().map_err(|e| ctx.fatal(e))
}

/// Refuses two handles of different contexts used together (`ZXV_E_ARG`,
/// as the C groups do).
pub(crate) fn same_context(
    a: &crate::Capability<'_>,
    b: &crate::Capability<'_>,
    what: &str,
) -> Result<()> {
    if !std::ptr::eq(a.context(), b.context()) {
        return Err(Error::with_detail(
            sys::ZXV_E_ARG,
            format!("{what} belongs to a different context"),
        ));
    }
    Ok(())
}

/// Turns a non-NULL out handle into a `NonNull` (`ZXV_E_INTERNAL` for NULL).
pub(crate) fn non_null<T>(p: *mut T, what: &str) -> Result<std::ptr::NonNull<T>> {
    std::ptr::NonNull::new(p)
        .ok_or_else(|| Error::with_detail(sys::ZXV_E_INTERNAL, format!("NULL {what}")))
}
