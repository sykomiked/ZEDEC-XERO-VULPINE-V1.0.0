#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Inventory tracked and non-ignored working-tree sources and literal test references; never infer coverage.

This is a structural census, not a security audit or proof of correctness.
--strict fails when build files refer to missing test inputs.
"""
import argparse
import fnmatch
import json
from pathlib import Path
import re
import subprocess


def inventory(root):
    files = subprocess.check_output(
        ['git', '-C', str(root), 'ls-files', '--cached', '--others', '--exclude-standard', '-z'], text=True).split('\0')
    files = sorted(f for f in files if f)
    sources = [f for f in files if f.endswith('.c')]
    tests = [f for f in sources if Path(f).name.startswith(('test_', 'fuzz_'))
             or Path(f).name == 'test.c']
    references = {}
    for name in files:
        path = Path(name)
        if not (path.name.startswith('Makefile') or name.startswith('.github/workflows/')):
            continue
        text = (root / name).read_text()
        # Literal paths only; variable-generated paths are deliberately unassessed.
        for match in re.finditer(r'(?<![\w/])(?:[\w.-]+/)*(?:test_[\w.-]+|fuzz_[\w.-]+)\.(?:c|sh)', text):
            ref = match.group()
            # Top-level build_system makefiles run via `make -f` at the
            # repository root; component Makefiles run in their own directory.
            base = root if name.startswith(('build_system/', '.github/workflows/')) else root / path.parent
            resolved = (base / ref).resolve()
            if not resolved.is_file():
                key = str(resolved.relative_to(root)) if resolved.is_relative_to(root) else ref
                references.setdefault(key, []).append(name)
    subsystems = {}
    for src in sources:
        parts = Path(src).parts
        if len(parts) >= 4 and parts[:2] == ('kernel', 'src'):
            subsystems.setdefault(parts[2], []).append(src)
    # Explicit source references are traceability candidates, not feature proof.
    by_name = {}
    for source in files:
        by_name.setdefault(Path(source).name, []).append(source)
    spec_refs = []
    for doc in files:
        if not doc.endswith('.md'):
            continue
        if not any(word in Path(doc).name.upper() for word in ('SPEC', 'ARCHITECTURE', 'ROADMAP', 'CANONICAL')):
            continue
        for line_no, line in enumerate((root / doc).read_text().splitlines(), 1):
            for literal in re.findall(r'`([^`]+\.(?:c|h|sutra|wat|py))`', line):
                if any(char in literal for char in '*?['):
                    candidates = [f for f in files if fnmatch.fnmatch(f,literal)
                                  or fnmatch.fnmatch(f,'*/'+literal)]
                else:
                    candidates = ([literal] if literal in files
                                  else by_name.get(Path(literal).name, []))
                spec_refs.append({'document': doc, 'line': line_no,
                                  'reference': literal, 'candidates': candidates,
                                  'status': 'unresolved' if not candidates else
                                            'ambiguous' if len(candidates) > 1 else
                                            'source located; integration unverified'})
    def nonblank(name):
        return sum(bool(line.strip()) for line in (root / name).read_text().splitlines())
    def is_test(name):
        return name.startswith('tests/') or '/tests/' in name or Path(name).name.startswith(('test_', 'fuzz_'))
    owned = [f for f in files if '/third_party/' not in f]
    production = sum(nonblank(f) for f in owned
                     if f.endswith(('.c', '.h')) and not is_test(f))
    test_lines = sum(nonblank(f) for f in owned
                     if f.endswith(('.c', '.h', '.py')) and is_test(f))
    c_test_lines = sum(nonblank(f) for f in owned
                       if f.endswith(('.c', '.h')) and is_test(f))
    return {
        'scope': 'Tracked and non-ignored working-tree C files and literal test inputs in tracked Makefiles/workflows; no coverage claim',
        'spec_source_references': spec_refs,
        'test_code_budget': {
            'metric': 'Nonblank repository C/H production lines; C/H/Python test lines; includes generated tables, excludes third-party code and QA runners',
            'production_lines': production, 'test_lines': test_lines,
            'target_ratio': 6, 'actual_ratio': round(test_lines / production, 6) if production else None,
            'additional_test_lines_for_target': max(0, 6 * production - test_lines),
            'c_h_test_lines': c_test_lines,
            'c_h_only_ratio': round(c_test_lines / production, 6) if production else None,
            'status': 'quantity target only; not a correctness or certification claim'},
        'c_sources': len(sources), 'c_test_files': tests,
        'missing_test_inputs': {k: sorted(set(v)) for k,v in sorted(references.items())},
        'subsystems': {k: {'sources': v, 'local_test_files': [s for s in v if s in tests],
                          'status': 'requires functional validation'}
                       for k,v in sorted(subsystems.items())},
        'architectures': ['arm64', 'x86_64', 'riscv', 'riscv32', 'arm32'],
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path)
    parser.add_argument('--strict', action='store_true')
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    report = inventory(root)
    contents = json.dumps(report, indent=2) + '\n'
    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(contents)
    else:
        print(contents, end='')
    return int(args.strict and bool(report['missing_test_inputs']))


if __name__ == '__main__':
    raise SystemExit(main())
