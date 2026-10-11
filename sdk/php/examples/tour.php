<?php

// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
//
// A tour of the PHP SDK. Run from sdk/php:
//   php examples/tour.php

declare(strict_types=1);

require dirname(__DIR__) . "/autoload.php";

use Zedec\Zxv\{Budgets, Capability, Context, Exception\CapabilityDeniedException, Exception\ZxvFatalError,
    GenerateOptions, Hashes, Ledger, Loop, MlDsa87, Model, PhaseClock, Rational, StateChannel, Truth, Ubh168Frame, Zxv};

echo "libzxv ABI " . implode(".", Zxv::abiVersion()) . "\n";

// Exact numbers: 0.1 + 0.2 is 3/10; a float enters only on purpose.
echo "0.1 + 0.2 = " . Rational::parse("0.1")->add("0.2") . "; the double 0.1 is really "
    . Rational::fromDoubleExact(0.1)->numerator . "/" . Rational::fromDoubleExact(0.1)->denominator . "\n";

// Four-valued logic: a contradiction is held, never collapsed.
$t = Truth::fromEvidence(2, 1);
echo "evidence 2 for, 1 against -> $t; certainly true? " . var_export($t->isCertainlyTrue(), true) . "\n";

$ctx = new Context();
$root = $ctx->rootCapability();

// Ledger through a capability.
$ctx->enableCurrency("NGN", cap: $root);
$ledger = new Ledger($root);
$bank = $ledger->openAccount("bank", "NGN", Ledger::ISSUER);
$ada = $ledger->openAccount("ada", "NGN");
$ledger->post($bank, $ada, 1_500_075, "NGN", "E2E-0001");
echo "ada holds " . $ledger->balance($ada)->equity . " kobo\n";

try {
    (new Ledger($root->attenuate(Capability::LEDGER_READ)))->post($ada, $bank, 1, "NGN", "E2E-0002");
} catch (CapabilityDeniedException $e) {
    echo "read-only cap: " . $e->getStatusName() . "\n";
}

// A budget, and a Fiber that waits for it while another releases it.
$gpu = (new Budgets($root))->budget("gpu", Budgets::COMPUTE_CYCLES, 10);
$gpu->consume(10);
$wait = $gpu->consumeAsync(Rational::of(5, 2), timeoutMs: 5000);
Loop::run(
    function () use ($wait): void {
        $wait->await();
    },
    function () use ($gpu): void {
        $gpu->release(3);
    },
);
echo "gpu used " . $gpu->used() . " of 10\n";

// Phase ticks.
$clock = new PhaseClock($root);
$event = $clock->defer(PhaseClock::CYCLE);
$clock->advance(PhaseClock::CYCLE);
echo "event {$event->id} cleared: " . var_export($event->isCleared(), true) . " at tick " . $clock->now() . "\n";

// A UBH-168 frame, packed in pure PHP.
$frame = new Ubh168Frame(Ubh168Frame::tag(3, 2, 1), [1, 2, 3], [4, 5]);
echo "frame " . bin2hex($frame->toBytes()) . "\n";

// Post-quantum signature; the secret key lives in native memory.
$dsa = new MlDsa87($root);
$keys = $dsa->generate();
$sig = $dsa->sign($keys->secretKey, "agent message", "zxv");
echo "ML-DSA-87 signature (" . strlen($sig) . " bytes) verifies: " . var_export($dsa->verify($keys->publicKey, "agent message", $sig, "zxv"), true) . "\n";
$keys->close();

// The integer engine, if the fixtures are present.
$fx = dirname(__DIR__, 2) . "/conformance/fixtures";
if (is_file("$fx/tiny-qwen2.gguf")) {
    $model = Model::open($root, "$fx/tiny-qwen2.gguf", "$fx/tiny-tokenizer.gguf");
    $r = $model->newSession(128)->generate("Hello", new GenerateOptions(maxTokens: 8));
    echo "generated " . count($r->tokens) . " tokens: " . implode(",", $r->tokens) . "\n";
}

// The state sdk/solidity/ZxvStateChannel.sol checks.
$state = (new StateChannel($root))->ledgerState(Hashes::keccak256("demo channel"), 1, "NGN", [$ada], ["0x" . str_repeat("11", 20)]);
echo "channel state digest " . $state->digestHex() . "\n";

// Fail closed: ZxvFatalError is an \Error, so catch (\Exception) does not swallow it.
$ctx->halt("demo");
try {
    $ledger->balance($ada);
} catch (ZxvFatalError $e) {
    echo "after halt: " . $e->getStatusName() . " (never retry)\n";
}
$ctx->close();
