// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0

using System;
using System.Runtime.InteropServices;

namespace Zxv.Interop;

/// <summary>
/// One-to-one P/Invoke declarations for <c>bindings/dotnet/native/zxv_api.h</c>.
/// Source-generated (<see cref="LibraryImportAttribute"/>): no runtime IL stubs,
/// AOT and trimming safe. Strings in are UTF-8; text out uses caller-owned
/// buffers with the two-call pattern (see <see cref="NativeText"/>).
/// </summary>
/// <remarks>
/// This is the only managed file that names native entry points. If the C ABI
/// grows, add the declaration here and a wrapper in Zxv.Sdk.
/// </remarks>
internal static unsafe partial class NativeMethods
{
    internal const string Library = "zxv";

    // ----- Library -----
    [LibraryImport(Library, EntryPoint = "zxv_version")]
    internal static partial int Version(out uint major, out uint minor, out uint patch);

    [LibraryImport(Library, EntryPoint = "zxv_features")]
    internal static partial int Features(out uint mask);

    [LibraryImport(Library, EntryPoint = "zxv_status_name")]
    internal static partial byte* StatusName(int status);

    [LibraryImport(Library, EntryPoint = "zxv_last_error")]
    internal static partial int LastError(byte* buf, nuint cap, out nuint len);

    // ----- Context -----
    [LibraryImport(Library, EntryPoint = "zxv_ctx_create")]
    internal static partial int CtxCreate(out ZxvContextHandle ctx);

    [LibraryImport(Library, EntryPoint = "zxv_ctx_destroy")]
    internal static partial void CtxDestroy(IntPtr ctx);

    // ----- ISO 4217 -----
    [LibraryImport(Library, EntryPoint = "zxv_iso4217_by_alpha", StringMarshalling = StringMarshalling.Utf8)]
    internal static partial int Iso4217ByAlpha(string alpha, out ushort numeric, out byte minorUnits, out uint flags);

    [LibraryImport(Library, EntryPoint = "zxv_iso4217_by_numeric")]
    internal static partial int Iso4217ByNumeric(ushort numeric, byte* buf, nuint cap, out nuint len, out byte minorUnits, out uint flags);

    [LibraryImport(Library, EntryPoint = "zxv_iso4217_name", StringMarshalling = StringMarshalling.Utf8)]
    internal static partial int Iso4217Name(string alpha, byte* buf, nuint cap, out nuint len);

    [LibraryImport(Library, EntryPoint = "zxv_iso4217_count")]
    internal static partial int Iso4217Count(out uint count);

    [LibraryImport(Library, EntryPoint = "zxv_iso4217_at")]
    internal static partial int Iso4217At(uint index, byte* buf, nuint cap, out nuint len);

    [LibraryImport(Library, EntryPoint = "zxv_iso4217_published")]
    internal static partial int Iso4217Published(byte* buf, nuint cap, out nuint len);

    [LibraryImport(Library, EntryPoint = "zxv_ccy_caveat")]
    internal static partial int CcyCaveat(ushort numeric, byte* buf, nuint cap, out nuint len);

    [LibraryImport(Library, EntryPoint = "zxv_au_is_member", StringMarshalling = StringMarshalling.Utf8)]
    internal static partial int AuIsMember(string countryAlpha2, out int isMember);

    [LibraryImport(Library, EntryPoint = "zxv_rail_numeric")]
    internal static partial int RailNumeric(int rail, out ushort numeric);

    // ----- Currency configuration -----
    [LibraryImport(Library, EntryPoint = "zxv_ccy_enable", StringMarshalling = StringMarshalling.Utf8)]
    internal static partial int CcyEnable(ZxvContextHandle ctx, string alpha);

    [LibraryImport(Library, EntryPoint = "zxv_ccy_enable_with_minor", StringMarshalling = StringMarshalling.Utf8)]
    internal static partial int CcyEnableWithMinor(ZxvContextHandle ctx, string alpha, byte minorUnits);

    [LibraryImport(Library, EntryPoint = "zxv_ccy_register_private", StringMarshalling = StringMarshalling.Utf8)]
    internal static partial int CcyRegisterPrivate(ZxvContextHandle ctx, string code, byte minorUnits);

    [LibraryImport(Library, EntryPoint = "zxv_ccy_get", StringMarshalling = StringMarshalling.Utf8)]
    internal static partial int CcyGet(ZxvContextHandle ctx, string code, out byte minorUnits, out uint flags);

    [LibraryImport(Library, EntryPoint = "zxv_ccy_enabled_count")]
    internal static partial int CcyEnabledCount(ZxvContextHandle ctx, out uint count);

    [LibraryImport(Library, EntryPoint = "zxv_ccy_enabled_at")]
    internal static partial int CcyEnabledAt(ZxvContextHandle ctx, uint index, byte* buf, nuint cap, out nuint len);

    // ----- Ledger -----
    [LibraryImport(Library, EntryPoint = "zxv_ledger_open_account", StringMarshalling = StringMarshalling.Utf8)]
    internal static partial int LedgerOpenAccount(ZxvContextHandle ctx, string name, string ccy, int kind, out uint accountId);

    [LibraryImport(Library, EntryPoint = "zxv_ledger_post", StringMarshalling = StringMarshalling.Utf8)]
    internal static partial int LedgerPost(ZxvContextHandle ctx, uint fromAccount, uint toAccount, long amountMinor, string ccy, string reference, out ulong entryId);

    [LibraryImport(Library, EntryPoint = "zxv_ledger_balance")]
    internal static partial int LedgerBalance(ZxvContextHandle ctx, uint account, out long debitMinor, out long creditMinor, out long equityMinor);

    [LibraryImport(Library, EntryPoint = "zxv_ledger_account_ccy")]
    internal static partial int LedgerAccountCcy(ZxvContextHandle ctx, uint account, byte* buf, nuint cap, out nuint len);

    [LibraryImport(Library, EntryPoint = "zxv_ledger_account_name")]
    internal static partial int LedgerAccountName(ZxvContextHandle ctx, uint account, byte* buf, nuint cap, out nuint len);

    [LibraryImport(Library, EntryPoint = "zxv_ledger_account_kind")]
    internal static partial int LedgerAccountKind(ZxvContextHandle ctx, uint account, out int kind);

    [LibraryImport(Library, EntryPoint = "zxv_ledger_account_count")]
    internal static partial int LedgerAccountCount(ZxvContextHandle ctx, out uint count);

    [LibraryImport(Library, EntryPoint = "zxv_ledger_entry_count")]
    internal static partial int LedgerEntryCount(ZxvContextHandle ctx, uint account, out uint count);

    [LibraryImport(Library, EntryPoint = "zxv_ledger_entry_at")]
    internal static partial int LedgerEntryAt(ZxvContextHandle ctx, uint account, uint index, out ulong entryId, out long signedMinor, out uint counterparty, byte* refBuf, nuint refCap, out nuint refLen);

    [LibraryImport(Library, EntryPoint = "zxv_ledger_check")]
    internal static partial int LedgerCheck(ZxvContextHandle ctx);

    // ----- ISO 20022 messages -----
    [LibraryImport(Library, EntryPoint = "zxv_msg_create")]
    internal static partial int MsgCreate(ZxvContextHandle ctx, int kind, out ZxvMessageHandle msg);

    [LibraryImport(Library, EntryPoint = "zxv_msg_destroy")]
    internal static partial void MsgDestroy(IntPtr msg);

    [LibraryImport(Library, EntryPoint = "zxv_msg_set_text", StringMarshalling = StringMarshalling.Utf8)]
    internal static partial int MsgSetText(ZxvMessageHandle msg, int field, string value);

    [LibraryImport(Library, EntryPoint = "zxv_msg_set_amount")]
    internal static partial int MsgSetAmount(ZxvMessageHandle msg, int field, long units, byte fracDigits);

    [LibraryImport(Library, EntryPoint = "zxv_msg_set_tx_count")]
    internal static partial int MsgSetTxCount(ZxvMessageHandle msg, uint count);

    [LibraryImport(Library, EntryPoint = "zxv_msg_add_entry")]
    internal static partial int MsgAddEntry(ZxvMessageHandle msg, long units, byte fracDigits, int cdtDbt, int status);

    [LibraryImport(Library, EntryPoint = "zxv_msg_render")]
    internal static partial int MsgRender(ZxvMessageHandle msg, byte* buf, nuint cap, out nuint len);

    // ----- Netting -----
    [LibraryImport(Library, EntryPoint = "zxv_netting_open", StringMarshalling = StringMarshalling.Utf8)]
    internal static partial int NettingOpen(ZxvContextHandle ctx, string cycleId, string ccy, out ZxvNettingHandle cycle);

    [LibraryImport(Library, EntryPoint = "zxv_netting_destroy")]
    internal static partial void NettingDestroy(IntPtr cycle);

    [LibraryImport(Library, EntryPoint = "zxv_netting_add_participant", StringMarshalling = StringMarshalling.Utf8)]
    internal static partial int NettingAddParticipant(ZxvNettingHandle cycle, string participant);

    [LibraryImport(Library, EntryPoint = "zxv_netting_submit", StringMarshalling = StringMarshalling.Utf8)]
    internal static partial int NettingSubmit(ZxvNettingHandle cycle, string paymentId, string debtor, string creditor, long amountMinor);

    [LibraryImport(Library, EntryPoint = "zxv_netting_close")]
    internal static partial int NettingClose(ZxvNettingHandle cycle);

    [LibraryImport(Library, EntryPoint = "zxv_netting_position_count")]
    internal static partial int NettingPositionCount(ZxvNettingHandle cycle, out uint count);

    [LibraryImport(Library, EntryPoint = "zxv_netting_position_at")]
    internal static partial int NettingPositionAt(ZxvNettingHandle cycle, uint index, byte* buf, nuint cap, out nuint len, out long netMinor);

    // ----- Cards -----
    [LibraryImport(Library, EntryPoint = "zxv_card_check_digit", StringMarshalling = StringMarshalling.Utf8)]
    internal static partial int CardCheckDigit(int network, string pan, out int valid);

    [LibraryImport(Library, EntryPoint = "zxv_card_issue")]
    internal static partial int CardIssue(ZxvContextHandle ctx, int network, uint account, long limitMinor, out ulong cardId, byte* maskedPan, nuint cap, out nuint len);

    [LibraryImport(Library, EntryPoint = "zxv_card_reissue")]
    internal static partial int CardReissue(ZxvContextHandle ctx, ulong cardId, out ulong newCardId);

    [LibraryImport(Library, EntryPoint = "zxv_card_authorize", StringMarshalling = StringMarshalling.Utf8)]
    internal static partial int CardAuthorize(ZxvContextHandle ctx, ulong cardId, long amountMinor, string ccy, out int response, byte* approval, nuint cap, out nuint len);

    // ----- VSS conformance -----
    [LibraryImport(Library, EntryPoint = "zxv_vss_run")]
    internal static partial int VssRun(ZxvContextHandle ctx, int check, out int passed, byte* detail, nuint cap, out nuint len);

    [LibraryImport(Library, EntryPoint = "zxv_vss_level_get")]
    internal static partial int VssLevelGet(ZxvContextHandle ctx, out int level);
}
