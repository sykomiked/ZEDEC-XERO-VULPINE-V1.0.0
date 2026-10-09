#!/usr/bin/env python3
# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
"""check_dist.py DIST VERSION — open every package and check what is inside
it: the right files, the right CPU architectures, execute permissions."""
import os
import struct
import sys
import tarfile
import zipfile

dist, version = sys.argv[1], sys.argv[2]
name = 'ZXV-Swarm-' + version
problems = []


def macho_archs(data):
    magic = struct.unpack('>I', data[:4])[0]
    cpu = {0x01000007: 'x86_64', 0x0100000C: 'arm64'}
    if magic == 0xCAFEBABE:                              # universal
        n = struct.unpack('>I', data[4:8])[0]
        return sorted(cpu.get(struct.unpack('>I', data[8 + 20 * i:12 + 20 * i])[0], '?') for i in range(n))
    if magic == 0xCFFAEDFE:
        return [cpu.get(struct.unpack('<I', data[4:8])[0], '?')]
    return []


def elf_arch(data):
    return {62: 'x86_64', 183: 'aarch64'}.get(struct.unpack('<H', data[18:20])[0], '?') if data[:4] == b'\x7fELF' else None


def check(cond, msg):
    print('  %s %s' % ('ok  ' if cond else 'FAIL', msg))
    if not cond:
        problems.append(msg)


z = zipfile.ZipFile(os.path.join(dist, name + '-macos.app.zip'))
exe = z.getinfo('ZXV Swarm.app/Contents/MacOS/zxv-swarm')
archs = macho_archs(z.read(exe))
check(archs in (['arm64', 'x86_64'], ['arm64']), 'mac app binary: %s' % ', '.join(archs))
check((exe.external_attr >> 16) & 0o111, 'mac app binary is executable')
check(b'com.36n9genetics.zxv-swarm' in z.read('ZXV Swarm.app/Contents/Info.plist'), 'mac Info.plist')
dmg = os.path.join(dist, name + '-macos.dmg')
if os.path.exists(dmg):
    import pycdlib
    iso = pycdlib.PyCdlib()
    try:
        iso.open(dmg)
        rr = {c.rock_ridge.name(): c.rock_ridge.get_file_mode()
              for c in iso.list_children(rr_path='/ZXV Swarm.app/Contents/MacOS') if c.rock_ridge}
        check(rr.get(b'zxv-swarm', 0) & 0o111, 'dmg holds the app, executable')
        iso.close()
    except Exception as e:                               # a native hdiutil image
        check(open(dmg, 'rb').read(512)[:4] != b'', 'dmg written (%s)' % type(e).__name__)

z = zipfile.ZipFile(os.path.join(dist, name + '-windows-x86_64.zip'))
data = z.read('zxv-swarm.exe')
pe = struct.unpack('<I', data[0x3C:0x40])[0]
check(data[:2] == b'MZ' and data[pe:pe + 4] == b'PE\0\0' and struct.unpack('<H', data[pe + 4:pe + 6])[0] == 0x8664,
      'windows exe is PE x86_64')

for arch in ('x86_64', 'aarch64'):
    t = tarfile.open(os.path.join(dist, '%s-linux-%s.tar.gz' % (name, arch)))
    m = t.getmember('zxv-swarm-%s/zxv-swarm' % version)
    check(elf_arch(t.extractfile(m).read(64)) == arch, 'linux %s binary' % arch)
    check(m.mode & 0o111, 'linux %s binary is executable' % arch)
    check('zxv-swarm-%s/zxv-swarm.desktop' % version in t.getnames(), 'linux %s desktop entry' % arch)

if problems:
    sys.exit('package check failed: ' + '; '.join(problems))
