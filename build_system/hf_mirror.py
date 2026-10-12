#!/usr/bin/env python3
# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
"""hf_mirror.py ASSETS_DIR REPO_ID [--dry-run] — stage and upload ZXV's
Hugging Face page.

Run by .github/workflows/mirror.yml after the owner has published a GitHub
release; never run it from a pull request. ASSETS_DIR holds that release's
assets and its SHA256SUMS (gh release download), already checked with
sha256sum -c. This script:

  1. builds the page in a staging folder: README.md from
     docs/release/HUGGING_FACE_PAGE.md with the DRAFT comment removed, every
     release asset at the top level, and models/ with each manifest model
     whose licence allows redistribution and commercial use, fetched and
     hash-checked by macos/zxv-model-fetch.sh, plus that model's upstream
     LICENSE at the same pinned commit;
  2. refuses anything unexpected (a model file among the release assets, a
     page that still has a DRAFT or TODO note); models under other
     licences are left out;
  3. with --dry-run, prints the plan and stops; otherwise creates REPO_ID as
     a public model repository if it does not exist and uploads the folder
     in one commit, with HF_TOKEN from the environment.
"""
import hashlib
import os
import re
import shutil
import subprocess
import sys
import tempfile
import urllib.request

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PAGE = os.path.join(ROOT, 'docs', 'release', 'HUGGING_FACE_PAGE.md')
MANIFEST = os.path.join(ROOT, 'macos', 'models.manifest')
FETCH = os.path.join(ROOT, 'macos', 'zxv-model-fetch.sh')
REDISTRIBUTABLE = {'Apache-2.0', 'MIT'}
WEIGHTS = re.compile(r'\.(gguf|ggml|safetensors|pt|pth|ckpt|onnx|mlmodel|npz)$', re.I)
HF_FILE = re.compile(r'^https://huggingface\.co/([\w.-]+/[\w.-]+)/resolve/([0-9a-f]{40})/[\w./-]+$')


def die(msg):
    print('error: ' + msg, file=sys.stderr)
    sys.exit(1)


def page_text():
    """The model card with the leading DRAFT comment lines of its front matter removed."""
    lines = open(PAGE, encoding='utf-8').read().split('\n')
    if not lines or lines[0] != '---':
        die('%s must start with YAML front matter' % PAGE)
    out = [lines[0]]
    i = 1
    while i < len(lines) and lines[i].startswith('#'):
        i += 1
    out += lines[i:]
    text = '\n'.join(out)
    if 'DRAFT' in text or 'TODO' in text:
        die('the page still has a DRAFT or TODO note; finish it in a pull request first')
    return text


def manifest_models():
    """(name, licence, upstream repo, commit) for every manifest line with a Hugging Face mirror."""
    models = []
    for line in open(MANIFEST, encoding='utf-8'):
        f = line.split()
        if not f or f[0].startswith('#') or len(f) < 6:
            continue
        for url in f[5:]:
            m = HF_FILE.match(url)
            if m:
                models.append((f[0], f[4], m.group(1), m.group(2)))
                break
    return models


def stage(assets, out, dry):
    files = sorted(os.listdir(assets))
    if 'SHA256SUMS' not in files:
        die('no SHA256SUMS among the release assets')
    for n in files:
        if WEIGHTS.search(n):
            die('a release asset is a model file: %s' % n)
        if not dry:
            shutil.copy2(os.path.join(assets, n), os.path.join(out, n))
    plan = ['README.md (from docs/release/HUGGING_FACE_PAGE.md)'] + files
    with open(os.path.join(out, 'README.md'), 'w', encoding='utf-8') as fh:
        fh.write(page_text())

    models_dir = os.path.join(out, 'models')
    os.makedirs(models_dir, exist_ok=True)
    for name, licence, repo, commit in manifest_models():
        if licence not in REDISTRIBUTABLE:
            print('  skipping %s: licence %s is not on the redistribution list' % (name, licence))
            continue
        plan.append('models/%s (%s, from %s@%s, sha256 checked)' % (name, licence, repo, commit[:12]))
        plan.append('models/LICENSE-%s (upstream licence at that commit)' % name)
        if dry:
            continue
        one = tempfile.NamedTemporaryFile('w', suffix='.manifest', delete=False)
        one.write(' '.join(l for l in open(MANIFEST, encoding='utf-8').read().split('\n')
                           if l.split() and l.split()[0] == name) + '\n')
        one.close()
        subprocess.run(['sh', FETCH, one.name, models_dir], check=True)
        os.unlink(one.name)
        lic = 'https://huggingface.co/%s/resolve/%s/LICENSE' % (repo, commit)
        with urllib.request.urlopen(lic, timeout=60) as r, \
                open(os.path.join(models_dir, 'LICENSE-' + name), 'wb') as fh:
            fh.write(r.read())
    return plan


def main():
    args = [a for a in sys.argv[1:] if a != '--dry-run']
    dry = '--dry-run' in sys.argv[1:]
    if len(args) != 2:
        die('usage: hf_mirror.py ASSETS_DIR REPO_ID [--dry-run]')
    assets, repo_id = args
    if not re.fullmatch(r'[\w.-]+/[\w.-]+', repo_id):
        die('REPO_ID must be OWNER/NAME, got %r' % repo_id)
    out = tempfile.mkdtemp(prefix='zxv-hf-')
    plan = stage(assets, out, dry)
    print('Hugging Face page %s (model repository, public):' % repo_id)
    for p in plan:
        print('  ' + p)
    if dry:
        print('dry run: nothing uploaded')
        return
    token = os.environ.get('HF_TOKEN')
    if not token:
        die('HF_TOKEN is not set')
    from huggingface_hub import HfApi
    api = HfApi(token=token)
    api.create_repo(repo_id, repo_type='model', private=False, exist_ok=True)
    sums = hashlib.sha256(open(os.path.join(out, 'SHA256SUMS'), 'rb').read()).hexdigest()[:12]
    api.upload_folder(folder_path=out, repo_id=repo_id, repo_type='model',
                      commit_message='ZXV release assets (SHA256SUMS %s)' % sums)
    print('uploaded to https://huggingface.co/%s' % repo_id)


if __name__ == '__main__':
    main()
