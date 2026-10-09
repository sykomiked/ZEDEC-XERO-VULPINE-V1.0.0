// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
//
// A tour of the ZXV .NET SDK: configure, open accounts, book a payment and
// emit its pacs.008, produce a camt.053 statement, and probe the features
// that are still being built in the kernel.

using System;
using System.Linq;
using System.Threading;
using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Hosting;
using Zxv;
using Zxv.Cards;
using Zxv.Currencies;
using Zxv.Interop;
using Zxv.Ledger;
using Zxv.Payments;

HostApplicationBuilder builder = Host.CreateApplicationBuilder(new HostApplicationBuilderSettings
{
    Args = args,
    ContentRootPath = AppContext.BaseDirectory, // appsettings.json sits next to the binary
});
builder.Services.AddZxv(builder.Configuration.GetSection(ZxvOptions.SectionName));
using IHost host = builder.Build();

ZxvPlatform zxv = host.Services.GetRequiredService<ZxvPlatform>();
using CancellationTokenSource cts = new(TimeSpan.FromSeconds(30));

Console.WriteLine($"libzxv ABI {zxv.AbiVersion}; features: {zxv.Features}");
Console.WriteLine($"ISO 4217 list published {zxv.Currencies.Iso4217PublicationDate}; {zxv.Currencies.ListIso().Count} currencies");
Console.WriteLine();

// 1. Currencies: ISO 4217 alpha codes on the wire, exact minor units.
Console.WriteLine("== Currencies enabled on this platform ==");
foreach (Currency c in zxv.Currencies.ListEnabled())
{
    string kind = c.IsIso4217 ? $"ISO 4217 {c.Numeric:D3}" : "platform-private (never on the wire)";
    Console.WriteLine($"  {c.Code,-4} {c.MinorUnits} minor units  {kind}  {c.Name}");
}

Console.WriteLine($"  Vino rails: DEBIT {CurrencyService.GetRailNumeric(VinoRail.Debit)}, CREDIT {CurrencyService.GetRailNumeric(VinoRail.Credit)}, EQUITY {CurrencyService.GetRailNumeric(VinoRail.Equity)} (internal numerics)");
Console.WriteLine($"  Caveat 810: {zxv.Currencies.GetCaveat(810)}");
Console.WriteLine();

// 2. Ledger: an issuer (settlement) account funds two holders.
Currency ngn = zxv.Currencies.GetEnabled("NGN");
Account settlement = zxv.Ledger.OpenAccount("Lagos settlement NGN", "NGN", AccountKind.Issuer);
Account ada = zxv.Ledger.OpenAccount("Adébáyọ̀ Okonkwo", "NGN");
Account chidi = zxv.Ledger.OpenAccount("Chidi Eze", "NGN");
zxv.Ledger.Post(new Transfer(settlement.Id, ada.Id, Money.FromDecimal(150_000.75m, ngn), "FUND-0001"));

// 3. Payment: book on the ledger and emit pacs.008 (transport: none in the sample).
PaymentReceipt receipt = await zxv.Payments.SubmitAsync(
    new PaymentRequest(ada.Id, chidi.Id, Money.FromDecimal(25_000.50m, ngn)), cts.Token);
Console.WriteLine($"== Payment {receipt.EndToEndId} booked as entry {receipt.EntryId} ==");
Console.WriteLine(receipt.Message.Xml);

foreach (Account a in new[] { settlement, ada, chidi })
{
    RailBalances b = zxv.Ledger.GetBalances(a.Id);
    Console.WriteLine($"  {a.Name,-24} DEBIT(846) {b.Debit,16}  CREDIT(810) {b.Credit,16}  EQUITY(888) {b.Equity,16}");
}

zxv.Ledger.Verify();
Console.WriteLine("  ledger invariants hold");
Console.WriteLine();

// 4. A refused payment is an exception with a stable code.
try
{
    await zxv.Payments.SubmitAsync(new PaymentRequest(chidi.Id, ada.Id, Money.FromDecimal(1_000_000m, ngn)), cts.Token);
}
catch (ZxvException ex) when (ex.Status == ZxvStatus.InsufficientFunds)
{
    Console.WriteLine($"Refused as expected: {ex.Code}: {ex.Detail}");
}

// 5. Statement: camt.053 reconciled from the journal.
var statement = zxv.Statements.BuildStatement(ada.Id);
Console.WriteLine($"== camt.053 for {statement.Account.Name}: opening {statement.Opening}, closing {statement.Closing}, {statement.Entries.Count} entries ==");
Console.WriteLine(statement.Document.Xml);

// 6. Features still being built in the kernel fail cleanly.
Console.WriteLine("== Feature probe ==");
Console.WriteLine($"  Dragon (Luhn) 79927398713 valid: {CardService.IsCheckDigitValid(CardNetwork.Dragon, "79927398713")}");
foreach ((string name, Action probe) in new (string, Action)[]
{
    ("netting cycle", () => zxv.Netting.OpenCycle("CYCLE-1", "NGN").Dispose()),
    ("card issue", () => zxv.Cards.Issue(CardNetwork.Phoenix, ada.Id, Money.FromDecimal(50_000m, ngn))),
    ("VSS level", () => zxv.Conformance.GetLevel()),
    ("pacs.002", () => _ = zxv.Messages.IsSupported(Zxv.Messaging.MxMessageKind.Pacs002) ? 0 : throw new ZxvFeatureNotAvailableException("Messages", "pacs.002 not built in yet (kernel/src/cbank cb_mx.h)")),
})
{
    try
    {
        probe();
        Console.WriteLine($"  {name}: available");
    }
    catch (ZxvFeatureNotAvailableException ex)
    {
        Console.WriteLine($"  {name}: pending ({ex.Detail})");
    }
}

Console.WriteLine();
Console.WriteLine($"Accounts: {zxv.Ledger.ListAccounts().Count}; enabled currencies: {string.Join(", ", zxv.Currencies.ListEnabled().Select(c => c.Code))}");
