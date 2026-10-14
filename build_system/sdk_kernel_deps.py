#!/usr/bin/env python3
# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
"""sdk_kernel_deps.py: the SDK workflows start only when what they build changes.

The SDK workflows (sdk*.yml) build libzxv, and libzxv is built from part of
kernel/. Their path filters used to say 'kernel/src/**', so every pull
request that touched any of the 240-odd kernel modules started all of them
(about 170 jobs). With 20 runners, CI's own jobs, ci-gate among them, then
queued behind work that could not be affected (docs/CI.md, "Merge gate").

This script asks the compiler what libzxv is built from:
- the kernel directories in `make -n -B` of sdk/libzxv (sources and -I);
- every header that `-MM` finds behind each compile.
Each SDK workflow's push and pull_request paths list those directories
between two marker comments.

  sdk_kernel_deps.py           print the directories
  sdk_kernel_deps.py --write   rewrite the marked blocks in the workflows
  sdk_kernel_deps.py --check   fail when a workflow misses a directory
                               (quick-checks runs this)
"""
import os
import re
import shlex
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
LIBZXV = os.path.join(ROOT, 'sdk', 'libzxv')
WORKFLOWS = ['sdk.yml', 'sdk-pillars.yml', 'sdk-engines.yml', 'sdk-frameworks.yml',
             'sdk-mobile.yml', 'sdk-embedded.yml', 'sdk-industry.yml']
BEGIN = "# kernel inputs of libzxv: build_system/sdk_kernel_deps.py --write"
END = "# end of the kernel inputs of libzxv"
KDIR = re.compile(r'kernel/(src|third_party)/([A-Za-z0-9_]+)')


def kernel_dirs():
    out = subprocess.run(['make', '-n', '-B'], cwd=LIBZXV, capture_output=True, text=True,
                         check=True).stdout
    dirs = set()
    for line in out.splitlines():
        if ' -c ' not in line:
            continue
        dirs.update(m.group(0) for m in KDIR.finditer(line))
        args = shlex.split(line)
        cc = args[0]
        keep, skip = [], False
        for a in args[1:]:
            if skip:
                skip = False
                continue
            if a == '-o':
                skip = True
                continue
            if a in ('-c', '-MMD', '-MP'):
                continue
            keep.append(a)
        dep = subprocess.run([cc, '-MM'] + keep, cwd=LIBZXV, capture_output=True, text=True)
        if dep.returncode != 0:
            sys.exit('sdk_kernel_deps: -MM failed for: %s\n%s' % (line, dep.stderr))
        dirs.update(m.group(0) for m in KDIR.finditer(dep.stdout))
    if not dirs:
        sys.exit('sdk_kernel_deps: no kernel inputs found (make -n in sdk/libzxv)')
    return sorted(dirs)


def wanted(dirs):
    out = [d + '/**' for d in dirs if d.startswith('kernel/src/')]
    out.append('kernel/include/**')
    out.append('kernel/third_party/**')
    return out


def block(dirs, indent):
    pad = ' ' * indent
    return ([pad + BEGIN] + ["%s- '%s'" % (pad, p) for p in wanted(dirs)] + [pad + END])


def rewrite(path, dirs):
    lines = open(path).read().split('\n')
    out, i, n = [], 0, 0
    while i < len(lines):
        line = lines[i]
        if line.strip() == BEGIN:
            indent = len(line) - len(line.lstrip())
            j = i
            while lines[j].strip() != END:
                j += 1
            out += block(dirs, indent)
            i, n = j + 1, n + 1
            continue
        out.append(line)
        i += 1
    if n != 2:
        sys.exit('sdk_kernel_deps: %s needs the marker block under push and pull_request '
                 '(found %d)' % (path, n))
    open(path, 'w').write('\n'.join(out))


def check(dirs):
    import yaml
    bad = []
    for name in WORKFLOWS:
        y = yaml.safe_load(open(os.path.join(ROOT, '.github', 'workflows', name)))
        on = y.get(True, y.get('on'))
        for ev in ('push', 'pull_request'):
            paths = set((on.get(ev) or {}).get('paths') or [])
            if 'kernel/src/**' in paths:
                bad.append('%s %s: lists all of kernel/src/** (run --write)' % (name, ev))
            for p in wanted(dirs):
                if p not in paths:
                    bad.append('%s %s: misses %s, which libzxv is built from' % (name, ev, p))
    if bad:
        print('\n'.join(bad))
        sys.exit('sdk_kernel_deps: the SDK workflows do not match libzxv\'s inputs; run '
                 'python3 build_system/sdk_kernel_deps.py --write')
    print('sdk_kernel_deps: %d SDK workflows list the %d kernel inputs of libzxv' %
          (len(WORKFLOWS), len(dirs)))


def main():
    dirs = kernel_dirs()
    if '--write' in sys.argv:
        for name in WORKFLOWS:
            rewrite(os.path.join(ROOT, '.github', 'workflows', name), dirs)
    if '--check' in sys.argv:
        check(dirs)
    elif '--write' not in sys.argv:
        print('\n'.join(dirs))


if __name__ == '__main__':
    main()
