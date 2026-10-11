<?php

// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0

declare(strict_types=1);

namespace Zedec\Zxv;

/**
 * An ISO 20022 message under construction (pacs.008, camt.053; other kinds
 * throw NotImplementedException until the kernel builds them).
 * Serialization only: libzxv has no network transport.
 *
 * Made through a Capability (needs LEDGER_READ), the message re-checks it
 * on every call: revoking it, or clearing its attestation, stops the message.
 */
final class Message extends NativeHandle
{
    public const PACS008 = 1;
    public const CAMT053 = 2;
    public const PACS002 = 3;
    public const PACS004 = 4;
    public const PACS009 = 5;
    public const CAMT056 = 6;
    public const CAMT029 = 7;

    public const MSG_ID = 1;
    public const CRE_DT_TM = 2;
    public const DEBTOR_NAME = 3;
    public const DEBTOR_ACCT = 4;
    public const CREDITOR_NAME = 5;
    public const CREDITOR_ACCT = 6;
    public const END_TO_END_ID = 7;
    public const CCY = 8;
    public const ACCT_ID = 9;

    public const AMT_SETTLEMENT = 1;
    public const AMT_OPENING = 2;
    public const AMT_CLOSING = 3;

    /** The capability the message re-checks on every call (null for the deprecated Context form). */
    public readonly ?Capability $capability;

    /**
     * @param Capability|Context $owner a Capability with LEDGER_READ. Passing a
     *     Context is deprecated since ABI 1.2 (no capability) and raises
     *     E_USER_DEPRECATED.
     */
    public function __construct(Capability|Context $owner, int $kind)
    {
        $out = Native::new("zxv_msg*");
        if ($owner instanceof Capability) {
            $this->capability = $owner;
            Native::check(Native::ffi()->zxv_msg_create_cap($owner->ptr(), $kind, \FFI::addr($out)), $owner->context);
            parent::__construct($owner->context, $out, "zxv_msg_destroy");
            return;
        }
        trigger_error("new Message(Context, kind) is deprecated since ABI 1.2: pass a Capability with LEDGER_READ", E_USER_DEPRECATED);
        $this->capability = null;
        Native::check(Native::ffi()->zxv_msg_create($owner->raw(), $kind, \FFI::addr($out)), $owner);
        parent::__construct($owner, $out, "zxv_msg_destroy");
    }

    /** @internal */
    public function ptr(): \FFI\CData
    {
        if ($this->capability !== null && $this->capability->isClosed()) {
            throw new Exception\ZxvUsageException(Status::E_STATE, "the capability of this Message is closed");
        }
        return parent::ptr();
    }

    /** Set a text field (plain text; the serializer escapes XML). */
    public function set(int $field, string $value): self
    {
        Native::check(Native::ffi()->zxv_msg_set_text($this->ptr(), $field, Native::utf8($value, "value")), $this->context);
        return $this;
    }

    /** Set an amount in exact minor units with $fracDigits decimals. */
    public function amount(int $field, int|float $units, int $fracDigits): self
    {
        Native::check(Native::ffi()->zxv_msg_set_amount($this->ptr(), $field, Native::int($units, "units"), Native::int($fracDigits, "fracDigits", 0, 255)), $this->context);
        return $this;
    }

    /** Serialize to XML. */
    public function render(): string
    {
        [$rc, $xml] = Native::text(fn ($b, $c, $l) => Native::ffi()->zxv_msg_render($this->ptr(), $b, $c, $l));
        Native::check($rc, $this->context);
        return (string) $xml;
    }
}
