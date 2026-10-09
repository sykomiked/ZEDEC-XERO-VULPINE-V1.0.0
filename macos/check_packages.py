#!/usr/bin/env python3
# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
"""check_packages.py DIST VERSION [--mac-only] — open every desktop package
build_desktop.sh made and check what is inside it: the right files, CPU
architectures, execute permissions and Info.plist. Supersedes
build_system/check_dist.py, which predates ZXV.app."""
import os
import plistlib
import struct
import sys
import tarfile
import zipfile

dist, version = sys.argv[1], sys.argv[2]
mac_only = '--mac-only' in sys.argv[3:]
name = 'ZXV-' + version
problems = []


def macho_archs(data):
    magic = struct.unpack('>I', data[:4])[0]
    cpu = {0x01000007: 'x86_64', 0x0100000C: 'arm64'}
    if magic == 0xCAFEBABE:                              # universal
        n = struct.unpack('>I', data[4:8])[0]
        return sorted(cpu.get(struct.unpack('>I', data[8 + 20 * i:12 + 20 * i])[0], '?')
                      for i in range(n))
    if magic == 0xCFFAEDFE:
        return [cpu.get(struct.unpack('<I', data[4:8])[0], '?')]
    return []


def elf_arch(data):
    if data[:4] != b'\x7fELF':
        return None
    return {62: 'x86_64', 183: 'aarch64'}.get(struct.unpack('<H', data[18:20])[0], '?')


def check(cond, msg):
    print('  %s %s' % ('ok  ' if cond else 'FAIL', msg))
    if not cond:
        problems.append(msg)


# ---- macOS
z = zipfile.ZipFile(os.path.join(dist, name + '-macos.app.zip'))
names = set(z.namelist())
plist = plistlib.loads(z.read('ZXV.app/Contents/Info.plist'))
check(plist.get('CFBundleIdentifier') == 'com.36n9genetics.zxv-swarm', 'Info.plist bundle id')
check(plist.get('CFBundleShortVersionString') == version, 'Info.plist version')
exe_name = plist.get('CFBundleExecutable')
native = exe_name == 'ZXV'
check(exe_name in ('ZXV', 'zxv-engine'), 'executable is the native shell or the engine')
check(plist.get('LSUIElement') is (not native),
      'LSUIElement %s (%s)' % (plist.get('LSUIElement'), 'Dock icon and menus' if native
                               else 'engine-only fallback'))
binaries = ['zxv-engine'] + (['ZXV'] if native else [])
for b in binaries:
    info = z.getinfo('ZXV.app/Contents/MacOS/' + b)
    archs = macho_archs(z.read(info))
    check(archs in (['arm64', 'x86_64'], ['arm64']), '%s: %s' % (b, ', '.join(archs) or 'not Mach-O'))
    check((info.external_attr >> 16) & 0o111, '%s is executable' % b)
for r in ('models.manifest', 'zxv-model-fetch.sh', 'MAC_APP.md'):
    check('ZXV.app/Contents/Resources/' + r in names, 'Resources/' + r)
fetch = z.getinfo('ZXV.app/Contents/Resources/zxv-model-fetch.sh')
check((fetch.external_attr >> 16) & 0o111, 'model fetcher is executable')
if not native:
    print('  note: built off a Mac, so ZXV.app has no native shell (see docs/MAC_APP.md)')

dmg = os.path.join(dist, name + '-macos.dmg')
if os.path.exists(dmg):
    try:
        import pycdlib
        iso = pycdlib.PyCdlib()
        iso.open(dmg)
        rr = {c.rock_ridge.name(): c.rock_ridge.get_file_mode()
              for c in iso.list_children(rr_path='/ZXV.app/Contents/MacOS') if c.rock_ridge}
        check(rr.get(b'zxv-engine', 0) & 0o111, 'dmg holds the app, executable')
        iso.close()
    except Exception as e:                               # a native hdiutil image
        check(os.path.getsize(dmg) > 0, 'dmg written (%s)' % type(e).__name__)

# ---- Windows and Linux
if not mac_only:
    z = zipfile.ZipFile(os.path.join(dist, name + '-windows-x86_64.zip'))
    data = z.read('zxv-swarm.exe')
    pe = struct.unpack('<I', data[0x3C:0x40])[0]
    check(data[:2] == b'MZ' and data[pe:pe + 4] == b'PE\0\0'
          and struct.unpack('<H', data[pe + 4:pe + 6])[0] == 0x8664, 'windows exe is PE x86_64')
    for arch in ('x86_64', 'aarch64'):
        t = tarfile.open(os.path.join(dist, '%s-linux-%s.tar.gz' % (name, arch)))
        m = t.getmember('zxv-swarm-%s/zxv-swarm' % version)
        check(elf_arch(t.extractfile(m).read(64)) == arch, 'linux %s binary' % arch)
        check(m.mode & 0o111, 'linux %s binary is executable' % arch)
        check('zxv-swarm-%s/zxv-swarm.desktop' % version in t.getnames(),
              'linux %s desktop entry' % arch)

if problems:
    sys.exit('package check failed: ' + '; '.join(problems))
