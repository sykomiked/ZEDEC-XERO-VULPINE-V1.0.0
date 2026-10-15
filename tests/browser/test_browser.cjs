// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
//
// test_browser.cjs ZXVD MODELS_DIR WEB_ROOT [SHOTS_DIR] — the browser path end
// to end, in headless Chromium: zxvd serves the root (--web-root), the boot
// screen streams and checks every file, the passport logs in inside the page
// (a seed with a typo is refused, a short passphrase is refused, a passphrase
// and then a seed log in to the DIDs the native build gives), the spaces come
// from zxvd, the companion opens in Home, and the answer comes from the model
// zxvd runs, which the test confirms on zxvd's own socket. Run by
// tests/browser/test_browser.sh. Needs playwright on NODE_PATH and Chromium
// (PLAYWRIGHT_BROWSERS_PATH).
'use strict';

const {chromium} = require('playwright');
const {spawn} = require('child_process');
const fs = require('fs');
const net = require('net');
const os = require('os');
const path = require('path');

// web/session/test_passport_wasm.c: what the passport gives natively
const SEED_TEXT = '000G4-0R40M-30E20-9185G-R38E1-W8124-GK2GA-HC5RR-34D1P-70X3R-FHHQ0';
const DID_SEED = 'did:zxv:2xin6nai2r2rmwk5wqv25nwmatah75fp2uukhsftpniqksm5zxcq';
const PASS = 'correct horse battery staple', HANDLE = 'ana';
const DID_PASS = 'did:zxv:yxprmmrkcsabstevsnynhlmlutiqa7cceizvg2vrjgwgrudnd74q';

let checks = 0, fails = 0;
function check(ok, what)
{
    checks++;
    if (!ok) fails++;
    console.log(`${ok ? '  ok  ' : '  FAIL'} ${what}`);
}

function rpcSocket(sock, method, params)
{
    return new Promise((resolve, reject) => {
        const c = net.connect({path : sock});
        let buf = '';
        c.on('data', (d) => {
            buf += d.toString('utf8');
            const i = buf.indexOf('\n');
            if (i >= 0) {
                c.end();
                try {
                    resolve(JSON.parse(buf.slice(0, i)));
                } catch (e) {
                    reject(e);
                }
            }
        });
        c.on('error', reject);
        c.write(JSON.stringify({jsonrpc : '2.0', id : 1, method, params : params || {}}) + '\n');
    });
}

function waitLine(proc, prefix, ms)
{
    return new Promise((resolve, reject) => {
        let buf = '';
        const t = setTimeout(() => reject(new Error(`no ${prefix} line in ${ms} ms:\n${buf}`)), ms);
        proc.stdout.on('data', (d) => {
            buf += d.toString('utf8');
            for (const line of buf.split('\n'))
                if (line.startsWith(prefix)) {
                    clearTimeout(t);
                    resolve(line.slice(prefix.length).trim());
                }
        });
        proc.on('exit', (code) => reject(new Error(`zxvd exited (${code}) before ${prefix}:\n${buf}`)));
    });
}

async function main()
{
    const [ zxvd, models, root, shots ] = process.argv.slice(2);
    if (!zxvd || !models || !root) {
        console.error('usage: test_browser.cjs ZXVD MODELS_DIR WEB_ROOT [SHOTS_DIR]');
        process.exit(2);
    }
    if (shots) fs.mkdirSync(shots, {recursive : true});
    const shot = async (page, name) => {
        if (shots) await page.screenshot({path : path.join(shots, name), animations : 'disabled'});
    };

    const home = fs.mkdtempSync(path.join(os.tmpdir(), 'zxvb-'));
    const sock = path.join(home, 'run', 'zxvd.sock');
    const env = Object.assign({}, process.env, {HOME : home, XDG_DATA_HOME : path.join(home, 'xdg')});
    const proc = spawn(zxvd, [ '--rpc', sock, '--models-dir', models, '--web-root', root, '--exit-with-parent' ],
                       {env, stdio : [ 'pipe', 'pipe', 'inherit' ]});
    let browser = null;
    try {
        const url = await waitLine(proc, 'ZXV-WEB: ', 30000);
        check(/^http:\/\/127\.0\.0\.1:\d+\/web\/#token=[0-9a-f]{64}$/.test(url), `zxvd prints the browser link (${url.replace(/token=.*/, 'token=...')})`);
        const info = (await rpcSocket(sock, 'model.info')).result;
        check(info.can_generate, `zxvd has a model that can answer: ${info.status}`);

        browser = await chromium.launch();
        const page = await browser.newPage({viewport : {width : 1280, height : 800}});
        const problems = [];
        page.on('pageerror', (e) => problems.push(`page error: ${e.message}`));
        page.on('console', (m) => {
            if (m.type() === 'error') problems.push(`console: ${m.text()}`);
        });
        page.on('requestfailed', (r) => problems.push(`request failed: ${r.url()}`));

        // 1. boot: the screen runs while the files stream, then hands over
        const t0 = Date.now();
        await page.goto(url);
        await page.waitForTimeout(400);
        await shot(page, '1-boot.png');
        const bootWhy = await page.locator('#why').textContent();
        check(bootWhy === '', `the boot screen raises no error${bootWhy ? ': ' + bootWhy : ''}`);
        await page.waitForURL(/\/web\/session\/index\.html$/, {timeout : 60000});
        check(true, `the boot hands over to the login (${Date.now() - t0} ms)`);
        check(!page.url().includes('token='), 'the token is not left in the address bar');

        // 2. login
        await page.waitForSelector('#v-login.on');
        await page.waitForFunction(() => !document.getElementById('why').textContent, null, {timeout : 5000});
        // first time: make a seed, write it down, type it back
        await page.click('#new-seed');
        await page.waitForSelector('#f-new:not([hidden]) #new-make:not([hidden])');
        await page.click('#make');
        await page.waitForSelector('#new-show:not([hidden])');
        const fresh = await page.locator('#seed-new-text').textContent();
        check(/^([0-9A-HJKMNP-TV-Z]{5}-){10}[0-9A-HJKMNP-TV-Z]{5}$/.test(fresh) && fresh !== SEED_TEXT,
              'a new seed is made in the page and shown to write down');
        check(await page.locator('#written').isDisabled(), 'Continue waits until it is written down');
        await shot(page, '2-create.png');
        await page.check('#written-ok');
        await page.click('#written');
        await page.waitForSelector('#new-confirm:not([hidden])');
        check((await page.locator('#seed-new-text').textContent()) === '' &&
                  (await page.locator('#seed').inputValue()) === '',
              'the seed is gone from the page once it is written down, and filled in nowhere');
        await page.fill('#confirm', SEED_TEXT); // a valid seed, but not this one
        await page.click('#enter');
        await page.waitForFunction(() => document.getElementById('why').textContent.length > 0);
        const wrongWhy = await page.locator('#why').textContent();
        check(/not the seed you were shown/.test(wrongWhy) && (await page.getAttribute('body', 'data-did')) === null,
              `a wrong confirmation is refused: "${wrongWhy}"`);
        await page.fill('#confirm', fresh.toLowerCase().replace(/-/g, ' '));
        await page.click('#enter');
        await page.waitForSelector('#v-spaces.on', {timeout : 60000});
        const createdDid = await page.getAttribute('body', 'data-did');
        check(/^did:zxv:[a-z2-7]{52}$/.test(createdDid || ''), `the new passport is made: ${createdDid}`);
        await page.click('#sign-out');
        await page.waitForSelector('#v-login.on');
        await page.fill('#seed', fresh);
        await page.click('#enter');
        await page.waitForSelector('#v-spaces.on', {timeout : 60000});
        check((await page.getAttribute('body', 'data-did')) === createdDid,
              'logging in later with that seed gives the same DID');
        await page.click('#sign-out');
        await page.waitForSelector('#v-login.on');

        const typo = SEED_TEXT.slice(0, 3) + (SEED_TEXT[3] === 'A' ? 'B' : 'A') + SEED_TEXT.slice(4);
        await page.fill('#seed', typo);
        await page.click('#enter');
        await page.waitForFunction(() => document.getElementById('why').textContent.length > 0);
        const typoWhy = await page.locator('#why').textContent();
        check(/typo/.test(typoWhy), `a mistyped seed is refused in words: "${typoWhy}"`);

        await page.click('#t-pass');
        await page.fill('#handle', HANDLE);
        await page.fill('#pass', 'too short');
        await page.click('#enter');
        await page.waitForFunction(() => /16/.test(document.getElementById('why').textContent));
        check(true, `a short passphrase is refused: "${await page.locator('#why').textContent()}"`);

        await page.fill('#pass', PASS);
        await page.click('#enter');
        await page.waitForSelector('#v-spaces.on', {timeout : 60000});
        check((await page.getAttribute('body', 'data-did')) === DID_PASS,
              `passphrase and handle give the native passport's DID (${DID_PASS})`);
        check((await page.locator('#bar').evaluate((e) => e.style.width)) === '100%', 'the login bar ended full');
        await page.click('#sign-out');
        await page.waitForSelector('#v-login.on');
        check((await page.getAttribute('body', 'data-did')) === null && (await page.locator('#seed').inputValue()) === '',
              'signed out: the login is back and empty');

        await page.fill('#seed', SEED_TEXT.toLowerCase().replace(/-/g, ' '));
        await shot(page, '3-login.png');
        await page.click('#enter');
        await page.waitForSelector('#v-spaces.on', {timeout : 60000});
        check((await page.getAttribute('body', 'data-did')) === DID_SEED,
              `the seed gives the native passport's DID (${DID_SEED})`);

        // 3. spaces, then Home and its apps
        await page.waitForSelector('#spaces .tile[data-id="space-home"]');
        const n = await page.locator('#spaces .tile').count();
        const list = (await rpcSocket(sock, 'spaces.list')).result.spaces;
        check(n === list.length, `the page shows zxvd's ${list.length} space(s)`);
        await page.click('#spaces .tile[data-id="space-home"]');
        await page.waitForSelector('#v-space.on #apps .tile[data-id="app-companion"]');
        await shot(page, '4-space.png');
        check((await page.locator('#space-title').textContent()) === 'Home', 'Home is open');

        // 4. the companion: a real answer from the model zxvd runs
        await page.click('#apps .tile[data-id="app-companion"]');
        await page.waitForSelector('#v-ask.on');
        const asks0 = (await rpcSocket(sock, 'swarm.state')).result.companion.asks;
        await page.fill('#prompt', 'Hello there, what can you do?');
        await page.click('#ask');
        await page.waitForSelector('#answer.on', {timeout : 120000});
        await shot(page, '5-answer.png');
        const text = await page.locator('#answer-text').textContent();
        const byModel = await page.getAttribute('#answer', 'data-model');
        const tokens = Number(await page.getAttribute('#answer', 'data-tokens'));
        const prov = await page.locator('#answer-prov').textContent();
        const c = (await rpcSocket(sock, 'swarm.state')).result.companion;
        check(c.asks === asks0 + 1, 'the page asked zxvd once');
        check(byModel === 'yes' && c.answered_by_model === true,
              'the page and zxvd agree the model wrote the answer');
        const tag = c.last_tag || {};
        check(tokens > 0 && tokens === tag.tokens && tag.base === 'true',
              `the page shows zxvd's tag: ${tokens} tokens, ${tag.base}`);
        check(text.length > 0 && !/did not run|cannot write|cannot run/.test(text),
              `the answer is the model's own text (${JSON.stringify(text.slice(0, 60))})`);
        check(prov.startsWith(`Written by ${info.name} `), `the page says who wrote it: "${prov}"`);
        check(problems.length === 0, `no page errors${problems.length ? ': ' + problems.join('; ') : ''}`);
    } finally {
        if (browser) await browser.close();
        proc.stdin.end();
        proc.kill('SIGTERM');
    }
    console.log(`test_browser: ${checks} checks, ${fails} failed`);
    process.exit(fails ? 1 : 0);
}

main().catch((err) => {
    console.error(`test_browser: ${err.stack || err}`);
    process.exit(1);
});
