<?php

// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0

declare(strict_types=1);

namespace Zedec\Zxv;

use Zedec\Zxv\Exception\NotImplementedException;

/**
 * A GGUF model on the integer inference engine (bit-identical on every
 * machine). Read-only after loading.
 *
 *     $model = Model::open($cap, "model.gguf");
 *     $session = $model->newSession(512);
 *     echo $session->generate("Hello", new GenerateOptions(maxTokens: 32))->text;
 */
final class Model extends NativeHandle
{
    public const ARCH = 1;
    public const N_VOCAB = 2;
    public const N_CTX_TRAIN = 3;
    public const N_LAYER = 4;
    public const N_EMBD = 5;
    public const N_HEAD = 6;
    public const N_HEAD_KV = 7;
    public const EOS = 8;

    private function __construct(public readonly Capability $capability, \FFI\CData $handle)
    {
        parent::__construct($capability->context, $handle, "zxv_model_destroy");
    }

    /**
     * Open a GGUF file ($tokenizerPath null = the model's own tokenizer).
     * @param Capability $capability needs AI_INFERENCE
     */
    public static function open(Capability $capability, string $path, ?string $tokenizerPath = null): self
    {
        $out = Native::new("zxv_model*");
        Native::check(Native::ffi()->zxv_model_open($capability->ptr(), $path, $tokenizerPath, \FFI::addr($out)), $capability->context);
        return new self($capability, $out);
    }

    /** One metadata value (ARCH: 1 qwen2, 2 qwen3, 3 llama; N_VOCAB; ...). */
    public function info(int $key): int
    {
        $out = Native::new("int64_t");
        Native::check(Native::ffi()->zxv_model_info($this->ptr(), $key, \FFI::addr($out)), $this->context);
        return $out->cdata;
    }

    /** @return list<int> token ids of UTF-8 text */
    public function tokenize(string $text, bool $parseSpecial = true): array
    {
        $t = Native::utf8($text, "text");
        $mem = Native::bytes($t, true);
        $n = Native::new("size_t");
        $rc = Native::ffi()->zxv_tokenize($this->ptr(), $mem, strlen($t), $parseSpecial ? 1 : 0, null, 0, \FFI::addr($n));
        if ($rc !== Status::E_BUFFER) {
            Native::check($rc, $this->context);
        }
        $count = $n->cdata;
        $ids = Native::new("int32_t[" . max($count, 1) . "]");
        Native::check(Native::ffi()->zxv_tokenize($this->ptr(), $mem, strlen($t), $parseSpecial ? 1 : 0, $ids, $count, \FFI::addr($n)), $this->context);
        $out = [];
        for ($i = 0; $i < $n->cdata; $i++) {
            $out[] = $ids[$i];
        }
        return $out;
    }

    /** @param list<int> $ids */
    public function detokenize(array $ids): string
    {
        $arr = Native::new("int32_t[" . max(count($ids), 1) . "]");
        foreach (array_values($ids) as $i => $id) {
            $arr[$i] = Native::int($id, "token", -2147483648, 2147483647);
        }
        [$rc, $s] = Native::text(fn ($b, $c, $l) => Native::ffi()->zxv_detokenize($this->ptr(), $arr, count($ids), $b, $c, $l));
        Native::check($rc, $this->context);
        return (string) $s;
    }

    /** A new session (KV cache): $nCtx 0 = min(n_ctx_train, 4096). */
    public function newSession(int $nCtx = 0, int $kv = Session::KV_Q16): Session
    {
        return new Session($this, $nCtx, $kv);
    }

    /**
     * LoRA adapters are declared in the ABI but not built yet.
     * @throws NotImplementedException while ZXV_FEAT_LORA is off
     */
    public function loadLora(string $path, Rational|int $scale = 1): never
    {
        $out = Native::new("zxv_lora*");
        Native::check(Native::ffi()->zxv_lora_load($this->capability->ptr(), $this->ptr(), $path, Rational::from($scale)->toQ16(), \FFI::addr($out)), $this->context);
        throw new NotImplementedException(Status::E_NOTIMPL, "LoRA handles are not wrapped by this SDK version");
    }
}
