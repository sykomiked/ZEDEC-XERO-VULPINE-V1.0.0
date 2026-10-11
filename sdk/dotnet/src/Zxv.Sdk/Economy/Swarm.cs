// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0

using System;
using System.Text;
using Zxv.Interop;

namespace Zxv;

/// <summary>One model's share of the current cycle.</summary>
/// <param name="Allotted">Tokens allotted this cycle: the real (Fibonacci level) share plus the imaginary (emotional) share.</param>
/// <param name="Imaginary">The imaginary part of <paramref name="Allotted"/>.</param>
/// <param name="Used">Tokens consumed so far this cycle.</param>
public readonly record struct SwarmAllotment(ulong Allotted, ulong Imaginary, ulong Used)
{
    /// <summary>Gets the real (level) part of the allotment.</summary>
    public ulong Real => Allotted - Imaginary;

    /// <summary>Gets the tokens still available this cycle.</summary>
    public ulong Remaining => Allotted > Used ? Allotted - Used : 0;
}

/// <summary>The outcome of <see cref="Swarm.Consume(uint, ulong)"/>.</summary>
/// <param name="Requested">Tokens asked for.</param>
/// <param name="Granted">Tokens granted (and consumed); less than requested when the allotment ran out.</param>
public readonly record struct SwarmGrant(ulong Requested, ulong Granted)
{
    /// <summary>Gets a value indicating whether fewer tokens were granted than requested.</summary>
    public bool IsPartial => Granted < Requested;
}

/// <summary>How an emotion steers a model (the kernel palette's behaviour profile).</summary>
/// <param name="Valence">Negative, zero or positive valence.</param>
/// <param name="TemperatureMilli">Sampling temperature in thousandths.</param>
/// <param name="ExplorePermille">Exploration rate in thousandths.</param>
/// <param name="VerifyPasses">How many verification passes it asks for.</param>
/// <param name="Priority">Scheduling priority.</param>
public readonly record struct EmotionProfile(int Valence, uint TemperatureMilli, uint ExplorePermille, uint VerifyPasses, uint Priority)
{
    /// <summary>Gets the sampling temperature as an exact rational.</summary>
    public Rational Temperature => Rational.Of(TemperatureMilli, 1000);
}

/// <summary>
/// The swarm token economy (the kernel's swarm budget and emotional economy).
/// Each cycle, <c>T</c> tokens are split across the swarm's Fibonacci levels
/// (the real axis) and, by the mood's charge, an imaginary (emotional) share
/// is split by each model's own feeling. All exact integers; unused tokens
/// expire at the end of a cycle. Create one with
/// <see cref="ZxvPlatform.CreateSwarm(Capability, int, ulong)"/>. Thread-safe.
/// </summary>
/// <remarks>
/// Feelings are emoji code points from the kernel palette (for example
/// U+1F914, U+1F604) with intensity 0..5 (charge 0, 1, 2, 3, 5, 8). Pass them as
/// <see cref="Rune"/>: <c>new Rune(0x1F914)</c> or <c>Rune.GetRuneAt("🤔", 0)</c>.
/// </remarks>
public sealed class Swarm : IDisposable
{
    private readonly Capability _cap;
    private readonly ZxvSwarmHandle _handle;

    internal Swarm(Capability capability, int levels, ulong tokensPerCycle)
    {
        ArgumentOutOfRangeException.ThrowIfNegativeOrZero(levels);
        _cap = capability;
        capability.Enter("Swarm.Create");
        int rc = NativeMethods.SwarmCreateCap(capability.Handle, (uint)levels, tokensPerCycle, out _handle);
        if (rc != (int)ZxvStatus.Ok)
        {
            _handle.Dispose();
            capability.Check(rc, "Swarm.Create");
        }

        _handle.AddDependency(capability.Context.Handle);
    }

    /// <summary>Gets how many models level <paramref name="level"/> (0-based) can hold: F(level + 2).</summary>
    /// <param name="level">The level.</param>
    /// <returns>The capacity.</returns>
    public static int LevelCapacity(int level)
    {
        ArgumentOutOfRangeException.ThrowIfNegative(level);
        NativeCall.Check(NativeMethods.SwarmLevelCapacity((uint)level, out uint capacity), "Swarm.LevelCapacity");
        return checked((int)capacity);
    }

    /// <summary>Gets the behaviour profile of an emotion of the palette.</summary>
    /// <param name="emotion">The emoji.</param>
    /// <returns>The profile.</returns>
    /// <exception cref="ZxvUsageException">The emoji is not in the palette (<see cref="ZxvStatus.NotFound"/>).</exception>
    public static EmotionProfile GetEmotionProfile(Rune emotion)
    {
        NativeCall.Check(
            NativeMethods.SwarmEmotionProfile((uint)emotion.Value, out int valence, out uint temp, out uint explore, out uint verify, out uint priority),
            "Swarm.GetEmotionProfile");
        return new EmotionProfile(valence, temp, explore, verify, priority);
    }

    /// <summary>Registers a model at a level. Needs <see cref="CapabilityRights.Swarm"/>.</summary>
    /// <param name="modelId">The model's id.</param>
    /// <param name="level">The 0-based level.</param>
    /// <exception cref="ZxvRefusedException">The level is full (<see cref="ZxvStatus.CapacityExceeded"/>).</exception>
    public void Register(uint modelId, int level)
    {
        ArgumentOutOfRangeException.ThrowIfNegative(level);
        Enter("Swarm.Register");
        _cap.Check(NativeMethods.SwarmRegister(_cap.Handle, _handle, modelId, (uint)level), "Swarm.Register");
    }

    /// <summary>Activates or pauses a model.</summary>
    /// <param name="modelId">The model.</param>
    /// <param name="active">Whether it takes part in the next cycles.</param>
    public void SetActive(uint modelId, bool active)
    {
        Enter("Swarm.SetActive");
        _cap.Check(NativeMethods.SwarmSetActive(_cap.Handle, _handle, modelId, active ? 1 : 0), "Swarm.SetActive");
    }

    /// <summary>Changes the tokens shared out per cycle.</summary>
    /// <param name="tokensPerCycle">Tokens per cycle.</param>
    public void SetRate(ulong tokensPerCycle)
    {
        Enter("Swarm.SetRate");
        _cap.Check(NativeMethods.SwarmSetRate(_cap.Handle, _handle, tokensPerCycle), "Swarm.SetRate");
    }

    /// <summary>Sets the swarm's mood, whose charge sizes the imaginary share.</summary>
    /// <param name="emotion">An emoji of the palette.</param>
    /// <param name="intensity">0..5.</param>
    public void SetMood(Rune emotion, int intensity)
    {
        ArgumentOutOfRangeException.ThrowIfNegative(intensity);
        Enter("Swarm.SetMood");
        _cap.Check(NativeMethods.SwarmSetMood(_cap.Handle, _handle, (uint)emotion.Value, (uint)intensity), "Swarm.SetMood");
    }

    /// <summary>Sets one model's feeling, which weights its imaginary share.</summary>
    /// <param name="modelId">The model.</param>
    /// <param name="emotion">An emoji of the palette.</param>
    /// <param name="intensity">0..5.</param>
    public void SetFeeling(uint modelId, Rune emotion, int intensity)
    {
        ArgumentOutOfRangeException.ThrowIfNegative(intensity);
        Enter("Swarm.SetFeeling");
        _cap.Check(NativeMethods.SwarmSetFeeling(_cap.Handle, _handle, modelId, (uint)emotion.Value, (uint)intensity), "Swarm.SetFeeling");
    }

    /// <summary>Starts a cycle: allots every token of the cycle.</summary>
    /// <returns>The cycle number.</returns>
    public ulong BeginCycle()
    {
        Enter("Swarm.BeginCycle");
        _cap.Check(NativeMethods.SwarmBeginCycle(_cap.Handle, _handle, out ulong cycle), "Swarm.BeginCycle");
        return cycle;
    }

    /// <summary>
    /// Consumes up to <paramref name="requested"/> tokens of a model's
    /// allotment. When the allotment runs short the rest is granted and
    /// <see cref="SwarmGrant.IsPartial"/> is set; this is not an exception.
    /// </summary>
    /// <param name="modelId">The model.</param>
    /// <param name="requested">Tokens wanted.</param>
    /// <returns>What was granted.</returns>
    public SwarmGrant Consume(uint modelId, ulong requested)
    {
        Enter("Swarm.Consume");
        int rc = NativeMethods.SwarmConsume(_cap.Handle, _handle, modelId, requested, out ulong granted);
        if (rc == (int)ZxvStatus.BudgetExhausted)
        {
            return new SwarmGrant(requested, granted);
        }

        _cap.Check(rc, "Swarm.Consume");
        return new SwarmGrant(requested, granted);
    }

    /// <summary>Reads a model's share of the current cycle.</summary>
    /// <param name="modelId">The model.</param>
    /// <returns>Allotted, imaginary and used tokens.</returns>
    public SwarmAllotment GetAllotment(uint modelId)
    {
        Enter("Swarm.GetAllotment");
        _cap.Check(NativeMethods.SwarmAllotment(_handle, modelId, out ulong allotted, out ulong imaginary, out ulong used), "Swarm.GetAllotment");
        return new SwarmAllotment(allotted, imaginary, used);
    }

    /// <summary>Closes the cycle.</summary>
    /// <returns>Tokens that went unused and expired (they never carry over).</returns>
    public ulong EndCycle()
    {
        Enter("Swarm.EndCycle");
        _cap.Check(NativeMethods.SwarmEndCycle(_cap.Handle, _handle, out ulong expired), "Swarm.EndCycle");
        return expired;
    }

    /// <summary>Releases the swarm.</summary>
    public void Dispose() => _handle.Dispose();

    private void Enter(string operation)
    {
        ObjectDisposedException.ThrowIf(_handle.IsClosed, this);
        _cap.Enter(operation);
    }
}
