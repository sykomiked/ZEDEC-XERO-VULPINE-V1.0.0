#!/usr/bin/env python3
# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
"""arm64_session_check.py: a user session on the booted arm64 kernel.

The boot checks prove the kernel reaches [BOOT_OK]. This one uses it: QEMU
boots kernel_arm64.elf twice on the same blank disk, and the script types
commands into the EL0 shell over the serial line and checks each reply.

  boot 1  the blank disk is formatted as ZXVFS and the signed hello.zsp is
          seeded on it; `write` persists a file; `run hello.zsp` verifies
          the Ed25519 signature, loads the ELF off the disk and runs it at
          EL0 in its own address space, where its privileged SYS_EXEC is
          denied; the chiglet governor reads the disk through its ABI gate,
          holds the chiglet's cell quarantine and reload until their exact
          digests are accepted, and refuses bad calls; `run` refuses a file
          that is not a signed package.
  (between boots one byte of hello.zsp's signed payload is flipped on the
   disk image, as a tampered download would be)
  boot 2  the disk mounts without reformatting; the file written in boot 1
          reads back; `run hello.zsp` now refuses the tampered package.

These are the ROADMAP G7 items "boot protected userspace", "P-TERM",
"persist files" and "run only signed apps", on QEMU's virt machine. Any
fault marker (the same pattern as ci_boot_check.sh) fails the check.

  build_system/arm64_session_check.py [kernel_arm64.elf] [workdir]
"""
import os
import re
import select
import subprocess
import sys
import time

KERNEL = sys.argv[1] if len(sys.argv) > 1 else 'kernel_arm64.elf'
WORK = sys.argv[2] if len(sys.argv) > 2 else os.environ.get('RUNNER_TEMP', '/tmp')
DISK = os.path.join(WORK, 'arm64_session.img')
FAULT_RE = re.compile(rb'\[FAULT\]|\[EL0 FAULT\]|CPU exception vector=|PANIC|[Kk]ernel panic')
PROMPT = b'P-TERM/EL0'
MARK = b'Hello from a REAL ELF'
failures = []


class Boot:
    def __init__(self, name):
        self.name = name
        self.out = b''
        # The kernel's virtio drivers are virtio 1.0 only; QEMU's virt
        # machine presents legacy virtio-mmio unless told otherwise.
        self.q = subprocess.Popen(
            ['qemu-system-aarch64', '-M', 'virt,gic-version=3', '-cpu', 'cortex-a53', '-m', '256M',
             '-kernel', KERNEL, '-display', 'none', '-monitor', 'none', '-serial', 'stdio',
             '-no-reboot', '-global', 'virtio-mmio.force-legacy=false',
             '-drive', 'if=none,id=d0,format=raw,file=' + DISK,
             '-device', 'virtio-blk-device,drive=d0'],
            stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)

    def read(self, want, secs):
        """Read serial output until `want` appears (or, with None, for `secs`)."""
        end = time.time() + secs
        while time.time() < end:
            r, _, _ = select.select([self.q.stdout], [], [], 0.2)
            if r:
                chunk = os.read(self.q.stdout.fileno(), 65536)
                if not chunk:
                    break
                self.out += chunk
                if want is not None and want in self.out:
                    return True
        return want is None

    def cmd(self, line, want, secs=15):
        """Type `line`; return what the kernel printed until `want` appeared."""
        start = len(self.out)
        for ch in line + '\r':
            self.q.stdin.write(ch.encode())
            self.q.stdin.flush()
            time.sleep(0.01)  # the UART has no input FIFO worth relying on
        ok = self.read(want, secs)
        reply = self.out[start:]
        if not ok:
            failures.append('%s: `%s` never printed %r; got:\n%s' %
                            (self.name, line, want, reply.decode(errors='replace')[-800:]))
        return reply

    def expect(self, cond, what):
        if not cond:
            failures.append('%s: %s' % (self.name, what))
        print('%s %s: %s' % ('[PASS]' if cond else '[FAIL]', self.name, what))

    def stop(self):
        self.q.terminate()
        try:
            self.q.wait(timeout=10)
        except subprocess.TimeoutExpired:
            self.q.kill()
            self.q.wait()
        m = FAULT_RE.search(self.out)
        self.expect(m is None, 'no fault marker in the serial log' +
                    ('' if m is None else ' (found %r)' % m.group(0)))
        with open(os.path.join(WORK, 'arm64_session_%s.log' % self.name), 'wb') as f:
            f.write(self.out)


def governor_checks(b):
    """The chiglet governor (kernel/src/chiglet/chg_abi.h): the console takes
    typed ABI calls; a change from the chiglet is held until the person
    accepts its exact digest; bad calls are refused."""
    b.expect(b'[OK] chiglet governor' in b.out and b'[CHIGLET] the console' in b.out,
             'boot armed the chiglet governor and handed it the console')
    r = b.cmd('abi storage_dma_read 0 2', b'storage_dma_read (you)')
    b.expect(b'read 2 sector(s) at lba 0, sha256 ' in r and b'(you) ran' in r,
             'the person\'s storage_dma_read ran through the gate')
    r = b.cmd('chiglet storage_dma_read 0 9', b'storage_dma_read (chiglet)')
    b.expect(b'(chiglet) refused' in r, 'an out-of-bounds call from the chiglet is refused')
    r = b.cmd('chiglet cell_quarantine 1', b'held: to run it')
    m = re.search(rb'accept (\d+) ([0-9a-f]{16})', r)
    b.expect(m is not None and b'(chiglet) held' in r,
             'a change from the chiglet is held with its digest')
    if m is None:
        return
    r = b.cmd('accept %s 0000000000000000' % m.group(1).decode(), b'[chiglet]')
    b.expect(b'the digest does not match' in r, 'a wrong digest runs nothing')
    r = b.cmd('held', b'digest')
    b.expect(m.group(2) in r, '`held` lists the waiting call')
    r = b.cmd('accept %s %s' % (m.group(1).decode(), m.group(2).decode()), b'cell_quarantine (you)')
    b.expect(b'cell_quarantine (you) ran' in r, 'the accepted quarantine ran')
    r = b.cmd('chiglet cell_reload 1', b'held: to run it')
    m = re.search(rb'accept (\d+) ([0-9a-f]{16})', r)
    b.expect(m is not None, 'a cell reload from the chiglet is held')
    if m is None:
        return
    r = b.cmd('accept %s %s' % (m.group(1).decode(), m.group(2).decode()), b'cell_reload (you)')
    b.expect(b'cell_reload (you) ran' in r, 'the quarantined cell reloaded as a new incarnation')
    r = b.cmd('held', b'held')
    b.expect(b'nothing is held' in r, 'nothing is left held')


def main():
    os.makedirs(WORK, exist_ok=True)
    with open(DISK, 'wb') as f:
        f.truncate(16 << 20)

    b = Boot('boot1')
    b.expect(b.read(PROMPT, 90), 'reached the EL0 P-TERM prompt')
    b.expect(b'[FORMAT] blank disk' in b.out and b'[MOUNTED] ZXVFS' in b.out,
             'formatted the blank disk as ZXVFS and mounted it')
    r = b.cmd('write g7.txt survives-a-reboot', b'(persisted)')
    b.expect(b'(persisted)' in r, '`write` persisted a file to the disk')
    r = b.cmd('run hello.zsp', b'least privilege ok')
    b.expect(b'signature verified' in r, '`run hello.zsp` verified the Ed25519 signature')
    b.expect(MARK in r, 'the signed ELF loaded off the disk and ran at EL0')
    b.expect(b'correctly DENIED' in r, "the EL0 program's privileged SYS_EXEC was denied")
    governor_checks(b)
    b.cmd('write plain.elf not-a-package', b'(persisted)')
    r = b.cmd('run plain.elf', b'refusing')
    b.expect(b'NOT A SIGNED PACKAGE' in r and MARK not in r,
             '`run` refuses a file that is not a signed package')
    b.stop()

    # Tamper with the signed payload of hello.zsp on the disk image. The
    # first copy of the program's banner is hello.zsp (seeded first);
    # app.slotA, written after it, holds the second.
    with open(DISK, 'r+b') as f:
        img = bytearray(f.read())
        at = img.find(MARK)
        if at < 0:
            failures.append('the hello.zsp payload was not found on the disk image')
        else:
            img[at] ^= 0x01
            f.seek(0)
            f.write(img)

    b = Boot('boot2')
    b.expect(b.read(PROMPT, 90), 'reached the EL0 P-TERM prompt again')
    b.expect(b'[FORMAT]' not in b.out and b'[MOUNTED] ZXVFS' in b.out,
             'the disk mounted without being reformatted')
    r = b.cmd('cat g7.txt', b'survives-a-reboot')
    b.expect(b'survives-a-reboot' in r, 'the file written in boot 1 read back after the reboot')
    r = b.cmd('run hello.zsp', b'refusing')
    b.expect(b'SIGNATURE REJECTED' in r and MARK not in r,
             '`run` refuses the tampered package and runs nothing')
    b.stop()

    if failures:
        print('\narm64 session check FAILED:')
        for f in failures:
            print('  ' + f)
        sys.exit(1)
    print('\narm64 session check passed: EL0 shell, persistence across a reboot, signed-only run')


if __name__ == '__main__':
    main()
