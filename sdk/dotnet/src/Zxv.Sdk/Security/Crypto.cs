// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0

using System;
using Zxv.Interop;

namespace Zxv;

/// <summary>A key pair: a public key as bytes and the secret key off-heap.</summary>
/// <param name="PublicKey">The public key (safe to share).</param>
/// <param name="SecretKey">The secret key; dispose it when done.</param>
public sealed record KeyPair(byte[] PublicKey, SecretKey SecretKey) : IDisposable
{
    /// <summary>Wipes the secret key.</summary>
    public void Dispose() => SecretKey.Dispose();
}

/// <summary>The result of an ML-KEM encapsulation.</summary>
/// <param name="Ciphertext">Send this to the holder of the decapsulation key.</param>
/// <param name="SharedSecret">The 32-byte shared secret, off-heap; dispose it when done.</param>
public sealed record KemEncapsulation(byte[] Ciphertext, SecretKey SharedSecret) : IDisposable
{
    /// <summary>Wipes the shared secret.</summary>
    public void Dispose() => SharedSecret.Dispose();
}

/// <summary>
/// RFC 8032 Ed25519 signatures through a <see cref="Capability"/>
/// (<see cref="CapabilityRights.CryptoSign"/>). Thread-safe.
/// </summary>
public sealed class Ed25519
{
    /// <summary>Public key length.</summary>
    public const int PublicKeyBytes = 32;

    /// <summary>Secret key (seed) length.</summary>
    public const int SecretKeyBytes = 32;

    /// <summary>Signature length.</summary>
    public const int SignatureBytes = 64;

    private readonly Capability _cap;

    /// <summary>Initializes a signer that presents <paramref name="capability"/> on every call.</summary>
    /// <param name="capability">Needs <see cref="CapabilityRights.CryptoSign"/>.</param>
    public Ed25519(Capability capability)
    {
        ArgumentNullException.ThrowIfNull(capability);
        _cap = capability;
    }

    /// <summary>Generates a key pair from the OS CSPRNG.</summary>
    /// <returns>The pair; dispose it when done.</returns>
    public unsafe KeyPair GenerateKey()
    {
        _cap.Enter("Ed25519.GenerateKey");
        var pk = new byte[PublicKeyBytes];
        SecretKey sk = SecretKey.Allocate(SecretKeyBytes, SecretKeyKind.Ed25519);
        try
        {
            sk.Use(s =>
            {
                fixed (byte* p = pk)
                {
                    _cap.Check(NativeMethods.Ed25519Keygen(_cap.Handle, p, s), "Ed25519.GenerateKey");
                }
            });
            return new KeyPair(pk, sk);
        }
        catch
        {
            sk.Dispose();
            throw;
        }
    }

    /// <summary>Derives the public key of a secret key.</summary>
    /// <param name="secretKey">A 32-byte Ed25519 secret key.</param>
    /// <returns>The public key.</returns>
    public unsafe byte[] GetPublicKey(SecretKey secretKey)
    {
        ArgumentNullException.ThrowIfNull(secretKey);
        secretKey.RequireLength(SecretKeyBytes, nameof(secretKey));
        _cap.Enter("Ed25519.GetPublicKey");
        var pk = new byte[PublicKeyBytes];
        secretKey.Use(s =>
        {
            fixed (byte* p = pk)
            {
                _cap.Check(NativeMethods.Ed25519Public(_cap.Handle, s, p), "Ed25519.GetPublicKey");
            }
        });
        return pk;
    }

    /// <summary>Signs a message (deterministic, as RFC 8032 defines).</summary>
    /// <param name="secretKey">A 32-byte Ed25519 secret key.</param>
    /// <param name="message">The message.</param>
    /// <returns>The 64-byte signature.</returns>
    public unsafe byte[] Sign(SecretKey secretKey, ReadOnlySpan<byte> message)
    {
        ArgumentNullException.ThrowIfNull(secretKey);
        secretKey.RequireLength(SecretKeyBytes, nameof(secretKey));
        _cap.Enter("Ed25519.Sign");
        var sig = new byte[SignatureBytes];
        fixed (byte* m = message)
        {
            byte* mp = m;
            nuint ml = (nuint)message.Length;
            secretKey.Use(s =>
            {
                fixed (byte* g = sig)
                {
                    _cap.Check(NativeMethods.Ed25519Sign(_cap.Handle, s, mp, ml, g), "Ed25519.Sign");
                }
            });
        }

        return sig;
    }

    /// <summary>Verifies a signature.</summary>
    /// <param name="publicKey">32-byte public key.</param>
    /// <param name="message">The message.</param>
    /// <param name="signature">64-byte signature.</param>
    /// <returns><see langword="true"/> if valid; <see langword="false"/> otherwise.</returns>
    public unsafe bool Verify(ReadOnlySpan<byte> publicKey, ReadOnlySpan<byte> message, ReadOnlySpan<byte> signature)
    {
        CryptoGuard.Length(publicKey, PublicKeyBytes, nameof(publicKey));
        CryptoGuard.Length(signature, SignatureBytes, nameof(signature));
        _cap.Enter("Ed25519.Verify");
        fixed (byte* p = publicKey)
        fixed (byte* m = message)
        fixed (byte* g = signature)
        {
            int rc = NativeMethods.Ed25519Verify(_cap.Handle, p, m, (nuint)message.Length, g);
            if (rc == (int)ZxvStatus.VerificationFailed)
            {
                return false;
            }

            _cap.Check(rc, "Ed25519.Verify");
            return true;
        }
    }
}

/// <summary>
/// FIPS 204 ML-DSA-87 post-quantum signatures through a <see cref="Capability"/>
/// (<see cref="CapabilityRights.CryptoSign"/>). Thread-safe. Not FIPS 140-3 validated.
/// </summary>
public sealed class MlDsa87
{
    /// <summary>Public key length.</summary>
    public const int PublicKeyBytes = 2592;

    /// <summary>Secret key length.</summary>
    public const int SecretKeyBytes = 4896;

    /// <summary>Signature length.</summary>
    public const int SignatureBytes = 4627;

    /// <summary>Longest FIPS 204 context string.</summary>
    public const int MaxContextBytes = 255;

    private readonly Capability _cap;

    /// <summary>Initializes a signer that presents <paramref name="capability"/> on every call.</summary>
    /// <param name="capability">Needs <see cref="CapabilityRights.CryptoSign"/>.</param>
    public MlDsa87(Capability capability)
    {
        ArgumentNullException.ThrowIfNull(capability);
        _cap = capability;
    }

    /// <summary>Generates a key pair from the OS CSPRNG.</summary>
    /// <returns>The pair; dispose it when done.</returns>
    public KeyPair GenerateKey() => Keygen(ReadOnlySpan<byte>.Empty, seeded: false);

    /// <summary>Derives a key pair deterministically from a 32-byte seed (for test vectors and key escrow schemes).</summary>
    /// <param name="seed">32 secret bytes; wipe them after the call.</param>
    /// <returns>The pair; dispose it when done.</returns>
    public KeyPair GenerateKey(ReadOnlySpan<byte> seed)
    {
        CryptoGuard.Length(seed, 32, nameof(seed));
        return Keygen(seed, seeded: true);
    }

    /// <summary>Signs with fresh randomness (hedged signing).</summary>
    /// <param name="secretKey">A 4896-byte ML-DSA-87 secret key.</param>
    /// <param name="message">The message.</param>
    /// <param name="context">FIPS 204 context string (at most 255 bytes); empty for none.</param>
    /// <returns>The 4627-byte signature.</returns>
    public byte[] Sign(SecretKey secretKey, ReadOnlySpan<byte> message, ReadOnlySpan<byte> context = default) =>
        SignCore(secretKey, message, context, deterministic: false);

    /// <summary>Signs deterministically: the same input gives the same signature (for test vectors).</summary>
    /// <param name="secretKey">A 4896-byte ML-DSA-87 secret key.</param>
    /// <param name="message">The message.</param>
    /// <param name="context">FIPS 204 context string (at most 255 bytes); empty for none.</param>
    /// <returns>The 4627-byte signature.</returns>
    public byte[] SignDeterministic(SecretKey secretKey, ReadOnlySpan<byte> message, ReadOnlySpan<byte> context = default) =>
        SignCore(secretKey, message, context, deterministic: true);

    /// <summary>Verifies a signature.</summary>
    /// <param name="publicKey">2592-byte public key.</param>
    /// <param name="message">The message.</param>
    /// <param name="signature">4627-byte signature.</param>
    /// <param name="context">The context string used when signing.</param>
    /// <returns><see langword="true"/> if valid; <see langword="false"/> otherwise.</returns>
    public unsafe bool Verify(ReadOnlySpan<byte> publicKey, ReadOnlySpan<byte> message, ReadOnlySpan<byte> signature, ReadOnlySpan<byte> context = default)
    {
        CryptoGuard.Length(publicKey, PublicKeyBytes, nameof(publicKey));
        CryptoGuard.Length(signature, SignatureBytes, nameof(signature));
        CryptoGuard.MaxLength(context, MaxContextBytes, nameof(context));
        _cap.Enter("MlDsa87.Verify");
        fixed (byte* p = publicKey)
        fixed (byte* m = message)
        fixed (byte* c = context)
        fixed (byte* g = signature)
        {
            int rc = NativeMethods.MlDsaVerify(_cap.Handle, p, m, (nuint)message.Length, c, (nuint)context.Length, g);
            if (rc == (int)ZxvStatus.VerificationFailed)
            {
                return false;
            }

            _cap.Check(rc, "MlDsa87.Verify");
            return true;
        }
    }

    private unsafe KeyPair Keygen(ReadOnlySpan<byte> seed, bool seeded)
    {
        _cap.Enter("MlDsa87.GenerateKey");
        var pk = new byte[PublicKeyBytes];
        SecretKey sk = SecretKey.Allocate(SecretKeyBytes, SecretKeyKind.MlDsa87);
        try
        {
            fixed (byte* sd = seed)
            {
                byte* seedPtr = sd;
                sk.Use(s =>
                {
                    fixed (byte* p = pk)
                    {
                        int rc = seeded
                            ? NativeMethods.MlDsaKeygenSeed(_cap.Handle, seedPtr, p, s)
                            : NativeMethods.MlDsaKeygen(_cap.Handle, p, s);
                        _cap.Check(rc, "MlDsa87.GenerateKey");
                    }
                });
            }

            return new KeyPair(pk, sk);
        }
        catch
        {
            sk.Dispose();
            throw;
        }
    }

    private unsafe byte[] SignCore(SecretKey secretKey, ReadOnlySpan<byte> message, ReadOnlySpan<byte> context, bool deterministic)
    {
        ArgumentNullException.ThrowIfNull(secretKey);
        secretKey.RequireLength(SecretKeyBytes, nameof(secretKey));
        CryptoGuard.MaxLength(context, MaxContextBytes, nameof(context));
        _cap.Enter("MlDsa87.Sign");
        var sig = new byte[SignatureBytes];
        fixed (byte* m = message)
        fixed (byte* c = context)
        {
            byte* mp = m, cp = c;
            nuint ml = (nuint)message.Length, cl = (nuint)context.Length;
            secretKey.Use(s =>
            {
                fixed (byte* g = sig)
                {
                    int rc = deterministic
                        ? NativeMethods.MlDsaSignDeterministic(_cap.Handle, s, mp, ml, cp, cl, g)
                        : NativeMethods.MlDsaSign(_cap.Handle, s, mp, ml, cp, cl, g);
                    _cap.Check(rc, "MlDsa87.Sign");
                }
            });
        }

        return sig;
    }
}

/// <summary>
/// FIPS 203 ML-KEM-1024 post-quantum key encapsulation through a
/// <see cref="Capability"/> (<see cref="CapabilityRights.CryptoKem"/>).
/// Thread-safe. Not FIPS 140-3 validated.
/// </summary>
public sealed class MlKem1024
{
    /// <summary>Encapsulation (public) key length.</summary>
    public const int EncapsulationKeyBytes = 1568;

    /// <summary>Decapsulation (secret) key length.</summary>
    public const int DecapsulationKeyBytes = 3168;

    /// <summary>Ciphertext length.</summary>
    public const int CiphertextBytes = 1568;

    /// <summary>Shared secret length.</summary>
    public const int SharedSecretBytes = 32;

    private readonly Capability _cap;

    /// <summary>Initializes a KEM that presents <paramref name="capability"/> on every call.</summary>
    /// <param name="capability">Needs <see cref="CapabilityRights.CryptoKem"/>.</param>
    public MlKem1024(Capability capability)
    {
        ArgumentNullException.ThrowIfNull(capability);
        _cap = capability;
    }

    /// <summary>Generates a key pair from the OS CSPRNG.</summary>
    /// <returns>The encapsulation key and the off-heap decapsulation key.</returns>
    public KeyPair GenerateKey() => Keygen(default, default, seeded: false);

    /// <summary>Derives a key pair deterministically (FIPS 203 <c>ML-KEM.KeyGen_internal</c>), for test vectors.</summary>
    /// <param name="d">32 bytes of seed <c>d</c>.</param>
    /// <param name="z">32 bytes of seed <c>z</c>.</param>
    /// <returns>The key pair.</returns>
    public KeyPair GenerateKey(ReadOnlySpan<byte> d, ReadOnlySpan<byte> z)
    {
        CryptoGuard.Length(d, 32, nameof(d));
        CryptoGuard.Length(z, 32, nameof(z));
        return Keygen(d, z, seeded: true);
    }

    /// <summary>Encapsulates a fresh shared secret to an encapsulation key.</summary>
    /// <param name="encapsulationKey">1568-byte key.</param>
    /// <returns>The ciphertext and the shared secret.</returns>
    /// <exception cref="ZxvUsageException">The key fails the FIPS 203 modulus check (<see cref="ZxvStatus.InvalidFormat"/>).</exception>
    public KemEncapsulation Encapsulate(ReadOnlySpan<byte> encapsulationKey) => Encaps(encapsulationKey, default, seeded: false);

    /// <summary>Encapsulates deterministically from the 32-byte message <paramref name="m"/>, for test vectors.</summary>
    /// <param name="encapsulationKey">1568-byte key.</param>
    /// <param name="m">32 bytes of randomness.</param>
    /// <returns>The ciphertext and the shared secret.</returns>
    public KemEncapsulation Encapsulate(ReadOnlySpan<byte> encapsulationKey, ReadOnlySpan<byte> m)
    {
        CryptoGuard.Length(m, 32, nameof(m));
        return Encaps(encapsulationKey, m, seeded: true);
    }

    /// <summary>Recovers the shared secret from a ciphertext.</summary>
    /// <param name="decapsulationKey">The 3168-byte decapsulation key.</param>
    /// <param name="ciphertext">1568-byte ciphertext.</param>
    /// <returns>The shared secret, off-heap.</returns>
    public unsafe SecretKey Decapsulate(SecretKey decapsulationKey, ReadOnlySpan<byte> ciphertext)
    {
        ArgumentNullException.ThrowIfNull(decapsulationKey);
        decapsulationKey.RequireLength(DecapsulationKeyBytes, nameof(decapsulationKey));
        CryptoGuard.Length(ciphertext, CiphertextBytes, nameof(ciphertext));
        _cap.Enter("MlKem1024.Decapsulate");
        SecretKey ss = SecretKey.Allocate(SharedSecretBytes, SecretKeyKind.SharedSecret);
        try
        {
            fixed (byte* c = ciphertext)
            {
                byte* cp = c;
                decapsulationKey.Use(dk => ss.Use(s =>
                    _cap.Check(NativeMethods.MlKemDecaps(_cap.Handle, dk, cp, s), "MlKem1024.Decapsulate")));
            }

            return ss;
        }
        catch
        {
            ss.Dispose();
            throw;
        }
    }

    private unsafe KeyPair Keygen(ReadOnlySpan<byte> d, ReadOnlySpan<byte> z, bool seeded)
    {
        _cap.Enter("MlKem1024.GenerateKey");
        var ek = new byte[EncapsulationKeyBytes];
        SecretKey dk = SecretKey.Allocate(DecapsulationKeyBytes, SecretKeyKind.MlKem1024);
        try
        {
            fixed (byte* dp = d)
            fixed (byte* zp = z)
            {
                byte* dd = dp, zz = zp;
                dk.Use(s =>
                {
                    fixed (byte* e = ek)
                    {
                        int rc = seeded
                            ? NativeMethods.MlKemKeygenDerand(_cap.Handle, dd, zz, e, s)
                            : NativeMethods.MlKemKeygen(_cap.Handle, e, s);
                        _cap.Check(rc, "MlKem1024.GenerateKey");
                    }
                });
            }

            return new KeyPair(ek, dk);
        }
        catch
        {
            dk.Dispose();
            throw;
        }
    }

    private unsafe KemEncapsulation Encaps(ReadOnlySpan<byte> encapsulationKey, ReadOnlySpan<byte> m, bool seeded)
    {
        CryptoGuard.Length(encapsulationKey, EncapsulationKeyBytes, nameof(encapsulationKey));
        _cap.Enter("MlKem1024.Encapsulate");
        var ct = new byte[CiphertextBytes];
        SecretKey ss = SecretKey.Allocate(SharedSecretBytes, SecretKeyKind.SharedSecret);
        try
        {
            fixed (byte* e = encapsulationKey)
            fixed (byte* mp = m)
            {
                byte* ep = e, mm = mp;
                ss.Use(s =>
                {
                    fixed (byte* c = ct)
                    {
                        int rc = seeded
                            ? NativeMethods.MlKemEncapsDerand(_cap.Handle, ep, mm, c, s)
                            : NativeMethods.MlKemEncaps(_cap.Handle, ep, c, s);
                        _cap.Check(rc, "MlKem1024.Encapsulate");
                    }
                });
            }

            return new KemEncapsulation(ct, ss);
        }
        catch
        {
            ss.Dispose();
            throw;
        }
    }
}

/// <summary>Length checks for key, signature and seed buffers.</summary>
internal static class CryptoGuard
{
    internal static void Length(ReadOnlySpan<byte> value, int expected, string paramName)
    {
        if (value.Length != expected)
        {
            throw new ArgumentException($"Expected {expected} bytes, got {value.Length}.", paramName);
        }
    }

    internal static void MaxLength(ReadOnlySpan<byte> value, int max, string paramName)
    {
        if (value.Length > max)
        {
            throw new ArgumentException($"At most {max} bytes allowed, got {value.Length}.", paramName);
        }
    }
}
