// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0

using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Threading;
using System.Threading.Tasks;
using Zxv.Interop;

namespace Zxv;

/// <summary>The K6 coordinator's answer to <see cref="PhaseClock.Admit"/>.</summary>
public enum PhaseDecision
{
    /// <summary>Admitted; a token was issued.</summary>
    Admit = 0,

    /// <summary>Rejected.</summary>
    Veto = 1,

    /// <summary>Parked in S0 until later.</summary>
    Defer = 2,

    /// <summary>Try again.</summary>
    Retry = 3,
}

/// <summary>Properties of a step submitted to <see cref="PhaseClock.Admit"/>.</summary>
[Flags]
#pragma warning disable CA1028 // uint matches the C ABI.
public enum PhaseStepRequirements : uint
#pragma warning restore CA1028
{
    /// <summary>No requirement.</summary>
    None = 0,

    /// <summary>The step needs its phase to be healthy.</summary>
    RequiresHealth = 0x1,

    /// <summary>The step drives hardware.</summary>
    Hardware = 0x2,

    /// <summary>The step waits on an unresolved (S0) question.</summary>
    S0Pending = 0x4,
}

/// <summary>Health of a phase, as reported to the coordinator.</summary>
public enum PhaseHealth
{
    /// <summary>Not reported.</summary>
    Unknown = 0,

    /// <summary>Healthy.</summary>
    Healthy = 1,

    /// <summary>Degraded.</summary>
    Degraded = 2,

    /// <summary>Unhealthy.</summary>
    Unhealthy = 3,
}

/// <summary>State of a <see cref="DeferredEvent"/>.</summary>
public enum DeferredEventState
{
    /// <summary>S0: waiting for its tick.</summary>
    Pending = 0,

    /// <summary>Its tick was reached.</summary>
    Cleared = 1,

    /// <summary>Cancelled before its tick.</summary>
    Cancelled = 2,
}

/// <summary>The result of <see cref="PhaseClock.Admit"/>.</summary>
/// <param name="Decision">The decision.</param>
/// <param name="Token">A non-zero token when admitted; zero otherwise.</param>
public readonly record struct PhaseAdmission(PhaseDecision Decision, ulong Token)
{
    /// <summary>Gets a value indicating whether the step was admitted.</summary>
    public bool IsAdmitted => Decision == PhaseDecision.Admit;
}

/// <summary>
/// The phase coordinator clock (the kernel's <c>phase_coord</c>, K6). ZXV time
/// is a logical tick count, not wall-clock time; one harmonic fundamental is
/// <see cref="TicksPerCycle"/> ticks. Create one with
/// <see cref="ZxvPlatform.CreatePhaseClock(Capability)"/>. Thread-safe.
/// </summary>
/// <remarks>
/// Two drivers: manual (<see cref="Advance(ulong)"/>, for deterministic tests
/// and simulations) and real time (<see cref="StartRealtime(ulong, TimeSpan?)"/>,
/// a <see cref="PeriodicTimer"/> loop). Either way, advancing past a deferred
/// event's tick completes its <see cref="DeferredEvent.WaitClearedAsync(CancellationToken)"/>.
/// </remarks>
public sealed class PhaseClock : IDisposable
{
    /// <summary>Ticks in one harmonic fundamental: 27,720 = lcm(1..11), so every harmonic 1..11 lands on a whole tick.</summary>
    public const ulong TicksPerCycle = 27720;

    private readonly Capability _cap;
    private readonly ZxvPhaseHandle _handle;
    private readonly object _gate = new();
    private readonly Dictionary<ulong, DeferredEvent> _waiting = new();
    private RealtimeDriver? _driver;

    internal PhaseClock(Capability capability)
    {
        _cap = capability;
        capability.Enter("PhaseClock.Create");
        int rc = NativeMethods.PhaseCreateCap(capability.Handle, out _handle);
        if (rc != (int)ZxvStatus.Ok)
        {
            _handle.Dispose();
            capability.Check(rc, "PhaseClock.Create");
        }

        _handle.AddDependency(capability.Context.Handle);
    }

    /// <summary>Gets the current tick.</summary>
    public ulong Now
    {
        get
        {
            Enter("PhaseClock.Now");
            _cap.Check(NativeMethods.PhaseNow(_handle, out ulong tick), "PhaseClock.Now");
            return tick;
        }
    }

    /// <summary>Gets the earliest due tick of a pending deferred event, or <see langword="null"/> when none is pending.</summary>
    public ulong? NextDue
    {
        get
        {
            Enter("PhaseClock.NextDue");
            int rc = NativeMethods.PhaseNextDue(_handle, out ulong tick);
            if (rc == (int)ZxvStatus.NotFound)
            {
                return null;
            }

            _cap.Check(rc, "PhaseClock.NextDue");
            return tick;
        }
    }

    /// <summary>Gets a value indicating whether a real-time driver is running.</summary>
    public bool IsRealtime => _driver is not null;

    /// <summary>
    /// Advances the clock by <paramref name="delta"/> ticks. Deferred events due
    /// at or before the new tick clear, and their waiters complete. Needs
    /// <see cref="CapabilityRights.Phase"/>.
    /// </summary>
    /// <param name="delta">Ticks to advance.</param>
    /// <returns>How many deferred events cleared.</returns>
    public int Advance(ulong delta)
    {
        Enter("PhaseClock.Advance");
        _cap.Check(NativeMethods.PhaseAdvance(_cap.Handle, _handle, delta, out _, out uint cleared), "PhaseClock.Advance");
        if (cleared > 0)
        {
            CompleteWaiters();
        }

        return checked((int)cleared);
    }

    /// <summary>Asks the coordinator to admit a step. Needs <see cref="CapabilityRights.Phase"/>.</summary>
    /// <param name="phaseId">The phase (1..13).</param>
    /// <param name="stepId">The step.</param>
    /// <param name="flags">The step's requirements.</param>
    /// <returns>The decision and, when admitted, a token.</returns>
    public PhaseAdmission Admit(uint phaseId, uint stepId, PhaseStepRequirements flags = PhaseStepRequirements.None)
    {
        Enter("PhaseClock.Admit");
        _cap.Check(NativeMethods.PhaseAdmit(_cap.Handle, _handle, phaseId, stepId, (uint)flags, out int decision, out ulong token), "PhaseClock.Admit");
        return new PhaseAdmission((PhaseDecision)decision, token);
    }

    /// <summary>Reports a phase's health. Needs <see cref="CapabilityRights.Phase"/>.</summary>
    /// <param name="phaseId">The phase (1..13).</param>
    /// <param name="health">Its health.</param>
    public void SetHealth(uint phaseId, PhaseHealth health)
    {
        Enter("PhaseClock.SetHealth");
        _cap.Check(NativeMethods.PhaseSetHealth(_cap.Handle, _handle, phaseId, (int)health), "PhaseClock.SetHealth");
    }

    /// <summary>Parks an S0 event until absolute tick <paramref name="dueTick"/> (later than <see cref="Now"/>). Needs <see cref="CapabilityRights.Phase"/>.</summary>
    /// <param name="dueTick">The tick at which it clears.</param>
    /// <returns>The event; await <see cref="DeferredEvent.WaitClearedAsync(CancellationToken)"/>.</returns>
    public DeferredEvent Defer(ulong dueTick)
    {
        Enter("PhaseClock.Defer");
        _cap.Check(NativeMethods.PhaseDefer(_cap.Handle, _handle, dueTick, out ulong id), "PhaseClock.Defer");
        var ev = new DeferredEvent(this, id, dueTick);
        lock (_gate)
        {
            _waiting[id] = ev;
        }

        // Another thread may have advanced past the tick before the event was registered.
        DeferredEventState state = QueryState(id);
        if (state != DeferredEventState.Pending)
        {
            lock (_gate)
            {
                _waiting.Remove(id);
            }

            ev.Complete(state);
        }

        return ev;
    }

    /// <summary>
    /// Starts a real-time driver: a background <see cref="PeriodicTimer"/>
    /// loop that advances the clock at <paramref name="ticksPerSecond"/>
    /// (computed from elapsed time, so it does not drift) and completes waiters.
    /// </summary>
    /// <param name="ticksPerSecond">Clock rate; e.g. <see cref="TicksPerCycle"/> for one fundamental per second.</param>
    /// <param name="period">How often to advance; default 10 ms.</param>
    /// <returns>The driver; dispose it (or call <see cref="RealtimeDriver.StopAsync"/>) to stop.</returns>
    public RealtimeDriver StartRealtime(ulong ticksPerSecond, TimeSpan? period = null)
    {
        ArgumentOutOfRangeException.ThrowIfZero(ticksPerSecond);
        Enter("PhaseClock.StartRealtime");
        var driver = new RealtimeDriver(this, ticksPerSecond, period ?? TimeSpan.FromMilliseconds(10));
        if (Interlocked.CompareExchange(ref _driver, driver, null) is not null)
        {
            driver.Dispose();
            throw new InvalidOperationException("A real-time driver is already running on this clock.");
        }

        driver.Start();
        return driver;
    }

    /// <summary>Stops any driver, fails pending waiters with <see cref="ObjectDisposedException"/>, and releases the clock.</summary>
    public void Dispose()
    {
        _driver?.Dispose();
        _handle.Dispose();
        DeferredEvent[] pending;
        lock (_gate)
        {
            pending = new DeferredEvent[_waiting.Count];
            _waiting.Values.CopyTo(pending, 0);
            _waiting.Clear();
        }

        foreach (DeferredEvent ev in pending)
        {
            ev.Fail(new ObjectDisposedException(nameof(PhaseClock)));
        }
    }

    internal DeferredEventState QueryState(ulong id)
    {
        Enter("DeferredEvent.State");
        _cap.Check(NativeMethods.PhaseEvent(_handle, id, out int state, out _), "DeferredEvent.State");
        return (DeferredEventState)state;
    }

    internal void Cancel(DeferredEvent ev)
    {
        Enter("DeferredEvent.Cancel");
        _cap.Check(NativeMethods.PhaseCancel(_cap.Handle, _handle, ev.Id), "DeferredEvent.Cancel");
        lock (_gate)
        {
            _waiting.Remove(ev.Id);
        }

        ev.Complete(DeferredEventState.Cancelled);
    }

    internal void DriverStopped(RealtimeDriver driver) => Interlocked.CompareExchange(ref _driver, null, driver);

    private void CompleteWaiters()
    {
        DeferredEvent[] pending;
        lock (_gate)
        {
            pending = new DeferredEvent[_waiting.Count];
            _waiting.Values.CopyTo(pending, 0);
        }

        foreach (DeferredEvent ev in pending)
        {
            DeferredEventState state = QueryState(ev.Id);
            if (state != DeferredEventState.Pending)
            {
                lock (_gate)
                {
                    _waiting.Remove(ev.Id);
                }

                ev.Complete(state);
            }
        }
    }

    private void Enter(string operation)
    {
        ObjectDisposedException.ThrowIf(_handle.IsClosed, this);
        _cap.Enter(operation);
    }

    /// <summary>
    /// Advances a <see cref="PhaseClock"/> in real time from a
    /// <see cref="PeriodicTimer"/> loop on the thread pool.
    /// </summary>
    public sealed class RealtimeDriver : IDisposable, IAsyncDisposable
    {
        private readonly PhaseClock _clock;
        private readonly ulong _ticksPerSecond;
        private readonly PeriodicTimer _timer;
        private readonly CancellationTokenSource _stop = new();
        private Task _loop = Task.CompletedTask;

        internal RealtimeDriver(PhaseClock clock, ulong ticksPerSecond, TimeSpan period)
        {
            _clock = clock;
            _ticksPerSecond = ticksPerSecond;
            _timer = new PeriodicTimer(period);
        }

        /// <summary>Gets the exception that stopped the loop, if any (for example a revoked capability).</summary>
        public Exception? Fault { get; private set; }

        /// <summary>Stops the loop and waits for it to finish.</summary>
        /// <returns>A task that completes when stopped.</returns>
        public async Task StopAsync()
        {
            await _stop.CancelAsync().ConfigureAwait(false);
            _timer.Dispose();
            await _loop.ConfigureAwait(false);
        }

        /// <inheritdoc/>
        public void Dispose()
        {
            _stop.Cancel();
            _timer.Dispose();
            try
            {
                _loop.GetAwaiter().GetResult();
            }
            finally
            {
                _stop.Dispose();
            }
        }

        /// <inheritdoc/>
        public async ValueTask DisposeAsync()
        {
            await StopAsync().ConfigureAwait(false);
            _stop.Dispose();
        }

        internal void Start() => _loop = Task.Run(RunAsync);

#pragma warning disable CA1031 // The loop records its fault instead of crashing the thread pool.
        private async Task RunAsync()
        {
            long start = Stopwatch.GetTimestamp();
            ulong advanced = 0;
            try
            {
                while (await _timer.WaitForNextTickAsync(_stop.Token).ConfigureAwait(false))
                {
                    TimeSpan elapsed = Stopwatch.GetElapsedTime(start);
                    var target = (ulong)(elapsed.Ticks / TimeSpan.TicksPerMillisecond) * _ticksPerSecond / 1000UL;
                    if (target > advanced)
                    {
                        _clock.Advance(target - advanced);
                        advanced = target;
                    }
                }
            }
            catch (OperationCanceledException)
            {
            }
            catch (Exception ex)
            {
                Fault = ex;
            }
            finally
            {
                _clock.DriverStopped(this);
            }
        }
#pragma warning restore CA1031
    }
}

/// <summary>
/// An S0 event parked until a tick (<see cref="PhaseClock.Defer(ulong)"/>).
/// </summary>
public sealed class DeferredEvent
{
    private readonly PhaseClock _clock;
    private readonly TaskCompletionSource<DeferredEventState> _done = new(TaskCreationOptions.RunContinuationsAsynchronously);

    internal DeferredEvent(PhaseClock clock, ulong id, ulong dueTick)
    {
        _clock = clock;
        Id = id;
        DueTick = dueTick;
    }

    /// <summary>Gets the native event id.</summary>
    public ulong Id { get; }

    /// <summary>Gets the tick at which it clears.</summary>
    public ulong DueTick { get; }

    /// <summary>Gets the current state.</summary>
    public DeferredEventState State => _done.Task.IsCompletedSuccessfully ? _done.Task.Result : _clock.QueryState(Id);

    /// <summary>Waits until the clock passes <see cref="DueTick"/>.</summary>
    /// <param name="cancellationToken">Cancels the wait (not the event).</param>
    /// <returns>A task that completes when the event clears.</returns>
    /// <exception cref="OperationCanceledException">The event was cancelled with <see cref="Cancel"/>, or the wait was cancelled.</exception>
    public async Task WaitClearedAsync(CancellationToken cancellationToken = default)
    {
        DeferredEventState state = await _done.Task.WaitAsync(cancellationToken).ConfigureAwait(false);
        if (state == DeferredEventState.Cancelled)
        {
            throw new OperationCanceledException($"Deferred event {Id} was cancelled.");
        }
    }

    /// <summary>Cancels the event before its tick. Needs <see cref="CapabilityRights.Phase"/>.</summary>
    public void Cancel() => _clock.Cancel(this);

    internal void Complete(DeferredEventState state) => _done.TrySetResult(state);

    internal void Fail(Exception error) => _done.TrySetException(error);
}
