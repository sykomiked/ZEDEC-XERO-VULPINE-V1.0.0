// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0

using System.Collections.Generic;
using System.Globalization;
using System.Linq;
using Zxv.Cards;
using Zxv.Currencies;
using Zxv.Ledger;
using Zxv.Messaging;
using Zxv.Payments;

namespace Zxv.Gateway.Endpoints;

/// <summary>
/// An exact amount on the wire. <c>minorUnits</c> is authoritative (an integer
/// within ±(2^53 - 1), safe in every JSON parser); <c>display</c> is informational.
/// </summary>
public sealed record MoneyDto(string Currency, long MinorUnits, int Scale, string Display)
{
    public static MoneyDto From(Money m) =>
        new(m.Currency, m.MinorUnits, m.Scale, m.ToDecimal().ToString("F" + m.Scale.ToString(CultureInfo.InvariantCulture), CultureInfo.InvariantCulture));
}

/// <summary>An amount in a request: currency and exact minor units.</summary>
public sealed record AmountInput(string Currency, long MinorUnits);

public sealed record PlatformDto(string AbiVersion, IReadOnlyList<string> Features, string Transport);

public sealed record CurrencyDto(string Code, int? Numeric, int? MinorUnits, string? Name, bool Iso4217, bool WireCurrency, IReadOnlyList<string> Attributes)
{
    public static CurrencyDto From(Currency c) => new(
        c.Code, c.Numeric, c.MinorUnits, c.Name, c.IsIso4217, c.IsWireCurrency,
        System.Enum.GetValues<CurrencyAttributes>().Where(a => a != CurrencyAttributes.None && c.Attributes.HasFlag(a)).Select(a => a.ToString()).ToList());
}

public sealed record EnableCurrencyRequest(string Code, int? MinorUnits);

public sealed record OpenAccountRequest(string Name, string Currency, AccountKind Kind = AccountKind.Holder);

public sealed record AccountDto(uint Id, string Identifier, string Name, string Currency, AccountKind Kind);

public sealed record BalancesDto(uint AccountId, MoneyDto Debit, MoneyDto Credit, MoneyDto Equity)
{
    /// <summary>Platform-internal rail numerics, for proprietary fields only.</summary>
    public IReadOnlyDictionary<string, int> RailNumerics { get; } = new Dictionary<string, int> { ["debit"] = 555, ["credit"] = 777, ["equity"] = 888 };

    public static BalancesDto From(AccountId id, RailBalances b) => new(id.Value, MoneyDto.From(b.Debit), MoneyDto.From(b.Credit), MoneyDto.From(b.Equity));
}

public sealed record EntryDto(ulong EntryId, MoneyDto Amount, uint Counterparty, string Reference)
{
    public static EntryDto From(LedgerEntry e) => new(e.EntryId, MoneyDto.From(e.Amount), e.Counterparty.Value, e.Reference);
}

public sealed record PartyInput(string Name, string Account);

public sealed record PaymentInput(uint FromAccount, uint ToAccount, AmountInput Amount, string? EndToEndId, PartyInput? Debtor, PartyInput? Creditor);

public sealed record PaymentDto(string EndToEndId, ulong EntryId, MoneyDto Amount, uint FromAccount, uint ToAccount, System.DateTimeOffset BookedAt, string MessageId, string Pacs008)
{
    public static PaymentDto From(PaymentReceipt r) =>
        new(r.EndToEndId, r.EntryId, MoneyDto.From(r.Amount), r.From.Value, r.To.Value, r.BookedAt, r.Message.MessageId, r.Message.Xml);
}

public sealed record StatementDto(uint AccountId, MoneyDto Opening, MoneyDto Closing, IReadOnlyList<EntryDto> Entries, int? NextStart, string MessageId, string Camt053)
{
    public static StatementDto From(AccountStatement s) => new(
        s.Account.Id.Value, MoneyDto.From(s.Opening), MoneyDto.From(s.Closing), s.Entries.Select(EntryDto.From).ToList(), s.NextStart, s.Document.MessageId, s.Document.Xml);
}

public sealed record OpenCycleRequest(string CycleId, string Currency, IReadOnlyList<string> Participants);

public sealed record ObligationInput(string PaymentId, string Debtor, string Creditor, long MinorUnits);

public sealed record NetPositionDto(string Participant, MoneyDto Net);

public sealed record IssueCardRequest(CardNetwork Network, uint Account, long LimitMinorUnits);

public sealed record CardDto(ulong CardId, CardNetwork Network, uint Account, string MaskedPan);

public sealed record ReissueDto(ulong OldCardId, ulong NewCardId);

public sealed record AuthorizeRequest(AmountInput Amount);

public sealed record AuthorizationDto(bool Approved, string ResponseCode, string ApprovalCode);

public sealed record CheckDigitRequest(CardNetwork Network, string Pan);

public sealed record CheckDigitDto(bool Valid);

public sealed record ConformanceDto(bool LedgerInvariantsHold, string LedgerDetail, string? VssLevel, string VssStatus);
