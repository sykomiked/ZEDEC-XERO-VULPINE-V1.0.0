// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
//
// test_browser_stress.cjs ZXVD MODELS_DIR WEB_ROOT SCRATCH — the browser path
// under abuse, in headless Chromium. Each part tries to break one promise of
// the path and checks that the page refuses in words, recovers, or stays
// clean:
//
//   1 credentials   empty, 10 KB pasted, Unicode, control bytes, every seed
//                   position mistyped, passphrase and handle at and past
//                   their limits
//   2 boot          reload and close mid-boot; a passport wasm whose hash is
//                   not the manifest's, one missing, one cut short, one that
//                   never arrives, a broken connection: each refused in
//                   words, none waits forever
//   3 zxvd down     killed in the middle of an ask: the page says so, and
//                   asks again once zxvd is back (same port and token)
//   4 no model      an empty models folder and corrupt model files: an
//                   honest "no model wrote this", zxvd still up
//   5 concurrency   tabs making seeds, logging in and asking at once: no
//                   pending seed crosses tabs, a double click makes one seed,
//                   and every /api request carries the token (one without it
//                   is refused)
//   6 soak          100 rounds of log in, ask, sign out: no page errors, the
//                   page's JS heap and zxvd's RSS stay within stated bounds
//   7 secrets       after a new seed is confirmed, and after memory pressure,
//                   the seed is in no DOM, storage, URL, console line or JS
//                   heap of the page
//
// SOAK_ROUNDS (default 100) sets part 6's rounds; ZXV_STRESS_PARTS (e.g.
// "1,5") runs only those parts. Run by tests/browser/test_browser.sh.
'use strict';

const {chromium} = require('playwright');
const fs = require('fs');
const http = require('http');
const net = require('net');
const path = require('path');
const {SEED_TEXT, DID_SEED, PASS, HANDLE, DID_PASS, SEED_RX, tally, check, rpcSocket, startZxvd, watchPage, askOnPage,
       rssKiB} = require('./lib.cjs');

const B32 = '0123456789ABCDEFGHJKMNPQRSTVWXYZ';
const SOAK = Number(process.env.SOAK_ROUNDS || 100);
// Part 6's bounds, after 10 warm-up rounds: the page's JS heap (after a
// forced GC) and zxvd's resident set may not grow by more than these.
const HEAP_BOUND = 2 * 1024 * 1024;
const RSS_BOUND_KIB = 8 * 1024;
const PARTS = (process.env.ZXV_STRESS_PARTS || '1,2,3,4,5,6,7').split(',').map(Number);

function freePort()
{
    return new Promise((resolve, reject) => {
        const s = net.createServer();
        s.listen(0, '127.0.0.1', () => {
            const p = s.address().port;
            s.close(() => resolve(p));
        });
        s.on('error', reject);
    });
}

// A raw request; the body goes with its Content-Length unless the headers
// say otherwise.
function httpReq(z, method, p, headers, body)
{
    const h = Object.assign({}, headers || {});
    if (body && !h['Transfer-Encoding']) h['Content-Length'] = Buffer.byteLength(body);
    return new Promise((resolve, reject) => {
        const r = http.request({host : '127.0.0.1', port : z.port, method, path : p, headers : h}, (res) => {
            let b = '';
            res.on('data', (d) => (b += d));
            res.on('end', () => resolve({status : res.statusCode, body : b}));
        });
        r.on('error', reject);
        if (body) r.write(body);
        r.end();
    });
}

// Open the ZXV-WEB link in a new tab and wait for the login.
async function openSession(ctx, z, label)
{
    const page = await ctx.newPage();
    const watch = watchPage(page, label);
    await page.goto(z.url);
    await page.waitForURL(/\/web\/session\/index\.html$/, {timeout : 60000});
    await page.waitForSelector('#v-login.on');
    return {page, watch};
}

// Put text in a field the way a paste does (every character, control bytes
// too), and tell the page.
async function paste(page, sel, text)
{
    await page.$eval(sel, (e, v) => {
        e.value = v;
        e.dispatchEvent(new Event('input', {bubbles : true}));
    }, text);
}

// Log in; resolves to {did} or {why} (the page's words), or {disabled}.
async function tryLogin(page, mode, a, b)
{
    await page.click(mode === 'pass' ? '#t-pass' : '#t-seed');
    if (mode === 'pass') {
        await paste(page, '#handle', a);
        await paste(page, '#pass', b);
    } else {
        await paste(page, '#seed', a);
    }
    if (await page.locator('#enter').isDisabled()) return {disabled : true};
    await page.click('#enter');
    await page.waitForFunction(() => document.body.dataset.did || document.getElementById('why').textContent, null,
                               {timeout : 120000});
    const did = await page.getAttribute('body', 'data-did');
    if (did) {
        await page.click('#sign-out');
        await page.waitForSelector('#v-login.on');
        return {did};
    }
    return {why : await page.locator('#why').textContent()};
}

async function newSeedShown(page)
{
    await page.click('#t-new');
    await page.waitForSelector('#new-make:not([hidden])');
    await page.click('#make');
    await page.waitForSelector('#new-show:not([hidden])');
    return page.locator('#seed-new-text').textContent();
}

async function confirmSeed(page, text)
{
    if (await page.locator('#new-show:not([hidden])').count()) { // a second try is already at the confirm step
        await page.check('#written-ok');
        await page.click('#written');
    }
    await page.waitForSelector('#new-confirm:not([hidden])');
    await paste(page, '#confirm', text);
    await page.click('#enter');
    await page.waitForFunction(() => document.body.dataset.did || document.getElementById('why').textContent, null,
                               {timeout : 120000});
    const did = await page.getAttribute('body', 'data-did');
    return did ? {did} : {why : await page.locator('#why').textContent()};
}

function mistype(text, i)
{
    const c = text[i];
    const k = (B32.indexOf(c) + 1 + (i % 7)) % 32;
    return text.slice(0, i) + (B32[k] === c ? B32[(k + 1) % 32] : B32[k]) + text.slice(i + 1);
}

// ---------------------------------------------------------------- 1

async function partCredentials(browser, z)
{
    console.log('-- 1 credentials');
    const ctx = await browser.newContext();
    const {page, watch} = await openSession(ctx, z, 'credentials');
    let r;

    r = await tryLogin(page, 'seed', '');
    check(r.disabled, 'an empty seed cannot be sent');
    r = await tryLogin(page, 'seed', ' \n\t  ');
    check(r.disabled, 'a seed of only spaces and line breaks cannot be sent');

    r = await tryLogin(page, 'seed', 'A'.repeat(10240));
    check(r.why && /longer than any seed/.test(r.why), `10 KB pasted as a seed is refused in words: "${r.why}"`);
    r = await tryLogin(page, 'seed', (SEED_TEXT + ' ').repeat(160));
    check(r.why && /longer than any seed/.test(r.why), '10 KB of seeds pasted is refused, not read as the first one');

    // what people paste: full-width letters, other dashes, line breaks
    const wide = SEED_TEXT.replace(/[0-9A-Z]/g, (c) => String.fromCharCode(c.charCodeAt(0) + 0xfee0));
    r = await tryLogin(page, 'seed', wide);
    check(r.did === DID_SEED, `a seed in full-width letters is the same seed (${r.did || r.why})`);
    r = await tryLogin(page, 'seed', SEED_TEXT.replace(/-/g, (_, i) => [ '–', '—', '−', '­' ][i % 4]));
    check(r.did === DID_SEED, `a seed with en and em dashes, minus signs and soft hyphens is the same seed (${r.did || r.why})`);
    r = await tryLogin(page, 'seed', '\n  ' + SEED_TEXT.slice(0, 35) + '\r\n' + SEED_TEXT.slice(36) + '\n\n');
    check(r.did === DID_SEED, `a seed written over two lines is the same seed (${r.did || r.why})`);

    // control bytes and other characters are refused, never skipped
    for (const [ what, text ] of [
             [ 'a NUL and more after the seed', SEED_TEXT + '\u0000XYZ' ],
             [ 'a NUL after the seed', SEED_TEXT + '\u0000' ],
             [ 'a bell inside the seed', SEED_TEXT.slice(0, 20) + '\u0007' + SEED_TEXT.slice(20) ],
             [ 'an escape sequence inside the seed', SEED_TEXT.slice(0, 20) + '\u001b[31m' + SEED_TEXT.slice(20) ],
             [ 'a right-to-left override', '‮' + SEED_TEXT ],
             [ 'a zero-width space inside a group', SEED_TEXT.slice(0, 2) + '​' + SEED_TEXT.slice(2) ],
             [ 'an emoji', SEED_TEXT.slice(0, 10) + '\u{1F98A}' + SEED_TEXT.slice(10) ],
             [ 'a lone surrogate', SEED_TEXT + '\ud800' ],
             [ 'a Cyrillic letter that looks Latin', SEED_TEXT.replace('E', 'Е') ],
         ]) {
        r = await tryLogin(page, 'seed', text);
        check(!r.did && r.why && /not a seed/.test(r.why), `${what} is refused in words: "${r.why || r.did}"`);
    }

    // every position mistyped
    const positions = [];
    for (let i = 0; i < SEED_TEXT.length; i++)
        if (SEED_TEXT[i] !== '-') positions.push(i);
    let refused = 0;
    const leaks = [];
    for (const i of positions) {
        r = await tryLogin(page, 'seed', mistype(SEED_TEXT, i));
        if (!r.did && r.why && /typo|not a seed/.test(r.why)) refused++;
        else leaks.push(`${i}: ${r.did || r.why}`);
    }
    check(refused === positions.length,
          `each of the ${positions.length} seed positions mistyped is refused in words${leaks.length ? ': ' + leaks.join('; ') : ''}`);

    // passphrase and handle
    r = await tryLogin(page, 'pass', '', PASS);
    check(r.disabled, 'no handle: cannot be sent');
    r = await tryLogin(page, 'pass', HANDLE, '');
    check(r.disabled, 'no passphrase: cannot be sent');
    r = await tryLogin(page, 'pass', HANDLE, 'x'.repeat(15));
    check(r.why && /at least 16 bytes/.test(r.why), `15 bytes are too few: "${r.why}"`);
    r = await tryLogin(page, 'pass', HANDLE, 'é'.repeat(7) + 'x');
    check(r.why && /at least 16 bytes/.test(r.why), '8 characters that are 15 bytes are too few, and the page says bytes');
    let t = Date.now();
    r = await tryLogin(page, 'pass', HANDLE, 'é'.repeat(8));
    check(/^did:zxv:/.test(r.did || ''), `8 accented letters (16 bytes) are enough (${Date.now() - t} ms)`);
    r = await tryLogin(page, 'pass', HANDLE, 'p'.repeat(10240));
    check(!r.did && r.why && /10240 bytes: it can be at most 256/.test(r.why),
          `a 10 KB passphrase is refused, not cut to fit: "${r.why || r.did}"`);
    r = await tryLogin(page, 'pass', HANDLE, 'é'.repeat(129));
    check(!r.did && r.why && /258 bytes/.test(r.why), '129 accented letters (258 bytes) are refused');
    const p256 = 'q'.repeat(256);
    r = await tryLogin(page, 'pass', 'h'.repeat(64), p256);
    check(/^did:zxv:/.test(r.did || ''), 'a 256-byte passphrase with a 64-byte handle is taken');
    r = await tryLogin(page, 'pass', 'h'.repeat(65), PASS);
    check(!r.did && r.why && /handle of 1 to 64 bytes/.test(r.why), `a 65-byte handle is refused: "${r.why || r.did}"`);
    r = await tryLogin(page, 'pass', 'h'.repeat(10240), PASS);
    check(!r.did && r.why && /handle of 1 to 64 bytes/.test(r.why),
          `a 10 KB handle is refused as a handle: "${r.why || r.did}"`);
    r = await tryLogin(page, 'pass', HANDLE, PASS);
    check(r.did === DID_PASS, 'after all that, the passphrase still gives its DID');
    const withNul = await tryLogin(page, 'pass', HANDLE, PASS + '\u0000');
    check(/^did:zxv:/.test(withNul.did || '') && withNul.did !== DID_PASS,
          'a NUL at the end of a passphrase is part of it (bytes, not C text): another identity');
    const spaced = await tryLogin(page, 'pass', HANDLE, ' ' + PASS + ' ');
    check(/^did:zxv:/.test(spaced.did || '') && spaced.did !== DID_PASS,
          'spaces around a passphrase are kept, as typed: another identity');

    // a typed seed does not survive a reload of the page
    await page.click('#t-seed');
    await page.fill('#seed', SEED_TEXT);
    await page.click('#t-pass');
    await page.fill('#pass', 'not sent');
    await page.reload();
    await page.waitForSelector('#v-login.on');
    check((await page.locator('#seed').inputValue()) === '' && (await page.locator('#pass').inputValue()) === '',
          'a seed and passphrase typed but not sent are gone after a reload');

    await page.waitForTimeout(100);
    check(watch.problems.length === 0, `credentials: no page errors${watch.problems.length ? ': ' + watch.problems.join('; ') : ''}`);
    await ctx.close();
}

// ---------------------------------------------------------------- 2

async function bootWhy(page, ms)
{
    await page.waitForFunction(() => document.getElementById('why') && document.getElementById('why').textContent, null,
                               {timeout : ms});
    return page.locator('#why').textContent();
}

async function partBoot(browser, z, root)
{
    console.log('-- 2 interrupted boot');
    const WASM = '**/web/session/zxv_passport.wasm';
    const good = fs.readFileSync(path.join(root, 'session', 'zxv_passport.wasm'));
    const sessionUrl = `${z.origin}/web/session/index.html#token=${z.token}`;

    // reload in the middle of the boot
    {
        const ctx = await browser.newContext();
        const page = await ctx.newPage();
        const watch = watchPage(page, 'reload');
        // what the first boot asked for: the reload cancels what was still loading
        const first = new Set(), cancelled = [];
        const onReq = (r) => first.add(r);
        page.on('request', onReq);
        page.on('requestfailed', (r) => {
            if (first.has(r) && (r.failure() || {}).errorText === 'net::ERR_ABORTED') cancelled.push(r.url());
        });
        await page.goto(z.url);
        await page.waitForTimeout(150);
        page.off('request', onReq);
        await page.reload();
        await page.waitForURL(/\/web\/session\/index\.html$/, {timeout : 60000});
        await page.waitForSelector('#v-login.on');
        const st = await page.evaluate(async () => (await fetch('/api/rpc', {
                                                       method : 'POST',
                                                       headers : {'Content-Type' : 'application/json'},
                                                       body : '{"jsonrpc":"2.0","id":1,"method":"spaces.list","params":{}}'
                                                   })).status);
        check(st === 200, 'a reload in the middle of the boot boots again, and the tab keeps its token');
        await page.waitForTimeout(100);
        const probs = watch.problems.filter((p) => !(/ERR_ABORTED/.test(p) && cancelled.some((u) => p.includes(u))));
        check(probs.length === 0,
              `reload: no page errors besides the ${cancelled.length} first-boot loads the reload cancelled${probs.length ? ': ' + probs.join('; ') : ''}`);
        await ctx.close();
    }
    // close in the middle of the boot: zxvd serves the next tab
    {
        const ctx = await browser.newContext();
        for (const ms of [ 20, 120, 400 ]) {
            const page = await ctx.newPage();
            page.goto(z.url).catch(() => {});
            await page.waitForTimeout(ms);
            await page.close();
        }
        const {page, watch} = await openSession(ctx, z, 'after-close');
        check(z.proc.exitCode === null, 'tabs closed mid-boot leave zxvd running');
        const r = await tryLogin(page, 'seed', SEED_TEXT);
        check(r.did === DID_SEED, 'and the next tab boots and logs in');
        check(watch.problems.length === 0, `after close: no page errors${watch.problems.length ? ': ' + watch.problems.join('; ') : ''}`);
        await ctx.close();
    }

    // the passport wasm, broken four ways: at the boot and in the page
    const broken = [
        [ 'a wasm whose hash is not the manifest\'s', (route) => {
             const b = Buffer.from(good);
             b[b.length >> 1] ^= 1;
             return route.fulfill({status : 200, contentType : 'application/wasm', body : b});
         },
          /does not match its SHA-256/, /does not match the hash the boot checked/ ],
        [ 'a missing wasm', (route) => route.fulfill({status : 404, contentType : 'text/plain', body : 'not found'}),
          /404/, /404/ ],
        [ 'a wasm cut short', (route) => route.fulfill({status : 200, contentType : 'application/wasm', body : good.subarray(0, good.length >> 1)}),
          new RegExp(`only ${good.length >> 1} of its ${good.length} bytes arrived`), /does not match the hash the boot checked/ ],
        [ 'a connection broken while the wasm loads', (route) => route.abort('connectionreset'),
          /connection broke/, /could not be read/ ],
    ];
    for (const [ what, handler, atBoot, inPage ] of broken) {
        const ctx = await browser.newContext();
        await ctx.route(WASM, handler);
        const page = await ctx.newPage();
        await page.goto(z.url);
        const why = await bootWhy(page, 20000);
        await page.waitForTimeout(2500);
        check(atBoot.test(why) && /\/web\/$/.test(page.url()),
              `boot: ${what} stops the boot in words: "${why}"`);
        const p2 = await ctx.newPage();
        await p2.goto(sessionUrl);
        const why2 = await bootWhy(p2, 20000);
        check(inPage.test(why2) && /could not start/.test(why2), `page: ${what} is refused: "${why2}"`);
        await paste(p2, '#seed', SEED_TEXT);
        await p2.click('#enter');
        await p2.waitForFunction(() => /could not run/.test(document.getElementById('why').textContent), null, {timeout : 10000});
        check((await p2.getAttribute('body', 'data-did')) === null, `page: with ${what}, nobody is logged in and it says why`);
        await ctx.close();
    }

    // a wasm that never arrives: words, not a wait
    {
        const ctx = await browser.newContext();
        await ctx.route(WASM, () => {}); // never answered
        const page = await ctx.newPage();
        const t0 = Date.now();
        await page.goto(z.url);
        const why = await bootWhy(page, 25000);
        check(/nothing arrived for 15 s/.test(why), `boot: a wasm that never arrives ends the boot in words (${Date.now() - t0} ms): "${why}"`);
        const p2 = await ctx.newPage();
        const t1 = Date.now();
        await p2.goto(sessionUrl);
        const why2 = await bootWhy(p2, 40000);
        check(/did not arrive within 30 s/.test(why2), `page: a wasm that never arrives is refused (${Date.now() - t1} ms): "${why2}"`);
        await ctx.close();
    }
}

// ---------------------------------------------------------------- 3

async function openCompanion(page)
{
    await page.waitForSelector('#spaces .tile[data-id="space-home"]');
    await page.click('#spaces .tile[data-id="space-home"]');
    await page.waitForSelector('#apps .tile[data-id="app-companion"]');
    await page.click('#apps .tile[data-id="app-companion"]');
    await page.waitForSelector('#v-ask.on');
}

// From the companion back to the login: up to the space, the spaces, sign out.
async function signOutFromAsk(page)
{
    await page.click('#to-space');
    await page.waitForSelector('#v-space.on');
    await page.click('#to-spaces');
    await page.waitForSelector('#v-spaces.on');
    await page.click('#sign-out');
    await page.waitForSelector('#v-login.on');
}

async function loginKeep(page, text)
{
    await page.click('#t-seed');
    await paste(page, '#seed', text);
    await page.click('#enter');
    await page.waitForSelector('#v-spaces.on', {timeout : 60000});
}

async function partZxvdDown(browser, zxvd, models, root)
{
    console.log('-- 3 zxvd killed mid-ask');
    const port = await freePort();
    const token = 'c0ffee'.padEnd(64, '7');
    let z = await startZxvd(zxvd, models, root, {port, token});
    const ctx = await browser.newContext();
    const {page, watch} = await openSession(ctx, z, 'down');
    await loginKeep(page, SEED_TEXT);
    await openCompanion(page);

    // the ask is on its way when zxvd dies
    process.kill(z.proc.pid, 'SIGSTOP');
    await page.fill('#prompt', 'Are you still there?');
    await page.click('#ask');
    await page.waitForTimeout(300);
    check(await page.locator('#thinking.on').count() === 1, 'the page shows it is waiting for the answer');
    process.kill(z.proc.pid, 'SIGKILL');
    await z.stop();
    const t0 = Date.now();
    await page.waitForFunction(() => document.getElementById('ask-why').textContent, null, {timeout : 15000});
    const why = await page.locator('#ask-why').textContent();
    check(/zxvd (did not answer|stopped in the middle)/.test(why), `zxvd killed mid-ask: the page says so (${Date.now() - t0} ms): "${why}"`);
    check(await page.locator('#thinking.on').count() === 0 && !(await page.locator('#ask').isDisabled()),
          'the page stops waiting and lets the person ask again');
    const r2 = await page.evaluate(async () => {
        try {
            await fetch('/api/rpc', {method : 'POST', body : '{}'});
            return 'answered';
        } catch (e) {
            return 'refused';
        }
    });
    check(r2 === 'refused', 'zxvd is really down');

    // back on the same port with the same token: the same page asks again
    z = await startZxvd(zxvd, models, root, {port, token, home : z.home});
    const asked = await askOnPage(page, 'Are you back?');
    check(asked.byModel, `zxvd back: the same page asks again and the model answers (${asked.tries} tries)`);
    check((await page.locator('#ask-why').textContent()) === '', 'and the earlier failure is cleared');

    // back with another token: refused in words that say what to do
    await z.stop('SIGKILL');
    z = await startZxvd(zxvd, models, root, {port, token : 'ab'.repeat(32), home : z.home});
    await page.fill('#prompt', 'And now?');
    await page.click('#ask');
    await page.waitForFunction(() => document.getElementById('ask-why').textContent, null, {timeout : 15000});
    const why3 = await page.locator('#ask-why').textContent();
    check(/refused this page \(bad token\).*ZXV-WEB link/.test(why3), `zxvd back with a new token: the page says to open its new link: "${why3}"`);

    // down while logging in: the spaces say so
    await signOutFromAsk(page);
    await z.stop('SIGKILL');
    await loginKeep(page, SEED_TEXT);
    await page.waitForFunction(() => document.getElementById('spaces-why').textContent.includes('did not answer'), null,
                               {timeout : 15000});
    check(true, `zxvd down at login: "${await page.locator('#spaces-why').textContent()}"`);
    const real = watch.problems.filter((p) => !/request failed: .*\/api\/rpc/.test(p) && !/console: Failed to load resource/.test(p));
    check(real.length === 0, `zxvd down: no page errors besides the refused requests${real.length ? ': ' + real.join('; ') : ''}`);
    await ctx.close();
}

// ---------------------------------------------------------------- 4

async function partNoModel(browser, zxvd, root, scratch)
{
    console.log('-- 4 no model, corrupt models');
    const real = fs.readFileSync(process.env.ZXV_TEST_MODEL);
    const flipped = Buffer.from(real);
    for (let i = 64; i < flipped.length; i += 97) flipped[i] ^= 0x5a;
    const noise = Buffer.alloc(65536);
    for (let i = 0; i < noise.length; i++) noise[i] = (i * 2654435761) >>> 24;
    const header = Buffer.alloc(24);
    header.write('GGUF', 0, 'latin1');
    header.writeUInt32LE(3, 4);
    header.writeBigUInt64LE(0xffffffffffffn, 8); // more tensors than any file holds
    header.writeBigUInt64LE(0xffffffffffffn, 16);
    const cases = [
        [ 'no model', null ],
        [ 'a model cut in half', real.subarray(0, real.length >> 1) ],
        [ 'a model with scrambled bytes', flipped ],
        [ 'random bytes named .gguf', noise ],
        [ 'a GGUF header that claims 2^48 tensors', header ],
        [ 'an empty .gguf', Buffer.alloc(0) ],
    ];
    for (const [ what, bytes ] of cases) {
        const dir = path.join(scratch, 'models-' + what.replace(/[^a-z0-9]+/gi, '-'));
        fs.rmSync(dir, {recursive : true, force : true});
        fs.mkdirSync(dir, {recursive : true});
        if (bytes) fs.writeFileSync(path.join(dir, 'model.gguf'), bytes);
        const z = await startZxvd(zxvd, dir, root);
        const info = (await rpcSocket(z.sock, 'model.info')).result;
        check(info.can_generate === false && (!/empty/.test(what) || /is empty/.test(info.status)),
              `${what}: zxvd says no model can answer, and why: "${info.status}"`);
        const ctx = await browser.newContext();
        const {page, watch} = await openSession(ctx, z, what);
        await loginKeep(page, SEED_TEXT);
        await page.click('#spaces .tile[data-id="space-home"]');
        await page.waitForSelector('#apps .tile[data-id="app-companion"]');
        const sub = await page.locator('#apps .tile[data-id="app-companion"] .sub').textContent();
        await page.click('#apps .tile[data-id="app-companion"]');
        const line = await page.locator('#model-line').textContent();
        check(/no model can answer yet/.test(sub) && /^No model here can write an answer/.test(line),
              `${what}: the companion says so before the ask: "${line}"`);
        const asked = await askOnPage(page, 'Hello?', 1);
        const prov = await page.locator('#answer-prov').textContent();
        const c = (await rpcSocket(z.sock, 'swarm.state')).result.companion;
        check(!asked.byModel && /^No model wrote this/.test(prov) && c.answered_by_model === false && z.proc.exitCode === null,
              `${what}: the answer says no model wrote it, zxvd agrees and is still running`);
        check(watch.problems.length === 0, `${what}: no page errors${watch.problems.length ? ': ' + watch.problems.join('; ') : ''}`);
        await ctx.close();
        await z.stop();
    }
}

// ---------------------------------------------------------------- 5

async function partConcurrency(browser, z)
{
    console.log('-- 5 concurrency');
    const ctx = await browser.newContext();
    const tabs = await Promise.all([ 0, 1, 2, 3 ].map((i) => openSession(ctx, z, `tab${i}`)));
    const [ A, B, C, D ] = tabs.map((t) => t.page);

    // a double click on "make" makes one seed: the one shown is the pending one
    await A.click('#t-new');
    await A.waitForSelector('#new-make:not([hidden])');
    await A.evaluate(() => {
        const b = document.getElementById('make');
        b.click();
        b.click();
        b.click();
    });
    await A.waitForSelector('#new-show:not([hidden])');
    await A.waitForTimeout(500);
    const seedA = await A.locator('#seed-new-text').textContent();

    // A and B make seeds while C logs in with a seed and D with a passphrase
    const [ seedB, rc, rd ] = await Promise.all([ newSeedShown(B), tryLogin(C, 'seed', SEED_TEXT), tryLogin(D, 'pass', HANDLE, PASS) ]);
    check(SEED_RX.test(seedA) && SEED_RX.test(seedB) && seedA !== seedB, 'two tabs make two different seeds at once');
    check(rc.did === DID_SEED && rd.did === DID_PASS, 'meanwhile two other tabs log in to their own DIDs');

    // no pending seed crosses tabs
    const [ xa, xb ] = await Promise.all([ confirmSeed(A, seedB), confirmSeed(B, seedA) ]);
    check(!xa.did && /not the seed you were shown/.test(xa.why || '') && !xb.did && /not the seed you were shown/.test(xb.why || ''),
          'each tab refuses the seed the other tab made');
    const [ oka, okb ] = await Promise.all([ confirmSeed(A, seedA), confirmSeed(B, seedB) ]);
    check(/^did:zxv:/.test(oka.did || '') && /^did:zxv:/.test(okb.did || '') && oka.did !== okb.did,
          `each tab takes its own seed after a double click and a race (${(oka.did || oka.why || '').slice(0, 24)}..., ${(okb.did || okb.why || '').slice(0, 24)}...)`);
    const [ ra, rb ] = await Promise.all([ tryLogin(C, 'seed', seedA), tryLogin(D, 'seed', seedB) ]);
    check(ra.did === oka.did && rb.did === okb.did, 'each new seed logs in to its DID from another tab');

    // all four ask at once
    await Promise.all([ C, D ].map((p) => loginKeep(p, SEED_TEXT)));
    for (const p of [ A, B, C, D ]) await openCompanion(p);
    const asks0 = (await rpcSocket(z.sock, 'swarm.state')).result.companion.asks;
    const out = await Promise.all([ A, B, C, D ].map((p, i) => askOnPage(p, `Tab ${i} asks at the same time`, 1)));
    const provs = await Promise.all([ A, B, C, D ].map((p) => p.locator('#answer-prov').textContent()));
    const asks1 = (await rpcSocket(z.sock, 'swarm.state')).result.companion.asks;
    check(asks1 - asks0 === 4, `zxvd counted the four asks (${asks1 - asks0})`);
    check(out.every((o) => o.text.length > 0) &&
              provs.every((p) => /^(Written by|Another ask reached zxvd|No model wrote this)/.test(p)),
          `every tab shows an answer and an honest line on who wrote it (${provs.map((p) => p.split(' ').slice(0, 2).join(' ')).join(' | ')})`);
    console.log(`  note tabs whose answer the page could attribute to the model: ${out.filter((o) => o.byModel).length} of 4`);
    const probs = tabs.flatMap((t) => t.watch.problems);
    check(probs.length === 0, `concurrency: no page errors${probs.length ? ': ' + probs.join('; ') : ''}`);

    // the token: on every /api request the tabs made, and required
    const api = tabs.flatMap((t) => t.watch.requests).filter((r) => new URL(r.url()).pathname.startsWith('/api/'));
    const carried = api.filter((r) => r.headers()['x-zxv-token'] === z.token).length;
    check(api.length > 20 && carried === api.length, `every one of the tabs' ${api.length} /api requests carried the token`);
    const body = '{"jsonrpc":"2.0","id":1,"method":"spaces.list","params":{}}';
    const host = `127.0.0.1:${z.port}`;
    const json = {'Content-Type' : 'application/json', Host : host};
    const cases = [
        [ 'no token', Object.assign({}, json), /token required/ ],
        [ 'a wrong token', Object.assign({'X-ZXV-Token' : 'f'.repeat(64)}, json), /bad token/ ],
        [ 'a short token', Object.assign({'X-ZXV-Token' : z.token.slice(0, 63)}, json), /bad token/ ],
        [ 'the token from another site', Object.assign({'X-ZXV-Token' : z.token, Origin : 'http://evil.example'}, json), /origin not allowed/ ],
        [ 'the token under another Host name', Object.assign({}, json, {'X-ZXV-Token' : z.token, Host : `evil.example:${z.port}`}), /host not allowed/ ],
        [ 'the token, sent cross-site', Object.assign({'X-ZXV-Token' : z.token, 'Sec-Fetch-Site' : 'cross-site'}, json), /cross-site request refused/ ],
        [ 'the token and a chunked body', Object.assign({'X-ZXV-Token' : z.token, 'Transfer-Encoding' : 'chunked'}, json), /malformed request/ ],
    ];
    for (const [ what, headers, rx ] of cases) {
        const r = await httpReq(z, 'POST', '/api/rpc', headers, body);
        check(r.status === 403 && rx.test(r.body), `/api/rpc with ${what} is refused (${r.status} ${r.body})`);
    }
    const okr = await httpReq(z, 'POST', '/api/rpc', Object.assign({'X-ZXV-Token' : z.token}, json), body);
    check(okr.status === 200, 'and with the token it answers');
    const webEvil = await httpReq(z, 'GET', '/web/boot.json', {Host : `evil.example:${z.port}`});
    check(webEvil.status === 403, `the static files refuse another Host name too (${webEvil.status})`);
    const xhr = await A.evaluate(() => new Promise((resolve) => {
        const x = new XMLHttpRequest();
        x.open('POST', '/api/rpc');
        x.onload = () => resolve(`${x.status} ${x.responseText}`);
        x.onerror = () => resolve('error');
        x.send('{"jsonrpc":"2.0","id":1,"method":"spaces.list","params":{}}');
    }));
    check(/^403 token required/.test(xhr), `a request from the page that skips the token is refused (${xhr})`);
    // a tab opened without the link's token can log in (that is local) but zxvd refuses it
    const bare = await ctx.newPage();
    const bw = watchPage(bare, 'bare');
    await bare.goto(`${z.origin}/web/session/index.html`);
    await loginKeep(bare, SEED_TEXT);
    await bare.waitForFunction(() => document.getElementById('spaces-why').textContent.includes('refused'), null, {timeout : 10000});
    const bareWhy = await bare.locator('#spaces-why').textContent();
    check(/\((token required|bad token)\).*ZXV-WEB link/.test(bareWhy), `a tab without the token is refused by zxvd, and told to open the ZXV-WEB link: "${bareWhy}"`);
    const bareProbs = bw.problems.filter((p) => !/status of 403/.test(p));
    check(bareProbs.length === 0, `the tab without the token: no errors besides the 403s${bareProbs.length ? ': ' + bareProbs.join('; ') : ''}`);
    await ctx.close();
}

// ---------------------------------------------------------------- 6

async function heapUsed(cdp)
{
    await cdp.send('HeapProfiler.collectGarbage');
    await cdp.send('HeapProfiler.collectGarbage');
    return (await cdp.send('Runtime.getHeapUsage')).usedSize;
}

async function partSoak(browser, z)
{
    console.log(`-- 6 soak: ${SOAK} rounds`);
    const ctx = await browser.newContext();
    const {page, watch} = await openSession(ctx, z, 'soak');
    const cdp = await ctx.newCDPSession(page);
    await cdp.send('HeapProfiler.enable');
    const t0 = Date.now();
    let heap10 = 0, rss10 = 0, byModel = 0;
    const ok = [];
    for (let i = 1; i <= SOAK; i++) {
        await loginKeep(page, SEED_TEXT);
        const d = await page.getAttribute('body', 'data-did');
        await openCompanion(page);
        const a = await askOnPage(page, `Round ${i}: say something.`, 1);
        if (a.byModel) byModel++;
        await signOutFromAsk(page);
        ok.push(d === DID_SEED && a.text.length > 0);
        if (i === 10) {
            heap10 = await heapUsed(cdp);
            rss10 = rssKiB(z.proc.pid);
        }
    }
    const heapN = await heapUsed(cdp);
    const rssN = rssKiB(z.proc.pid);
    const workers = page.workers().length;
    check(ok.every(Boolean), `${SOAK} rounds of log in, ask, sign out all completed (${Date.now() - t0} ms; the model wrote ${byModel} answers, the budget held back the rest)`);
    check(workers <= 1, `one passport worker at the end, not one per round (${workers})`);
    if (SOAK >= 20) {
        check(heapN - heap10 <= HEAP_BOUND,
              `the page's JS heap after GC: ${(heap10 / 1048576).toFixed(2)} MiB at round 10, ${(heapN / 1048576).toFixed(2)} MiB at round ${SOAK} (bound: +${HEAP_BOUND / 1048576} MiB)`);
        check(rssN - rss10 <= RSS_BOUND_KIB,
              `zxvd's RSS: ${rss10} KiB at round 10, ${rssN} KiB at round ${SOAK} (bound: +${RSS_BOUND_KIB} KiB)`);
    }
    await page.waitForTimeout(100);
    check(watch.problems.length === 0, `soak: no page errors${watch.problems.length ? ': ' + watch.problems.join('; ') : ''}`);
    await ctx.close();
}

// ---------------------------------------------------------------- 7

async function heapStrings(cdp)
{
    const parts = [];
    const on = (e) => parts.push(e.chunk);
    cdp.on('HeapProfiler.addHeapSnapshotChunk', on);
    await cdp.send('HeapProfiler.collectGarbage');
    await cdp.send('HeapProfiler.takeHeapSnapshot', {reportProgress : false});
    cdp.off('HeapProfiler.addHeapSnapshotChunk', on);
    return parts.join('');
}

function variants(seed)
{
    const groups = seed.split('-');
    return [ seed, seed.toLowerCase(), groups.join(' '), groups.join(''), groups.join('').toLowerCase(), groups.slice(0, 4).join('-') ];
}

async function everywhere(page)
{
    return page.evaluate(async () => {
        const out = [];
        out.push(document.documentElement.outerHTML);
        for (const e of document.querySelectorAll('input, textarea')) out.push(e.value);
        for (const s of [ localStorage, sessionStorage ])
            for (let i = 0; i < s.length; i++) out.push(s.key(i), s.getItem(s.key(i)));
        out.push(location.href, document.cookie, document.title, String(history.state));
        const dbs = indexedDB.databases ? await indexedDB.databases() : [];
        out.push(JSON.stringify(dbs));
        const ck = self.caches ? await caches.keys() : [];
        out.push(JSON.stringify(ck));
        return {text : out.join('\n'), dbs : dbs.length, caches : ck.length, local : localStorage.length};
    });
}

async function partSecrets(browser, z)
{
    console.log('-- 7 the seed after it is confirmed');
    const ctx = await browser.newContext();
    const {page, watch} = await openSession(ctx, z, 'secrets');
    const cdp = await ctx.newCDPSession(page);
    await cdp.send('HeapProfiler.enable');
    const seed = await newSeedShown(page);
    const typed = seed.toLowerCase().replace(/-/g, ' ');
    const r = await confirmSeed(page, typed);
    check(/^did:zxv:/.test(r.did || ''), 'a new seed confirmed');
    const found = async (when) => {
        const e = await everywhere(page);
        const heap = await heapStrings(cdp);
        const where = [];
        for (const v of variants(seed)) {
            if (e.text.includes(v)) where.push(`page: ${v.slice(0, 12)}`);
            if (heap.includes(v)) where.push(`JS heap: ${v.slice(0, 12)}`);
            for (const line of watch.console)
                if (line.includes(v)) where.push(`console: ${v.slice(0, 12)}`);
        }
        if (heap.includes(typed)) where.push('JS heap: the typed confirmation');
        check(where.length === 0 && e.local === 0 && e.dbs === 0 && e.caches === 0,
              `${when}: the seed is in no DOM, field, storage, URL, console line or page heap (${where.join(', ') || 'clean'}; ${e.local} local keys, ${e.dbs} databases, ${e.caches} caches)`);
    };
    await found('just after confirming');
    await page.click('#spaces .tile[data-id="space-home"]').catch(() => {});
    // memory pressure: the browser is told memory is critical, and the page
    // fills and drops 200 MiB
    await cdp.send('Memory.simulatePressureNotification', {level : 'critical'});
    await page.evaluate(() => {
        let keep = [];
        for (let i = 0; i < 50; i++) {
            const a = new Uint8Array(4 << 20);
            a.fill(i);
            keep.push(a);
        }
        keep = null;
        return keep;
    });
    await cdp.send('Memory.simulatePressureNotification', {level : 'critical'});
    await found('after memory pressure');
    await page.goto('about:blank');
    await page.goBack();
    await page.waitForSelector('#v-login.on');
    await found('after leaving and coming back');

    // the same seed typed into the login, and a passphrase
    const r2 = await tryLogin(page, 'seed', seed.toLowerCase());
    check(r2.did === r.did, 'the new seed typed into the login gives its DID');
    await found('after logging in with it and signing out');
    const secretPass = 'violet anchor 7 quietly folds the map';
    const r3 = await tryLogin(page, 'pass', 'mara', secretPass);
    check(/^did:zxv:/.test(r3.did || ''), 'a passphrase logs in');
    const e3 = await everywhere(page);
    const h3 = await heapStrings(cdp);
    const at = [];
    if (e3.text.includes(secretPass)) at.push('page');
    if (h3.includes(secretPass)) at.push('JS heap');
    if (watch.console.some((l) => l.includes(secretPass))) at.push('console');
    check(at.length === 0, `after it, the passphrase is in no DOM, field, storage, URL, console line or page heap (${at.join(', ') || 'clean'})`);
    check(watch.problems.length === 0, `secrets: no page errors${watch.problems.length ? ': ' + watch.problems.join('; ') : ''}`);
    await ctx.close();
}

// ----------------------------------------------------------------

async function main()
{
    const [ zxvd, models, root, scratch ] = process.argv.slice(2);
    if (!zxvd || !models || !root || !scratch) {
        console.error('usage: test_browser_stress.cjs ZXVD MODELS_DIR WEB_ROOT SCRATCH');
        process.exit(2);
    }
    process.env.ZXV_TEST_MODEL = process.env.ZXV_TEST_MODEL || path.join(models, fs.readdirSync(models).find((f) => f.endsWith('.gguf')));
    const browser = await chromium.launch();
    let z = null;
    try {
        z = await startZxvd(zxvd, models, root);
        if (PARTS.includes(1)) await partCredentials(browser, z);
        if (PARTS.includes(2)) await partBoot(browser, z, root);
        if (PARTS.includes(5)) await partConcurrency(browser, z);
        if (PARTS.includes(7)) await partSecrets(browser, z);
        if (PARTS.includes(6)) await partSoak(browser, z);
        check(z.proc.exitCode === null, 'zxvd ran through parts 1, 2, 5, 6 and 7');
        await z.stop();
        z = null;
        if (PARTS.includes(3)) await partZxvdDown(browser, zxvd, models, root);
        if (PARTS.includes(4)) await partNoModel(browser, zxvd, root, scratch);
    } finally {
        await browser.close();
        if (z) await z.stop();
    }
    console.log(`test_browser_stress: ${tally.checks} checks, ${tally.fails} failed`);
    process.exit(tally.fails ? 1 : 0);
}

main().catch((err) => {
    console.error(`test_browser_stress: ${err.stack || err}`);
    process.exit(1);
});
