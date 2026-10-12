// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0

using System;
using System.Collections.Generic;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using Zxv.Interop;

namespace Zxv.Hardware;

/// <summary>
/// The emulation matrix of ABI 1.4: functions (engine contracts) served by
/// interchangeable implementations, HARD silicon, a FABRIC or MICRO region,
/// a PEER component, a SOFT emulator or a REMOTE machine. An implementation
/// serves only after it reproduces the function's golden vectors bit for bit
/// (<see cref="Attest"/>); the cheapest proven one serves (<see cref="Bound"/>),
/// and a fault moves the function to the next.
/// </summary>
/// <remarks>
/// <para>
/// Every matrix starts with the <see cref="HostResource"/> and the standard
/// engines (<see cref="HwFunctions"/>), each served by a proven SOFT
/// implementation, so <see cref="Call(uint, uint, ReadOnlySpan{byte}, int)"/>
/// works on any device. Apps state placement constraints
/// (<see cref="SetPolicy"/>), never an implementation; key-bearing work is
/// always local. Create one with <see cref="ZxvPlatform.CreateHwMatrix"/>;
/// every call needs <see cref="CapabilityRights.HwMatrix"/>, trusting a key and
/// applying contract packages need <see cref="CapabilityRights.HwReconf"/>.
/// </para>
/// <para>
/// Thread-safe (libzxv serializes calls). A <see cref="HwImplementation"/>
/// runs on the calling thread inside the native call, is kept alive until
/// the matrix is released, and may call this matrix again for functions it
/// declared (<see cref="AddDependency"/>). Its exceptions never unwind into
/// native code: a <see cref="ZxvException"/> becomes its status, anything
/// else is rethrown from the call that ran it.
/// </para>
/// </remarks>
public sealed class HwMatrix : IDisposable
{
    /// <summary>The resource every matrix starts with: this machine's CPU (<c>ZXV_HW_RES_HOST</c>).</summary>
    public const uint HostResource = 0xFFFF_FFF0;

    /// <summary>The default output room of <see cref="Call(uint, uint, ReadOnlySpan{byte}, int)"/> (the largest golden-vector output).</summary>
    public const int DefaultOutputCapacity = 512;

    private readonly Capability _cap;

    internal HwMatrix(Capability capability)
    {
        _cap = capability;
        capability.Enter("HwMatrix.Create");
        int rc = NativeMethods.HwMatrixCreate(capability.Handle, out ZxvHwMatrixHandle handle);
        if (rc != (int)ZxvStatus.Ok)
        {
            handle.Dispose();
            capability.Check(rc, "HwMatrix.Create");
        }

        handle.AddDependency(capability.Context.Handle);
        Handle = handle;
    }

    internal ZxvHwMatrixHandle Handle { get; }

    internal Capability Capability => _cap;

    /// <summary>Adds a function.</summary>
    /// <param name="functionId">Its id.</param>
    /// <param name="name">UTF-8, at most 23 bytes; may be <see langword="null"/>.</param>
    public void AddFunction(uint functionId, string? name)
    {
        if (name is not null)
        {
            Guard.Text(name, nameof(name), allowEmpty: true);
        }

        Enter("HwMatrix.AddFunction");
        Done(NativeMethods.HwFunctionAdd(_cap.Handle, Handle, functionId, name), "HwMatrix.AddFunction");
    }

    /// <summary>Adds a golden vector to the function's current contract; every implementation must prove itself again.</summary>
    /// <param name="functionId">The function.</param>
    /// <param name="vector">The vector.</param>
    public unsafe void AddVector(uint functionId, HwVector vector)
    {
        ArgumentNullException.ThrowIfNull(vector);
        Enter("HwMatrix.AddVector");
        fixed (byte* i = vector.Input.Span)
        fixed (byte* e = vector.Expect.Span)
        {
            Done(NativeMethods.HwVectorAdd(_cap.Handle, Handle, functionId, vector.Op, i, (nuint)vector.Input.Length, e, (nuint)vector.Expect.Length), "HwMatrix.AddVector");
        }
    }

    /// <summary>Starts contract <paramref name="version"/>: the vectors are cleared and every implementation must prove itself again.</summary>
    /// <param name="functionId">The function.</param>
    /// <param name="version">Strictly newer than the current one.</param>
    /// <exception cref="ZxvRefusedException">Not newer (<see cref="ZxvStatus.Rollback"/>).</exception>
    public void StartContract(uint functionId, uint version)
    {
        Enter("HwMatrix.StartContract");
        Done(NativeMethods.HwFunctionContract(_cap.Handle, Handle, functionId, version), "HwMatrix.StartContract");
    }

    /// <summary>Sets placement: only implementations that declared values within these limits qualify.</summary>
    /// <param name="functionId">The function.</param>
    /// <param name="flags">Policy flags.</param>
    /// <param name="maxLatencyNs">A latency ceiling (0: none).</param>
    /// <param name="powerBudgetMilliwatts">A power budget (0: none).</param>
    /// <exception cref="ZxvRefusedException">Clearing <see cref="HwPolicy.KeyBearing"/> once set (<see cref="ZxvStatus.PolicyRefused"/>).</exception>
    public void SetPolicy(uint functionId, HwPolicy flags, uint maxLatencyNs = 0, uint powerBudgetMilliwatts = 0)
    {
        Enter("HwMatrix.SetPolicy");
        Done(NativeMethods.HwPolicySet(_cap.Handle, Handle, functionId, (uint)flags, maxLatencyNs, powerBudgetMilliwatts), "HwMatrix.SetPolicy");
    }

    /// <summary>Reads a function's contract version, policy and vector count.</summary>
    /// <param name="functionId">The function.</param>
    /// <returns>The info.</returns>
    public HwFunctionInfo GetFunctionInfo(uint functionId)
    {
        Enter("HwMatrix.GetFunctionInfo");
        Done(NativeMethods.HwFunctionInfo(_cap.Handle, Handle, functionId, out uint contract, out uint policy, out uint vectors), "HwMatrix.GetFunctionInfo");
        return new HwFunctionInfo(contract, (HwPolicy)policy, vectors);
    }

    /// <summary>Adds a resource (a component implementations run on).</summary>
    /// <param name="resourceId">Its id.</param>
    public void AddResource(uint resourceId)
    {
        Enter("HwMatrix.AddResource");
        Done(NativeMethods.HwResourceAdd(_cap.Handle, Handle, resourceId), "HwMatrix.AddResource");
    }

    /// <summary>Sets a resource's state; a fault moves its functions to other implementations.</summary>
    /// <param name="resourceId">The resource.</param>
    /// <param name="state">The new state.</param>
    public void SetResourceState(uint resourceId, HwResourceState state)
    {
        Enter("HwMatrix.SetResourceState");
        Done(NativeMethods.HwResourceSet(_cap.Handle, Handle, resourceId, (int)state), "HwMatrix.SetResourceState");
    }

    /// <summary>
    /// Adds an implementation. It does not serve until <see cref="Attest"/>
    /// proves it; the delegate is kept alive until the matrix is released.
    /// </summary>
    /// <param name="functionId">The function it implements.</param>
    /// <param name="resourceId">The resource it runs on.</param>
    /// <param name="kind">How it is built.</param>
    /// <param name="cost">Its cost; the cheapest proven implementation serves.</param>
    /// <param name="implementation">The implementation.</param>
    /// <returns>Its provider index.</returns>
    public unsafe uint AddProvider(uint functionId, uint resourceId, HwKind kind, uint cost, HwImplementation implementation)
    {
        ArgumentNullException.ThrowIfNull(implementation);
        Enter("HwMatrix.AddProvider");
        IntPtr user = Handle.Pin(implementation);
        Done(NativeMethods.HwProviderAdd(_cap.Handle, Handle, functionId, resourceId, (int)kind, cost, &OnCall, (void*)user, out uint provider), "HwMatrix.AddProvider");
        return provider;
    }

    /// <summary>Declares traits used by placement and discovery.</summary>
    /// <param name="provider">The implementation.</param>
    /// <param name="latencyNs">Typical latency per call.</param>
    /// <param name="powerMilliwatts">Power while serving.</param>
    /// <param name="throughputClass">0 (slowest) .. 15.</param>
    public void SetTraits(uint provider, uint latencyNs, uint powerMilliwatts, uint throughputClass)
    {
        Enter("HwMatrix.SetTraits");
        Done(NativeMethods.HwProviderTraits(_cap.Handle, Handle, provider, latencyNs, powerMilliwatts, throughputClass), "HwMatrix.SetTraits");
    }

    /// <summary>Declares that an implementation calls another function.</summary>
    /// <param name="provider">The implementation.</param>
    /// <param name="dependencyFunctionId">The function it calls.</param>
    public void AddDependency(uint provider, uint dependencyFunctionId)
    {
        Enter("HwMatrix.AddDependency");
        Done(NativeMethods.HwProviderDep(_cap.Handle, Handle, provider, dependencyFunctionId), "HwMatrix.AddDependency");
    }

    /// <summary>Enables or disables an implementation.</summary>
    /// <param name="provider">The implementation.</param>
    /// <param name="enabled">Whether it may serve.</param>
    public void SetEnabled(uint provider, bool enabled)
    {
        Enter("HwMatrix.SetEnabled");
        Done(NativeMethods.HwProviderEnable(_cap.Handle, Handle, provider, enabled ? 1 : 0), "HwMatrix.SetEnabled");
    }

    /// <summary>Runs the golden vectors against an implementation.</summary>
    /// <param name="provider">The implementation.</param>
    /// <returns><see cref="Truth.True"/> (serves), <see cref="Truth.False"/> (a vector failed) or <see cref="Truth.Neither"/> (no vectors).</returns>
    public Truth Attest(uint provider)
    {
        Enter("HwMatrix.Attest");
        Done(NativeMethods.HwAttest(_cap.Handle, Handle, provider, out int lp), "HwMatrix.Attest");
        return (Truth)lp;
    }

    /// <summary>Gets the number of implementations (indexed 0 .. count - 1).</summary>
    /// <returns>The count.</returns>
    public int GetProviderCount()
    {
        Enter("HwMatrix.GetProviderCount");
        Done(NativeMethods.HwProviderCount(_cap.Handle, Handle, out uint n), "HwMatrix.GetProviderCount");
        return checked((int)n);
    }

    /// <summary>Discovery of one implementation.</summary>
    /// <param name="provider">Its index.</param>
    /// <returns>What it is and how it is doing.</returns>
    public HwProviderInfo GetProviderInfo(uint provider)
    {
        Enter("HwMatrix.GetProviderInfo");
        Done(
            NativeMethods.HwProviderInfo(_cap.Handle, Handle, provider, out uint fn, out int kind, out int lp, out uint contract, out uint flags, out uint tput, out uint power, out ulong calls, out ulong cost),
            "HwMatrix.GetProviderInfo");
        return new HwProviderInfo(fn, (HwKind)kind, (Truth)lp, contract, (HwProviderStatus)flags, tput, (HwPowerClass)power, calls, cost);
    }

    /// <summary>Discovery of every implementation, by index.</summary>
    /// <returns>The list.</returns>
    public IReadOnlyList<HwProviderInfo> GetProviders()
    {
        int n = GetProviderCount();
        var list = new HwProviderInfo[n];
        for (int i = 0; i < n; i++)
        {
            list[i] = GetProviderInfo((uint)i);
        }

        return list;
    }

    /// <summary>The implementation serving the function now.</summary>
    /// <param name="functionId">The function.</param>
    /// <returns>Its provider index.</returns>
    /// <exception cref="ZxvRefusedException">None may serve (<see cref="ZxvStatus.NoProvider"/>).</exception>
    public uint Bound(uint functionId)
    {
        Enter("HwMatrix.Bound");
        Done(NativeMethods.HwBound(_cap.Handle, Handle, functionId, out uint provider), "HwMatrix.Bound");
        return provider;
    }

    /// <summary>Runs operation <paramref name="op"/> of a function on the implementation placement picks.</summary>
    /// <param name="functionId">The function.</param>
    /// <param name="op">The operation.</param>
    /// <param name="input">The input.</param>
    /// <param name="maxOutput">Room for the output.</param>
    /// <returns>The output.</returns>
    /// <exception cref="ZxvRefusedException">Nothing may serve (<see cref="ZxvStatus.NoProvider"/>, <see cref="ZxvStatus.PolicyRefused"/>).</exception>
    public unsafe byte[] Call(uint functionId, uint op, ReadOnlySpan<byte> input, int maxOutput = DefaultOutputCapacity)
    {
        ArgumentOutOfRangeException.ThrowIfNegative(maxOutput);
        var output = new byte[maxOutput];
        Enter("HwMatrix.Call");
        nuint n;
        fixed (byte* i = input)
        fixed (byte* o = output)
        {
            Done(NativeMethods.HwCall(_cap.Handle, Handle, functionId, op, i, (nuint)input.Length, o, (nuint)output.Length, out n), "HwMatrix.Call");
        }

        return output.AsSpan(0, checked((int)n)).ToArray();
    }

    /// <summary>Runs a call into caller memory (no allocation).</summary>
    /// <param name="functionId">The function.</param>
    /// <param name="op">The operation.</param>
    /// <param name="input">The input.</param>
    /// <param name="output">Room for the output.</param>
    /// <returns>The output length.</returns>
    public unsafe int Call(uint functionId, uint op, ReadOnlySpan<byte> input, Span<byte> output)
    {
        Enter("HwMatrix.Call");
        nuint n;
        fixed (byte* i = input)
        fixed (byte* o = output)
        {
            Done(NativeMethods.HwCall(_cap.Handle, Handle, functionId, op, i, (nuint)input.Length, o, (nuint)output.Length, out n), "HwMatrix.Call");
        }

        return checked((int)n);
    }

    /// <summary>A call that also runs the next-best implementation and compares.</summary>
    /// <param name="functionId">The function.</param>
    /// <param name="op">The operation.</param>
    /// <param name="input">The input.</param>
    /// <returns>The output and whether a second implementation agreed.</returns>
    /// <exception cref="ZxvRefusedException">They disagree (<see cref="ZxvStatus.Dispute"/>); both stop serving.</exception>
    public unsafe HwShadowResult Shadow(uint functionId, uint op, ReadOnlySpan<byte> input)
    {
        var output = new byte[DefaultOutputCapacity];
        Enter("HwMatrix.Shadow");
        nuint n;
        int isChecked;
        fixed (byte* i = input)
        fixed (byte* o = output)
        {
            Done(NativeMethods.HwShadow(_cap.Handle, Handle, functionId, op, i, (nuint)input.Length, o, (nuint)output.Length, out n, out isChecked), "HwMatrix.Shadow");
        }

        return new HwShadowResult(output.AsSpan(0, checked((int)n)).ToArray(), isChecked != 0);
    }

    /// <summary>Declares the function pure and gives it a memo (a power of two up to 65536 entries; 0 removes it).</summary>
    /// <param name="functionId">The function.</param>
    /// <param name="entries">The memo size.</param>
    /// <returns>The counters.</returns>
    /// <exception cref="ZxvRefusedException">A key-bearing function (<see cref="ZxvStatus.PolicyRefused"/>).</exception>
    public HwMemoStats Memo(uint functionId, uint entries)
    {
        Enter("HwMatrix.Memo");
        Done(NativeMethods.HwMemo(_cap.Handle, Handle, functionId, entries, out ulong hits, out ulong misses), "HwMatrix.Memo");
        return new HwMemoStats(hits, misses);
    }

    /// <summary>Trusts a release key for contract packages (copied; at most 4). Needs <see cref="CapabilityRights.HwReconf"/>.</summary>
    /// <param name="publicKey"><see cref="HwPackageSigner.PublicKeyBytes"/> bytes.</param>
    public unsafe void TrustKey(ReadOnlySpan<byte> publicKey)
    {
        HwPackageSigner.RequireLength(publicKey, HwPackageSigner.PublicKeyBytes, nameof(publicKey));
        Enter("HwMatrix.TrustKey");
        fixed (byte* p = publicKey)
        {
            Done(NativeMethods.HwMatrixTrustKey(_cap.Handle, Handle, p), "HwMatrix.TrustKey");
        }
    }

    /// <summary>
    /// Applies a signed <see cref="HwImageKind.Vectors"/> package: verifies both
    /// signatures, requires a newer version and an svn at or above the floor,
    /// installs the vectors, proves every implementation again and records the
    /// change. Needs <see cref="CapabilityRights.HwReconf"/>.
    /// </summary>
    /// <param name="package">The package.</param>
    /// <returns>How many implementations prove the new contract.</returns>
    public unsafe uint ApplyContract(ReadOnlySpan<byte> package)
    {
        Enter("HwMatrix.ApplyContract");
        uint proven;
        fixed (byte* p = package)
        {
            Done(NativeMethods.HwContractApply(_cap.Handle, Handle, p, (nuint)package.Length, out proven), "HwMatrix.ApplyContract");
        }

        return proven;
    }

    /// <summary>Reads the audit log's head and length.</summary>
    /// <returns>The head.</returns>
    /// <exception cref="ZxvFatalException">The chain does not verify; the platform halts.</exception>
    public unsafe HwAuditHead GetLog()
    {
        Enter("HwMatrix.GetLog");
        var head = new byte[32];
        uint count;
        fixed (byte* h = head)
        {
            Done(NativeMethods.HwMatrixLog(_cap.Handle, Handle, h, out count), "HwMatrix.GetLog");
        }

        return new HwAuditHead(head, count);
    }

    /// <summary>Reads audit record <paramref name="index"/> (0 = oldest).</summary>
    /// <param name="index">The record.</param>
    /// <returns>The record.</returns>
    public unsafe HwAuditRecord GetLogEntry(int index)
    {
        ArgumentOutOfRangeException.ThrowIfNegative(index);
        Enter("HwMatrix.GetLogEntry");
        byte[] subject = new byte[32], hash = new byte[32];
        int ev;
        uint reason, id, version, tag;
        fixed (byte* s = subject)
        fixed (byte* h = hash)
        {
            Done(NativeMethods.HwMatrixLogEntry(_cap.Handle, Handle, (uint)index, out ev, out reason, out id, out version, out tag, s, h), "HwMatrix.GetLogEntry");
        }

        return new HwAuditRecord((HwEvent)ev, (int)reason, id, version, Hexagram.FromTag((int)tag), subject, hash);
    }

    /// <summary>Releases the matrix and its implementations once no region uses it.</summary>
    public void Dispose() => Handle.Dispose();

    internal void Enter(string operation)
    {
        ObjectDisposedException.ThrowIf(Handle.IsClosed, this);
        _cap.Enter(operation);
        HwCallbacks.Clear();
    }

#pragma warning disable CA1031 // An implementation's exception must never unwind into native code; it is rethrown after the matrix call returns.
    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
    private static unsafe int OnCall(void* user, uint op, byte* input, nuint inLen, byte* output, nuint outCap, nuint* outLen)
    {
        try
        {
            var impl = (HwImplementation)GCHandle.FromIntPtr((IntPtr)user).Target!;
            int room = outCap > int.MaxValue ? int.MaxValue : (int)outCap;
            int n = impl(op, new ReadOnlySpan<byte>(input, checked((int)inLen)), new Span<byte>(output, room));
            if (n < 0)
            {
                return (int)ZxvStatus.InvalidArgument;
            }

            if ((nuint)n > outCap)
            {
                return (int)ZxvStatus.BufferTooSmall;
            }

            *outLen = (nuint)n;
            return (int)ZxvStatus.Ok;
        }
        catch (Exception ex)
        {
            return HwCallbacks.Fail(ex);
        }
    }
#pragma warning restore CA1031

    private void Done(int rc, string operation) => HwCallbacks.Check(rc, _cap, operation);
}
