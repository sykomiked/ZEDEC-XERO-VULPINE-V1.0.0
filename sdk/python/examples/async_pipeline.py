# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
"""asyncio: stream tokens, wait for budget, and wait on phase ticks.

Run:  ZXV_LIBRARY_PATH=../libzxv/build python examples/async_pipeline.py
"""

import asyncio
from pathlib import Path

import zxv
from zxv import GenerateOptions

FIX = Path(__file__).resolve().parents[2] / "conformance" / "fixtures"


async def main() -> None:
    with zxv.Context() as ctx, ctx.root_capability() as cap:
        clock = zxv.PhaseClock(cap)
        driver = clock.start_realtime(ticks_per_second=27_720)      # one fundamental per second

        budgets = zxv.Budgets(cap)
        budgets.on_exhausted(lambda b: print(f"  budget {b.name!r} exhausted; waiting for a refill"))
        quota = budgets.create("requests", zxv.Resource.COMPUTE_CYCLES, 1)

        async def refill_later() -> None:
            await clock.until_tick(clock.now + 2_772)                # a tenth of a fundamental
            quota.release(1)

        await quota.consume_async(1)                                 # immediate
        refill = asyncio.create_task(refill_later())
        await quota.consume_async(1, timeout=5)                     # waits for the refill
        await refill
        print("got the second request slot at tick", clock.now)

        model = zxv.Model.open(cap, str(FIX / "tiny-qwen2.gguf"), str(FIX / "tiny-tokenizer.gguf"))
        with model.new_session(128) as session:
            prompt = (FIX / "prompt.txt").read_text()
            async with session.stream_async(prompt, GenerateOptions.greedy(16)) as stream:
                async for token in stream:
                    print(f"  token {token.id:5d} {token.piece!r}")
            print("stop:", stream.result.stop.name if stream.result else None)

        await driver.stop()
        model.close()


asyncio.run(main())
