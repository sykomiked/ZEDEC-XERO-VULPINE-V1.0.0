<?php

// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0

declare(strict_types=1);

namespace Zedec\Zxv;

/**
 * ABI 1.5: a Web 4 key handle: an ML-DSA-65 key pair and the agent's
 * signed card (zxv_w4_agent). Also used for a human's key (the human id is
 * the handle's agentId). It belongs to its capability's context, is freed
 * before it (R6) and fails closed with it. NOT thread-safe: sealing
 * advances its envelope sequence.
 *
 *     $alice = new Web4Agent($cap, "alice", $now);
 *     $env = $alice->seal($bob->agentId(), "request", "pay", 7, $now, "amount=100");
 *
 * SECRETS: the private key stays inside the handle and close() wipes it;
 * a seed passed as a string is copied to a native buffer that is zeroed
 * right after the call (the PHP string itself is yours to discard).
 */
final class Web4Agent extends NativeHandle
{
    /**
     * @param Capability $capability needs CRYPTO_SIGN
     * @param string $name 1..31 of [a-z0-9._-]
     * @param int|string|float $nowMs the binding time (Unix ms)
     * @param SecretKey|string|null $seed 32 bytes for a reproducible key, or null for a fresh one
     * @param ?Web4Agent $node the node hosting this agent (signs the binding; same context), or null for self-bound
     * @throws Exception\CapabilityDeniedException without CRYPTO_SIGN
     * @throws Exception\ZxvUsageException ZXV_E_ARG (bad name characters), ZXV_E_TOO_LONG
     */
    public function __construct(public readonly Capability $capability, string $name, int|string|float $nowMs, SecretKey|string|null $seed = null, ?self $node = null)
    {
        $s = null;
        if ($seed instanceof SecretKey) {
            $s = SecretKey::expect($seed, 32);
        } elseif ($seed !== null) {
            $s = Native::fixed($seed, "seed", 32);
        }
        $out = Native::new("zxv_w4_agent*");
        $rc = Native::ffi15()->zxv_w4_agent_create($capability->ptr(), $s, Native::cstr($name, "name"), $node?->ptr(), Native::u64($nowMs, "nowMs"), \FFI::addr($out));
        if ($s !== null && !($seed instanceof SecretKey)) {
            \FFI::memset($s, 0, 32);
        }
        Native::check($rc, $capability->context);
        parent::__construct($capability->context, $out, "zxv_w4_agent_destroy");
    }

    /** SHA3-256 of the public key (32 bytes). */
    public function agentId(): string
    {
        return $this->facts()[0];
    }

    /** The hosting node's id (== agentId when self-bound). */
    public function peerId(): string
    {
        return $this->facts()[1];
    }

    /** The ML-DSA-65 public key (Web4::PK_BYTES). */
    public function publicKey(): string
    {
        return $this->facts()[2];
    }

    /** The agent's card: what a peer needs to verify it (Web4::verifyCard). */
    public function card(): string
    {
        return Native::bytesOut(fn ($b, $c, $l) => Native::ffi15()->zxv_w4_agent_card($this->ptr(), $b, $c, $l), Web4::CARD_MAX, $this->context);
    }

    /**
     * Build, sign and encode an envelope from this agent (the next sequence number).
     * @param string $to the recipient's 32-byte agent id
     * @param string|int $kind a Web4::KINDS name
     * @param ?string $consent a consent token to attach
     * @return string the envelope bytes
     */
    public function seal(string $to, string|int $kind, string $tool, int|string|float $reqId, int|string|float $nowMs, string $payload = "", int $chunk = 0, bool $final = false, ?string $consent = null): string
    {
        $k = Web4::flags(Web4::KINDS, $kind, "kind");
        $t = Web4::id32($to, "to");
        $tok = $consent === null ? null : Web4::token($consent);
        $p = Native::bytes($payload, true);
        $args = [Native::u64($reqId, "reqId"), Native::uint32($chunk, "chunk"), Native::cstr($tool, "tool"), $final ? Web4::MF_FINAL : 0];
        $now = Native::u64($nowMs, "nowMs");
        return Native::bytesOut(fn ($b, $c, $l) => Native::ffi15()->zxv_w4_env_seal($this->capability->ptr(), $this->ptr(), $k, $t, $args[0], $args[1], $args[2], $args[3],
            $p, strlen($payload), $tok, $now, $b, $c, $l), Web4::ENV_MAX, $this->context);
    }

    /**
     * Seal a reply to a REQUEST envelope addressed to this agent: to/from
     * swapped, same request id and tool. The request's signature is not
     * checked here: verify it first (Web4Envelope::verify).
     * @param string|int $kind "response", "stream" or "error"
     * @throws Exception\ZxvUsageException ZXV_E_ARG (not a REQUEST, or not addressed to this agent)
     */
    public function reply(Web4Envelope|string $request, string|int $kind, int|string|float $nowMs, string $payload = "", int $chunk = 0, bool $final = false): string
    {
        $r = $request instanceof Web4Envelope ? $request->bytes : $request;
        $rm = Native::bytes($r, true);
        $k = Web4::flags(Web4::KINDS, $kind, "kind");
        $p = Native::bytes($payload, true);
        $ch = Native::uint32($chunk, "chunk");
        $now = Native::u64($nowMs, "nowMs");
        return Native::bytesOut(fn ($b, $c, $l) => Native::ffi15()->zxv_w4_env_reply($this->capability->ptr(), $this->ptr(), $rm, strlen($r), $k, $ch,
            $final ? Web4::MF_FINAL : 0, $p, strlen($payload), $now, $b, $c, $l), Web4::ENV_MAX, $this->context);
    }

    /**
     * Issue a consent token on the HUMAN's side (this handle is the human's
     * key), after showing them the action: lets $agentId do $action within
     * $scope, money up to $maxVfv minor units, valid in [issuedMs, expiresMs).
     * @param string $action 32 bytes (Web4::actionDigest)
     * @param string|int|list<string> $scope Web4::CONSENT_SCOPES (nonzero)
     * @param ?string $nonce 16 bytes, or null for a random one
     * @param ?Capability $capability needs ATTEST (default: this handle's)
     * @return string the token (Web4::CONSENT_BYTES)
     * @throws Exception\CapabilityDeniedException without ATTEST
     */
    public function issueConsent(string $agentId, string $action, string|int|array $scope, int|string|float $maxVfv, int|string|float $issuedMs, int|string|float $expiresMs, ?string $nonce = null, ?Capability $capability = null): string
    {
        $cap = $capability ?? $this->capability;
        $n = $nonce === null ? null : Native::fixed($nonce, "nonce", 16);
        $out = Native::new("uint8_t[" . Web4::CONSENT_BYTES . "]");
        Native::check(Native::ffi15()->zxv_w4_consent_issue($cap->ptr(), $this->ptr(), Web4::id32($agentId, "agentId"), Web4::id32($action, "action"),
            Web4::flags(Web4::CONSENT_SCOPES, $scope, "scope"), Native::u64($maxVfv, "maxVfv"), Native::u64($issuedMs, "issuedMs"),
            Native::u64($expiresMs, "expiresMs"), $n, $out), $this->context);
        return Native::read($out, Web4::CONSENT_BYTES);
    }

    /** @return array<string, mixed> never key material */
    public function __debugInfo(): array
    {
        return ["closed" => $this->isClosed()];
    }

    /** @return array{0:string,1:string,2:string} */
    private function facts(): array
    {
        $a = Native::new("uint8_t[32]");
        $p = Native::new("uint8_t[32]");
        $k = Native::new("uint8_t[" . Web4::PK_BYTES . "]");
        Native::check(Native::ffi15()->zxv_w4_agent_info($this->ptr(), $a, $p, $k), $this->context);
        return [Native::read($a, 32), Native::read($p, 32), Native::read($k, Web4::PK_BYTES)];
    }
}
