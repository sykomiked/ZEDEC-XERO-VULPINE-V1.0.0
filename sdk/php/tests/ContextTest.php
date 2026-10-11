<?php

// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0

declare(strict_types=1);

namespace Zedec\Zxv\Tests;

use Zedec\Zxv\Attestations;
use Zedec\Zxv\Budgets;
use Zedec\Zxv\Capability;
use Zedec\Zxv\Context;
use Zedec\Zxv\Exception;
use Zedec\Zxv\Ledger;
use Zedec\Zxv\Message;
use Zedec\Zxv\Native;
use Zedec\Zxv\PhaseClock;
use Zedec\Zxv\Status;
use Zedec\Zxv\Truth;

final class ContextTest extends TestCase
{
    /** @return array{0:Ledger,1:int,2:int} */
    private function setup(Context $ctx, Capability $cap): array
    {
        $ctx->enableCurrency("NGN", 2, cap: $cap);
        $l = new Ledger($cap);
        return [$l, $l->openAccount("bank", "NGN", Ledger::ISSUER), $l->openAccount("ada", "NGN")];
    }

    public function testLedger(): void
    {
        $this->withContext(function (Context $ctx, Capability $cap): void {
            [$l, $bank, $ada] = $this->setup($ctx, $cap);
            $bob = $l->openAccount("bob", "NGN");
            $l->post($bank, $ada, 50_000, "NGN", "E2E-1");
            $l->post($ada, $bob, 12_345, "NGN", "E2E-2");
            $b = $l->balance($ada);
            $this->assertSame([50_000, 12_345, 37_655], [$b->debit, $b->credit, $b->equity]);
            $this->assertSame(-50_000, $l->balance($bank)->equity);
            $this->assertTrue($l->check());
            $this->assertSame(3, $l->accountCount());
            $this->assertSame(["E2E-1", "E2E-2"], array_column($l->journal($ada), "reference"));
            $this->assertSame([50_000, -12_345], array_column($l->journal($ada), "amount"));
            $e = $this->assertThrows(Exception\InsufficientFundsException::class, fn () => $l->post($bob, $ada, 1_000_000, "NGN", "E2E-3"));
            $this->assertSame("ZXV_E_FUNDS", $e->getStatusName());
            $this->assertThrows(Exception\FloatRejectedException::class, fn () => $l->post($bank, $ada, 1.5, "NGN", "E2E-4"));
            $this->assertTrue($ctx->registerPrivateCurrency("VFV", 2, cap: $cap)->isPrivate());
            $this->assertSame(null, $ctx->currency("XOF"));
        });
    }

    public function testCapabilityDenialAttenuationRevocation(): void
    {
        $this->withContext(function (Context $ctx, Capability $cap): void {
            [$l, $bank, $ada] = $this->setup($ctx, $cap);
            $l->post($bank, $ada, 500, "NGN", "r1");
            $ro = $cap->attenuate(Capability::LEDGER_READ);
            $this->assertSame(["ledger_read"], $ro->info()["names"]);
            $reader = new Ledger($ro);
            $this->assertSame(500, $reader->balance($ada)->equity);
            $e = $this->assertThrows(Exception\CapabilityDeniedException::class, fn () => $reader->post($bank, $ada, 1, "NGN", "r2"));
            $this->assertSame(Status::E_CAP, $e->getStatus());
            $this->assertFalse($ro->attenuate(Capability::ALL)->allows(Capability::LEDGER_POST));
            $cap->setAttestation(Truth::both());
            $e = $this->assertThrows(Exception\CapabilityDeniedException::class, fn () => $l->balance($ada));
            $this->assertTrue(str_contains($e->getDetail(), "contradictory"));
            $cap->setAttestation(Truth::true());
            $cap->revoke();
            $this->assertTrue($cap->info()["revoked"]);
            $this->assertThrows(Exception\CapabilityDeniedException::class, fn () => $reader->balance($ada));
            $this->assertThrows(Exception\ZxvUsageException::class, fn () => $ctx->rootCapability(1 << 40));
        });
    }

    public function testFatalPathViaHalt(): void
    {
        $ctx = new Context();
        $cap = $ctx->rootCapability();
        [$l, $bank, $ada] = $this->setup($ctx, $cap);
        $ctx->halt("test halt");
        $this->assertTrue($ctx->isPoisoned());
        $caughtAsException = false;
        $e = $this->assertThrows(Exception\ZxvFatalError::class, function () use ($l, $bank, $ada, &$caughtAsException): void {
            try {
                $l->post($bank, $ada, 1, "NGN", "r3");
            } catch (\Exception) {
                $caughtAsException = true;
            }
        });
        $this->assertFalse($caughtAsException, "catch (\\Exception) must not catch ZxvFatalError");
        $this->assertSame(Status::E_POISONED, $e->getStatus());
        $this->assertThrows(Exception\ZxvFatalError::class, fn () => $l->balance($ada));
        $this->assertThrows(Exception\ZxvFatalError::class, fn () => new Budgets($cap));
        $ctx->close();
        $this->assertTrue($ctx->isClosed());
    }

    public function testFatalStatusFromCMarksTheContextDead(): void
    {
        $this->withContext(function (Context $ctx, Capability $cap): void {
            [$l, , $ada] = $this->setup($ctx, $cap);
            Native::ffi()->zxv_ctx_halt($ctx->raw(), "behind the SDK's back");
            $e = $this->assertThrows(Exception\ZxvFatalError::class, fn () => $l->balance($ada));
            $this->assertSame(Status::E_POISONED, $e->getStatus());
            $this->assertTrue($ctx->isPoisoned());
            $this->assertThrows(Exception\ZxvFatalError::class, fn () => new PhaseClock($cap));
        });
    }

    public function testFatalPolicyAbortEndsTheProcess(): void
    {
        $root = dirname(__DIR__);
        $script = '<?php declare(strict_types=1); require "' . $root . '/autoload.php";
            use Zedec\Zxv\{Zxv, FatalPolicy, Context, Ledger};
            Zxv::setFatalPolicy(FatalPolicy::Abort);
            register_shutdown_function(fn () => print("shutdown ran\n"));
            $ctx = new Context(); $cap = $ctx->rootCapability(); $ctx->halt("x");
            try { (new Ledger($cap))->balance(0); } catch (\Throwable $e) { echo "caught\n"; } finally { echo "finally\n"; }
            echo "survived\n";';
        $file = tempnam(sys_get_temp_dir(), "zxv") . ".php";
        file_put_contents($file, $script);
        $proc = proc_open([PHP_BINARY, "-d", "ffi.enable=1", $file], [1 => ["pipe", "w"], 2 => ["pipe", "w"]], $pipes);
        $out = stream_get_contents($pipes[1]);
        $err = stream_get_contents($pipes[2]);
        $code = proc_close($proc);
        unlink($file);
        $this->assertSame(70, $code, "exit status");
        $this->assertSame("", $out, "nothing after the fatal ran");
        $this->assertTrue(str_contains((string) $err, "FATAL"));
    }

    public function testAttestationsAndMessages(): void
    {
        $this->withContext(function (Context $ctx, Capability $cap): void {
            $att = new Attestations($cap);
            $i = $att->attest("model.gguf", 6, Truth::true(), "loader");
            $this->assertTrue($att->isGateClear());
            $att->evidence($i, true);
            $att->evidence($i, false);
            $st = $att->state($i);
            $this->assertTrue($st["truth"] === Truth::both());
            $this->assertSame([2, 1], [$st["positive"], $st["negative"]]);
            $this->assertFalse($att->isGateClear());
            $this->assertSame($i, $att->find("model.gguf"));
            $this->assertSame(null, $att->find("nope"));

            $m = new Message($cap, Message::PACS008);
            foreach ([Message::MSG_ID => "M1", Message::CRE_DT_TM => "2026-10-11T12:00:00Z", Message::DEBTOR_NAME => "Ada & Co",
                Message::DEBTOR_ACCT => "D1", Message::CREDITOR_NAME => "Kofi", Message::CREDITOR_ACCT => "C1",
                Message::END_TO_END_ID => "E2E", Message::CCY => "NGN"] as $f => $v) {
                $m->set($f, $v);
            }
            $xml = $m->amount(Message::AMT_SETTLEMENT, 150_075, 2)->render();
            $this->assertTrue(str_contains($xml, "Ada &amp; Co"));
            $this->assertTrue(str_contains($xml, "1500.75"));
            $this->assertThrows(Exception\NotImplementedException::class, fn () => new Message($cap, Message::PACS002));
        });
    }
}
