// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
//! ISO 20022 message builder (pacs.008, camt.053).

use std::marker::PhantomData;
use std::ptr::NonNull;

use zxv_sys as sys;

use crate::capability::Capability;
use crate::context::Context;
use crate::error::{Error, Result};
use crate::ffi;

/// The message kinds of the ABI. Only `Pacs008` and `Camt053` are built
/// into this library; the others return `NotImplemented`.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash)]
pub enum MessageKind {
    /// FIToFICstmrCdtTrf pacs.008.001.09.
    Pacs008,
    /// BkToCstmrStmt camt.053.001.08.
    Camt053,
    /// FIToFIPmtStsRpt (pending).
    Pacs002,
    /// PmtRtr (pending).
    Pacs004,
    /// FICdtTrf (pending).
    Pacs009,
    /// FIToFIPmtCxlReq (pending).
    Camt056,
    /// RsltnOfInvstgtn (pending).
    Camt029,
}

impl MessageKind {
    fn raw(self) -> i32 {
        (match self {
            MessageKind::Pacs008 => sys::ZXV_MX_PACS008,
            MessageKind::Camt053 => sys::ZXV_MX_CAMT053,
            MessageKind::Pacs002 => sys::ZXV_MX_PACS002,
            MessageKind::Pacs004 => sys::ZXV_MX_PACS004,
            MessageKind::Pacs009 => sys::ZXV_MX_PACS009,
            MessageKind::Camt056 => sys::ZXV_MX_CAMT056,
            MessageKind::Camt029 => sys::ZXV_MX_CAMT029,
        }) as i32
    }
}

/// Text fields; each kind accepts a subset (others are `ZXV_E_FIELD`).
#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash)]
pub enum TextField {
    /// GrpHdr/MsgId (<= 35).
    MsgId,
    /// GrpHdr/CreDtTm, ISO 8601 (<= 32).
    CreationDateTime,
    /// Dbtr/Nm (<= 70, pacs.008).
    DebtorName,
    /// DbtrAcct/Id (<= 35, pacs.008).
    DebtorAccount,
    /// Cdtr/Nm (<= 70, pacs.008).
    CreditorName,
    /// CdtrAcct/Id (<= 35, pacs.008).
    CreditorAccount,
    /// PmtId/EndToEndId (<= 35, pacs.008).
    EndToEndId,
    /// ISO 4217 alpha-3.
    Currency,
    /// Stmt/Acct/Id/Othr/Id (<= 35, camt.053).
    AccountId,
}

/// Amount fields (exact minor units plus decimal places).
#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash)]
pub enum AmountField {
    /// pacs.008 IntrBkSttlmAmt / InstdAmt.
    Settlement,
    /// camt.053 OPBD.
    Opening,
    /// camt.053 CLBD.
    Closing,
}

/// Credit or debit.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash)]
pub enum CreditDebit {
    /// CRDT.
    Credit,
    /// DBIT.
    Debit,
}

/// Statement entry status.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash)]
pub enum EntryStatus {
    /// BOOK.
    Booked,
    /// PDNG.
    Pending,
    /// INFO.
    Info,
}

/// One ISO 20022 message under construction.
///
/// `Send` but not `Sync` (R6: a message handle is used from one thread at
/// a time); it borrows its [`Context`] and the [`Capability`] it was made
/// with ([`Capability::new_message`]).
pub struct Message<'c> {
    raw: NonNull<sys::zxv_msg>,
    ctx: &'c Context,
    _cap: Option<&'c Capability<'c>>,
    _not_sync: PhantomData<std::cell::Cell<()>>,
}

// SAFETY: R6 allows a zxv_msg to move between threads; it is not Sync.
unsafe impl Send for Message<'_> {}

impl<'c> Message<'c> {
    pub(crate) fn new(ctx: &'c Context, kind: MessageKind) -> Result<Message<'c>> {
        let mut raw = std::ptr::null_mut();
        ctx.call(|x| unsafe { sys::zxv_msg_create(x, kind.raw(), &mut raw) })?;
        let raw = NonNull::new(raw)
            .ok_or_else(|| Error::with_detail(sys::ZXV_E_INTERNAL, "NULL message"))?;
        Ok(Message {
            raw,
            ctx,
            _cap: None,
            _not_sync: PhantomData,
        })
    }

    pub(crate) fn with_cap(cap: &'c Capability<'c>, kind: MessageKind) -> Result<Message<'c>> {
        let mut raw = std::ptr::null_mut();
        cap.call(|c| unsafe { sys::zxv_msg_create_cap(c, kind.raw(), &mut raw) })?;
        let raw = NonNull::new(raw)
            .ok_or_else(|| Error::with_detail(sys::ZXV_E_INTERNAL, "NULL message"))?;
        Ok(Message {
            raw,
            ctx: cap.context(),
            _cap: Some(cap),
            _not_sync: PhantomData,
        })
    }

    /// Sets a text field (plain text; the serializer escapes XML).
    pub fn set_text(&mut self, field: TextField, value: &str) -> Result<&mut Self> {
        let f = match field {
            TextField::MsgId => sys::ZXV_FLD_MSG_ID,
            TextField::CreationDateTime => sys::ZXV_FLD_CRE_DT_TM,
            TextField::DebtorName => sys::ZXV_FLD_DEBTOR_NAME,
            TextField::DebtorAccount => sys::ZXV_FLD_DEBTOR_ACCT,
            TextField::CreditorName => sys::ZXV_FLD_CREDITOR_NAME,
            TextField::CreditorAccount => sys::ZXV_FLD_CREDITOR_ACCT,
            TextField::EndToEndId => sys::ZXV_FLD_END_TO_END_ID,
            TextField::Currency => sys::ZXV_FLD_CCY,
            TextField::AccountId => sys::ZXV_FLD_ACCT_ID,
        } as i32;
        let v = ffi::cstr(value)?;
        self.call(|m| unsafe { sys::zxv_msg_set_text(m, f, v.as_ptr()) })?;
        Ok(self)
    }

    /// Sets an amount as exact minor `units` with `frac_digits` decimals
    /// (must equal the currency's minor units).
    pub fn set_amount(
        &mut self,
        field: AmountField,
        units: i64,
        frac_digits: u8,
    ) -> Result<&mut Self> {
        let f = match field {
            AmountField::Settlement => sys::ZXV_AMT_SETTLEMENT,
            AmountField::Opening => sys::ZXV_AMT_OPENING,
            AmountField::Closing => sys::ZXV_AMT_CLOSING,
        } as i32;
        self.call(|m| unsafe { sys::zxv_msg_set_amount(m, f, units, frac_digits) })?;
        Ok(self)
    }

    /// Number of transactions (pacs.008 GrpHdr/NbOfTxs; default 1).
    pub fn set_tx_count(&mut self, n: u32) -> Result<&mut Self> {
        self.call(|m| unsafe { sys::zxv_msg_set_tx_count(m, n) })?;
        Ok(self)
    }

    /// Appends a camt.053 statement entry.
    pub fn add_entry(
        &mut self,
        units: i64,
        frac_digits: u8,
        direction: CreditDebit,
        status: EntryStatus,
    ) -> Result<&mut Self> {
        let d = match direction {
            CreditDebit::Credit => sys::ZXV_CRDT,
            CreditDebit::Debit => sys::ZXV_DBIT,
        } as i32;
        let s = match status {
            EntryStatus::Booked => sys::ZXV_STS_BOOK,
            EntryStatus::Pending => sys::ZXV_STS_PDNG,
            EntryStatus::Info => sys::ZXV_STS_INFO,
        } as i32;
        self.call(|m| unsafe { sys::zxv_msg_add_entry(m, units, frac_digits, d, s) })?;
        Ok(self)
    }

    /// Serializes to XML, validating mandatory fields and the currency.
    pub fn render(&self) -> Result<String> {
        self.ctx.guard()?;
        let m = self.raw.as_ptr();
        ffi::read_string(|b, c, l| unsafe { sys::zxv_msg_render(m, b, c, l) })
            .map_err(|e| self.ctx.fatal(e))
    }

    fn call(&self, f: impl FnOnce(*mut sys::zxv_msg) -> i32) -> Result<()> {
        self.ctx.guard()?;
        self.ctx.check(f(self.raw.as_ptr()))
    }
}

impl Drop for Message<'_> {
    fn drop(&mut self) {
        // SAFETY: owned handle, destroyed once, before its context.
        unsafe { sys::zxv_msg_destroy(self.raw.as_ptr()) }
    }
}
