#!/usr/bin/env python3
"""gamedrive.py — the game plays the system.

THE INVERSION
-------------
Normally a player drives the game. Here the ROM drives ZXV: the emulated
machine's memory traffic, interrupt cadence, timing edges and state transitions
become the input stream that exercises the OS. The playthrough IS the harness.
Nobody has to author test cases, because decades of shipped software already
encode millions of them.

One run yields three products at once:

  1. DEBUG      faults surface under sustained adversarial load, with phase-tick
                context so a fault can be traced back to its cause rather than
                to wherever it finally exploded (see the bomb-squad model: in a
                nonlinear system the fuse burns for a while before the bang).
  2. COVERAGE   which ZXV symbols a real workload actually reaches. This is the
                empirical answer to the triage question. 1,673 of 2,301 symbols
                are currently unreachable; the ones a genuine workload calls
                belong, and the ones that stay dark through hours of hostile
                input are candidates for deletion. Guessing is not required.
  3. TRAINING   abstract dynamics, emitted as Sutra schemas.

ERA STRATIFICATION — the two eras teach different things
--------------------------------------------------------
EARLY (6502 / Z80 / LR35902 / HuC6280): deterministic rule-spaces. Small state,
tight loops, exact timing. What these exercise is the SYSTEM: scheduling, memory,
interrupt latency, the emulator cores themselves. Extract MECHANICS — state
machines, invariants, resource rules, failure modes.

LATE (65816 / ARM7TDMI / 68000 and up): story-bearing. Larger state, branching
consequence, persistent relationships between agents. These additionally teach
SOCIAL DYNAMICS — obligation, reciprocity, betrayal, escalation, repair. That is
the signal worth training on, and it does not exist in the early era.

WHAT IS EXTRACTED, AND WHAT IS NOT
----------------------------------
Extraction is of STRUCTURE, never of content. A schema records that an agent
conferred a resource conditional on prior aid, and that withdrawing it later
raised hostility — a relational pattern. It does NOT record dialogue, character
names, plot text, script, or any asset bytes. ROMs are identified by SHA-256
digest only. Titles, where a human needs them, live outside the corpus as dev
references and never inside a schema.

This is both the legal position and the useful one: the transferable signal is
the relational shape, not the words, and a corpus of shapes generalises where a
corpus of quotations would only memorise.

LOGGING
-------
Everything is JSONL, one record per event, appendable and resumable. The point
is that the next action is readable off the log without rerunning anything:
which ROMs ran, what broke and when, which symbols went from dark to live, and
which modules are still dark after N hours of real load.

Usage:
  gamedrive.py --corpus DIR --elf kernel_arm64.elf --out RUNDIR [--era early|late|all]
  gamedrive.py --report RUNDIR        # what happened, and what to do next
"""
import shutil
import argparse, hashlib, json, os, subprocess, sys, time, glob, zipfile, tempfile

# Only extensions whose console has a REACHABLE core. Verified against
# kernel_arm64.elf, not against the Makefile:
#   .sms/.gg  -- sms.c is in NO Makefile; nm shows 0 sms_ symbols. cpu_z80.c is
#                linked but orphaned (its only machine is unbuilt).
#   .n64/.z64 -- no core exists at all.
# Listing them would schedule ROMs that can never run, consuming campaign slots
# and reporting nothing.
ERA = {
    # dev references only — never written into a schema
    'early': {'.nes', '.gb', '.pce'},
    'late':  {'.smc', '.sfc', '.gba', '.md', '.gen'},
}
# Cores that silently no-op unknown opcodes and will therefore report CLEAN
# having executed almost nothing. Campaign results from these are not evidence.
SUSPECT_CORES = {'.gba': 'ARM7 core: 16 hex cases for full ARM32+THUMB',
                 '.md': 'm68k core: 11 hex cases; write_ea discards unhandled writes',
                 '.gen': 'm68k core: as .md',
                 '.smc': 'SNES: no SPC700; APU is a 2-byte fake that converts hangs into passes',
                 '.sfc': 'SNES: as .smc'}
NM = os.environ.get('NM', 'aarch64-linux-gnu-nm')


def sha256(p):
    h = hashlib.sha256()
    with open(p, 'rb') as f:
        for b in iter(lambda: f.read(1 << 20), b''):
            h.update(b)
    return h.hexdigest()


def elf_symbols(elf):
    try:
        out = subprocess.run([NM, elf], capture_output=True, text=True, timeout=120).stdout
    except Exception:
        return set()
    return {f[2] for f in (l.split() for l in out.split('\n')) if len(f) >= 3}


ALL_EXT = set().union(*ERA.values())


def resolve_rom(path):
    """Corpus entries are mostly .zip with the ROM inside. Extract to a temp file
    and return (real_path, cleanup) so the era/extension logic works on the ROM
    rather than on its container. Returns (None, None) if the archive holds
    nothing we have a core for -- which is a SKIP, not a failure."""
    if not path.lower().endswith('.zip'):
        return path, None
    try:
        with zipfile.ZipFile(path) as z:
            cand = [n for n in z.namelist()
                    if os.path.splitext(n)[1].lower() in ALL_EXT]
            if not cand:
                return None, None
            n = max(cand, key=lambda x: z.getinfo(x).file_size)
            d = tempfile.mkdtemp(prefix='gamedrive_')
            out = os.path.join(d, os.path.basename(n))
            with open(out, 'wb') as f:
                f.write(z.read(n))
            return out, d
    except Exception:
        return None, None


def era_of(path):
    e = os.path.splitext(path)[1].lower()
    for k, v in ERA.items():
        if e in v:
            return k
    return 'unknown'


class Log:
    """Append-only JSONL. Every record carries wall time and the run id so two
    runs can be concatenated without ambiguity."""
    def __init__(self, path, run_id):
        self.f = open(path, 'a')
        self.run = run_id

    def __call__(self, kind, **kw):
        rec = {'t': round(time.time(), 3), 'run': self.run, 'kind': kind}
        rec.update(kw)
        self.f.write(json.dumps(rec) + '\n')
        self.f.flush()
        return rec


def run_rom(rom, elf, out_dir, log, timeout_s, console='?'):
    """Drive ZXV with one ROM. Returns the record.

    The emulator runs INSIDE the kernel under test, so a ROM that misbehaves
    stresses ZXV rather than a host process — that is the whole point of the
    inversion, and it is why a crash here is a finding rather than a nuisance.
    """
    digest = sha256(rom)
    era = era_of(rom)
    rec = {'rom_sha256': digest, 'era': era, 'console': console,
           'bytes': os.path.getsize(rom)}
    trace = os.path.join(out_dir, 'traces', digest[:16] + '.log')
    os.makedirs(os.path.dirname(trace), exist_ok=True)

    # ROM DELIVERY. The first version passed the path via -append. The kernel
    # never parses its command line (grep 'gamedrive' kernel/ -> 0 hits), and the
    # game runner is gated on g_vblk.present (kernel_main_arm64.c:2031), so every
    # run was a byte-identical boot with an unread string attached: 144,000
    # copies of one boot log, every ROM 'passing' without ever existing.
    # build_system/game_master.py:97 already did this correctly; this is a port
    # of those two lines back.
    cmd = ['qemu-system-aarch64', '-M', 'virt', '-cpu', 'cortex-a72', '-nographic',
           '-kernel', elf, '-no-reboot',
           '-drive', f'if=none,file={rom},format=raw,id=gd0,readonly=on',
           '-device', 'virtio-blk-device,drive=gd0']
    t0 = time.time()
    try:
        r = subprocess.run(cmd, capture_output=True, text=True, timeout=timeout_s)
        output, rc = r.stdout + r.stderr, r.returncode
        status = 'ok'
    except subprocess.TimeoutExpired as e:
        output = (e.stdout or b'').decode('utf8', 'replace') if isinstance(e.stdout, bytes) else (e.stdout or '')
        rc, status = -1, 'timeout'
    except FileNotFoundError:
        return log('skip', reason='qemu-system-aarch64 not installed', **rec)
    rec['secs'] = round(time.time() - t0, 2)
    rec['rc'], rec['status'] = rc, status
    with open(trace, 'w') as f:
        f.write(output)

    # ---- FUSE ANALYSIS ------------------------------------------------------
    # A fault manifests at tick T; its CAUSE was at tick T-k. Recording only the
    # tick at the bang is close to useless -- it names the victim, not the
    # culprit. So we keep a ring buffer of the lines preceding each fault (the
    # FUSE WINDOW) and, where the bomb squad has been reporting margins, we find
    # the first degradation that precedes the bang and call the distance between
    # them the FUSE LENGTH. That number is the thing worth having: it says how
    # far back to look, and it is measured rather than assumed.
    FUSE = 400                      # lines of pre-fault context retained
    lines = output.split('\n')
    faults, ring = [], []
    first_watch = first_armed = None   # bomb-squad margin transitions
    tick = None
    for i, line in enumerate(lines):
        low = line.lower()
        if '[tick]' in low:
            tick = line.strip()[:80]
        # bomb squad states: SAFE -> WATCH -> ARMED -> BREACHED
        if first_watch is None and '[watch]' in low:
            first_watch = i
        if first_armed is None and '[armed]' in low:
            first_armed = i
        if any(k in low for k in ('panic', 'fault', 'abort', '[fail]',
                                  'exception', '[breached]')):
            fuse_from = first_armed if first_armed is not None else first_watch
            faults.append({
                'at_line': i,
                'line': line.strip()[:200],
                'tick_at_fault': tick,
                # the window BEFORE the bang -- where the cause actually is
                'fuse_window': [l.strip()[:160] for l in ring[-FUSE:] if l.strip()],
                # distance from first margin degradation to manifestation
                'fuse_len_lines': (i - fuse_from) if fuse_from is not None else None,
                'fuse_origin': ('armed' if first_armed is not None
                                else 'watch' if first_watch is not None else None),
            })
        ring.append(line)
        if len(ring) > FUSE:
            ring.pop(0)

    rec['faults'] = faults[:20]
    rec['fault_count'] = len(faults)
    rec['boot_ok'] = 'BOOT_OK' in output
    rec['first_watch_line'] = first_watch
    rec['first_armed_line'] = first_armed

    # ---- PASS CLASSIFICATION, and why "no fault" is not "pass" ---------------
    # A ROM that ran to completion without a bang may still have left a fuse
    # burning: the bomb squad saw a margin degrade and the run ended before the
    # breach. Calling that a pass is exactly the error this whole model exists
    # to prevent, so it gets its own verdict. Only a run with no fault AND no
    # unresolved margin degradation is CLEAN.
    if faults:
        rec['verdict'] = 'FAULT'
    elif first_armed is not None:
        rec['verdict'] = 'FUSE_LIT_ARMED'    # degraded to ARMED, never breached
    elif first_watch is not None:
        rec['verdict'] = 'FUSE_LIT_WATCH'    # margin moved, did not recover
    elif status == 'timeout':
        rec['verdict'] = 'INCONCLUSIVE'      # ran out of time, not a pass
    else:
        rec['verdict'] = 'CLEAN'

    # Symbols the run announced touching. Requires the kernel to emit
    # [COVER] <symbol> markers; absent that, coverage stays empty and the log
    # says so rather than implying a measurement that did not happen.
    cover = {l.split('[COVER]', 1)[1].strip() for l in lines if '[COVER]' in l}
    rec['covered'] = sorted(cover)
    rec['covered_n'] = len(cover)
    rec['coverage_instrumented'] = bool(cover)
    return log('rom', trace=trace, **rec)


def schema_stub(rec, out_dir):
    """Emit an ABSTRACT-DYNAMICS schema stub.

    Deliberately carries no title, no text, no asset bytes -- only the ROM
    digest, the era, and slots for the relational structure a later analysis
    pass fills in. Early-era ROMs get mechanics slots; late-era additionally get
    social slots, because that is the signal the later era actually contains.
    """
    d = rec['rom_sha256']
    s = {
        'schema': 'sutra/abstract-dynamics/v1',
        'rom_sha256': d,
        'era': rec['era'],
        'source': 'gamedrive',
        'mechanics': {'state_machines': [], 'invariants': [], 'resource_rules': [],
                      'failure_modes': []},
    }
    if rec['era'] == 'late':
        s['social'] = {'obligation': [], 'reciprocity': [], 'betrayal': [],
                       'escalation': [], 'repair': [], 'agency_consequence': []}
    p = os.path.join(out_dir, 'schemas', d[:16] + '.json')
    os.makedirs(os.path.dirname(p), exist_ok=True)
    json.dump(s, open(p, 'w'), indent=1)
    return p


def report(run_dir):
    """What happened, and what to do next -- read off the log, no rerun."""
    logp = os.path.join(run_dir, 'gamedrive.jsonl')
    if not os.path.exists(logp):
        print('no log at', logp); return 1
    roms, cover, faults, skipped = [], set(), 0, 0
    for line in open(logp):
        try: r = json.loads(line)
        except Exception: continue
        if r.get('kind') == 'rom':
            roms.append(r); cover |= set(r.get('covered') or []); faults += r.get('fault_count', 0)
        elif r.get('kind') == 'skip':
            skipped += 1
    early = [r for r in roms if r['era'] == 'early']
    late = [r for r in roms if r['era'] == 'late']
    print(f'ROMs driven      : {len(roms)}  (early {len(early)} / late {len(late)})')
    print(f'skipped          : {skipped}')
    print(f'faults observed  : {faults}')
    from collections import Counter
    verd = Counter(r.get('verdict', '?') for r in roms)
    print('verdicts         : ' + '  '.join(f'{k}={v}' for k, v in verd.most_common()))
    lit = verd.get('FUSE_LIT_ARMED', 0) + verd.get('FUSE_LIT_WATCH', 0)
    if lit:
        print(f'  ** {lit} run(s) ended with a LIT FUSE -- no bang, but a margin')
        print(f'     degraded and never recovered. These are NOT passes.')
    fl = [f['fuse_len_lines'] for r in roms for f in (r.get('faults') or [])
          if f.get('fuse_len_lines') is not None]
    if fl:
        fl.sort()
        print(f'fuse length (lines from first margin loss to bang):')
        print(f'  min {fl[0]}  median {fl[len(fl)//2]}  max {fl[-1]}  n={len(fl)}')
        print(f'  -> look back at least {fl[-1]} lines from any bang for the cause')
    print(f'booted OK        : {sum(1 for r in roms if r.get("boot_ok"))}/{len(roms)}')
    instrumented = sum(1 for r in roms if r.get('coverage_instrumented'))
    print(f'coverage-instrumented runs: {instrumented}/{len(roms)}')
    print(f'distinct symbols reached  : {len(cover)}')
    if not instrumented and roms:
        print('\nNOTE: no [COVER] markers seen. The kernel is not emitting coverage,')
        print('      so "symbols reached" is 0 because it was NOT MEASURED --')
        print('      not because nothing was reached. Instrument first.')
    from collections import defaultdict
    bycon = defaultdict(lambda: defaultdict(int))
    for r in roms:
        bycon[r.get('console', '?')][r.get('verdict', '?')] += 1
    if len(bycon) > 1:
        print('\nper-console verdicts (the only readable view of an 81-console run):')
        rows = sorted(bycon.items(), key=lambda kv: -sum(kv[1].values()))
        print(f'  {"console":<44} {"runs":>6} {"CLEAN":>6} {"FAULT":>6} {"LIT":>5}')
        for c, v in rows[:25]:
            tot = sum(v.values())
            lit = v.get('FUSE_LIT_ARMED', 0) + v.get('FUSE_LIT_WATCH', 0)
            print(f'  {c[:44]:<44} {tot:>6} {v.get("CLEAN",0):>6} {v.get("FAULT",0):>6} {lit:>5}')
        allclean = [c for c, v in rows if v.get('CLEAN', 0) == sum(v.values()) and sum(v.values()) > 20]
        if allclean:
            print(f'\n  ** {len(allclean)} console(s) reported 100% CLEAN over 20+ runs.')
            print('  ** Treat that as SUSPICIOUS until coverage proves the ROMs really')
            print('  ** executed -- a core that no-ops unknown opcodes looks perfect.')
    worst = sorted(roms, key=lambda r: -r.get('fault_count', 0))[:8]
    if worst and worst[0].get('fault_count'):
        print('\nmost fault-dense ROMs (best debugging leads):')
        for r in worst:
            if r.get('fault_count'):
                print(f'  {r["fault_count"]:4d} faults  {r["era"]:5s}  {r["rom_sha256"][:16]}')
    return 0


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--corpus'); ap.add_argument('--elf', default='kernel_arm64.elf')
    ap.add_argument('--out', default='gamedrive_run')
    ap.add_argument('--era', default='all', choices=['early', 'late', 'all'])
    ap.add_argument('--limit', type=int, default=0)
    ap.add_argument('--timeout', type=int, default=120)
    ap.add_argument('--report')
    ap.add_argument('--preflight', action='store_true',
                    help='check the run can produce VALID results before spending hours on it')
    a = ap.parse_args()
    if a.report:
        return report(a.report)
    if a.preflight:
        ok = True
        print('gamedrive preflight')
        q = shutil.which('qemu-system-aarch64')
        print(f'  qemu-system-aarch64 : {q or "MISSING"}'); ok &= bool(q)
        e = os.path.exists(a.elf)
        print(f'  kernel elf          : {a.elf} {"" if e else "MISSING"}'); ok &= e
        # THE CHECK THAT MATTERS. Without margin markers every run reports CLEAN,
        # so a 144k campaign would return a uniformly false pass. Refuse to let
        # that be discovered after the fact.
        marks = {}
        for m in ('[COVER]', '[WATCH]', '[ARMED]', '[BREACHED]'):
            n = 0
            for root, _, fs in os.walk('kernel'):
                for f in fs:
                    if f.endswith(('.c', '.h')):
                        try:
                            if m in open(os.path.join(root, f), errors='ignore').read(): n += 1
                        except Exception: pass
            marks[m] = n
            print(f'  emits {m:<11}: {n} files')
        instrumented = marks['[WATCH]'] or marks['[ARMED]']
        if not instrumented:
            ok = False
            print('\n  *** BLOCKED: the kernel emits no bomb-squad margin markers.')
            print('  *** Every run would classify CLEAN because there is nothing to')
            print('  *** detect -- a false pass across the whole corpus. Instrument')
            print('  *** the WATCH/ARMED transitions before starting a long campaign.')
        if not marks['[COVER]']:
            print('\n  WARNING: no [COVER] markers -- the coverage product will be')
            print('  empty. Fault-finding still works; reachability triage does not.')
        if a.corpus:
            exts = set().union(*ERA.values())
            files = [p for p in glob.glob(os.path.join(a.corpus, '**', '*'), recursive=True)
                     if os.path.isfile(p)]
            direct = sum(1 for p in files if os.path.splitext(p)[1].lower() in exts)
            zips = sum(1 for p in files if p.lower().endswith('.zip'))
            n = direct + zips
            print(f'\n  corpus              : {a.corpus}')
            print(f'    files total       : {len(files)}')
            print(f'    direct ROMs       : {direct}')
            print(f'    zip containers    : {zips} (contents resolved at run time)')
            print(f'    candidates        : {n}')
            print(f'    NOTE: candidacy is by EXTENSION only. A file whose console has')
            print(f'    no machine layer in this kernel will still be counted here and')
            print(f'    then skipped or run meaninglessly -- see the core audit.')
            ok &= n > 0
        else:
            print('\n  corpus              : (not given -- pass --corpus DIR)')
        print('\nPREFLIGHT ' + ('PASS -- safe to start' if ok else 'FAIL -- fix the above first'))
        return 0 if ok else 1
    if not a.corpus:
        ap.error('--corpus required (or --report RUNDIR)')

    os.makedirs(a.out, exist_ok=True)
    run_id = time.strftime('%Y%m%dT%H%M%S')
    log = Log(os.path.join(a.out, 'gamedrive.jsonl'), run_id)

    exts = set().union(*ERA.values()) if a.era == 'all' else ERA[a.era]
    roms = [p for p in glob.glob(os.path.join(a.corpus, '**', '*'), recursive=True)
            if os.path.isfile(p) and (os.path.splitext(p)[1].lower() in exts
                                      or p.lower().endswith('.zip'))]
    roms.sort()
    if a.limit:
        roms = roms[:a.limit]

    base = elf_symbols(a.elf)
    log('start', elf=a.elf, elf_symbols=len(base), roms=len(roms), era=a.era,
        note='baseline symbol count; coverage deltas are measured against this')
    print(f'{len(roms)} ROMs, era={a.era}, elf has {len(base)} symbols')

    # Resume: skip ROMs already recorded for this corpus.
    done = set()
    lp = os.path.join(a.out, 'gamedrive.jsonl')
    if os.path.exists(lp):
        for line in open(lp):
            try:
                r = json.loads(line)
                if r.get('kind') == 'rom':
                    done.add(r['rom_sha256'])
            except Exception:
                pass

    for i, rom in enumerate(roms, 1):
        d = sha256(rom)
        if d in done:
            continue
        real, tmp = resolve_rom(rom)
        if real is None:
            log('skip', rom_sha256=d, reason='archive holds no ROM this system has a core for')
            continue
        if a.era != 'all' and era_of(real) != a.era:
            if tmp: shutil.rmtree(tmp, ignore_errors=True)
            continue
        # Console tag comes from the corpus directory. It is OPERATIONAL data --
        # it lets an 81-console run be diagnosed and sharded per platform. It is
        # deliberately NOT propagated into the Sutra schema, which stays
        # digest-identified and content-free.
        console = os.path.relpath(rom, a.corpus).split(os.sep)[0] if a.corpus else '?'
        rec = run_rom(real, a.elf, a.out, log, a.timeout, console=console)
        if tmp:
            shutil.rmtree(tmp, ignore_errors=True)
        if rec.get('kind') == 'rom':
            schema_stub(rec, a.out)
        if i % 25 == 0:
            print(f'  {i}/{len(roms)}')
    log('end', roms=len(roms))
    return report(a.out)


if __name__ == '__main__':
    sys.exit(main())
