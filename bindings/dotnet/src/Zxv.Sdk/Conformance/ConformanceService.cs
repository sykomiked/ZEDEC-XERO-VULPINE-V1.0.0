// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0

using Zxv.Interop;

namespace Zxv.Conformance;

/// <summary>The VSS conformance checks.</summary>
public enum VssCheck
{
    /// <summary>C1: every netting cycle nets to zero.</summary>
    Balance = 1,

    /// <summary>C2: every payment has a receipt.</summary>
    Receipts = 2,

    /// <summary>C3: message build, parse and rebuild is byte-identical.</summary>
    RoundTrip = 3,

    /// <summary>C4: settlement is backed by funded positions.</summary>
    Backing = 4,
}

/// <summary>Overall VSS conformance level.</summary>
public enum VssLevel
{
    /// <summary>No level reached.</summary>
    None = 0,

    /// <summary>Bronze.</summary>
    Bronze = 1,

    /// <summary>Silver.</summary>
    Silver = 2,

    /// <summary>Gold.</summary>
    Gold = 3,
}

/// <summary>Result of one conformance check.</summary>
/// <param name="Check">The check.</param>
/// <param name="Passed">Whether it passed.</param>
/// <param name="Detail">Diagnostic text.</param>
public sealed record ConformanceResult(VssCheck Check, bool Passed, string Detail);

/// <summary>
/// VSS conformance checks. Obtain it from <see cref="ZxvPlatform.Conformance"/>.
/// Pending kernel/src/cbank: throws <see cref="ZxvFeatureNotAvailableException"/>
/// until <see cref="ZxvFeatures.Conformance"/> is set. A conformance level is a
/// self-assessment, not a certification by any scheme.
/// </summary>
public sealed class ConformanceService
{
    private readonly ZxvContextHandle _ctx;

    internal ConformanceService(ZxvContextHandle ctx)
    {
        _ctx = ctx;
    }

    /// <summary>Runs one check.</summary>
    /// <param name="check">The check.</param>
    /// <returns>The result.</returns>
    public unsafe ConformanceResult Run(VssCheck check)
    {
        int passed = 0;
        string detail = NativeCall.ReadText((byte* b, nuint c, out nuint l) => NativeMethods.VssRun(_ctx, (int)check, out passed, b, c, out l), "Conformance.Run");
        return new ConformanceResult(check, passed != 0, detail);
    }

    /// <summary>Gets the overall conformance level.</summary>
    /// <returns>The level.</returns>
    public VssLevel GetLevel()
    {
        NativeCall.Check(NativeMethods.VssLevelGet(_ctx, out int level), "Conformance.GetLevel");
        return (VssLevel)level;
    }
}
