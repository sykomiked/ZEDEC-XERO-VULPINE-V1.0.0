# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
"""The integer inference engine: vectors, budgets, streams, threads and the GIL."""

from __future__ import annotations

import asyncio
import sys
import threading
import time
from pathlib import Path
from typing import Any, Dict, Iterator, List, Tuple

import pytest
from conftest import needs

import zxv
from zxv import Cap, GenerateOptions, Rational, StopReason

pytestmark = needs(zxv.Feature.TENSOR)


@pytest.fixture
def engine(cap: zxv.Capability, fixtures_dir: Path,
           vectors: Dict[str, Any]) -> Iterator[Tuple[zxv.Model, bytes]]:
    g = vectors["generate"]
    with zxv.Model.open(cap, str(fixtures_dir / g["model"]), str(fixtures_dir / g["tokenizer"])) as m:
        yield m, (fixtures_dir / g["prompt"]).read_bytes()


def sampled_options(v: Dict[str, Any]) -> GenerateOptions:
    # Exact decimal text -> Q16 gives exactly the vectors' integers.
    o = GenerateOptions(max_tokens=v["max_tokens"], temperature="0.7", top_k=v["top_k"],
                        top_p=Rational.parse("0.95"), repeat_penalty=Rational(11, 10),
                        repeat_last_n=v["repeat_last_n"], seed=v["seed"])
    assert o.q16() == (v["temperature_q16"], v["top_p_q16"], v["repeat_penalty_q16"])
    return o


def test_model_info_and_tokenizer(engine: Tuple[zxv.Model, bytes], vectors: Dict[str, Any]) -> None:
    m, prompt = engine
    assert m.n_vocab == 1200 and m.arch is zxv.ModelArch.QWEN2
    ids = m.tokenize(prompt)
    assert ids == vectors["generate"]["prompt_tokens"]
    assert m.detokenize_bytes(ids) == prompt and m.detokenize(ids) == prompt.decode()
    assert m.tokenize(prompt.decode()) == ids  # str and bytes prompts agree


def test_greedy_matches_vectors(engine: Tuple[zxv.Model, bytes], vectors: Dict[str, Any]) -> None:
    m, prompt = engine
    g = vectors["generate"]
    with m.new_session(g["n_ctx"]) as s:
        r = s.generate(prompt, GenerateOptions.greedy(g["greedy"]["max_tokens"]))
        assert r.token_ids == g["greedy"]["tokens"]
        assert r.n_prompt == len(g["prompt_tokens"]) and r.n_generated == 16
        assert r.stop is StopReason.MAX_TOKENS
        assert r.text == m.detokenize(r.token_ids)
        # the last sampled token is not evaluated into the KV cache yet
        assert s.position == r.n_prompt + r.n_generated - 1


def test_seeded_sampling_matches_vectors(engine: Tuple[zxv.Model, bytes], vectors: Dict[str, Any]) -> None:
    m, prompt = engine
    v = vectors["generate"]["sampled"]
    with m.new_session(vectors["generate"]["n_ctx"]) as s:
        a = s.generate(prompt, sampled_options(v))
        s.reset()
        b = s.generate(prompt, sampled_options(v))
    assert a.token_ids == b.token_ids == v["tokens"]


def test_budget_stops_generation(cap: zxv.Capability, engine: Tuple[zxv.Model, bytes]) -> None:
    m, prompt = engine
    n_prompt = len(m.tokenize(prompt))
    with zxv.Budgets(cap) as budgets, m.new_session(128) as s:
        b = budgets.create("gen", zxv.Resource.TOKENS, n_prompt + 8)
        s.bind_budget(b)
        r = s.generate(prompt, GenerateOptions.greedy(16))
        assert r.stop is StopReason.BUDGET and len(r.token_ids) == 8
        s.reset()
        with pytest.raises(zxv.BudgetExhaustedError):
            s.generate(prompt, GenerateOptions.greedy(16))  # prompt refused up front
        s.bind_budget(None)
        assert s.generate(prompt, GenerateOptions.greedy(4)).n_generated == 4


def test_callback_stop_and_errors(engine: Tuple[zxv.Model, bytes]) -> None:
    m, prompt = engine
    seen: List[zxv.Token] = []
    with m.new_session(128) as s:
        r = s.generate(prompt, GenerateOptions.greedy(16), on_token=lambda t: seen.append(t) or len(seen) < 3)
        assert r.stop is StopReason.CALLBACK and len(seen) == 3 == len(r.token_ids)
        assert all(isinstance(t.piece, bytes) for t in seen)

        def boom(t: zxv.Token) -> bool:
            raise RuntimeError("from callback")

        s.reset()
        with pytest.raises(RuntimeError, match="from callback"):
            s.generate(prompt, GenerateOptions.greedy(4), on_token=boom)


def test_capability_and_io_errors(ctx: zxv.Context, cap: zxv.Capability, fixtures_dir: Path,
                                  engine: Tuple[zxv.Model, bytes]) -> None:
    m, prompt = engine
    no_ai = cap.attenuate(Cap.LEDGER_READ)
    with pytest.raises(zxv.CapabilityDeniedError):
        zxv.Model.open(no_ai, str(fixtures_dir / "tiny-qwen2.gguf"))
    with pytest.raises(zxv.ZxvSystemError) as e:
        zxv.Model.open(cap, "/nonexistent.gguf")
    assert e.value.status_name == "ZXV_E_IO"
    with m.new_session(128, cap=no_ai) as s, pytest.raises(zxv.CapabilityDeniedError):
        s.generate(prompt)
    with pytest.raises(zxv.ZxvNotImplementedError):
        m.load_lora("adapter.gguf", Rational(1))


def test_model_from_memory(cap: zxv.Capability, fixtures_dir: Path, vectors: Dict[str, Any]) -> None:
    g = vectors["generate"]
    with zxv.Model.from_bytes(cap, (fixtures_dir / g["model"]).read_bytes(),
                              (fixtures_dir / g["tokenizer"]).read_bytes()) as m, m.new_session(128) as s:
        prompt = (fixtures_dir / g["prompt"]).read_bytes()
        assert s.generate(prompt, GenerateOptions.greedy(16)).token_ids == g["greedy"]["tokens"]


def test_sync_stream(engine: Tuple[zxv.Model, bytes], vectors: Dict[str, Any]) -> None:
    m, prompt = engine
    with m.new_session(128) as s:
        with s.stream(prompt, GenerateOptions.greedy(16)) as stream:
            ids = [t.id for t in stream]
        assert ids == vectors["generate"]["greedy"]["tokens"]
        assert stream.result is not None and stream.result.token_ids == ids
        s.reset()
        with s.stream(prompt, GenerateOptions.greedy(16)) as stream:
            first = next(stream)
        assert first.id == ids[0]  # leaving the block stopped generation early
        s.reset()
        text = "".join(t.text for t in s.stream(prompt, GenerateOptions.greedy(16)))
        assert text == s.model.detokenize_bytes(ids).decode("utf-8", "replace")


async def test_async_stream_keeps_the_loop_free(engine: Tuple[zxv.Model, bytes],
                                                vectors: Dict[str, Any]) -> None:
    m, prompt = engine
    ticks = 0
    stop = asyncio.Event()

    async def ticker() -> None:
        nonlocal ticks
        while not stop.is_set():
            ticks += 1
            await asyncio.sleep(0)

    t = asyncio.create_task(ticker())
    with m.new_session(128) as s:
        ids = []
        async with s.stream_async(prompt, GenerateOptions.greedy(16)) as stream:
            async for tok in stream:
                ids.append(tok.id)
        assert ids == vectors["generate"]["greedy"]["tokens"]
        s.reset()
        r = await s.generate_async(prompt, GenerateOptions.greedy(16))
        assert r.token_ids == ids
        s.reset()
        async with s.stream_async(prompt, GenerateOptions.greedy(64)) as stream:
            async for _ in stream:
                break  # early exit stops the worker
        assert stream.result is None or stream.result.stop is StopReason.CALLBACK
    stop.set()
    await t
    assert ticks > 0


def test_gil_is_released_during_generate(engine: Tuple[zxv.Model, bytes]) -> None:
    """A second Python thread makes progress while zxv_generate runs.

    The switch interval is raised so that CPython never forces the main
    thread to hand over the GIL: the only way the counter thread can run
    during the call is that the foreign call released the GIL.
    """
    m, prompt = engine
    counter = [0]
    started = threading.Event()
    done = threading.Event()

    def count() -> None:
        started.set()
        while not done.is_set():
            counter[0] += 1
            time.sleep(0)  # yields the GIL so the main thread can return from C

    old = sys.getswitchinterval()
    sys.setswitchinterval(1000.0)
    t = threading.Thread(target=count)
    try:
        with m.new_session(128) as s:
            t.start()
            started.wait()
            # Control: work that holds the GIL lets the counter make no progress.
            before = counter[0]
            sum(range(2_000_000))
            assert counter[0] == before, "control failed: the switch interval did not pin the GIL"
            progress = []
            for _ in range(5):
                s.reset()
                before = counter[0]
                s.generate(prompt, GenerateOptions.greedy(100))
                progress.append(counter[0] - before)
    finally:
        done.set()
        sys.setswitchinterval(old)
        t.join()
    assert sum(progress) > 0, "no Python thread ran during zxv_generate: the GIL was held"


def test_parallel_sessions_on_threads_are_deterministic(engine: Tuple[zxv.Model, bytes],
                                                        vectors: Dict[str, Any]) -> None:
    m, prompt = engine
    results: Dict[int, List[int]] = {}

    def worker(i: int) -> None:
        with m.new_session(128) as s:
            results[i] = s.generate(prompt, GenerateOptions.greedy(16)).token_ids

    threads = [threading.Thread(target=worker, args=(i,)) for i in range(6)]
    for th in threads:
        th.start()
    for th in threads:
        th.join()
    assert all(r == vectors["generate"]["greedy"]["tokens"] for r in results.values()) and len(results) == 6
