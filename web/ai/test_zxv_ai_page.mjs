// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
//
// deno run --unstable-webgpu --location http://zxv.test/ -A test_zxv_ai_page.mjs DIR MODEL NATIVE.zw [MAX_NEW]
//
// What a page gets from zxv_ai.js, run in Deno (module Workers, WebGPU,
// localStorage): DIR holds zxv_ai.js, zxv_ai_worker.js and the engine; MODEL
// is a GGUF in DIR; NATIVE.zw holds the native engine's ZW lines for it
// (test_zxv_ai_engine.c). Checks:
//   - a model whose bytes or SHA-256 differ from its entry is refused, and so
//     is one whose raw CID names another digest; nothing runs;
//   - each prompt's reply has the native ids and logits digest, and the
//     streamed pieces make its text;
//   - the measured choice is kept: the second open reuses it ("cached");
//   - device 'cpu' keeps the model on the CPU, with the same digests;
//   - device 'webgpu' puts every pass on the GPU (when there is an adapter),
//     with the same digests;
//   - an abort stops a reply between tokens.

const [dir, modelName, nativePath, maxArg] = Deno.args;
const maxNew = Number(maxArg || 24);
const base = new URL(`file://${await Deno.realPath(dir)}/`);
const {openAI} = await import(new URL('zxv_ai.js', base).href);

const PROMPTS = [
    'Hello! Who are you?',
    'Integer math is exact.',
    'Grüße aus Köln, 東京 🦊',
];

let checks = 0, failures = 0;
function check(ok, what)
{
    checks++;
    if (!ok) failures++;
    console.log(`  ${ok ? 'ok  ' : 'FAIL'} ${what}`);
}

async function refused(entry, what)
{
    try {
        const ai = await openAI({model : entry, device : 'cpu'});
        ai.close();
        check(false, `${what}: refused`);
    } catch (e) {
        check(true, `${what}: refused (${e.message.slice(0, 160)})`);
    }
}

const bytes = await Deno.readFile(new URL(modelName, base));
const sha256 = Array.from(new Uint8Array(await crypto.subtle.digest('SHA-256', bytes)), (b) => b.toString(16).padStart(2, '0')).join('');
const native = new Map();
for (const line of (await Deno.readTextFile(nativePath)).split('\n')) {
    const m = /^ZW (\d+) (.*)$/.exec(line);
    if (m) native.set(Number(m[1]), m[2]);
}
check(native.size === PROMPTS.length, `the native lines are read (${native.size})`);

const entry = {name : modelName, cid : 'bafybeigdyrzt5sfp7udm7hu76uh7y26nf3efuylqabf3oclgtqy55fbzdi', bytes : bytes.length, sha256, path : modelName, quant : 'Q4_K_M', base : base.href};

// refusals: nothing runs on a file that is not the one named
await refused({...entry, sha256 : '0'.repeat(64)}, 'a SHA-256 that does not match');
await refused({...entry, bytes : bytes.length - 1}, 'a size that does not match');
await refused({...entry, cid : 'bafkreigh2akiscaildcqabsyg3dfr6chu3fgpregiymsck7e7aqa4s52zy'}, 'a raw CID naming another digest');
await refused({...entry, path : 'no-such-model.gguf'}, 'a path that is not there');

async function answers(ai, label)
{
    let same = true;
    for (let i = 0; i < PROMPTS.length; i++) {
        let streamed = '';
        const r = await ai.generate(PROMPTS[i], {maxTokens : maxNew, onToken : (t) => { streamed += t; }});
        const line = `n_prompt=${r.nPrompt} n_gen=${r.nGen} stop=${r.stopped} digest=${r.digest} ids=${r.ids.join(',')}`;
        if (line !== native.get(i)) {
            same = false;
            console.log(`    native ${native.get(i)}\n    page   ${line}`);
        }
        if (streamed !== r.text) {
            same = false;
            console.log(`    streamed ${JSON.stringify(streamed)} != ${JSON.stringify(r.text)}`);
        }
    }
    check(same, `${label}: every reply has the native ids and digest, streamed piece by piece (${ai.device})`);
}

localStorage.clear();
let ai = await openAI({model : entry, device : 'auto'});
console.log(`[INFO] ${ai.record.slice(0, 400)}`);
check(ai.record.startsWith('zt-choice/1 cpu=') && !ai.record.includes('cached'), 'the first open measures the engines');
await answers(ai, 'measured');

// an abort stops a reply between tokens
{
    const ctl = new AbortController();
    let n = 0;
    const r = await ai.generate(PROMPTS[0], {maxTokens : 200, signal : ctl.signal, onToken : () => { if (++n === 2) ctl.abort(); }});
    check(r.stopped === 'abort' && r.nGen < 200, `an abort stops the reply (${r.nGen} tokens, ${r.stopped})`);
}
ai.close();

ai = await openAI({model : entry, device : 'auto'});
check(ai.record.includes('cached (measured on first launch)'), 'the second open reuses the kept choice');
await answers(ai, 'kept choice');
ai.close();

ai = await openAI({model : entry, device : 'cpu'});
check(ai.record.includes('gpu=none'), `device cpu stays on the CPU (${ai.device})`);
await answers(ai, 'CPU only');
ai.close();

if (navigator.gpu && await navigator.gpu.requestAdapter()) {
    ai = await openAI({model : entry, device : 'webgpu'});
    check(ai.device.startsWith('webgpu') && ai.record.includes('gpu=webgpu:0-max'), `device webgpu puts every pass on the GPU (${ai.device})`);
    await answers(ai, 'WebGPU only');
    ai.close();
} else {
    console.log('[SKIP] device webgpu (no WebGPU adapter here)');
}

console.log(`${checks - failures}/${checks} checks passed`);
Deno.exit(failures ? 1 : 0);
