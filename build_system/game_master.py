#!/usr/bin/env python3
# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: LicenseRef-OPL-1.1 AND CC-BY-SA-4.0 AND LicenseRef-Royal-Writ-Sicilian-Crown-1.0 AND LicenseRef-SEL-3.3
#
# game_master.py — the Game Master QC stress harness for the ZEDEC pqOS
# event-space kernel.
#
# It treats a legacy-ROM corpus as a vast DETERMINISTIC DATASET. For each ROM it
# boots the kernel with the ROM's raw bytes attached as a virtio-blk device (so
# the dataset flows through the real storage/VFS/event pipeline) and drives a
# reproducible input storm (seeded by the ROM path) at the event space, then
# watches the serial console. Every kernel fault is logged with full context:
#   ROM, system, size, seconds survived, ESR/FAR/ELR, and ELR -> function.
# Runs many ROMs in PARALLEL (bounded pool). Resumable: ROMs already logged PASS
# are skipped. The fault log is the deliverable — it names where to debug next.
#
#   python3 build_system/game_master.py --roms DIR [--n 6] [--secs 20] [--limit N]
#
# NOTE: this is the harness. It does NOT yet emulate the consoles — it pushes the
# ROM datasets through the kernel to stress it and localize faults. The in-kernel
# unified-emulation layer is a separate, growing build; this harness will drive it
# unchanged as cores are added.
import argparse, os, sys, json, time, socket, subprocess, signal, hashlib, math, threading, queue

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
        lo, hi, best = 0, len(syms), None
        for a, n in syms:
            if a <= addr: best = (a, n)
            else: break
        return f"{best[1]} (+0x{addr-best[0]:x})" if best else "?"
    return sym

def seeded_storm(rom_path, secs):
    """Deterministic input events derived from the ROM path hash."""
    h = hashlib.sha256(rom_path.encode()).digest()
    seed = int.from_bytes(h[:8], "big")
    return seed

def run_one(rom, system, kernel, elf, secs, symbolize):
    """Boot the kernel with ROM as a virtio-blk dataset + a seeded input storm."""
    tag = hashlib.sha1(rom.encode()).hexdigest()[:12]
    ser = f"/tmp/gm_{tag}_s.sock"; qmp = f"/tmp/gm_{tag}_q.sock"
    for p in (ser, qmp):
        try: os.unlink(p)
        except OSError: pass
    q = subprocess.Popen(
        ["qemu-system-aarch64", "-M", "virt,gic-version=3", "-cpu", "cortex-a53", "-m", "512",
         "-global", "virtio-mmio.force-legacy=false", "-device", "ramfb",
         "-device", "virtio-tablet-device", "-device", "virtio-keyboard-device",
         "-drive", f"if=none,file={rom},format=raw,id=gm0,readonly=on",
         "-device", "virtio-blk-device,drive=gm0",
         "-display", "none",
         "-chardev", f"socket,id=s0,path={ser},server=on,wait=off", "-serial", "chardev:s0",
         "-qmp", f"unix:{qmp},server,nowait", "-kernel", kernel],
        stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    rec = {"rom": os.path.basename(rom), "system": system, "path": rom,
           "size": os.path.getsize(rom) if os.path.exists(rom) else 0,
           "status": "PASS", "survived_s": 0.0, "fault": None}
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
        buf = bytearray()
        def pump():
            try: buf.extend(s.recv(65536))
            except OSError: pass
        t0 = time.time()
        while time.time() - t0 < 8: pump(); time.sleep(0.1)   # boot
        seed = seeded_storm(rom, secs)
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
                if i % 7 == 0: qc("input-send-event", events=[{"type": "btn", "data": {"down": True, "button": "left"}}]);\
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
            import re
            far = re.search(r"FAR_EL1=0x([0-9A-Fa-f]+)", txt)
            elr = re.search(r"ELR_EL1=0x([0-9A-Fa-f]+)", txt)
            esr = re.search(r"ESR_EL1=0x([0-9A-Fa-f]+)", txt)
            rec["fault"] = {
                "esr": esr.group(1) if esr else None,
                "far": far.group(1) if far else None,
                "elr": elr.group(1) if elr else None,
                "elr_sym": symbolize(int(elr.group(1), 16)) if elr else None,
            }
    finally:
        try: q.send_signal(signal.SIGTERM); q.wait(3)
        except Exception:
            try: q.kill()
            except Exception: pass
    return rec

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--roms", default="", help="ROM corpus dir (recursively scanned)")
    ap.add_argument("--kernel", default="kernel_arm64.bin")
    ap.add_argument("--elf", default="kernel_arm64.elf")
    ap.add_argument("--n", type=int, default=6, help="parallel kernel instances")
    ap.add_argument("--secs", type=int, default=20, help="stress seconds per ROM")
    ap.add_argument("--limit", type=int, default=0, help="max ROMs (0 = all)")
    ap.add_argument("--log", default="gamemaster_faults.jsonl")
    ap.add_argument("--sample", default="", help="newline-separated explicit ROM paths (demo; filenames may contain commas)")
    a = ap.parse_args()
    symbolize = nm_symbolizer(a.elf)

    if a.sample:
        roms = [(p, os.path.basename(os.path.dirname(p))) for p in a.sample.splitlines() if p.strip()]
    else:
        roms = []
        for root, _, files in os.walk(a.roms):
            sysname = os.path.basename(root)
            for fn in files:
                if fn.startswith("."): continue
                roms.append((os.path.join(root, fn), sysname))
        roms.sort()
        if a.limit: roms = roms[:a.limit]

    done = set()
    if os.path.exists(a.log):
        for line in open(a.log):
            try:
                r = json.loads(line)
                if r.get("status") == "PASS": done.add(r["path"])
            except Exception: pass
    todo = [r for r in roms if r[0] not in done]
    print(f"[game master] corpus={len(roms)} to-run={len(todo)} (skipped {len(roms)-len(todo)} already PASS) "
          f"parallel={a.n} secs/rom={a.secs}")

    work = queue.Queue()
    for r in todo: work.put(r)
    results, lock = [], threading.Lock()
    logf = open(a.log, "a")
    def worker():
        while True:
            try: rom, system = work.get_nowait()
            except queue.Empty: return
            rec = run_one(rom, system, a.kernel, a.elf, a.secs, symbolize)
            with lock:
                results.append(rec); logf.write(json.dumps(rec) + "\n"); logf.flush()
                mark = "PASS " if rec["status"] == "PASS" else f"** {rec['status']} **"
                extra = ""
                if rec["fault"]: extra = f"  ELR={rec['fault']['elr_sym']} FAR=0x{rec['fault']['far']}"
                print(f"  [{mark}] {rec['system'][:24]:24} {rec['rom'][:40]:40} {rec['survived_s']}s{extra}")
            work.task_done()
    threads = [threading.Thread(target=worker) for _ in range(a.n)]
    for t in threads: t.start()
    for t in threads: t.join()
    logf.close()

    n_fault = sum(1 for r in results if r["status"] == "FAULT")
    n_pass = sum(1 for r in results if r["status"] == "PASS")
    print(f"\n[game master] PASS={n_pass}  FAULT={n_fault}  other={len(results)-n_pass-n_fault}")
    if n_fault:
        print("[game master] fault hotspots (ELR -> count):")
        from collections import Counter
        c = Counter(r["fault"]["elr_sym"] for r in results if r["status"] == "FAULT" and r["fault"])
        for sym, cnt in c.most_common(10):
            print(f"    {cnt:4}x  {sym}")
    print(f"[game master] full fault log: {a.log}")

if __name__ == "__main__":
    main()
