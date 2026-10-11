// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
//! UBH-168 harmonic wire frames, packed in pure Rust (cross-checked against
//! libzxv in the tests).

use std::fmt;

use zxv_sys as sys;

use crate::error::{check, Error, Result};

/// Bytes in one frame.
pub const FRAME_BYTES: usize = 21;

/// What [`Ubh168Frame::resolve`] reports.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash)]
pub enum WireTruth {
    /// Unknown.
    Unknown,
    /// True.
    True,
    /// False.
    False,
    /// Neutral (e.g. the idle pilot).
    Neutral,
    /// Contradiction held.
    Glut,
    /// Persistent glut.
    Paradox,
}

impl WireTruth {
    fn from_raw(v: i32) -> WireTruth {
        match v {
            sys::ZXV_TRUTH_TRUE => WireTruth::True,
            sys::ZXV_TRUTH_FALSE => WireTruth::False,
            sys::ZXV_TRUTH_NEUTRAL => WireTruth::Neutral,
            sys::ZXV_TRUTH_GLUT => WireTruth::Glut,
            sys::ZXV_TRUTH_PARADOX => WireTruth::Paradox,
            _ => WireTruth::Unknown,
        }
    }

    /// The kernel number (0..=5).
    pub fn raw(self) -> i32 {
        match self {
            WireTruth::Unknown => 0,
            WireTruth::True => 1,
            WireTruth::False => 2,
            WireTruth::Neutral => 3,
            WireTruth::Glut => 4,
            WireTruth::Paradox => 5,
        }
    }
}

/// A 21-octet UBH-168 frame: a tag, then five 32-bit words in the fixed
/// byte order L-B-L-B-L (W0 little-endian S+, W1 big-endian S-, W2 LE S+,
/// W3 BE S-, W4 LE S+). The octets are identical on every host.
///
/// Packing and unpacking are pure Rust, so a service can frame without
/// the native library.
#[derive(Clone, Copy, PartialEq, Eq, Hash)]
pub struct Ubh168Frame([u8; FRAME_BYTES]);

impl Ubh168Frame {
    /// `tag = 100*sync + 10*shell + trunk` with trunk and shell 0..=9 and
    /// sync 0..=1.
    pub fn tag(trunk: u32, shell: u32, sync: u32) -> Result<u8> {
        if trunk > 9 || shell > 9 || sync > 1 {
            return Err(Error::with_detail(
                sys::ZXV_E_ARG,
                "trunk, shell 0..9 and sync 0..1",
            ));
        }
        Ok((100 * sync + 10 * shell + trunk) as u8)
    }

    /// Packs a tag, three S+ words and two S- words.
    pub fn pack(tag: u8, s_plus: [u32; 3], s_minus: [u32; 2]) -> Ubh168Frame {
        let mut f = [0u8; FRAME_BYTES];
        f[0] = tag;
        f[1..5].copy_from_slice(&s_plus[0].to_le_bytes());
        f[5..9].copy_from_slice(&s_minus[0].to_be_bytes());
        f[9..13].copy_from_slice(&s_plus[1].to_le_bytes());
        f[13..17].copy_from_slice(&s_minus[1].to_be_bytes());
        f[17..21].copy_from_slice(&s_plus[2].to_le_bytes());
        Ubh168Frame(f)
    }

    /// The tag and the words.
    pub fn unpack(&self) -> (u8, [u32; 3], [u32; 2]) {
        let w = |r: std::ops::Range<usize>| -> [u8; 4] { self.0[r].try_into().unwrap() };
        (
            self.0[0],
            [
                u32::from_le_bytes(w(1..5)),
                u32::from_le_bytes(w(9..13)),
                u32::from_le_bytes(w(17..21)),
            ],
            [u32::from_be_bytes(w(5..9)), u32::from_be_bytes(w(13..17))],
        )
    }

    /// A frame from its 21 octets.
    pub fn from_bytes(bytes: [u8; FRAME_BYTES]) -> Ubh168Frame {
        Ubh168Frame(bytes)
    }

    /// A frame from a slice of exactly 21 octets.
    pub fn from_slice(bytes: &[u8]) -> Result<Ubh168Frame> {
        let b: [u8; FRAME_BYTES] = bytes
            .try_into()
            .map_err(|_| Error::with_detail(sys::ZXV_E_FORMAT, "a UBH-168 frame is 21 octets"))?;
        Ok(Ubh168Frame(b))
    }

    /// The 21 octets.
    pub fn as_bytes(&self) -> &[u8; FRAME_BYTES] {
        &self.0
    }

    /// The same packing done by libzxv (for cross-checks).
    pub fn pack_native(tag: u8, s_plus: [u32; 3], s_minus: [u32; 2]) -> Result<Ubh168Frame> {
        let mut f = [0u8; FRAME_BYTES];
        // SAFETY: arrays of the sizes the ABI requires.
        check(unsafe {
            sys::zxv_wire_pack(tag, s_plus.as_ptr(), s_minus.as_ptr(), f.as_mut_ptr())
        })?;
        Ok(Ubh168Frame(f))
    }

    /// The idle pilot frame of a trunk (never all zeros), from libzxv.
    pub fn idle(trunk: u32) -> Result<Ubh168Frame> {
        let mut f = [0u8; FRAME_BYTES];
        // SAFETY: 21-byte out buffer.
        check(unsafe { sys::zxv_wire_idle(trunk, f.as_mut_ptr()) })?;
        Ok(Ubh168Frame(f))
    }

    /// The W5 correlation resolver (words read as signed Q16.16), from
    /// libzxv.
    pub fn resolve(&self) -> Result<WireTruth> {
        let mut t = 0;
        // SAFETY: 21-byte frame; out pointer valid.
        check(unsafe { sys::zxv_wire_resolve(self.0.as_ptr(), &mut t) })?;
        Ok(WireTruth::from_raw(t))
    }
}

impl fmt::Debug for Ubh168Frame {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(f, "Ubh168Frame({})", crate::ffi::hex(&self.0))
    }
}

impl AsRef<[u8]> for Ubh168Frame {
    fn as_ref(&self) -> &[u8] {
        &self.0
    }
}
