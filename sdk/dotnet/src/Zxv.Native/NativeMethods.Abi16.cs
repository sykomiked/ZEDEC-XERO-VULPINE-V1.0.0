// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0

using System;
using System.Runtime.InteropServices;

namespace Zxv.Interop;

/// <summary>
/// The ABI 1.6 group of <c>zxv_api.h</c>: vino (Vino Embed): the play-lane
/// voucher sandbox, payment requests, wallet receipts and the daily meter.
/// </summary>
/// <remarks>
/// <c>uint32_t</c> is <see cref="uint"/>, <c>uint64_t</c> <see cref="ulong"/>,
/// <c>int32_t</c> <see cref="int"/>, <c>size_t</c> <see cref="nuint"/>; fixed
/// byte arrays (160-byte requests, 32-byte ids, 88-byte receipt messages,
/// ML-DSA-87 keys and signatures) are <c>byte*</c> to pinned memory. An
/// optional output pointer the SDK does not need is passed as
/// <see langword="null"/>.
/// </remarks>
internal static unsafe partial class NativeMethods
{
    // ===== vino: play lane =====
    [LibraryImport(Library, EntryPoint = "zxv_vino_play_create")]
    internal static partial int VinoPlayCreate(ZxvCapabilityHandle cap, out ZxvVinoPlayHandle play);

    [LibraryImport(Library, EntryPoint = "zxv_vino_play_destroy")]
    internal static partial void VinoPlayDestroy(IntPtr play);

    [LibraryImport(Library, EntryPoint = "zxv_vino_play_open")]
    internal static partial int VinoPlayOpen(ZxvCapabilityHandle cap, ZxvVinoPlayHandle play, uint owner, int isIssuer, out uint account);

    [LibraryImport(Library, EntryPoint = "zxv_vino_play_back")]
    internal static partial int VinoPlayBack(ZxvCapabilityHandle cap, ZxvVinoPlayHandle play, uint issuer, ulong amount);

    [LibraryImport(Library, EntryPoint = "zxv_vino_play_unback")]
    internal static partial int VinoPlayUnback(ZxvCapabilityHandle cap, ZxvVinoPlayHandle play, uint issuer, ulong amount);

    [LibraryImport(Library, EntryPoint = "zxv_vino_issue")]
    internal static partial int VinoIssue(ZxvCapabilityHandle cap, ZxvVinoPlayHandle play, uint issuer, uint holder, ulong face, int rail, out ulong id);

    [LibraryImport(Library, EntryPoint = "zxv_vino_transfer")]
    internal static partial int VinoTransfer(ZxvCapabilityHandle cap, ZxvVinoPlayHandle play, ulong voucher, uint from, uint to, int rail);

    [LibraryImport(Library, EntryPoint = "zxv_vino_set_rail")]
    internal static partial int VinoSetRail(ZxvCapabilityHandle cap, ZxvVinoPlayHandle play, ulong voucher, uint holder, int rail);

    [LibraryImport(Library, EntryPoint = "zxv_vino_redeem")]
    internal static partial int VinoRedeem(ZxvCapabilityHandle cap, ZxvVinoPlayHandle play, ulong voucher, uint holder);

    [LibraryImport(Library, EntryPoint = "zxv_vino_voucher_info")]
    internal static partial int VinoVoucherInfo(ZxvCapabilityHandle cap, ZxvVinoPlayHandle play, ulong voucher, out ulong face, out uint issuer, out uint holder, out int rail, out int state);

    [LibraryImport(Library, EntryPoint = "zxv_vino_account_info")]
    internal static partial int VinoAccountInfo(ZxvCapabilityHandle cap, ZxvVinoPlayHandle play, uint account, out uint owner, out int isIssuer, out ulong backing, out ulong faces, out uint count);

    [LibraryImport(Library, EntryPoint = "zxv_vino_vouchers")]
    internal static partial int VinoVouchers(ZxvCapabilityHandle cap, ZxvVinoPlayHandle play, uint account, ulong* ids, nuint capLen, out nuint count);

    [LibraryImport(Library, EntryPoint = "zxv_vino_play_check")]
    internal static partial int VinoPlayCheck(ZxvCapabilityHandle cap, ZxvVinoPlayHandle play);

    // ===== vino: payment requests (pure; fields as scalars, rule R2) =====
    [LibraryImport(Library, EntryPoint = "zxv_vino_request_encode")]
    internal static partial int VinoRequestEncode(int lane, uint flags, byte* merchant, byte* nonce, ulong amount, ulong createdAt, ulong expiresAt, byte* memo, byte* output, byte* outId);

    [LibraryImport(Library, EntryPoint = "zxv_vino_request_decode")]
    internal static partial int VinoRequestDecode(byte* input, out int lane, out uint flags, byte* merchant, byte* nonce, out ulong amount, out ulong createdAt, out ulong expiresAt, byte* memo, byte* outId);

    // ===== vino: receipts (pure, except signing) =====
    [LibraryImport(Library, EntryPoint = "zxv_vino_receipt_msg")]
    internal static partial int VinoReceiptMsg(byte* requestId, byte* walletPk, ulong approvedAt, byte* output);

    [LibraryImport(Library, EntryPoint = "zxv_vino_receipt_sign")]
    internal static partial int VinoReceiptSign(ZxvCapabilityHandle cap, byte* request, byte* walletSk, byte* walletPk, ulong approvedAt, byte* sig);

    [LibraryImport(Library, EntryPoint = "zxv_vino_receipt_verify")]
    internal static partial int VinoReceiptVerify(byte* request, byte* walletPk, ulong approvedAt, byte* sig, ulong now);

    // ===== vino: the daily meter =====
    [LibraryImport(Library, EntryPoint = "zxv_vino_meter_create")]
    internal static partial int VinoMeterCreate(ZxvCapabilityHandle cap, ulong dailyCap, out ZxvVinoMeterHandle meter);

    [LibraryImport(Library, EntryPoint = "zxv_vino_meter_destroy")]
    internal static partial void VinoMeterDestroy(IntPtr meter);

    [LibraryImport(Library, EntryPoint = "zxv_vino_meter_admit")]
    internal static partial int VinoMeterAdmit(ZxvCapabilityHandle cap, ZxvVinoMeterHandle meter, ulong day, ulong amount, out ulong left);
}
