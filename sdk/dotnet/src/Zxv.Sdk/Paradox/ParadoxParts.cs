// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0

using System;
using Zxv.Interop;

namespace Zxv.Paradox;

/// <summary>A policy's live state.</summary>
/// <param name="Values">The live values.</param>
/// <param name="Previous">The values before the last applied update.</param>
/// <param name="IsArmed">Whether an applied update awaits evaluation.</param>
/// <param name="TensionAtApply">The tension it was applied at.</param>
public sealed record ParadoxPolicyState(long[] Values, long[] Previous, bool IsArmed, uint TensionAtApply);

/// <summary>A hold's state.</summary>
/// <param name="Count">Branches held.</param>
/// <param name="Settled">The settled branch, or -1 while deferred.</param>
/// <param name="Decisive">The branch that currently has decisive evidence, or -1.</param>
public readonly record struct ParadoxHoldInfo(int Count, int Settled, int Decisive);

/// <summary>One branch of a hold.</summary>
/// <param name="Id">The 32-byte id.</param>
/// <param name="Positive">Evidence for.</param>
/// <param name="Negative">Evidence against.</param>
public sealed record ParadoxBranch(byte[] Id, uint Positive, uint Negative);

/// <summary>One graph node, in id order.</summary>
/// <param name="Id">The 32-byte id.</param>
/// <param name="Kind">Its current kind.</param>
/// <param name="State">Its state.</param>
public sealed record ParadoxNode(byte[] Id, ParadoxKind Kind, ParadoxNodeState State);

/// <summary>A graph's summary.</summary>
/// <param name="Nodes">Node count.</param>
/// <param name="Passes">Passes run.</param>
/// <param name="TotalMagnitude">Net tension magnitude of the open nodes after the last pass.</param>
/// <param name="TotalDirection">Its direction: -1, 0 or +1.</param>
/// <param name="Escalated">Whether it reached the step limit.</param>
/// <param name="Chain">The SHA-256 chain over every pass (zeros before the first).</param>
public sealed record ParadoxGraphInfo(int Nodes, int Passes, uint TotalMagnitude, int TotalDirection, bool Escalated, byte[] Chain);

/// <summary>
/// The shared plumbing of the paradox handles: one capability, one native
/// handle bound to the capability's context, and a managed lock (policy,
/// hold and graph handles are not thread-safe natively).
/// </summary>
public abstract class ParadoxHandleBase : IDisposable
{
    private readonly object _gate = new();

    private protected ParadoxHandleBase(Capability capability)
    {
        Cap = capability;
    }

    private protected Capability Cap { get; }

    private protected object Gate => _gate;

    private protected abstract ZxvDependentHandle NativeHandle { get; }

    /// <summary>Releases the native handle.</summary>
    public void Dispose()
    {
        lock (_gate)
        {
            NativeHandle.Dispose();
        }

        GC.SuppressFinalize(this);
    }

    private protected void Created(int rc, string operation)
    {
        if (rc != (int)ZxvStatus.Ok)
        {
            NativeHandle.Dispose();
            Cap.Check(rc, operation);
        }

        NativeHandle.AddDependency(Cap.Context.Handle);
    }

    private protected void Write(string operation)
    {
        ObjectDisposedException.ThrowIf(NativeHandle.IsClosed, this);
        Cap.Enter(operation);
    }

    private protected void Read(string operation)
    {
        ObjectDisposedException.ThrowIf(NativeHandle.IsClosed, this);
        Cap.Context.ThrowIfHalted(operation);
    }

    private protected void Check(int rc, string operation)
    {
        if (rc != (int)ZxvStatus.Ok)
        {
            NativeCall.Check(rc, operation, Cap.Context);
        }
    }
}

/// <summary>
/// A constructive policy (<c>zxv_paradox_policy</c>): n parameters with
/// published bounds that never change. Values are swarm parameters (budget
/// shares, routing weights, escrow timing), never balances. Create one with
/// <see cref="ZxvPlatform.CreateParadoxPolicy(Capability, ReadOnlySpan{long}, ReadOnlySpan{long}, ReadOnlySpan{long}, ParadoxConcerns)"/>.
/// </summary>
/// <remarks>A policy flagged <see cref="ParadoxConcerns.Money"/> or <see cref="ParadoxConcerns.Device"/> is never changed automatically: every update becomes a proposal for the owner.</remarks>
public sealed class ParadoxPolicy : ParadoxHandleBase
{
    /// <summary>Most parameters (<c>ZXV_PDX_POLICY_MAX</c>).</summary>
    public const int MaxParameters = 64;

    private readonly ZxvParadoxPolicyHandle _handle;

    internal unsafe ParadoxPolicy(Capability capability, ReadOnlySpan<long> lo, ReadOnlySpan<long> hi, ReadOnlySpan<long> initial, ParadoxConcerns concerns)
        : base(capability)
    {
        if (hi.Length != lo.Length || initial.Length != lo.Length)
        {
            throw new ArgumentException("lo, hi and initial must have the same length.", nameof(initial));
        }

        Count = lo.Length;
        capability.Enter("ParadoxPolicy.Create");
        int rc;
        fixed (long* l = lo)
        fixed (long* h = hi)
        fixed (long* i = initial)
        {
            rc = NativeMethods.ParadoxPolicyCreate(capability.Handle, (uint)lo.Length, l, h, i, (uint)concerns, out _handle);
        }

        Created(rc, "ParadoxPolicy.Create");
    }

    /// <summary>Gets the number of parameters.</summary>
    public int Count { get; }

    /// <summary>Gets the live and previous values and the arming state.</summary>
    public unsafe ParadoxPolicyState State
    {
        get
        {
            var v = new long[Count];
            var p = new long[Count];
            lock (Gate)
            {
                Read("ParadoxPolicy.State");
                fixed (long* pv = v)
                fixed (long* pp = p)
                {
                    Check(NativeMethods.ParadoxPolicyValues(_handle, pv, pp, (uint)Count, out int armed, out uint at), "ParadoxPolicy.State");
                    return new ParadoxPolicyState(v, p, armed != 0, at);
                }
            }
        }
    }

    /// <summary>Gets the live values.</summary>
    public long[] Values => State.Values;

    /// <summary>Gets the lower bounds.</summary>
    public long[] Lower => Bounds().Lo;

    /// <summary>Gets the upper bounds.</summary>
    public long[] Upper => Bounds().Hi;

    /// <summary>Gets the money or device flags.</summary>
    public ParadoxConcerns Concerns => Bounds().Concerns;

    internal ZxvParadoxPolicyHandle Handle => _handle;

    internal object Lock => Gate;

    private protected override ZxvDependentHandle NativeHandle => _handle;

    /// <summary>
    /// A proposed update as bytes for the owner or for signing: n, how many
    /// values are out of bounds, the flags, then lo, hi, current and proposed
    /// for each parameter (little-endian). Reads only.
    /// </summary>
    /// <param name="proposed">Exactly <see cref="Count"/> values.</param>
    /// <returns>12 + 32n bytes.</returns>
    public unsafe byte[] Proposal(ReadOnlySpan<long> proposed)
    {
        RequireCount(proposed, nameof(proposed));
        lock (Gate)
        {
            Read("ParadoxPolicy.Proposal");
            fixed (long* s = proposed)
            {
                long* x = s;
                uint n = (uint)Count;
                return NativeBuffers.ReadBytes((byte* b, nuint c, out nuint l) => NativeMethods.ParadoxPolicyProposal(_handle, x, n, b, c, out l), rc => Check(rc, "ParadoxPolicy.Proposal"));
            }
        }
    }

    internal void RequireCount(ReadOnlySpan<long> values, string paramName)
    {
        if (values.Length != Count)
        {
            throw new ArgumentException($"Must be exactly {Count} values (got {values.Length}).", paramName);
        }
    }

    internal void EnsureOpen() => ObjectDisposedException.ThrowIf(_handle.IsClosed, this);

    private unsafe (long[] Lo, long[] Hi, ParadoxConcerns Concerns) Bounds()
    {
        var lo = new long[Count];
        var hi = new long[Count];
        lock (Gate)
        {
            Read("ParadoxPolicy.Bounds");
            fixed (long* l = lo)
            fixed (long* h = hi)
            {
                Check(NativeMethods.ParadoxPolicyBounds(_handle, l, h, (uint)Count, out uint flags), "ParadoxPolicy.Bounds");
                return (lo, hi, (ParadoxConcerns)flags);
            }
        }
    }
}

/// <summary>
/// A neutral hold (<c>zxv_paradox_hold</c>): up to 64 candidate branches,
/// all kept until one has decisive evidence or the owner picks it. Create one
/// with <see cref="ZxvPlatform.CreateParadoxHold(Capability, int, int)"/>.
/// </summary>
public sealed class ParadoxHold : ParadoxHandleBase
{
    /// <summary>Most branches (<c>ZXV_PDX_HOLD_MAX</c>).</summary>
    public const int MaxBranches = 64;

    private readonly ZxvParadoxHoldHandle _handle;

    internal ParadoxHold(Capability capability, int capacity, int decisive)
        : base(capability)
    {
        ArgumentOutOfRangeException.ThrowIfNegative(capacity);
        ArgumentOutOfRangeException.ThrowIfNegative(decisive);
        capability.Enter("ParadoxHold.Create");
        int rc = NativeMethods.ParadoxHoldCreate(capability.Handle, (uint)capacity, (uint)decisive, out _handle);
        Created(rc, "ParadoxHold.Create");
    }

    /// <summary>Gets the branch count, the settled branch and the decisive branch.</summary>
    public ParadoxHoldInfo Info
    {
        get
        {
            lock (Gate)
            {
                Read("ParadoxHold.Info");
                Check(NativeMethods.ParadoxHoldInfo(_handle, out uint n, out int settled, out int decisive), "ParadoxHold.Info");
                return new ParadoxHoldInfo((int)n, settled, decisive);
            }
        }
    }

    internal ZxvParadoxHoldHandle Handle => _handle;

    internal object Lock => Gate;

    private protected override ZxvDependentHandle NativeHandle => _handle;

    /// <summary>Adds a candidate (a 32-byte digest). The same id again is the same candidate.</summary>
    /// <param name="id">32 bytes.</param>
    /// <exception cref="ZxvException">Settled (<see cref="ZxvStatus.InvalidState"/>) or full (<see cref="ZxvStatus.CapacityExceeded"/>).</exception>
    public unsafe void Add(ReadOnlySpan<byte> id)
    {
        NativeBuffers.RequireLength(id, 32, nameof(id));
        lock (Gate)
        {
            Read("ParadoxHold.Add");
            fixed (byte* p = id)
            {
                Check(NativeMethods.ParadoxHoldAdd(_handle, p), "ParadoxHold.Add");
            }
        }
    }

    /// <summary>Adds one piece of evidence for or against a candidate.</summary>
    /// <param name="id">32 bytes.</param>
    /// <param name="positive">For (<see langword="true"/>) or against.</param>
    /// <exception cref="ZxvException">Unknown id (<see cref="ZxvStatus.NotFound"/>) or settled (<see cref="ZxvStatus.InvalidState"/>).</exception>
    public unsafe void AddEvidence(ReadOnlySpan<byte> id, bool positive)
    {
        NativeBuffers.RequireLength(id, 32, nameof(id));
        lock (Gate)
        {
            Read("ParadoxHold.AddEvidence");
            fixed (byte* p = id)
            {
                Check(NativeMethods.ParadoxHoldEvidence(_handle, p, positive ? 1 : 0), "ParadoxHold.AddEvidence");
            }
        }
    }

    /// <summary>Branch <paramref name="index"/>.</summary>
    /// <param name="index">0-based.</param>
    /// <returns>The branch.</returns>
    public unsafe ParadoxBranch GetBranch(int index)
    {
        ArgumentOutOfRangeException.ThrowIfNegative(index);
        var id = new byte[32];
        lock (Gate)
        {
            Read("ParadoxHold.GetBranch");
            fixed (byte* p = id)
            {
                Check(NativeMethods.ParadoxHoldBranch(_handle, (uint)index, p, out uint pos, out uint neg), "ParadoxHold.GetBranch");
                return new ParadoxBranch(id, pos, neg);
            }
        }
    }

    internal void EnsureOpen() => ObjectDisposedException.ThrowIf(_handle.IsClosed, this);
}

/// <summary>
/// A complex paradox (<c>zxv_paradox_graph</c>): up to 64 nodes with
/// dependencies, resolved pass by pass in a fixed order, escalating to the
/// owner after a step limit. Create one with
/// <see cref="ZxvPlatform.CreateParadoxGraph(Capability, int)"/>; run passes
/// with <see cref="ParadoxBook.Pass"/>.
/// </summary>
/// <remarks>Peers with the same signed inputs reach the same states and the same chain because nodes are taken in id order; there is no shared clock.</remarks>
public sealed class ParadoxGraph : ParadoxHandleBase
{
    /// <summary>Most nodes (<c>ZXV_PDX_GRAPH_MAX</c>).</summary>
    public const int MaxNodes = 64;

    /// <summary>Most dependencies per node (<c>ZXV_PDX_MAX_DEPS</c>).</summary>
    public const int MaxDependencies = 4;

    private readonly ZxvParadoxGraphHandle _handle;

    internal ParadoxGraph(Capability capability, int stepLimit)
        : base(capability)
    {
        ArgumentOutOfRangeException.ThrowIfNegative(stepLimit);
        capability.Enter("ParadoxGraph.Create");
        int rc = NativeMethods.ParadoxGraphCreate(capability.Handle, (uint)stepLimit, out _handle);
        Created(rc, "ParadoxGraph.Create");
    }

    /// <summary>Gets the node count, passes, net tension, escalation and chain.</summary>
    public unsafe ParadoxGraphInfo Info
    {
        get
        {
            var chain = new byte[32];
            lock (Gate)
            {
                Read("ParadoxGraph.Info");
                fixed (byte* c = chain)
                {
                    Check(NativeMethods.ParadoxGraphInfo(_handle, out uint nodes, out uint pass, out uint mag, out int dir, out int esc, c), "ParadoxGraph.Info");
                    return new ParadoxGraphInfo((int)nodes, (int)pass, mag, dir, esc != 0, chain);
                }
            }
        }
    }

    internal ZxvParadoxGraphHandle Handle => _handle;

    internal object Lock => Gate;

    private protected override ZxvDependentHandle NativeHandle => _handle;

    /// <summary>Adds a node before the first pass.</summary>
    /// <param name="id">The 32-byte hash of its signed input (the sort key).</param>
    /// <param name="kind">Its classified kind.</param>
    /// <param name="tag">Its classified tag.</param>
    /// <param name="magnitude">Tension magnitude.</param>
    /// <param name="direction">-1, 0 or +1.</param>
    /// <exception cref="ZxvException">The id is present (<see cref="ZxvStatus.Duplicate"/>), full, or after the first pass (<see cref="ZxvStatus.InvalidState"/>).</exception>
    public unsafe void AddNode(ReadOnlySpan<byte> id, ParadoxKind kind, Hexagram tag, uint magnitude, int direction)
    {
        NativeBuffers.RequireLength(id, 32, nameof(id));
        lock (Gate)
        {
            Read("ParadoxGraph.AddNode");
            fixed (byte* p = id)
            {
                Check(NativeMethods.ParadoxGraphAddNode(_handle, p, (int)kind, tag.Tag, magnitude, direction), "ParadoxGraph.AddNode");
            }
        }
    }

    /// <summary>Node <paramref name="id"/> depends on node <paramref name="dependency"/> (which may be added later, but before the first pass).</summary>
    /// <param name="id">32 bytes.</param>
    /// <param name="dependency">32 bytes.</param>
    public unsafe void AddDependency(ReadOnlySpan<byte> id, ReadOnlySpan<byte> dependency)
    {
        NativeBuffers.RequireLength(id, 32, nameof(id));
        NativeBuffers.RequireLength(dependency, 32, nameof(dependency));
        lock (Gate)
        {
            Read("ParadoxGraph.AddDependency");
            fixed (byte* p = id)
            fixed (byte* d = dependency)
            {
                Check(NativeMethods.ParadoxGraphAddDep(_handle, p, d), "ParadoxGraph.AddDependency");
            }
        }
    }

    /// <summary>Queues new evidence for a node, applied at the start of the next pass ("worst wins" within a batch).</summary>
    /// <param name="id">32 bytes.</param>
    /// <param name="kind">The new kind.</param>
    /// <param name="tag">The new tag.</param>
    /// <param name="magnitude">Tension magnitude.</param>
    /// <param name="direction">-1, 0 or +1.</param>
    public unsafe void Update(ReadOnlySpan<byte> id, ParadoxKind kind, Hexagram tag, uint magnitude, int direction)
    {
        NativeBuffers.RequireLength(id, 32, nameof(id));
        lock (Gate)
        {
            Read("ParadoxGraph.Update");
            fixed (byte* p = id)
            {
                Check(NativeMethods.ParadoxGraphUpdate(_handle, p, (int)kind, tag.Tag, magnitude, direction), "ParadoxGraph.Update");
            }
        }
    }

    /// <summary>A node's state (<see cref="ParadoxNodeState.Open"/> before the first pass).</summary>
    /// <param name="id">32 bytes.</param>
    /// <returns>The state.</returns>
    public unsafe ParadoxNodeState StateOf(ReadOnlySpan<byte> id)
    {
        NativeBuffers.RequireLength(id, 32, nameof(id));
        lock (Gate)
        {
            Read("ParadoxGraph.StateOf");
            fixed (byte* p = id)
            {
                Check(NativeMethods.ParadoxGraphState(_handle, p, out int state), "ParadoxGraph.StateOf");
                return (ParadoxNodeState)state;
            }
        }
    }

    /// <summary>Node <paramref name="index"/> in id order (after the first pass).</summary>
    /// <param name="index">0-based.</param>
    /// <returns>The node.</returns>
    public unsafe ParadoxNode NodeAt(int index)
    {
        ArgumentOutOfRangeException.ThrowIfNegative(index);
        var id = new byte[32];
        lock (Gate)
        {
            Read("ParadoxGraph.NodeAt");
            fixed (byte* p = id)
            {
                Check(NativeMethods.ParadoxGraphNodeAt(_handle, (uint)index, p, out int kind, out int state), "ParadoxGraph.NodeAt");
                return new ParadoxNode(id, (ParadoxKind)kind, (ParadoxNodeState)state);
            }
        }
    }

    internal void EnsureOpen() => ObjectDisposedException.ThrowIf(_handle.IsClosed, this);
}
