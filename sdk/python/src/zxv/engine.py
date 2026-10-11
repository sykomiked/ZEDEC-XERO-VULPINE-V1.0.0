# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
"""The integer GGUF inference engine (kernel ``tensor``).

Models (qwen2, qwen3, llama; F32/F16/BF16/Q8_0/Q4_0/Q5_x/Q4_K/Q6_K) run in
integer arithmetic only, on the CPU, bit-identical on every machine: the
same seed and inputs give the same tokens everywhere.

Threading: a :class:`Model` is read-only after loading and may be shared by
sessions on any threads. A :class:`Session` (one KV cache) is used by one
thread at a time; the SDK serializes calls on a session with a lock.

The GIL: ``zxv_generate`` is a cffi foreign call, and cffi releases the GIL
for every foreign call. Other Python threads run while a session generates;
the GIL is re-taken only for the moments the token callback runs.
"""

from __future__ import annotations

import asyncio
import codecs
import enum
import queue
import threading
from dataclasses import dataclass
from typing import Any, AsyncIterator, Callable, Iterator, List, Optional, Union

from . import _native
from ._handle import NativeHandle
from ._native import ffi
from .budget import Budget, Budgets
from .context import Capability
from .errors import FloatRejectedError
from .rational import Rational, RationalLike

__all__ = [
    "KvType", "StopReason", "ModelArch", "GenerateOptions", "Token", "GenerationResult",
    "Model", "Session", "TokenStream", "AsyncTokenStream",
]

Prompt = Union[str, bytes]
"""A prompt: text (encoded as UTF-8) or raw UTF-8 bytes."""


class KvType(enum.IntEnum):
    """KV cache precision."""

    Q16 = 0
    """Exact KV cache, 4 bytes per value."""
    Q8 = 1
    """8-bit KV cache, about 1.16 bytes per value."""


class StopReason(enum.IntEnum):
    """Why generation stopped."""

    EOS = 1
    """End-of-sequence token."""
    MAX_TOKENS = 2
    """``max_tokens`` reached."""
    STOP_STRING = 3
    """Reserved."""
    CALLBACK = 4
    """The token callback (or a closed stream) asked to stop."""
    CONTEXT_FULL = 5
    """The context window is full."""
    BUDGET = 6
    """The bound budget ran out."""


class ModelArch(enum.IntEnum):
    """Model architecture."""

    QWEN2 = 1
    QWEN3 = 2
    LLAMA = 3


def _opt_q16(v: Optional[RationalLike], what: str) -> int:
    if v is None:
        return 0
    if isinstance(v, float):
        raise FloatRejectedError(what)
    return Rational.coerce(v, what).to_q16()


@dataclass(frozen=True)
class GenerateOptions:
    """Sampling options. Every real-valued parameter is exact (no floats).

    ``temperature``, ``top_p`` and ``repeat_penalty`` take a
    :class:`Rational` (or int, Fraction, Decimal, text such as ``"0.7"``)
    and are converted to the engine's Q16.16 with ``zxv_rat_to_q16``.
    ``None`` or 0 means off (temperature 0 = greedy).
    """

    max_tokens: int = 128
    temperature: Optional[RationalLike] = None
    top_k: int = 0
    top_p: Optional[RationalLike] = None
    repeat_penalty: Optional[RationalLike] = None
    repeat_last_n: int = 0
    seed: int = 0

    def __post_init__(self) -> None:
        _native.exact_int(self.max_tokens, "max_tokens", *_native.U32)
        _native.exact_int(self.top_k, "top_k", *_native.U32)
        _native.exact_int(self.repeat_last_n, "repeat_last_n", *_native.U32)
        _native.exact_int(self.seed, "seed", *_native.U64)
        for name in ("temperature", "top_p", "repeat_penalty"):
            if isinstance(getattr(self, name), float):
                raise FloatRejectedError(name)

    @classmethod
    def greedy(cls, max_tokens: int = 128) -> "GenerateOptions":
        """Deterministic argmax decoding."""
        return cls(max_tokens=max_tokens)

    def q16(self) -> "tuple[int, int, int]":
        """(temperature, top_p, repeat_penalty) as Q16.16 integers."""
        return (_opt_q16(self.temperature, "temperature"), _opt_q16(self.top_p, "top_p"),
                _opt_q16(self.repeat_penalty, "repeat_penalty"))


@dataclass(frozen=True)
class Token:
    """One generated token."""

    id: int
    piece: bytes
    """The token's bytes (UTF-8, possibly part of a character)."""
    text: str
    """The text completed by this token (``""`` while a character is partial)."""


@dataclass(frozen=True)
class GenerationResult:
    """What a generation produced."""

    text: str
    token_ids: List[int]
    n_prompt: int
    """Prompt tokens evaluated."""
    n_generated: int
    stop: StopReason


def _prompt_bytes(prompt: Prompt) -> bytes:
    if isinstance(prompt, str):
        return prompt.encode("utf-8")
    return _native.to_bytes(prompt, "prompt")


class Model(NativeHandle):
    """A loaded GGUF model. Read-only and shareable across threads.

    Opening needs ``Cap.AI_INFERENCE``.
    """

    def __init__(self, handle: Any, cap: Capability) -> None:
        self._cap = cap
        super().__init__(handle, cap, cap.context)

    def _destroy(self, handle: Any) -> None:
        _native.lib().zxv_model_destroy(handle)

    @classmethod
    def open(cls, cap: Capability, path: str, tokenizer_path: Optional[str] = None) -> "Model":
        """Open a GGUF file (``tokenizer_path``: another GGUF holding the tokenizer).

        Raises:
            CapabilityDeniedError: ``cap`` lacks ``AI_INFERENCE``.
            ZxvSystemError: the file cannot be read (``ZXV_E_IO``).
            ZxvUsageError: the file is not a usable GGUF (``ZXV_E_FORMAT``).
        """
        out = ffi.new("zxv_model **")
        tp = _native.to_cstr(str(tokenizer_path), "tokenizer_path") if tokenizer_path is not None else ffi.NULL
        _native.check(_native.lib().zxv_model_open(cap._handle(), _native.to_cstr(str(path), "path"), tp, out),
                      cap.context)
        return cls(out[0], cap)

    @classmethod
    def from_bytes(cls, cap: Capability, model: bytes, tokenizer: Optional[bytes] = None) -> "Model":
        """Load a model from memory (the bytes are copied)."""
        m = _native.to_bytes(model, "model")
        t = _native.to_bytes(tokenizer, "tokenizer") if tokenizer is not None else None
        out = ffi.new("zxv_model **")
        _native.check(_native.lib().zxv_model_open_memory(
            cap._handle(), m, len(m), t if t else ffi.NULL, len(t) if t else 0, out), cap.context)
        return cls(out[0], cap)

    def _info(self, key: int) -> int:
        out = ffi.new("int64_t *")
        self._check(_native.lib().zxv_model_info(self._handle(), key, out))
        return int(out[0])

    @property
    def arch(self) -> ModelArch:
        """The architecture."""
        return ModelArch(self._info(1))

    @property
    def n_vocab(self) -> int:
        """Vocabulary size."""
        return self._info(2)

    @property
    def n_ctx_train(self) -> int:
        """Context length the model was trained with."""
        return self._info(3)

    @property
    def n_layer(self) -> int:
        """Number of layers."""
        return self._info(4)

    @property
    def n_embd(self) -> int:
        """Embedding width."""
        return self._info(5)

    @property
    def n_head(self) -> int:
        """Attention heads."""
        return self._info(6)

    @property
    def n_head_kv(self) -> int:
        """KV heads."""
        return self._info(7)

    @property
    def eos(self) -> int:
        """End-of-sequence token id."""
        return self._info(8)

    def tokenize(self, text: Prompt, *, parse_special: bool = True) -> List[int]:
        """Token ids of ``text``."""
        b = _prompt_bytes(text)
        L = _native.lib()
        h = self._handle()
        n = ffi.new("size_t *")
        rc = L.zxv_tokenize(h, b, len(b), int(parse_special), ffi.NULL, 0, n)
        if rc == 0:
            return []
        if rc != -2:
            self._check(rc)
        while True:
            ids = ffi.new("int32_t[]", n[0])
            rc = L.zxv_tokenize(h, b, len(b), int(parse_special), ids, n[0], n)
            if rc == 0:
                return list(ids[0:n[0]])
            if rc != -2:
                self._check(rc)

    def detokenize_bytes(self, ids: List[int]) -> bytes:
        """The UTF-8 bytes of token ids."""
        arr = ffi.new("int32_t[]", [_native.exact_int(i, "token id", *_native.I32) for i in ids] or [0])
        h = self._handle()
        L = _native.lib()
        n = ffi.new("size_t *")
        count = len(ids)
        rc = L.zxv_detokenize(h, arr, count, ffi.NULL, 0, n)
        if rc == 0:
            return b""
        if rc != -2:
            self._check(rc)
        buf = ffi.new("char[]", n[0] + 1)
        self._check(L.zxv_detokenize(h, arr, count, buf, n[0] + 1, n))
        return bytes(ffi.unpack(buf, n[0]))

    def detokenize(self, ids: List[int]) -> str:
        """The text of token ids (invalid UTF-8 is replaced with U+FFFD)."""
        return self.detokenize_bytes(ids).decode("utf-8", "replace")

    def new_session(self, n_ctx: int = 0, kv: KvType = KvType.Q16, *,
                    cap: Optional[Capability] = None) -> "Session":
        """A new conversation (KV cache).

        Args:
            n_ctx: context window; 0 means min(n_ctx_train, 4096).
            kv: KV cache precision.
            cap: the capability its generations use (default: the model's).
        """
        out = ffi.new("zxv_session **")
        self._check(_native.lib().zxv_session_create(
            self._handle(), _native.exact_int(n_ctx, "n_ctx", *_native.U32), int(KvType(kv)), out))
        return Session(out[0], self, cap or self._cap)

    def load_lora(self, path: str, scale: RationalLike = 1) -> Any:
        """Load a LoRA adapter.

        Not implemented by the engine yet (``ZXV_FEAT_LORA`` is never set):
        this always raises :class:`ZxvNotImplementedError` in ABI 1.1.
        """
        out = ffi.new("zxv_lora **")
        q = Rational.coerce(scale, "scale").to_q16()
        rc = _native.lib().zxv_lora_load(self._cap._handle(), self._handle(), _native.to_cstr(str(path)), q, out)
        self._check(rc)
        _native.lib().zxv_lora_destroy(out[0])  # pragma: no cover - unreachable while NOTIMPL
        raise NotImplementedError("LoRA handles are not wrapped yet")  # pragma: no cover


TokenCallback = Callable[[Token], Optional[bool]]
"""Called per token; return ``False`` to stop (``None``/``True`` continue)."""


class Session(NativeHandle):
    """One conversation (a KV cache) on a :class:`Model`.

    Generation appends to the session, so a chat continues turn after
    turn; :meth:`reset` forgets it. Calls are serialized by a lock: a
    session runs one generation at a time. Use one session per thread for
    parallelism (they can share the model).
    """

    def __init__(self, handle: Any, model: Model, cap: Capability) -> None:
        self._model = model
        self._cap = cap
        self._lock = threading.RLock()
        self._bound: Optional[Budget] = None
        super().__init__(handle, model, model._context)

    def _destroy(self, handle: Any) -> None:
        with self._lock:
            _native.lib().zxv_session_destroy(handle)

    @property
    def model(self) -> Model:
        """The model."""
        return self._model

    @property
    def capability(self) -> Capability:
        """The capability generations use (needs ``Cap.AI_INFERENCE``)."""
        return self._cap

    @property
    def position(self) -> int:
        """Tokens evaluated into the KV cache.

        After a generation this is ``n_prompt + n_generated - 1``: the last
        sampled token has not been evaluated yet.
        """
        out = ffi.new("uint32_t *")
        with self._lock:
            self._check(_native.lib().zxv_session_position(self._handle(), out))
        return int(out[0])

    def reset(self) -> None:
        """Forget the conversation (empty the KV cache)."""
        with self._lock:
            self._check(_native.lib().zxv_session_reset(self._handle()))

    def bind_budget(self, budget: Optional[Budget]) -> None:
        """Charge ``budget`` one unit per prompt and generated token (``None`` unbinds).

        Prompt tokens are charged up front: if they do not fit, generation
        raises :class:`BudgetExhaustedError` and runs nothing; if the budget
        runs out mid-way, generation stops with :attr:`StopReason.BUDGET`.
        The budget set must stay open while bound.
        """
        with self._lock:
            if budget is None:
                self._check(_native.lib().zxv_session_bind_budget(self._handle(), ffi.NULL, 0))
            else:
                owner: Budgets = budget.budgets
                self._check(_native.lib().zxv_session_bind_budget(self._handle(), owner._handle(), budget.id))
                owner._add_dependent(self)  # closing the budget set closes this session first
            self._bound = budget

    # ------------------------------------------------------------ generation

    def generate(self, prompt: Prompt, options: Optional[GenerateOptions] = None, *,
                 on_token: Optional[TokenCallback] = None) -> GenerationResult:
        """Generate a continuation of ``prompt``. Releases the GIL while it runs.

        Args:
            prompt: text or UTF-8 bytes.
            options: sampling options (default: greedy, 128 tokens).
            on_token: optional callback per token; return ``False`` to stop.

        Raises:
            CapabilityDeniedError: the capability lacks ``AI_INFERENCE``.
            BudgetExhaustedError: a bound budget cannot cover the prompt.
        """
        return self._run(prompt, options or GenerateOptions(), on_token, None)

    def _run(self, prompt: Prompt, opts: GenerateOptions, on_token: Optional[TokenCallback],
             cancel: Optional[threading.Event]) -> GenerationResult:
        p = _prompt_bytes(prompt)
        temp, top_p, rep = opts.q16()
        ids: List[int] = []
        pieces: List[bytes] = []
        decoder = codecs.getincrementaldecoder("utf-8")("replace")
        error: List[BaseException] = []

        def cb(user: Any, token: int, piece: Any, n: int) -> int:
            try:
                b = ffi.unpack(ffi.cast("char *", piece), n) if n else b""
                ids.append(int(token))
                pieces.append(b)
                if on_token is not None:
                    keep = on_token(Token(int(token), b, decoder.decode(b)))
                    if keep is False:
                        return 0
                if cancel is not None and cancel.is_set():
                    return 0
                return 1
            except BaseException as exc:  # surfaced after zxv_generate returns
                error.append(exc)
                return 0

        c_cb = ffi.callback("zxv_token_fn", cb)
        np_ = ffi.new("uint32_t *")
        ng = ffi.new("uint32_t *")
        stop = ffi.new("int32_t *")
        with self._lock:
            rc = _native.lib().zxv_generate(
                self._cap._handle(), self._handle(), p, len(p), opts.max_tokens, temp, opts.top_k,
                top_p, rep, opts.repeat_last_n, opts.seed, c_cb, ffi.NULL, np_, ng, stop)
        if error:
            raise error[0]
        self._check(rc)
        return GenerationResult(b"".join(pieces).decode("utf-8", "replace"), ids, int(np_[0]),
                                int(ng[0]), StopReason(stop[0]))

    def stream(self, prompt: Prompt, options: Optional[GenerateOptions] = None) -> "TokenStream":
        """Generate on a worker thread and iterate the tokens as they come.

        Closing the stream (or leaving a ``with`` block) stops generation.
        After iteration ends, :attr:`TokenStream.result` holds the result.
        """
        return TokenStream(self, prompt, options or GenerateOptions())

    async def generate_async(self, prompt: Prompt, options: Optional[GenerateOptions] = None) -> GenerationResult:
        """:meth:`generate` on a worker thread, so the event loop stays free.

        Cancelling the awaiting task stops generation at the next token.
        """
        async with self.stream_async(prompt, options) as s:
            async for _ in s:
                pass
        assert s.result is not None
        return s.result

    def stream_async(self, prompt: Prompt, options: Optional[GenerateOptions] = None) -> "AsyncTokenStream":
        """An async iterator of tokens; generation runs on a worker thread."""
        return AsyncTokenStream(self, prompt, options or GenerateOptions())


_END = object()


class TokenStream(Iterator[Token]):
    """A synchronous iterator over a generation running on a worker thread."""

    def __init__(self, session: Session, prompt: Prompt, options: GenerateOptions) -> None:
        self._q: "queue.Queue[Any]" = queue.Queue()
        self._cancel = threading.Event()
        self.result: Optional[GenerationResult] = None
        """The :class:`GenerationResult`, once iteration has finished."""
        self._done = False

        def work() -> None:
            try:
                r = session._run(prompt, options, self._q.put, self._cancel)
                self._q.put((_END, r))
            except BaseException as exc:
                self._q.put((_END, exc))

        self._thread = threading.Thread(target=work, name="zxv-generate", daemon=True)
        self._thread.start()

    def __iter__(self) -> "TokenStream":
        return self

    def __next__(self) -> Token:
        if self._done:
            raise StopIteration
        item = self._q.get()
        if isinstance(item, tuple) and item and item[0] is _END:
            self._done = True
            self._thread.join()
            if isinstance(item[1], BaseException):
                raise item[1]
            self.result = item[1]
            raise StopIteration
        return item  # type: ignore[no-any-return]

    def close(self) -> None:
        """Stop generation (at the next token) and wait for the worker."""
        self._cancel.set()
        if not self._done:
            for _ in self:
                pass

    def __enter__(self) -> "TokenStream":
        return self

    def __exit__(self, *exc: Any) -> None:
        self.close()


class AsyncTokenStream:
    """An async iterator over a generation running on a worker thread.

    Use ``async with session.stream_async(...) as s: async for tok in s``.
    Leaving the block, or cancelling the consuming task, stops generation.
    """

    def __init__(self, session: Session, prompt: Prompt, options: GenerateOptions) -> None:
        self._session = session
        self._prompt = prompt
        self._options = options
        self._cancel = threading.Event()
        self._q: Optional["asyncio.Queue[Any]"] = None
        self._thread: Optional[threading.Thread] = None
        self._done = False
        self.result: Optional[GenerationResult] = None
        """The :class:`GenerationResult`, once iteration has finished."""

    def _start(self) -> None:
        if self._thread is not None:
            return
        loop = asyncio.get_running_loop()
        q: asyncio.Queue[Any] = asyncio.Queue()
        self._q = q

        def push(item: Any) -> None:
            try:
                loop.call_soon_threadsafe(q.put_nowait, item)
            except RuntimeError:  # loop closed: stop generating
                self._cancel.set()

        def work() -> None:
            try:
                r = self._session._run(self._prompt, self._options, push, self._cancel)
                push((_END, r))
            except BaseException as exc:
                push((_END, exc))

        self._thread = threading.Thread(target=work, name="zxv-generate-async", daemon=True)
        self._thread.start()

    def __aiter__(self) -> "AsyncTokenStream":
        self._start()
        return self

    async def __anext__(self) -> Token:
        self._start()
        if self._done:
            raise StopAsyncIteration
        assert self._q is not None
        try:
            item = await self._q.get()
        except asyncio.CancelledError:
            self._cancel.set()
            raise
        if isinstance(item, tuple) and item and item[0] is _END:
            self._done = True
            if isinstance(item[1], BaseException):
                raise item[1]
            self.result = item[1]
            raise StopAsyncIteration
        return item  # type: ignore[no-any-return]

    async def aclose(self) -> None:
        """Stop generation and wait for the worker thread to finish."""
        self._cancel.set()
        if self._thread is None:
            return
        while not self._done:
            try:
                await self.__anext__()
            except StopAsyncIteration:
                break
            except Exception:
                break
        await asyncio.get_running_loop().run_in_executor(None, self._thread.join)

    async def __aenter__(self) -> "AsyncTokenStream":
        self._start()
        return self

    async def __aexit__(self, *exc: Any) -> None:
        await self.aclose()

