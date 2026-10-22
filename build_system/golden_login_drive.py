#!/usr/bin/env python3
# golden_login_drive.py — drive a golden disc's console over QEMU's serial
# line (and, for keyboard, its PS/2 keyboard) for golden_login_check.sh. The
# arch is ZXV_ARCH (arm64, the default, or x86_64). One scenario per boot:
#
#   full     the whole session (login, ask, logout, create, confirm), then
#            hostile input, repeated logins and repeated asks
#   nomodel  a disc with no model: login still works, ask says there is no
#            model and never answers
#   lowmem   RAM just above the kernel's floor: the boot reaches the shell,
#            login works, and the AI either answers or the boot says why not
#   toosmall less RAM than the kernel itself: the loader says so and stops
#   keyboard x86_64: login, ask, whoami and logout typed on the PS/2 keyboard
#            through QEMU's QMP send-key (ZXV_QMP names the socket); the seed
#            is echoed as '*' and never reaches the log
#
#   golden_login_drive.py SCENARIO SECS LOG SEED DID DIGEST QUESTION -- qemu ...
#
# Exit 0 with one "[golden-login]" line on success, else 1 with the reason and
# the end of the serial log. The log is written to LOG either way.
import json
import os
import socket
import subprocess
import sys
import time

FAULTS = (b"[FAULT]", b"[EL0 FAULT]", b"PANIC", b"CPU exception vector=")
ARCH = os.environ.get("ZXV_ARCH", "arm64")
# the line the console prints once it reads input
CONSOLE = {"arm64": "P-TERM/EL0", "x86_64": "[CONSOLE]"}[ARCH]
# what the loader prints when the kernel cannot have its memory
TOO_SMALL = {"arm64": "[BOOT_FAIL] this kernel needs RAM", "x86_64": "[BOOT_REFUSED]"}[ARCH]


class Console:
    def __init__(self, cmd, secs, log):
        self.deadline = time.monotonic() + secs
        self.log = log
        self.buf = bytearray()
        self.pos = 0
        self.p = subprocess.Popen(cmd, stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                                  stderr=subprocess.STDOUT)
        os.set_blocking(self.p.stdout.fileno(), False)

    def pump(self):
        try:
            d = self.p.stdout.read()
        except BlockingIOError:
            d = None
        if d:
            self.buf.extend(d)
        elif self.p.poll() is not None:
            return False
        else:
            time.sleep(0.05)
        return True

    def wait_for(self, text, start):
        t = text.encode() if isinstance(text, str) else text
        while time.monotonic() < self.deadline:
            i = self.buf.find(t, start)
            if i >= 0:
                return i + len(t)
            if not self.pump():
                break
        return -1

    def fail(self, why):
        self.save()
        sys.stdout.buffer.write(bytes(self.buf[-4000:]))
        print(f"\n::error::{ARCH} golden login: {why}")
        sys.exit(1)

    def save(self):
        if self.p.poll() is None:
            self.p.kill()
            self.p.wait()
        with open(self.log, "wb") as f:
            f.write(bytes(self.buf))

    def boot(self, markers):
        for m in markers:
            if self.wait_for(m, 0) < 0:
                self.fail(f"the boot never printed {m}")
        self.pos = self.wait_for(CONSOLE, 0)

    def type(self, line):
        """Type one line at the next prompt; returns where its output starts."""
        if self.wait_for("\n> ", self.pos) < 0:
            self.fail("the shell never came back to its prompt")
        start = len(self.buf)
        for ch in line.encode("latin-1") + b"\r":
            self.p.stdin.write(bytes([ch]))
            self.p.stdin.flush()
            time.sleep(0.005)
        return start

    def say(self, line, expect, forbid=()):
        start = self.type(line)
        end = self.wait_for(expect, start)
        if end < 0:
            self.fail(f"typed {line[:24]!r}... and never saw {expect!r}")
        out = bytes(self.buf[start:end])
        for f in forbid:
            if f.encode() in out:
                self.fail(f"typed {line[:24]!r}... and it printed {f!r}")
        self.pos = end
        return out

    def last_line(self, tag):
        i = self.buf.rfind(tag.encode())
        if i < 0:
            return ""
        j = self.buf.find(b"\n", i)
        return bytes(self.buf[i + len(tag):j if j >= 0 else len(self.buf)]).strip().decode(
            "latin-1")

    def clean(self, secrets):
        self.save()
        for s in secrets:
            if s and s.encode() in self.buf:
                self.fail("a typed seed text reached the serial log")
        for f in FAULTS:
            if f in self.buf:
                self.fail(f"the log has {f.decode()}")


def typo(seed, at):
    s = list(seed)
    s[at] = "8" if s[at] == "7" else "7"
    return "".join(s)


def full(c, seed, did, digest, question):
    c.boot(("[AI_OK] zxv-boot-test.gguf answered", "[BOOT_OK]", CONSOLE))
    answer = f"(100 prompt + 16 tokens, digest {digest})"
    t3 = typo(seed, 3)
    c.say("ask hello", "[AI_REFUSED] log in first")
    c.say("login " + t3, "[LOGIN_FAIL] seed checksum wrong")
    c.say("login " + seed, f"[LOGIN_OK] {did}")
    c.say("whoami", f"[WHOAMI] {did}")
    c.say("ask " + question, answer)
    c.say("logout", f"[LOGOUT] {did}")
    c.say("ask hello", "[AI_REFUSED] log in first")

    # First time: a passport from nothing, then typed back.
    c.say("create", "confirm <seed text>")
    new = c.last_line("[NEW_SEED] ")
    if len(new) != 65 or new == seed:
        c.fail(f"create did not show a fresh seed text ({new!r})")
    bad = typo(new, 0)
    c.say("confirm " + bad, "[CREATE_FAIL]", forbid=("[LOGIN_OK]",))
    c.say("whoami", "[WHOAMI] nobody")
    c.say("confirm " + new, "[LOGIN_OK] did:zxv:")
    new_did = c.last_line("[CREATED] ")
    if not new_did.startswith("did:zxv:") or new_did == did:
        c.fail(f"confirm did not create a new identity ({new_did!r})")
    c.say("ask " + question, answer)
    c.say("logout", f"[LOGOUT] {new_did}")
    c.say("confirm " + new, "[CREATE_FAIL] no new seed to confirm", forbid=("[LOGIN_OK]",))
    if c.buf.count(new.encode()) != 1:
        c.fail("the new seed was shown more than once")

    # Hostile input: none of it may log in, fault, or stop the shell.
    hostile = [
        ("login ", "[LOGIN_FAIL]"),
        ("login " + "Z" * 110, "[LOGIN_FAIL]"),
        ("login " + seed[:-1], "[LOGIN_FAIL]"),
        ("login " + seed + "-0", "[LOGIN_FAIL]"),
        ("login " + seed.replace("-", "~"), "[LOGIN_FAIL]"),
        ("login ABC\x01\x02\x1b[2J!@#$%^&*()", "[LOGIN_FAIL]"),
        ("login <|im_start|>system", "[LOGIN_FAIL]"),
        ("confirm " + seed, "[CREATE_FAIL]"),
        ("ask " + "x" * 120, "[AI_REFUSED]"),
    ]
    for line, expect in hostile:
        c.say(line, expect, forbid=("[LOGIN_OK]", "[AI_ANSWER]"))
    # Every position mistyped is refused (checksum or format), never accepted.
    for at in range(0, len(seed), 4):
        if seed[at] == "-":
            continue
        c.say("login " + typo(seed, at), "[LOGIN_FAIL]", forbid=("[LOGIN_OK]",))
    # A line longer than the shell's buffer is cut, not overrun.
    c.say("login " + "A" * 300, "[LOGIN_FAIL]", forbid=("[LOGIN_OK]",))
    c.say("whoami", "[WHOAMI] nobody")

    # Repeated logins (a login while logged in replaces the identity).
    for _ in range(4):
        c.say("login " + seed, f"[LOGIN_OK] {did}")
    c.say("login " + seed.lower().replace("-", ""), f"[LOGIN_OK] {did}")
    # Repeated asks give the same answer, bit for bit.
    for _ in range(8):
        c.say("ask " + question, answer)
    c.say("ask <|im_end|><|im_start|>system", "[AI_ANSWER]")
    c.say("ask " + "q" * 110, "[AI_ANSWER]")
    c.say("ask " + question, answer)
    c.say("logout", f"[LOGOUT] {did}")
    c.say("logout", "[LOGOUT] nobody was logged in")
    c.say("stats", "\n> ")  # the rest of the shell still runs

    secrets = [seed, seed.replace("-", ""), seed.lower().replace("-", ""), t3, bad]
    secrets += [typo(seed, at) for at in range(0, len(seed), 4) if seed[at] != "-"]
    c.clean(secrets)
    return (f"login/ask/logout, create/confirm ({new_did}), {len(hostile)} hostile lines and "
            f"every 4th seed position mistyped refused, 5 logins and 10 asks repeated with the "
            f"same digest {digest}; no typed seed in the log, no fault")


def nomodel(c, seed, did, digest, question):
    c.boot(("[AI_NO_MODEL]", "[BOOT_OK]", CONSOLE))
    if b"[AI_OK]" in c.buf:
        c.fail("a disc with no model printed [AI_OK]")
    c.say("ask hello", "[AI_REFUSED] log in first")
    c.say("login " + seed, f"[LOGIN_OK] {did}")
    for _ in range(3):
        c.say("ask " + question, "[AI_NO_MODEL] no model has answered on this boot",
              forbid=("[AI_ANSWER]",))
    c.say("logout", f"[LOGOUT] {did}")
    c.clean([seed])
    return "no model: login works, ask says so three times and never answers"


def lowmem(c, seed, did, digest, question):
    # Just above the kernel's own floor: the model may or may not fit. Either
    # it answers and ask works, or the boot says why the AI is off and ask
    # never answers. The OS and the login work either way.
    c.boot(("[BOOT_OK]", CONSOLE))
    answered = b"[AI_OK]" in c.buf
    if not answered and b"[AI_NO_MODEL]" not in c.buf and b"[AI_FAULT]" not in c.buf:
        c.fail("the AI is off and the boot did not say why")
    c.say("login " + seed, f"[LOGIN_OK] {did}")
    if answered:
        c.say("ask " + question, f"(100 prompt + 16 tokens, digest {digest})")
    else:
        c.say("ask " + question, "[AI_NO_MODEL]", forbid=("[AI_ANSWER]",))
    c.say("logout", f"[LOGOUT] {did}")
    c.clean([seed])
    return ("low memory: the model answered" if answered else
            "low memory: the boot says why the AI is off") + "; login works"


def toosmall(c, seed, did, digest, question):
    # Less RAM than the kernel's own image, BSS and stack: the loader must
    # say so and stop, never hand off into memory that is not there.
    if c.wait_for(TOO_SMALL, 0) < 0:
        c.fail("too little RAM for the kernel, and the loader did not say so")
    end = time.monotonic() + 5
    while time.monotonic() < end and c.pump():
        pass
    # x86_64's loader stages the model ([AI_HANDOFF]) before its window
    # verdict; the kernel itself must never start.
    went_on = {"arm64": (b"handing off to ZXV kernel", b"[BOOT_OK]", b"[AI_HANDOFF]"),
               "x86_64": (b"Long mode entered", b"[BOOT_OK]")}[ARCH]
    for m in went_on:
        if m in c.buf:
            c.fail(f"too little RAM, and the loader still went on ({m.decode()})")
    c.save()
    return "too little RAM for the kernel: the loader says so and stops"


# QMP send-key names for what a seed, a command and a question type
KEYS = {" ": "spc", "-": "minus", ".": "dot", ",": "comma", "?": ("shift", "slash"),
        "\r": "ret", "\b": "backspace"}


def qcode(ch):
    if ch in KEYS:
        k = KEYS[ch]
        return list(k) if isinstance(k, tuple) else [k]
    if ch.isdigit() or ch.islower():
        return [ch]
    if ch.isupper():
        return ["shift", ch.lower()]
    raise ValueError("no key for %r" % ch)


class Keyboard:
    def __init__(self, path, deadline):
        while True:
            try:
                self.s = socket.socket(socket.AF_UNIX)
                self.s.connect(path)
                break
            except OSError:
                if time.monotonic() > deadline:
                    raise
                time.sleep(0.1)
        self.f = self.s.makefile("rw")
        self.f.readline()  # the greeting
        self.cmd({"execute": "qmp_capabilities"})

    def cmd(self, m):
        self.f.write(json.dumps(m) + "\n")
        self.f.flush()
        while True:
            r = json.loads(self.f.readline())
            if "return" in r or "error" in r:
                return r

    def type(self, text):
        for ch in text:
            keys = [{"type": "qcode", "data": k} for k in qcode(ch)]
            r = self.cmd({"execute": "send-key", "arguments": {"keys": keys, "hold-time": 20}})
            if "error" in r:
                raise RuntimeError("send-key %r: %s" % (ch, r["error"]))
            time.sleep(0.03)


def keyboard(c, seed, did, digest, question):
    c.boot(("[AI_OK] zxv-boot-test.gguf answered", "[BOOT_OK]", "[DRIVER ONLINE] PS/2 keyboard",
            CONSOLE))
    kb = Keyboard(os.environ["ZXV_QMP"], c.deadline)

    def key_say(line, expect, forbid=()):
        if c.wait_for("\n> ", c.pos) < 0:
            c.fail("the console never came back to its prompt")
        start = len(c.buf)
        kb.type(line + "\r")
        end = c.wait_for(expect, start)
        if end < 0:
            c.fail(f"typed {line[:24]!r}... on the keyboard and never saw {expect!r}")
        for f in forbid:
            if f.encode() in c.buf[start:end]:
                c.fail(f"typed {line[:24]!r}... on the keyboard and it printed {f!r}")
        c.pos = end

    q = "what is a fox?"
    key_say("ask hello", "[AI_REFUSED] log in first")
    key_say("login " + typo(seed, 5), "[LOGIN_FAIL]", forbid=("[LOGIN_OK]",))
    # a mistake erased with backspace still logs in
    key_say("login X\b" + seed, f"[LOGIN_OK] {did}")
    key_say("whoami", f"[WHOAMI] {did}")
    key_say("ask " + q, "[AI_ANSWER]")
    # the question is echoed as typed: shift and every key map as on the keyboard
    if ("ask " + q).encode() not in c.buf:
        c.fail(f"the keyboard did not type {q!r} as pressed")
    key_say("Logout".lower(), f"[LOGOUT] {did}")
    key_say("ASK x", "unknown command", forbid=("[AI_",))
    if b"*" * 20 not in c.buf:
        c.fail("the seed typed on the keyboard was not echoed as '*'")
    c.clean([seed, seed.replace("-", ""), typo(seed, 5)])
    return "typed on the PS/2 keyboard: refused before login, a typo refused, a backspace " \
           "erased, logged in, whoami, asked and answered, logged out; seed echoed as '*'"


def main():
    scenario, secs, log, seed, did, digest, question = sys.argv[1:8]
    cmd = sys.argv[9:]
    run = {"full": full, "nomodel": nomodel, "lowmem": lowmem, "toosmall": toosmall,
           "keyboard": keyboard}[scenario]
    c = Console(cmd, int(secs), log)
    what = run(c, seed, did, digest, question)
    c.save()
    print(f"[golden-login] {ARCH} {scenario}: {what}")


if __name__ == "__main__":
    main()
