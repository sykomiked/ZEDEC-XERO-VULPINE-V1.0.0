#!/usr/bin/env python3
# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
"""Tiamat organism DNA: the record codec (TDNA version 1), reference version.

The record is the byte layout below; kernel/src/dnx (dnx_duplex_encode and
dnx_duplex_decode) turns it into a two-way strand and back, accepting only a
TRUE verdict. This module owns the record bytes. Layout agreed with the
two-way genomic DNA thread (/mnt/project-files/zxv-dnx/TIAMAT_DNA_ENCODING.md),
with the open items settled here.

Big-endian, version 1:
  magic "TDNA" (4), version 1 (1)
  owner key hash: SHA3-256 of the ML-DSA-87 public key (32)
  profile id u8: index into PROFILES
  profile hash: SHA-256 of that profile's canonical JSON in tiamat_swarm.json (32)
  ISF class u8: a Fibonacci index k; the budget per cycle is F(k) tokens.
      Defined: 18 laptop_core (2584), 20 laptop_plus (6765),
      23 workstation (28657), 26 server (121393). Any 2..40 decodes.
  birth seed (8)
  parent A id (32), parent B id (32): zero for a founder; B is zero when a
      chiglet spawns an agent alone. Both stay readable, so kinship checks
      never need the parents' records.
  journal head: hash of the latest reconfiguration journal entry (32)
  hobby count h (1, 3..5), hobby indexes (5): sorted, 0..143, unused 0xFF
  cell count c (1, 1..128)
  c cells: name_len u8 1..32, dragon name (UTF-8); model_len u8 1..96,
      model id (ASCII); revision (20, the Hugging Face commit's SHA-1);
      shipped sha256 (32); adapter sha256 (32, zero = none);
      evidence-head sha256 (32, zero = not fitted yet)
  tissue count t (1, 0..16)
  t tissues: trunk cell index u8 (< c); split layer u16 (cells adapt layers
      >= split; layers below run once for the tissue); member count m u8
      (1..32); m member cell indexes u8 (< c, sorted, distinct, not the trunk;
      a cell is in at most one tissue)
  signature: ML-DSA-87 (4627) by the owner over every byte before it

DNA id = SHA3-256 of the body (everything before the signature).
Decoding checks every bound and count before any field is used; the
signature itself is checked by the caller's ML-DSA-87 verifier.
"""
import hashlib
import json
import pathlib
import struct

MAGIC, VERSION = b"TDNA", 1
SIG_LEN = 4627
PROFILES = ["laptop_core", "laptop_plus", "workstation", "server"]
ISF_CLASS = {"laptop_core": 18, "laptop_plus": 20, "workstation": 23, "server": 26}
MAX_CELLS, MAX_TISSUES, MAX_MEMBERS = 128, 16, 32
CFG = pathlib.Path(__file__).with_name("tiamat_swarm.json")


class DnaError(ValueError):
    pass


def fib(k):
    a, b = 0, 1
    for _ in range(k):
        a, b = b, a + b
    return a


def profile_hash(name, cfg=None):
    cfg = cfg or json.loads(CFG.read_text())
    doc = json.dumps(cfg["profiles"][name], sort_keys=True, separators=(",", ":")).encode()
    return hashlib.sha256(doc).digest()


def encode_body(d):
    if not 3 <= len(d["hobbies"]) <= 5 or sorted(set(d["hobbies"])) != list(d["hobbies"]):
        raise DnaError("hobbies: 3 to 5 distinct, sorted")
    if not all(0 <= h < 144 for h in d["hobbies"]):
        raise DnaError("hobby index out of range")
    out = bytearray(MAGIC + bytes([VERSION]))
    out += _h(d["owner_key_hash"])
    out += bytes([PROFILES.index(d["profile"])]) + _h(d["profile_hash"])
    if not 2 <= d["isf_class"] <= 40:
        raise DnaError("ISF class out of range")
    out += bytes([d["isf_class"]]) + _n(d["birth_seed"], 8)
    out += _h(d["parent_a"]) + _h(d["parent_b"]) + _h(d["journal_head"])
    out += bytes([len(d["hobbies"])]) + bytes(d["hobbies"] + [0xFF] * (5 - len(d["hobbies"])))
    cells = d["cells"]
    if not 1 <= len(cells) <= MAX_CELLS:
        raise DnaError("cell count out of range")
    out += bytes([len(cells)])
    for c in cells:
        name, model = c["name"].encode(), c["model"].encode("ascii")
        if not 1 <= len(name) <= 32 or not 1 <= len(model) <= 96:
            raise DnaError(f"cell {c['name']}: name or model id length")
        out += bytes([len(name)]) + name + bytes([len(model)]) + model
        out += _n(c["revision"], 20) + _h(c["shipped"]) + _h(c["adapter"]) + _h(c["evh"])
    tissues = d["tissues"]
    if len(tissues) > MAX_TISSUES:
        raise DnaError("tissue count out of range")
    out += bytes([len(tissues)])
    seen = set()
    for t in tissues:
        m = t["members"]
        _check_tissue(t["trunk"], m, len(cells), seen)
        out += bytes([t["trunk"]]) + struct.pack(">H", t["split"]) + bytes([len(m)]) + bytes(m)
    return bytes(out)


def encode(d, sign):
    """sign(body) -> 4627-byte ML-DSA-87 signature (the owner's key, kernel/src/companion)."""
    body = encode_body(d)
    sig = sign(body)
    if len(sig) != SIG_LEN:
        raise DnaError("signature length")
    return body + sig


def dna_id(record):
    return hashlib.sha3_256(record[:-SIG_LEN]).digest()


def decode(record):
    """Bounds-checked parse. Returns (fields, body, signature); the caller verifies the signature."""
    r = _Reader(record)
    if r.take(4) != MAGIC or r.u8() != VERSION:
        raise DnaError("not a TDNA v1 record")
    d = {"owner_key_hash": r.take(32)}
    p = r.u8()
    if p >= len(PROFILES):
        raise DnaError("unknown profile id")
    d["profile"], d["profile_hash"] = PROFILES[p], r.take(32)
    d["isf_class"] = r.u8()
    if not 2 <= d["isf_class"] <= 40:
        raise DnaError("ISF class out of range")
    d["birth_seed"] = r.take(8)
    d["parent_a"], d["parent_b"], d["journal_head"] = r.take(32), r.take(32), r.take(32)
    h = r.u8()
    slots = list(r.take(5))
    if not 3 <= h <= 5 or any(x != 0xFF for x in slots[h:]):
        raise DnaError("hobby count or padding")
    d["hobbies"] = slots[:h]
    if d["hobbies"] != sorted(set(d["hobbies"])) or any(x >= 144 for x in d["hobbies"]):
        raise DnaError("hobbies not sorted, distinct and in range")
    c = r.u8()
    if not 1 <= c <= MAX_CELLS:
        raise DnaError("cell count out of range")
    d["cells"] = []
    for _ in range(c):
        n = r.u8()
        if not 1 <= n <= 32:
            raise DnaError("name length")
        name = r.take(n).decode("utf-8")
        m = r.u8()
        if not 1 <= m <= 96:
            raise DnaError("model id length")
        d["cells"].append({"name": name, "model": r.take(m).decode("ascii"), "revision": r.take(20),
                           "shipped": r.take(32), "adapter": r.take(32), "evh": r.take(32)})
    t = r.u8()
    if t > MAX_TISSUES:
        raise DnaError("tissue count out of range")
    d["tissues"], seen = [], set()
    for _ in range(t):
        trunk, split, m = r.u8(), r.u16(), r.u8()
        members = list(r.take(m))
        _check_tissue(trunk, members, c, seen)
        d["tissues"].append({"trunk": trunk, "split": split, "members": members})
    body_len = r.pos
    sig = r.take(SIG_LEN)
    if r.pos != len(record):
        raise DnaError("trailing bytes")
    d["isf_tokens_per_cycle"] = fib(d["isf_class"])
    return d, record[:body_len], sig


def _check_tissue(trunk, members, c, seen):
    if trunk >= c or not 1 <= len(members) <= MAX_MEMBERS:
        raise DnaError("tissue trunk or member count")
    if members != sorted(set(members)) or any(x >= c or x == trunk for x in members):
        raise DnaError("tissue members not sorted, distinct, in range and apart from the trunk")
    if seen & set(members):
        raise DnaError("a cell is in two tissues")
    seen.update(members)


def _h(b):
    if len(b) != 32:
        raise DnaError("hash must be 32 bytes")
    return b


def _n(b, n):
    if len(b) != n:
        raise DnaError(f"field must be {n} bytes")
    return b


class _Reader:
    def __init__(self, b):
        self.b, self.pos = b, 0

    def take(self, n):
        if self.pos + n > len(self.b):
            raise DnaError("record truncated")
        out = self.b[self.pos:self.pos + n]
        self.pos += n
        return bytes(out)

    def u8(self):
        return self.take(1)[0]

    def u16(self):
        return struct.unpack(">H", self.take(2))[0]


def founder_example():
    """A laptop_core organism of the disc's 1.5B tissue, with placeholder hashes; for tests."""
    z = bytes(32)
    cells = [{"name": n, "model": m, "revision": bytes.fromhex("11" * 20), "shipped": bytes([i]) * 32,
              "adapter": z if i == 0 else bytes([0x80 | i]) * 32, "evh": z}
             for i, (n, m) in enumerate([("Spinewyrm", "Qwen/Qwen2.5-1.5B-Instruct"),
                                         ("Wardendrake", "JackKozmo29/codeguard-jev-style-1.5b"),
                                         ("Debatelindworm", "iamjayeshc/ArgParser-v4-Qwen1.5B")])]
    return {"owner_key_hash": bytes([7]) * 32, "profile": "laptop_core", "profile_hash": profile_hash("laptop_core"),
            "isf_class": ISF_CLASS["laptop_core"], "birth_seed": bytes(range(8)), "parent_a": z, "parent_b": z,
            "journal_head": z, "hobbies": [3, 40, 99], "cells": cells,
            "tissues": [{"trunk": 0, "split": 14, "members": [1, 2]}]}
