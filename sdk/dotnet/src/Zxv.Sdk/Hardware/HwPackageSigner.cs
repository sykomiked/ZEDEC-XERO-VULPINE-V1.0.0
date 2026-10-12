// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0

using System;
using System.Collections.Generic;
using Zxv.Interop;

namespace Zxv.Hardware;

/// <summary>
/// Release keys and signed packages for hardware updates (ABI 1.4). Every
/// package is <c>header || payload || signatures</c>, signed with ML-DSA-87
/// AND SLH-DSA-SHAKE-256s; a region or matrix accepts it only if both verify
/// under a key it trusts. Needs <see cref="CapabilityRights.CryptoSign"/>.
/// SLH-DSA signing takes seconds: building a package is a release step.
/// Thread-safe.
/// </summary>
public sealed class HwPackageSigner
{
    /// <summary>Release public key length: the ML-DSA-87 key followed by the SLH-DSA one.</summary>
    public const int PublicKeyBytes = 2656;

    /// <summary>ML-DSA-87 secret key length.</summary>
    public const int MlDsaSecretKeyBytes = 4896;

    /// <summary>SLH-DSA-SHAKE-256s secret key length.</summary>
    public const int SlhDsaSecretKeyBytes = 128;

    /// <summary>Length of both signatures.</summary>
    public const int SignatureBytes = 34419;

    /// <summary>Package header length.</summary>
    public const int HeaderBytes = 96;

    private readonly Capability _cap;

    /// <summary>Initializes a signer that presents <paramref name="capability"/> on every call.</summary>
    /// <param name="capability">Needs <see cref="CapabilityRights.CryptoSign"/>.</param>
    public HwPackageSigner(Capability capability)
    {
        ArgumentNullException.ThrowIfNull(capability);
        _cap = capability;
    }

    /// <summary>Generates a release key pair. Both secret keys live off the managed heap and are wiped on dispose.</summary>
    /// <returns>The pair; dispose it when done.</returns>
    public unsafe HwReleaseKey GenerateKey()
    {
        _cap.Enter("HwPackageSigner.GenerateKey");
        var pk = new byte[PublicKeyBytes];
        SecretKey sk = SecretKey.Allocate(MlDsaSecretKeyBytes, SecretKeyKind.MlDsa87);
        SecretKey ssk = SecretKey.Allocate(SlhDsaSecretKeyBytes, SecretKeyKind.SlhDsaShake256s);
        try
        {
            sk.Use(s => ssk.Use(ss =>
            {
                fixed (byte* p = pk)
                {
                    _cap.Check(NativeMethods.HwKeygen(_cap.Handle, p, s, ss), "HwPackageSigner.GenerateKey");
                }
            }));
            return new HwReleaseKey(pk, sk, ssk);
        }
        catch
        {
            sk.Dispose();
            ssk.Dispose();
            throw;
        }
    }

    /// <summary>Builds and signs an image for a region.</summary>
    /// <param name="kind">What the payload is (not <see cref="HwImageKind.Vectors"/>; use <see cref="BuildContract"/>).</param>
    /// <param name="targetClass">The hardware class it is for.</param>
    /// <param name="targetDigest">The exact-part digest (32 bytes), or empty for none.</param>
    /// <param name="functionId">The function the region will serve.</param>
    /// <param name="version">Strictly increasing per region.</param>
    /// <param name="svn">The anti-rollback floor it raises to on commit.</param>
    /// <param name="payload">The image bytes.</param>
    /// <param name="key">The release key.</param>
    /// <returns>The package.</returns>
    public byte[] BuildImage(HwImageKind kind, uint targetClass, ReadOnlySpan<byte> targetDigest, uint functionId, uint version, uint svn, ReadOnlySpan<byte> payload, HwReleaseKey key) =>
        Build((uint)kind, targetClass, targetDigest, functionId, version, svn, payload, key);

    /// <summary>
    /// Builds and signs a contract package (<see cref="HwImageKind.Vectors"/>):
    /// a newer golden-vector set for a function, applied with
    /// <see cref="HwMatrix.ApplyContract"/>.
    /// </summary>
    /// <param name="functionId">The function.</param>
    /// <param name="version">The contract version (strictly newer than the matrix's).</param>
    /// <param name="svn">The function's new anti-rollback floor.</param>
    /// <param name="vectors">1..<see cref="HwVector.MaxVectors"/> vectors.</param>
    /// <param name="key">The release key.</param>
    /// <returns>The package.</returns>
    public byte[] BuildContract(uint functionId, uint version, uint svn, IReadOnlyList<HwVector> vectors, HwReleaseKey key) =>
        Build((uint)HwImageKind.Vectors, 0, ReadOnlySpan<byte>.Empty, functionId, version, svn, HwVector.EncodeList(vectors), key);

    /// <summary>Requires an exact length.</summary>
    internal static void RequireLength(ReadOnlySpan<byte> value, int length, string paramName)
    {
        if (value.Length != length)
        {
            throw new ArgumentException($"Expected {length} bytes, got {value.Length}.", paramName);
        }
    }

    private unsafe byte[] Build(uint kind, uint targetClass, ReadOnlySpan<byte> targetDigest, uint functionId, uint version, uint svn, ReadOnlySpan<byte> payload, HwReleaseKey key)
    {
        ArgumentNullException.ThrowIfNull(key);
        if (!targetDigest.IsEmpty)
        {
            RequireLength(targetDigest, 32, nameof(targetDigest));
        }

        key.MlDsaSecretKey.RequireLength(MlDsaSecretKeyBytes, nameof(key));
        key.SlhDsaSecretKey.RequireLength(SlhDsaSecretKeyBytes, nameof(key));
        _cap.Enter("HwPackageSigner.Build");
        byte[]? package = null;
        fixed (byte* td = targetDigest)
        fixed (byte* pl = payload)
        {
            byte* tdp = targetDigest.IsEmpty ? null : td, plp = pl;
            nuint plen = (nuint)payload.Length;
            key.MlDsaSecretKey.Use(s => key.SlhDsaSecretKey.Use(ss =>
            {
                // R4: the first call only sizes the package (nothing is signed).
                int rc = NativeMethods.HwImageBuild(_cap.Handle, kind, targetClass, tdp, functionId, version, svn, plp, plen, s, ss, null, 0, out nuint need);
                if (rc != (int)ZxvStatus.BufferTooSmall)
                {
                    _cap.Check(rc, "HwPackageSigner.Build");
                }

                var buf = new byte[checked((int)need)];
                fixed (byte* b = buf)
                {
                    _cap.Check(NativeMethods.HwImageBuild(_cap.Handle, kind, targetClass, tdp, functionId, version, svn, plp, plen, s, ss, b, need, out need), "HwPackageSigner.Build");
                }

                package = buf.AsSpan(0, checked((int)need)).ToArray();
            }));
        }

        return package!;
    }
}

/// <summary>
/// A hardware release key pair: the public key to trust on regions and
/// matrices, and two off-heap secret keys wiped on <see cref="Dispose"/>.
/// </summary>
public sealed class HwReleaseKey : IDisposable
{
    private readonly byte[] _publicKey;

    internal HwReleaseKey(byte[] publicKey, SecretKey mlDsa, SecretKey slhDsa)
    {
        _publicKey = publicKey;
        MlDsaSecretKey = mlDsa;
        SlhDsaSecretKey = slhDsa;
    }

    /// <summary>Gets the public key (<see cref="HwPackageSigner.PublicKeyBytes"/> bytes).</summary>
    public ReadOnlyMemory<byte> PublicKey => _publicKey;

    /// <summary>Gets the ML-DSA-87 secret key.</summary>
    public SecretKey MlDsaSecretKey { get; }

    /// <summary>Gets the SLH-DSA-SHAKE-256s secret key.</summary>
    public SecretKey SlhDsaSecretKey { get; }

    /// <summary>Wipes and frees both secret keys.</summary>
    public void Dispose()
    {
        MlDsaSecretKey.Dispose();
        SlhDsaSecretKey.Dispose();
    }
}
