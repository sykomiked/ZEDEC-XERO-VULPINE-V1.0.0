#!/usr/bin/env python3
# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: LicenseRef-OPL-1.1 AND CC-BY-SA-4.0 AND LicenseRef-Royal-Writ-Sicilian-Crown-1.0 AND LicenseRef-SEL-3.3
#
# game_master.py — the Game Master QC + OS-observation harness for the ZEDEC
# pqOS event-space kernel.
#
# PURPOSE (two-fold):
#   1. QC stress: boot the kernel with each legacy ROM attached as a virtio-blk
#      dataset (so the data flows through the real storage/VFS/event pipeline),
#      drive a reproducible input storm, and log every kernel [FAULT] with full
#      context (ESR/FAR/ELR -> function). The fault log names where to debug.
#   2. OS observation: run the corpus in BOTH polarities of the computing stack.
#      Positive space (S+) = the raw dataset. Negative space (S-) = the dataset's
#      arithmetic negative image (bytes XOR 0xFF) — the same information in the
#      opposite polarity, exercising the S- side of the stack (dual_space's
#      DS_RESOLUTION_S_MINUS concept). Comparing OS behavior across the two
#      cohorts (fault rate, survival, output) is the R&D signal.
#
# It runs many ROMs in PARALLEL (bounded pool), STRATIFIED evenly across every
# console ecosystem, and AUTO-GENERATES logs: a JSONL record per run, a full
# serial dump per fault, and a rolling human-readable summary. Resumable: runs
# already recorded (PASS or FAULT) are skipped.
#
# NOTE: this harness pushes ROM DATASETS through the kernel to stress/observe it
# and localize faults. It does NOT emulate the consoles; the in-kernel unified-
# emulation layer (6502 + Z80 cores so far) is a separate, growing build.
#
#   python3 build_system/game_master.py \
#       --roms /abs/path/to/GAMEMASTER --stratify 33000 --polarity split \
#       --n 8 --secs 3 --maxmb 8 --logdir /abs/path/to/logs
import argparse, os, sys, json, time, socket, subprocess, signal, hashlib, math, threading, queue, tempfile, re, shutil
from collections import Counter

_NEG = bytes(0xFF ^ i for i in range(256))   # byte-complement table for negative space

def nm_symbolizer(elf):
    """Return f(addr)->'func (+0xNN)' using the kernel ELF symbol table."""
    syms = []
    try:
        out = subprocess.run(["aarch64-linux-gnu-nm", "-n", elf], capture_output=True, text=True).stdout
        for line in out.splitlines():
            p = line.split()
            if len(p) >= 3:
                try: syms.append((int(p[0], 16), p[2]))
                except ValueError: pass
    except Exception:
        pass
    def sym(addr):
        best = None
        for a, n in syms:
            if a <= addr: best = (a, n)
            else: break
        return f"{best[1]} (+0x{addr-best[0]:x})" if best else "?"
    return sym

def seeded_storm(rom_path):
    """Deterministic input-storm seed derived from the ROM path hash."""
    return int.from_bytes(hashlib.sha256(rom_path.encode()).digest()[:8], "big")

def sanitize(s):
    return re.sub(r"[^A-Za-z0-9._-]+", "_", s)[:80]

def run_one(rom, system, polarity, kernel, elf, secs, symbolize, logdir, maxmb):
    """Boot the kernel with the ROM as a virtio-blk dataset (in the given
    polarity) + a seeded input storm; capture serial; classify the outcome."""
    tag = hashlib.sha1((rom + "|" + polarity).encode()).hexdigest()[:12]
    ser = f"/tmp/gm_{tag}_s.sock"; qmp = f"/tmp/gm_{tag}_q.sock"
    for p in (ser, qmp):
        try: os.unlink(p)
        except OSError: pass
    rec = {"rom": os.path.basename(rom), "system": system, "path": rom,
           "polarity": polarity,
           "size": os.path.getsize(rom) if os.path.exists(rom) else 0,
           "status": "PASS", "survived_s": 0.0, "fault": None}

    # Negative space (S-): materialise the dataset's negative image into a temp
    # file and attach THAT. Positive space (S+): attach the raw ROM.
    use_path = rom; tmpneg = None
    if polarity == "S-":
        try:
            nread = (maxmb * 1024 * 1024 + 1) if maxmb > 0 else -1   # -1 = whole file
            with open(rom, "rb") as fh: data = fh.read(nread)
            fd, tmpneg = tempfile.mkstemp(prefix="gmneg_", suffix=".bin")
            with os.fdopen(fd, "wb") as tf: tf.write(data.translate(_NEG))
            use_path = tmpneg
        except Exception:
            rec["status"] = "PREP_FAIL"; return rec

    # QEMU uses ',' as the -drive option separator; No-Intro names contain
    # "(USA, Europe)" so literal commas must be doubled or QEMU truncates+exits.
    drive_q = use_path.replace(",", ",,")
    q = subprocess.Popen(
        ["qemu-system-aarch64", "-M", "virt,gic-version=3", "-cpu", "cortex-a53", "-m", "512",
         "-global", "virtio-mmio.force-legacy=false", "-device", "ramfb",
         "-device", "virtio-tablet-device", "-device", "virtio-keyboard-device",
         "-drive", f"if=none,file={drive_q},format=raw,id=gm0,readonly=on",
         "-device", "virtio-blk-device,drive=gm0",
         "-display", "none",
         "-chardev", f"socket,id=s0,path={ser},server=on,wait=off", "-serial", "chardev:s0",
         "-qmp", f"unix:{qmp},server,nowait", "-kernel", kernel],
        stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    buf = bytearray()
    try:
        for _ in range(200):
            if q.poll() is not None: rec["status"] = "QEMU_FAIL"; return rec
            if os.path.exists(ser) and os.path.exists(qmp): break
            time.sleep(0.05)
        time.sleep(0.3)
        s = socket.socket(socket.AF_UNIX); s.connect(ser); s.setblocking(False)
        qm = socket.socket(socket.AF_UNIX); qm.connect(qmp); qf = qm.makefile("rw"); qf.readline()
        def qc(c, **a):
            o = {"execute": c}
            if a: o["arguments"] = a
            qf.write(json.dumps(o) + "\n"); qf.flush()
            while True:
                m = json.loads(qf.readline())
                if "return" in m or "error" in m: return m
        qc("qmp_capabilities")
        def pump():
            try: buf.extend(s.recv(65536))
            except OSError: pass
        t0 = time.time()
        while time.time() - t0 < 8: pump(); time.sleep(0.1)   # boot window
        seed = seeded_storm(rom)
        i = 0; t1 = time.time()
        while time.time() - t1 < secs:
            i += 1; r = (seed >> (i % 40)) & 0xffff
            x = 176 + int(600 * (0.5 + 0.5 * math.sin(i * 0.3 + (r & 7))))
            y = 70 + int(560 * (0.5 + 0.5 * math.cos(i * 0.37 + (r & 15))))
            try:
                qc("input-send-event", events=[
                    {"type": "abs", "data": {"axis": "x", "value": int(x * 32767 / 1280)}},
                    {"type": "abs", "data": {"axis": "y", "value": int(y * 32767 / 720)}}])
                if i % 3 == 0: s.sendall(b"ls\r" if (r & 1) else b"AAAA\r")
                if i % 7 == 0:
                    qc("input-send-event", events=[{"type": "btn", "data": {"down": True, "button": "left"}}])
                    qc("input-send-event", events=[{"type": "btn", "data": {"down": False, "button": "left"}}])
            except OSError: pass
            pump()
            if b"[FAULT]" in bytes(buf) or b"Halting" in bytes(buf): break
            time.sleep(0.02)
        rec["survived_s"] = round(time.time() - t1, 1)
        time.sleep(0.3); pump()
        txt = bytes(buf).decode("latin1")
        if "[FAULT]" in txt or "Halting" in txt:
            rec["status"] = "FAULT"
            far = re.search(r"FAR_EL1=0x([0-9A-Fa-f]+)", txt)
            elr = re.search(r"ELR_EL1=0x([0-9A-Fa-f]+)", txt)
            esr = re.search(r"ESR_EL1=0x([0-9A-Fa-f]+)", txt)
            rec["fault"] = {
                "esr": esr.group(1) if esr else None,
                "far": far.group(1) if far else None,
                "elr": elr.group(1) if elr else None,
                "elr_sym": symbolize(int(elr.group(1), 16)) if elr else None,
            }
            # AUTO-LOG: full serial capture for every fault, for R&D.
            try:
                fn = os.path.join(logdir, f"fault_{sanitize(system)}_{polarity}_{tag}.log")
                with open(fn, "w") as ff:
                    ff.write(f"# ROM: {rom}\n# system: {system}\n# polarity: {polarity}\n")
                    ff.write(f"# fault: {json.dumps(rec['fault'])}\n\n{txt}")
                rec["serial_log"] = fn
            except Exception: pass
    finally:
        try: q.send_signal(signal.SIGTERM); q.wait(3)
        except Exception:
            try: q.kill()
            except Exception: pass
        if tmpneg:
            try: os.unlink(tmpneg)
            except OSError: pass
    return rec

def stratified(cor, n, maxmb):
    """Pick ~n ROMs spread evenly across every ecosystem (round-robin)."""
    systems = sorted(d for d in os.listdir(cor) if os.path.isdir(os.path.join(cor, d)))
    cap = maxmb * 1024 * 1024 if maxmb > 0 else float("inf")
    per = {}
    for sname in systems:
        files = []
        for root, _, fs in os.walk(os.path.join(cor, sname)):
            for fn in sorted(fs):
                if fn.startswith("."): continue
                fp = os.path.join(root, fn)
                try:
                    if os.path.getsize(fp) <= cap: files.append(fp)
                except OSError: pass
        per[sname] = files
    out, idx = [], {s: 0 for s in systems}
    while len(out) < n:
        progressed = False
        for sname in systems:
            if idx[sname] < len(per[sname]):
                out.append((per[sname][idx[sname]], sname)); idx[sname] += 1; progressed = True
                if len(out) >= n: break
        if not progressed: break
    return out, systems

def assign_polarity(roms, mode):
    """Return [(path, system, polarity)]. 'split' balances S+/S- within each
    system so a polarity comparison isn't confounded by ecosystem."""
    out, ctr = [], {}
    for path, sysn in roms:
        if mode == "positive":   pol = "S+"
        elif mode == "negative": pol = "S-"
        else:
            c = ctr.get(sysn, 0); ctr[sysn] = c + 1
            pol = "S+" if (c % 2 == 0) else "S-"
        out.append((path, sysn, pol))
    return out

def write_summary(path, results, meta):
    n = len(results)
    st = Counter(r["status"] for r in results)
    pol = Counter((r.get("polarity"), r["status"]) for r in results)
    sysflt = Counter(r["system"] for r in results if r["status"] == "FAULT")
    hot = Counter((r.get("fault") or {}).get("elr_sym")
                  for r in results if r["status"] == "FAULT" and r.get("fault"))
    L = []
    L.append("ZEDEC pqOS — GAME MASTER campaign summary")
    L.append(f"generated: {time.strftime('%Y-%m-%d %H:%M:%S')}")
    L.append(f"corpus: {meta['roms']}   stratify={meta['stratify']} polarity={meta['polarity']} "
             f"secs/rom={meta['secs']} parallel={meta['n']} maxmb={meta['maxmb']}")
    L.append(f"kernel sha256[:16]: {meta.get('kbin','?')}  (binary+elf preserved in this logdir)")
    L.append("")
    L.append(f"RUNS: {n}")
    for k, v in st.most_common(): L.append(f"  {k}: {v}")
    L.append("")
    L.append("KERNEL FAULTS (the number that matters): %d" % st.get("FAULT", 0))
    L.append("")
    L.append("BY POLARITY (positive S+ vs negative S-):")
    for p in ("S+", "S-"):
        row = {s: c for (pp, s), c in pol.items() if pp == p}
        tot = sum(row.values())
        L.append(f"  {p}: total={tot} PASS={row.get('PASS',0)} FAULT={row.get('FAULT',0)} "
                 f"QEMU_FAIL={row.get('QEMU_FAIL',0)} other={tot-row.get('PASS',0)-row.get('FAULT',0)-row.get('QEMU_FAIL',0)}")
    L.append("")
    if sysflt:
        L.append("SYSTEMS WITH FAULTS:")
        for sname, c in sysflt.most_common(): L.append(f"  {c:4}x  {sname}")
        L.append("")
        L.append("FAULT HOTSPOTS (ELR -> count):")
        for sym, c in hot.most_common(15): L.append(f"  {c:4}x  {sym}")
    else:
        L.append("SYSTEMS WITH FAULTS: none")
    L.append("")
    try:
        with open(path, "w") as f: f.write("\n".join(L) + "\n")
    except Exception: pass

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--roms", default="", help="ROM corpus dir (recursively scanned)")
    ap.add_argument("--kernel", default="kernel_arm64.bin")
    ap.add_argument("--elf", default="kernel_arm64.elf")
    ap.add_argument("--n", type=int, default=8, help="parallel kernel instances")
    ap.add_argument("--secs", type=int, default=3, help="stress seconds per ROM")
    ap.add_argument("--stratify", type=int, default=0, help="pick ~N ROMs spread across all ecosystems (0=full walk)")
    ap.add_argument("--polarity", choices=["split", "positive", "negative"], default="split",
                    help="S+/S- assignment: split=~50/50 per system")
    ap.add_argument("--maxmb", type=int, default=8, help="skip ROMs larger than this (MB)")
    ap.add_argument("--limit", type=int, default=0, help="max ROMs after selection (0=all)")
    ap.add_argument("--sample", default="", help="newline-separated explicit ROM paths (targeted repro)")
    ap.add_argument("--logdir", default="", help="auto-log dir (JSONL + per-fault serial + summary)")
    a = ap.parse_args()

    if not a.sample and (not a.roms or not os.path.isdir(a.roms)):
        print(f"[game master] ERROR: --roms dir not found: {a.roms!r}"); sys.exit(2)
    symbolize = nm_symbolizer(a.elf)
    logdir = a.logdir or os.path.join(os.getcwd(), "gamemaster_logs_" + time.strftime("%Y%m%d_%H%M%S"))
    os.makedirs(logdir, exist_ok=True)
    logpath = os.path.join(logdir, "runs.jsonl")
    summ = os.path.join(logdir, "summary.txt")

    # Preserve the EXACT kernel binary + ELF used, so any fault's ELR can always
    # be mapped back to source even after the tree is rebuilt. Without this a
    # fault's "func +0xNN" becomes unmappable once kernel_arm64.elf changes.
    kbin_hash = "?"
    try:
        with open(a.kernel, "rb") as fh: kbin_hash = hashlib.sha256(fh.read()).hexdigest()[:16]
        for src in (a.kernel, a.elf):
            dst = os.path.join(logdir, os.path.basename(src))
            if os.path.exists(src) and not os.path.exists(dst): shutil.copy2(src, dst)
        # symbolize against the PRESERVED elf copy so it survives rebuilds
        elf_copy = os.path.join(logdir, os.path.basename(a.elf))
        if os.path.exists(elf_copy): symbolize = nm_symbolizer(elf_copy)
    except Exception as e:
        print(f"[game master] WARN: could not preserve kernel build: {e}")

    if a.sample:
        roms = [(p, os.path.basename(os.path.dirname(p))) for p in a.sample.splitlines() if p.strip()]
    elif a.stratify:
        roms, systems = stratified(a.roms, a.stratify, a.maxmb)
    else:
        roms = []
        cap = a.maxmb * 1024 * 1024 if a.maxmb > 0 else float("inf")
        for root, _, files in os.walk(a.roms):
            sysname = os.path.basename(root)
            for fn in files:
                if fn.startswith("."): continue
                fp = os.path.join(root, fn)
                try:
                    if os.path.getsize(fp) <= cap: roms.append((fp, sysname))
                except OSError: pass
        roms.sort()
    if a.limit: roms = roms[:a.limit]
    plan = assign_polarity(roms, a.polarity)

    done = set()
    if os.path.exists(logpath):
        for line in open(logpath):
            try:
                r = json.loads(line)
                if r.get("status") in ("PASS", "FAULT"):
                    done.add(r["path"] + "|" + r.get("polarity", "S+"))
            except Exception: pass
    todo = [t for t in plan if (t[0] + "|" + t[2]) not in done]
    meta = {"roms": a.roms, "stratify": a.stratify, "polarity": a.polarity,
            "secs": a.secs, "n": a.n, "maxmb": a.maxmb, "kbin": kbin_hash}
    print(f"[game master] logdir: {logdir}   kernel sha256[:16]={kbin_hash} (binary+elf preserved here)")
    print(f"[game master] selected={len(plan)} (S+={sum(1 for t in plan if t[2]=='S+')} "
          f"S-={sum(1 for t in plan if t[2]=='S-')})  to-run={len(todo)} "
          f"(skipped {len(plan)-len(todo)} already done)  parallel={a.n} secs/rom={a.secs}")
    print(f"[game master] live log: {logpath}   summary: {summ}")

    work = queue.Queue()
    for t in todo: work.put(t)
    results, lock = [], threading.Lock()
    logf = open(logpath, "a")
    ctr = {"done": 0}
    def worker():
        while True:
            try: rom, system, pol = work.get_nowait()
            except queue.Empty: return
            rec = run_one(rom, system, pol, a.kernel, a.elf, a.secs, symbolize, logdir, a.maxmb)
            with lock:
                results.append(rec); logf.write(json.dumps(rec) + "\n"); logf.flush()
                ctr["done"] += 1
                mark = "PASS " if rec["status"] == "PASS" else f"**{rec['status']}**"
                extra = ""
                if rec["fault"]: extra = f"  ELR={rec['fault']['elr_sym']} FAR=0x{rec['fault']['far']}"
                print(f"  [{ctr['done']:6}/{len(todo)}] [{rec['polarity']}] [{mark:10}] "
                      f"{rec['system'][:22]:22} {rec['rom'][:38]:38} {rec['survived_s']}s{extra}")
                if ctr["done"] % 200 == 0: write_summary(summ, results, meta)   # rolling summary
            work.task_done()
    threads = [threading.Thread(target=worker) for _ in range(a.n)]
    for t in threads: t.start()
    for t in threads: t.join()
    logf.close()
    write_summary(summ, results, meta)

    n_fault = sum(1 for r in results if r["status"] == "FAULT")
    n_pass = sum(1 for r in results if r["status"] == "PASS")
    print(f"\n[game master] PASS={n_pass}  KERNEL_FAULT={n_fault}  other={len(results)-n_pass-n_fault}")
    print(f"[game master] summary: {summ}")
    print(f"[game master] full JSONL: {logpath}")
    if n_fault:
        print("[game master] fault hotspots (ELR -> count):")
        c = Counter(r["fault"]["elr_sym"] for r in results if r["status"] == "FAULT" and r["fault"])
        for sym, cnt in c.most_common(10): print(f"    {cnt:4}x  {sym}")

if __name__ == "__main__":
    main()
