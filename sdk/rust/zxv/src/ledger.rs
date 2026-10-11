// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
//! The exact triple ledger.

use zxv_sys as sys;

use crate::capability::Capability;
#[cfg(doc)]
use crate::capability::Caps;
use crate::error::Result;
use crate::ffi;

/// An account id inside one context.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash, PartialOrd, Ord)]
#[repr(transparent)]
pub struct AccountId(pub u32);

/// The kind of an account.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash)]
pub enum AccountKind {
    /// May never go below zero (no debt).
    Holder,
    /// May carry a CREDIT (777) balance: issuance, nostro.
    Issuer,
}

impl AccountKind {
    fn raw(self) -> i32 {
        match self {
            AccountKind::Holder => sys::ZXV_ACCT_HOLDER as i32,
            AccountKind::Issuer => sys::ZXV_ACCT_ISSUER as i32,
        }
    }
}

/// Rail balances of an account, in exact minor units.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct Balance {
    /// DEBIT rail (555): sum received.
    pub debit: i64,
    /// CREDIT rail (777): sum sent.
    pub credit: i64,
    /// EQUITY rail (888): `debit - credit`, the spendable position.
    pub equity: i64,
}

/// Name, currency and kind of an account.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct AccountInfo {
    /// The name given at opening.
    pub name: String,
    /// Its currency code.
    pub currency: String,
    /// Holder or issuer.
    pub kind: AccountKind,
}

/// One journal line of an account.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct JournalLine {
    /// Platform entry id (shared by both sides of a posting).
    pub entry_id: u64,
    /// Signed amount: positive received, negative sent.
    pub amount: i64,
    /// The other account.
    pub counterparty: AccountId,
    /// The posting reference.
    pub reference: String,
}

/// Largest amount of one posting and largest `|balance|` (2^53 - 1).
pub const AMOUNT_MAX: i64 = sys::ZXV_AMOUNT_MAX;

/// The ledger of a context, used through a capability.
///
/// Postings and opening accounts need [`Caps::LEDGER_POST`]; balances,
/// account metadata and journals need [`Caps::LEDGER_READ`]. Every call
/// goes through the capability-gated (`*_cap`) form of the C ABI.
#[derive(Debug, Clone, Copy)]
pub struct Ledger<'a> {
    cap: &'a Capability<'a>,
}

impl<'a> Ledger<'a> {
    /// The ledger seen through `cap`.
    pub fn new(cap: &'a Capability<'a>) -> Ledger<'a> {
        Ledger { cap }
    }

    /// Opens an account in an enabled currency (`name` <= 63 bytes).
    pub fn open_account(&self, name: &str, currency: &str, kind: AccountKind) -> Result<AccountId> {
        let (n, c) = (ffi::cstr(name)?, ffi::cstr(currency)?);
        let mut id = 0u32;
        self.cap.call(|cap| unsafe {
            sys::zxv_ledger_open_account_cap(cap, n.as_ptr(), c.as_ptr(), kind.raw(), &mut id)
        })?;
        Ok(AccountId(id))
    }

    /// Posts a balanced transfer of `amount_minor` (> 0) and returns the
    /// entry id. `reference` (<= 35 bytes) must be unique in the context.
    /// The posting is atomic: fully validated before any balance changes.
    pub fn post(
        &self,
        from: AccountId,
        to: AccountId,
        amount_minor: i64,
        currency: &str,
        reference: &str,
    ) -> Result<u64> {
        let (c, r) = (ffi::cstr(currency)?, ffi::cstr(reference)?);
        let mut entry = 0u64;
        self.cap.call(|cap| unsafe {
            sys::zxv_ledger_post_cap(
                cap,
                from.0,
                to.0,
                amount_minor,
                c.as_ptr(),
                r.as_ptr(),
                &mut entry,
            )
        })?;
        Ok(entry)
    }

    /// The rail balances of an account.
    pub fn balance(&self, account: AccountId) -> Result<Balance> {
        let (mut d, mut c, mut e) = (0, 0, 0);
        self.cap.call(|cap| unsafe {
            sys::zxv_ledger_balance_cap(cap, account.0, &mut d, &mut c, &mut e)
        })?;
        Ok(Balance {
            debit: d,
            credit: c,
            equity: e,
        })
    }

    /// Verifies every ledger invariant. A failure is [`Error::Fatal`](crate::Error::Fatal):
    /// the context is halted.
    pub fn check(&self) -> Result<()> {
        self.cap
            .call(|cap| unsafe { sys::zxv_ledger_check_cap(cap) })
    }

    /// Name, currency and kind of an account.
    pub fn account(&self, account: AccountId) -> Result<AccountInfo> {
        let cap = self.cap.raw();
        let name = self
            .text(|b, c, l| unsafe { sys::zxv_ledger_account_name_cap(cap, account.0, b, c, l) })?;
        let currency = self
            .text(|b, c, l| unsafe { sys::zxv_ledger_account_ccy_cap(cap, account.0, b, c, l) })?;
        let mut k = 0i32;
        self.cap
            .call(|x| unsafe { sys::zxv_ledger_account_kind_cap(x, account.0, &mut k) })?;
        let kind = if k == sys::ZXV_ACCT_ISSUER as i32 {
            AccountKind::Issuer
        } else {
            AccountKind::Holder
        };
        Ok(AccountInfo {
            name,
            currency,
            kind,
        })
    }

    /// Number of accounts.
    pub fn account_count(&self) -> Result<u32> {
        let mut n = 0u32;
        self.cap
            .call(|x| unsafe { sys::zxv_ledger_account_count_cap(x, &mut n) })?;
        Ok(n)
    }

    /// The journal of an account, oldest first.
    pub fn journal(&self, account: AccountId) -> Result<Vec<JournalLine>> {
        let cap = self.cap.raw();
        let mut n = 0u32;
        self.cap
            .call(|x| unsafe { sys::zxv_ledger_entry_count_cap(x, account.0, &mut n) })?;
        let mut out = Vec::with_capacity(n as usize);
        for i in 0..n {
            let (mut id, mut amt, mut cp) = (0u64, 0i64, 0u32);
            let reference = self.text(|b, c, l| unsafe {
                sys::zxv_ledger_entry_at_cap(cap, account.0, i, &mut id, &mut amt, &mut cp, b, c, l)
            })?;
            out.push(JournalLine {
                entry_id: id,
                amount: amt,
                counterparty: AccountId(cp),
                reference,
            });
        }
        Ok(out)
    }

    fn text(
        &self,
        f: impl FnMut(*mut std::ffi::c_char, usize, *mut usize) -> i32,
    ) -> Result<String> {
        self.cap.context().guard()?;
        ffi::read_string(f).map_err(|e| self.cap.fatal(e))
    }
}
