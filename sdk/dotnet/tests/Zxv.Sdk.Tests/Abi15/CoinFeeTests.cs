// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0

using System;
using System.Linq;
using System.Text.Json;
using Zxv.Coins;
using Zxv.Fees;
using Zxv.Interop;

namespace Zxv.Sdk.Tests.Abi15;

/// <summary>ABI 1.5 coin and fee groups against <c>vectors.json</c> "coin" and "fee".</summary>
public sealed class CoinFeeTests : IDisposable
{
    private static readonly JsonElement C = V.Group("coin");
    private static readonly JsonElement F = V.Group("fee");

    private readonly ZxvPlatform _p = TestPlatform.Create();

    public void Dispose() => _p.Dispose();

    [Fact]
    public void Coin_ladder_faces_rungs_and_splits()
    {
        Assert.Equal(Coin.Rungs, C.GetProperty("rungs").GetInt32());
        Assert.Equal(Coin.TopFace, V.U64(C.GetProperty("top_face")));
        foreach (JsonElement v in C.GetProperty("face").EnumerateArray())
        {
            ulong face = 0;
            if (V.Expect(v, () => face = Coin.Face(v.GetProperty("rung").GetInt32())))
            {
                Assert.Equal(V.U64(v.GetProperty("face")), face);
            }
        }

        foreach (JsonElement v in C.GetProperty("rung_of").EnumerateArray())
        {
            int rung = 0;
            if (V.Expect(v, () => rung = Coin.RungOf(V.U64(v.GetProperty("face")))))
            {
                Assert.Equal(v.GetProperty("rung").GetInt32(), rung);
            }
        }

        foreach (JsonElement v in C.GetProperty("split").EnumerateArray())
        {
            ulong amount = V.U64(v.GetProperty("amount"));
            long count = 0;
            if (!V.Expect(v, () => count = Coin.SplitCount(amount)))
            {
                continue;
            }

            Assert.Equal((long)V.U64(v.GetProperty("count")), count);
            if (V.Has(v, "rungs"))
            {
                Assert.Equal(Abi11.Conformance.Ints(v.GetProperty("rungs")), Coin.Split(amount));
            }
        }
    }

    [Fact]
    public void Denomination_status_is_a_usage_error()
    {
        var e = Assert.Throws<ZxvUsageException>(() => Coin.RungOf(4));
        Assert.Equal(ZxvStatus.Denomination, e.Status);
        Assert.Equal("ZXV_E_DENOM", e.StatusName);
    }

    [Fact]
    public void Token_faces_tiers_values_and_canonical_forms()
    {
        foreach (JsonElement v in C.GetProperty("token_face").EnumerateArray())
        {
            int tier = v.GetProperty("tier").GetInt32();
            ulong face = 0;
            if (V.Expect(v, () => face = Token.Face(tier)))
            {
                Assert.Equal(V.U64(v.GetProperty("face")), face);
                Assert.Equal(v.GetProperty("super").GetBoolean(), Token.IsSuper(tier));
            }
        }

        foreach (JsonElement v in C.GetProperty("token_tier").EnumerateArray())
        {
            int tier = 0;
            if (V.Expect(v, () => tier = Token.TierOf(V.U64(v.GetProperty("face")))))
            {
                Assert.Equal(v.GetProperty("tier").GetInt32(), tier);
            }
        }

        foreach (JsonElement v in C.GetProperty("token_value").EnumerateArray())
        {
            TokenAmount amount = default;
            if (V.Expect(v, () => amount = Token.Measure(V.Holding(v.GetProperty("counts")))))
            {
                Assert.Equal(new TokenAmount(V.U64(v.GetProperty("value")), V.U64(v.GetProperty("pieces"))), amount);
            }
        }

        foreach (JsonElement v in C.GetProperty("token_canonical").EnumerateArray())
        {
            ulong[] counts = Array.Empty<ulong>();
            if (V.Expect(v, () => counts = Token.Canonical(V.U64(v.GetProperty("value")), v.GetProperty("octane").GetInt32())))
            {
                Assert.Equal(V.Holding(v.GetProperty("counts")), counts);
                Assert.Equal(V.U64(v.GetProperty("pieces")), Token.Measure(counts).Pieces);
                Assert.True(Token.IsCanonical(counts, v.GetProperty("octane").GetInt32()));
            }
        }

        foreach (JsonElement v in C.GetProperty("token_is_canonical").EnumerateArray())
        {
            Assert.Equal(v.GetProperty("canonical").GetBoolean(), Token.IsCanonical(V.Holding(v.GetProperty("counts")), v.GetProperty("octane").GetInt32()));
        }

        foreach (JsonElement v in C.GetProperty("token_compress").EnumerateArray())
        {
            ulong[] counts = V.Holding(v.GetProperty("counts"));
            V.Expect(v, () => Token.Compress(counts, v.GetProperty("octane").GetInt32()));
            Assert.Equal(V.Holding(v.GetProperty("out")), counts);
        }
    }

    [Fact]
    public void Token_fuse_and_split_leave_counts_unchanged_on_failure()
    {
        foreach (JsonElement v in C.GetProperty("token_fuse_split").EnumerateArray())
        {
            ulong[] counts = V.Holding(v.GetProperty("counts"));
            int tier = v.GetProperty("tier").GetInt32();
            if (V.Str(v, "op") == "fuse")
            {
                V.Expect(v, () => Token.Fuse(counts, v.GetProperty("octane").GetInt32(), tier));
            }
            else
            {
                V.Expect(v, () => Token.Split(counts, tier));
            }

            Assert.Equal(V.Holding(v.GetProperty("out")), counts);
        }

        Assert.Throws<InsufficientFundsException>(() => Token.Split(new ulong[64], 5));
        Assert.Throws<ArgumentException>(() => Token.Measure(new ulong[63]));
    }

    [Fact]
    public void Fibonacci_codes_write_read_and_stream()
    {
        foreach (JsonElement v in C.GetProperty("token_code").EnumerateArray())
        {
            ulong value = V.U64(v.GetProperty("value"));
            int bits = 0;
            if (!V.Expect(v, () => bits = FibonacciCode.BitLength(value)))
            {
                Assert.Equal(V.Str(v, "status"), V.StatusOf(() =>
                {
                    int b = 0;
                    FibonacciCode.Write(new byte[16], ref b, value);
                }));
                continue;
            }

            Assert.Equal(v.GetProperty("bits").GetInt32(), bits);
            byte[] bytes = FibonacciCode.Encode(new[] { value }, out int used);
            Assert.Equal(bits, used);
            Assert.Equal(V.Str(v, "bytes"), V.ToHex(bytes));
            int at = 0;
            Assert.Equal(value, FibonacciCode.Read(bytes, ref at));
            Assert.Equal(bits, at);
        }

        JsonElement s = C.GetProperty("token_code_stream");
        ulong[] values = s.GetProperty("values").EnumerateArray().Select(V.U64).ToArray();
        byte[] stream = FibonacciCode.Encode(values, out int total);
        Assert.Equal(s.GetProperty("bits").GetInt32(), total);
        Assert.Equal(V.Str(s, "bytes"), V.ToHex(stream));
        int bit = 0;
        foreach (ulong expected in values)
        {
            Assert.Equal(expected, FibonacciCode.Read(stream, ref bit));
        }

        foreach (JsonElement v in C.GetProperty("token_code_get_reject").EnumerateArray())
        {
            byte[] bytes = V.Hex(v.GetProperty("bytes"));
            int at = v.GetProperty("bit").GetInt32();
            V.Expect(v, () => FibonacciCode.Read(bytes, ref at));
            Assert.Equal(v.GetProperty("bit").GetInt32(), at);
        }

        // A code that does not fit is refused and nothing moves.
        int pos = 0;
        var e = Assert.Throws<ZxvUsageException>(() => FibonacciCode.Write(new byte[1], ref pos, 100));
        Assert.Equal(ZxvStatus.BufferTooSmall, e.Status);
        Assert.Equal(0, pos);
    }

    [Fact]
    public void Token_wallet_mints_spends_transfers_and_needs_mint()
    {
        using Capability cap = _p.CreateRootCapability(CapabilityRights.AllAbi15);
        using TokenWallet a = _p.CreateTokenWallet(cap, 7, Octane.Racing);
        using TokenWallet b = _p.CreateTokenWallet(cap, 8, Octane.Plain);
        a.Mint(1_000_000);
        Assert.Equal(new TokenWalletInfo(7, Octane.Racing, 1_000_000, 17), a.Info);
        Assert.Equal(Token.Canonical(1_000_000, Octane.Racing), a.GetCounts());
        a.TransferTo(b, 1000);
        Assert.Equal(999_000UL, a.Value);
        Assert.Equal(new TokenWalletInfo(8, Octane.Plain, 1000, 1000), b.Info);
        a.Spend(9_000);
        Assert.Equal(990_000UL, a.Value);
        Assert.Throws<InsufficientFundsException>(() => b.Spend(1001));
        Assert.Equal(1000UL, b.Value);
        b.SetOctane(Octane.Max);
        Assert.Equal(1000UL, b.Value);
        Assert.True(Token.IsCanonical(b.GetCounts(), Octane.Max));

        // A capability without MINT is refused: creating, minting, spending.
        using Capability noMint = cap.Attenuate(CapabilityRights.AllAbi15 & ~CapabilityRights.Mint);
        Assert.Throws<CapabilityDeniedException>(() => _p.CreateTokenWallet(noMint, 1));
        using Capability legacy = _p.CreateRootCapability();
        Assert.Throws<CapabilityDeniedException>(() => _p.CreateTokenWallet(legacy, 1));
    }

    [Fact]
    public void Token_wallet_lifetime_and_poisoned_context()
    {
        using Capability cap = _p.CreateRootCapability(CapabilityRights.AllAbi15);
        TokenWallet w = _p.CreateTokenWallet(cap, 1);
        w.Mint(5);
        w.Dispose();
        Assert.Throws<ObjectDisposedException>(() => w.Value);
        Assert.Throws<ObjectDisposedException>(() => w.Mint(1));
        w.Dispose();

        using TokenWallet live = _p.CreateTokenWallet(cap, 2);
        _p.Halt("test");
        var fatal = Assert.Throws<ZxvFatalException>(() => live.Mint(1));
        Assert.Equal(ZxvStatus.Poisoned, fatal.Status);
        Assert.Throws<ZxvFatalException>(() => live.Value);
    }

    [Fact]
    public void Assurance_fee_vectors()
    {
        Assert.Equal(AssuranceFee.Numerator, V.U64(F.GetProperty("num")));
        Assert.Equal(AssuranceFee.Denominator, V.U64(F.GetProperty("den")));
        foreach (JsonElement v in F.GetProperty("assure").EnumerateArray())
        {
            Assert.Equal(V.U64(v.GetProperty("fee")), AssuranceFee.Compute(V.U64(v.GetProperty("gross"))));
        }

        foreach (JsonElement seq in F.GetProperty("carry").EnumerateArray())
        {
            ulong rem = V.U64(seq.GetProperty("rem_in"));
            foreach (JsonElement step in seq.GetProperty("steps").EnumerateArray())
            {
                FeeCharge c = default;
                Assert.True(V.Expect(step, () => c = AssuranceFee.ComputeWithCarry(rem, V.U64(step.GetProperty("gross")))));
                Assert.Equal(new FeeCharge(V.U64(step.GetProperty("fee")), V.U64(step.GetProperty("rem"))), c);
                rem = c.Remainder;
            }
        }

        foreach (JsonElement v in F.GetProperty("carry_reject").EnumerateArray())
        {
            V.Expect(v, () => AssuranceFee.ComputeWithCarry(V.U64(v.GetProperty("rem_in")), V.U64(v.GetProperty("gross"))));
        }

        foreach (JsonElement v in F.GetProperty("split").EnumerateArray())
        {
            ulong fee = V.U64(v.GetProperty("fee"));
            FeeSplit s = AssuranceFee.Split(fee);
            ulong[] parts = v.GetProperty("parts").EnumerateArray().Select(V.U64).ToArray();
            Assert.Equal(new FeeSplit(parts[0], parts[1], parts[2], parts[3]), s);
            Assert.Equal(fee, s.Total);
        }

        JsonElement d = F.GetProperty("policy_default");
        FeePolicy p = AssuranceFee.DefaultPolicy;
        Assert.Equal(Abi11.Conformance.Ints(d.GetProperty("pct")), new[] { (int)p.ReservePercent, (int)p.DividendPercent, (int)p.InfrastructurePercent, (int)p.RegenerativePercent });
        Assert.Equal(d.GetProperty("allow_below").GetInt32() != 0, p.AllowBelowFee);
        for (int u = 0; u < 4; u++)
        {
            JsonElement r = d.GetProperty("rate")[u];
            Assert.Equal(new FeeRatio(V.U64(r[0]), V.U64(r[1])), p.CreditRate((FeeUnit)u));
        }

        Assert.Equal(new FeeRatio(V.U64(d.GetProperty("multiplier")[0]), V.U64(d.GetProperty("multiplier")[1])), p.CreditMultiplier);
        Assert.Equal(new FeeRatio(V.U64(d.GetProperty("commons_cap")[0]), V.U64(d.GetProperty("commons_cap")[1])), p.CommonsCap);
    }
}
