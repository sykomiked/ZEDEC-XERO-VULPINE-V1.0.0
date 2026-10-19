#!/usr/bin/env python3
# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
"""gen_lattice_routing.py — the lattice interconnect table: every kernel
module mapped onto a coordinate of the SCP-144 lattice, embedded in the ELF
(zxv_lattice_route.h) the way the seed carries the blueprint.

THE NON-LINEAR BUILD MADE CONCRETE. Instead of a flat symbol list resolved
by a serial linker, the strata map (build_system/strata.txt) is projected
onto the lattice's partial order:

  * the strata S0..S8 map to lattice rings (a stratum's modules occupy the
    shell the ring defines: base modules live near the lattice origin,
    higher strata in outer rings — the build order is a ring sweep, not a
    pointer chase);
  * every module becomes one cell: (i, j) assigned deterministically from
    the sorted module list within its stratum's ring, row-major from the
    ring's origin (the same task ordering the GPU scaffold kernel proves);
  * the table is a canonical byte image — magic ZLR0, version, stratum
    count, module count, then one record per module
    (name_hash, stratum, ring, i, j, dep_count, dep cell hashes);
  * the whole image is sha256'd to route_root, and
    zxv_lattice_route_selfcheck() in the kernel recomputes the projection
    and compares — fail-closed, like the seed and the SCP-144 ROM.

Usage: gen_lattice_routing.py <strata.txt> <out header>
"""
import hashlib
import pathlib
import re
import sys

MAGIC = b"ZLR0"
VERSION = 1

# the strata order -> ring assignment. S0 (base) sits at the lattice origin;
# the rings widen through the strata the way the lattice widens.
STRATUM_RING = {
    "base":       2,   # near the origin (ring 1 is the base cell itself)
    "kernel":     3,
    "transport":  5,
    "trust":      6,
    "logic":      7,
    "ai":         8,
    "settlement": 9,
    "adapters":   10,
    "above":      11,
}

# the distinct-ladder ring bounds, matching zvt_seed_dkern.h / the scaffold
# kernel exactly: ring k holds cells with f[k-1] <= max(i,j) < f[k].
FIB = [0, 1, 2, 3, 5, 8, 13, 21, 34, 55, 89, 144]


def parse_strata(path):
    """-> list of (path, stratum) for every module line."""
    mods = []
    for line in path.read_text().splitlines():
        line = line.strip()
        if not line or line.startswith("#"):
            continue
        parts = line.split()
        if len(parts) < 2:
            continue
        mods.append((parts[0], parts[1]))
    return mods


def ring_cells(ring):
    """the cells of a ring's shell, in task order (new rows then new cols,
    matching zk_build_task): the annulus [prev, F_k)^2 minus [0, prev)^2."""
    prev, f_k = FIB[ring - 1], FIB[ring]
    cells = []
    for i in range(prev, f_k):
        for j in range(f_k):
            cells.append((i, j))
    for i in range(prev):
        for j in range(prev, f_k):
            cells.append((i, j))
    return cells


def assign(mods):
    """modules ordered by (stratum order, path) laid out in ring order:
    the cells of ring k all fill before ring k+1, so the deepest strata sit
    nearest the origin and the outermost ring carries the products. The
    projection is total and deterministic; ring k's shell is the task order
    the scaffold kernel proves."""
    order = sorted(mods,
                   key=lambda ms: (STRATUM_RING.get(ms[1], 11), ms[0]))
    cells = []
    for ring in range(1, 12):
        cells += ring_cells(ring)
    if len(order) > len(cells):
        raise RuntimeError("lattice full: %d modules, %d cells"
                           % (len(order), len(cells)))
    assign_ = {}
    n = 0
    for ring in range(1, 12):
        for c in ring_cells(ring):
            if n >= len(order):
                break
            assign_[order[n][0]] = (c, ring)
            n += 1
    return assign_


# "the lattice is the routing; what part lights up is a matter of context."
# The MegaROM world (BOOTSEL.TVL) selects which strata are live — a stratum
# mask, not a ring mask, because a ring's cells are shared across strata.
# Every world lights the two lower anchors (base + kernel + the logic and
# trust they sit on); server lights the mesh and the ledgers (transport,
# settlement), user lights the chiglet desktop and the products (ai,
# adapters, above), foundry lights the whole ladder.
STRATA_ORDER = ["base", "kernel", "transport", "trust", "logic",
                "ai", "settlement", "adapters", "above"]


def world_mask(world):
    """TVL_WORLD_SERVER 1, TVL_WORLD_USER 2, TVL_WORLD_FOUNDRY 3; bit k
    lights STRATA_ORDER[k]."""
    lit = {0, 1, 3, 4}                       # base kernel trust logic
    if world == 1:                           # server: + transport settlement
        lit |= {2, 6}
    elif world == 2:                         # user: + ai adapters above
        lit |= {5, 7, 8}
    else:                                    # foundry: the whole ladder
        lit = set(range(len(STRATA_ORDER)))
    return sum(1 << i for i in lit)


WORLD_MASKS = [world_mask(1), world_mask(2), world_mask(3)]


def build(mods):
    """the canonical byte image of the routing table."""
    assign_ = assign(mods)
    # module name hash: first 4 bytes of sha256 of the path
    def nh(p):
        return int.from_bytes(hashlib.sha256(p.encode()).digest()[:4], "big")

    # THE 144 PLANES + THE 145TH CONTAINER. Each of the 144 lattice axes is
    # a tensor plane; the nine strata partition them into bands of 16 axes
    # (9*16 = 144). A module's plane is its stratum's band plus a hash
    # offset — so which plane carries a module is a function of its role
    # AND its identity. The 145th layer is the container: the trailer that
    # counts every plane's occupancy, sealed inside the digest — nothing
    # escapes the manifold without passing the membrane.
    def plane(path, st):
        band = STRATA_ORDER.index(st) * 16 if st in STRATA_ORDER else 0
        return band + nh(path) % 16

    planes = [0] * 144
    out = bytearray()
    out += MAGIC
    out += bytes([VERSION, 0, 0, 0])       # version + flags[3]
    out += len(STRATUM_RING).to_bytes(4, "big")
    out += len(assign_).to_bytes(4, "big")
    # the three world masks: server, user, foundry — which strata the
    # context lights up. 9 bits each, bit k = STRATA_ORDER[k] live.
    for m in WORLD_MASKS:
        out += m.to_bytes(4, "big")
    for path in sorted(assign_):
        (i, j), ring = assign_[path]
        st = next(s for p, s in mods if p == path)
        pl = plane(path, st)
        planes[pl] += 1
        out += nh(path).to_bytes(4, "big")
        out += bytes([STRATA_ORDER.index(st) if st in STRATA_ORDER else 0,
                      ring, i & 0xFF, j & 0xFF])
        # dep count: for the committed table we record the cell's geometric
        # predecessors (the scaffold's lower-ring deps), 0 for ring 1.
        ndep = 0 if ring <= 1 else (2 if (i >= FIB[ring-1] and j >= FIB[ring-1]) else 1)
        out += bytes([ndep, pl & 0xFF])

    # the 145th layer: the container trailer, inside the seal. "ZLRX",
    # the plane count, and one u16 occupancy per axis — the membrane's
    # census of every plane it encloses.
    out += b"ZLRX"
    out += (144).to_bytes(2, "big")
    for c in planes:
        out += c.to_bytes(2, "big")

    digest = hashlib.sha256(bytes(out)).digest()
    out += digest                        # appended for the self-check
    return bytes(out), digest


def emit(out, digest, path):
    lines = ["/* generated by gen_lattice_routing.py -- do not edit */",
             "#ifndef ZXV_LATTICE_ROUTE_H", "#define ZXV_LATTICE_ROUTE_H", "",
             "#include <stdint.h>", "",
             "#define ZXV_LATTICE_ROUTE_LEN %uu" % len(out), ""]
    lines.append("static const uint8_t zxv_lattice_route[ZXV_LATTICE_ROUTE_LEN] = {")
    for off in range(0, len(out), 16):
        lines.append("    " + ", ".join("0x%02X" % b for b in out[off:off+16]) + ",")
    lines.append("};")
    lines += ["", "static const uint8_t zxv_lattice_route_root[32] = {"]
    lines.append("    " + ", ".join("0x%02X" % b for b in digest))
    lines += ["};", "", "#endif /* ZXV_LATTICE_ROUTE_H */", ""]
    pathlib.Path(path).write_text("\n".join(lines))


def main(argv):
    strata = pathlib.Path(argv[1])
    out_path = pathlib.Path(argv[2])
    mods = parse_strata(strata)
    img, digest = build(mods)
    if "--bin" in argv:
        out_path.write_bytes(img)
    else:
        emit(img, digest, out_path)
    print("lattice routing: %d modules over 9 strata, digest %s" %
          (len(mods), digest.hex()))


if __name__ == "__main__":
    main(sys.argv)