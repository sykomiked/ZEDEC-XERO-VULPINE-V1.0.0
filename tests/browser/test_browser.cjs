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
const fs = require('fs');
const path = require('path');
const {SEED_TEXT, DID_SEED, PASS, HANDLE, DID_PASS, SEED_RX, tally, check, rpcSocket, startZxvd, watchPage, askOnPage} =
    require('./lib.cjs');

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

    const z = await startZxvd(zxvd, models, root);
    const {url, sock} = z;
    let browser = null;
    try {
        check(/^http:\/\/127\.0\.0\.1:\d+\/web\/boot\.html#token=[0-9a-f]{64}$/.test(url), `zxvd prints the browser link (${url.replace(/token=.*/, 'token=...')})`);
        const info = (await rpcSocket(sock, 'model.info')).result;
        check(info.can_generate, `zxvd has a model that can answer: ${info.status}`);

        browser = await chromium.launch();
        const page = await browser.newPage({viewport : {width : 1280, height : 800}});
        const watch = watchPage(page);

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
        check(SEED_RX.test(fresh) && fresh !== SEED_TEXT,
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
        const asked = await askOnPage(page, 'Hello there, what can you do?');
        await shot(page, '5-answer.png');
        const text = await page.locator('#answer-text').textContent();
        const byModel = await page.getAttribute('#answer', 'data-model');
        const tokens = Number(await page.getAttribute('#answer', 'data-tokens'));
        const prov = await page.locator('#answer-prov').textContent();
        const c = (await rpcSocket(sock, 'swarm.state')).result.companion;
        check(c.asks === asks0 + asked.tries,
              `the page asked zxvd once per try (${asked.tries}; a spent market cycle's budget is waited out)`);
        check(byModel === 'yes' && c.answered_by_model === true,
              'the page and zxvd agree the model wrote the answer');
        const tag = c.last_tag || {};
        check(tokens > 0 && tokens === tag.tokens && tag.base === 'true',
              `the page shows zxvd's tag: ${tokens} tokens, ${tag.base}`);
        check(text.length > 0 && !/did not run|cannot write|cannot run/.test(text),
              `the answer is the model's own text (${JSON.stringify(text.slice(0, 60))})`);
        check(prov.startsWith(`Written by ${info.name} `), `the page says who wrote it: "${prov}"`);
        await page.waitForTimeout(100); // a failed request's verdict is settled asynchronously
        check(watch.problems.length === 0, `no page errors${watch.problems.length ? ': ' + watch.problems.join('; ') : ''}`);
        for (const n of watch.notes) console.log(`  note ${n}`);
    } finally {
        if (browser) await browser.close();
        await z.stop();
    }
    console.log(`test_browser: ${tally.checks} checks, ${tally.fails} failed`);
    process.exit(tally.fails ? 1 : 0);
}

main().catch((err) => {
    console.error(`test_browser: ${err.stack || err}`);
    process.exit(1);
});
