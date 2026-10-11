<?php

// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0

declare(strict_types=1);

namespace Zedec\Zxv\Tests;

use Zedec\Zxv\Budgets;
use Zedec\Zxv\Capability;
use Zedec\Zxv\Context;
use Zedec\Zxv\Exception;
use Zedec\Zxv\GenerateOptions;
use Zedec\Zxv\Ledger;
use Zedec\Zxv\Model;
use Zedec\Zxv\Session;
use Zedec\Zxv\StateChannel;

final class EngineEvmTest extends TestCase
{
    private static function gen(): array
    {
        return self::vectors()["generate"];
    }

    private static function prompt(): string
    {
        return (string) file_get_contents(self::fixtures() . "/" . self::gen()["prompt"]);
    }

    private static function open(Capability $cap): Model
    {
        return Model::open($cap, self::fixtures() . "/" . self::gen()["model"], self::fixtures() . "/" . self::gen()["tokenizer"]);
    }

    public function testTokenizeAndGreedyGeneration(): void
    {
        $g = self::gen();
        $this->withContext(function (Context $ctx, Capability $cap) use ($g): void {
            $this->assertThrows(Exception\CapabilityDeniedException::class, fn () => self::open($cap->attenuate(Capability::LEDGER_READ)));
            $e = $this->assertThrows(Exception\ZxvSystemException::class, fn () => Model::open($cap, "/nonexistent.gguf"));
            $this->assertSame("ZXV_E_IO", $e->getStatusName());
            $m = self::open($cap);
            $this->assertSame(1200, $m->info(Model::N_VOCAB));
            $ids = $m->tokenize(self::prompt());
            $this->assertSame($g["prompt_tokens"], $ids);
            $this->assertSame(self::prompt(), $m->detokenize($ids));
            $s = $m->newSession($g["n_ctx"]);
            $streamed = [];
            $r = $s->generate(self::prompt(), new GenerateOptions(maxTokens: $g["greedy"]["max_tokens"]), function (string $piece, int $id) use (&$streamed) {
                $streamed[] = $id;
            });
            $this->assertSame($g["greedy"]["tokens"], $r->tokens);
            $this->assertSame($r->tokens, $streamed);
            $this->assertSame(Session::STOP_MAX, $r->stopReason);
            $this->assertSame(count($g["prompt_tokens"]), $r->promptTokens);
            $this->assertSame($m->detokenize($r->tokens), $r->text);
            $this->assertThrows(Exception\NotImplementedException::class, fn () => $m->loadLora("x.gguf"));
        });
    }

    public function testSeededSamplingAndCallbackStop(): void
    {
        $g = self::gen();
        $sv = $g["sampled"];
        $this->withContext(function (Context $ctx, Capability $cap) use ($g, $sv): void {
            $s = self::open($cap)->newSession($g["n_ctx"]);
            $o = GenerateOptions::fromQ16($sv["max_tokens"], $sv["temperature_q16"], $sv["top_k"], $sv["top_p_q16"], $sv["repeat_penalty_q16"], $sv["repeat_last_n"], $sv["seed"]);
            $this->assertSame($sv["tokens"], $s->generate(self::prompt(), $o)->tokens);
            $s->reset();
            $this->assertSame($sv["tokens"], $s->generate(self::prompt(), $o)->tokens);
            $s->reset();
            $n = 0;
            $r = $s->generate(self::prompt(), new GenerateOptions(maxTokens: 16), function () use (&$n) {
                return ++$n < 3;
            });
            $this->assertSame(Session::STOP_CALLBACK, $r->stopReason);
            $this->assertSame(3, count($r->tokens));
            $this->assertThrows(Exception\FloatRejectedException::class, fn () => new GenerateOptions(temperature: 0.7));
        });
    }

    public function testBudgetBoundGeneration(): void
    {
        $g = self::gen();
        $this->withContext(function (Context $ctx, Capability $cap) use ($g): void {
            $s = self::open($cap)->newSession($g["n_ctx"]);
            $b = (new Budgets($cap))->budget("gen", Budgets::TOKENS, count($g["prompt_tokens"]) + 8);
            $s->bindBudget($b);
            $r = $s->generate(self::prompt(), new GenerateOptions(maxTokens: 16));
            $this->assertSame(Session::STOP_BUDGET, $r->stopReason);
            $this->assertSame(8, count($r->tokens));
            $s->reset();
            $this->assertThrows(Exception\BudgetExhaustedException::class, fn () => $s->generate(self::prompt(), new GenerateOptions(maxTokens: 16)));
            $s->bindBudget(null);
        });
    }

    public function testEvmStateMatchesVectors(): void
    {
        $v = self::vectors()["evm_state"];
        $this->withContext(function (Context $ctx, Capability $cap) use ($v): void {
            $ch = new StateChannel($cap);
            $st = $ch->encodeState((string) hex2bin($v["channel_id"]), $v["nonce"], array_map(fn ($a) => (string) hex2bin($a), $v["addresses"]), $v["balances"]);
            $this->assertSame($v["encoding"], bin2hex($st->encoding));
            $this->assertSame($v["digest"], bin2hex($st->digest));
            $st2 = $ch->encodeState("0x" . $v["channel_id"], $v["nonce"], array_map(fn ($a) => "0x" . $a, $v["addresses"]), $v["balances"]);
            $this->assertSame($st->digest, $st2->digest);

            $ctx->enableCurrency("USD", 2, cap: $cap);
            $l = new Ledger($cap);
            $a = $l->openAccount("a", "USD", Ledger::ISSUER);
            $b = $l->openAccount("b", "USD");
            $l->post($a, $b, 250, "USD", "x");
            $parties = array_map(fn ($x) => "0x" . $x, array_slice($v["addresses"], 0, 2));
            $fromLedger = $ch->ledgerState("0x" . $v["channel_id"], 1, "USD", [$a, $b], $parties);
            $explicit = $ch->encodeState("0x" . $v["channel_id"], 1, $parties, [-250, 250]);
            $this->assertSame($explicit->encoding, $fromLedger->encoding);
            $this->assertSame(10 * 32, strlen($fromLedger->encoding));
            $this->assertThrows(Exception\CapabilityDeniedException::class, fn () => (new StateChannel($cap->attenuate(Capability::LEDGER_READ)))->encodeState("0x" . $v["channel_id"], 1, $parties, [1, 2]));
            $this->assertThrows(Exception\FloatRejectedException::class, fn () => $ch->encodeState("0x" . $v["channel_id"], 1, $parties, [1.0, 2]));
        });
    }
}
