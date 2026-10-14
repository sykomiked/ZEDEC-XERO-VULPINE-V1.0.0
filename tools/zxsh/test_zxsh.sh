#!/bin/sh
# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
#
# test_zxsh.sh ZXSH — the host side (zxsh_host.c H1-H4) end to end, with
# real processes, files and a fake zxvd socket.
set -u
Z=${1:?usage: test_zxsh.sh path/to/zxsh}
case $Z in /*) ;; *) Z=$(pwd)/$Z ;; esac
T=$(mktemp -d "${TMPDIR:-/tmp}/zxsh-test.XXXXXX")
trap 'rm -rf "$T"' EXIT
export ZXSH_HOME="$T/home"
cd "$T" || exit 1
pass=0 fail=0
ok() { pass=$((pass + 1)); }
bad() { fail=$((fail + 1)); echo "[FAIL] $1"; }
# check NAME EXPECTED-EXIT EXPECTED-STDOUT -- zxsh args
check() {
    name=$1 want_rc=$2 want_out=$3
    shift 4
    out=$("$Z" "$@" 2>"$T/err")
    rc=$?
    if [ "$rc" = "$want_rc" ] && [ "$out" = "$want_out" ]; then ok; else
        bad "$name: rc=$rc out='$out' (want rc=$want_rc out='$want_out') err=$(head -c 300 "$T/err")"
    fi
}

# H1 exit codes and signals
check "true program" 0 "" -- -c "/bin/sh -c 'exit 0'"
check "false program" 1 "" -- -c "/bin/sh -c 'exit 3'"
check "not found is neither" 5 "" -- -c "no-such-program-zxsh"
check "crash is forbidden" 3 "" -- -c "sh -c 'kill -SEGV \$\$'; echo after"
grep -q "SIGSEGV" "$T/err" && ok || bad "crash reason"
check "abort is forbidden" 3 "" -- -c "sh -c 'kill -ABRT \$\$'"
check "interrupt is neither" 5 "" -- -c "sh -c 'kill -TERM \$\$'"
check "sigpipe upstream is fine" 0 "y" -- -c "yes | head -n 1"

# S2 pipelines through real processes
check "pipeline all stages" 1 "" -- -c "false | cat"
check "pipeline data" 0 "B" -- -c "printf 'a\nB\nc\n' | grep B"

# H2 verdict channel
check "verdict channel both" 4 "" -- -c "sh -c 'echo both >&3'"
check "verdict channel overrides exit" 0 "" -- -c "sh -c 'echo true/negentropic >&3; exit 1'"
check "crash beats the channel" 3 "" -- -c "sh -c 'echo true >&3; kill -SEGV \$\$'"
check "channel both holds" 4 "" -- -c "sh -c 'echo both >&3' && echo never"
grep -q "held #1" "$T/err" && ok || bad "hold announced"

# S4 cross-check with real programs
check "cross-check agree" 0 "x" -- -c "echo x <?> printf 'x\n'"
check "cross-check differ" 4 "$(printf 'x\n--- <?> the second command said:\ny')" -- -c "echo x <?> echo y"

# redirects, globs, variables, scripts
check "redirect out" 0 "hello" -- -c "echo hello > f.txt; cat < f.txt"
check "append" 0 "$(printf 'a\nb')" -- -c "echo a > g.txt; echo b >> g.txt; cat g.txt"
touch m1.zz m2.zz
check "glob" 0 "m1.zz m2.zz" -- -c "echo *.zz"
check "quoted glob" 0 "*.zz" -- -c "echo '*.zz'"
check "no match keeps word" 0 "*.nomatch" -- -c "echo *.nomatch"
cat > s.zxs <<'EOF'
# a script
greeting=hi
for w in a b; do
  echo "$greeting $w $arg1"
done
if sh -c 'echo both >&3'; then
  echo yes
both
  echo contradiction
fi
EOF
check "script" 0 "$(printf 'hi a P\nhi b P\ncontradiction')" -- s.zxs P
check "script syntax error runs nothing" 2 "" -- -c "echo a; if"
check "cd and pwd" 0 "$T" -- -c "cd $T && pwd"
check "cd failure" 1 "" -- -c "cd /no/such/dir"
check "sandbox denies write" 1 "" -- -c "sandbox exec -- echo x > denied.txt"
[ ! -e denied.txt ] && ok || bad "sandbox wrote a file"
check "scripts cannot resolve" 1 "" -- -c "both && echo x; resolve 1 true"

# H3 history + quarantine chains (history via piped stdin is not recorded;
# run an interactive-like file through the chain helpers instead)
"$Z" -c "sh -c 'kill -SEGV \$\$'" 2>/dev/null
"$Z" -c "sh -c 'kill -BUS \$\$'" 2>/dev/null
check "qlog verify" 0 "quarantine: $(wc -l < "$ZXSH_HOME/quarantine" | tr -d " ") lines, chain intact, head $(cut -c1-16 "$ZXSH_HOME/quarantine" | tail -1)" -- -c "qlog verify"
sed -i.bak 's/cmd=sh/cmd=XX/' "$ZXSH_HOME/quarantine"
check "qlog tamper is forbidden" 3 "" -- -c "qlog verify"
grep -q "altered" "$T/err" && ok || bad "tamper reason"

# stdin mode: lines evaluate as they arrive; multi-line constructs work
out=$(printf 'if true\nthen\n echo multi\nfi\necho next\n' | "$Z" 2>/dev/null)
[ "$out" = "$(printf 'multi\nnext')" ] && ok || bad "stdin multi-line: $out"
out=$(printf 'sh -c "kill -SEGV \\$\\$"\necho no\n' | "$Z" 2>/dev/null)
[ -z "$out" ] && ok || bad "stdin stops after forbidden: $out"

# H4 zxvd client against a one-shot fake server (python3 if present)
if command -v python3 >/dev/null 2>&1; then
    export ZXVD_SOCK="$T/zxvd.sock"
    python3 - "$ZXVD_SOCK" <<'EOF' &
import socket, sys, json
s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM); s.bind(sys.argv[1]); s.listen(4)
replies = [{"result": {"ok": True}}, {"error": {"code": -32601, "message": "no"}},
           {"result": {"verdict": "both"}}]
for r in replies:
    c, _ = s.accept(); req = json.loads(c.makefile().readline())
    r.update({"jsonrpc": "2.0", "id": req["id"]}); c.sendall((json.dumps(r) + "\n").encode()); c.close()
EOF
    i=0; while [ ! -S "$ZXVD_SOCK" ] && [ $i -lt 50 ]; do sleep 0.1; i=$((i + 1)); done
    check "zxv ok" 0 '{"result": {"ok": true}, "jsonrpc": "2.0", "id": 1}' -- -c "zxv zxvd.version"
    check "zxv error is false" 1 '{"error": {"code": -32601, "message": "no"}, "jsonrpc": "2.0", "id": 1}' -- -c "zxv bad.method {}"
    check "zxv verdict both" 4 '{"result": {"verdict": "both"}, "jsonrpc": "2.0", "id": 1}' -- -c "zxv x.y"
    wait
    check "no daemon is neither" 5 "" -- -c "zxv zxvd.version"
    check "zxv needs net" 1 "" -- -c "cap drop net; zxv zxvd.version"
    unset ZXVD_SOCK
fi

echo "$pass passed, $fail failed"
[ "$fail" = 0 ]
