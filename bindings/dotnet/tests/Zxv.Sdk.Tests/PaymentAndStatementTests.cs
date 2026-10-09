// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0

using System;
using System.Collections.Concurrent;
using System.IO;
using System.Linq;
using System.Threading;
using System.Threading.Tasks;
using Microsoft.Extensions.Time.Testing;
using Zxv.Interop;
using Zxv.Ledger;
using Zxv.Messaging;
using Zxv.Payments;

namespace Zxv.Sdk.Tests;

public sealed class PaymentAndStatementTests
{
    private static readonly DateTimeOffset Start = new(2026, 10, 9, 8, 30, 0, TimeSpan.Zero);

    private sealed class RecordingTransport : IMxTransport
    {
        public ConcurrentQueue<MxDocument> Sent { get; } = new();

        public bool Fail { get; set; }

        public string Name => "recording";

        public async Task SendAsync(MxDocument document, CancellationToken cancellationToken)
        {
            await Task.Yield();
            if (Fail)
            {
                throw new IOException("link down");
            }

            Sent.Enqueue(document);
        }
    }

    [Fact]
    public async Task Submit_books_then_transmits_pacs008()
    {
        var transport = new RecordingTransport();
        using ZxvPlatform p = TestPlatform.Create(new FakeTimeProvider(Start), transport);
        (_, Account ada) = TestPlatform.Funded(p, "NGN", 1_000_000);
        Account kwame = p.Ledger.OpenAccount("Kwame", "NGN");
        Money amount = Money.FromDecimal(2500.50m, p.Currencies.GetEnabled("NGN"));

        PaymentReceipt r = await p.Payments.SubmitAsync(new PaymentRequest(ada.Id, kwame.Id, amount));

        Assert.StartsWith("ZXVE20261009083000", r.EndToEndId, StringComparison.Ordinal);
        Assert.True(r.EndToEndId.Length <= 35);
        Assert.Equal(Start, r.BookedAt);
        Assert.Equal(250_050, p.Ledger.GetAvailable(kwame.Id).MinorUnits);
        MxDocument sent = Assert.Single(transport.Sent);
        Assert.Same(r.Message, sent);
        Assert.Contains("<EndToEndId>" + r.EndToEndId + "</EndToEndId>", sent.Xml, StringComparison.Ordinal);
        Assert.Contains(p.Payments.ToIdentifier(ada.Id), sent.Xml, StringComparison.Ordinal);
    }

    [Fact]
    public async Task Invalid_wire_currency_books_nothing()
    {
        using ZxvPlatform p = TestPlatform.Create();
        (_, Account a) = TestPlatform.Funded(p, "VFV", 1000);
        Account b = p.Ledger.OpenAccount("B", "VFV");
        var ex = await Assert.ThrowsAsync<ZxvException>(() => p.Payments.SubmitAsync(new PaymentRequest(a.Id, b.Id, Money.FromMinorUnits(10, p.Currencies.GetEnabled("VFV")))));
        Assert.Equal(ZxvStatus.InvalidCurrency, ex.Status);
        Assert.Equal(1000, p.Ledger.GetAvailable(a.Id).MinorUnits);
    }

    [Fact]
    public async Task Transport_failure_keeps_booking_and_allows_retransmit()
    {
        var transport = new RecordingTransport { Fail = true };
        using ZxvPlatform p = TestPlatform.Create(transport: transport);
        (_, Account a) = TestPlatform.Funded(p, "GHS", 1000);
        Account b = p.Ledger.OpenAccount("B", "GHS");
        var ex = await Assert.ThrowsAsync<ZxvTransportException>(() =>
            p.Payments.SubmitAsync(new PaymentRequest(a.Id, b.Id, Money.FromMinorUnits(400, p.Currencies.GetEnabled("GHS"))) { EndToEndId = "E2E-T1" }));
        Assert.NotNull(ex.Receipt);
        Assert.IsType<IOException>(ex.InnerException);
        Assert.Equal(400, p.Ledger.GetAvailable(b.Id).MinorUnits);

        transport.Fail = false;
        await p.Payments.RetransmitAsync(ex.Receipt!);
        Assert.Equal("E2E-T1", Assert.Single(transport.Sent).Xml.Split("<EndToEndId>")[1].Split('<')[0]);
    }

    [Fact]
    public async Task Cancellation_before_booking_books_nothing()
    {
        using ZxvPlatform p = TestPlatform.Create();
        (_, Account a) = TestPlatform.Funded(p, "NGN", 1000);
        Account b = p.Ledger.OpenAccount("B", "NGN");
        using var cts = new CancellationTokenSource();
        cts.Cancel();
        await Assert.ThrowsAnyAsync<OperationCanceledException>(() =>
            p.Payments.SubmitAsync(new PaymentRequest(a.Id, b.Id, Money.FromMinorUnits(1, p.Currencies.GetEnabled("NGN"))), cts.Token));
        Assert.Equal(0, p.Ledger.GetAvailable(b.Id).MinorUnits);
    }

    [Fact]
    public async Task Directory_transport_writes_one_file_per_message()
    {
        string dir = Path.Combine(Path.GetTempPath(), "zxv-drop-" + Guid.NewGuid().ToString("N"));
        try
        {
            using ZxvPlatform p = TestPlatform.Create(transport: new DirectoryMxTransport(dir));
            (_, Account a) = TestPlatform.Funded(p, "KES", 10_000);
            Account b = p.Ledger.OpenAccount("B", "KES");
            PaymentReceipt r = await p.Payments.SubmitAsync(new PaymentRequest(a.Id, b.Id, Money.FromMinorUnits(1234, p.Currencies.GetEnabled("KES"))));
            string file = Path.Combine(dir, r.Message.MessageId + ".xml");
            Assert.Equal(r.Message.Xml, await File.ReadAllTextAsync(file));
        }
        finally
        {
            if (Directory.Exists(dir))
            {
                Directory.Delete(dir, recursive: true);
            }
        }
    }

    [Fact]
    public void Statements_page_and_reconcile()
    {
        using ZxvPlatform p = TestPlatform.Create(new FakeTimeProvider(Start));
        var ngn = p.Currencies.GetEnabled("NGN");
        (_, Account a) = TestPlatform.Funded(p, "NGN", 1_000_000);
        Account b = p.Ledger.OpenAccount("B", "NGN");
        for (int i = 1; i <= 39; i++)
        {
            p.Ledger.Post(new Transfer(a.Id, b.Id, Money.FromMinorUnits(i * 100, ngn), "S-" + i));
        }

        AccountStatement page1 = p.Statements.BuildStatement(a.Id);
        Assert.Equal(32, page1.Entries.Count);
        Assert.Equal(32, page1.NextStart);
        Assert.Equal(0, page1.Opening.MinorUnits);

        AccountStatement page2 = p.Statements.BuildStatement(a.Id, new StatementRequest { Start = page1.NextStart!.Value });
        Assert.Equal(8, page2.Entries.Count);
        Assert.Null(page2.NextStart);
        Assert.Equal(page1.Closing, page2.Opening);
        Assert.Equal(p.Ledger.GetAvailable(a.Id), page2.Closing);
        Assert.Contains("<CreDtTm>2026-10-09T08:30:00Z</CreDtTm>", page2.Document.Xml, StringComparison.Ordinal);
        Assert.Equal(1_000_000 - (39 * 40 / 2 * 100), page2.Closing.MinorUnits);
        Assert.Single(page2.Entries, e => e.Reference == "S-39");
    }
}
