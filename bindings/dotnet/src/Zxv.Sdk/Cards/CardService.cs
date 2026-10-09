// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0

using System;
using Zxv.Interop;
using Zxv.Ledger;

namespace Zxv.Cards;

/// <summary>The platform's charge-card networks and their check-digit schemes.</summary>
public enum CardNetwork
{
    /// <summary>Phoenix (Damm check digit).</summary>
    Phoenix = 1,

    /// <summary>Dragon (Luhn check digit, ISO/IEC 7812-1).</summary>
    Dragon = 2,

    /// <summary>Thunderbird (Verhoeff check digit).</summary>
    Thunderbird = 3,
}

/// <summary>An issued card. The full PAN never leaves the native core.</summary>
/// <param name="CardId">Card identifier.</param>
/// <param name="Network">Network.</param>
/// <param name="Account">Funding ledger account.</param>
/// <param name="MaskedPan">Masked PAN for display.</param>
public sealed record IssuedCard(ulong CardId, CardNetwork Network, AccountId Account, string MaskedPan);

/// <summary>The outcome of an authorization. A decline is a result, not an exception.</summary>
/// <param name="Approved">Whether the authorization was approved.</param>
/// <param name="ResponseCode">ISO 8583 response code (00 approved, 51 insufficient funds, ...).</param>
/// <param name="ApprovalCode">Approval code when approved; otherwise empty.</param>
public sealed record AuthorizationResult(bool Approved, string ResponseCode, string ApprovalCode);

/// <summary>
/// Charge cards on the Phoenix, Dragon and Thunderbird networks. Obtain it
/// from <see cref="ZxvPlatform.Cards"/>.
/// </summary>
/// <remarks>
/// Check digits are live when <see cref="ZxvFeatures.CardCheckDigits"/> is set.
/// Issue, reissue and authorize are pending kernel/src/cardnet and throw
/// <see cref="ZxvFeatureNotAvailableException"/> until <see cref="ZxvFeatures.Cards"/> is set.
/// A check digit is an integrity check, not proof that a card was issued.
/// </remarks>
public sealed class CardService
{
    private readonly ZxvContextHandle _ctx;

    internal CardService(ZxvContextHandle ctx)
    {
        _ctx = ctx;
    }

    /// <summary>Validates the check digit of a PAN for a network.</summary>
    /// <param name="network">The network.</param>
    /// <param name="pan">2 to 19 decimal digits, check digit last.</param>
    /// <returns><see langword="true"/> when the check digit is correct.</returns>
    public static bool IsCheckDigitValid(CardNetwork network, string pan)
    {
        Guard.Text(pan, nameof(pan));
        NativeCall.Check(NativeMethods.CardCheckDigit((int)network, pan, out int valid), "Cards.IsCheckDigitValid");
        return valid != 0;
    }

    /// <summary>Issues a card funded by a ledger account.</summary>
    /// <param name="network">The network.</param>
    /// <param name="account">The funding account.</param>
    /// <param name="limit">Spending limit in the account currency.</param>
    /// <returns>The issued card.</returns>
    public unsafe IssuedCard Issue(CardNetwork network, AccountId account, Money limit)
    {
        ulong id = 0;
        string masked = NativeCall.ReadText(
            (byte* b, nuint c, out nuint l) => NativeMethods.CardIssue(_ctx, (int)network, account.Value, limit.MinorUnits, out id, b, c, out l),
            "Cards.Issue");
        return new IssuedCard(id, network, account, masked);
    }

    /// <summary>Reissues a card (lost, stolen or expired); the old card is blocked.</summary>
    /// <param name="cardId">The card to replace.</param>
    /// <returns>The new card id.</returns>
    public ulong Reissue(ulong cardId)
    {
        NativeCall.Check(NativeMethods.CardReissue(_ctx, cardId, out ulong newId), "Cards.Reissue");
        return newId;
    }

    /// <summary>Authorizes an amount on a card.</summary>
    /// <param name="cardId">The card.</param>
    /// <param name="amount">Positive amount.</param>
    /// <returns>Approved or declined with an ISO 8583 response code.</returns>
    public unsafe AuthorizationResult Authorize(ulong cardId, Money amount)
    {
        Guard.Text(amount.Currency, nameof(amount));
        int response = 0;
        int rc = NativeText.TryRead(
            (byte* b, nuint c, out nuint l) => NativeMethods.CardAuthorize(_ctx, cardId, amount.MinorUnits, amount.Currency, out response, b, c, out l),
            out string approval);
        if (rc == (int)ZxvStatus.Declined)
        {
            return new AuthorizationResult(false, response.ToString("D2", System.Globalization.CultureInfo.InvariantCulture), string.Empty);
        }

        NativeCall.Check(rc, "Cards.Authorize");
        return new AuthorizationResult(true, "00", approval);
    }
}
