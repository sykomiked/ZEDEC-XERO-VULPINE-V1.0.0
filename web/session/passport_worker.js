// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
//
// passport_worker.js — the passport (kernel/src/passport, built to
// zxv_passport.wasm) in its own thread, so the page keeps drawing while the
// keys are built. What the person typed comes in once and is written into
// the module, which zeroes it after reading; only the DID (public) goes back.
//
// Every message in carries an id, and every answer to it the same id, so a
// page that asks twice before the first answer cannot take one answer for
// the other. Messages in:
//   {op: 'start', sha256}            load zxv_passport.wasm and check it
//                                    against the boot manifest's hash
//   {op: 'seed_new'}                 a fresh seed as text, from this
//                                    browser's CSPRNG, to show once; it is
//                                    kept pending until it is typed back
//   {op: 'login', mode: 'seed', text}
//   {op: 'login', mode: 'pass', handle, pass}
//   {op: 'login', mode: 'new', text}  the pending seed typed back
//   {op: 'wipe'}                     zero the seed, the keys and the scratch
// Messages out (each with the id it answers):
//   {op: 'ready'} | {op: 'step', done, of, label} | {op: 'seed', text}
//   {op: 'did', did} | {op: 'wiped'} | {op: 'error', code, where, message?}
//
// A step is sent when it begins, with how many steps are already done: the
// page's bar shows done/of, so it moves only when a step has really ended.

'use strict';

let X = null; // the module's exports
let current = 0; // the id of the message being answered

const LOAD_MS = 30000; // zxv_passport.wasm is local: 30 s without it is an answer

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
// Either way the page's copies are zeroed.
function writeIn(parts)
{
    let n = 0;
    for (const b of parts) n += b.length;
    if (n > X.pw_in_max()) {
        for (const b of parts) b.fill(0);
        return false;
    }
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
    const stop = new AbortController();
    const timer = setTimeout(() => stop.abort(), LOAD_MS);
    let bytes;
    try {
        const res = await fetch('zxv_passport.wasm', {cache : 'no-cache', signal : stop.signal});
        if (!res.ok) throw new Error(`zxv_passport.wasm: ${res.status} ${res.statusText}`);
        bytes = new Uint8Array(await res.arrayBuffer());
    } catch (err) {
        if (stop.signal.aborted) throw new Error(`zxv_passport.wasm did not arrive within ${LOAD_MS / 1000} s`);
        if (err instanceof TypeError) throw new Error(`zxv_passport.wasm could not be read (${err.message})`);
        throw err;
    } finally {
        clearTimeout(timer);
    }
    if (!sha256 || (await sha256hex(bytes)) !== sha256.toLowerCase())
        throw new Error('zxv_passport.wasm does not match the hash the boot checked');
    const w = await WebAssembly.instantiate(bytes);
    X = w.instance.exports;
}

function step(done, of, label)
{
    postMessage({id : current, op : 'step', done, of, label});
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
        if (!writeIn([ pass, handle ])) return {code : -1, where : 'stretch'}; // one is over its limit
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

// Messages are answered one at a time, in the order they came: a login that
// is building keys finishes before the next message is looked at.
let queue = Promise.resolve();

async function handle(msg)
{
    const id = msg.id;
    current = id;
    const answer = (m) => postMessage(Object.assign({id}, m));
    try {
        if (msg.op === 'start') {
            await start(msg.sha256);
            answer({op : 'ready'});
        } else if (!X) {
            answer({op : 'error', code : -18, where : 'start'});
        } else if (msg.op === 'seed_new') {
            const r = new Uint8Array(32);
            crypto.getRandomValues(r);
            writeIn([ r ]);
            const n = X.pw_seed_new();
            if (n < 0) {
                answer({op : 'error', code : n, where : 'seed_new'});
            } else {
                const text = readOut();
                X.pw_out_wipe();
                answer({op : 'seed', text});
            }
        } else if (msg.op === 'login') {
            const out = login(msg);
            if (out.did)
                answer({op : 'did', did : out.did});
            else
                answer({op : 'error', code : out.code, where : out.where});
        } else if (msg.op === 'wipe') {
            X.pw_wipe();
            answer({op : 'wiped'});
        } else {
            answer({op : 'error', code : -1, where : 'op', message : `no such request: ${msg.op}`});
        }
    } catch (err) {
        answer({op : 'error', code : 0, where : msg.op || '', message : String(err.message || err)});
    }
}

onmessage = (ev) => {
    const msg = ev.data || {};
    queue = queue.then(() => handle(msg));
};
