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
import argparse, hashlib, json, os, subprocess, sys, time, glob

ERA = {
    # dev references only — never written into a schema
    'early': {'.nes', '.sms', '.gb', '.gg', '.pce'},
    'late':  {'.smc', '.sfc', '.gba', '.md', '.gen', '.n64', '.z64'},
}
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


def run_rom(rom, elf, out_dir, log, timeout_s):
    """Drive ZXV with one ROM. Returns the record.

    The emulator runs INSIDE the kernel under test, so a ROM that misbehaves
    stresses ZXV rather than a host process — that is the whole point of the
    inversion, and it is why a crash here is a finding rather than a nuisance.
    """
    digest = sha256(rom)
    era = era_of(rom)
    rec = {'rom_sha256': digest, 'era': era, 'bytes': os.path.getsize(rom)}
    trace = os.path.join(out_dir, 'traces', digest[:16] + '.log')
    os.makedirs(os.path.dirname(trace), exist_ok=True)

    cmd = ['qemu-system-aarch64', '-M', 'virt', '-cpu', 'cortex-a72', '-nographic',
           '-kernel', elf, '-append', f'gamedrive={rom}', '-no-reboot']
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

    # Faults, with the phase tick that was live when they fired. A fault's tick
    # is not necessarily where the cause is -- record it so the lag can be
    # measured rather than assumed.
    faults, tick = [], None
    for line in output.split('\n'):
        if '[TICK]' in line:
            tick = line.strip()[:80]
        low = line.lower()
        if any(k in low for k in ('panic', 'fault', 'abort', '[fail]', 'exception')):
            faults.append({'line': line.strip()[:200], 'tick_at_fault': tick})
    rec['faults'] = faults[:40]
    rec['fault_count'] = len(faults)
    rec['boot_ok'] = 'BOOT_OK' in output

    # Symbols the run announced touching. Requires the kernel to emit
    # [COVER] <symbol> markers; absent that, coverage stays empty and the log
    # says so rather than implying a measurement that did not happen.
    cover = {l.split('[COVER]', 1)[1].strip() for l in output.split('\n') if '[COVER]' in l}
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
    print(f'booted OK        : {sum(1 for r in roms if r.get("boot_ok"))}/{len(roms)}')
    instrumented = sum(1 for r in roms if r.get('coverage_instrumented'))
    print(f'coverage-instrumented runs: {instrumented}/{len(roms)}')
    print(f'distinct symbols reached  : {len(cover)}')
    if not instrumented and roms:
        print('\nNOTE: no [COVER] markers seen. The kernel is not emitting coverage,')
        print('      so "symbols reached" is 0 because it was NOT MEASURED --')
        print('      not because nothing was reached. Instrument first.')
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
    a = ap.parse_args()
    if a.report:
        return report(a.report)
    if not a.corpus:
        ap.error('--corpus required (or --report RUNDIR)')

    os.makedirs(a.out, exist_ok=True)
    run_id = time.strftime('%Y%m%dT%H%M%S')
    log = Log(os.path.join(a.out, 'gamedrive.jsonl'), run_id)

    exts = set().union(*ERA.values()) if a.era == 'all' else ERA[a.era]
    roms = [p for p in glob.glob(os.path.join(a.corpus, '**', '*'), recursive=True)
            if os.path.isfile(p) and os.path.splitext(p)[1].lower() in exts]
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
        rec = run_rom(rom, a.elf, a.out, log, a.timeout)
        if rec.get('kind') == 'rom':
            schema_stub(rec, a.out)
        if i % 25 == 0:
            print(f'  {i}/{len(roms)}')
    log('end', roms=len(roms))
    return report(a.out)


if __name__ == '__main__':
    sys.exit(main())
