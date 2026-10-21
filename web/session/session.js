// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
//
// session.js — what the browser boot (web/boot) hands over to: the passport
// login, the person's spaces, a space's apps and the companion.
//
//   login      the passport (passport_worker.js + zxv_passport.wasm) rebuilds
//              the identity from a seed or a passphrase and handle, in this
//              page, with no server; the bar fills as each step really ends
//   spaces     spaces.list from the local zxvd, through the window's
//              guarded /api/rpc (the token zxvd printed on its ZXV-WEB line)
//   space      the space's apps; the companion opens on it
//   companion  swarm.ask: the model zxvd runs, under the budget gate and the
//              answer tag. The page says who wrote the answer from zxvd's own
//              record (swarm.state companion.answered_by_model and the tag);
//              when no model wrote it, the page says so and why.
//
// Where no zxvd serves the page (the root opened from an IPFS gateway or any
// static host), the page runs in this tab alone: one Home space, and the
// companion is the model boot.json's models list names, run in the tab by
// web/ai (zxv_ai.js), fetched and checked against its sha256 when the
// companion opens, its reply streamed as it is written. The same model and
// prompt give the same tokens and digest as the app and the golden disc.
//
// Every failure is shown in words where it happened. Nothing is invented.

'use strict';

const $ = (id) => document.getElementById(id);

// passport status codes (kernel/src/passport/passport.h pp_status_t)
const PP = {
    ARG : -1,
    SIZE : -2,
    FORMAT : -3,
    CHECKSUM : -4,
    WEAK : -5,
    KDF : -6,
    CRYPTO : -7,
    MISMATCH : -16,
    STATE : -18
};

const STEP_WORDS = {
    seed : 'Reading the seed',
    confirm : 'Checking the seed you typed back',
    memory : 'Setting aside 64 MiB for the passphrase stretch',
    stretch : 'Stretching the passphrase (Argon2id, 64 MiB, three passes)',
    keys : 'Building your keys (ML-DSA-87 and SLH-DSA)',
    done : 'Done'
};

let worker = null, workerReady = null, pending = null;
let mode = 'seed';
let did = '';
let space = null;

function show(id)
{
    for (const v of document.querySelectorAll('.view')) v.classList.toggle('on', v.id === id);
}

function say(el, text, ok)
{
    el.textContent = text || '';
    el.classList.toggle('ok', !!ok);
}

// ---------- the passport worker ----------

async function manifestHash(path)
{
    const res = await fetch('../boot.json', {cache : 'no-cache'});
    if (!res.ok) throw new Error(`boot.json: ${res.status} ${res.statusText}`);
    const m = await res.json();
    const e = (m.system || []).find((f) => f.path === path);
    if (!e || !e.sha256) throw new Error(`boot.json does not list ${path}`);
    return e.sha256;
}

function startWorker()
{
    worker = new Worker('passport_worker.js');
    workerReady = new Promise((resolve, reject) => {
        worker.onmessage = (ev) => {
            const m = ev.data;
            if (m.op === 'ready') return resolve();
            if (m.op === 'error' && m.where === 'start') return reject(new Error(m.message || 'the passport did not start'));
            onWorker(m);
        };
        worker.onerror = (ev) => {
            ev.preventDefault();
            const err = new Error(ev.message || 'the passport stopped');
            reject(err);
            if (pending) pending.reject(err);
            pending = null;
        };
    });
    manifestHash('session/zxv_passport.wasm')
        .then((sha256) => worker.postMessage({op : 'start', sha256}))
        .catch((err) => worker.onmessage({data : {op : 'error', where : 'start', message : err.message}}));
    return workerReady;
}

function onWorker(m)
{
    if (m.op === 'step') {
        $('meter').classList.add('on');
        $('bar').style.width = `${Math.round((100 * m.done) / m.of)}%`;
        $('what').textContent = STEP_WORDS[m.label] || m.label;
        $('what').classList.toggle('pulse', m.done < m.of);
        return;
    }
    if (!pending) return;
    const p = pending;
    pending = null;
    if (m.op === 'error' && m.message) p.reject(new Error(m.message));
    else p.resolve(m);
}

function ask(msg)
{
    return workerReady.then(() => new Promise((resolve, reject) => {
        pending = {resolve, reject};
        worker.postMessage(msg);
    }));
}

function utf8len(s)
{
    return new TextEncoder().encode(s).length;
}

function loginWords(code, where)
{
    const handle = $('handle').value, pass = $('pass').value;
    switch (code) {
    case PP.MISMATCH:
        return 'That is not the seed you were shown. Check what you wrote down and type it again, or start over with a new seed.';
    case PP.STATE:
        if (where === 'confirm') return 'There is no new seed to confirm: make one first.';
        return `The passport stopped (code ${code}).`;
    case PP.CHECKSUM:
        return 'That seed has a typo: its built-in check does not match. Look at each group of five again.';
    case PP.FORMAT:
        return 'That is not a seed: a seed is 55 letters and digits in 11 groups of five.';
    case PP.SIZE:
        return 'That is longer than any seed or passphrase.';
    case PP.WEAK:
        return 'The passphrase is too short: it needs at least 16 characters.';
    case PP.KDF:
        return 'This browser could not set aside the 64 MiB the passphrase stretch needs.';
    case PP.CRYPTO:
        return 'Your keys could not be built from this seed.';
    case PP.ARG:
        if (where === 'stretch' && (!handle || utf8len(handle) > 64))
            return 'Give a handle of 1 to 64 bytes: the name that keeps your passphrase apart from anyone else\'s.';
        if (where === 'stretch' && utf8len(pass) > 256) return 'The passphrase is longer than 256 bytes.';
        if (where === 'seed' || where === 'confirm') return 'Type your seed first.';
        return 'The passport refused that input.';
    default:
        return `The passport stopped (code ${code}).`;
    }
}

function canEnter()
{
    if (mode === 'seed') return $('seed').value.trim().length > 0;
    if (mode === 'new') return newStage === 'confirm' && $('confirm').value.trim().length > 0;
    return $('handle').value.length > 0 && $('pass').value.length > 0;
}

// First time: make a seed, show it once, have it typed back (the module
// keeps it pending until then), then log in exactly as with a typed seed.
let newStage = 'make';
function setNewStage(st)
{
    newStage = st;
    $('new-make').hidden = st !== 'make';
    $('new-show').hidden = st !== 'show';
    $('new-confirm').hidden = st !== 'confirm';
    if (st !== 'show') $('seed-new-text').textContent = ''; // shown once, then gone from the page
    if (st !== 'confirm') $('confirm').value = '';
    $('enter').hidden = mode === 'new' && st !== 'confirm';
    $('enter').disabled = !canEnter();
}

function setMode(m)
{
    mode = m;
    $('t-seed').setAttribute('aria-selected', String(m === 'seed'));
    $('t-pass').setAttribute('aria-selected', String(m === 'pass'));
    $('t-new').setAttribute('aria-selected', String(m === 'new'));
    $('f-seed').hidden = m !== 'seed';
    $('f-pass').hidden = m !== 'pass';
    $('f-new').hidden = m !== 'new';
    $('meter').classList.remove('on');
    say($('why'), '');
    setNewStage(m === 'new' && newStage === 'confirm' ? 'confirm' : 'make');
}

async function enter()
{
    $('enter').disabled = true;
    say($('why'), '');
    $('bar').style.width = '0%';
    const msg = mode === 'pass' ? {op : 'login', mode, handle : $('handle').value, pass : $('pass').value}
                                : {op : 'login', mode, text : $(mode === 'new' ? 'confirm' : 'seed').value};
    try {
        const r = await ask(msg);
        if (r.op === 'did') {
            $('seed').value = '';
            $('pass').value = '';
            newStage = 'make';
            $('confirm').value = '';
            did = r.did;
            await openSpaces();
            return;
        }
        $('meter').classList.remove('on');
        say($('why'), loginWords(r.code, r.where));
    } catch (err) {
        $('meter').classList.remove('on');
        say($('why'), `The passport could not run: ${err.message}`);
    }
    $('enter').disabled = !canEnter();
}

async function newSeed()
{
    say($('why'), '');
    try {
        const r = await ask({op : 'seed_new'});
        if (r.op !== 'seed') throw new Error(loginWords(r.code, r.where));
        setNewStage('show');
        $('seed-new-text').textContent = r.text;
        $('written').disabled = true;
        $('written-ok').checked = false;
    } catch (err) {
        say($('why'), `No seed was made: ${err.message}`);
    }
}

async function signOut()
{
    try {
        await ask({op : 'wipe'});
    } catch (err) {
        // the worker is ended below either way
    }
    worker.terminate();
    did = '';
    newStage = 'make';
    delete document.body.dataset.did;
    $('did').textContent = '';
    space = null;
    $('prompt').value = '';
    $('answer-text').textContent = '';
    $('answer').classList.remove('on');
    $('meter').classList.remove('on');
    $('bar').style.width = '0%';
    startWorker().catch((err) => say($('why'), `The passport could not start: ${err.message}`));
    setMode('seed');
    show('v-login');
}

// ---------- zxvd, through the window's /api/rpc ----------

let rpcId = 0;

// zxvd serves the root at /web/ and answers its own JSON-RPC there, or
// refuses a page that lacks this launch's token (403). Anywhere else (a
// gateway's /ipfs/<cid>/, any static host) nothing is asked: the tab is the
// whole system.
let host = null;
async function probeHost()
{
    if (host) return host;
    host = 'tab';
    document.body.dataset.host = host;
    if (!location.pathname.startsWith('/web/')) return host;
    try {
        const res = await fetch('/api/rpc', {
            method : 'POST',
            headers : {'Content-Type' : 'application/json'},
            body : JSON.stringify({jsonrpc : '2.0', id : ++rpcId, method : 'model.info', params : {}})
        });
        if (res.status === 403) host = 'zxvd';
        else if (res.ok && (await res.json().catch(() => ({}))).jsonrpc === '2.0') host = 'zxvd';
    } catch (err) {
        // nothing answers: this tab is the whole system
    }
    document.body.dataset.host = host;
    return host;
}

async function rpc(method, params)
{
    let res;
    try {
        res = await fetch('/api/rpc', {
            method : 'POST',
            headers : {'Content-Type' : 'application/json'},
            body : JSON.stringify({jsonrpc : '2.0', id : ++rpcId, method, params : params || {}})
        });
    } catch (err) {
        throw new Error('zxvd did not answer: is it still running?');
    }
    if (res.status === 403) {
        const why = await res.text();
        throw new Error(`zxvd refused this page (${why}). Open the page from the ZXV-WEB link zxvd printed, so it carries this launch's token.`);
    }
    if (!res.ok) throw new Error(`zxvd answered ${res.status} ${res.statusText}`);
    const j = await res.json();
    if (j.error) throw new Error(`${method}: ${j.error.message}`);
    return j.result;
}

const ICON = {
    home : '<svg viewBox="0 0 32 32" fill="none" stroke="#F2C230" stroke-width="1.8" stroke-linejoin="round"><path d="M5 15 16 6l11 9"/><path d="M8 13v13h16V13"/><path d="M13 26v-7h6v7"/></svg>',
    office : '<svg viewBox="0 0 32 32" fill="none" stroke="#F2C230" stroke-width="1.8"><rect x="7" y="5" width="18" height="22" rx="1.5"/><path d="M11 10h3M18 10h3M11 15h3M18 15h3M11 20h3M18 20h3"/></svg>',
    team : '<svg viewBox="0 0 32 32" fill="none" stroke="#F2C230" stroke-width="1.8"><circle cx="11" cy="12" r="4"/><circle cx="21" cy="12" r="4"/><path d="M4 26c1-5 4-7 7-7s6 2 7 7M14 26c1-5 4-7 7-7s6 2 7 7"/></svg>',
    cloudtop : '<svg viewBox="0 0 32 32" fill="none" stroke="#F2C230" stroke-width="1.8" stroke-linejoin="round"><path d="M9 23h15a5 5 0 0 0 0-10 7 7 0 0 0-13-2 6 6 0 0 0-2 12z"/></svg>',
    park : '<svg viewBox="0 0 32 32" fill="none" stroke="#F2C230" stroke-width="1.8" stroke-linejoin="round"><path d="M16 4 8 17h5l-5 7h16l-5-7h5z"/><path d="M16 24v4"/></svg>',
    companion : '<svg viewBox="0 0 32 32" fill="none" stroke="#F2C230" stroke-width="1.8" stroke-linejoin="round"><path d="M6 8h20v13H14l-6 5v-5H6z"/><path d="M11 13h10M11 17h6"/></svg>'
};

function tile(icon, name, sub, onOpen, testId)
{
    const b = document.createElement('button');
    b.className = 'tile';
    b.type = 'button';
    if (testId) b.dataset.id = testId;
    b.innerHTML = ICON[icon] || ICON.home;
    const n = document.createElement('span');
    n.className = 'name';
    n.textContent = name;
    const s = document.createElement('span');
    s.className = 'sub';
    s.textContent = sub;
    b.append(n, s);
    b.addEventListener('click', onOpen);
    return b;
}

async function openSpaces()
{
    $('did').textContent = did;
    $('did').title = did;
    document.body.dataset.did = did;
    show('v-spaces');
    const box = $('spaces');
    box.textContent = '';
    if (await probeHost() === 'tab') {
        say($('spaces-why'), '');
        const home = {id : 'home', kind : 'home', name : 'Home', role : 'owner', view : true, write : true};
        box.append(tile('home', 'Home', 'in this browser', () => openSpace(home), 'space-home'));
        return;
    }
    say($('spaces-why'), 'Asking zxvd for your spaces.', true);
    try {
        const r = await rpc('spaces.list');
        say($('spaces-why'), '');
        for (const s of r.spaces || [])
            box.append(tile(s.kind, s.name || s.id, `${s.kind}, ${s.role}`, () => openSpace(s), `space-${s.id}`));
        if (!box.children.length) say($('spaces-why'), 'zxvd lists no spaces.');
    } catch (err) {
        say($('spaces-why'), err.message);
    }
}

let model = null;
async function openSpace(s)
{
    space = s;
    $('space-title').textContent = s.name || s.id;
    $('to-space').textContent = s.name || s.id;
    show('v-space');
    const apps = $('apps');
    apps.textContent = '';
    const rights = s.write ? 'see and change' : s.view ? 'see' : 'not see';
    say($('space-note'), `A ${s.kind} space. You are its ${s.role}: you can ${rights} what is in it.`, true);
    if (host === 'tab') {
        tabModel = await tabModelEntry();
        model = tabModel ? {name : tabModel.name, can_generate : true} : null;
        say($('space-note'), 'This space lives in this browser tab: nothing in it leaves the tab.', true);
        apps.append(tile('companion', 'Companion', tabModel ? `${tabModel.name}, runs in this tab` : 'no model in this root',
                         openCompanion, 'app-companion'));
        return;
    }
    try {
        model = await rpc('model.info');
    } catch (err) {
        model = null;
        say($('space-note'), err.message);
    }
    const sub = model ? (model.can_generate ? `${model.name}, ready` : 'no model can answer yet') : 'zxvd did not answer';
    apps.append(tile('companion', 'Companion', sub, openCompanion, 'app-companion'));
}

function openCompanion()
{
    show('v-ask');
    $('answer').classList.remove('on');
    say($('ask-why'), '');
    if (host === 'tab') {
        $('thinking').textContent = 'The companion is writing, in this tab.';
        if (tabModel) {
            $('model-line').textContent =
                `${tabModel.name} (${tabModel.quant}, ${size(tabModel.bytes)}) answers in this tab. ` +
                'Nothing you ask leaves it.';
            loadTabAI().catch(() => {});
        } else {
            $('model-line').textContent = 'No model can answer here: this root\'s boot.json names none.';
        }
        $('ask').disabled = !tabModel || !$('prompt').value.trim();
        $('prompt').focus();
        return;
    }
    if (model && model.can_generate)
        $('model-line').textContent = `${model.name} (${model.arch}) answers on this machine, within the companion's token budget.`;
    else
        $('model-line').textContent = `No model here can write an answer: ${model ? model.status : 'zxvd did not answer'}`;
    $('ask').disabled = !$('prompt').value.trim();
    $('prompt').focus();
}

// ---------- the model in this tab (web/ai) ----------

let tabModel = null, tabAI = null, tabAILoading = null;

function size(n)
{
    return n >= 1048576 ? `${Math.round(n / 1048576)} MiB` : `${Math.ceil(n / 1024)} KiB`;
}

async function tabModelEntry()
{
    try {
        const res = await fetch('../boot.json', {cache : 'no-cache'});
        const m = res.ok ? ((await res.json()).models || [])[0] : null;
        if (!m || !m.path || !(m.bytes > 0) || !/^[0-9a-f]{64}$/.test(m.sha256 || '')) return null;
        return m;
    } catch (err) {
        return null;
    }
}

// A software GPU (the browser's fallback adapter, as on a server with no
// graphics card) is never faster than the CPU engine, and measuring a whole
// model on it takes minutes: such a tab runs on the CPU from the start.
async function tabDevice()
{
    try {
        const a = navigator.gpu && await navigator.gpu.requestAdapter();
        if (!a || a.isFallbackAdapter || (a.info && a.info.isFallbackAdapter)) return 'cpu';
    } catch (err) {
        return 'cpu';
    }
    return 'auto';
}

const AI_PHASE = {fetch : 'Fetching the model', check : 'Checking it against its SHA-256', load : 'Loading it into this tab'};

function loadTabAI()
{
    if (tabAILoading) return tabAILoading;
    const meter = $('ai-meter');
    meter.classList.add('on');
    $('ai-what').textContent = 'Starting the model\'s engine.';
    $('ai-bar').style.width = '0';
    tabAILoading = (async () => {
        const {openAI} = await import('../ai/zxv_ai.js');
        return openAI({
            model : Object.assign({}, tabModel, {base : new URL('../', location.href).href}),
            device : await tabDevice(),
            onProgress : (bytes, total, phase) => {
                const pct = total > 0 ? Math.min(100, Math.floor(bytes * 100 / total)) : 0;
                $('ai-bar').style.width = `${pct}%`;
                $('ai-what').textContent = phase === 'fetch' ? `${AI_PHASE.fetch}: ${size(bytes)} of ${size(total)}` :
                    (AI_PHASE[phase] || phase);
            }
        });
    })();
    tabAILoading.then((ai) => {
        tabAI = ai;
        meter.classList.remove('on');
    }, (err) => {
        tabAILoading = null;
        meter.classList.remove('on');
        say($('ask-why'), `The model could not start in this tab: ${err.message}`);
    });
    return tabAILoading;
}

const STOP_WORDS = {
    eos : '',
    max : ' It stopped at the reply\'s length limit.',
    ctx : ' It stopped where the model\'s context ran out.',
    abort : ' It was stopped.'
};

async function askInTab(prompt)
{
    const ai = tabAI || await loadTabAI();
    const box = $('answer'), text = $('answer-text');
    text.textContent = '';
    $('answer-prov').textContent = '';
    box.classList.remove('nomodel');
    box.dataset.model = 'yes';
    box.classList.add('on');
    const r = await ai.generate(prompt, {
        maxTokens : 256,
        onToken : (t) => {
            text.textContent += t;
            text.scrollTop = text.scrollHeight;
        }
    });
    text.textContent = r.text;
    box.dataset.tokens = String(r.nGen);
    box.dataset.digest = r.digest;
    const on = ai.device === 'webgpu' ? 'the GPU (WebGPU)' : 'the CPU (WebAssembly)';
    $('answer-prov').textContent = `Written by ${tabModel.name} in this tab on ${on}: ${r.nGen} tokens after a ` +
        `${r.nPrompt}-token prompt, logits digest ${r.digest}.${STOP_WORDS[r.stopped] || ''}`;
}

async function askCompanion()
{
    const prompt = $('prompt').value.trim();
    if (!prompt) return;
    if (host === 'tab') {
        if (!tabModel) return;
        $('ask').disabled = true;
        say($('ask-why'), '');
        $('thinking').classList.add('on');
        try {
            await askInTab(prompt);
        } catch (err) {
            say($('ask-why'), err.message);
        }
        $('thinking').classList.remove('on');
        $('ask').disabled = !$('prompt').value.trim();
        return;
    }
    $('ask').disabled = true;
    $('answer').classList.remove('on');
    say($('ask-why'), '');
    $('thinking').classList.add('on');
    try {
        const before = await rpc('swarm.state');
        const r = await rpc('swarm.ask', {prompt});
        const st = await rpc('swarm.state');
        const c = st.companion || {}, c0 = before.companion || {};
        const mine = c.asks === (c0.asks || 0) + 1; // no other ask came between
        const byModel = mine && c.answered_by_model === true;
        $('answer-text').textContent = r.answer;
        const box = $('answer');
        box.classList.toggle('nomodel', !byModel);
        box.dataset.model = byModel ? 'yes' : 'no';
        if (byModel) {
            const tag = c.last_tag || {}, gate = c.last_answer || {};
            box.dataset.tokens = String(tag.tokens || 0);
            $('answer-prov').textContent =
                `Written by ${st.model ? st.model.name : 'the model'} on this machine: ${tag.tokens || 0} tokens, ` +
                `${gate.charged || 0} charged to the companion's budget; answer tag ${tag.base || '?'} / ${tag.action || '?'}.`;
        } else if (!mine) {
            $('answer-prov').textContent = 'Another ask reached zxvd at the same time, so this page cannot tell who wrote this answer.';
        } else {
            $('answer-prov').textContent = 'No model wrote this: it is the swarm\'s own reply, which says why the model did not run.';
        }
        box.classList.add('on');
    } catch (err) {
        say($('ask-why'), err.message);
    }
    $('thinking').classList.remove('on');
    $('ask').disabled = !$('prompt').value.trim();
}

// ---------- wiring ----------

$('t-seed').addEventListener('click', () => setMode('seed'));
$('t-pass').addEventListener('click', () => setMode('pass'));
$('t-new').addEventListener('click', () => setMode('new'));
$('make').addEventListener('click', newSeed);
$('remake').addEventListener('click', () => setNewStage('make'));
$('written-ok').addEventListener('change', () => {
    $('written').disabled = !$('written-ok').checked;
});
$('written').addEventListener('click', () => {
    setNewStage('confirm');
    $('confirm').focus();
});
$('confirm').addEventListener('keydown', (e) => {
    if (e.key === 'Enter') {
        e.preventDefault();
        if (!$('enter').disabled) enter();
    }
});
for (const id of [ 'seed', 'handle', 'pass', 'confirm' ]) $(id).addEventListener('input', () => {
    $('enter').disabled = !canEnter();
    say($('why'), '');
});
$('seed').addEventListener('keydown', (e) => {
    if (e.key === 'Enter') {
        e.preventDefault();
        if (!$('enter').disabled) enter();
    }
});
$('pass').addEventListener('keydown', (e) => {
    if (e.key === 'Enter' && !$('enter').disabled) enter();
});
$('enter').addEventListener('click', enter);
$('new-seed').addEventListener('click', () => setMode('new'));
$('sign-out').addEventListener('click', signOut);
$('to-spaces').addEventListener('click', openSpaces);
$('to-space').addEventListener('click', () => space && openSpace(space));
$('prompt').addEventListener('input', () => {
    $('ask').disabled = !$('prompt').value.trim();
});
$('prompt').addEventListener('keydown', (e) => {
    if (e.key === 'Enter' && !e.shiftKey) {
        e.preventDefault();
        if (!$('ask').disabled) askCompanion();
    }
});
$('ask').addEventListener('click', askCompanion);

show('v-login');
startWorker().catch((err) => say($('why'), `The passport could not start: ${err.message}`));
