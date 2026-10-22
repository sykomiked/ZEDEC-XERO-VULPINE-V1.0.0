#!/usr/bin/env python3
# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
"""How far each built app reaches through six layers, read from its manifest and model.

A built app is a manifest with a slot (kernel/src/apps/*.zxapp, as app_gate.py
gates them); the shared blocks (blocks/*.zxblock) are not apps. Each layer is
T when the app's own code shows it and N when it does not (kernel/src/lpres's
TRUE and NEITHER; nothing here says FALSE or BOTH). The daemon methods a model
calls are the string literals in its C (comments left out) that
api/zxvd.openrpc.json lists, whether passed to zxm_begin directly or through a
helper, and swarm.ask for each zxm_ai_ask.

  data       a provided entry runs in MYRA (a logic myra line), or the model
             calls files.* (the Hoard).
  logic      a provided entry is witnessed in two languages or more.
  security   the manifest has a needs line and the model gates a
             state-changing call on the owner: in one function, a ZXM_E_STATE
             refusal, or a parameter named for the owner or a confirmation,
             comes before the first call that changes state (a method whose
             verb is not a read, or a function of the same file that makes
             one); or the app uses gatehouse, lockbox or pavise.
  network    the model calls net.*, peer.*, devmesh.*, web4.*, social.*,
             chat.*, ehop.*, irc.*, hall.* or pspace.* (place.* is local).
  interface  the model the model line names exists, its header declares the
             state struct (<name>_t), a view line's view includes that header,
             and the manifest has an icon line.
  ai         the model calls swarm.*, model.*, aipi.*, play.* or xlate.*, or
             the app uses a pce_* block.

Prints one line per app and the totals; --json writes them; --write writes
docs/APP_LAYERS.md and --check fails when it is stale (test_zxapp.sh runs it,
so every app a commit adds or changes is measured in that commit);
--self-test checks each rule both ways on a made-up app.
"""
import argparse
import json
import os
import re
import sys
import tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import app_gate  # noqa: E402  (its manifest parser, so both read a manifest alike)

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
SRC = os.path.join(ROOT, "kernel", "src")
APPS = os.path.join(SRC, "apps")
SPEC = os.path.join(ROOT, "api", "zxvd.openrpc.json")
DOC = os.path.join(ROOT, "docs", "APP_LAYERS.md")

LAYERS = ("data", "logic", "security", "network", "interface", "ai")
NETWORK = ("net", "peer", "devmesh", "web4", "social", "chat", "ehop", "irc", "hall", "pspace")
AI = ("swarm", "model", "aipi", "play", "xlate")
GUARDS = ("gatehouse", "lockbox", "pavise")
# Verbs that only read or compute: a call to any other verb changes state.
READS = {"list", "read", "stat", "root", "status", "get", "feed", "query", "verify", "check",
         "info", "tail", "balance", "entries", "accounts", "holds", "peers", "offers",
         "devices", "me", "thread", "members", "events", "groups", "catalogue", "languages",
         "detect", "translate", "card_verify", "agent_card", "cid", "envelope", "open", "here",
         "apps", "version", "discover", "earnings", "state", "mood", "dna", "generation",
         "versions", "bands", "ask", "faraid", "report", "manifest", "eval", "epoch",
         "account", "books", "disk", "net", "budget", "top", "job", "render", "decode",
         "encode", "card", "find", "quote", "draft", "catalog", "keys", "issuer", "positions",
         "receipts", "plan", "segment", "iso8583_parse", "check_pan"}
READ_METHODS = {"play.run", "aipi.execute"}  # sandboxed compute: nothing of the owner's moves

LIT = re.compile(r'"(?:\\.|[^"\\\n])*"|\'(?:\\.|[^\'\\\n])*\'|/\*.*?\*/|//[^\n]*', re.S)
STR = re.compile(r'"(?:\\.|[^"\\\n])*"|\'(?:\\.|[^\'\\\n])*\'')
SIG = re.compile(r"(\w+)\s*\(([^()]*)\)\s*$", re.S)
OWNER = re.compile(r"owner|confirm", re.I)


def strip_comments(text):
    """C with its comments blanked and its strings kept, so offsets stay the same."""
    def keep(m):
        s = m.group()
        return s if s[0] in "\"'" else re.sub(r"[^\n]", " ", s)
    return LIT.sub(keep, text)


def functions(code):
    """(name, params, body, body offset) for each top-level function of C code."""
    out, depth, start, sig_from = [], 0, 0, 0
    shape = STR.sub(lambda m: "_" * len(m.group()), code)  # a brace in a string is no brace
    for i, ch in enumerate(shape):
        if ch == "{":
            if depth == 0:
                start = i
            depth += 1
        elif ch == "}" and depth:
            depth -= 1
            if depth == 0:
                m = SIG.search(code[sig_from:start])
                if m:
                    out.append((m.group(1), m.group(2), code[start + 1:i], start + 1))
                sig_from = i + 1
        elif ch == ";" and depth == 0:
            sig_from = i + 1
    return out


def methods_in(code, known):
    """Every daemon method a model's code names, in order of first use."""
    found = []
    for s in re.findall(r'"((?:\\.|[^"\\\n])*)"', code):
        if s in known and s not in found:
            found.append(s)
    if re.search(r"\bzxm_ai_ask\s*\(", code) and "swarm.ask" not in found:
        found.append("swarm.ask")
    return found


def changes_state(meth):
    return meth not in READ_METHODS and meth.split(".")[-1] not in READS


def gated(code, known):
    """The first function that refuses (ZXM_E_STATE) or asks the owner before it changes state."""
    funcs = functions(code)
    lit = re.compile(r'"((?:\\.|[^"\\\n])*)"')

    def first_write(body, writers):
        pos = [m.start() for m in lit.finditer(body)
               if m.group(1) in known and changes_state(m.group(1))]
        pos += [m.start() for m in re.finditer(r"\b(\w+)\s*\(", body) if m.group(1) in writers]
        return min(pos) if pos else -1

    writers = set()
    while True:  # a function writes when it calls a method or a function that does
        more = {n for n, _, b, _ in funcs if n not in writers and first_write(b, writers) >= 0}
        if not more:
            break
        writers |= more
    for name, params, body, _ in funcs:
        w = first_write(body, writers)
        if w < 0:
            continue
        refuse = [m.start() for m in re.finditer(r"\bZXM_E_STATE\b", body)]
        for p in re.findall(r"(\w+)\s*(?:,|$)", params.strip()):
            if OWNER.search(p):
                refuse += [m.start() for m in re.finditer(r"\b%s\b" % re.escape(p), body)]
        if refuse and min(refuse) < w:
            return name
    return None


def langs_of(path):
    """entry -> the languages its logic lines run it in (inline logic counted too)."""
    out, inline = {}, False
    for line in open(path, encoding="utf-8"):
        if inline:
            inline = line.strip() != "."
            continue
        w = line.split("#", 1)[0].split()
        if len(w) >= 4 and w[0] == "logic":
            out.setdefault(w[2], set()).add(w[1])
            inline = w[3] == "<<"
    return out


def measure(path, src, known):
    """id, {layer: (T|N, why)} for one manifest under the source root src."""
    m = app_gate.manifest(path)
    text = open(path, encoding="utf-8").read()
    words = [line.split("#", 1)[0].split() for line in text.splitlines()]
    needs = [w[1] for w in words if len(w) >= 2 and w[0] == "needs"]
    icon = next((w[1] for w in words if len(w) >= 2 and w[0] == "icon"), "")
    view = next((w[1] for w in words if len(w) >= 2 and w[0] == "view"), "")
    langs = langs_of(path)
    provided = [e for e in m["provides"] if e in langs]
    model = os.path.join(src, m["model"]) if m["model"] else ""
    code = strip_comments(open(model, encoding="utf-8").read()) if model and os.path.exists(model) else ""
    meths = methods_in(code, known)
    r = {}

    myra = [e for e in provided if "myra" in langs[e]]
    files = [x for x in meths if x.startswith("files.")]
    r["data"] = ("T", f"myra {myra[0]}") if myra else ("T", files[0]) if files else \
        ("N", "no MYRA record entry, no files.* call")

    multi = [e for e in provided if len(langs[e]) >= 2]
    r["logic"] = ("T", f"{multi[0]} in {len(langs[multi[0]])} languages") if multi else \
        ("N", "no entry witnessed in two languages")

    guard = [u for u in m["uses"] if u in GUARDS]
    gate = gated(code, set(known)) if needs and code else None
    r["security"] = ("T", f"use {guard[0]}") if guard else \
        ("T", f"needs {needs[0]}, {gate} gated") if gate else \
        ("N", "no needs line" if not needs else "no owner-gated state change")

    net = [x for x in meths if x.split(".")[0] in NETWORK]
    r["network"] = ("T", net[0]) if net else ("N", "no network method")

    why = ""
    if not code:
        why = "no model"
    else:
        base = os.path.basename(model)[:-len("_model.c")] if model.endswith("_model.c") else \
            os.path.basename(model)[:-2]
        hdr = model[:-2] + ".h"
        if not os.path.exists(hdr) or not re.search(r"\}\s*%s_t\s*;" % re.escape(base),
                                                    open(hdr, encoding="utf-8").read()):
            why = f"no {base}_t state struct"
        elif view:
            vc = os.path.join(src, view)
            texts = [open(p, encoding="utf-8").read() for p in (vc, vc[:-2] + ".h")
                     if os.path.exists(p)]
            if not any(f'#include "{os.path.basename(hdr)}"' in t for t in texts):
                why = f"view {view} does not include {os.path.basename(hdr)}"
        if not why and not icon:
            why = "no icon line"
    r["interface"] = ("N", why) if why else ("T", f"{os.path.basename(model)}, {icon}")

    ai = [x for x in meths if x.split(".")[0] in AI]
    pce = [u for u in m["uses"] if u.startswith("pce_")]
    r["ai"] = ("T", ai[0]) if ai else ("T", f"use {pce[0]}") if pce else ("N", "no AI method or pce block")
    return m["id"], r


def known_methods(spec=SPEC):
    with open(spec, encoding="utf-8") as f:
        return [x["name"] for x in json.load(f)["methods"]]


def measure_all(apps=APPS, src=SRC, spec=SPEC):
    known = known_methods(spec)
    out = {}
    for f in sorted(os.listdir(apps)):
        p = os.path.join(apps, f)
        if f.endswith(".zxapp") and app_gate.manifest(p)["slot"]:
            i, r = measure(p, src, known)
            out[i] = r
    return out


def totals(res):
    t = {k: sum(1 for r in res.values() if r[k][0] == "T") for k in LAYERS}
    return t, sum(1 for r in res.values() if all(r[k][0] == "T" for k in LAYERS))


def lines(res):
    t, six = totals(res)
    out = [f"{i} " + " ".join(res[i][k][0] for k in LAYERS) for i in sorted(res)]
    out.append("total " + " ".join(f"{k}={t[k]}" for k in LAYERS))
    out.append(f"all six: {six} of {len(res)}")
    return out


def render(res):
    t, six = totals(res)
    md = ["<!--", "Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC",
          "SPDX-License-Identifier: Apache-2.0", "-->", "",
          "# App layers", "",
          "Generated by `tools/apps/app_layers.py` (do not edit; `--check` fails when stale). "
          "Each built app (a manifest with a slot) is measured through six layers from its "
          "manifest and its model's C: T when the code shows the layer, N when it does not. "
          "The rules are in the tool's docstring.", "",
          f"- **Apps measured:** {len(res)}",
          f"- **T on all six:** {six}", ""]
    md += ["| layer | T | N |", "|---|---|---|"]
    md += [f"| {k} | {t[k]} | {len(res) - t[k]} |" for k in LAYERS]
    md += ["", "## Every app", "", "| app | " + " | ".join(LAYERS) + " |",
           "|---|" + "---|" * len(LAYERS)]
    for i in sorted(res):
        md.append(f"| {i} | " + " | ".join(res[i][k][0] for k in LAYERS) + " |")
    md += ["", "## Missing, by layer", ""]
    for k in LAYERS:
        miss = sorted(i for i in res if res[i][k][0] == "N")
        md.append(f"- **{k}** ({len(miss)}): " + (", ".join(miss) if miss else "none"))
    return "\n".join(md) + "\n"


def as_json(res):
    t, six = totals(res)
    return {"apps": {i: {k: {"state": v[0], "why": v[1]} for k, v in r.items()}
                     for i, r in res.items()},
            "totals": t, "all_six": six, "count": len(res)}


def self_test():
    """Each rule both ways on a made-up app; returns the failures."""
    bad = []
    with tempfile.TemporaryDirectory() as d:
        src = os.path.join(d, "src")
        apps = os.path.join(src, "apps")
        os.makedirs(os.path.join(apps, "blocks"))
        spec = os.path.join(d, "spec.json")
        meths = ["files.write", "files.read", "web4.post", "place.add", "swarm.ask", "ledger.post",
                 "net.peers"]
        with open(spec, "w", encoding="utf-8") as f:
            json.dump({"methods": [{"name": x} for x in meths]}, f)
        man = ("id fake\nslot 999\nicon icons/fake.svg\nneeds fs.write\n"
               "model apps/fake_model.c\n"
               "provides keep truth a\nlogic myra keep apps/blocks/fake/keep.myra\n"
               "provides two num a\nlogic eve two apps/blocks/fake/two.eve\n"
               "logic kron two apps/blocks/fake/two.kron\n")
        hdr = "typedef struct {\n    int n;\n} fake_t;\n"
        model = ("/* a comment naming \"net.peers\" is not a call */\n"
                 "static void send(fake_t *f, const char *m) { zxm_begin(f->link, m); }\n"
                 "int fake_keep(fake_t *f, bool owner_confirmed)\n{\n"
                 "    if (!owner_confirmed) return ZXM_E_STATE;\n"
                 "    send(f, \"files.write\");\n    return 0;\n}\n"
                 "int fake_post(fake_t *f) { zxm_begin(f->link, \"web4.post\"); return 0; }\n"
                 "int fake_ask(fake_t *f) { zxm_begin(f->link, \"swarm.ask\"); return 0; }\n")

        def run(m_text=man, c_text=model, h_text=hdr):
            for name, t in (("fake.zxapp", m_text), ("fake_model.c", c_text),
                            ("fake_model.h", h_text)):
                with open(os.path.join(apps, name), "w", encoding="utf-8") as f:
                    f.write(t)
            with open(os.path.join(apps, "blocks", "shared.zxblock"), "w", encoding="utf-8") as f:
                f.write("id shared\nslot 1\n")
            r = measure_all(apps, src, spec)
            return {k: v[0] for k, v in r["fake"].items()}, sorted(r)

        def want(label, got, layer, state):
            if got[layer] != state:
                bad.append(f"{label}: {layer} is {got[layer]}, want {state}")

        got, ids = run()
        if ids != ["fake"]:
            bad.append(f"apps measured {ids}, want only fake (a block is no app)")
        for k in LAYERS:
            want("whole app", got, k, "T")
        no_myra = man.replace("logic myra keep", "logic eve keep")
        want("no myra, files.write", run(no_myra)[0], "data", "T")
        want("no myra, no files", run(no_myra, model.replace('"files.write"', '"ledger.post"'))[0],
             "data", "N")
        want("one language each", run(man.replace("logic kron two", "logic eve two"))[0],
             "logic", "N")
        want("no needs", run(man.replace("needs fs.write\n", ""))[0], "security", "N")
        ungated = model.replace("    if (!owner_confirmed) return ZXM_E_STATE;\n", "")
        want("write not gated", run(c_text=ungated)[0], "security", "N")
        late = model.replace("    if (!owner_confirmed) return ZXM_E_STATE;\n    send(f, \"files.write\");\n",
                             "    send(f, \"files.write\");\n    if (!owner_confirmed) return ZXM_E_STATE;\n")
        want("refusal after the write", run(c_text=late)[0], "security", "N")
        refuse = ungated.replace("    send(f,", "    if (f->n) return ZXM_E_STATE;\n    send(f,")
        want("ZXM_E_STATE refusal", run(c_text=refuse)[0], "security", "T")
        want("use gatehouse", run(man.replace("needs fs.write\n", "use gatehouse 1\n"), ungated)[0],
             "security", "T")
        local = model.replace('"web4.post"', '"place.add"')
        want("place.* only", run(c_text=local)[0], "network", "N")
        want("net.peers in a comment only", run(c_text=local)[0], "network", "N")
        want("no model file", run(c_text="")[0], "interface", "N")
        want("no state struct", run(h_text="typedef int other_t;\n")[0], "interface", "N")
        want("no icon", run(man.replace("icon icons/fake.svg\n", ""))[0], "interface", "N")
        want("view without the header", run(man + "view zui/views/fake.c\n")[0], "interface", "N")
        noai = model.replace('"swarm.ask"', '"files.read"')
        want("no AI method", run(c_text=noai)[0], "ai", "N")
        want("zxm_ai_ask", run(c_text=noai.replace("int fake_ask(fake_t *f) {",
                                                   "int fake_ask(fake_t *f) { zxm_ai_ask(0);"))[0],
             "ai", "T")
        want("use pce_prob", run(man + "use pce_prob 1\n", noai)[0], "ai", "T")
    return bad


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--check", action="store_true", help="fail when docs/APP_LAYERS.md is stale")
    ap.add_argument("--write", action="store_true", help="write docs/APP_LAYERS.md")
    ap.add_argument("--json", help="also write the measurements here")
    ap.add_argument("--self-test", action="store_true", help="check each rule on a made-up app")
    a = ap.parse_args()
    if a.self_test:
        bad = self_test()
        for b in bad:
            print(f"[FAIL] app_layers self-test: {b}")
        if bad:
            return 1
        print("[PASS] app_layers self-test: every rule, both ways")
        return 0
    res = measure_all()
    md = render(res)
    if a.json:
        with open(a.json, "w", encoding="utf-8") as f:
            json.dump(as_json(res), f, indent=1, sort_keys=True)
    t, six = totals(res)
    if a.check:
        if not os.path.exists(DOC) or open(DOC, encoding="utf-8").read() != md:
            print(f"[FAIL] {os.path.relpath(DOC, ROOT)} is stale: "
                  "python3 tools/apps/app_layers.py --write")
            return 1
        print(f"[PASS] app layers: {six} of {len(res)} apps T on all six")
        return 0
    if a.write:
        with open(DOC, "w", encoding="utf-8") as f:
            f.write(md)
    print("\n".join(lines(res)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
