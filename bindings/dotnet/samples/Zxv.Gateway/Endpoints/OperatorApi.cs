// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0

using System;
using System.Collections.Generic;
using System.Linq;
using System.Threading;
using System.Threading.Tasks;
using Microsoft.AspNetCore.Builder;
using Microsoft.AspNetCore.Http;
using Microsoft.AspNetCore.Http.HttpResults;
using Microsoft.AspNetCore.Routing;
using Zxv.Cards;
using Zxv.Currencies;
using Zxv.Gateway.Auth;
using Zxv.Gateway.Infrastructure;
using Zxv.Interop;
using Zxv.Ledger;
using Zxv.Messaging;
using Zxv.Netting;
using Zxv.Payments;

namespace Zxv.Gateway.Endpoints;

/// <summary>
/// The operator REST API, version 1. Mirrors api/openapi/zxv-operator.yaml:
/// operation ids, paths and status codes must stay in step (a test checks the paths).
/// </summary>
public static class OperatorApi
{
    public static IEndpointRouteBuilder MapOperatorApi(this IEndpointRouteBuilder app)
    {
        RouteGroupBuilder v1 = app.MapGroup("/v1").RequireAuthorization(GatewayAuthentication.OperatorPolicy).WithOpenApi();

        // Every operation may answer with RFC 9457 problem details (see ZxvExceptionHandler.HttpStatusFor).
        foreach (int status in new[] { 400, 401, 403, 404, 409, 422, 501, 503 })
        {
            v1.WithMetadata(new ProducesResponseTypeMetadata(status, typeof(Microsoft.AspNetCore.Mvc.ProblemDetails), new[] { "application/problem+json" }));
        }

        // ----- Platform -----
        v1.MapGet("/platform", (ZxvPlatform zxv) => TypedResults.Ok(new PlatformDto(
                zxv.AbiVersion.ToString(),
                Enum.GetValues<ZxvFeatures>().Where(f => f != ZxvFeatures.None && zxv.Supports(f)).Select(f => f.ToString()).ToList(),
                zxv.Payments.TransportName)))
            .WithName("getPlatform").WithTags("Platform").WithSummary("ABI version, live features and transport");

        // ----- Currencies -----
        RouteGroupBuilder ccy = v1.MapGroup("/currencies").WithTags("Currencies");
        ccy.MapGet("/", (ZxvPlatform zxv, string? scope) =>
                TypedResults.Ok((string.Equals(scope, "enabled", StringComparison.Ordinal) ? zxv.Currencies.ListEnabled() : zxv.Currencies.ListIso())
                    .Select(CurrencyDto.From).ToList()))
            .WithName("listCurrencies").WithSummary("ISO 4217 currencies (scope=iso, default) or those enabled on this platform (scope=enabled)");
        ccy.MapGet("/{code}", (ZxvPlatform zxv, string code) =>
                TypedResults.Ok(CurrencyDto.From(zxv.Currencies.IsEnabled(code) ? zxv.Currencies.GetEnabled(code) : zxv.Currencies.GetIso(code))))
            .WithName("getCurrency").WithSummary("One currency by ISO 4217 alpha code (or private unit code)");
        ccy.MapPost("/enabled", (ZxvPlatform zxv, EnableCurrencyRequest body) =>
            {
                Currency c = body.MinorUnits is int mu ? zxv.Currencies.Enable(body.Code, checked((byte)mu)) : zxv.Currencies.Enable(body.Code);
                return TypedResults.Created($"/v1/currencies/{c.Code}", CurrencyDto.From(c));
            })
            .WithName("enableCurrency").WithSummary("Enable an ISO 4217 currency for ledger accounts");

        // ----- Accounts -----
        RouteGroupBuilder acct = v1.MapGroup("/accounts").WithTags("Accounts");
        acct.MapPost("/", (ZxvPlatform zxv, OpenAccountRequest body) =>
            {
                Account a = zxv.Ledger.OpenAccount(body.Name, body.Currency, body.Kind);
                return TypedResults.Created($"/v1/accounts/{a.Id}", ToDto(zxv, a));
            })
            .WithName("openAccount").WithSummary("Open a holder or issuer account");
        acct.MapGet("/", (ZxvPlatform zxv) => TypedResults.Ok(zxv.Ledger.ListAccounts().Select(a => ToDto(zxv, a)).ToList()))
            .WithName("listAccounts").WithSummary("List accounts");
        acct.MapGet("/{id}", (ZxvPlatform zxv, uint id) => TypedResults.Ok(ToDto(zxv, zxv.Ledger.GetAccount(new AccountId(id)))))
            .WithName("getAccount").WithSummary("Get an account");
        acct.MapGet("/{id}/balances", (ZxvPlatform zxv, uint id) => TypedResults.Ok(BalancesDto.From(new AccountId(id), zxv.Ledger.GetBalances(new AccountId(id)))))
            .WithName("getBalances").WithSummary("DEBIT (846), CREDIT (810) and EQUITY (888) rail balances in exact minor units");
        acct.MapGet("/{id}/entries", (ZxvPlatform zxv, uint id, int? start, int? count) =>
                TypedResults.Ok(zxv.Ledger.GetEntries(new AccountId(id), start ?? 0, Math.Clamp(count ?? 100, 1, 1000)).Select(EntryDto.From).ToList()))
            .WithName("listEntries").WithSummary("Journal lines, oldest first");
        acct.MapGet("/{id}/statement", (ZxvPlatform zxv, uint id, int? start, int? maxEntries) =>
                TypedResults.Ok(StatementDto.From(zxv.Statements.BuildStatement(new AccountId(id), new StatementRequest
                {
                    Start = start ?? 0,
                    MaxEntries = maxEntries ?? MessageService.MaxStatementEntries,
                }))))
            .WithName("getStatement").WithTags("Statements").WithSummary("One reconciled camt.053 statement page");

        // ----- Payments -----
        RouteGroupBuilder pay = v1.MapGroup("/payments").WithTags("Payments");
        pay.MapPost("/", async Task<Created<PaymentDto>> (ZxvPlatform zxv, GatewayState state, PaymentInput body, CancellationToken ct) =>
            {
                ArgumentNullException.ThrowIfNull(body.Amount);
                Currency c = zxv.Currencies.GetEnabled(body.Amount.Currency);
                var request = new PaymentRequest(new AccountId(body.FromAccount), new AccountId(body.ToAccount), Money.FromMinorUnits(body.Amount.MinorUnits, c))
                {
                    EndToEndId = body.EndToEndId,
                    Debtor = body.Debtor is null ? null : new PartyAccount(body.Debtor.Name, body.Debtor.Account),
                    Creditor = body.Creditor is null ? null : new PartyAccount(body.Creditor.Name, body.Creditor.Account),
                };
                try
                {
                    PaymentReceipt r = await zxv.Payments.SubmitAsync(request, ct);
                    state.Payments[r.EndToEndId] = r;
                    return TypedResults.Created($"/v1/payments/{r.EndToEndId}", PaymentDto.From(r));
                }
                catch (ZxvTransportException tx) when (tx.Receipt is not null)
                {
                    state.Payments[tx.Receipt.EndToEndId] = tx.Receipt;
                    throw;
                }
            })
            .WithName("submitPayment").WithSummary("Book a customer credit transfer and emit its pacs.008");
        pay.MapGet("/{endToEndId}", Results<Ok<PaymentDto>, NotFound> (GatewayState state, string endToEndId) =>
                state.Payments.TryGetValue(endToEndId, out PaymentReceipt? r) ? TypedResults.Ok(PaymentDto.From(r)) : TypedResults.NotFound())
            .WithName("getPayment").WithSummary("A booked payment by EndToEndId");
        pay.MapGet("/{endToEndId}/pacs008", Results<ContentHttpResult, NotFound> (GatewayState state, string endToEndId) =>
                state.Payments.TryGetValue(endToEndId, out PaymentReceipt? r) ? TypedResults.Content(r.Message.Xml, "application/xml") : TypedResults.NotFound())
            .Produces<string>(StatusCodes.Status200OK, "application/xml")
            .WithName("getPaymentPacs008").WithSummary("The pacs.008 document of a payment");
        pay.MapPost("/{endToEndId}/retransmit", async Task<Results<NoContent, NotFound>> (ZxvPlatform zxv, GatewayState state, string endToEndId, CancellationToken ct) =>
            {
                if (!state.Payments.TryGetValue(endToEndId, out PaymentReceipt? r))
                {
                    return TypedResults.NotFound();
                }

                await zxv.Payments.RetransmitAsync(r, ct);
                return TypedResults.NoContent();
            })
            .WithName("retransmitPayment").WithSummary("Send the pacs.008 of a booked payment again");

        // ----- Netting -----
        RouteGroupBuilder net = v1.MapGroup("/netting-cycles").WithTags("Netting");
        net.MapPost("/", Results<Created<OpenCycleRequest>, Conflict> (ZxvPlatform zxv, GatewayState state, OpenCycleRequest body) =>
            {
                if (state.Cycles.ContainsKey(body.CycleId))
                {
                    return TypedResults.Conflict();
                }

                NettingCycle cycle = zxv.Netting.OpenCycle(body.CycleId, body.Currency);
                foreach (string p in body.Participants ?? Array.Empty<string>())
                {
                    cycle.AddParticipant(p);
                }

                if (!state.Cycles.TryAdd(body.CycleId, cycle))
                {
                    cycle.Dispose();
                    return TypedResults.Conflict();
                }

                return TypedResults.Created($"/v1/netting-cycles/{body.CycleId}", body);
            })
            .WithName("openNettingCycle").WithSummary("Open a multilateral netting cycle (pending kernel/src/cbank: 501 until built)");
        net.MapPost("/{cycleId}/obligations", Results<Accepted, NotFound> (GatewayState state, string cycleId, ObligationInput body) =>
            {
                if (!state.Cycles.TryGetValue(cycleId, out NettingCycle? cycle))
                {
                    return TypedResults.NotFound();
                }

                lock (cycle)
                {
                    cycle.Submit(body.PaymentId, body.Debtor, body.Creditor, Money.FromMinorUnits(body.MinorUnits, cycle.Currency));
                }

                return TypedResults.Accepted($"/v1/netting-cycles/{cycleId}");
            })
            .WithName("submitObligation").WithSummary("Submit one obligation (idempotent on paymentId)");
        net.MapPost("/{cycleId}/close", Results<Ok<List<NetPositionDto>>, NotFound> (GatewayState state, string cycleId) =>
            {
                if (!state.Cycles.TryGetValue(cycleId, out NettingCycle? cycle))
                {
                    return TypedResults.NotFound();
                }

                lock (cycle)
                {
                    return TypedResults.Ok(cycle.Close().Select(p => new NetPositionDto(p.Participant, MoneyDto.From(p.Net))).ToList());
                }
            })
            .WithName("closeNettingCycle").WithSummary("Close the cycle and return net positions (sum to zero)");

        // ----- Cards -----
        RouteGroupBuilder cards = v1.MapGroup("/cards").WithTags("Cards");
        cards.MapPost("/", (ZxvPlatform zxv, IssueCardRequest body) =>
            {
                Account a = zxv.Ledger.GetAccount(new AccountId(body.Account));
                IssuedCard card = zxv.Cards.Issue(body.Network, a.Id, Money.FromMinorUnits(body.LimitMinorUnits, zxv.Currencies.GetEnabled(a.Currency)));
                return TypedResults.Created($"/v1/cards/{card.CardId}", new CardDto(card.CardId, card.Network, card.Account.Value, card.MaskedPan));
            })
            .WithName("issueCard").WithSummary("Issue a charge card (pending kernel/src/cardnet: 501 until built)");
        cards.MapPost("/{cardId}/reissue", (ZxvPlatform zxv, ulong cardId) => TypedResults.Ok(new ReissueDto(cardId, zxv.Cards.Reissue(cardId))))
            .WithName("reissueCard").WithSummary("Reissue a card; the old card is blocked");
        cards.MapPost("/{cardId}/authorizations", (ZxvPlatform zxv, ulong cardId, AuthorizeRequest body) =>
            {
                AuthorizationResult r = zxv.Cards.Authorize(cardId, Money.FromMinorUnits(body.Amount.MinorUnits, zxv.Currencies.GetEnabled(body.Amount.Currency)));
                return TypedResults.Ok(new AuthorizationDto(r.Approved, r.ResponseCode, r.ApprovalCode));
            })
            .WithName("authorizeCard").WithSummary("Authorize an amount; a decline is 200 with approved=false and an ISO 8583 response code");
        cards.MapPost("/check-digit", (CheckDigitRequest body) => TypedResults.Ok(new CheckDigitDto(CardService.IsCheckDigitValid(body.Network, body.Pan))))
            .WithName("checkCardDigit").WithSummary("Validate a PAN check digit (Damm, Luhn or Verhoeff by network)");

        // ----- Conformance -----
        v1.MapGet("/conformance", (ZxvPlatform zxv) =>
            {
                bool ok = zxv.Ledger.TryVerify(out string detail);
                string? level = null;
                string status = "pending: kernel/src/cbank cb_vss.h";
                if (zxv.Supports(ZxvFeatures.Conformance))
                {
                    level = zxv.Conformance.GetLevel().ToString();
                    status = "live";
                }

                return TypedResults.Ok(new ConformanceDto(ok, detail, level, status));
            })
            .WithName("getConformance").WithTags("Conformance").WithSummary("Ledger invariants and VSS conformance level (self-assessment, not certification)");

        return app;
    }

    private static AccountDto ToDto(ZxvPlatform zxv, Account a) => new(a.Id.Value, zxv.Payments.ToIdentifier(a.Id), a.Name, a.Currency, a.Kind);
}
