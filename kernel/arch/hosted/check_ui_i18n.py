#!/usr/bin/env python3
# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
"""check_ui_i18n.py — the desktop window's words all come from kernel/src/i18n.

  check_ui_i18n.py zxv_ui.html TABLE.json SAMPLES.json PAGE.html

TABLE.json, SAMPLES.json and PAGE.html are gen_ui.py's --table-out,
--samples-out and --html-out (test_hosted.sh makes them). Checks:

  1. every message id the page uses is in the table, with English text, and
     the table holds no other ids;
  2. no hard-coded words in zxv_ui.html: no letters in the text of the
     markup or in its visible attributes (title, placeholder, alt, label,
     aria-label, value), every data-i18n element empty; and every string
     literal or template text in the scripts that holds a letter (after
     dropping HTML tags) is either a message id or one of the code strings
     listed in CODE below (element ids, API paths, class names, ...);
  3. the embedded page carries the table and the English text, filled in;
  4. the page's i18n script (<script id="zxv-i18n-core">), run under node,
     gives what the C i18n module gives for locale negotiation, number
     formatting and message substitution (SAMPLES.json, from
     gen_ui_strings.c). node is required when $CI is set (GitHub Actions
     sets it); elsewhere a missing node is reported and that part is not run.

The checker tests itself first on a few bad pages.
"""
import html.parser
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile
import unicodedata

# String literals of the page's scripts that are code, not words a person
# reads. Adding one here is a decision: a word for people goes in en.msg.
CODE = {
    # element ids, attributes, selectors, tags, classes, properties
    'zxv-i18n', 'langnote', 'harm', 'div', 'h', 'mode', 'budget', 'net', 'upd', 'clock',
    'ledger', 'agents', 'notes', 'chat', 'q', 'ask', 'quit', 'updcheck', 'noteread',
    '[data-i18n]', '[data-i18n-title]', '[data-i18n-placeholder]', '[data-net]',
    'on', 'unread', 'msg', ' you', 'rtl', 'ltr', 'en',
    # the API
    '/api/state', '/api/notes', '/api/net', '/api/update', '/api/notes/read', '/api/ask',
    '/api/quit', 'POST',
    # the staple apps and the Playground: element ids, selectors, classes
    'tab-swarm', 'tab-play', 'swarm', 'play', 'files', 'text', 'photos', 'console', 'dock',
    '#dock button', '.view', 'v-', '.tpl', 'tpl', 'tpls', 'tile', 'row', 'drop', 'split', 'primary',
    'prose', 'edited', 'on ', 'small', 'err', 'go', 'a.go', 'viewer', 'modal', 'span', 'button',
    'option', 'input', 'img', 'a', 'tr', 'ul', 'ol', 'aria-label', 'var(--bad)',
    'fcrumbs', 'fdrop', 'fempty', 'fimport', 'flist', 'fnewdir', 'fnewtxt', 'fpick', 'fstat', 'ftrash',
    'ed', 'mdview', 'tdirty', 'tfont', 'tname', 'tnew', 'topen', 'tprev', 'tsave', 'tsaveas', 'tstat',
    'twrap', 'pcrumbs', 'pgrid', 'pimport', 'ppick', 'pstat', 'vclose', 'vname', 'vnext', 'vprev',
    'vstage', 'cclear', 'cmeth', 'cout', 'cparams', 'crun', 'minput', 'mno', 'mtext', 'mtitle', 'myes',
    'pl-askf', 'pl-discard', 'pl-draft', 'pl-draftbox', 'pl-draftnote', 'pl-drafttext', 'pl-files',
    'pl-req', 'pl-reset', 'pl-result', 'pl-run', 'pl-sum', 'pl-text', 'pl-title', 'pl-use',
    # events, keys and storage
    'keydown', 'beforeunload', 'Enter', 'Escape', 'Tab', 'ArrowLeft', 'ArrowRight', 's',
    'zxv-app', 'zxv-draft', 'null',
    # the API: zxvd methods, files.* parameters, the Playground's request lines
    '/api/rpc', '/api/play/catalogue', '/api/play/run', '/api/play/draft', 'Content-Type',
    'application/json', 'application/octet-stream', 'rpc.discover', 'zxvd.shutdown', 'files.list',
    'files.read', 'files.write', 'files.mkdir', 'files.rename', 'files.trash', 'files.empty_trash',
    'files.root', 'utf8', 'base64', 'begin', 'add', 'end', 'replace', '.Trash', '.Trash/',
    'README.md', 'template.zxv', 'required', 'none', 'call', 'ok', 'error',
    'template \n', 'request \n', 'file  \n',
    # file kinds, extensions and media types
    'image', 'audio', 'video', 'other', 'txt', 'md', 'markdown', 'json', 'csv', 'tsv', 'log', 'yaml',
    'yml', 'toml', 'ini', 'xml', 'html', 'css', 'js', 'ts', 'c', 'py', 'sh', 'swift', 'rs', 'java',
    'cob', 'cbl', 'sutra', 'zxt', 'f90', 'ada', 'adb', 'ads', 'apl', 'k', 'm', 'sql', 'rtf', 'png',
    'jpg', 'jpeg', 'gif', 'webp', 'bmp', 'svg', 'avif', 'heic', 'mp3', 'm4a', 'wav', 'ogg', 'flac',
    'aac', 'opus', 'mp4', 'm4v', 'mov', 'webm', 'image/png', 'image/jpeg', 'image/gif', 'image/webp',
    'image/bmp', 'image/svg+xml', 'image/avif', 'image/heic', 'audio/mpeg', 'audio/mp4', 'audio/wav',
    'audio/ogg', 'audio/flac', 'audio/aac', 'video/mp4', 'video/quicktime', 'video/webm',
    # JavaScript
    'bigint', 'string',
    # the people apps and simple mode (zxvd_people.h): element ids, classes,
    # the methods and the values they send or compare
    'simple', 'simplebox', 'adv', '.ppl', 'home', 'feed', 'calls', 'xlate', 'devices', 'sq', 'sask',
    'sans', 'pfrom', 'pto', 'pamt', 'pref', 'pay', 'pout', 'ptext', 'pgroup', 'post', 'postout',
    'groups', 'gnew', 'gname', 'feedinfo', 'cnew', 'cpeer', 'cinfo', 'lines', 'xtext', 'xto', 'xgo',
    'xdet', 'xout', 'xnote', 'dstat', 'dsas', 'dinv', 'dcode', 'djoin', 'djcode', 'dout', 'devs',
    'item', 'acts', 'btns', '8px', 'win-', 'reply', 'member', 'admin', 'owner', 'idle', 'ended',
    'incoming', 'loopback', 'active', 'und', 'ui.get', 'ui.set', 'ledger.accounts', 'ledger.post',
    'social.feed', 'social.groups', 'social.react', 'social.join', 'social.leave', 'social.post',
    'social.group_create', 'call.status', 'call.accept', 'call.hangup', 'call.start',
    'xlate.languages', 'xlate.detect', 'xlate.translate', 'devmesh.status', 'devmesh.devices',
    'devmesh.confirm_sas', 'devmesh.revoke', 'devmesh.invite', 'devmesh.join',
}

VISIBLE_ATTRS = {'title', 'placeholder', 'alt', 'label', 'aria-label', 'aria-description', 'value'}
ID = re.compile(r'^[A-Z][A-Z0-9_]*$')
SLOT_RX = re.compile(r'<script id="zxv-i18n" type="application/json">(.*?)</script>', re.S)


def has_letter(s):
    return any(unicodedata.category(c).startswith('L') for c in s)


def strip_tags(s):
    return re.sub(r'<[^<>]*>', '', s)


# ---------------------------------------------------------------- markup

class Markup(html.parser.HTMLParser):
    def __init__(self):
        super().__init__(convert_charrefs=True)
        self.stack, self.errors, self.scripts = [], [], []
        self.i18n_open = 0

    def handle_starttag(self, tag, attrs):
        a = dict(attrs)
        for k, v in attrs:
            if k in VISIBLE_ATTRS and v and has_letter(v):
                self.errors.append('<%s %s="%s">: words in markup' % (tag, k, v))
        self.stack.append((tag, a))

    def handle_endtag(self, tag):
        while self.stack:
            t, _ = self.stack.pop()
            if t == tag:
                break

    def handle_data(self, data):
        tag, a = self.stack[-1] if self.stack else ('', {})
        if tag == 'script':
            if a.get('type') != 'application/json':
                self.scripts.append((a.get('id', ''), data))
            return
        if tag == 'style':
            return
        if data.strip() and 'data-i18n' in a:
            self.errors.append('<%s data-i18n="%s"> is not empty' % (tag, a['data-i18n']))
        elif has_letter(data):
            self.errors.append('text in markup: %r' % data.strip()[:60])


# ---------------------------------------------------------------- scripts

ESC = {'n': '\n', 't': '\t', 'r': '\r', 'b': '\b', 'f': '\f', 'v': '\v', '0': '\0'}


def unescape(s):
    def one(m):
        e = m.group(1)
        if e[0] == 'u':
            return chr(int(e[2:-1] if e[1] == '{' else e[1:], 16))
        if e[0] == 'x':
            return chr(int(e[1:], 16))
        return ESC.get(e, e)
    return re.sub(r'\\(u\{[0-9a-fA-F]+\}|u[0-9a-fA-F]{4}|x[0-9a-fA-F]{2}|.)', one, s, flags=re.S)


REGEX_AFTER = set('(,=:[!&|?{};+-*%<>~^')
REGEX_WORDS = {'return', 'typeof', 'case', 'do', 'else', 'in', 'of', 'new', 'delete', 'void'}


def literals(src):
    """The string literals and template texts of a script (a template's
    ${...} parts become U+E000), skipping comments and regular expressions."""
    out = []

    def lex(i, stop):
        prev, word, depth = '', '', 0
        while i < len(src):
            c = src[i]
            if c in ' \t\r\n':
                i += 1
                continue
            if c == '/' and src.startswith('//', i):
                i = src.find('\n', i)
                i = len(src) if i < 0 else i
                continue
            if c == '/' and src.startswith('/*', i):
                i = src.index('*/', i) + 2
                continue
            if c == '/' and (prev == '' or prev in REGEX_AFTER or word in REGEX_WORDS):
                i += 1
                cls = False
                while src[i] != '/' or cls:
                    if src[i] == '\\':
                        i += 1
                    elif src[i] == '[':
                        cls = True
                    elif src[i] == ']':
                        cls = False
                    i += 1
                i += 1
                while i < len(src) and src[i].isalpha():
                    i += 1
                prev, word = ')', ''
                continue
            if c in '\'"':
                j = i + 1
                while src[j] != c:
                    j += 2 if src[j] == '\\' else 1
                out.append(unescape(src[i + 1:j]))
                i, prev, word = j + 1, ')', ''
                continue
            if c == '`':
                i += 1
                text = ''
                while src[i] != '`':
                    if src[i] == '\\':
                        text += src[i:i + 2]
                        i += 2
                    elif src.startswith('${', i):
                        i = lex(i + 2, True)
                        text += '\ue000'
                    else:
                        text += src[i]
                        i += 1
                out.append(unescape(text))
                i, prev, word = i + 1, ')', ''
                continue
            if c == '{':
                depth += 1
            elif c == '}':
                if stop and depth == 0:
                    return i + 1
                depth -= 1
            if c.isalnum() or c in '_$':
                j = i
                while j < len(src) and (src[j].isalnum() or src[j] in '_$'):
                    j += 1
                word, prev = src[i:j], 'a'
                i = j
                continue
            prev, word = c, ''
            i += 1
        return i

    lex(0, False)
    return out


def check_source(page, table_keys=None):
    """Errors for words written directly into zxv_ui.html."""
    m = Markup()
    m.feed(page)
    m.close()
    errors = list(m.errors)
    for sid, code in m.scripts:
        for lit in literals(code):
            text = strip_tags(lit).replace('\ue000', '')
            if not has_letter(text) or text in CODE:
                continue
            if ID.match(text) and (table_keys is None or text in table_keys):
                continue
            errors.append('script%s literal %r: a word for people belongs in en.msg' %
                          (' #' + sid if sid else '', lit[:60]))
    return errors


def self_test():
    ok = ('<p data-i18n="UI_QUIT"></p><script>T(\'UI_QUIT\');x=/[a-z]"\'/g;'
          '// Hello\n/* Hello */y=`<b class="z">${T(\'UI_QUIT\')}</b>`;$(\'mode\')</script>')
    bad = ['<p>Hello</p>', '<p data-i18n="UI_QUIT">Quit</p>', '<input placeholder="Ask">',
           '<script>say(\'Hello there\')</script>', '<script>x=`<b>Hi ${y}</b>`</script>',
           '<script>x=a/b;y="Oops"</script>', '<button title="Stop">&#8230;</button>']
    if check_source(ok):
        return 'self-test: a clean page was refused: %s' % check_source(ok)
    for b in bad:
        if not check_source(b):
            return 'self-test: missed the words in %r' % b
    return None


# ---------------------------------------------------------------- node

NODE_TEST = r'''
const fs = require('fs');
const tb = JSON.parse(fs.readFileSync(process.argv[2], 'utf8'));
const S = JSON.parse(fs.readFileSync(process.argv[3], 'utf8'));
const byTag = {}; tb.locs.forEach(l => { byTag[l[0]] = l; });
let bad = 0, n = 0;
function eq(what, got, want) { n++; if (got !== want) { bad++; if (bad < 20) console.log('[FAIL]', what, JSON.stringify(got), '!=', JSON.stringify(want)); } }
for (const [tag, want] of S.negotiate) eq('negotiate ' + JSON.stringify(tag), tb.locs[ZXVI18N.negotiate(tb, tag)][0], want);
for (const [tag, v, want] of S.num) eq('fmt_int ' + tag + ' ' + v, ZXVI18N.fmtNum(tb.nums[byTag[tag][4]], v, 0), want);
for (const [tag, s, f, want] of S.dec) eq('fmt_decimal ' + tag + ' ' + s + '/' + f, ZXVI18N.fmtNum(tb.nums[byTag[tag][4]], s, f), want);
for (const [tag, t, a, want] of S.fmt) eq('format_msg ' + tag + ' ' + t, ZXVI18N.format(!!byTag[tag][2], t, a), want);
// every locale: a string for every id; English is the catalog's text
for (const l of tb.locs) { const I = ZXVI18N.make(tb, l[0]); for (const k of tb.keys) { n++; if (typeof I.T(k) !== 'string' || !I.T(k)) { bad++; console.log('[FAIL] no text', l[0], k); } } }
const en = ZXVI18N.make(tb, 'en-US');
tb.keys.forEach((k, i) => eq('en ' + k, en.T(k), tb.en[i]));
eq('en has no fallback notice', en.fallback || en.machine, false);
const de = ZXVI18N.make(tb, 'de-DE');
eq('de falls back, says so', de.fallback && de.machine && de.T('BTN_SEND') === 'Senden' && de.entry('UI_QUIT').fb, true);
eq('de numbers', de.n(1234567), '1.234.567');
console.log((bad ? '[FAIL]' : '[PASS]') + ' page i18n script under node: ' + n + ' checks, ' + bad + ' failed');
process.exit(bad ? 1 : 0);
'''


def run_node(core, table_path, samples_path):
    node = shutil.which('node') or shutil.which('nodejs')
    if not node:
        if os.environ.get('CI'):
            return 'node is required on CI to run the page i18n script against the C results'
        print('[NOTE] node not found: the page i18n script was not run against the C results')
        return None
    with tempfile.TemporaryDirectory() as tmp:
        js = os.path.join(tmp, 'ui_i18n_test.js')
        with open(js, 'w', encoding='utf-8') as f:
            f.write(core + '\n' + NODE_TEST)
        r = subprocess.run([node, js, table_path, samples_path], stdout=subprocess.PIPE,
                           stderr=subprocess.STDOUT, universal_newlines=True)
        sys.stdout.write(r.stdout)
        return None if r.returncode == 0 else 'the page i18n script differs from the C i18n module'


def main():
    if len(sys.argv) != 5:
        sys.exit(__doc__)
    src_path, table_path, samples_path, page_path = sys.argv[1:]
    errors = []
    e = self_test()
    if e:
        errors.append(e)
    with open(src_path, encoding='utf-8') as f:
        page = f.read()
    with open(table_path, encoding='utf-8') as f:
        tb = json.load(f)
    with open(page_path, encoding='utf-8') as f:
        built = f.read()

    # 1. ids
    used = set(re.findall(r'\bdata-i18n(?:-title|-placeholder)?="([A-Z][A-Z0-9_]*)"', page))
    used |= set(re.findall(r"\bT\('([A-Z][A-Z0-9_]*)'", page))
    keys = set(tb['keys'])
    for k in sorted(used - keys):
        errors.append('id %s is used by the page but not in the table' % k)
    for k in sorted(keys - used):
        errors.append('id %s is in the table but the page does not use it' % k)
    for k, t in zip(tb['keys'], tb['en']):
        if not t.strip():
            errors.append('id %s has no English text' % k)
    for s in tb['sets']:
        for i, (text, review) in s.items():
            if not (0 <= int(i) < len(tb['keys'])) or not text or review not in (1, 2, 3):
                errors.append('bad set entry %s: %r' % (i, (text, review)))

    # 2. no words written into the page
    errors += check_source(page, keys)

    # 3. the page as embedded
    m = SLOT_RX.search(built)
    if not m or json.loads(m.group(1)) != tb:
        errors.append('the embedded page does not carry the table')
    for k, t in zip(tb['keys'], tb['en']):
        if 'data-i18n="%s"' % k in page and ('data-i18n="%s">' % k) + html_escape(t) + '<' not in built:
            errors.append('the embedded page lacks the English text of %s' % k)

    # 4. the page's script against the C module
    core = re.search(r'<script id="zxv-i18n-core">(.*?)</script>', page, re.S)
    if not core:
        errors.append('no <script id="zxv-i18n-core">')
    else:
        e = run_node(core.group(1), table_path, samples_path)
        if e:
            errors.append(e)

    for e in errors:
        print('[FAIL] check_ui_i18n: ' + e)
    if errors:
        sys.exit(1)
    print('[PASS] check_ui_i18n: %d ids from the i18n catalog, %d locales, %d string sets; '
          'no words written into zxv_ui.html' % (len(keys), len(tb['locs']), len(tb['sets'])))


def html_escape(t):
    return t.replace('&', '&amp;').replace('<', '&lt;').replace('>', '&gt;')


main()
