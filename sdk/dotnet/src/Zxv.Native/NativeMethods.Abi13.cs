// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0

using System;
using System.Runtime.InteropServices;

namespace Zxv.Interop;

/// <summary>
/// The ABI 1.3 group of <c>zxv_api.h</c>: the 64-state hexagram tag (pure
/// functions over a <c>uint8_t</c> tag) and the quarantine log handle.
/// </summary>
internal static unsafe partial class NativeMethods
{
    [LibraryImport(Library, EntryPoint = "zxv_hex_pack")]
    internal static partial int HexPack(byte b, byte polarity, byte phase, out byte tag);

    [LibraryImport(Library, EntryPoint = "zxv_hex_unpack")]
    internal static partial int HexUnpack(byte tag, byte* b, byte* polarity, byte* phase);

    [LibraryImport(Library, EntryPoint = "zxv_hex_action")]
    internal static partial int HexAction(byte tag, out int action);

    [LibraryImport(Library, EntryPoint = "zxv_hex_phase_next")]
    internal static partial int HexPhaseNext(byte phase, out byte next);

    [LibraryImport(Library, EntryPoint = "zxv_hex_step")]
    internal static partial int HexStep(byte tag, byte phase, out byte result);

    [LibraryImport(Library, EntryPoint = "zxv_hex_set")]
    internal static partial int HexSet(byte tag, byte b, byte polarity, out byte result);

    [LibraryImport(Library, EntryPoint = "zxv_hex_forbid")]
    internal static partial int HexForbid(byte tag, out byte result);

    [LibraryImport(Library, EntryPoint = "zxv_hex_and")]
    internal static partial int HexAnd(byte a, byte b, out byte result);

    [LibraryImport(Library, EntryPoint = "zxv_hex_or")]
    internal static partial int HexOr(byte a, byte b, out byte result);

    [LibraryImport(Library, EntryPoint = "zxv_hex_not")]
    internal static partial int HexNot(byte a, out byte result);

    [LibraryImport(Library, EntryPoint = "zxv_hex_polarity")]
    internal static partial int HexPolarity(uint pos0, uint neg0, uint pos1, uint neg1, int stale, out byte polarity);

    [LibraryImport(Library, EntryPoint = "zxv_hex_from_lp")]
    internal static partial int HexFromLp(int lpState, out byte b);

    [LibraryImport(Library, EntryPoint = "zxv_hex_to_lp")]
    internal static partial int HexToLp(byte b, out int lpState);

    [LibraryImport(Library, EntryPoint = "zxv_hex_name")]
    internal static partial IntPtr HexName(byte tag);

    [LibraryImport(Library, EntryPoint = "zxv_hex_action_name")]
    internal static partial IntPtr HexActionName(int action);

    [LibraryImport(Library, EntryPoint = "zxv_hexlog_create")]
    internal static partial int HexlogCreate(ZxvCapabilityHandle cap, uint capacity, out ZxvQuarantineLogHandle log);

    [LibraryImport(Library, EntryPoint = "zxv_hexlog_destroy")]
    internal static partial void HexlogDestroy(IntPtr log);

    [LibraryImport(Library, EntryPoint = "zxv_hexlog_append")]
    internal static partial int HexlogAppend(ZxvCapabilityHandle cap, ZxvQuarantineLogHandle log, byte tag, byte reason, uint submitter, byte* subject, out ulong seq);

    [LibraryImport(Library, EntryPoint = "zxv_hexlog_count")]
    internal static partial int HexlogCount(ZxvQuarantineLogHandle log, out uint count);

    [LibraryImport(Library, EntryPoint = "zxv_hexlog_head")]
    internal static partial int HexlogHead(ZxvQuarantineLogHandle log, byte* head);

    [LibraryImport(Library, EntryPoint = "zxv_hexlog_at")]
    internal static partial int HexlogAt(ZxvQuarantineLogHandle log, uint index, out ulong seq, out byte tag, out byte reason, out uint submitter, byte* subject, byte* prev, byte* hash);

    [LibraryImport(Library, EntryPoint = "zxv_hexlog_verify")]
    internal static partial int HexlogVerify(ZxvQuarantineLogHandle log);
}
