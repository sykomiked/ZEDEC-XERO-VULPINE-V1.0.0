// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0

using System;
using Zxv.Interop;

namespace Zxv;

/// <summary>What an attestation is about (the kernel's <c>lpres_subject_type_t</c>).</summary>
public enum AttestationSubject
{
    /// <summary>Source code.</summary>
    Source = 0,

    /// <summary>A language dialect.</summary>
    Dialect = 1,

    /// <summary>A dependency.</summary>
    Dependency = 2,

    /// <summary>A runtime.</summary>
    Runtime = 3,

    /// <summary>A data set.</summary>
    Data = 4,

    /// <summary>A symbol.</summary>
    Symbol = 5,

    /// <summary>A model pack (e.g. a GGUF file).</summary>
    ModelPack = 6,

    /// <summary>Geometry.</summary>
    Geometry = 7,

    /// <summary>A parameter.</summary>
    Parameter = 8,

    /// <summary>A sensor.</summary>
    Sensor = 9,

    /// <summary>An actuator.</summary>
    Actuator = 10,

    /// <summary>A calibration.</summary>
    Calibration = 11,

    /// <summary>A file or wire format.</summary>
    Format = 12,

    /// <summary>A key.</summary>
    Key = 13,

    /// <summary>Firmware.</summary>
    Firmware = 14,
}

/// <summary>The state of one attestation.</summary>
/// <param name="Truth">Its four-valued state.</param>
/// <param name="Positive">Pieces of evidence for (an initial TRUE attestation counts as one).</param>
/// <param name="Negative">Pieces of evidence against.</param>
public readonly record struct AttestationState(Truth Truth, uint Positive, uint Negative);

/// <summary>
/// An attestation registry (the kernel's <c>lpres</c>): named claims about
/// sources, models, sensors, keys and firmware, each a four-valued
/// <see cref="Truth"/> that follows its evidence. Conflicting evidence is held
/// as <see cref="Truth.Both"/>, never collapsed. Create one with
/// <see cref="ZxvPlatform.CreateAttestations(Capability)"/>. Thread-safe.
/// </summary>
/// <remarks>
/// <see cref="IsGateClear"/> is the actuator gate: it is clear only when every
/// attestation is certain (TRUE or FALSE), with no contradiction and nothing
/// unknown. Actuator authority requires a clear gate.
/// </remarks>
public sealed class Attestations : IDisposable
{
    private readonly Capability _cap;
    private readonly ZxvAttestationsHandle _handle;

    internal Attestations(Capability capability)
    {
        _cap = capability;
        capability.Enter("Attestations.Create");
        int rc = NativeMethods.LpresCreateCap(capability.Handle, out _handle);
        if (rc != (int)ZxvStatus.Ok)
        {
            _handle.Dispose();
            capability.Check(rc, "Attestations.Create");
        }

        _handle.AddDependency(capability.Context.Handle);
    }

    /// <summary>
    /// Gets a value indicating whether every attestation is certain (TRUE or
    /// FALSE): no <see cref="Truth.Both"/>, no <see cref="Truth.Neither"/>.
    /// </summary>
    public bool IsGateClear
    {
        get
        {
            ObjectDisposedException.ThrowIf(_handle.IsClosed, this);
            _cap.Check(NativeMethods.LpresGate(_handle, out int clear), "Attestations.IsGateClear");
            return clear != 0;
        }
    }

    /// <summary>Records an attestation. Needs <see cref="CapabilityRights.Attest"/>.</summary>
    /// <param name="name">Name of the subject (at most 31 bytes of UTF-8).</param>
    /// <param name="subject">What kind of subject it is.</param>
    /// <param name="state">The attested state.</param>
    /// <param name="attester">Who attests (at most 31 bytes).</param>
    /// <param name="digest">Up to 32 bytes of digest of the subject; empty for none.</param>
    /// <returns>The attestation's index.</returns>
    public unsafe int Attest(string name, AttestationSubject subject, Truth state, string attester, ReadOnlySpan<byte> digest = default)
    {
        Guard.Text(name, nameof(name));
        Guard.Text(attester, nameof(attester));
        CryptoGuard.MaxLength(digest, 32, nameof(digest));
        Enter("Attestations.Attest");
        fixed (byte* d = digest)
        {
            int rc = NativeMethods.LpresAttest(_cap.Handle, _handle, name, (int)subject, (int)state, attester, digest.IsEmpty ? null : d, (uint)digest.Length, out uint index);
            _cap.Check(rc, "Attestations.Attest");
            return checked((int)index);
        }
    }

    /// <summary>Adds one piece of evidence; the state then follows <see cref="TruthExtensions.FromEvidence(uint, uint)"/>.</summary>
    /// <param name="index">The attestation index.</param>
    /// <param name="positive"><see langword="true"/> for evidence for; <see langword="false"/> for evidence against.</param>
    public void AddEvidence(int index, bool positive)
    {
        ArgumentOutOfRangeException.ThrowIfNegative(index);
        Enter("Attestations.AddEvidence");
        _cap.Check(NativeMethods.LpresEvidence(_cap.Handle, _handle, (uint)index, positive ? 1 : 0), "Attestations.AddEvidence");
    }

    /// <summary>Finds an attestation by name.</summary>
    /// <param name="name">The name.</param>
    /// <returns>Its index, or <see langword="null"/> when there is none.</returns>
    public int? Find(string name)
    {
        Guard.Text(name, nameof(name));
        Enter("Attestations.Find");
        int rc = NativeMethods.LpresFind(_handle, name, out uint index);
        if (rc == (int)ZxvStatus.NotFound)
        {
            return null;
        }

        _cap.Check(rc, "Attestations.Find");
        return checked((int)index);
    }

    /// <summary>Reads an attestation's state.</summary>
    /// <param name="index">The attestation index.</param>
    /// <returns>Its truth and evidence counts.</returns>
    public AttestationState GetState(int index)
    {
        ArgumentOutOfRangeException.ThrowIfNegative(index);
        Enter("Attestations.GetState");
        _cap.Check(NativeMethods.LpresState(_handle, (uint)index, out int st, out uint pos, out uint neg), "Attestations.GetState");
        return new AttestationState((Truth)st, pos, neg);
    }

    /// <summary>Releases the registry.</summary>
    public void Dispose() => _handle.Dispose();

    private void Enter(string operation)
    {
        ObjectDisposedException.ThrowIf(_handle.IsClosed, this);
        _cap.Enter(operation);
    }
}
