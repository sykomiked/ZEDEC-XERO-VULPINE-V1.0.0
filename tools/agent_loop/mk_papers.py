#!/usr/bin/env python3
# mk_papers.py — generate the post-doctorate paper stack on the GPU. The
# LLM writes; the artifacts are the source of truth. Each paper is a
# discipline silo with: the claim, the construction (cited by path), the
# proof, the measurements, the limitations, the reproduction, and an
# annotated bibliography citing the scientists, mathematicians and
# philosophers whose principles the work validates.
#
# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
"""usage: mk_papers.py [--out docs/papers] [--only I] [--model URL]"""
import argparse
import json
import os
import sys
import urllib.request

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "../.."))

# the ten silos: (number, the title, the discipline, the artifacts, the
# canonical references the paper must cite and annotate)
PAPERS = [
    ("I", "The Paraconsistent Ring 0", "formal logic / paraconsistent logic",
     ["kernel/src/lpres/lpres.c", "kernel/src/lpres/lpres_bridge.c",
      "tools/agent_loop/lattice_rules.py"],
     ["Belnap (1977) A Useful Four-Valued Logic",
      "Priest (1979) The Logic of Paradox",
      "da Costa (1974) On the theory of inconsistent formal systems",
      "Kleene (1952) Introduction to Metamathematics",
      "Dunn (1976) Intuitive semantics for first-degree entailments",
      "Łukasiewicz (1920) O logice trójwartościowej"]),
    ("II", "The Traffic-Driven Resource Economy", "economics / accounting",
     ["kernel/src/vino/vino.c", "kernel/src/tribute/tribute.c",
      "kernel/src/swarm/swarm_denom.h"],
     ["Pacioli (1494) Summa de arithmetica (double-entry)",
      "Ijiri (1989) Momentum accounting and triple-entry bookkeeping",
      "Zeckendorf (1972) Représentation des nombres naturels par une somme",
      "Fibonacci (1202) Liber Abaci",
      "Ostrom (1990) Governing the Commons",
      "Gesell (1916) The Natural Economic Order"]),
    ("III", "The Immutable Epistemic Graph", "distributed systems",
     ["build_system/mk_curriculum_car.py", "docs/curriculum/EXCHANGE.json"],
     ["Merkle (1987) A digital signature based on a conventional encryption function",
      "Benet (2014) IPFS - Content Addressed, Versioned, P2P File System",
      "Lamport (1978) Time, Clocks, and the Ordering of Events",
      "Shapiro et al. (2011) Conflict-free Replicated Data Types",
      "Nakamoto (2008) Bitcoin: A Peer-to-Peer Electronic Cash System"]),
    ("IV", "The 144-Dimensional Lattice", "geometry of numbers / modular forms",
     ["kernel/src/lpres/lpres_phases.h", "tools/agent_loop/mk_lpres_phases.py"],
     ["Korkine & Zolotareff (1873) Sur les formes quadratiques",
      "Voronoi (1908) Nouvelles applications des paramètres continus",
      "Viazovska (2017) The sphere packing problem in dimension 8",
      "Conway & Sloane (1988) Sphere Packings, Lattices and Groups",
      "Mallows, Odlyzko & Sloane (1975) Upper bounds for modular forms",
      "Venkov (2001) Even unimodular extremal lattices",
      "Pisano (c.1202) the Pisano period"]),
    ("V", "SCP-144: Sicilian Crown Protection", "cryptography / security",
     ["tools/agent_loop/lattice_harden.py", "kernel/src/mlkem/keccak.c"],
     ["Shor (1994) Algorithms for quantum computation: discrete logarithms and factoring",
      "Grover (1996) A fast quantum mechanical algorithm for database search",
      "Regev (2005) On lattices, learning with errors, random linear codes",
      "Ajtai (1996) Generating hard instances of lattice problems",
      "NIST FIPS 203/204 (2024) ML-KEM / ML-DSA",
      "Diffie & Hellman (1976) New Directions in Cryptography",
      "Zimmermann (1991) PGP"]),
    ("VI", "Negentropy in Practice", "complex systems / thermodynamics",
     ["tools/agent_loop/NEGENTROPY.md"],
     ["Shannon (1948) A Mathematical Theory of Communication",
      "Boltzmann (1877) Über die Beziehung zwischen dem zweiten Hauptsatze",
      "Schrödinger (1944) What is Life? (negative entropy)",
      "Prigogine (1977) Self-Organization in Nonequilibrium Systems",
      "Landauer (1961) Irreversibility and heat generation in computing",
      "Wiener (1948) Cybernetics"]),
    ("VII", "The Omni-Tautology", "software architecture",
     ["tools/agent_loop/MASTER.md", "tools/agent_loop/completion_state.py"],
     ["Dijkstra (1968) Go To Statement Considered Harmful",
      "Hoare (1969) An Axiomatic Basis for Computer Programming",
      "Parnas (1972) On the Criteria To Be Used in Decomposing Systems",
      "Conway (1968) How Do Committees Invent?",
      "Meyer (1988) Object-Oriented Software Construction"]),
    ("VIII", "Universal Language Hosting", "linguistics / compiler theory",
     ["tools/agent_loop/ROSETTA.md", "web/i18n/sa.json"],
     ["Pāṇini (c.4th c. BCE) Aṣṭādhyāyī",
      "Chomsky (1957) Syntactic Structures",
      "Turing (1936) On Computable Numbers",
      "McCarthy (1960) Recursive Functions of Symbolic Expressions",
      "Dee (1583) Liber Loagaeth (Enochian)",
      "Aho, Sethi & Ullman (1986) Compilers: Principles, Techniques, and Tools"]),
    ("IX", "The World Computer", "web systems / distributed computing",
     ["web/boot/zxv_app_wasm.c", "tools/agent_loop/WASM_FACTORY.md"],
     ["Berners-Lee (1989) Information Management: A Proposal",
      "Haas et al. (2017) Bringing the Web up to Speed with WebAssembly",
      "Fielding (2000) Architectural Styles and the Design of Network-based Software",
      "Hickey (2011) The Web is the Computer"]),
    ("X", "Adaptive Hardware", "operating systems / hardware",
     ["tools/agent_loop/ADAPTIVE_DRIVERS.md", "kernel/src/net/m5route.c"],
     ["Dijkstra (1968) The Structure of the THE Multiprogramming System",
      "Ritchie & Thompson (1974) The UNIX Time-Sharing System",
      "Torvalds (1991) Linux (the monolith, for contrast)",
      "Liedtke (1995) On µ-Kernel Construction",
      "Bovet & Cesati (2005) Understanding the Linux Kernel"]),
]


def vllm(prompt, url, model, tokens=6000):
    import http.client
    from urllib.parse import urlparse
    body = json.dumps({"model": model, "messages": [{"role": "user", "content": prompt}],
                       "max_tokens": tokens, "temperature": 0.4, "top_p": 0.9,
                       "stream": True}).encode()
    u = urlparse(url)
    conn = http.client.HTTPConnection(u.hostname or "127.0.0.1", u.port or 8000, timeout=3600)
    parts = []
    try:
        conn.request("POST", "/v1/chat/completions", body=body,
                     headers={"Content-Type": "application/json", "Accept": "text/event-stream"})
        resp = conn.getresponse()
        if resp.status != 200:
            raise RuntimeError("HTTP %d" % resp.status)
        for line in resp:
            line = line.decode("utf-8", "replace").strip()
            if not line.startswith("data:"):
                continue
            p = line[5:].strip()
            if p == "[DONE]":
                break
            try:
                d = json.loads(p)
            except Exception:
                continue
            piece = ((d.get("choices") or [{}])[0].get("delta") or {}).get("content")
            if piece:
                parts.append(piece)
    finally:
        conn.close()
    return "".join(parts)


def prompt_for(num, title, disc, artifacts, refs):
    refs_list = "\n".join("  - %s" % r for r in refs)
    arts = "\n".join("  - %s" % a for a in artifacts)
    return """You are writing paper %s of a post-doctorate dissertation stack on the ZXV
(ZEDEC XERO VULPINE) system. The paper is a DISCIPLINARY SILO: it stands alone,
readable without the others, and belongs to its discipline.

TITLE: %s
DISCIPLINE: %s
THE SYSTEM ARTIFACTS IT RESTS ON (cite by path):
%s

THE REQUIRED REFERENCES (cite and ANNOTATE each — a sentence on what it
established and how this work validates or extends it):
%s

Write the paper in rigorous academic prose. Structure exactly:
1. ABSTRACT — the claim in 5 sentences.
2. INTRODUCTION — the problem, in the discipline's own terms.
3. THE CONSTRUCTION — how ZXV realizes it, citing the artifact paths.
4. THE MATHEMATICAL PROOF — the core result, with the actual argument
   (state the theorem, the proof, the invariants). Be exact: integers,
   ranks, the chain FALSE < NEITHER < BOTH < TRUE, the Fibonacci ladder,
   the never-authorise guard. No hand-waving.
5. THE MEASUREMENTS — the concrete numbers (the anchors, the ratio, the
   byte-identity), stated as what was observed.
6. THE ANNOTATED BIBLIOGRAPHY — every reference above, annotated.
7. THE LIMITATIONS — stated honestly.
8. THE REPRODUCTION — the exact commands.

Be exact, cite the references, and do not overclaim. This is dissertation
quality."""


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", default="docs/papers")
    ap.add_argument("--only", default="")
    ap.add_argument("--model", default=os.environ.get("VLLM_URL", "http://127.0.0.1:8000"))
    ap.add_argument("--name", default=os.environ.get("VLLM_MODEL",
                    "Qwen/Qwen2.5-Coder-32B-Instruct-AWQ"))
    args = ap.parse_args()
    out = os.path.join(ROOT, args.out)
    os.makedirs(out, exist_ok=True)
    for num, title, disc, arts, refs in PAPERS:
        if args.only and args.only != num:
            continue
        fn = os.path.join(out, "PAPER_%s_%s.md" % (num, title.split(":")[0].replace(" ", "_")))
        if os.path.exists(fn) and os.path.getsize(fn) > 500:
            print("  %s: exists, skipping" % num)
            continue
        print("  %s: generating (%s)..." % (num, disc))
        try:
            body = vllm(prompt_for(num, title, disc, arts, refs), args.model, args.name)
        except Exception as e:
            print("    %s: model error %s" % (num, str(e)[:80]))
            continue
        if body and len(body) > 300:
            with open(fn, "w") as f:
                f.write(body)
            print("    %s: written (%d chars)" % (num, len(body)))
        else:
            print("    %s: the model returned nothing usable" % num)
    return 0


if __name__ == "__main__":
    sys.exit(main())
