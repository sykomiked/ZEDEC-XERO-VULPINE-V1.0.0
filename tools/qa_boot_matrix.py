#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Boot all five advertised targets; fail on missing BOOT_OK, fault or exit.

Build the targets first. Logs and machine-readable results go to build/qa.
QEMU_DATA_DIR optionally supplies data for an extracted local installation.
A BOOT_OK milestone validates bring-up, not complete subsystem functionality.
"""
import argparse
import json
import os
import re
from pathlib import Path
import selectors
import subprocess
import time

ROOT = Path(__file__).resolve().parents[1]
TARGETS = {
    'arm64': ['qemu-system-aarch64','-M','virt,gic-version=3','-cpu','cortex-a53','-m','256M','-kernel','kernel_arm64.elf'],
    'x86_64': ['qemu-system-x86_64','-m','256M','-kernel','kernel_x86_64.bin'],
    'riscv': ['qemu-system-riscv64','-M','virt','-cpu','rv64','-m','256M','-kernel','kernel_riscv.elf'],
    'riscv32': ['qemu-system-riscv32','-M','virt','-m','256M','-kernel','kernel_riscv32.elf'],
    'arm32': ['qemu-system-arm','-M','virt','-cpu','cortex-a15','-m','512M','-kernel','zedec_xero_arm32.elf'],
}


FAULT_RE = re.compile(rb'\[FAULT\]|\[EL0 FAULT\]|CPU exception vector=|PANIC|[Kk]ernel panic')


def boot(command, timeout=30, required_markers=(b'[BOOT_OK]',)):
    data = bytearray()
    try:
        process = subprocess.Popen(command, cwd=ROOT, stdin=subprocess.DEVNULL,
                                   stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    except OSError as exc:
        return False, str(exc)
    milestone_at = None
    try:
        with selectors.DefaultSelector() as selector:
            selector.register(process.stdout, selectors.EVENT_READ)
            deadline = time.monotonic() + timeout
            while time.monotonic() < deadline:
                if milestone_at is not None and time.monotonic() - milestone_at >= 2:
                    return process.poll() is None, data.decode(errors='replace')
                for key, _ in selector.select(min(0.5, max(0,deadline-time.monotonic()))):
                    chunk = os.read(key.fd,65536)
                    if not chunk:
                        return False, data.decode(errors='replace')
                    data.extend(chunk)
                    if FAULT_RE.search(data):
                        return False, data.decode(errors='replace')
                    if all(marker in data for marker in required_markers) and milestone_at is None:
                        milestone_at = time.monotonic()
        return False, data.decode(errors='replace') + '\n[QA] boot timed out\n'
    finally:
        if process.poll() is None:
            process.terminate()
            try:
                process.wait(timeout=3)
            except subprocess.TimeoutExpired:
                process.kill()
                process.wait()
        process.stdout.close()


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--targets', nargs='+', choices=TARGETS, default=list(TARGETS))
    args=parser.parse_args()
    output=ROOT/'build/qa'; output.mkdir(parents=True,exist_ok=True)
    results={}
    for target in args.targets:
        base=TARGETS[target]
        command=base.copy()
        if os.environ.get('QEMU_DATA_DIR'):
            command += ['-L',os.environ['QEMU_DATA_DIR']]
        command += ['-nographic','-serial','mon:stdio']
        if target == 'riscv32':
            firmware=subprocess.run(['bash','build_system/riscv32_sbi.sh','path'],cwd=ROOT,capture_output=True,text=True)
            if firmware.returncode:
                results[target]={'passed':False,'reason':firmware.stderr}
                print(f'{target}: FAIL (SBI prerequisite)')
                continue
            if firmware.stdout.strip():
                command += ['-bios',firmware.stdout.strip()]
        markers = (b'[BOOT_OK]', b'[AI_OK]') if target in ('arm64', 'x86_64') else (b'[BOOT_OK]',)
        passed, log=boot(command, required_markers=markers)
        (output/f'boot-{target}.log').write_text(log)
        results[target]={'passed':passed,'command':command,'log':f'build/qa/boot-{target}.log'}
        print(f'{target}: {"PASS" if passed else "FAIL"}',flush=True)
    (output/'boot-matrix.json').write_text(json.dumps(results,indent=2)+'\n')
    return int(not all(r['passed'] for r in results.values()))


if __name__ == '__main__':
    raise SystemExit(main())
