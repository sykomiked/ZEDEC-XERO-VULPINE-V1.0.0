// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0

using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using System.Threading;
using System.Threading.Tasks;
using Zxv.Interop;

namespace Zxv;

/// <summary>The resource a <see cref="Budget"/> meters (the kernel's <c>rmag_resource_t</c>).</summary>
public enum BudgetResource
{
    /// <summary>Compute cycles.</summary>
    ComputeCycles = 0,

    /// <summary>Memory.</summary>
    Memory = 1,

    /// <summary>Bandwidth.</summary>
    Bandwidth = 2,

    /// <summary>Energy.</summary>
    Energy = 3,

    /// <summary>Storage.</summary>
    Storage = 4,

    /// <summary>Model tokens: one prompt or generated token is one unit (the kernel's event count).</summary>
    Tokens = 12,
}

/// <summary>What happened to a budget, as the native listener reports it.</summary>
public enum BudgetEventKind
{
    /// <summary>A consume was refused (<see cref="ZxvStatus.BudgetExhausted"/>).</summary>
    Denied = 1,

    /// <summary>A consume or release was malformed (<see cref="ZxvStatus.BudgetAmountInvalid"/>).</summary>
    Invalid = 2,

    /// <summary>A release or a larger limit freed budget.</summary>
    Replenished = 3,
}

/// <summary>Data of a budget event.</summary>
public sealed class BudgetEventArgs : EventArgs
{
    internal BudgetEventArgs(Budget budget, BudgetEventKind kind)
    {
        Budget = budget;
        Kind = kind;
    }

    /// <summary>Gets the budget.</summary>
    public Budget Budget { get; }

    /// <summary>Gets what happened.</summary>
    public BudgetEventKind Kind { get; }
}

/// <summary>A budget's exact state.</summary>
/// <param name="Remaining">Limit minus consumed (zero or more for an enforced budget).</param>
/// <param name="Used">Consumed so far.</param>
/// <param name="Epoch">How many times it was replenished; changes whenever budget is freed.</param>
public readonly record struct BudgetStatus(Rational Remaining, Rational Used, ulong Epoch);

/// <summary>
/// A set of exact rational resource budgets (the kernel's <c>rmag</c>, K2).
/// Create one with <see cref="ZxvPlatform.CreateBudgets(Capability)"/>, then
/// add budgets with <see cref="Create"/>. Thread-safe.
/// </summary>
/// <remarks>
/// Events (<see cref="Exhausted"/>, <see cref="Replenished"/>,
/// <see cref="InvalidAmount"/>) are raised synchronously on the thread whose
/// call caused them, after the native lock is released, so a handler may call
/// back into the budgets. An exception thrown by a handler is caught and
/// written to <see cref="Trace"/>: it must not unwind through native code.
/// </remarks>
public sealed class Budgets : IDisposable
{
    private readonly Capability _cap;

    // SafeHandle.IsClosed stays false while a session bound to a budget holds
    // a reference, so disposal is tracked here (once, observably at once).
    private int _disposed;
    private readonly ZxvBudgetsHandle _handle;
    private readonly object _gate = new();
    private readonly Dictionary<uint, Budget> _budgets = new();
    private readonly Dictionary<uint, TaskCompletionSource> _replenished = new();
    private GCHandle _self;

    internal unsafe Budgets(Capability capability)
    {
        _cap = capability;
        capability.Enter("Budgets.Create");
        int rc = NativeMethods.RmagCreateCap(capability.Handle, out _handle);
        if (rc != (int)ZxvStatus.Ok)
        {
            _handle.Dispose();
            capability.Check(rc, "Budgets.Create");
        }

        _handle.AddDependency(capability.Context.Handle);
        _self = GCHandle.Alloc(this, GCHandleType.Weak);
        rc = NativeMethods.RmagListen(_handle, &OnNativeEvent, (void*)GCHandle.ToIntPtr(_self));
        if (rc != (int)ZxvStatus.Ok)
        {
            Dispose();
            capability.Check(rc, "Budgets.Listen");
        }
    }

    /// <summary>Raised when a consume is refused because the budget is exhausted.</summary>
    public event EventHandler<BudgetEventArgs>? Exhausted;

    /// <summary>Raised when a release or a larger limit frees budget.</summary>
    public event EventHandler<BudgetEventArgs>? Replenished;

    /// <summary>Raised when a consume or release amount is malformed (negative or overflowing).</summary>
    public event EventHandler<BudgetEventArgs>? InvalidAmount;

    internal Capability Capability => _cap;

    internal ZxvBudgetsHandle Handle => _handle;

    /// <summary>Creates a budget. Needs <see cref="CapabilityRights.Budget"/>.</summary>
    /// <param name="name">A name for diagnostics.</param>
    /// <param name="resource">What it meters.</param>
    /// <param name="limit">The exact limit (zero or more).</param>
    /// <param name="enforce"><see langword="true"/> to refuse consumes past the limit; <see langword="false"/> only counts.</param>
    /// <returns>The budget.</returns>
    public Budget Create(string name, BudgetResource resource, Rational limit, bool enforce = true)
    {
        Guard.Text(name, nameof(name));
        Enter("Budgets.Create");
        int rc = NativeMethods.RmagBudget(_cap.Handle, _handle, name, (int)resource, limit.Numerator, limit.Denominator, enforce ? 1 : 0, out uint index);
        _cap.Check(rc, "Budgets.Create");
        var budget = new Budget(this, index, name, resource);
        lock (_gate)
        {
            _budgets[index] = budget;
        }

        return budget;
    }

    /// <summary>Stops listening and releases the native budgets. Pending <see cref="Budget.ConsumeAsync(Rational, CancellationToken)"/> calls fail with <see cref="ObjectDisposedException"/>.</summary>
    public unsafe void Dispose()
    {
        if (Interlocked.Exchange(ref _disposed, 1) != 0)
        {
            return;
        }

        if (!_handle.IsInvalid)
        {
            _ = NativeMethods.RmagListen(_handle, null, null);
        }

        _handle.Dispose();
        if (_self.IsAllocated)
        {
            _self.Free();
        }

        TaskCompletionSource[] waiters;
        lock (_gate)
        {
            waiters = new TaskCompletionSource[_replenished.Count];
            _replenished.Values.CopyTo(waiters, 0);
            _replenished.Clear();
        }

        foreach (TaskCompletionSource w in waiters)
        {
            w.TrySetException(new ObjectDisposedException(nameof(Budgets)));
        }
    }

    internal void Enter(string operation)
    {
        ObjectDisposedException.ThrowIf(Volatile.Read(ref _disposed) != 0, this);
        _cap.Enter(operation);
    }

    /// <summary>A task that completes on the next replenishment of <paramref name="index"/>. Take it BEFORE trying, so no wake-up is lost.</summary>
    internal Task NextReplenishment(uint index)
    {
        lock (_gate)
        {
            ObjectDisposedException.ThrowIf(Volatile.Read(ref _disposed) != 0, this);
            if (!_replenished.TryGetValue(index, out TaskCompletionSource? tcs))
            {
                tcs = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
                _replenished[index] = tcs;
            }

            return tcs.Task;
        }
    }

    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
    private static unsafe void OnNativeEvent(void* user, uint budget, int ev)
    {
        if (GCHandle.FromIntPtr((IntPtr)user).Target is Budgets self)
        {
            self.Dispatch(budget, (BudgetEventKind)ev);
        }
    }

#pragma warning disable CA1031 // A handler's exception must never unwind into native code.
    private void Dispatch(uint index, BudgetEventKind kind)
    {
        Budget? budget;
        TaskCompletionSource? wake = null;
        lock (_gate)
        {
            _budgets.TryGetValue(index, out budget);
            if (kind == BudgetEventKind.Replenished && _replenished.Remove(index, out TaskCompletionSource? tcs))
            {
                wake = tcs;
            }
        }

        wake?.TrySetResult();
        if (budget is null)
        {
            return;
        }

        EventHandler<BudgetEventArgs>? handler = kind switch
        {
            BudgetEventKind.Denied => Exhausted,
            BudgetEventKind.Replenished => Replenished,
            BudgetEventKind.Invalid => InvalidAmount,
            _ => null,
        };
        try
        {
            handler?.Invoke(this, new BudgetEventArgs(budget, kind));
        }
        catch (Exception ex)
        {
            Trace.TraceError("ZXV budget event handler threw: {0}", ex);
        }
    }
#pragma warning restore CA1031
}

/// <summary>
/// One exact rational budget. <see cref="Consume(Rational)"/> refuses an
/// amount that would pass an enforced limit (nothing is consumed);
/// <see cref="ConsumeAsync(Rational, CancellationToken)"/> waits for a
/// <see cref="Release(Rational)"/> or a larger limit instead.
/// </summary>
public sealed class Budget
{
    private readonly Budgets _owner;

    internal Budget(Budgets owner, uint index, string name, BudgetResource resource)
    {
        _owner = owner;
        Index = index;
        Name = name;
        Resource = resource;
    }

    /// <summary>Gets the budget's native index.</summary>
    public uint Index { get; }

    /// <summary>Gets the name given at creation.</summary>
    public string Name { get; }

    /// <summary>Gets what it meters.</summary>
    public BudgetResource Resource { get; }

    /// <summary>Gets the exact remaining amount.</summary>
    public Rational Remaining => GetStatus().Remaining;

    internal Budgets Owner => _owner;

    /// <summary>Consumes <paramref name="amount"/> (greater than zero).</summary>
    /// <param name="amount">The amount.</param>
    /// <exception cref="BudgetExhaustedException">It would pass the limit; nothing was consumed. Retry after a release.</exception>
    /// <exception cref="ZxvUsageException">The amount is negative or overflows (<see cref="ZxvStatus.BudgetAmountInvalid"/>); never retry it.</exception>
    public void Consume(Rational amount)
    {
        _owner.Enter("Budget.Consume");
        _owner.Capability.Check(ConsumeCore(amount), "Budget.Consume");
    }

    /// <summary>Consumes <paramref name="amount"/> if it fits.</summary>
    /// <param name="amount">The amount.</param>
    /// <returns><see langword="false"/> when it would pass the limit (nothing consumed).</returns>
    public bool TryConsume(Rational amount)
    {
        _owner.Enter("Budget.TryConsume");
        int rc = ConsumeCore(amount);
        if (rc == (int)ZxvStatus.BudgetExhausted)
        {
            return false;
        }

        _owner.Capability.Check(rc, "Budget.TryConsume");
        return true;
    }

    /// <summary>
    /// Consumes <paramref name="amount"/>, waiting while the budget is
    /// exhausted until a release or a larger limit replenishes it.
    /// </summary>
    /// <param name="amount">The amount.</param>
    /// <param name="cancellationToken">Cancels the wait.</param>
    /// <returns>A task that completes once the amount is consumed.</returns>
    /// <exception cref="ZxvUsageException">The amount is malformed (fails at once, never waits).</exception>
    public Task ConsumeAsync(Rational amount, CancellationToken cancellationToken = default) =>
        ConsumeAsync(amount, Timeout.InfiniteTimeSpan, cancellationToken);

    /// <summary>Like <see cref="ConsumeAsync(Rational, CancellationToken)"/>, giving up after <paramref name="timeout"/>.</summary>
    /// <param name="amount">The amount.</param>
    /// <param name="timeout">How long to wait in total; <see cref="Timeout.InfiniteTimeSpan"/> for no limit.</param>
    /// <param name="cancellationToken">Cancels the wait.</param>
    /// <returns>A task that completes once the amount is consumed.</returns>
    /// <exception cref="TimeoutException">The budget was not replenished in time.</exception>
    public async Task ConsumeAsync(Rational amount, TimeSpan timeout, CancellationToken cancellationToken = default)
    {
        long start = Stopwatch.GetTimestamp();
        while (true)
        {
            cancellationToken.ThrowIfCancellationRequested();
            _owner.Enter("Budget.ConsumeAsync");
            Task replenished = _owner.NextReplenishment(Index);
            int rc = ConsumeCore(amount);
            if (rc == (int)ZxvStatus.Ok)
            {
                return;
            }

            if (rc != (int)ZxvStatus.BudgetExhausted)
            {
                _owner.Capability.Check(rc, "Budget.ConsumeAsync");
            }

            if (timeout == Timeout.InfiniteTimeSpan)
            {
                await replenished.WaitAsync(cancellationToken).ConfigureAwait(false);
            }
            else
            {
                TimeSpan left = timeout - Stopwatch.GetElapsedTime(start);
                if (left <= TimeSpan.Zero)
                {
                    throw new TimeoutException($"Budget '{Name}' was not replenished within {timeout}.");
                }

                await replenished.WaitAsync(left, cancellationToken).ConfigureAwait(false);
            }
        }
    }

    /// <summary>Gives back <paramref name="amount"/> (consumed is clamped at zero) and wakes waiters.</summary>
    /// <param name="amount">The amount.</param>
    public void Release(Rational amount)
    {
        _owner.Enter("Budget.Release");
        _owner.Capability.Check(NativeMethods.RmagRelease(_owner.Capability.Handle, _owner.Handle, Index, amount.Numerator, amount.Denominator), "Budget.Release");
    }

    /// <summary>Replaces the limit; a larger limit wakes waiters.</summary>
    /// <param name="limit">The new limit.</param>
    public void SetLimit(Rational limit)
    {
        _owner.Enter("Budget.SetLimit");
        _owner.Capability.Check(NativeMethods.RmagSetLimit(_owner.Capability.Handle, _owner.Handle, Index, limit.Numerator, limit.Denominator), "Budget.SetLimit");
    }

    /// <summary>Reads the exact state.</summary>
    /// <returns>Remaining, used and the replenishment epoch.</returns>
    public BudgetStatus GetStatus()
    {
        _owner.Enter("Budget.GetStatus");
        _owner.Capability.Check(NativeMethods.RmagStatus(_owner.Handle, Index, out long rn, out long rd, out long un, out long ud, out ulong epoch), "Budget.GetStatus");
        return new BudgetStatus(Rational.Of(rn, rd), Rational.Of(un, ud), epoch);
    }

    /// <inheritdoc/>
    public override string ToString() => $"{Name} ({Resource})";

    private int ConsumeCore(Rational amount) =>
        NativeMethods.RmagConsume(_owner.Capability.Handle, _owner.Handle, Index, amount.Numerator, amount.Denominator);
}
