// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
//
// test_zxv_ai.mjs ENGINE.mjs MODEL.gguf [MAX_NEW] [CHOICE] — the browser's
// engine (zxv_ai_engine.wasm) answers the prompts test_zxv_ai_engine.c
// answers natively, and prints the same ZW lines (ids and logits digest),
// which kernel/Makefile test-ai-web compares with the native ones. Runs in
// Node (no WebGPU: the CPU in wasm) and in Deno (WebGPU). CHOICE, when given,
// is passed as ZT_CHOICE (e.g. "cpu=c/1 gpu=webgpu:0-max" puts every pass on
// the GPU).

const PROMPTS = [
    'Hello! Who are you?',
    'Integer math is exact.',
    'Grüße aus Köln, 東京 🦊',
];

const isDeno = typeof Deno !== 'undefined';
const args = isDeno ? Deno.args : process.argv.slice(2);
const [enginePath, modelPath, maxArg, choice] = args;
const maxNew = Number(maxArg || 24);

async function readFile(path)
{
    if (isDeno) return await Deno.readFile(path);
    const fs = await import('node:fs/promises');
    return new Uint8Array(await fs.readFile(path));
}

function out(line)
{
    if (isDeno)
        console.log(line);
    else
        process.stdout.write(line + '\n');
}

const engineUrl = new URL(enginePath, isDeno ? `file://${Deno.cwd()}/` : `file://${process.cwd()}/`);
const zxvAIEngine = (await import(engineUrl.href)).default;
const E = await zxvAIEngine({
    preRun : [ (m) => {
        m.ENV.ZT_CHOICE_CACHE = 'off';
        if (choice) m.ENV.ZT_CHOICE = choice;
    } ],
    print : (t) => out(`[engine] ${t}`),
    printErr : (t) => out(`[engine] ${t}`),
});

const call = (name, ret, types, vals) => E.ccall(name, ret, types, vals, {async : true});
const str = (name) => E.UTF8ToString(E.ccall(name, 'number', [], []));

const bytes = await readFile(modelPath);
const ptr = E._malloc(bytes.length);
E.HEAPU8.set(bytes, ptr);
let r = await call('zw_load', 'number', [ 'number', 'number' ], [ ptr, bytes.length ]);
if (r !== 0) {
    out(`FAIL load: ${str('zw_error')}`);
    if (isDeno) Deno.exit(1); else process.exit(1);
}
out(`[INFO] device: ${str('zw_device')}`);
out(`[INFO] choice: ${str('zw_record')}`);

let failed = 0;
for (let i = 0; i < PROMPTS.length; i++) {
    const nPrompt = await call('zw_start', 'number', [ 'string', 'number' ], [ PROMPTS[i], maxNew ]);
    if (nPrompt < 0) {
        out(`FAIL prompt ${i}: ${str('zw_error')}`);
        failed++;
        continue;
    }
    const ids = [];
    let text = '';
    for (;;) {
        const t = await call('zw_next', 'number', [], []);
        if (t >= 0) ids.push(t);
        text += str('zw_text');
        if (t < 0) break;
    }
    out(`ZW ${i} n_prompt=${nPrompt} n_gen=${ids.length} stop=${str('zw_stop')} digest=${str('zw_digest')} ids=${ids.join(',')}`);
    out(`TEXT ${i} ${JSON.stringify(text)}`);
}
E.ccall('zw_free', null, [], []);
E._free(ptr);
if (isDeno) Deno.exit(failed ? 1 : 0); else process.exit(failed ? 1 : 0);
