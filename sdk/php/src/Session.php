<?php

// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0

declare(strict_types=1);

namespace Zedec\Zxv;

/** A conversation (KV cache) on a Model. Generation appends to it. */
final class Session extends NativeHandle
{
    public const KV_Q16 = 0;
    public const KV_Q8 = 1;

    public const STOP_EOS = 1;
    public const STOP_MAX = 2;
    public const STOP_STR = 3;
    public const STOP_CALLBACK = 4;
    public const STOP_CTX = 5;
    public const STOP_BUDGET = 6;

    private ?Budget $budget = null;

    /** @internal use Model::newSession() */
    public function __construct(public readonly Model $model, int $nCtx, int $kv)
    {
        $out = Native::new("zxv_session*");
        Native::check(Native::ffi()->zxv_session_create($model->ptr(), Native::uint32($nCtx, "nCtx"), $kv, \FFI::addr($out)), $model->context);
        parent::__construct($model->context, $out, "zxv_session_destroy");
    }

    /**
     * Generate. $onToken(string $piece, int $tokenId) streams each token
     * (the piece may be a partial UTF-8 character); return false from it to
     * stop early (STOP_CALLBACK).
     * @throws Exception\BudgetExhaustedException a bound budget cannot cover the prompt
     */
    public function generate(string $prompt, ?GenerateOptions $options = null, ?\Closure $onToken = null): GenerateResult
    {
        $o = $options ?? new GenerateOptions();
        $p = Native::utf8($prompt, "prompt");
        $tokens = [];
        $text = "";
        $error = null;
        $cb = function ($user, int $token, $piece, int $len) use (&$tokens, &$text, &$error, $onToken): int {
            $bytes = $len === 0 ? "" : \FFI::string($piece, $len);
            $tokens[] = $token;
            $text .= $bytes;
            if ($onToken === null) {
                return 1;
            }
            try {
                return $onToken($bytes, $token) === false ? 0 : 1;
            } catch (\Throwable $e) {
                $error = $e;
                return 0;
            }
        };
        $np = Native::new("uint32_t");
        $ng = Native::new("uint32_t");
        $stop = Native::new("int32_t");
        $rc = Native::ffi()->zxv_generate($this->model->capability->ptr(), $this->ptr(), Native::bytes($p, true), strlen($p), $o->maxTokens, $o->temperature->toQ16(), $o->topK, $o->topP->toQ16(), $o->repeatPenalty->toQ16(), $o->repeatLastN, $o->seed, $cb, null, \FFI::addr($np), \FFI::addr($ng), \FFI::addr($stop));
        if ($error !== null) {
            throw $error;
        }
        Native::check($rc, $this->context);
        return new GenerateResult($text, $tokens, $stop->cdata, $np->cdata);
    }

    /** Forget the conversation. */
    public function reset(): void
    {
        Native::check(Native::ffi()->zxv_session_reset($this->ptr()), $this->context);
    }

    /** Tokens in the KV cache. */
    public function position(): int
    {
        $out = Native::new("uint32_t");
        Native::check(Native::ffi()->zxv_session_position($this->ptr(), \FFI::addr($out)), $this->context);
        return $out->cdata;
    }

    /** Charge $budget one unit per prompt and generated token (null unbinds). */
    public function bindBudget(?Budget $budget): void
    {
        $rc = $budget === null
            ? Native::ffi()->zxv_session_bind_budget($this->ptr(), null, 0)
            : Native::ffi()->zxv_session_bind_budget($this->ptr(), $budget->budgets->ptr(), $budget->id);
        Native::check($rc, $this->context);
        $this->budget = $budget; // keeps the Budgets alive while bound
    }
}
