// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
//
// entry.js — the root's front page: the three ways into one sealed root,
// read from entry.json (tools/web/make_entry_json.py):
//
//   Run here     the boot screen (entry.run.path, boot.html): the system in
//                this tab, the model running in the tab (web/ai)
//   Install      entry.install.images (arch, format, path, bytes, sha256,
//                cid) and entry.install.scripts (os, path, ...): this
//                machine's arch and system first, each with its link
//   Run as a VM  entry.vm names vm.json, checked against its sha256 here:
//                { zxv_vm: 1, ram_mb, cpus,
//                  discs: { x86_64: {path, bytes, sha256, cid?}, arm64: ... },
//                  launchers: [ { host, os: mac|windows|linux, path?, command? } ] }
//                the person's own system first
//
// A root with no entry.json is a browser root only (the one zxvd serves the
// app's window): it goes straight to the boot screen, as does a page zxvd
// itself serves (it hands the page its launch token). A way the root does
// not carry is shown as not here, never as a dead link.

'use strict';

const $ = (id) => document.getElementById(id);
const BOOT = 'boot.html';

function servedByZxvd()
{
    if (typeof window.__ZXV_TOKEN === 'string') return true;
    try {
        return !!sessionStorage.getItem('zxv-token');
    } catch (e) {
        return false;
    }
}

function say(text)
{
    $('why').textContent = text || '';
}

function mib(n)
{
    if (n >= 1 << 30) return `${(n / (1 << 30)).toFixed(2)} GiB`;
    if (n >= 1 << 20) return `${(n / (1 << 20)).toFixed(1)} MiB`;
    return `${Math.ceil(n / 1024)} KiB`;
}

function hereOS()
{
    const p = ((navigator.userAgentData && navigator.userAgentData.platform) || navigator.platform || navigator.userAgent || '').toLowerCase();
    if (p.includes('mac')) return 'mac';
    if (p.includes('win')) return 'windows';
    if (p.includes('linux') || p.includes('x11') || p.includes('cros')) return 'linux';
    return '';
}

async function hereArch()
{
    try {
        if (navigator.userAgentData && navigator.userAgentData.getHighEntropyValues) {
            const v = await navigator.userAgentData.getHighEntropyValues([ 'architecture', 'bitness' ]);
            if (v.architecture === 'arm') return 'arm64';
            if (v.architecture === 'x86' && v.bitness === '64') return 'x86_64';
        }
    } catch (e) {
        // fall through to the user agent
    }
    const ua = navigator.userAgent;
    if (/aarch64|arm64/i.test(ua)) return 'arm64';
    // a Mac's user agent says Intel on Apple silicon too, so it decides nothing
    if (!/Macintosh/.test(ua) && /x86_64|Win64|x64|amd64/i.test(ua)) return 'x86_64';
    return '';
}

const FORMAT_WORDS = {
    img : 'raw disc image: write it to a USB stick or a disk, or give it to a server as its disk',
    iso : 'bootable ISO, BIOS and UEFI: burn it, or attach it to a machine as a CD',
    qcow2 : 'QEMU/KVM image: a VPS custom image, or a disc for a local VM',
};
const OS_WORDS = {unix : 'macOS and Linux', windows : 'Windows', mac : 'macOS', linux : 'Linux'};
const HOST_WORDS = {qemu : 'QEMU', utm : 'UTM', hyperv : 'Hyper-V'};

function url(path)
{
    return new URL(path, location.href).href;
}

async function copy(text, btn)
{
    try {
        await navigator.clipboard.writeText(text);
        const was = btn.textContent;
        btn.textContent = 'Copied';
        setTimeout(() => {
            btn.textContent = was;
        }, 1400);
    } catch (e) {
        say('This browser would not copy: select the text and copy it yourself.');
    }
}

function button(text, ghost, onClick)
{
    const b = document.createElement('button');
    b.type = 'button';
    b.className = ghost ? 'btn ghost' : 'btn';
    b.textContent = text;
    b.addEventListener('click', () => onClick(b));
    return b;
}

function download(path)
{
    const a = document.createElement('a');
    a.className = 'btn';
    a.href = path;
    a.setAttribute('download', path.split('/').pop());
    a.textContent = 'Download';
    return a;
}

// one file row: what it is, its size, sha256 and CID, and its links
function fileRow(title, mine, f, extra)
{
    const row = document.createElement('div');
    row.className = mine ? 'item mine' : 'item';
    row.dataset.path = f.path;
    const what = document.createElement('div');
    what.className = 'what';
    const b = document.createElement('b');
    b.textContent = title;
    if (mine) {
        const t = document.createElement('span');
        t.className = 'tag';
        t.textContent = 'this machine';
        t.style.display = 'inline';
        b.append(t);
    }
    what.append(b);
    for (const line of [ extra, `${mib(f.bytes)}, SHA-256 ${f.sha256}`, f.cid ? `ipfs://${f.cid}` : '' ]) {
        if (!line) continue;
        const s = document.createElement('span');
        s.textContent = line;
        what.append(s);
    }
    row.append(what, download(f.path), button('Copy link', true, (btn) => copy(url(f.path), btn)));
    return row;
}

function okFile(f)
{
    return f && typeof f.path === 'string' && f.path && !f.path.startsWith('/') && !/^[a-z]+:/i.test(f.path) &&
           Number.isSafeInteger(f.bytes) && f.bytes >= 0 && /^[0-9a-f]{64}$/.test(f.sha256 || '');
}

function showInstall(entry, arch, os)
{
    const ins = entry.install || {};
    const images = (ins.images || []).filter(okFile);
    const scripts = (ins.scripts || []).filter(okFile);
    const box = $('images'), sbox = $('scripts');
    box.textContent = '';
    sbox.textContent = '';
    images.sort((a, b) => (b.arch === arch) - (a.arch === arch));
    for (const im of images)
        box.append(fileRow(`${im.arch} disc, ${im.format}`, im.arch === arch, im, FORMAT_WORDS[im.format] || ''));
    const mine = os === 'windows' ? 'windows' : os ? 'unix' : '';
    scripts.sort((a, b) => (b.os === mine) - (a.os === mine));
    for (const s of scripts) sbox.append(fileRow(`Install script for ${OS_WORDS[s.os] || s.os}`, s.os === mine, s, ''));
    $('install-note').textContent = images.length ?
        (arch ? `This machine looks ${arch}; its disc is first. ` : 'This page cannot tell this machine\'s processor: pick the disc for it. ') +
            'Check the SHA-256 after downloading; the link is the root\'s own, so it never changes for this root.' :
        'This root carries no disc image yet.';
}

async function sha256hex(buf)
{
    const d = new Uint8Array(await crypto.subtle.digest('SHA-256', buf));
    return Array.from(d, (x) => x.toString(16).padStart(2, '0')).join('');
}

async function loadVM(f)
{
    if (!crypto.subtle) throw new Error('this page needs a secure origin (https or localhost) to check vm.json');
    const res = await fetch(f.path, {cache : 'no-cache'});
    if (!res.ok) throw new Error(`${f.path}: ${res.status} ${res.statusText}`);
    const buf = await res.arrayBuffer();
    if (buf.byteLength !== f.bytes || await sha256hex(buf) !== f.sha256)
        throw new Error(`${f.path} does not match entry.json: it is not this root's`);
    const vm = JSON.parse(new TextDecoder().decode(buf));
    if (!vm || vm.zxv_vm !== 1) throw new Error(`${f.path} is not a VM description`);
    return vm;
}

function showVM(vm, arch, os)
{
    const box = $('launchers');
    box.textContent = '';
    const base = new URL(entryVM.path, location.href);
    const at = (p) => new URL(p, base).href;
    const discs = vm.discs || {};
    const words = [];
    if (vm.ram_mb) words.push(`${vm.ram_mb} MiB of memory`);
    if (vm.cpus) words.push(`${vm.cpus} processors`);
    const order = Object.keys(discs).sort((a, b) => (b === arch) - (a === arch));
    for (const a of order) {
        const d = discs[a];
        if (!okFile(d)) continue;
        const rel = new URL(d.path, base).href;
        box.append(fileRow(`${a} VM disc`, a === arch, Object.assign({}, d, {path : rel}), 'the same disc as Install; a VM boots it under UEFI'));
    }
    const launchers = (vm.launchers || []).filter((l) => l && HOST_WORDS[l.host]);
    launchers.sort((a, b) => (b.os === os) - (a.os === os));
    for (const l of launchers) {
        const row = document.createElement('div');
        row.className = l.os === os ? 'item mine' : 'item';
        const what = document.createElement('div');
        what.className = 'what';
        const b = document.createElement('b');
        b.textContent = `${HOST_WORDS[l.host]} on ${OS_WORDS[l.os] || l.os || 'any system'}`;
        what.append(b);
        row.append(what);
        if (typeof l.path === 'string' && l.path && !/^[a-z]+:/i.test(l.path)) {
            const a = document.createElement('a');
            a.className = 'btn';
            a.href = at(l.path);
            a.setAttribute('download', l.path.split('/').pop());
            a.textContent = 'Download';
            row.append(a);
        }
        if (typeof l.command === 'string' && l.command) {
            const pre = document.createElement('pre');
            pre.textContent = l.command;
            row.append(pre, button('Copy command', true, (btn) => copy(l.command, btn)));
        }
        box.append(row);
    }
    $('vm-note').textContent = (words.length ? `Give it ${words.join(' and ')}. ` : '') +
        (launchers.length ? 'Your own system is first.' : 'This root names no launcher yet; boot the disc in any UEFI VM.');
}

let entryVM = null;

function pick(id)
{
    for (const w of [ 'w-run', 'w-install', 'w-vm' ]) $(w).setAttribute('aria-pressed', String(w === id));
    $('p-install').hidden = id !== 'w-install';
    $('p-vm').hidden = id !== 'w-vm';
}

async function main()
{
    if (servedByZxvd()) return location.replace(BOOT);
    let entry;
    try {
        const res = await fetch('entry.json', {cache : 'no-cache'});
        if (res.status === 404) return location.replace(BOOT);
        if (!res.ok) throw new Error(`entry.json: ${res.status} ${res.statusText}`);
        entry = await res.json();
        if (!entry || entry.zxv_entry !== 1) throw new Error('entry.json is not this page\'s');
    } catch (err) {
        say(`${err.message}. Run here still works.`);
        entry = {run : {path : BOOT}};
    }
    const run = entry.run && typeof entry.run.path === 'string' && !/^[a-z]+:/i.test(entry.run.path) ? entry.run.path : BOOT;
    $('w-run').addEventListener('click', () => location.assign(run));

    const [ arch, os ] = [ await hereArch(), hereOS() ];
    const images = ((entry.install || {}).images || []).filter(okFile);
    if (!images.length && !((entry.install || {}).scripts || []).length) {
        $('w-install').disabled = true;
        $('s-install').textContent = 'Not in this root yet.';
    } else {
        $('w-install').addEventListener('click', () => {
            pick('w-install');
            showInstall(entry, arch, os);
        });
    }
    entryVM = okFile(entry.vm) ? entry.vm : null;
    if (!entryVM) {
        $('w-vm').disabled = true;
        $('s-vm').textContent = 'Not in this root yet.';
    } else {
        let vm = null;
        $('w-vm').addEventListener('click', async () => {
            pick('w-vm');
            say('');
            try {
                vm = vm || await loadVM(entryVM);
                showVM(vm, arch, os);
            } catch (err) {
                $('launchers').textContent = '';
                $('vm-note').textContent = '';
                say(err.message);
            }
        });
    }
    $('w-run').focus();
}

main().catch((err) => say(`This page could not start: ${err.message}`));
