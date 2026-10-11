# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
"""AI orchestration across worker "nodes": several Sessions on several threads.

The story: a coordinator runs a swarm of agents, each on its own worker
(think: one per accelerator node). The swarm economy decides how many
tokens each agent may spend this cycle; each agent's session is bound to an
exact rmag budget of that size; the phase clock admits each step; the model
file is attested before anything runs; every agent's answer is signed with
ML-DSA-87 so the coordinator (or anyone) can verify who said what.

HONEST LIMITS. The ZXV engine is integer-only and runs on the CPU: there is
no GPU or accelerator offload in ABI 1.1. Here the "nodes" are threads in
one process. What carries over to real multi-node deployments is the
property shown at the end: generation is bit-identical everywhere, so any
node's output can be re-run and checked by any other node.

Run:  ZXV_LIBRARY_PATH=../libzxv/build python examples/orchestrate_sessions.py [model.gguf tokenizer.gguf]
"""

from __future__ import annotations

import sys
import threading
import time
from dataclasses import dataclass
from pathlib import Path
from typing import Dict, List

import zxv
from zxv import Cap, GenerateOptions, Truth

FIXTURES = Path(__file__).resolve().parents[2] / "conformance" / "fixtures"
MODEL = Path(sys.argv[1]) if len(sys.argv) > 1 else FIXTURES / "tiny-qwen2.gguf"
TOKENIZER = Path(sys.argv[2]) if len(sys.argv) > 2 else FIXTURES / "tiny-tokenizer.gguf"
PROMPT = (FIXTURES / "prompt.txt").read_text(encoding="utf-8")

AGENTS = {101: 0, 102: 1, 103: 1, 104: 2, 105: 2, 106: 2}   # model id -> Fibonacci level
TOKENS_PER_CYCLE = 600


@dataclass
class AgentResult:
    agent: int
    node: str
    tokens: List[int]
    stop: zxv.StopReason
    signature: bytes
    seconds: float
    allotment: zxv.Allotment


def main() -> None:
    with zxv.Context() as ctx, ctx.root_capability() as root:
        # 1. Attest the model before it may run (actuator authority needs a clear gate).
        with zxv.Attestations(root.attenuate(Cap.ATTEST)) as reg:
            digest = zxv.Hashes.sha3_256(MODEL.read_bytes())
            reg.attest(MODEL.name, zxv.Subject.MODEL_PACK, Truth.TRUE, "coordinator", digest)
            assert reg.gate(), "model attestation is not certain"

        # 2. Least privilege: the workers get inference + budget only; signing stays here.
        worker_cap = root.attenuate(Cap.AI_INFERENCE | Cap.BUDGET)
        signer = zxv.MlDsa87(root.attenuate(Cap.CRYPTO_SIGN))
        keys = signer.generate()

        model = zxv.Model.open(worker_cap, str(MODEL), str(TOKENIZER))   # shared, read-only
        budgets = zxv.Budgets(worker_cap)
        clock = zxv.PhaseClock(root.attenuate(Cap.PHASE))
        swarm = zxv.Swarm(root.attenuate(Cap.SWARM), levels=3, tokens_per_cycle=TOKENS_PER_CYCLE)
        for agent, level in AGENTS.items():
            swarm.register(agent, level)
        swarm.set_mood("\U0001F914", 3)                 # thinking: shifts the imaginary share
        swarm.set_feeling(102, "\U0001F604", 4)
        cycle = swarm.begin_cycle()
        clock.set_health(6, zxv.Health.HEALTHY)

        results: Dict[int, AgentResult] = {}
        lock = threading.Lock()

        def run_agent(agent: int) -> None:
            node = threading.current_thread().name
            allot = swarm.allotment(agent)
            admission = clock.admit(6, agent, zxv.StepFlag.REQUIRES_HEALTH)
            if not admission.admitted:
                print(f"agent {agent}: step {admission.decision.name}")
                return
            # Each agent gets an exact budget = its swarm allotment (prompt + generated tokens).
            budget = budgets.create(f"agent-{agent}", zxv.Resource.TOKENS, allot.allotted)
            with model.new_session(n_ctx=256) as session:
                session.bind_budget(budget)
                t0 = time.perf_counter()
                r = session.generate(PROMPT, GenerateOptions(max_tokens=64, temperature="0.7",
                                                              top_k=40, seed=agent))
                dt = time.perf_counter() - t0
            used = r.n_prompt + r.n_generated
            swarm.consume(agent, min(used, allot.allotted))
            sig = signer.sign(keys.secret_key, bytes(str(r.token_ids), "ascii"), context=b"zxv-agent")
            with lock:
                results[agent] = AgentResult(agent, node, r.token_ids, r.stop, sig, dt, allot)

        # 3. Fan out: one thread ("node") per agent. zxv_generate releases the GIL,
        #    so the threads really generate in parallel on the CPU cores.
        threads = [threading.Thread(target=run_agent, args=(a,), name=f"node-{i}")
                   for i, a in enumerate(AGENTS)]
        t0 = time.perf_counter()
        for t in threads:
            t.start()
        for t in threads:
            t.join()
        wall = time.perf_counter() - t0
        expired = swarm.end_cycle()

        print(f"cycle {cycle}: {len(results)} agents in {wall * 1000:.1f} ms wall, {expired} tokens expired")
        for agent in sorted(results):
            r = results[agent]
            a = r.allotment
            ok = signer.verify(keys.public_key, bytes(str(r.tokens), "ascii"), r.signature, b"zxv-agent")
            print(f"  agent {agent} on {r.node}: allotted {a.allotted:4d} (imaginary {a.imaginary:3d}), "
                  f"{len(r.tokens):2d} tokens, stop={r.stop.name:<10} signature {'valid' if ok else 'INVALID'}")

        # 4. Determinism: re-run one agent on this thread and get the same tokens.
        probe = next(iter(results))
        with model.new_session(n_ctx=256) as s:
            again = s.generate(PROMPT, GenerateOptions(max_tokens=len(results[probe].tokens),
                                                       temperature="0.7", top_k=40, seed=probe))
        same = again.token_ids == results[probe].tokens
        print(f"re-run of agent {probe} on another thread is bit-identical: {same}")

        keys.close()                         # wipes the ML-DSA secret key
        for h in (swarm, clock, budgets, model):
            h.close()


if __name__ == "__main__":
    main()
