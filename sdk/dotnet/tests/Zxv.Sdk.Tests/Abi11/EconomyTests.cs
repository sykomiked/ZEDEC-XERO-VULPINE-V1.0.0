// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0

using System;
using System.Collections.Generic;
using System.Text;
using System.Text.Json;
using System.Threading;
using System.Threading.Tasks;
using Zxv.Interop;

namespace Zxv.Sdk.Tests.Abi11;

/// <summary>Budgets (sync and async), phase ticks and the swarm economy.</summary>
public sealed class EconomyTests : IDisposable
{
    private readonly ZxvPlatform _p = ZxvPlatform.Create();
    private readonly Capability _cap;

    public EconomyTests()
    {
        _cap = _p.CreateRootCapability();
    }

    public void Dispose()
    {
        _cap.Dispose();
        _p.Dispose();
    }

    [Fact]
    public void Budget_consumes_exactly_and_refuses_past_the_limit()
    {
        using Budgets budgets = _p.CreateBudgets(_cap);
        var events = new List<BudgetEventKind>();
        budgets.Exhausted += (_, e) => events.Add(e.Kind);
        budgets.Replenished += (_, e) => events.Add(e.Kind);
        budgets.InvalidAmount += (_, e) => events.Add(e.Kind);
        Budget b = budgets.Create("tokens", BudgetResource.Tokens, 10);

        b.Consume(Rational.Of(7, 2));
        b.Consume(Rational.Of(13, 2));
        Assert.Throws<BudgetExhaustedException>(() => b.Consume(Rational.Of(1, 3)));
        Assert.False(b.TryConsume(Rational.Of(1, 3)));
        Assert.Equal(ZxvStatus.BudgetAmountInvalid, Assert.ThrowsAny<ZxvUsageException>(() => b.Consume(-1)).Status);
        Assert.Equal(new BudgetStatus(Rational.Zero, Rational.Of(10), 0), b.GetStatus());

        b.Release(Rational.Of(1, 2));
        Assert.Equal(new BudgetStatus(Rational.Of(1, 2), Rational.Of(19, 2), 1), b.GetStatus());
        Assert.Equal(new[] { BudgetEventKind.Denied, BudgetEventKind.Denied, BudgetEventKind.Invalid, BudgetEventKind.Replenished }, events);

        using Capability noBudget = _cap.Attenuate(CapabilityRights.LedgerRead);
        Assert.Throws<CapabilityDeniedException>(() => _p.CreateBudgets(noBudget)); // ABI 1.2: creating needs Budget
        using Capability budget = _cap.Attenuate(CapabilityRights.Budget);
        using Budgets denied = _p.CreateBudgets(budget);
        budget.Revoke();
        Assert.Throws<CapabilityDeniedException>(() => denied.Create("x", BudgetResource.Energy, 1));
    }

    [Fact]
    public async Task ConsumeAsync_waits_for_replenishment()
    {
        using Budgets budgets = _p.CreateBudgets(_cap);
        Budget b = budgets.Create("gpu", BudgetResource.ComputeCycles, 3);
        await b.ConsumeAsync(3); // fits: completes at once

        Task waiting = b.ConsumeAsync(2);
        await Task.Delay(50);
        Assert.False(waiting.IsCompleted);
        b.Release(1);                 // 1 free: not enough, keeps waiting
        await Task.Delay(50);
        Assert.False(waiting.IsCompleted);
        b.SetLimit(4);                // 2 free now
        await waiting.WaitAsync(TimeSpan.FromSeconds(5));
        Assert.Equal(Rational.Zero, b.Remaining);
    }

    [Fact]
    public async Task ConsumeAsync_times_out_cancels_and_fails_fast_on_invalid()
    {
        using Budgets budgets = _p.CreateBudgets(_cap);
        Budget b = budgets.Create("io", BudgetResource.Bandwidth, 1);
        b.Consume(1);
        await Assert.ThrowsAsync<TimeoutException>(() => b.ConsumeAsync(1, TimeSpan.FromMilliseconds(50)));
        using var cts = new CancellationTokenSource(50);
        await Assert.ThrowsAnyAsync<OperationCanceledException>(() => b.ConsumeAsync(1, cts.Token));
        await Assert.ThrowsAnyAsync<ZxvUsageException>(() => b.ConsumeAsync(-5));

        Task pending = b.ConsumeAsync(1);
        budgets.Dispose();
        await Assert.ThrowsAsync<ObjectDisposedException>(() => pending);
    }

    [Fact]
    public async Task Phase_clock_defers_until_its_tick()
    {
        using PhaseClock clock = _p.CreatePhaseClock(_cap);
        Assert.Equal(0ul, clock.Now);
        Assert.Null(clock.NextDue);
        DeferredEvent ev = clock.Defer(PhaseClock.TicksPerCycle);
        Assert.Equal(PhaseClock.TicksPerCycle, clock.NextDue);
        Task cleared = ev.WaitClearedAsync();

        Assert.Equal(0, clock.Advance(PhaseClock.TicksPerCycle - 1));
        Assert.Equal(DeferredEventState.Pending, ev.State);
        Assert.False(cleared.IsCompleted);
        Assert.Equal(1, clock.Advance(1));
        await cleared.WaitAsync(TimeSpan.FromSeconds(5));
        Assert.Equal(DeferredEventState.Cleared, ev.State);
        Assert.Equal(27720ul, clock.Now);

        Assert.Equal(new PhaseAdmission(PhaseDecision.Defer, 0), clock.Admit(6, 1, PhaseStepRequirements.S0Pending));
        clock.SetHealth(6, PhaseHealth.Healthy);
        PhaseAdmission ok = clock.Admit(6, 2, PhaseStepRequirements.RequiresHealth);
        Assert.True(ok.IsAdmitted);
        Assert.NotEqual(0ul, ok.Token);

        DeferredEvent cancelled = clock.Defer(clock.Now + 10);
        cancelled.Cancel();
        await Assert.ThrowsAnyAsync<OperationCanceledException>(() => cancelled.WaitClearedAsync());

        using Capability noPhase = _cap.Attenuate(CapabilityRights.Budget);
        Assert.Throws<CapabilityDeniedException>(() => _p.CreatePhaseClock(noPhase)); // ABI 1.2: creating needs Phase
        using Capability phase = _cap.Attenuate(CapabilityRights.Phase);
        using PhaseClock locked = _p.CreatePhaseClock(phase);
        phase.Revoke();
        Assert.Throws<CapabilityDeniedException>(() => locked.Advance(1));
    }

    [Fact]
    public async Task Phase_clock_realtime_driver_clears_events()
    {
        using PhaseClock clock = _p.CreatePhaseClock(_cap);
        DeferredEvent ev = clock.Defer(clock.Now + (PhaseClock.TicksPerCycle / 20)); // 50 ms at one cycle per second
        await using (PhaseClock.RealtimeDriver driver = clock.StartRealtime(PhaseClock.TicksPerCycle))
        {
            Assert.True(clock.IsRealtime);
            Assert.Throws<InvalidOperationException>(() => clock.StartRealtime(1));
            await ev.WaitClearedAsync().WaitAsync(TimeSpan.FromSeconds(10));
            Assert.Null(driver.Fault);
        }

        Assert.False(clock.IsRealtime);
        Assert.True(clock.Now >= PhaseClock.TicksPerCycle / 20);
    }

    [Fact]
    public void Swarm_allotments_match_vectors()
    {
        JsonElement v = Conformance.Vectors.GetProperty("swarm");
        using Swarm swarm = _p.CreateSwarm(_cap, v.GetProperty("levels").GetInt32(), v.GetProperty("tokens_per_cycle").GetUInt64());
        foreach (JsonElement m in v.GetProperty("models").EnumerateArray())
        {
            swarm.Register(m[0].GetUInt32(), m[1].GetInt32());
        }

        JsonElement mood = v.GetProperty("mood");
        swarm.SetMood(new Rune(mood[0].GetInt32()), mood[1].GetInt32());
        foreach (JsonElement f in v.GetProperty("feelings").EnumerateArray())
        {
            swarm.SetFeeling(f[0].GetUInt32(), new Rune(f[1].GetInt32()), f[2].GetInt32());
        }

        swarm.BeginCycle();
        ulong total = 0;
        foreach (JsonElement a in v.GetProperty("allotments").EnumerateArray())
        {
            SwarmAllotment got = swarm.GetAllotment(a[0].GetUInt32());
            Assert.Equal(a[1].GetUInt64(), got.Allotted);
            Assert.Equal(a[2].GetUInt64(), got.Imaginary);
            total += got.Allotted;
        }

        Assert.Equal(v.GetProperty("tokens_per_cycle").GetUInt64(), total);
        int level = 0;
        foreach (JsonElement c in v.GetProperty("level_capacity").EnumerateArray())
        {
            Assert.Equal(c.GetInt32(), Swarm.LevelCapacity(level++));
        }

        SwarmAllotment first = swarm.GetAllotment(100);
        Assert.False(swarm.Consume(100, 10).IsPartial);
        SwarmGrant rest = swarm.Consume(100, first.Allotted);
        Assert.True(rest.IsPartial);
        Assert.Equal(first.Allotted - 10, rest.Granted);
        Assert.True(swarm.EndCycle() > 0);
    }

    [Fact]
    public void Swarm_refuses_full_levels_and_unknown_emotions()
    {
        using Swarm swarm = _p.CreateSwarm(_cap, 3, 1000);
        swarm.Register(1, 0);
        Assert.Equal(ZxvStatus.CapacityExceeded, Assert.ThrowsAny<ZxvRefusedException>(() => swarm.Register(4, 0)).Status);
        Assert.Equal(ZxvStatus.NotFound, Assert.ThrowsAny<ZxvUsageException>(() => swarm.SetMood(new Rune(0x1F600), 1)).Status);
        EmotionProfile thinking = Swarm.GetEmotionProfile(new Rune(0x1F914));
        Assert.Equal(Rational.Of(thinking.TemperatureMilli, 1000), thinking.Temperature);
    }
}
