// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0

using System;
using System.Text;
using System.Text.Json;
using Zxv.Interop;
using Zxv.Wire;

namespace Zxv.Sdk.Tests.Abi11;

/// <summary>UBH-168 frames, hashes, Ed25519, ML-DSA-87 and ML-KEM-1024 against vectors.json.</summary>
public sealed class WireHashCryptoTests : IDisposable
{
    private readonly ZxvPlatform _p = ZxvPlatform.Create();
    private readonly Capability _cap;

    public WireHashCryptoTests()
    {
        _cap = _p.CreateRootCapability();
    }

    public void Dispose()
    {
        _cap.Dispose();
        _p.Dispose();
    }

    [Fact]
    public void Ubh168_frames_match_vectors_byte_for_byte()
    {
        foreach (JsonElement v in Conformance.Vectors.GetProperty("ubh168").EnumerateArray())
        {
            byte tag = Ubh168Frame.MakeTag(v.GetProperty("trunk").GetInt32(), v.GetProperty("shell").GetInt32(), v.GetProperty("sync").GetInt32() == 1);
            Assert.Equal(v.GetProperty("tag").GetInt32(), tag);
            JsonElement sp = v.GetProperty("s_plus"), sm = v.GetProperty("s_minus");
            var frame = new Ubh168Frame(tag, sp[0].GetUInt32(), sp[1].GetUInt32(), sp[2].GetUInt32(), sm[0].GetUInt32(), sm[1].GetUInt32());
            string expected = v.GetProperty("frame").GetString()!;
            Assert.Equal(expected, Conformance.ToHex(frame.Pack()));        // pure managed
            Assert.Equal(expected, Conformance.ToHex(frame.PackNative()));  // the C library
            Assert.Equal(frame, Ubh168Frame.Unpack(Conformance.Hex(expected)));
            Assert.Equal((WireTruth)v.GetProperty("truth").GetInt32(), frame.Resolve());
            Assert.Equal(v.GetProperty("trunk").GetInt32(), frame.Trunk);
        }
    }

    [Fact]
    public unsafe void Idle_frames_match_vectors_and_native()
    {
        int trunk = 0;
        byte* native = stackalloc byte[21];
        foreach (JsonElement v in Conformance.Vectors.GetProperty("ubh168_idle").EnumerateArray())
        {
            Ubh168Frame idle = Ubh168Frame.Idle(trunk);
            Assert.Equal(v.GetString(), idle.ToString());
            Assert.Equal(0, NativeMethods.WireIdle((uint)trunk, native));
            Assert.Equal(v.GetString(), Conformance.ToHex(new ReadOnlySpan<byte>(native, 21)));
            Assert.Equal(WireTruth.Neutral, idle.Resolve());
            trunk++;
        }

        Assert.Throws<ArgumentException>(() => Ubh168Frame.Unpack(new byte[20]));
    }

    [Fact]
    public void Hashes_match_vectors()
    {
        foreach (JsonElement v in Conformance.Vectors.GetProperty("hash").EnumerateArray())
        {
            byte[] data = Encoding.UTF8.GetBytes(v.GetProperty("utf8").GetString()!);
            Assert.Equal(v.GetProperty("keccak256").GetString(), Conformance.ToHex(Hashes.Keccak256(data)));
            Assert.Equal(v.GetProperty("sha3_256").GetString(), Conformance.ToHex(Hashes.Sha3256(data)));
            Assert.Equal(v.GetProperty("shake256_64").GetString(), Conformance.ToHex(Hashes.Shake256(data, 64)));
        }

        Span<byte> a = stackalloc byte[32], b = stackalloc byte[32];
        SecureMemory.FillRandom(a);
        SecureMemory.FillRandom(b);
        Assert.False(a.SequenceEqual(b));
        SecureMemory.Zero(a);
        Assert.True(a.IndexOfAnyExcept((byte)0) < 0);
    }

    [Fact]
    public void Ed25519_matches_vectors()
    {
        JsonElement v = Conformance.Vectors.GetProperty("ed25519");
        var ed = new Ed25519(_cap);
        byte[] skBytes = Conformance.Hex(v.GetProperty("sk").GetString()!);
        using SecretKey sk = SecretKey.Import(skBytes, SecretKeyKind.Ed25519);
        System.Security.Cryptography.CryptographicOperations.ZeroMemory(skBytes);
        byte[] pk = ed.GetPublicKey(sk);
        Assert.Equal(v.GetProperty("pk").GetString(), Conformance.ToHex(pk));
        byte[] msg = Encoding.UTF8.GetBytes(v.GetProperty("msg_utf8").GetString()!);
        byte[] sig = ed.Sign(sk, msg);
        Assert.Equal(v.GetProperty("sig").GetString(), Conformance.ToHex(sig));
        Assert.True(ed.Verify(pk, msg, sig));
        sig[0] ^= 1;
        Assert.False(ed.Verify(pk, msg, sig));

        using KeyPair fresh = ed.GenerateKey();
        Assert.Equal(fresh.PublicKey, ed.GetPublicKey(fresh.SecretKey));
        Assert.True(ed.Verify(fresh.PublicKey, msg, ed.Sign(fresh.SecretKey, msg)));
    }

    [Fact]
    public void MlDsa87_matches_vectors()
    {
        JsonElement v = Conformance.Vectors.GetProperty("mldsa87");
        var dsa = new MlDsa87(_cap);
        using KeyPair kp = dsa.GenerateKey(Conformance.Hex(v.GetProperty("seed").GetString()!));
        Assert.Equal(v.GetProperty("pk_sha3_256").GetString(), Conformance.ToHex(Hashes.Sha3256(kp.PublicKey)));
        byte[] skCopy = kp.SecretKey.ExportBytesUnsafe();
        Assert.Equal(v.GetProperty("sk_sha3_256").GetString(), Conformance.ToHex(Hashes.Sha3256(skCopy)));
        SecureMemory.Zero(skCopy);
        byte[] ctx = Encoding.UTF8.GetBytes(v.GetProperty("ctx_utf8").GetString()!);
        byte[] msg = Encoding.UTF8.GetBytes(v.GetProperty("msg_utf8").GetString()!);
        byte[] sig = dsa.SignDeterministic(kp.SecretKey, msg, ctx);
        Assert.Equal(v.GetProperty("sig_deterministic_sha3_256").GetString(), Conformance.ToHex(Hashes.Sha3256(sig)));
        Assert.True(dsa.Verify(kp.PublicKey, msg, sig, ctx));
        Assert.False(dsa.Verify(kp.PublicKey, msg, sig, "zxw"u8));
        byte[] hedged = dsa.Sign(kp.SecretKey, msg, ctx);
        Assert.True(dsa.Verify(kp.PublicKey, msg, hedged, ctx));
    }

    [Fact]
    public void MlKem1024_matches_vectors()
    {
        JsonElement v = Conformance.Vectors.GetProperty("mlkem1024");
        var kem = new MlKem1024(_cap);
        using KeyPair kp = kem.GenerateKey(Conformance.Hex(v.GetProperty("d").GetString()!), Conformance.Hex(v.GetProperty("z").GetString()!));
        Assert.Equal(v.GetProperty("ek_sha3_256").GetString(), Conformance.ToHex(Hashes.Sha3256(kp.PublicKey)));
        using KemEncapsulation enc = kem.Encapsulate(kp.PublicKey, Conformance.Hex(v.GetProperty("m").GetString()!));
        Assert.Equal(v.GetProperty("ct_sha3_256").GetString(), Conformance.ToHex(Hashes.Sha3256(enc.Ciphertext)));
        Assert.Equal(v.GetProperty("ss").GetString(), Conformance.ToHex(enc.SharedSecret.ExportBytesUnsafe()));
        using SecretKey ss = kem.Decapsulate(kp.SecretKey, enc.Ciphertext);
        Assert.True(ss.FixedTimeEquals(enc.SharedSecret));

        using KeyPair random = kem.GenerateKey();
        using KemEncapsulation e2 = kem.Encapsulate(random.PublicKey);
        using SecretKey s2 = kem.Decapsulate(random.SecretKey, e2.Ciphertext);
        Assert.True(s2.FixedTimeEquals(e2.SharedSecret));

        byte[] bad = (byte[])random.PublicKey.Clone();
        bad.AsSpan(0, 32).Fill(0xFF);
        Assert.Equal(ZxvStatus.InvalidFormat, Assert.ThrowsAny<ZxvUsageException>(() => kem.Encapsulate(bad)).Status);
    }

    [Fact]
    public void Crypto_needs_its_capability()
    {
        using Capability kemOnly = _cap.Attenuate(CapabilityRights.CryptoKem);
        using SecretKey sk = SecretKey.Import(new byte[32], SecretKeyKind.Ed25519);
        var ex = Assert.Throws<CapabilityDeniedException>(() => new Ed25519(kemOnly).Sign(sk, "x"u8));
        Assert.Equal(ZxvStatus.CapabilityDenied, ex.Status);
        Assert.Throws<CapabilityDeniedException>(() => new MlDsa87(kemOnly).GenerateKey());
        using KeyPair kp = new MlKem1024(kemOnly).GenerateKey(); // allowed
        Assert.Equal(MlKem1024.EncapsulationKeyBytes, kp.PublicKey.Length);
    }

    [Fact]
    public void Secret_keys_live_off_heap_and_refuse_use_after_dispose()
    {
        var ed = new Ed25519(_cap);
        SecretKey sk = SecretKey.Import(new byte[] { 1, 2, 3 }, SecretKeyKind.Raw);
        Assert.Equal(new byte[] { 1, 2, 3 }, sk.ExportBytesUnsafe());
        Assert.Throws<ArgumentException>(() => ed.Sign(sk, "x"u8)); // wrong length for Ed25519
        sk.Dispose();
        sk.Dispose(); // idempotent
        Assert.True(sk.IsDisposed);
        Assert.Throws<ObjectDisposedException>(() => sk.ExportBytesUnsafe());
        Assert.Throws<ArgumentException>(() => SecretKey.Import(ReadOnlySpan<byte>.Empty));
    }
}
