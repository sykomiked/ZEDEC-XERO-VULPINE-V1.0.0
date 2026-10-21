// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
//
// zxv_ai_worker.js — the model's own thread (a module Worker started by
// zxv_ai.js), so the page never waits on it. It:
//
//   1. starts the engine (zxv_ai_engine.wasm: the app's integer engine with
//      WebGPU beside the CPU kernels);
//   2. streams the model's bytes straight into the engine's memory, from
//      each of the root's own paths in turn, then each URL given, telling the
//      page every chunk;
//   3. checks them before anything runs: SHA-256 against the manifest, and
//      the CID too when it is a raw SHA-256 CIDv1 (bafkrei...); a file that
//      does not match is refused, never run;
//   4. loads the model: the determiner measures the WebGPU and CPU engines
//      and keeps the fastest exact ones (or rebuilds the choice the page kept
//      for this device, after one run that must give the logits measured
//      then, bit for bit);
//   5. answers one request at a time, a message per token.
//
// Messages in:  {op:'open', engine, model, device, choice}
//               {op:'generate', id, prompt, maxTokens}  {op:'abort', id}
//               {op:'close'}
// Messages out: {op:'progress', phase, bytes, total}
//               {op:'ready', device, record, choice}  {op:'error', id, why}
//               {op:'token', id, text, token}
//               {op:'done', id, text, ids, nPrompt, nGen, digest, stopped}

'use strict';

const CHOICE_DIR = '/zxv-choices';

let E = null; // the engine
let modelPtr = 0;
let busy = false;
let aborted = new Set();

function post(msg)
{
    self.postMessage(msg);
}

// Let queued messages (an abort) in between tokens: a CPU step finishes
// without ever leaving the microtask queue.
const lane = new MessageChannel();
const waiting = [];
lane.port1.onmessage = () => waiting.shift()();
function yieldNow()
{
    return new Promise((resolve) => {
        waiting.push(resolve);
        lane.port2.postMessage(0);
    });
}

const call = (name, ret, types, vals) => E.ccall(name, ret, types, vals, {async : true});
const str = (name) => E.UTF8ToString(E.ccall(name, 'number', [], []));

// ---------- checking ----------

function hex(bytes)
{
    return Array.from(bytes, (b) => b.toString(16).padStart(2, '0')).join('');
}

// The SHA-256 a CID names, when it names one directly: CIDv1, raw codec,
// sha2-256, base32 ("bafkrei..."). Null for any other kind of CID.
export function cidSha256(cid)
{
    if (typeof cid !== 'string' || !cid.startsWith('b')) return null;
    const A = 'abcdefghijklmnopqrstuvwxyz234567';
    const out = [];
    let acc = 0, bits = 0;
    for (const c of cid.slice(1)) {
        const v = A.indexOf(c);
        if (v < 0) return null;
        acc = (acc << 5) | v;
        bits += 5;
        if (bits >= 8) {
            bits -= 8;
            out.push((acc >> bits) & 0xff);
        }
    }
    // version 1, raw (0x55), sha2-256 (0x12), 32 bytes
    if (out.length !== 36 || out[0] !== 1 || out[1] !== 0x55 || out[2] !== 0x12 || out[3] !== 0x20) return null;
    return hex(out.slice(4));
}

// ---------- fetching ----------

// Stream url into the engine's memory at modelPtr; the bytes read.
async function fetchInto(url, size)
{
    const res = await fetch(url, {cache : 'no-cache'});
    if (!res.ok) throw new Error(`${url}: ${res.status} ${res.statusText}`);
    const reader = res.body.getReader();
    let at = 0, last = 0;
    for (;;) {
        const {done, value} = await reader.read();
        if (done) break;
        if (at + value.length > size) {
            reader.cancel();
            throw new Error(`${url}: longer than the ${size} bytes the manifest gives`);
        }
        E.HEAPU8.set(value, modelPtr + at); // HEAPU8 is re-read: memory may have grown
        at += value.length;
        if (at - last >= (1 << 20) || at === size) {
            last = at;
            post({op : 'progress', phase : 'fetch', bytes : at, total : size});
        }
    }
    if (at !== size) throw new Error(`${url}: ${at} bytes, the manifest gives ${size}`);
    return at;
}

async function fetchModel(model)
{
    const sources = [...(model.paths || []).map((p) => new URL(p, model.base).href), ...(model.urls || [])];
    if (!sources.length) throw new Error('the model entry names no path and no URL');
    const want = String(model.sha256 || '').toLowerCase();
    if (!/^[0-9a-f]{64}$/.test(want)) throw new Error('the model entry has no SHA-256');
    const fromCid = cidSha256(model.cid);
    if (fromCid && fromCid !== want) throw new Error(`the CID ${model.cid} names another file than the manifest's SHA-256`);
    const tried = [];
    for (const src of sources) {
        try {
            await fetchInto(src, model.size);
            post({op : 'progress', phase : 'check', bytes : model.size, total : model.size});
            const d = hex(new Uint8Array(await crypto.subtle.digest('SHA-256', E.HEAPU8.subarray(modelPtr, modelPtr + model.size))));
            if (d !== want) throw new Error(`${src}: SHA-256 ${d}, the manifest gives ${want}`);
            return src;
        } catch (e) {
            tried.push(e.message);
        }
    }
    throw new Error(`the model could not be fetched and checked: ${tried.join('; ')}`);
}

// ---------- the determiner's kept choice ----------

function choiceRead()
{
    const out = {};
    try {
        for (const n of E.FS.readdir(CHOICE_DIR))
            if (n.startsWith('zt-choice-')) out[n] = E.FS.readFile(`${CHOICE_DIR}/${n}`, {encoding : 'utf8'});
    } catch (e) {
        // no directory: nothing was kept
    }
    return out;
}

function choiceWrite(files)
{
    try {
        E.FS.mkdir(CHOICE_DIR);
    } catch (e) {
        // already there
    }
    for (const [n, text] of Object.entries(files || {}))
        if (/^zt-choice-[0-9a-f]{16}\.txt$/.test(n) && typeof text === 'string') E.FS.writeFile(`${CHOICE_DIR}/${n}`, text);
}

// ---------- requests ----------

async function open(msg)
{
    const model = msg.model || {};
    if (!(Number.isSafeInteger(model.size) && model.size > 0 && model.size < 2 ** 32))
        throw new Error('the model entry gives no usable size');
    const zxvAIEngine = (await import(msg.engine)).default;
    E = await zxvAIEngine({
        preRun : [ (m) => {
            // the page's device choice narrows what the determiner measures
            if (msg.device === 'cpu') m.ENV.ZT_GPU = 'cpu';
            if (msg.device === 'webgpu') m.ENV.ZT_CHOICE = 'cpu=c/1 gpu=webgpu:0-max'; // every pass on the GPU
            m.ENV.ZT_CHOICE_CACHE = CHOICE_DIR;
        } ],
        locateFile : (p) => new URL(p, msg.engine).href,
    });
    modelPtr = E._malloc(model.size);
    if (!modelPtr) throw new Error(`no room for the model's ${model.size} bytes in this tab`);
    await fetchModel(model);
    choiceWrite(msg.choice);
    post({op : 'progress', phase : 'load', bytes : model.size, total : model.size});
    const r = await call('zw_load', 'number', [ 'number', 'number' ], [ modelPtr, model.size ]);
    if (r !== 0) throw new Error(str('zw_error'));
    post({op : 'ready', device : str('zw_device'), record : str('zw_record'), choice : choiceRead()});
}

async function generate(msg)
{
    const nPrompt = await call('zw_start', 'number', [ 'string', 'number' ], [ String(msg.prompt), msg.maxTokens >>> 0 ]);
    if (nPrompt < 0) throw new Error(str('zw_error'));
    const ids = [];
    let text = '', stopped = '';
    for (;;) {
        await yieldNow();
        if (aborted.has(msg.id)) {
            stopped = 'abort';
            break;
        }
        const t = await call('zw_next', 'number', [], []);
        if (t === -2) throw new Error(str('zw_error'));
        if (t >= 0) ids.push(t);
        const piece = str('zw_text');
        text += piece;
        if (t >= 0 || piece) post({op : 'token', id : msg.id, text : piece, token : t});
        if (t < 0) break;
    }
    aborted.delete(msg.id);
    post({
        op : 'done',
        id : msg.id,
        text,
        ids,
        nPrompt,
        nGen : ids.length,
        digest : str('zw_digest'),
        stopped : stopped || str('zw_stop'),
    });
}

self.onmessage = async (ev) => {
    const msg = ev.data || {};
    if (msg.op === 'abort') {
        aborted.add(msg.id);
        return;
    }
    if (msg.op === 'close') {
        if (E) E.ccall('zw_free', null, [], []);
        self.close();
        return;
    }
    if (busy) {
        post({op : 'error', id : msg.id, why : 'the model is busy with another request'});
        return;
    }
    busy = true;
    try {
        if (msg.op === 'open')
            await open(msg);
        else if (msg.op === 'generate')
            await generate(msg);
        else
            throw new Error(`unknown request ${msg.op}`);
    } catch (e) {
        post({op : 'error', id : msg.id, why : e && e.message ? e.message : String(e)});
    } finally {
        busy = false;
    }
};
