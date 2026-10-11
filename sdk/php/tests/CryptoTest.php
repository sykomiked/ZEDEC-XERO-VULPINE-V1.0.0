<?php

// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0

declare(strict_types=1);

namespace Zedec\Zxv\Tests;

use Zedec\Zxv\Capability;
use Zedec\Zxv\Context;
use Zedec\Zxv\Ed25519;
use Zedec\Zxv\Exception;
use Zedec\Zxv\Hashes;
use Zedec\Zxv\MlDsa87;
use Zedec\Zxv\MlKem1024;
use Zedec\Zxv\SecretKey;

final class CryptoTest extends TestCase
{
    private static function h(string $hex): string
    {
        return (string) hex2bin($hex);
    }

    public function testHashesMatchVectors(): void
    {
        foreach (self::vectors()["hash"] as $v) {
            $this->assertSame($v["keccak256"], bin2hex(Hashes::keccak256($v["utf8"])));
            $this->assertSame($v["sha3_256"], bin2hex(Hashes::sha3_256($v["utf8"])));
            $this->assertSame($v["shake256_64"], bin2hex(Hashes::shake256($v["utf8"], 64)));
        }
    }

    public function testEd25519MatchesVectors(): void
    {
        $v = self::vectors()["ed25519"];
        $this->withContext(function (Context $ctx, Capability $cap) use ($v): void {
            $ed = new Ed25519($cap);
            $sk = SecretKey::fromBytesUnsafe(self::h($v["sk"]));
            $this->assertSame($v["pk"], bin2hex($ed->publicKey($sk)));
            $sig = $ed->sign($sk, $v["msg_utf8"]);
            $this->assertSame($v["sig"], bin2hex($sig));
            $this->assertTrue($ed->verify(self::h($v["pk"]), $v["msg_utf8"], $sig));
            $bad = $sig;
            $bad[0] = chr(ord($bad[0]) ^ 1);
            $this->assertFalse($ed->verify(self::h($v["pk"]), $v["msg_utf8"], $bad));
            $this->assertThrows(Exception\VerificationFailedException::class, fn () => $ed->verifyOrThrow(self::h($v["pk"]), $v["msg_utf8"], $bad));
            $this->assertThrows(Exception\CapabilityDeniedException::class, fn () => (new Ed25519($cap->attenuate(Capability::CRYPTO_KEM)))->sign($sk, "x"));
            $kp = $ed->generate();
            $this->assertTrue($ed->verify($kp->publicKey, "hi", $ed->sign($kp->secretKey, "hi")));
            $kp->close();
            $sk->close();
            $this->assertTrue($sk->isClosed());
            $this->assertThrows(Exception\ZxvUsageException::class, fn () => $ed->sign($sk, "x"));
            $this->assertFalse(str_contains(print_r($kp->secretKey, true), $v["sk"]));
        });
    }

    public function testMlDsa87MatchesVectors(): void
    {
        $v = self::vectors()["mldsa87"];
        $this->withContext(function (Context $ctx, Capability $cap) use ($v): void {
            $dsa = new MlDsa87($cap);
            $kp = $dsa->generate(self::h($v["seed"]));
            $this->assertSame($v["pk_sha3_256"], bin2hex(Hashes::sha3_256($kp->publicKey)));
            $this->assertSame($v["sk_sha3_256"], bin2hex(Hashes::sha3_256($kp->secretKey->exportBytesUnsafe())));
            $sig = $dsa->sign($kp->secretKey, $v["msg_utf8"], $v["ctx_utf8"], deterministic: true);
            $this->assertSame($v["sig_deterministic_sha3_256"], bin2hex(Hashes::sha3_256($sig)));
            $this->assertTrue($dsa->verify($kp->publicKey, $v["msg_utf8"], $sig, $v["ctx_utf8"]));
            $this->assertFalse($dsa->verify($kp->publicKey, $v["msg_utf8"], $sig, "zxw"));
            $hedged = $dsa->sign($kp->secretKey, "agent message", "zxv");
            $this->assertTrue($dsa->verify($kp->publicKey, "agent message", $hedged, "zxv"));
            $kp->close();
        });
    }

    public function testMlKem1024MatchesVectors(): void
    {
        $v = self::vectors()["mlkem1024"];
        $this->withContext(function (Context $ctx, Capability $cap) use ($v): void {
            $kem = new MlKem1024($cap->attenuate(Capability::CRYPTO_KEM));
            $kp = $kem->generate(self::h($v["d"]), self::h($v["z"]));
            $this->assertSame($v["ek_sha3_256"], bin2hex(Hashes::sha3_256($kp->publicKey)));
            $enc = $kem->encapsulate($kp->publicKey, self::h($v["m"]));
            $this->assertSame($v["ct_sha3_256"], bin2hex(Hashes::sha3_256($enc["ciphertext"])));
            $this->assertSame($v["ss"], bin2hex($enc["sharedSecret"]->exportBytesUnsafe()));
            $ss = $kem->decapsulate($kp->secretKey, $enc["ciphertext"]);
            $this->assertSame($v["ss"], bin2hex($ss->exportBytesUnsafe()));
            $bad = str_repeat("\xFF", 32) . substr($kp->publicKey, 32);
            $this->assertThrows(Exception\ZxvUsageException::class, fn () => $kem->encapsulate($bad));
            $kp->close();
            $ss->close();
        });
    }
}
