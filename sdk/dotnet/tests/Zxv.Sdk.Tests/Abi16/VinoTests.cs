// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0

using System;
using System.Linq;
using System.Text.Json;
using Zxv.Interop;
using Zxv.Sdk.Tests.Abi15;
using Zxv.Vino;

namespace Zxv.Sdk.Tests.Abi16;

/// <summary>ABI 1.6 Vino Embed against <c>vectors.json</c> "vino", plus lifetimes, the voucher list path and a halted context.</summary>
public sealed class VinoTests : IDisposable
{
    private static readonly JsonElement G = V.Group("vino");

    private readonly ZxvPlatform _p = TestPlatform.Create();

    public void Dispose() => _p.Dispose();

    private static PaymentRequest RequestOf(JsonElement v) => new(
        (VinoLane)v.GetProperty("lane").GetInt32(),
        V.Hex(v.GetProperty("merchant")),
        V.Hex(v.GetProperty("nonce")),
        V.U64(v.GetProperty("amount")),
        V.U64(v.GetProperty("created_at")),
        V.U64(v.GetProperty("expires_at")),
        V.Str(v, "memo"),
        (PaymentRequestFlags)v.GetProperty("flags").GetUInt32());

    private static PaymentRequestBytes FirstRequest() => PaymentRequestBytes.FromBytes(V.Hex(G.GetProperty("requests")[0].GetProperty("bytes")));

    [Fact]
    public void Requests_encode_byte_exact_with_their_ids_and_refusals()
    {
        int ok = 0, refused = 0;
        foreach (JsonElement v in G.GetProperty("requests").EnumerateArray())
        {
            PaymentRequest req = RequestOf(v);
            PaymentRequestBytes bytes = default;
            if (V.Expect(v, () => bytes = req.Encode()))
            {
                Assert.Equal(V.Str(v, "bytes"), bytes.ToString());
                Assert.Equal(V.Str(v, "id"), bytes.Id.ToString());

                // Decoding gives back the same request, field for field.
                PaymentRequest back = PaymentRequest.Decode(bytes);
                Assert.Equal(req, back);
                Assert.Equal(req.GetHashCode(), back.GetHashCode());
                Assert.Equal(V.Str(v, "memo"), back.Memo);
                Assert.Equal(bytes, back.Encode());
                ok++;
            }
            else
            {
                refused++;
            }
        }

        Assert.True(ok >= 5 && refused >= 8, $"{ok} ok, {refused} refused");
    }

    [Fact]
    public void Decode_refuses_each_changed_byte_with_its_status()
    {
        byte[] good = FirstRequest().ToArray();
        foreach (JsonElement v in G.GetProperty("decode").EnumerateArray())
        {
            byte[] bad = (byte[])good.Clone();
            bad[v.GetProperty("offset").GetInt32()] = (byte)v.GetProperty("value").GetInt32();
            V.Expect(v, () => PaymentRequest.Decode(bad));

            // The receipt checks validate the request the same way.
            if (V.Str(v, "status") != "ZXV_OK")
            {
                V.Expect(v, () => Receipt.Verify(PaymentRequestBytes.FromBytes(bad), new byte[Receipt.PublicKeyLength], 1700000100, new byte[Receipt.SignatureLength], 1700000200));
            }
        }
    }

    [Fact]
    public void Request_arguments_checked_in_managed_code()
    {
        byte[] m = new byte[32], n = new byte[16];
        Assert.Throws<ArgumentException>(() => new PaymentRequest(VinoLane.Live, new byte[31], n, 1, 0, 1));
        Assert.Throws<ArgumentException>(() => new PaymentRequest(VinoLane.Live, m, new byte[17], 1, 0, 1));
        Assert.Throws<ArgumentNullException>(() => new PaymentRequest(VinoLane.Live, m, n, 1, 0, 1, null!));
        Assert.Throws<ArgumentException>(() => new PaymentRequest(VinoLane.Live, m, n, 1, 0, 1, "a\0b"));
        Assert.Throws<ArgumentException>(() => PaymentRequest.Decode(new byte[159]));
        Assert.Throws<ArgumentException>(() => PaymentRequest.Decode(default(PaymentRequestBytes)));
        Assert.Throws<ArgumentException>(() => PaymentRequestBytes.FromBytes(new byte[161]));
        Assert.Throws<InvalidOperationException>(() => default(PaymentRequestBytes).Id);

        // A non-ASCII memo is refused by the native rule, not silently changed.
        var e = Assert.Throws<ZxvUsageException>(() => new PaymentRequest(VinoLane.Play, m, n, 1, 0, 1, "café").Encode());
        Assert.Equal(ZxvStatus.InvalidFormat, e.Status);

        // A paid chance outcome is fine on the play lane and refused live.
        new PaymentRequest(VinoLane.Play, m, n, 1, 0, 1, flags: PaymentRequestFlags.PaidChance).Encode();
        var chance = Assert.Throws<ZxvRefusedException>(() => new PaymentRequest(VinoLane.Live, m, n, 1, 0, 1, flags: PaymentRequestFlags.PaidChance).Encode());
        Assert.Equal(ZxvStatus.PaidChance, chance.Status);
    }

    [Fact]
    public void Receipt_vectors_message_signature_and_checks()
    {
        JsonElement r = G.GetProperty("receipt");
        using Capability cap = _p.CreateRootCapability(CapabilityRights.AllAbi16);
        var dsa = new MlDsa87(cap);
        using KeyPair wallet = dsa.GenerateKey(V.Hex(r.GetProperty("seed")));
        Assert.Equal(V.Str(r, "pk_sha3_256"), V.ToHex(Hashes.Sha3256(wallet.PublicKey)));

        PaymentRequestBytes request = FirstRequest();
        ulong approvedAt = V.U64(r.GetProperty("approved_at"));
        ReceiptMessage msg = Receipt.CreateMessage(request.Id, wallet.PublicKey, approvedAt);
        Assert.Equal(V.Str(r, "msg"), msg.ToString());
        byte[] sig = dsa.SignDeterministic(wallet.SecretKey, msg.ToArray());
        Assert.Equal(V.Str(r, "sig"), V.ToHex(sig));

        foreach (JsonElement c in r.GetProperty("checks").EnumerateArray())
        {
            byte[] s = (byte[])sig.Clone();
            int flip = c.GetProperty("flip_sig").GetInt32();
            if (flip >= 0)
            {
                s[flip] ^= 1;
            }

            ReceiptVerdict verdict = Receipt.Verify(request, wallet.PublicKey, V.U64(c.GetProperty("approved_at")), s, V.U64(c.GetProperty("now")));
            string got = verdict switch
            {
                ReceiptVerdict.Valid => "ZXV_OK",
                ReceiptVerdict.NotApproved => "ZXV_E_VERIFY",
                _ => "ZXV_E_EXPIRED",
            };
            Assert.Equal(V.Str(c, "status"), got);
        }
    }

    [Fact]
    public void Wallet_signs_a_receipt_the_app_verifies_offline()
    {
        using Capability cap = _p.CreateRootCapability(CapabilityRights.AllAbi16);
        using KeyPair wallet = new MlDsa87(cap).GenerateKey();
        PaymentRequestBytes request = FirstRequest();
        PaymentRequest req = PaymentRequest.Decode(request);

        byte[] sig = Receipt.Sign(cap, request, wallet.SecretKey, wallet.PublicKey, req.CreatedAt + 5);
        Assert.Equal(Receipt.SignatureLength, sig.Length);
        Assert.Equal(ReceiptVerdict.Valid, Receipt.Verify(request, wallet.PublicKey, req.CreatedAt + 5, sig, req.CreatedAt + 6));
        Assert.Equal(ReceiptVerdict.NotApproved, Receipt.Verify(request, wallet.PublicKey, req.CreatedAt + 4, sig, req.CreatedAt + 6));
        Assert.Equal(ReceiptVerdict.Expired, Receipt.Verify(request, wallet.PublicKey, req.CreatedAt + 5, sig, req.ExpiresAt + 1));

        // Another wallet's key does not verify.
        using KeyPair other = new MlDsa87(cap).GenerateKey();
        Assert.Equal(ReceiptVerdict.NotApproved, Receipt.Verify(request, other.PublicKey, req.CreatedAt + 5, sig, req.CreatedAt + 6));

        // Approving outside the window is refused; signing needs CRYPTO_SIGN.
        var late = Assert.Throws<ZxvRefusedException>(() => Receipt.Sign(cap, request, wallet.SecretKey, wallet.PublicKey, req.ExpiresAt + 1));
        Assert.Equal(ZxvStatus.Expired, late.Status);
        using Capability noSign = cap.Attenuate(CapabilityRights.AllAbi16 & ~CapabilityRights.CryptoSign);
        Assert.Throws<CapabilityDeniedException>(() => Receipt.Sign(noSign, request, wallet.SecretKey, wallet.PublicKey, req.CreatedAt));

        // Wrong lengths are caught before any native call.
        Assert.Throws<ArgumentException>(() => Receipt.Verify(request, new byte[10], 0, sig, 0));
        Assert.Throws<ArgumentException>(() => Receipt.Verify(request, wallet.PublicKey, 0, new byte[10], 0));
        Assert.Throws<ArgumentException>(() => Receipt.CreateMessage(default, wallet.PublicKey, 0));
    }

    [Fact]
    public void Play_scenario_runs_in_order_with_the_same_results()
    {
        JsonElement play = G.GetProperty("play");
        using Capability cap = _p.CreateRootCapability(CapabilityRights.AllAbi16);
        using PlaySandbox s = _p.CreatePlaySandbox(cap);
        foreach (JsonElement st in play.GetProperty("steps").EnumerateArray())
        {
            switch (V.Str(st, "op"))
            {
                case "open":
                    uint acct = 0;
                    if (V.Expect(st, () => acct = s.Open(st.GetProperty("owner").GetUInt32(), st.GetProperty("is_issuer").GetInt32(), "test")))
                    {
                        Assert.Equal(st.GetProperty("account").GetUInt32(), acct);
                    }

                    break;
                case "back":
                    V.Expect(st, () => s.PostBacking(st.GetProperty("issuer").GetUInt32(), V.U64(st.GetProperty("amount"))));
                    break;
                case "unback":
                    V.Expect(st, () => s.WithdrawBacking(st.GetProperty("issuer").GetUInt32(), V.U64(st.GetProperty("amount"))));
                    break;
                case "issue":
                    ulong id = 0;
                    if (V.Expect(st, () => id = s.Issue(st.GetProperty("issuer").GetUInt32(), st.GetProperty("holder").GetUInt32(), V.U64(st.GetProperty("face")), (VinoRail)st.GetProperty("rail").GetInt32())))
                    {
                        Assert.Equal(V.U64(st.GetProperty("id")), id);
                    }

                    break;
                case "transfer":
                    V.Expect(st, () => s.Transfer(V.U64(st.GetProperty("voucher")), st.GetProperty("from").GetUInt32(), st.GetProperty("to").GetUInt32(), (VinoRail)st.GetProperty("rail").GetInt32()));
                    break;
                case "set_rail":
                    V.Expect(st, () => s.SetRail(V.U64(st.GetProperty("voucher")), st.GetProperty("holder").GetUInt32(), (VinoRail)st.GetProperty("rail").GetInt32()));
                    break;
                case "redeem":
                    V.Expect(st, () => s.Redeem(V.U64(st.GetProperty("voucher")), st.GetProperty("holder").GetUInt32()));
                    break;
                default:
                    Assert.Fail("unknown op " + V.Str(st, "op"));
                    break;
            }
        }

        foreach (JsonElement a in play.GetProperty("accounts").EnumerateArray())
        {
            uint account = a.GetProperty("account").GetUInt32();
            VinoAccount info = s.GetAccount(account);
            Assert.Equal(
                new VinoAccount(account, a.GetProperty("owner").GetUInt32(), a.GetProperty("is_issuer").GetInt32() == 1, V.U64(a.GetProperty("backing")), V.U64(a.GetProperty("faces")), a.GetProperty("count").GetUInt32()),
                info);
            Assert.Equal(a.GetProperty("vouchers").EnumerateArray().Select(V.U64).ToArray(), s.GetVouchers(account));
        }

        foreach (JsonElement v in play.GetProperty("vouchers").EnumerateArray())
        {
            ulong id = V.U64(v.GetProperty("id"));
            Assert.Equal(
                new VoucherInfo(id, V.U64(v.GetProperty("face")), v.GetProperty("issuer").GetUInt32(), v.GetProperty("holder").GetUInt32(), (VinoRail)v.GetProperty("rail").GetInt32(), (VoucherState)v.GetProperty("state").GetInt32()),
                s.GetVoucher(id));
        }

        s.Check();
        Assert.Equal(ZxvStatus.NotFound, Assert.Throws<ZxvUsageException>(() => s.GetVoucher(99)).Status);
        Assert.Equal(ZxvStatus.NotFound, Assert.Throws<ZxvUsageException>(() => s.GetAccount(99)).Status);
        Assert.Equal(ZxvStatus.NotFound, Assert.Throws<ZxvUsageException>(() => s.GetVouchers(99)).Status);
    }

    [Fact]
    public void Each_new_play_status_maps_to_its_exception()
    {
        using Capability cap = _p.CreateRootCapability(CapabilityRights.AllAbi16);
        using PlaySandbox s = _p.CreatePlaySandbox(cap);
        uint issuer = s.OpenIssuer(1), alice = s.OpenHolder(2), bob = s.OpenHolder(3);
        Assert.Equal(ZxvStatus.InsufficientCoverage, Assert.Throws<ZxvRefusedException>(() => s.Issue(issuer, alice, 1, VinoRail.Bearer)).Status);
        s.PostBacking(issuer, 18);
        ulong v = s.Issue(issuer, alice, 8, VinoRail.Ghost);
        Assert.Equal(ZxvStatus.WrongRail, Assert.Throws<ZxvRefusedException>(() => s.Transfer(v, alice, bob, VinoRail.Bearer)).Status);
        Assert.Equal(ZxvStatus.WrongRail, Assert.Throws<ZxvRefusedException>(() => s.SetRail(v, alice, VinoRail.Ghost)).Status);
        Assert.Throws<InsufficientFundsException>(() => s.WithdrawBacking(issuer, 19));
        Assert.Equal(ZxvStatus.InvalidArgument, Assert.Throws<ZxvUsageException>(() => s.Issue(issuer, alice, 1, (VinoRail)7)).Status);
        Assert.Equal(ZxvStatus.Denomination, Assert.Throws<ZxvUsageException>(() => s.Issue(issuer, alice, 4, VinoRail.Bearer)).Status);

        // Nothing changed after the refusals.
        Assert.Equal(new VinoAccount(issuer, 1, true, 18, 8, 1), s.GetAccount(issuer));
        Assert.Equal(new VoucherInfo(v, 8, issuer, alice, VinoRail.Ghost, VoucherState.Active), s.GetVoucher(v));
    }

    [Fact]
    public void Voucher_list_path_returns_every_id_in_order()
    {
        using Capability cap = _p.CreateRootCapability(CapabilityRights.AllAbi16);
        using PlaySandbox s = _p.CreatePlaySandbox(cap);
        uint issuer = s.OpenIssuer(1), alice = s.OpenHolder(2), bob = s.OpenHolder(3);
        Assert.Empty(s.GetVouchers(alice));
        Assert.Empty(s.GetVouchers(issuer));

        s.PostBacking(issuer, 10_000);
        var expected = new ulong[600];
        for (int i = 0; i < expected.Length; i++)
        {
            expected[i] = s.Issue(issuer, alice, 1, i % 2 == 0 ? VinoRail.Bearer : VinoRail.Ghost);
        }

        Assert.Equal(expected, s.GetVouchers(alice));
        Assert.Equal(expected, s.GetVouchers(issuer));
        Assert.True(expected.SequenceEqual(expected.Order()));

        // Spending and redeeming update both lists.
        s.Transfer(expected[1], alice, bob, VinoRail.Ghost);
        s.Redeem(expected[0], alice);
        Assert.Equal(expected.Skip(2).ToArray(), s.GetVouchers(alice));
        Assert.Equal(new[] { expected[1] }, s.GetVouchers(bob));
        Assert.Equal(expected.Skip(1).ToArray(), s.GetVouchers(issuer));
        Assert.Equal(VoucherState.Redeemed, s.GetVoucher(expected[0]).State);
        Assert.Equal(new VinoAccount(issuer, 1, true, 10_000, 599, 599), s.GetAccount(issuer));
        s.Check();
    }

    [Fact]
    public void Meter_scenario_counts_and_refuses_in_order()
    {
        JsonElement m = G.GetProperty("meter");
        using Capability cap = _p.CreateRootCapability(CapabilityRights.AllAbi16);
        using SpendMeter meter = _p.CreateSpendMeter(cap, V.U64(m.GetProperty("daily_cap")));
        foreach (JsonElement st in m.GetProperty("steps").EnumerateArray())
        {
            ulong day = V.U64(st.GetProperty("day")), amount = V.U64(st.GetProperty("amount"));
            string status = V.Str(st, "status");
            if (status == "ZXV_E_RANGE")
            {
                Assert.Equal(ZxvStatus.AmountOutOfRange, Assert.Throws<ZxvUsageException>(() => meter.TryAdmit(day, amount, out _)).Status);
                continue;
            }

            bool admitted = meter.TryAdmit(day, amount, out ulong left);
            Assert.Equal(status == "ZXV_OK", admitted);
            Assert.Equal(V.U64(st.GetProperty("left")), left);
        }

        // Admit throws the refusal TryAdmit reports as false.
        using SpendMeter fresh = _p.CreateSpendMeter(cap, 10);
        Assert.Equal(4UL, fresh.Admit(1, 6));
        Assert.Equal(ZxvStatus.LimitExceeded, Assert.Throws<ZxvRefusedException>(() => fresh.Admit(1, 5)).Status);
        Assert.Equal(0UL, fresh.Admit(1, 4));
    }

    [Fact]
    public void Handles_refuse_use_after_dispose_and_outlive_their_platform()
    {
        using Capability cap = _p.CreateRootCapability(CapabilityRights.AllAbi16);
        PlaySandbox s = _p.CreatePlaySandbox(cap);
        SpendMeter m = _p.CreateSpendMeter(cap, 5);
        uint h = s.OpenHolder(1);
        s.Dispose();
        m.Dispose();
        Assert.True(s.IsDisposed);
        Assert.Throws<ObjectDisposedException>(() => s.GetAccount(h));
        Assert.Throws<ObjectDisposedException>(() => s.GetVouchers(h));
        Assert.Throws<ObjectDisposedException>(() => s.OpenIssuer(1));
        Assert.Throws<ObjectDisposedException>(() => m.Admit(1, 1));
        Assert.Throws<ObjectDisposedException>(() => m.TryAdmit(1, 1, out _));
        s.Dispose();
        m.Dispose();

        // R6: the native context stays alive while a sandbox holds it.
        ZxvPlatform p = TestPlatform.Create();
        Capability c = p.CreateRootCapability(CapabilityRights.AllAbi16);
        PlaySandbox live = p.CreatePlaySandbox(c);
        uint issuer = live.OpenIssuer(7);
        p.Dispose();
        c.Dispose();
        Assert.Throws<ObjectDisposedException>(() => live.PostBacking(issuer, 1));
        live.Dispose();
    }

    [Fact]
    public void Halted_context_refuses_every_play_and_meter_call()
    {
        using Capability cap = _p.CreateRootCapability(CapabilityRights.AllAbi16);
        using PlaySandbox s = _p.CreatePlaySandbox(cap);
        using SpendMeter m = _p.CreateSpendMeter(cap, 5);
        uint issuer = s.OpenIssuer(1);
        _p.Halt("test");
        Assert.Equal(ZxvStatus.Poisoned, Assert.Throws<ZxvFatalException>(() => s.PostBacking(issuer, 1)).Status);
        Assert.Throws<ZxvFatalException>(() => s.GetAccount(issuer));
        Assert.Throws<ZxvFatalException>(() => s.Check());
        Assert.Throws<ZxvFatalException>(() => m.Admit(1, 1));
        Assert.Throws<ZxvFatalException>(() => _p.CreatePlaySandbox(cap));

        // The native side refuses too, even without the SDK's halted mark.
        int rc = NativeMethods.VinoPlayBack(cap.Handle, GetHandle(s), issuer, 1);
        Assert.Equal(ZxvStatus.Poisoned, (ZxvStatus)rc);

        // Pure calls need no context and keep working.
        Assert.Equal(PaymentRequestBytes.Length, FirstRequest().Bytes.Length);
        PaymentRequest.Decode(FirstRequest());
    }

    private static ZxvVinoPlayHandle GetHandle(PlaySandbox s) =>
        (ZxvVinoPlayHandle)typeof(PlaySandbox).GetField("_handle", System.Reflection.BindingFlags.NonPublic | System.Reflection.BindingFlags.Instance)!.GetValue(s)!;
}
