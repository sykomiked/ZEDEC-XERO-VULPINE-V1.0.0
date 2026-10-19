# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
"""Write sdk/conformance/vectors.json as an Ada package of constants.

The Ada conformance test (test/test_zxv.adb) reads its expected values
from this generated package: every value comes from vectors.json, none is
typed in by hand. The output goes to the build directory. A port of the
Fortran pillar's gen_vectors_f90.py.

usage: python3 tools/gen_vectors_ada.py vectors.json conformance_vectors.ads
"""
import json
import sys

WIDTH = 96


def wrap(items, indent="      "):
    lines, cur = [], ""
    for i, it in enumerate(items):
        piece = it + (", " if i + 1 < len(items) else "")
        if len(cur) + len(piece) > WIDTH:
            lines.append(cur.rstrip())
            cur = ""
        cur += piece
    lines.append(cur.rstrip())
    return ("\n" + indent).join(lines)


def i64(v):
    if v == -(2**63):
        return "Integer_64'First"
    return str(v)


def ada_str(s):
    out = '"'
    for ch in s:
        if ch == '"':
            out += '""'
        elif 32 <= ord(ch) < 127:
            out += ch
        else:
            out += '" & Character\'Val (%d) & "' % ord(ch)
    return out + '"'


class Gen:
    def __init__(self):
        self.o = []
        self.n = 0

    def emit(self, s):
        self.o.append(s)

    def vec(self, name, typ, values, first=1, conv=str):
        """A one-dimensional constant array, index from `first`."""
        if len(values) == 1:
            agg = f"({first} => {conv(values[0])})"
        else:
            agg = "(" + wrap([conv(v) for v in values]) + ")"
        last = first + len(values) - 1
        self.emit(f"   {name} : constant {typ} ({first} .. {last}) :=\n     {agg};\n")

    def table(self, name, typ, rows, first=0, conv=str):
        """A two-dimensional constant array rows x cols, both from `first`/0."""
        cols = len(rows[0])
        aggs = ["(" + wrap([conv(v) for v in r], "       ") + ")" for r in rows]
        self.emit(f"   {name} : constant {typ} ({first} .. {first + len(rows) - 1}, 0 .. {cols - 1}) :=\n"
                  f"     ({(',' + chr(10) + '      ').join(aggs)});\n")

    def texts(self, name, values, first=1):
        refs = []
        for v in values:
            self.n += 1
            self.emit(f"   S{self.n} : aliased constant String := {ada_str(v)};\n")
            refs.append(f"S{self.n}'Access")
        self.vec(name, "Text_Array", refs, first, conv=lambda r: r)


def pairs(items, key):
    return [list(r.get(key, [0, 1])) for r in items]


def main(src, dst):
    with open(src, encoding="utf-8") as f:
        v = json.load(f)
    g = Gen()
    e = g.emit
    e("--  generated from sdk/conformance/vectors.json by tools/gen_vectors_ada.py; do not edit\n")
    e("pragma Style_Checks (Off);\nwith Interfaces; use Interfaces;\n\npackage Conformance_Vectors is\n\n")
    e("   type Text is access constant String;\n")
    e("   type Text_Array is array (Integer range <>) of Text;\n")
    e("   type I32_Array is array (Integer range <>) of Integer_32;\n")
    e("   type I64_Array is array (Integer range <>) of Integer_64;\n")
    e("   type U64_Array is array (Integer range <>) of Unsigned_64;\n")
    e("   type Bool_Array is array (Integer range <>) of Boolean;\n")
    e("   type I32_Table is array (Integer range <>, Integer range <>) of Integer_32;\n")
    e("   type I64_Table is array (Integer range <>, Integer range <>) of Integer_64;\n")
    e("   type U8_Table is array (Integer range <>, Integer range <>) of Unsigned_8;\n\n")

    st = v["status"]
    g.vec("Status_Code", "I32_Array", [s["code"] for s in st])
    g.vec("Status_Class", "I32_Array", [s["class"] for s in st])
    g.texts("Status_Name", [s["name"] for s in st])

    ro = v["rational_ops"]
    for key in ("a", "b", "add", "sub", "mul", "div"):
        g.table(f"Rat_{key.capitalize()}", "I64_Table", pairs(ro, key), first=1, conv=i64)
    g.vec("Rat_Cmp", "I32_Array", [r["cmp"] for r in ro])

    rp = v["rational_parse"]
    g.texts("Parse_Text", [r["text"] for r in rp])
    g.vec("Parse_Status", "I32_Array", [r["status"] for r in rp])
    g.table("Parse_Value", "I64_Table", pairs(rp, "value"), first=1, conv=i64)
    g.texts("Parse_Format", [r.get("format", "") for r in rp])

    ie = v["ieee754"]
    g.vec("IEEE_Bits", "U64_Array", [r["bits"] for r in ie], conv=lambda b: f"16#{b}#")
    g.vec("IEEE_Status", "I32_Array", [r["status"] for r in ie])
    g.table("IEEE_Value", "I64_Table", pairs(ie, "value"), first=1, conv=i64)

    q = v["q16"]
    g.table("Q16_Value", "I64_Table", pairs(q, "value"), first=1, conv=i64)
    g.vec("Q16_Status", "I32_Array", [r["status"] for r in q])
    g.vec("Q16_Q", "I32_Array", [r["q16"] for r in q])

    lp = v["lpres"]
    g.texts("LP_Name", lp["names"], first=0)
    g.vec("LP_Not", "I32_Array", lp["not"], first=0)
    g.table("LP_And", "I32_Table", lp["and"])
    g.table("LP_Or", "I32_Table", lp["or"])

    h = v["hex"]
    g.texts("Hex_Base_Name", h["base_names"], first=0)
    g.texts("Hex_Polarity_Name", h["polarity_names"], first=0)
    g.texts("Hex_Phase_Name", h["phase_names"], first=0)
    g.texts("Hex_Action_Name", h["action_names"], first=0)
    g.vec("Hex_LP_To_Base", "I32_Array", h["lp_to_base"], first=0)
    g.vec("Hex_Base_To_LP", "I32_Array", h["base_to_lp"], first=0)
    g.vec("Hex_Phase_Next", "I32_Array", h["phase_next"], first=0)
    tags = sorted(h["tags"], key=lambda t: t["tag"])
    g.texts("Hex_Name", [t["name"] for t in tags], first=0)
    for key in ("base", "polarity", "phase", "action"):
        g.vec(f"Hex_{key.capitalize()}", "I32_Array", [t[key] for t in tags], first=0)
    g.vec("Hex_Not", "I32_Array", h["not"], first=0)
    g.table("Hex_And", "I32_Table", h["and"])
    g.table("Hex_Or", "I32_Table", h["or"])
    g.table("Hex_Step", "I32_Table", h["step"])
    #  set[tag][base * 4 + polarity]
    g.table("Hex_Set", "I32_Table", h["set"])
    pol = h["polarity"]
    for key in ("pos0", "neg0", "pos1", "neg1"):
        g.vec(f"Pol_{key.capitalize()}", "I64_Array", [p[key] for p in pol])
    g.vec("Pol_Stale", "Bool_Array", [p["stale"] for p in pol], conv=lambda b: "True" if b else "False")
    g.vec("Pol_Result", "I32_Array", [p["polarity"] for p in pol])

    recs = h["qlog"]["records"]
    g.vec("QLog_Seq", "I64_Array", [r["seq"] for r in recs])
    g.vec("QLog_Tag", "I32_Array", [r["tag"] for r in recs])
    g.vec("QLog_Reason", "I32_Array", [r["reason"] for r in recs])
    g.vec("QLog_Submitter", "I64_Array", [r["submitter"] for r in recs])
    g.table("QLog_Subject", "U8_Table", [list(bytes.fromhex(r["subject"])) for r in recs], first=1)
    g.texts("QLog_Prev", [r["prev"] for r in recs])
    g.texts("QLog_Hash", [r["hash"] for r in recs])
    e(f"   QLog_Head : constant String := \"{h['qlog']['head']}\";\n")

    hs = v["hash"]
    g.texts("Hash_Text", [x["utf8"] for x in hs])
    g.texts("Hash_SHA3_256", [x["sha3_256"] for x in hs])

    e("\nend Conformance_Vectors;\n")
    with open(dst, "w", encoding="utf-8") as f:
        f.write("".join(g.o))
    return 0


if __name__ == "__main__":
    if len(sys.argv) != 3:
        print(__doc__)
        sys.exit(2)
    sys.exit(main(sys.argv[1], sys.argv[2]))
