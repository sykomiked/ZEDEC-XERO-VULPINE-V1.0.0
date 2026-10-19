// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0

using System;
using System.Runtime.InteropServices;

namespace Zxv.Interop;

/// <summary>
/// The ABI 1.4 group of <c>zxv_api.h</c>: hardware emulation (ZHP-1
/// profiles and the emulation matrix) and hardware updates (signed packages
/// and reconfigurable regions), 55 <c>zxv_hw_*</c> functions.
/// </summary>
/// <remarks>
/// <c>uint32_t</c> is <see cref="uint"/>, <c>size_t</c> is <see cref="nuint"/>,
/// fixed-size arrays (<c>uint8_t[32]</c>, keys) are <c>byte*</c> to pinned or
/// off-heap memory. Callbacks are unmanaged function pointers to
/// <see cref="UnmanagedCallersOnlyAttribute"/> methods whose <c>user</c>
/// argument is a <see cref="GCHandle"/> owned by the matrix or region handle.
/// </remarks>
internal static unsafe partial class NativeMethods
{
    // ----- Profiles (ZHP-1) -----
    [LibraryImport(Library, EntryPoint = "zxv_hw_profile_create")]
    internal static partial int HwProfileCreate(out ZxvHwProfileHandle profile);

    [LibraryImport(Library, EntryPoint = "zxv_hw_profile_destroy")]
    internal static partial void HwProfileDestroy(IntPtr profile);

    [LibraryImport(Library, EntryPoint = "zxv_hw_profile_probe_host")]
    internal static partial int HwProfileProbeHost(out ZxvHwProfileHandle profile);

    [LibraryImport(Library, EntryPoint = "zxv_hw_profile_reference")]
    internal static partial int HwProfileReference(out ZxvHwProfileHandle profile);

    [LibraryImport(Library, EntryPoint = "zxv_hw_profile_present")]
    internal static partial int HwProfilePresent(ZxvHwProfileHandle host, ZxvHwProfileHandle target, out ZxvHwProfileHandle profile);

    [LibraryImport(Library, EntryPoint = "zxv_hw_profile_decode")]
    internal static partial int HwProfileDecode(byte* buf, nuint len, out ZxvHwProfileHandle profile);

    [LibraryImport(Library, EntryPoint = "zxv_hw_profile_encode")]
    internal static partial int HwProfileEncode(ZxvHwProfileHandle profile, byte* buf, nuint cap, out nuint len);

    [LibraryImport(Library, EntryPoint = "zxv_hw_profile_digest")]
    internal static partial int HwProfileDigest(ZxvHwProfileHandle profile, byte* digest);

    [LibraryImport(Library, EntryPoint = "zxv_hw_profile_node_add")]
    internal static partial int HwProfileNodeAdd(ZxvHwProfileHandle profile, uint id, uint parent, uint cls);

    [LibraryImport(Library, EntryPoint = "zxv_hw_profile_cap_set")]
    internal static partial int HwProfileCapSet(ZxvHwProfileHandle profile, uint nodeId, uint key, ulong value);

    [LibraryImport(Library, EntryPoint = "zxv_hw_profile_cap_get")]
    internal static partial int HwProfileCapGet(ZxvHwProfileHandle profile, uint nodeId, uint key, out ulong value);

    [LibraryImport(Library, EntryPoint = "zxv_hw_profile_ext_set")]
    internal static partial int HwProfileExtSet(ZxvHwProfileHandle profile, uint nodeId, byte* ext, nuint len);

    [LibraryImport(Library, EntryPoint = "zxv_hw_profile_ext_get")]
    internal static partial int HwProfileExtGet(ZxvHwProfileHandle profile, uint nodeId, byte* buf, nuint cap, out nuint len);

    [LibraryImport(Library, EntryPoint = "zxv_hw_profile_count")]
    internal static partial int HwProfileCount(ZxvHwProfileHandle profile, out uint nodes);

    [LibraryImport(Library, EntryPoint = "zxv_hw_profile_node")]
    internal static partial int HwProfileNode(ZxvHwProfileHandle profile, uint index, out uint id, out uint parent, out uint cls, out uint ncaps);

    [LibraryImport(Library, EntryPoint = "zxv_hw_profile_match")]
    internal static partial int HwProfileMatch(ZxvHwProfileHandle host, ZxvHwProfileHandle target, out uint native, out uint emulated);

    [LibraryImport(Library, EntryPoint = "zxv_hw_profile_pair")]
    internal static partial int HwProfilePair(ZxvHwProfileHandle host, ZxvHwProfileHandle target, uint index, out uint needId, out int kind, out uint haveId, out uint unmetKey);

    // ----- The emulation matrix -----
    [LibraryImport(Library, EntryPoint = "zxv_hw_matrix_create_cap")]
    internal static partial int HwMatrixCreate(ZxvCapabilityHandle cap, out ZxvHwMatrixHandle matrix);

    [LibraryImport(Library, EntryPoint = "zxv_hw_matrix_destroy")]
    internal static partial void HwMatrixDestroy(IntPtr matrix);

    [LibraryImport(Library, EntryPoint = "zxv_hw_function_add", StringMarshalling = StringMarshalling.Utf8)]
    internal static partial int HwFunctionAdd(ZxvCapabilityHandle cap, ZxvHwMatrixHandle matrix, uint fnId, string? name);

    [LibraryImport(Library, EntryPoint = "zxv_hw_vector_add")]
    internal static partial int HwVectorAdd(ZxvCapabilityHandle cap, ZxvHwMatrixHandle matrix, uint fnId, uint op, byte* input, nuint inLen, byte* expect, nuint expectLen);

    [LibraryImport(Library, EntryPoint = "zxv_hw_function_contract")]
    internal static partial int HwFunctionContract(ZxvCapabilityHandle cap, ZxvHwMatrixHandle matrix, uint fnId, uint version);

    [LibraryImport(Library, EntryPoint = "zxv_hw_policy_set")]
    internal static partial int HwPolicySet(ZxvCapabilityHandle cap, ZxvHwMatrixHandle matrix, uint fnId, uint flags, uint maxLatencyNs, uint powerBudgetMw);

    [LibraryImport(Library, EntryPoint = "zxv_hw_function_info")]
    internal static partial int HwFunctionInfo(ZxvCapabilityHandle cap, ZxvHwMatrixHandle matrix, uint fnId, out uint contract, out uint policy, out uint vectors);

    [LibraryImport(Library, EntryPoint = "zxv_hw_resource_add")]
    internal static partial int HwResourceAdd(ZxvCapabilityHandle cap, ZxvHwMatrixHandle matrix, uint resId);

    [LibraryImport(Library, EntryPoint = "zxv_hw_resource_set")]
    internal static partial int HwResourceSet(ZxvCapabilityHandle cap, ZxvHwMatrixHandle matrix, uint resId, int state);

    [LibraryImport(Library, EntryPoint = "zxv_hw_provider_add")]
    internal static partial int HwProviderAdd(
        ZxvCapabilityHandle cap,
        ZxvHwMatrixHandle matrix,
        uint fnId,
        uint resId,
        int kind,
        uint cost,
        delegate* unmanaged[Cdecl]<void*, uint, byte*, nuint, byte*, nuint, nuint*, int> call,
        void* user,
        out uint provider);

    [LibraryImport(Library, EntryPoint = "zxv_hw_provider_traits")]
    internal static partial int HwProviderTraits(ZxvCapabilityHandle cap, ZxvHwMatrixHandle matrix, uint provider, uint latencyNs, uint powerMw, uint throughputClass);

    [LibraryImport(Library, EntryPoint = "zxv_hw_provider_dep")]
    internal static partial int HwProviderDep(ZxvCapabilityHandle cap, ZxvHwMatrixHandle matrix, uint provider, uint depFnId);

    [LibraryImport(Library, EntryPoint = "zxv_hw_provider_enable")]
    internal static partial int HwProviderEnable(ZxvCapabilityHandle cap, ZxvHwMatrixHandle matrix, uint provider, int on);

    [LibraryImport(Library, EntryPoint = "zxv_hw_attest")]
    internal static partial int HwAttest(ZxvCapabilityHandle cap, ZxvHwMatrixHandle matrix, uint provider, out int lp);

    [LibraryImport(Library, EntryPoint = "zxv_hw_provider_count")]
    internal static partial int HwProviderCount(ZxvCapabilityHandle cap, ZxvHwMatrixHandle matrix, out uint count);

    [LibraryImport(Library, EntryPoint = "zxv_hw_provider_info")]
    internal static partial int HwProviderInfo(
        ZxvCapabilityHandle cap,
        ZxvHwMatrixHandle matrix,
        uint provider,
        out uint fnId,
        out int kind,
        out int lp,
        out uint contract,
        out uint flags,
        out uint throughputClass,
        out uint powerClass,
        out ulong calls,
        out ulong costSpent);

    [LibraryImport(Library, EntryPoint = "zxv_hw_bound")]
    internal static partial int HwBound(ZxvCapabilityHandle cap, ZxvHwMatrixHandle matrix, uint fnId, out uint provider);

    [LibraryImport(Library, EntryPoint = "zxv_hw_call")]
    internal static partial int HwCall(ZxvCapabilityHandle cap, ZxvHwMatrixHandle matrix, uint fnId, uint op, byte* input, nuint inLen, byte* output, nuint outCap, out nuint outLen);

    [LibraryImport(Library, EntryPoint = "zxv_hw_shadow")]
    internal static partial int HwShadow(ZxvCapabilityHandle cap, ZxvHwMatrixHandle matrix, uint fnId, uint op, byte* input, nuint inLen, byte* output, nuint outCap, out nuint outLen, out int checkedCount);

    [LibraryImport(Library, EntryPoint = "zxv_hw_memo")]
    internal static partial int HwMemo(ZxvCapabilityHandle cap, ZxvHwMatrixHandle matrix, uint fnId, uint entries, out ulong hits, out ulong misses);

    // ----- Signed packages: keys, images, contracts -----
    [LibraryImport(Library, EntryPoint = "zxv_hw_keygen")]
    internal static partial int HwKeygen(ZxvCapabilityHandle cap, byte* pk, byte* mldsaSk, byte* slhdsaSk);

    [LibraryImport(Library, EntryPoint = "zxv_hw_image_build")]
    internal static partial int HwImageBuild(
        ZxvCapabilityHandle cap,
        uint kind,
        uint targetClass,
        byte* targetDigest,
        uint fnId,
        uint version,
        uint svn,
        byte* payload,
        nuint payloadLen,
        byte* mldsaSk,
        byte* slhdsaSk,
        byte* buf,
        nuint capBytes,
        out nuint len);

    [LibraryImport(Library, EntryPoint = "zxv_hw_matrix_trust_key")]
    internal static partial int HwMatrixTrustKey(ZxvCapabilityHandle cap, ZxvHwMatrixHandle matrix, byte* pk);

    [LibraryImport(Library, EntryPoint = "zxv_hw_contract_apply")]
    internal static partial int HwContractApply(ZxvCapabilityHandle cap, ZxvHwMatrixHandle matrix, byte* pkg, nuint len, out uint proven);

    // ----- Reconfigurable regions -----

    /// <remarks><paramref name="matrix"/> may be <see cref="IntPtr.Zero"/>; the caller keeps it alive.</remarks>
    [LibraryImport(Library, EntryPoint = "zxv_hw_region_create_cap")]
    internal static partial int HwRegionCreate(
        ZxvCapabilityHandle cap,
        IntPtr matrix,
        uint provider,
        uint regionId,
        uint targetClass,
        byte* targetDigest,
        delegate* unmanaged[Cdecl]<void*, uint, byte*, nuint, int> load,
        delegate* unmanaged[Cdecl]<void*, uint, byte*, int> readback,
        delegate* unmanaged[Cdecl]<void*, uint, int> activate,
        void* user,
        uint logCap,
        out ZxvHwRegionHandle region);

    [LibraryImport(Library, EntryPoint = "zxv_hw_region_destroy")]
    internal static partial void HwRegionDestroy(IntPtr region);

    [LibraryImport(Library, EntryPoint = "zxv_hw_region_events")]
    internal static partial int HwRegionEvents(ZxvCapabilityHandle cap, ZxvHwRegionHandle region, delegate* unmanaged[Cdecl]<void*, uint, int, uint, uint, void> fn, void* user);

    [LibraryImport(Library, EntryPoint = "zxv_hw_region_trust_key")]
    internal static partial int HwRegionTrustKey(ZxvCapabilityHandle cap, ZxvHwRegionHandle region, byte* pk);

    [LibraryImport(Library, EntryPoint = "zxv_hw_region_stage")]
    internal static partial int HwRegionStage(ZxvCapabilityHandle cap, ZxvHwRegionHandle region, byte* image, nuint len);

    [LibraryImport(Library, EntryPoint = "zxv_hw_region_trial")]
    internal static partial int HwRegionTrial(ZxvCapabilityHandle cap, ZxvHwRegionHandle region, uint ticks);

    [LibraryImport(Library, EntryPoint = "zxv_hw_region_commit")]
    internal static partial int HwRegionCommit(ZxvCapabilityHandle cap, ZxvHwRegionHandle region);

    [LibraryImport(Library, EntryPoint = "zxv_hw_region_rollback")]
    internal static partial int HwRegionRollback(ZxvCapabilityHandle cap, ZxvHwRegionHandle region);

    [LibraryImport(Library, EntryPoint = "zxv_hw_region_tick")]
    internal static partial int HwRegionTick(ZxvCapabilityHandle cap, ZxvHwRegionHandle region);

    [LibraryImport(Library, EntryPoint = "zxv_hw_region_info")]
    internal static partial int HwRegionInfo(ZxvCapabilityHandle cap, ZxvHwRegionHandle region, out int state, out uint version, out uint floor, out uint quarantined);

    // ----- Audit logs -----
    [LibraryImport(Library, EntryPoint = "zxv_hw_region_log")]
    internal static partial int HwRegionLog(ZxvCapabilityHandle cap, ZxvHwRegionHandle region, byte* head, out uint count);

    [LibraryImport(Library, EntryPoint = "zxv_hw_region_log_entry")]
    internal static partial int HwRegionLogEntry(ZxvCapabilityHandle cap, ZxvHwRegionHandle region, uint index, out int ev, out uint reason, out uint id, out uint version, out uint tag, byte* subject, byte* hash);

    [LibraryImport(Library, EntryPoint = "zxv_hw_matrix_log")]
    internal static partial int HwMatrixLog(ZxvCapabilityHandle cap, ZxvHwMatrixHandle matrix, byte* head, out uint count);

    [LibraryImport(Library, EntryPoint = "zxv_hw_matrix_log_entry")]
    internal static partial int HwMatrixLogEntry(ZxvCapabilityHandle cap, ZxvHwMatrixHandle matrix, uint index, out int ev, out uint reason, out uint id, out uint version, out uint tag, byte* subject, byte* hash);
}
