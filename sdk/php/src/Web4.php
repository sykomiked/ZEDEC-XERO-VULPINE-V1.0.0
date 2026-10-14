<?php

// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0

declare(strict_types=1);

namespace Zedec\Zxv;

use FFI\CData;
use Zedec\Zxv\Exception\ZxvUsageException;

/**
 * ABI 1.5: the Web 4 agent layer (zxv_w4_*): agents with ML-DSA-65 keys
 * (FIPS 204), signed tool manifests, a directory of verified agent cards,
 * human consent tokens and signed envelopes.
 *
 * IDENTITY: agent_id = SHA3-256(agent public key). An agent is bound to the
 * node hosting it by a binding its node signs (peer_id = SHA3-256(node
 * public key)); a self-bound agent has peer_id == agent_id.
 * CONTEXTS: every signature carries a FIPS 204 context naming its purpose,
 * so a signature for one purpose cannot be replayed as another.
 * CONSENT: an agent never moves money or shares personal data without a
 * token signed by the HUMAN's key over the exact action (actionDigest),
 * scope, amount, validity and a nonce. Tokens are single-use (Web4ConsentLog).
 * SECRETS: a private key never leaves its Web4Agent handle: no call returns
 * it, and close() wipes it. Never log seeds.
 * TIME: there is no clock in here: calls that need the time take $nowMs.
 *
 * Capabilities: a Web4Agent and everything it signs need CRYPTO_SIGN;
 * issuing consent needs ATTEST. Parsing, verifying, digests, manifests, the
 * directory and the consent log need none.
 *
 * Honest limits: envelopes are signed, not encrypted. A consent token
 * proves that a key holder signed; that the holder is the right human and
 * understood what they signed depends on the device and UI that showed the
 * action, which this library cannot see. The receiving runtime (replay
 * window, rate limits), carriage over freight, and Web 2 / Web 3 bridges
 * are NOT here. Interoperability is between ZXV instances only.
 */
final class Web4
{
    public const PK_BYTES = 1952;
    public const SIG_BYTES = 3309;
    public const ID_BYTES = 32;
    public const NAME_MAX = 31;
    public const MAX_TOOLS = 16;
    public const MSG_MAX_PAYLOAD = 2048;
    public const CONSENT_BYTES = 3453;
    public const CARD_MAX = 7259;
    public const MANIFEST_MAX = 4349;
    public const ENV_MAX = 8970;
    public const DIR_MAX = 4096;
    public const CONSENT_LOG_MAX = 65536;
    /** Tool flags: money (moves money), pii (reveals personal data), stream (answers with STREAM chunks). */
    public const TOOL_FLAGS = ["money" => 0x01, "pii" => 0x02, "stream" => 0x04];
    public const CONSENT_SCOPES = ["money" => 0x01, "pii" => 0x02];
    /** Envelope kinds. */
    public const KINDS = ["request" => 1, "response" => 2, "stream" => 3, "cancel" => 4, "error" => 5];
    /** Envelope flag: last STREAM chunk / final response. */
    public const MF_FINAL = 0x01;
    /** Envelope flag: a consent token is attached (set by sealing). */
    public const MF_CONSENT = 0x02;

    private function __construct()
    {
    }

    /**
     * Verify a card: well formed, and its binding signature valid under the
     * node key (or the agent's own key when self-bound).
     * @return array{agentId:string, peerId:string, name:string, boundMs:int|string, selfBound:bool}
     * @throws Exception\VerificationFailedException
     * @throws ZxvUsageException ZXV_E_FORMAT
     */
    public static function verifyCard(string $card): array
    {
        $aid = Native::new("uint8_t[32]");
        $pid = Native::new("uint8_t[32]");
        $name = Native::new("char[32]");
        [$ms, $msP] = Native::out("uint64_t");
        Native::check(Native::ffi15()->zxv_w4_card_verify(Native::bytes($card, true), strlen($card), $aid, $pid, $name, $msP));
        $a = Native::read($aid, 32);
        $p = Native::read($pid, 32);
        return ["agentId" => $a, "peerId" => $p, "name" => \FFI::string($name), "boundMs" => Native::u64Out($ms->cdata), "selfBound" => $a === $p];
    }

    /**
     * The digest a consent token for one request must carry: SHA3-256 over
     * "web4/v1/action", the tool, from, to, reqId and the payload (each
     * length-prefixed).
     * @return string 32 bytes
     */
    public static function actionDigest(string $tool, string $from, string $to, int|string|float $reqId, string $payload = ""): string
    {
        $out = Native::new("uint8_t[32]");
        Native::check(Native::ffi15()->zxv_w4_action_digest(Native::cstr($tool, "tool"), self::id32($from, "from"), self::id32($to, "to"),
            Native::u64($reqId, "reqId"), Native::bytes($payload, true), strlen($payload), $out));
        return Native::read($out, 32);
    }

    /**
     * A consent token's fields (no signature check).
     * @return array{humanId:string, agentId:string, action:string, scope:list<string>, maxVfv:int|string, issuedMs:int|string, expiresMs:int|string, nonce:string}
     * @throws ZxvUsageException ZXV_E_FORMAT
     */
    public static function consentInfo(string $token): array
    {
        $h = Native::new("uint8_t[32]");
        $a = Native::new("uint8_t[32]");
        $act = Native::new("uint8_t[32]");
        [$sc, $scP] = Native::out("uint32_t");
        [$mx, $mxP] = Native::out("uint64_t");
        [$is, $isP] = Native::out("uint64_t");
        [$ex, $exP] = Native::out("uint64_t");
        $n = Native::new("uint8_t[16]");
        Native::check(Native::ffi15()->zxv_w4_consent_info(self::token($token), $h, $a, $act, $scP, $mxP, $isP, $exP, $n));
        return ["humanId" => Native::read($h, 32), "agentId" => Native::read($a, 32), "action" => Native::read($act, 32),
            "scope" => self::flagNames(self::CONSENT_SCOPES, $sc->cdata), "maxVfv" => Native::u64Out($mx->cdata),
            "issuedMs" => Native::u64Out($is->cdata), "expiresMs" => Native::u64Out($ex->cdata), "nonce" => Native::read($n, 16)];
    }

    /** @internal */
    public static function id32(string $v, string $name): CData
    {
        return Native::fixed($v, $name, self::ID_BYTES);
    }

    /** @internal */
    public static function token(string $v): CData
    {
        return Native::fixed($v, "token", self::CONSENT_BYTES);
    }

    /**
     * @internal flags from a name, a list of names or an int
     * @param array<string,int> $map
     * @param string|int|list<string> $spec
     */
    public static function flags(array $map, string|int|array $spec, string $name): int
    {
        if (is_int($spec)) {
            return Native::uint32($spec, $name);
        }
        if (is_string($spec)) {
            return $map[$spec] ?? throw new ZxvUsageException(Status::E_ARG, "unknown $name " . var_export($spec, true));
        }
        $m = 0;
        foreach ($spec as $s) {
            $m |= self::flags($map, $s, $name);
        }
        return $m;
    }

    /**
     * @internal
     * @param array<string,int> $map
     * @return list<string>
     */
    public static function flagNames(array $map, int $bits): array
    {
        return array_keys(array_filter($map, fn (int $b) => ($bits & $b) !== 0));
    }
}
