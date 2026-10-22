// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
//
// lib.cjs — what tests/browser's Playwright tests share: checks, zxvd started
// on a scratch home with its ZXV-WEB link, zxvd's own socket, a page watched
// for errors, and the companion asked until the market cycle's budget lets
// the model run.
'use strict';

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
const SEED_RX = /^([0-9A-HJKMNP-TV-Z]{5}-){10}[0-9A-HJKMNP-TV-Z]{5}$/;

const tally = {checks : 0, fails : 0};
function check(ok, what)
{
    tally.checks++;
    if (!ok) tally.fails++;
    console.log(`${ok ? '  ok  ' : '  FAIL'} ${what}`);
    return ok;
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

// zxvd on a scratch home. opts: {home, port, token, args}. Resolves to
// {proc, url, origin, token, sock, home, stop()}.
async function startZxvd(zxvd, models, root, opts)
{
    const o = opts || {};
    const home = o.home || fs.mkdtempSync(path.join(os.tmpdir(), 'zxvb-'));
    const sock = path.join(home, 'run', 'zxvd.sock');
    const env = Object.assign({}, process.env, {HOME : home, XDG_DATA_HOME : path.join(home, 'xdg')});
    if (o.token) env.ZXV_TOKEN = o.token;
    else delete env.ZXV_TOKEN;
    const args = [ '--rpc', sock, '--web-root', root, '--exit-with-parent' ];
    if (models) args.push('--models-dir', models);
    if (o.port) args.push('--port', String(o.port));
    const proc = spawn(zxvd, args.concat(o.args || []), {env, stdio : [ 'pipe', 'pipe', 'pipe' ]});
    proc.stderr.on('data', () => {}); // refusals are logged there; the tests read the replies
    const url = await waitLine(proc, 'ZXV-WEB: ', 30000);
    const m = /^(http:\/\/127\.0\.0\.1:(\d+))\/web\/boot\.html#token=([0-9a-f]{64})$/.exec(url);
    return {
        proc,
        url,
        origin : m ? m[1] : '',
        port : m ? Number(m[2]) : 0,
        token : m ? m[3] : '',
        sock,
        home,
        stop(signal)
        {
            return new Promise((resolve) => {
                if (proc.exitCode !== null || proc.signalCode !== null) return resolve();
                proc.on('exit', () => resolve());
                proc.stdin.end();
                proc.kill(signal || 'SIGTERM');
            });
        }
    };
}

// Watch a page: page errors, console errors and failed requests become
// problems. Chromium reports a streamed fetch as ERR_ABORTED when the page
// has read the whole body before the network layer says it is finished;
// such a request had its 200 response and every byte (the boot then checks
// its hash), so it is noted, not counted. Every console line is kept, to
// look for secrets in.
function watchPage(page, label)
{
    const w = {problems : [], notes : [], console : [], requests : []};
    const tag = label ? `${label}: ` : '';
    page.on('pageerror', (e) => w.problems.push(`${tag}page error: ${e.message}`));
    page.on('console', (m) => {
        w.console.push(m.text());
        if (m.type() === 'error') w.problems.push(`${tag}console: ${m.text()}`);
    });
    page.on('request', (r) => w.requests.push(r));
    page.on('requestfailed', (r) => {
        const why = (r.failure() || {}).errorText || '';
        r.response().then(
            (resp) => {
                if (why === 'net::ERR_ABORTED' && resp && resp.status() === 200)
                    w.notes.push(`${r.url()}: whole body read before the network layer finished`);
                else
                    w.problems.push(`${tag}request failed: ${r.url()} (${why})`);
            },
            () => w.problems.push(`${tag}request failed: ${r.url()} (${why})`));
    });
    return w;
}

// Ask the companion on the page; when zxvd says this market cycle's budget
// is spent (it refills about once a second), wait and ask again, as its own
// reply says to. Resolves to {byModel, text, tries}.
async function askOnPage(page, prompt, tries)
{
    const max = tries || 6;
    for (let i = 1;; i++) {
        await page.evaluate(() => document.getElementById('answer').classList.remove('on'));
        await page.fill('#prompt', prompt);
        await page.click('#ask');
        await page.waitForFunction(() => document.getElementById('answer').classList.contains('on') ||
                                              document.getElementById('ask-why').textContent.length > 0,
                                   null, {timeout : 120000});
        const text = await page.locator('#answer-text').textContent();
        const byModel = (await page.getAttribute('#answer', 'data-model')) === 'yes';
        if (byModel || !/^Budget (exhausted|too small)/.test(text) || i >= max) return {byModel, text, tries : i};
        await page.waitForTimeout(1500);
    }
}

function rssKiB(pid)
{
    const s = fs.readFileSync(`/proc/${pid}/status`, 'utf8');
    const m = /VmRSS:\s+(\d+) kB/.exec(s);
    return m ? Number(m[1]) : -1;
}

module.exports = {
    SEED_TEXT,
    DID_SEED,
    PASS,
    HANDLE,
    DID_PASS,
    SEED_RX,
    tally,
    check,
    rpcSocket,
    waitLine,
    startZxvd,
    watchPage,
    askOnPage,
    rssKiB
};
