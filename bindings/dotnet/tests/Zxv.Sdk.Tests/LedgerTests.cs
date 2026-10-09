// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0

using System;
using System.Linq;
using System.Threading;
using System.Threading.Tasks;
using Zxv.Currencies;
using Zxv.Interop;
using Zxv.Ledger;

namespace Zxv.Sdk.Tests;

public sealed class LedgerTests : IDisposable
{
    private readonly ZxvPlatform _p = TestPlatform.Create();
    private readonly Currency _ngn;

    public LedgerTests() => _ngn = _p.Currencies.GetEnabled("NGN");

    public void Dispose() => _p.Dispose();

    private Money Ngn(long minor) => Money.FromMinorUnits(minor, _ngn);

    [Fact]
    public void Post_moves_exact_minor_units_across_three_rails()
    {
        (Account issuer, Account ada) = TestPlatform.Funded(_p, "NGN", 1_500_075, "Ada Obi");
        Account kwame = _p.Ledger.OpenAccount("Kwame Mensah", "NGN");
        PostingReceipt r = _p.Ledger.Post(new Transfer(ada.Id, kwame.Id, Ngn(250_050), "E2E-1"));
        Assert.True(r.EntryId > 0);

        RailBalances a = _p.Ledger.GetBalances(ada.Id);
        Assert.Equal(1_500_075, a.Debit.MinorUnits);
        Assert.Equal(250_050, a.Credit.MinorUnits);
        Assert.Equal(1_250_025, a.Equity.MinorUnits);
        Assert.Equal(250_050, _p.Ledger.GetAvailable(kwame.Id).MinorUnits);
        Assert.Equal(-1_500_075, _p.Ledger.GetAvailable(issuer.Id).MinorUnits);
        _p.Ledger.Verify();
    }

    [Fact]
    public void Holder_cannot_overdraw()
    {
        (_, Account ada) = TestPlatform.Funded(_p, "NGN", 100);
        Account b = _p.Ledger.OpenAccount("B", "NGN");
        ZxvException ex = Assert.Throws<ZxvException>(() => _p.Ledger.Post(new Transfer(ada.Id, b.Id, Ngn(101), "OVR-1")));
        Assert.Equal(ZxvStatus.InsufficientFunds, ex.Status);
        Assert.Equal(100, _p.Ledger.GetAvailable(ada.Id).MinorUnits); // nothing written
    }

    [Fact]
    public void Duplicate_reference_is_refused()
    {
        (_, Account ada) = TestPlatform.Funded(_p, "NGN", 1000);
        Account b = _p.Ledger.OpenAccount("B", "NGN");
        _p.Ledger.Post(new Transfer(ada.Id, b.Id, Ngn(1), "DUP-1"));
        Assert.Equal(ZxvStatus.Duplicate, Assert.Throws<ZxvException>(() => _p.Ledger.Post(new Transfer(ada.Id, b.Id, Ngn(1), "DUP-1"))).Status);
    }

    [Fact]
    public void No_implicit_fx()
    {
        (_, Account ada) = TestPlatform.Funded(_p, "NGN", 1000);
        Account gh = _p.Ledger.OpenAccount("Accra", "GHS");
        Assert.Equal(ZxvStatus.InvalidCurrency, Assert.Throws<ZxvException>(() => _p.Ledger.Post(new Transfer(ada.Id, gh.Id, Ngn(1), "FX-1"))).Status);
    }

    [Theory]
    [InlineData(0)]
    [InlineData(-5)]
    [InlineData(Money.MaxMinorUnits + 1)]
    public void Amount_out_of_range(long minor)
    {
        (_, Account ada) = TestPlatform.Funded(_p, "NGN", 1000);
        Account b = _p.Ledger.OpenAccount("B", "NGN");
        Assert.Equal(ZxvStatus.AmountOutOfRange, Assert.Throws<ZxvException>(() => _p.Ledger.Post(new Transfer(ada.Id, b.Id, Ngn(minor), "RNG-" + minor))).Status);
    }

    [Theory]
    [InlineData("Adébáyọ̀ Ọláòṣebìkan")]
    [InlineData("አበበ ቢቂላ")]
    [InlineData("محمد بن علي")]
    [InlineData("Ngũgĩ wa Thiong'o")]
    public void Utf8_names_round_trip(string name)
    {
        Account a = _p.Ledger.OpenAccount(name, "KES");
        Assert.Equal(name, _p.Ledger.GetAccount(a.Id).Name);
    }

    [Fact]
    public void Name_length_is_measured_in_utf8_bytes()
    {
        Assert.Equal(ZxvStatus.TooLong, Assert.Throws<ZxvException>(() => _p.Ledger.OpenAccount(new string('é', 32), "NGN")).Status); // 64 bytes
        _p.Ledger.OpenAccount(new string('a', 63), "NGN");
    }

    [Fact]
    public void Journal_lines_are_signed_and_ordered()
    {
        (Account issuer, Account ada) = TestPlatform.Funded(_p, "NGN", 10_000);
        Account b = _p.Ledger.OpenAccount("B", "NGN");
        _p.Ledger.Post(new Transfer(ada.Id, b.Id, Ngn(300), "J-1"));
        _p.Ledger.Post(new Transfer(b.Id, ada.Id, Ngn(100), "J-2"));
        var lines = _p.Ledger.GetEntries(ada.Id);
        Assert.Equal(new long[] { 10_000, -300, 100 }, lines.Select(l => l.Amount.MinorUnits));
        Assert.Equal(issuer.Id, lines[0].Counterparty);
        Assert.Equal("J-2", lines[2].Reference);
        Assert.Equal(lines[1].EntryId, _p.Ledger.GetEntries(b.Id)[0].EntryId);
        Assert.Single(_p.Ledger.GetEntries(ada.Id, 1, 1));
    }

    [Fact]
    public void Account_listing()
    {
        Account a = _p.Ledger.OpenAccount("Lomé", "XOF", AccountKind.Issuer);
        Assert.Contains(_p.Ledger.ListAccounts(), x => x == a);
    }

    [Fact]
    public async Task Concurrent_postings_are_exact()
    {
        Account issuer = _p.Ledger.OpenAccount("Settlement", "NGN", AccountKind.Issuer);
        Account[] pools = Enumerable.Range(0, 8).Select(i => _p.Ledger.OpenAccount("Pool " + i, "NGN")).ToArray();
        await Task.WhenAll(pools.Select((pool, i) => Task.Run(() =>
        {
            for (int k = 0; k < 50; k++)
            {
                _p.Ledger.Post(new Transfer(issuer.Id, pool.Id, Ngn(k + 1), $"C-{i}-{k}"));
            }
        })));
        Assert.All(pools, pool => Assert.Equal(1275, _p.Ledger.GetAvailable(pool.Id).MinorUnits));
        Assert.Equal(-8 * 1275, _p.Ledger.GetAvailable(issuer.Id).MinorUnits);
        Assert.True(_p.Ledger.TryVerify(out string detail), detail);
    }

    [Fact]
    public void PostMany_honours_cancellation()
    {
        (_, Account ada) = TestPlatform.Funded(_p, "NGN", 1000);
        Account b = _p.Ledger.OpenAccount("B", "NGN");
        using var cts = new CancellationTokenSource();
        cts.Cancel();
        Assert.Throws<OperationCanceledException>(() => _p.Ledger.PostMany(new[] { new Transfer(ada.Id, b.Id, Ngn(1), "PM-1") }, cts.Token));
        Assert.Equal(2, _p.Ledger.PostMany(new[] { new Transfer(ada.Id, b.Id, Ngn(1), "PM-2"), new Transfer(ada.Id, b.Id, Ngn(1), "PM-3") }).Count);
    }

    [Fact]
    public void Private_unit_lives_on_the_ledger()
    {
        (_, Account v) = TestPlatform.Funded(_p, "VFV", 5_00);
        Assert.Equal("VFV 5.00", _p.Ledger.GetAvailable(v.Id).ToString());
    }

    [Fact]
    public void Unknown_account()
    {
        Assert.Equal(ZxvStatus.NotFound, Assert.Throws<ZxvException>(() => _p.Ledger.GetBalances(new AccountId(9999))).Status);
    }
}
