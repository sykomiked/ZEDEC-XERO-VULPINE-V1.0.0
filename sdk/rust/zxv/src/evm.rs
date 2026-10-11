// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
//! EVM state-channel encoding of exact ledger balances.

use zxv_sys as sys;

use crate::capability::Capability;
use crate::error::{Error, Result};
use crate::ffi;
use crate::ledger::AccountId;

/// An Ethereum address (20 bytes).
pub type Address = [u8; 20];

/// An encoded channel state and its digest.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct EvmState {
    /// `abi.encode(bytes32 channelId, uint256 nonce, address[] parties,
    /// int256[] balances)`.
    pub encoding: Vec<u8>,
    /// `keccak256(encoding)`: what the parties sign (EIP-191) in their own
    /// wallets. ZXV has no secp256k1.
    pub digest: [u8; 32],
}

/// State-channel encoding for `sdk/solidity/ZxvStateChannel.sol`. Needs
/// [`Caps::EVM`](crate::Caps::EVM) (and `LEDGER_READ` for
/// [`ledger_state`](Self::ledger_state)).
#[derive(Debug, Clone, Copy)]
pub struct StateChannel<'a> {
    cap: &'a Capability<'a>,
}

impl<'a> StateChannel<'a> {
    /// The encoder through `cap`.
    pub fn new(cap: &'a Capability<'a>) -> StateChannel<'a> {
        StateChannel { cap }
    }

    /// Encodes explicit balances (one per party).
    pub fn encode_state(
        &self,
        channel_id: &[u8; 32],
        nonce: u64,
        parties: &[Address],
        balances: &[i64],
    ) -> Result<EvmState> {
        if parties.len() != balances.len() {
            return Err(Error::with_detail(sys::ZXV_E_ARG, "one balance per party"));
        }
        let count = Self::count(parties.len())?;
        let addrs = parties.concat();
        self.two_call(|buf, cap_bytes, len, dg| {
            self.cap.call(|c| unsafe {
                sys::zxv_evm_encode_state(
                    c,
                    channel_id.as_ptr(),
                    nonce,
                    addrs.as_ptr(),
                    balances.as_ptr(),
                    count,
                    buf,
                    cap_bytes,
                    len,
                    dg,
                )
            })
        })
    }

    /// Encodes each account's EQUITY (888) balance, all in `currency`.
    pub fn ledger_state(
        &self,
        channel_id: &[u8; 32],
        nonce: u64,
        currency: &str,
        accounts: &[AccountId],
        parties: &[Address],
    ) -> Result<EvmState> {
        if parties.len() != accounts.len() {
            return Err(Error::with_detail(sys::ZXV_E_ARG, "one account per party"));
        }
        let count = Self::count(parties.len())?;
        let addrs = parties.concat();
        let ids: Vec<u32> = accounts.iter().map(|a| a.0).collect();
        let ccy = ffi::cstr(currency)?;
        self.two_call(|buf, cap_bytes, len, dg| {
            self.cap.call(|c| unsafe {
                sys::zxv_evm_ledger_state(
                    c,
                    channel_id.as_ptr(),
                    nonce,
                    ccy.as_ptr(),
                    ids.as_ptr(),
                    addrs.as_ptr(),
                    count,
                    buf,
                    cap_bytes,
                    len,
                    dg,
                )
            })
        })
    }

    fn count(n: usize) -> Result<u32> {
        u32::try_from(n).map_err(|_| Error::with_detail(sys::ZXV_E_ARG, "too many parties"))
    }

    fn two_call(
        &self,
        mut f: impl FnMut(*mut u8, usize, *mut usize, *mut u8) -> Result<()>,
    ) -> Result<EvmState> {
        let mut buf = vec![0u8; 512];
        let mut digest = [0u8; 32];
        loop {
            let mut len = 0usize;
            match f(buf.as_mut_ptr(), buf.len(), &mut len, digest.as_mut_ptr()) {
                Ok(()) => {
                    buf.truncate(len);
                    return Ok(EvmState {
                        encoding: buf,
                        digest,
                    });
                }
                Err(e) if e.status() == sys::ZXV_E_BUFFER && len >= buf.len() => {
                    buf = vec![0u8; len + 1]
                }
                Err(e) => return Err(e),
            }
        }
    }
}
