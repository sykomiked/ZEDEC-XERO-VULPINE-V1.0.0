// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0

using System;
using System.Threading;
using System.Threading.Tasks;
using Microsoft.Extensions.Logging;
using Zxv.Ledger;
using Zxv.Messaging;

namespace Zxv.Payments;

/// <summary>A customer credit transfer to book and send.</summary>
/// <param name="From">Debtor ledger account.</param>
/// <param name="To">Creditor ledger account.</param>
/// <param name="Amount">Positive amount in an ISO 4217 currency enabled on both accounts.</param>
public sealed record PaymentRequest(AccountId From, AccountId To, Money Amount)
{
    /// <summary>Gets the EndToEndId; generated when <see langword="null"/>.</summary>
    public string? EndToEndId { get; init; }

    /// <summary>Gets the debtor party; defaults to the ledger account name and identifier.</summary>
    public PartyAccount? Debtor { get; init; }

    /// <summary>Gets the creditor party; defaults to the ledger account name and identifier.</summary>
    public PartyAccount? Creditor { get; init; }
}

/// <summary>A booked payment.</summary>
/// <param name="EndToEndId">The EndToEndId (also the ledger reference).</param>
/// <param name="EntryId">Triple-ledger entry id.</param>
/// <param name="Amount">The amount.</param>
/// <param name="From">Debtor account.</param>
/// <param name="To">Creditor account.</param>
/// <param name="BookedAt">Booking time (from the platform <see cref="TimeProvider"/>).</param>
/// <param name="Message">The pacs.008 document handed to the transport.</param>
public sealed record PaymentReceipt(string EndToEndId, ulong EntryId, Money Amount, AccountId From, AccountId To, DateTimeOffset BookedAt, MxDocument Message);

/// <summary>
/// Books a customer credit transfer on the ledger and hands its pacs.008 to the
/// configured <see cref="IMxTransport"/>. Obtain it from <see cref="ZxvPlatform.Payments"/>.
/// </summary>
/// <remarks>
/// Order of work: (1) build and validate the pacs.008 (nothing is booked if the
/// message is invalid, e.g. a private unit such as VFV); (2) post on the ledger
/// atomically; (3) await the transport. If the transport fails the posting
/// stands and <see cref="ZxvTransportException"/> carries the receipt for
/// retransmission with <see cref="RetransmitAsync"/>. For internal transfers that
/// produce no wire message (including private units) use <see cref="LedgerService.Post"/>.
/// </remarks>
public sealed class PaymentService
{
    private readonly LedgerService _ledger;
    private readonly MessageService _messages;
    private readonly IMxTransport _transport;
    private readonly IdGenerator _ids;
    private readonly TimeProvider _time;
    private readonly string _accountPrefix;
    private readonly ILogger _logger;

    internal PaymentService(LedgerService ledger, MessageService messages, IMxTransport transport, IdGenerator ids, TimeProvider time, string accountPrefix, ILogger logger)
    {
        _ledger = ledger;
        _messages = messages;
        _transport = transport;
        _ids = ids;
        _time = time;
        _accountPrefix = accountPrefix;
        _logger = logger;
    }

    /// <summary>Gets the name of the configured transport.</summary>
    public string TransportName => _transport.Name;

    /// <summary>Books a payment and sends its pacs.008.</summary>
    /// <param name="request">The payment.</param>
    /// <param name="cancellationToken">Cancels before booking, or the transport send.</param>
    /// <returns>The receipt.</returns>
    /// <exception cref="ZxvException">Validation or ledger refusal; nothing was booked.</exception>
    /// <exception cref="ZxvTransportException">Booked, but the transport failed.</exception>
    public async Task<PaymentReceipt> SubmitAsync(PaymentRequest request, CancellationToken cancellationToken = default)
    {
        ArgumentNullException.ThrowIfNull(request);
        cancellationToken.ThrowIfCancellationRequested();

        string e2e = request.EndToEndId ?? _ids.NextEndToEndId();
        DateTimeOffset now = _time.GetUtcNow();
        Account from = _ledger.GetAccount(request.From);
        Account to = _ledger.GetAccount(request.To);
        var message = new Pacs008Message(
            _ids.NextMessageId(),
            now,
            request.Debtor ?? new PartyAccount(from.Name, ToIdentifier(from.Id)),
            request.Creditor ?? new PartyAccount(to.Name, ToIdentifier(to.Id)),
            request.Amount,
            e2e);

        MxDocument document = _messages.BuildPacs008(message);
        PostingReceipt posting = _ledger.Post(new Transfer(request.From, request.To, request.Amount, e2e));
        var receipt = new PaymentReceipt(e2e, posting.EntryId, request.Amount, request.From, request.To, now, document);

        await SendAsync(receipt, cancellationToken).ConfigureAwait(false);
        Log.PaymentSubmitted(_logger, e2e, posting.EntryId, _transport.Name);
        return receipt;
    }

    /// <summary>Sends the message of an already booked payment again.</summary>
    /// <param name="receipt">The receipt from <see cref="SubmitAsync"/> or <see cref="ZxvTransportException.Receipt"/>.</param>
    /// <param name="cancellationToken">Cancels the send.</param>
    /// <returns>A task that completes when the transport accepted the message.</returns>
    public Task RetransmitAsync(PaymentReceipt receipt, CancellationToken cancellationToken = default)
    {
        ArgumentNullException.ThrowIfNull(receipt);
        return SendAsync(receipt, cancellationToken);
    }

    /// <summary>Formats the external identifier of a ledger account (e.g. <c>ZXV-000042</c>).</summary>
    /// <param name="id">The account id.</param>
    /// <returns>The identifier used in DbtrAcct/CdtrAcct and camt.053 Acct.</returns>
    public string ToIdentifier(AccountId id) => FormattableString.Invariant($"{_accountPrefix}-{id.Value:D6}");

    private async Task SendAsync(PaymentReceipt receipt, CancellationToken cancellationToken)
    {
        try
        {
            await _transport.SendAsync(receipt.Message, cancellationToken).ConfigureAwait(false);
        }
        catch (Exception ex) when (ex is not OperationCanceledException)
        {
            Log.PaymentTransmissionFailed(_logger, ex, receipt.EndToEndId);
            throw new ZxvTransportException(receipt, ex);
        }
    }
}
