<?php

// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0

declare(strict_types=1);

namespace Zedec\Zxv\Tests;

use Zedec\Zxv\Attestations;
use Zedec\Zxv\Budgets;
use Zedec\Zxv\Capability;
use Zedec\Zxv\Context;
use Zedec\Zxv\Exception\CapabilityDeniedException;
use Zedec\Zxv\Ledger;
use Zedec\Zxv\Message;
use Zedec\Zxv\PhaseClock;
use Zedec\Zxv\Status;
use Zedec\Zxv\Swarm;
use Zedec\Zxv\Truth;

/** ABI 1.2: every gated call goes through a capability (the zxv_*_cap forms). */
final class CapFormsTest extends TestCase
{
    private function denied(callable $fn): CapabilityDeniedException
    {
        $e = $this->assertThrows(CapabilityDeniedException::class, $fn);
        $this->assertSame(Status::E_CAP, $e->getStatus());
        return $e;
    }

    public function testCapFormsNeedTheRightBit(): void
    {
        $this->withContext(function (Context $ctx, Capability $root): void {
            $post = $root->attenuate(Capability::LEDGER_POST);
            $read = $root->attenuate(Capability::LEDGER_READ);
            $ctx->enableCurrency("NGN", cap: $post);
            $ctx->enableCurrency("GHS", 2, cap: $post);
            $this->assertTrue($ctx->registerPrivateCurrency("VFV", 2, cap: $post)->isPrivate());
            $this->denied(fn () => $ctx->enableCurrency("KES", cap: $read));
            $this->denied(fn () => $ctx->enableCurrency("KES", 2, cap: $read));
            $this->denied(fn () => $ctx->registerPrivateCurrency("VFX", 2, cap: $read));
            $this->denied(fn () => (new Ledger($read))->openAccount("x", "NGN"));

            $w = new Ledger($post);
            $bank = $w->openAccount("bank", "NGN", Ledger::ISSUER);
            $ada = $w->openAccount("ada", "NGN");
            $w->post($bank, $ada, 7, "NGN", "R-1");
            $r = new Ledger($read);
            $this->assertSame(2, $r->accountCount());
            $this->assertSame(["id" => $ada, "name" => "ada", "currency" => "NGN", "kind" => Ledger::HOLDER], $r->account($ada));
            $this->assertSame(["R-1"], array_column($r->journal($ada), "reference"));
            $this->denied(fn () => $w->accountCount());
            $this->denied(fn () => $w->account($ada));
            $this->denied(fn () => $w->journal($ada));

            $this->denied(fn () => new Message($post, Message::PACS008));
            $m = new Message($read, Message::PACS008);
            $m->set(Message::MSG_ID, "M-1");
            $read->setAttestation(Truth::neither());
            $this->denied(fn () => $m->set(Message::MSG_ID, "M-2"));

            $this->denied(fn () => new Budgets($post));
            $this->denied(fn () => new PhaseClock($post));
            $this->denied(fn () => new Attestations($post));
            $this->denied(fn () => new Swarm($post, 2, 10));
            (new Budgets($root->attenuate(Capability::BUDGET)))->close();
            (new PhaseClock($root->attenuate(Capability::PHASE)))->close();
            (new Attestations($root->attenuate(Capability::ATTEST)))->close();
            (new Swarm($root->attenuate(Capability::SWARM), 2, 10))->close();
        });
    }

    public function testCapFromAnotherContextIsRefused(): void
    {
        $this->withContext(function (Context $ctx, Capability $cap): void {
            $other = new Context();
            try {
                $foreign = $other->rootCapability();
                foreach ([fn () => $ctx->enableCurrency("NGN", cap: $foreign),
                    fn () => $ctx->enableCurrency("NGN", 2, cap: $foreign),
                    fn () => $ctx->registerPrivateCurrency("VFV", 2, cap: $foreign)] as $f) {
                    $e = $this->denied($f);
                    $this->assertTrue(str_contains($e->getDetail(), "different context"));
                }
                $this->assertSame(null, $ctx->currency("NGN"));
                $this->assertSame(null, $other->currency("NGN"));
            } finally {
                $other->close();
            }
        });
    }

    public function testNoCapFormsStillWorkAndAreDeprecated(): void
    {
        $this->withContext(function (Context $ctx, Capability $cap): void {
            $seen = [];
            set_error_handler(function (int $no, string $msg) use (&$seen): bool {
                $seen[] = $msg;
                return true;
            }, E_USER_DEPRECATED);
            try {
                $ctx->enableCurrency("NGN");
                $ctx->registerPrivateCurrency("VFV", 2);
                (new Message($ctx, Message::PACS008))->close();
            } finally {
                restore_error_handler();
            }
            $this->assertSame(3, count($seen));
            $this->assertTrue(str_contains($seen[0], "deprecated since ABI 1.2"));
            $this->assertTrue($ctx->currency("NGN") !== null && $ctx->currency("VFV") !== null);
        });
    }
}
