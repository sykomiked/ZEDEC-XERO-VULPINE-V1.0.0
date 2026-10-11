# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
"""Write sdk/conformance/vectors.json as a Fortran module of named constants.

Fortran has no JSON reader, so the conformance test (test/test_zxv.f90)
uses this generated module instead: every value comes from vectors.json,
nothing is typed in by hand. The output goes to the build directory.

usage: python3 tools/gen_vectors_f90.py vectors.json conformance_vectors.f90
"""
import json
import sys

WIDTH = 100


def wrap(items):
    """Comma-separated items as continuation lines."""
    lines, cur = [], ""
    for i, it in enumerate(items):
        piece = it + (", " if i + 1 < len(items) else "")
        if len(cur) + len(piece) > WIDTH:
            lines.append(cur.rstrip())
            cur = ""
        cur += piece
    lines.append(cur.rstrip())
    return " &\n    ".join(lines)


def i64(v):
    if v == -(2**63):
        return "(-9223372036854775807_int64 - 1_int64)"
    return f"{v}_int64"


def signed64(hexbits):
    v = int(hexbits, 16)
    return v - 2**64 if v >= 2**63 else v


def int_array(name, kind, values, shape=None):
    if kind == "int64":
        vals = [i64(v) for v in values]
    elif kind == "int8":
        vals = [f"{v}_int8" for v in values]
    else:
        vals = [str(v) for v in values]
    body = wrap(vals)
    if shape is None:
        return f"  integer({kind}), parameter :: {name}({len(values)}) = [ &\n    {body}]\n"
    dims = ", ".join(shape)
    ext = ", ".join(str(int(s.split(":")[1]) - int(s.split(":")[0]) + 1) if ":" in s else s for s in shape)
    return (f"  integer({kind}), parameter :: {name}({dims}) = reshape([ &\n    {body}], [{ext}])\n")


def str_array(name, values, lb=1):
    n = max(1, max(len(v) for v in values))
    vals = ["'" + v.replace("'", "''") + "'" for v in values]
    dims = f"{lb}:{lb + len(values) - 1}" if lb != 1 else f"{len(values)}"
    return (f"  character(len={n}), parameter :: {name}({dims}) = [character(len={n}) :: &\n"
            f"    {wrap(vals)}]\n")


def bytes_of(hexs):
    b = bytes.fromhex(hexs)
    return [v - 256 if v > 127 else v for v in b]


def main(src, dst):
    with open(src, encoding="utf-8") as f:
        v = json.load(f)
    o = []
    o.append("! generated from sdk/conformance/vectors.json by tools/gen_vectors_f90.py; do not edit\n")
    o.append("module conformance_vectors\n  use, intrinsic :: iso_fortran_env, only: int8, int32, int64\n"
             "  implicit none\n  public\n\n")

    st = v["status"]
    o.append(f"  integer, parameter :: V_STATUS_N = {len(st)}\n")
    o.append(int_array("V_STATUS_CODE", "int32", [s["code"] for s in st]))
    o.append(int_array("V_STATUS_CLASS", "int32", [s["class"] for s in st]))
    o.append(str_array("V_STATUS_NAME", [s["name"] for s in st]))

    ro = v["rational_ops"]
    o.append(f"\n  integer, parameter :: V_RAT_N = {len(ro)}\n")
    for key in ("a", "b", "add", "sub", "mul", "div"):
        flat = [x for r in ro for x in r[key]]
        o.append(int_array(f"V_RAT_{key.upper()}", "int64", flat, ["2", "V_RAT_N"]))
    o.append(int_array("V_RAT_CMP", "int32", [r["cmp"] for r in ro]))

    rp = v["rational_parse"]
    o.append(f"\n  integer, parameter :: V_PARSE_N = {len(rp)}\n")
    o.append(str_array("V_PARSE_TEXT", [r["text"] for r in rp]))
    o.append(int_array("V_PARSE_STATUS", "int32", [r["status"] for r in rp]))
    o.append(int_array("V_PARSE_VALUE", "int64", [x for r in rp for x in r.get("value", [0, 1])], ["2", "V_PARSE_N"]))
    o.append(str_array("V_PARSE_FORMAT", [r.get("format", "") for r in rp]))

    ie = v["ieee754"]
    o.append(f"\n  integer, parameter :: V_IEEE_N = {len(ie)}\n")
    o.append(int_array("V_IEEE_BITS", "int64", [signed64(r["bits"]) for r in ie]))
    o.append(int_array("V_IEEE_STATUS", "int32", [r["status"] for r in ie]))
    o.append(int_array("V_IEEE_VALUE", "int64", [x for r in ie for x in r.get("value", [0, 1])], ["2", "V_IEEE_N"]))

    q = v["q16"]
    o.append(f"\n  integer, parameter :: V_Q16_N = {len(q)}\n")
    o.append(int_array("V_Q16_VALUE", "int64", [x for r in q for x in r["value"]], ["2", "V_Q16_N"]))
    o.append(int_array("V_Q16_STATUS", "int32", [r["status"] for r in q]))
    o.append(int_array("V_Q16_Q", "int32", [r["q16"] for r in q]))

    lp = v["lpres"]
    o.append("\n")
    o.append(str_array("V_LP_NAME", lp["names"], lb=0))
    o.append(int_array("V_LP_NOT", "int32", lp["not"], ["0:3"]))
    # Fortran is column-major: element (a, b) is and[a][b]
    o.append(int_array("V_LP_AND", "int32", [lp["and"][a][b] for b in range(4) for a in range(4)], ["0:3", "0:3"]))
    o.append(int_array("V_LP_OR", "int32", [lp["or"][a][b] for b in range(4) for a in range(4)], ["0:3", "0:3"]))

    h = v["hex"]
    o.append("\n")
    o.append(str_array("V_HEX_BASE_NAME", h["base_names"], lb=0))
    o.append(str_array("V_HEX_POL_NAME", h["polarity_names"], lb=0))
    o.append(str_array("V_HEX_PHASE_NAME", h["phase_names"], lb=0))
    o.append(str_array("V_HEX_ACTION_NAME", h["action_names"], lb=0))
    o.append(int_array("V_HEX_LP_TO_BASE", "int32", h["lp_to_base"], ["0:3"]))
    o.append(int_array("V_HEX_BASE_TO_LP", "int32", h["base_to_lp"], ["0:3"]))
    o.append(int_array("V_HEX_PHASE_NEXT", "int32", h["phase_next"], ["0:3"]))
    tags = sorted(h["tags"], key=lambda t: t["tag"])
    o.append(str_array("V_HEX_NAME", [t["name"] for t in tags], lb=0))
    for key in ("base", "polarity", "phase", "action"):
        o.append(int_array(f"V_HEX_{key.upper()}", "int32", [t[key] for t in tags], ["0:63"]))
    o.append(int_array("V_HEX_NOT", "int32", h["not"], ["0:63"]))
    o.append(int_array("V_HEX_AND", "int32", [h["and"][a][b] for b in range(64) for a in range(64)], ["0:63", "0:63"]))
    o.append(int_array("V_HEX_OR", "int32", [h["or"][a][b] for b in range(64) for a in range(64)], ["0:63", "0:63"]))
    o.append(int_array("V_HEX_STEP", "int32", [h["step"][t][p] for p in range(4) for t in range(64)], ["0:63", "0:3"]))
    # set[tag][base * 4 + polarity]
    o.append(int_array("V_HEX_SET", "int32", [h["set"][t][k] for k in range(16) for t in range(64)], ["0:63", "0:15"]))
    pol = h["polarity"]
    o.append(f"  integer, parameter :: V_POL_N = {len(pol)}\n")
    for key in ("pos0", "neg0", "pos1", "neg1"):
        o.append(int_array(f"V_POL_{key.upper()}", "int64", [p[key] for p in pol]))
    o.append(f"  logical, parameter :: V_POL_STALE({len(pol)}) = [ &\n    "
             + wrap([".true." if p["stale"] else ".false." for p in pol]) + "]\n")
    o.append(int_array("V_POL_RESULT", "int32", [p["polarity"] for p in pol]))

    ql = h["qlog"]
    recs = ql["records"]
    o.append(f"\n  integer, parameter :: V_QLOG_N = {len(recs)}\n")
    o.append(int_array("V_QLOG_SEQ", "int64", [r["seq"] for r in recs]))
    o.append(int_array("V_QLOG_TAG", "int32", [r["tag"] for r in recs]))
    o.append(int_array("V_QLOG_REASON", "int32", [r["reason"] for r in recs]))
    o.append(int_array("V_QLOG_SUBMITTER", "int64", [r["submitter"] for r in recs]))
    o.append(int_array("V_QLOG_SUBJECT", "int8", [x for r in recs for x in bytes_of(r["subject"])], ["32", "V_QLOG_N"]))
    o.append(str_array("V_QLOG_PREV", [r["prev"] for r in recs]))
    o.append(str_array("V_QLOG_HASH", [r["hash"] for r in recs]))
    o.append(f"  character(len=64), parameter :: V_QLOG_HEAD = '{ql['head']}'\n")
    o.append("\nend module conformance_vectors\n")
    text = "".join(o)
    with open(dst, "w", encoding="utf-8") as f:
        f.write(text)
    return 0


if __name__ == "__main__":
    if len(sys.argv) != 3:
        print(__doc__)
        sys.exit(2)
    sys.exit(main(sys.argv[1], sys.argv[2]))
