// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
//
// zxv_ai.js — the model in the browser tab, for a page:
//
//   import { openAI } from './ai/zxv_ai.js';
//   const ai = await openAI({
//       model : entry,     // boot.json's models entry: { name, cid, bytes, sha256, path, quant }
//       device : 'auto',                            // 'auto' | 'webgpu' | 'cpu'
//       onProgress : (bytes, total, phase) => {},   // 'fetch' | 'check' | 'load'
//   });
//   ai.device, ai.record                            // what runs it; the determiner's line
//   const r = await ai.generate(prompt, { maxTokens, onToken : (text, id) => {}, signal });
//   // r: { text, ids, nPrompt, nGen, digest, stopped: 'eos' | 'max' | 'ctx' | 'abort' }
//   ai.close();
//
// The model runs in its own Worker (zxv_ai_worker.js) on the app's integer
// engine (zxv_ai_engine.wasm), so the same model and prompt give the same
// tokens and the same logits digest as the app and the golden disc. The
// entry's path (relative to model.base, default: this page) is read first,
// then any model.urls (gateways). The bytes must be exactly `bytes` long and
// match `sha256` before anything runs; when the CID is a raw SHA-256 CIDv1
// (bafkrei...) it must name the same digest. A chunked file's CID (UnixFS) is
// held to its sha256, as boot.json holds every file.
//
// The engine is chosen by measuring (WebGPU where the browser has it and it
// is faster, the CPU otherwise). That choice is kept in this browser's
// storage, per model and GPU, so later visits skip the measuring; it is used
// again only after a run gives the logits it was measured with.

const STORE = 'zxv-ai-choice:';

async function gpuKey()
{
    try {
        const a = navigator.gpu && await navigator.gpu.requestAdapter();
        const i = a && a.info ? a.info : {};
        return a ? [ i.vendor, i.architecture, i.device, i.description ].map((s) => s || '').join('/') : 'none';
    } catch (e) {
        return 'none';
    }
}

function kept(key)
{
    try {
        return JSON.parse(localStorage.getItem(key) || '{}');
    } catch (e) {
        return {};
    }
}

function keep(key, files)
{
    try {
        localStorage.setItem(key, JSON.stringify(files || {}));
    } catch (e) {
        // no storage here: the next visit measures again
    }
}

export async function openAI(opts = {})
{
    const e = opts.model || {};
    const model = {
        name : e.name,
        cid : e.cid,
        size : e.bytes !== undefined ? e.bytes : e.size,
        sha256 : e.sha256,
        paths : e.paths || (e.path ? [ e.path ] : []),
        urls : e.urls || [],
        base : e.base,
    };
    model.base = new URL(model.base || '.', globalThis.document ? document.baseURI : import.meta.url).href;
    const device = opts.device || 'auto';
    if (![ 'auto', 'webgpu', 'cpu' ].includes(device)) throw new Error(`unknown device ${device}`);
    const key = `${STORE}${String(model.sha256 || '').toLowerCase()}:${await gpuKey()}`;
    const worker = new Worker(new URL('./zxv_ai_worker.js', import.meta.url), {type : 'module'});
    const pending = new Map(); // request id -> {resolve, reject, onToken}
    let next = 1, closed = false, queue = Promise.resolve();

    const ready = new Promise((resolve, reject) => pending.set(0, {resolve, reject}));
    worker.onmessage = (ev) => {
        const m = ev.data || {};
        if (m.op === 'progress') {
            if (opts.onProgress) opts.onProgress(m.bytes, m.total, m.phase);
            return;
        }
        const p = pending.get(m.op === 'ready' ? 0 : m.id || 0);
        if (!p) return;
        if (m.op === 'token') {
            if (p.onToken && (m.text || m.token >= 0)) p.onToken(m.text, m.token);
        } else if (m.op === 'ready' || m.op === 'done') {
            pending.delete(m.op === 'ready' ? 0 : m.id);
            p.resolve(m);
        } else if (m.op === 'error') {
            pending.delete(m.id || 0);
            p.reject(new Error(m.why));
        }
    };
    worker.onerror = (ev) => {
        for (const p of pending.values()) p.reject(new Error(ev.message || 'the model worker stopped'));
        pending.clear();
    };
    worker.postMessage({
        op : 'open',
        engine : new URL('./zxv_ai_engine.js', import.meta.url).href,
        model,
        device,
        choice : device === 'auto' ? kept(key) : {},
    });
    let info;
    try {
        info = await ready;
    } catch (e) {
        worker.terminate();
        throw e;
    }
    if (device === 'auto') keep(key, info.choice);

    function run(prompt, o)
    {
        if (closed) return Promise.reject(new Error('the model is closed'));
        const id = next++;
        return new Promise((resolve, reject) => {
            const sig = o.signal;
            if (sig && sig.aborted) return reject(new Error('aborted'));
            const onAbort = () => worker.postMessage({op : 'abort', id});
            if (sig) sig.addEventListener('abort', onAbort, {once : true});
            const done = (f) => (v) => {
                if (sig) sig.removeEventListener('abort', onAbort);
                f(v);
            };
            pending.set(id, {resolve : done(resolve), reject : done(reject), onToken : o.onToken});
            worker.postMessage({op : 'generate', id, prompt, maxTokens : o.maxTokens || 256});
        });
    }

    return {
        device : info.device,
        record : info.record,
        // one request at a time: later ones wait their turn
        generate(prompt, o = {})
        {
            const r = queue.then(() => run(prompt, o));
            queue = r.catch(() => {});
            return r.then((m) => ({
                text : m.text,
                ids : m.ids,
                nPrompt : m.nPrompt,
                nGen : m.nGen,
                digest : m.digest,
                stopped : m.stopped,
            }));
        },
        close()
        {
            if (closed) return;
            closed = true;
            worker.postMessage({op : 'close'});
            for (const p of pending.values()) p.reject(new Error('the model is closed'));
            pending.clear();
        },
    };
}
