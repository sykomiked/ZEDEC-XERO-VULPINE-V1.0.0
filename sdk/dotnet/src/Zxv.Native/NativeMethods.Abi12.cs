// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0

using System.Runtime.InteropServices;

namespace Zxv.Interop;

/// <summary>
/// The ABI 1.2 <c>_cap</c> forms of <c>zxv_api.h</c>: the context surface
/// (currencies, accounts, journals, ISO 20022 messages) and the handle
/// creates, each through a capability the native library checks.
/// </summary>
internal static unsafe partial class NativeMethods
{
    // ----- LEDGER_POST -----
    [LibraryImport(Library, EntryPoint = "zxv_ccy_enable_cap", StringMarshalling = StringMarshalling.Utf8)]
    internal static partial int CcyEnableCap(ZxvCapabilityHandle cap, string alpha);

    [LibraryImport(Library, EntryPoint = "zxv_ccy_enable_with_minor_cap", StringMarshalling = StringMarshalling.Utf8)]
    internal static partial int CcyEnableWithMinorCap(ZxvCapabilityHandle cap, string alpha, byte minorUnits);

    [LibraryImport(Library, EntryPoint = "zxv_ccy_register_private_cap", StringMarshalling = StringMarshalling.Utf8)]
    internal static partial int CcyRegisterPrivateCap(ZxvCapabilityHandle cap, string code, byte minorUnits);

    [LibraryImport(Library, EntryPoint = "zxv_ledger_open_account_cap", StringMarshalling = StringMarshalling.Utf8)]
    internal static partial int LedgerOpenAccountCap(ZxvCapabilityHandle cap, string name, string ccy, int kind, out uint accountId);

    // ----- LEDGER_READ -----
    [LibraryImport(Library, EntryPoint = "zxv_ledger_account_ccy_cap")]
    internal static partial int LedgerAccountCcyCap(ZxvCapabilityHandle cap, uint account, byte* buf, nuint capLen, out nuint len);

    [LibraryImport(Library, EntryPoint = "zxv_ledger_account_name_cap")]
    internal static partial int LedgerAccountNameCap(ZxvCapabilityHandle cap, uint account, byte* buf, nuint capLen, out nuint len);

    [LibraryImport(Library, EntryPoint = "zxv_ledger_account_kind_cap")]
    internal static partial int LedgerAccountKindCap(ZxvCapabilityHandle cap, uint account, out int kind);

    [LibraryImport(Library, EntryPoint = "zxv_ledger_account_count_cap")]
    internal static partial int LedgerAccountCountCap(ZxvCapabilityHandle cap, out uint count);

    [LibraryImport(Library, EntryPoint = "zxv_ledger_entry_count_cap")]
    internal static partial int LedgerEntryCountCap(ZxvCapabilityHandle cap, uint account, out uint count);

    [LibraryImport(Library, EntryPoint = "zxv_ledger_entry_at_cap")]
    internal static partial int LedgerEntryAtCap(ZxvCapabilityHandle cap, uint account, uint index, out ulong entryId, out long signedMinor, out uint counterparty, byte* refBuf, nuint refCap, out nuint refLen);

    [LibraryImport(Library, EntryPoint = "zxv_msg_create_cap")]
    internal static partial int MsgCreateCap(ZxvCapabilityHandle cap, int kind, out ZxvMessageHandle msg);

    // ----- handle creates (BUDGET, PHASE, ATTEST, SWARM) -----
    [LibraryImport(Library, EntryPoint = "zxv_rmag_create_cap")]
    internal static partial int RmagCreateCap(ZxvCapabilityHandle cap, out ZxvBudgetsHandle rmag);

    [LibraryImport(Library, EntryPoint = "zxv_phase_create_cap")]
    internal static partial int PhaseCreateCap(ZxvCapabilityHandle cap, out ZxvPhaseHandle phase);

    [LibraryImport(Library, EntryPoint = "zxv_lpres_create_cap")]
    internal static partial int LpresCreateCap(ZxvCapabilityHandle cap, out ZxvAttestationsHandle reg);

    [LibraryImport(Library, EntryPoint = "zxv_swarm_create_cap")]
    internal static partial int SwarmCreateCap(ZxvCapabilityHandle cap, uint numLevels, ulong tokensPerCycle, out ZxvSwarmHandle swarm);
}
