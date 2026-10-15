// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
//
// passport_worker.js — the passport (kernel/src/passport, built to
// zxv_passport.wasm) in its own thread, so the page keeps drawing while the
// keys are built. What the person typed comes in once and is written into
// the module, which zeroes it after reading; only the DID (public) goes back.
//
// Messages in:
//   {op: 'start', sha256}            load zxv_passport.wasm and check it
//                                    against the boot manifest's hash
//   {op: 'seed_new'}                 a fresh seed as text, from this
//                                    browser's CSPRNG, to show once; it is
//                                    kept pending until it is typed back
//   {op: 'login', mode: 'seed', text}
//   {op: 'login', mode: 'pass', handle, pass}
//   {op: 'login', mode: 'new', text}  the pending seed typed back
//   {op: 'wipe'}                     zero the seed, the keys and the scratch
// Messages out:
//   {op: 'ready'} | {op: 'step', done, of, label} | {op: 'seed', text}
//   {op: 'did', did} | {op: 'error', code, where}
//
// A step is sent when it begins, with how many steps are already done: the
// page's bar shows done/of, so it moves only when a step has really ended.

'use strict';

let X = null; // the module's exports

function mem()
{
    return new Uint8Array(X.memory.buffer);
}

function readOut()
{
    const m = mem(), p = X.pw_out();
    let e = p;
    while (m[e] !== 0) e++;
    return new TextDecoder().decode(m.slice(p, e));
}

// Write bytes into the module's input buffer; false when they do not fit.
function writeIn(parts)
{
    let n = 0;
    for (const b of parts) n += b.length;
    if (n > X.pw_in_max()) return false;
    const m = mem();
    let at = X.pw_in();
    for (const b of parts) {
        m.set(b, at);
        at += b.length;
        b.fill(0);
    }
    return true;
}

async function sha256hex(bytes)
{
    const d = await crypto.subtle.digest('SHA-256', bytes);
    return Array.from(new Uint8Array(d), (b) => b.toString(16).padStart(2, '0')).join('');
}

async function start(sha256)
{
    const res = await fetch('zxv_passport.wasm', {cache : 'no-cache'});
    if (!res.ok) throw new Error(`zxv_passport.wasm: ${res.status} ${res.statusText}`);
    const bytes = new Uint8Array(await res.arrayBuffer());
    if (!sha256 || (await sha256hex(bytes)) !== sha256.toLowerCase())
        throw new Error('zxv_passport.wasm does not match the hash the boot checked');
    const w = await WebAssembly.instantiate(bytes);
    X = w.instance.exports;
}

function step(done, of, label)
{
    postMessage({op : 'step', done, of, label});
}

function login(msg)
{
    const enc = new TextEncoder();
    if (msg.mode === 'seed' || msg.mode === 'new') {
        const text = enc.encode(String(msg.text || ''));
        const where = msg.mode === 'new' ? 'confirm' : 'seed';
        step(0, 2, where);
        if (!writeIn([ text ])) return {code : -2, where};
        let r = msg.mode === 'new' ? X.pw_seed_confirm(text.length) : X.pw_seed_text(text.length);
        if (r !== 0) return {code : r, where};
        step(1, 2, 'keys');
        r = X.pw_hydrate();
        if (r < 0) return {code : r, where : 'keys'};
        step(2, 2, 'done');
        return {did : readOut()};
    }
    if (msg.mode === 'pass') {
        const pass = enc.encode(String(msg.pass || ''));
        const handle = enc.encode(String(msg.handle || ''));
        step(0, 3, 'memory');
        if (X.pw_kdf_memory() !== 0) return {code : -6, where : 'memory'};
        step(1, 3, 'stretch');
        const np = pass.length, nh = handle.length;
        if (!writeIn([ pass, handle ])) return {code : np > 256 ? -1 : -2, where : 'stretch'};
        let r = X.pw_seed_pass(np, nh);
        if (r !== 0) return {code : r, where : 'stretch'};
        step(2, 3, 'keys');
        r = X.pw_hydrate();
        if (r < 0) return {code : r, where : 'keys'};
        step(3, 3, 'done');
        return {did : readOut()};
    }
    return {code : -1, where : 'mode'};
}

onmessage = async (ev) => {
    const msg = ev.data || {};
    try {
        if (msg.op === 'start') {
            await start(msg.sha256);
            postMessage({op : 'ready'});
        } else if (!X) {
            postMessage({op : 'error', code : -18, where : 'start'});
        } else if (msg.op === 'seed_new') {
            const r = new Uint8Array(32);
            crypto.getRandomValues(r);
            writeIn([ r ]);
            const n = X.pw_seed_new();
            if (n < 0)
                postMessage({op : 'error', code : n, where : 'seed_new'});
            else
                postMessage({op : 'seed', text : readOut()});
        } else if (msg.op === 'login') {
            const out = login(msg);
            if (out.did)
                postMessage({op : 'did', did : out.did});
            else
                postMessage({op : 'error', code : out.code, where : out.where});
        } else if (msg.op === 'wipe') {
            X.pw_wipe();
            postMessage({op : 'wiped'});
        }
    } catch (err) {
        postMessage({op : 'error', code : 0, where : msg.op || '', message : String(err.message || err)});
    }
};
