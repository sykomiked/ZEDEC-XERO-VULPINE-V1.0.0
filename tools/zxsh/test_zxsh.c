/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* test_zxsh.c — the shell core (zxsh_core.h S1-S9) against a fake host.
 *
 * The fake host runs "programs" by name: ok, no, con, unk, crash return
 * true, false, both, neither and forbidden; "say WORDS" prints its words;
 * "cnt" prints how many programs ran. Every run is logged, so each test
 * checks both the verdict and exactly what ran. */
#include <stdio.h>
#include <string.h>
#include "zxsh_core.h"

static int pass, fail;
#define CHECK(c, msg)                                                                              \
    do {                                                                                           \
        if (c)                                                                                     \
            pass++;                                                                                \
        else {                                                                                     \
            fail++;                                                                                \
            printf("[FAIL] %s (%s:%d)\n", msg, __FILE__, __LINE__);                                \
        }                                                                                          \
    } while (0)

static char runlog[4096], outbuf[8192], errbuf[8192];
static unsigned nrun, nquar;
static char lastq[600];
static zxsh_stage_t last_stage;

static void cat(char *dst, size_t cap, const char *s, size_t n)
{
    size_t L = strlen(dst);
    if (L + n + 1 > cap) n = cap - L - 1;
    memcpy(dst + L, s, n);
    dst[L + n] = 0;
}

static void f_out(void *u, int fd, const char *s, size_t n)
{
    (void) u;
    cat(fd == 1 ? outbuf : errbuf, fd == 1 ? sizeof outbuf : sizeof errbuf, s, n);
}

static lpx_t f_exec(void *u, zxsh_t *sh, const zxsh_stage_t *st, unsigned n, char *cap, uint32_t capmax,
                    uint32_t *caplen, char *why, uint32_t whymax)
{
    (void) u;
    (void) sh;
    (void) whymax;
    lpx_t v = zxsh_tag(LPX_B_TRUE, LPX_P_NEUTRAL);
    char o[512] = "";
    for (unsigned i = 0; i < n; i++) {
        const char *c = st[i].argv[0];
        last_stage = st[i];
        nrun++;
        cat(runlog, sizeof runlog, c, strlen(c));
        cat(runlog, sizeof runlog, " ", 1);
        lpx_t t = zxsh_tag(LPX_B_TRUE, LPX_P_NEUTRAL);
        if (!strcmp(c, "no")) t = zxsh_tag(LPX_B_FALSE, LPX_P_NEUTRAL);
        else if (!strcmp(c, "con")) t = zxsh_tag(LPX_B_BOTH, LPX_P_NEUTRAL);
        else if (!strcmp(c, "conn")) t = zxsh_tag(LPX_B_BOTH, LPX_P_NEGENTROPIC);
        else if (!strcmp(c, "unk")) t = zxsh_tag(LPX_B_NEITHER, LPX_P_NEUTRAL);
        else if (!strcmp(c, "weak")) t = zxsh_tag(LPX_B_TRUE, LPX_P_ENTROPIC);
        else if (!strcmp(c, "crash")) {
            t = zxsh_tag(LPX_B_FALSE, LPX_P_FORBIDDEN);
            snprintf(why, whymax, "crash crashed");
        } else if (!strcmp(c, "say")) {
            for (int k = 1; k < st[i].argc; k++) {
                cat(o, sizeof o, st[i].argv[k], strlen(st[i].argv[k]));
                cat(o, sizeof o, k + 1 < st[i].argc ? " " : "\n", 1);
            }
        }
        v = lpx_and(v, t);
    }
    if (cap) {
        size_t L = strlen(o) < capmax ? strlen(o) : capmax;
        memcpy(cap, o, L);
        *caplen = (uint32_t) L;
    } else {
        cat(outbuf, sizeof outbuf, o, strlen(o));
    }
    return v;
}

static char envlog[256];
static void f_setenv(void *u, const char *n, const char *v)
{
    (void) u;
    snprintf(envlog, sizeof envlog, "%s=%s", n, v);
}

static void f_quar(void *u, const lpx_qrec_t *r, const char *cmd)
{
    (void) u;
    (void) r;
    nquar++;
    snprintf(lastq, sizeof lastq, "%s", cmd);
}

static const zxsh_ops_t OPS = {f_out, f_exec, f_setenv, f_quar};
static zxsh_t sh;

static zxsh_status_t run(const char *src)
{
    runlog[0] = outbuf[0] = errbuf[0] = 0;
    nrun = 0;
    return zxsh_eval(&sh, src);
}

static char L(void)
{
    return zxsh_letter(sh.last);
}

static void fresh(bool interactive)
{
    zxsh_init(&sh, &OPS, NULL, interactive);
    nquar = 0;
}

int main(void)
{
    /* S1 + builtins */
    fresh(true);
    CHECK(run("true") == ZXSH_OK && L() == 'T', "true builtin");
    CHECK(run("false") == ZXSH_OK && L() == 'F', "false builtin");
    CHECK(run("both") == ZXSH_OK && L() == 'B', "both builtin");
    CHECK(run("neither") == ZXSH_OK && L() == 'N', "neither builtin");
    CHECK(run("both negentropic") == ZXSH_OK && lpx_pol(sh.last) == LPX_P_NEGENTROPIC, "polarity arg");
    CHECK(run("verdict both entropic") == ZXSH_OK && L() == 'B' && lpx_pol(sh.last) == LPX_P_ENTROPIC,
          "verdict spelling");
    CHECK(run("true forbidden") == ZXSH_OK && L() == 'F', "a builtin cannot claim forbidden");
    CHECK(lpx_phase(sh.last) == LPX_PH_MANIFEST, "finished commands are manifest");

    /* S2 pipelines: every stage counts */
    run("no | ok | ok");
    CHECK(L() == 'F' && nrun == 3, "failing producer is not hidden");
    run("ok | con");
    CHECK(L() == 'B', "contradiction in a stage");
    run("ok | unk");
    CHECK(L() == 'N', "unknown stage");
    run("ok | ok");
    CHECK(L() == 'T', "all-true pipeline");
    run("! no");
    CHECK(L() == 'T', "negate false");
    run("! con");
    CHECK(L() == 'B', "negation keeps both");

    /* S3 and/or by the action table */
    run("ok && say yes");
    CHECK(!strcmp(outbuf, "yes\n"), "&& runs on proceed");
    run("no && say yes");
    CHECK(outbuf[0] == 0 && L() == 'F', "&& skips on refuse");
    run("no || say alt");
    CHECK(!strcmp(outbuf, "alt\n"), "|| runs on refuse");
    run("ok || say alt");
    CHECK(outbuf[0] == 0, "|| skips on proceed");
    run("no && say a || say b");
    CHECK(!strcmp(outbuf, "b\n"), "chain: false && a || b");

    /* S5 holds */
    CHECK(zxsh_held_count(&sh) == 0, "no holds yet");
    run("con && say deploy; say next");
    CHECK(!strcmp(outbuf, "next\n"), "both holds the rest, the next statement runs");
    CHECK(zxsh_held_count(&sh) == 1 && strstr(errbuf, "held #1"), "one hold announced");
    CHECK(sh.held[0].used && !strcmp(sh.held[0].text, "&& say deploy"), "hold keeps the continuation");
    CHECK(lpx_phase(sh.held[0].tag) == LPX_PH_POTENTIAL, "hold starts potential");
    CHECK(lpx_action(sh.held[0].tag) == LPX_ACT_ESCALATE, "neutral both escalates");
    run("unk || say fallback");
    CHECK(outbuf[0] == 0 && zxsh_held_count(&sh) == 2, "neither holds || too");
    run("weak && say go");
    CHECK(outbuf[0] == 0 && zxsh_held_count(&sh) == 3, "weakening truth waits");
    run("conn && say x && say y");
    CHECK(nrun == 1 && zxsh_held_count(&sh) == 4, "converging dispute waits, nothing more runs");
    run("held");
    CHECK(strstr(outbuf, "#1") && strstr(outbuf, "#4") && strstr(outbuf, "escalate"), "held lists all");

    run("resolve 1 true");
    CHECK(!strcmp(outbuf, "deploy\n") && zxsh_held_count(&sh) == 3, "resolve true runs &&-rest");
    run("resolve 2 false");
    CHECK(!strcmp(outbuf, "fallback\n"), "resolve false runs ||-rest");
    run("resolve 3 false");
    CHECK(outbuf[0] == 0 && zxsh_held_count(&sh) == 1, "resolve false skips &&-rest");
    run("resolve 4 both");
    CHECK(outbuf[0] == 0 && zxsh_held_count(&sh) == 1, "resolve both re-holds");
    run("drop 5");
    CHECK(zxsh_held_count(&sh) == 0 && outbuf[0] == 0, "drop runs nothing");
    run("resolve 99 true");
    CHECK(L() == 'F', "unknown hold refused");

    /* S5: scripts cannot resolve */
    fresh(false);
    run("con && say x");
    run("resolve 1 true");
    CHECK(L() == 'F' && outbuf[0] == 0 && zxsh_held_count(&sh) == 1, "non-interactive cannot resolve");

    /* S4 cross-check */
    fresh(true);
    run("say a <?> say a");
    CHECK(L() == 'T' && lpx_pol(sh.last) == LPX_P_NEGENTROPIC && !strcmp(outbuf, "a\n"), "agreement");
    run("say a <?> say b");
    CHECK(L() == 'B' && lpx_pol(sh.last) == LPX_P_ENTROPIC && strstr(outbuf, "second"), "disagreement");
    run("ok <?> no");
    CHECK(L() == 'B', "verdicts differ -> both");
    run("no <?> no");
    CHECK(L() == 'F' && lpx_pol(sh.last) == LPX_P_NEGENTROPIC, "agreeing refusal");
    run("unk <?> ok");
    CHECK(L() == 'T' && lpx_pol(sh.last) == LPX_P_ENTROPIC, "neither joins to the other");
    run("both <?> true");
    CHECK(L() == 'B', "builtins can be cross-checked");
    run("say a <?> say b && say after");
    CHECK(zxsh_held_count(&sh) == 1 && !strstr(outbuf, "after"), "disagreement holds the chain");
    CHECK(run("ok <?> ok <?> ok") == ZXSH_SYNTAX && nrun == 0, "one <?> per check");

    /* S6 forbidden */
    fresh(false);
    zxsh_status_t st = run("say one; crash arg; say two");
    CHECK(st == ZXSH_ABORTED && !strcmp(outbuf, "one\n"), "forbidden aborts the rest");
    CHECK(nquar == 1 && !strcmp(lastq, "crash arg"), "quarantine record handed to host");
    CHECK(sh.qlog.n == 1 && lpx_qlog_verify(&sh.qlog), "qlog chain");
    CHECK(L() == '!', "forbidden letter");
    run("for i in a b c; do crash; done");
    CHECK(nrun == 1 && sh.qlog.n == 2, "forbidden stops a loop");
    run("! crash");
    CHECK(lpx_forbidden(sh.last), "negation keeps forbidden");
    run("say ok");
    CHECK(!strcmp(outbuf, "ok\n"), "next input runs again");

    /* S7 if */
    fresh(true);
    run("if ok; then say T; else say F; fi");
    CHECK(!strcmp(outbuf, "T\n"), "if true");
    run("if no; then say T; else say F; fi");
    CHECK(!strcmp(outbuf, "F\n"), "if false");
    run("if no; then say T; fi");
    CHECK(outbuf[0] == 0 && L() == 'T', "if false without else is true");
    run("if con; then say T; else say F; both say B; neither say N; fi");
    CHECK(!strcmp(outbuf, "B\n"), "both clause");
    run("if unk; then say T; both say B; neither say N; fi");
    CHECK(!strcmp(outbuf, "N\n"), "neither clause");
    run("if con; then say T; else say F; fi");
    CHECK(outbuf[0] == 0 && zxsh_held_count(&sh) == 1 && L() == 'B', "if without both clause holds");
    CHECK(sh.held[0].is_if && !strncmp(sh.held[0].text, "then", 4), "hold keeps the if body");
    run("resolve 1 false");
    CHECK(!strcmp(outbuf, "F\n"), "resolving an if picks its branch");
    run("if ok; then if con; then say in; both say inner-both; fi; fi");
    CHECK(!strcmp(outbuf, "inner-both\n"), "nested if");
    run("if ok\nthen\n  say multi\nfi");
    CHECK(!strcmp(outbuf, "multi\n"), "multi-line if");
    run("if ok; then verdict both; fi");
    CHECK(L() == 'B', "verdict both inside a clause");
    CHECK(run("if ok; then say a; then say b; fi") == ZXSH_SYNTAX && nrun == 0, "repeated then");
    CHECK(run("if ok; then say a; else say b; else say c; fi") == ZXSH_SYNTAX, "repeated else");
    CHECK(run("fi") == ZXSH_SYNTAX, "stray fi");

    /* S8 for */
    run("for x in a b c; do say $x; done");
    CHECK(!strcmp(outbuf, "a\nb\nc\n"), "for loop");
    run("for x in a b; do no; done");
    CHECK(L() == 'F', "loop verdict is the and of iterations");
    run("for x in a b; do ok; done");
    CHECK(L() == 'T', "all-true loop");
    run("for x in; do say never; done");
    CHECK(outbuf[0] == 0, "empty loop");
    run("for x in a b; do if con; then say t; both say B$x; fi; done");
    CHECK(!strcmp(outbuf, "Ba\nBb\n"), "if inside for");

    /* NEED_MORE and syntax */
    CHECK(run("if ok; then") == ZXSH_NEED_MORE, "need more: if");
    CHECK(run("ok &&") == ZXSH_NEED_MORE, "need more: &&");
    CHECK(run("say 'open") == ZXSH_NEED_MORE, "need more: quote");
    CHECK(run("ok | | ok") == ZXSH_SYNTAX && nrun == 0, "empty stage");
    CHECK(run("say a & say b") == ZXSH_SYNTAX && nrun == 0, "no background jobs");
    CHECK(run("say a; ok &&& say b") == ZXSH_SYNTAX && nrun == 0, "syntax error means nothing ran");
    CHECK(run("# just a comment") == ZXSH_OK && nrun == 0, "comment");

    /* expansion */
    run("NAME=world");
    CHECK(zxsh_get(&sh, "NAME") && !strcmp(zxsh_get(&sh, "NAME"), "world"), "assignment");
    run("say \"hello $NAME\" 'and $NAME' ${NAME}s \\$NAME");
    CHECK(!strcmp(outbuf, "hello world and $NAME worlds $NAME\n"), "quotes and variables");
    run("con; say $?");
    CHECK(!strcmp(outbuf, "both\n"), "$? is the verdict word");
    run("say *.c a\\*");
    CHECK(last_stage.glob == 1u << 1, "glob flag only on unquoted magic");
    run("say \"*.c\"");
    CHECK(last_stage.glob == 0, "quoted glob is literal");
    run("export ZX=1");
    CHECK(!strcmp(envlog, "ZX=1"), "export");
    run("unset NAME");
    CHECK(zxsh_get(&sh, "NAME") == NULL, "unset");
    run("say ~/x");
    CHECK(!strcmp(outbuf, "~/x\n"), "tilde without HOME");
    zxsh_set(&sh, "HOME", "/h");
    run("say ~/x");
    CHECK(!strcmp(outbuf, "/h/x\n"), "tilde with HOME");

    /* S9 capabilities */
    run("say a > f");
    CHECK(nrun == 1 && last_stage.out && !strcmp(last_stage.out, "f"), "redirect passed through");
    run("cap drop write");
    run("say a > f");
    CHECK(nrun == 0 && L() == 'F' && strstr(errbuf, "write"), "write capability enforced");
    run("cap drop all");
    CHECK(sh.caps == 0, "drop all");
    run("say x");
    CHECK(nrun == 0 && L() == 'F', "exec capability enforced");
    run("cap drop bogus");
    CHECK(L() == 'F', "unknown cap refused");
    fresh(true);
    run("sandbox read -- say hi");
    CHECK(nrun == 0 && L() == 'F', "sandbox without exec refuses");
    run("sandbox exec -- say hi > out");
    CHECK(nrun == 0 && L() == 'F', "sandbox without write refuses redirect");
    run("sandbox exec -- say hi");
    CHECK(nrun == 1 && !strcmp(outbuf, "hi\n") && sh.caps == ZXSH_CAP_ALL, "sandbox runs, caps restored");
    run("sandbox exec,write -- say *.x");
    CHECK(last_stage.glob == 1u << 1, "sandbox shifts glob flags");
    run("sandbox exec,bogus -- say hi");
    CHECK(nrun == 0 && L() == 'F', "sandbox unknown cap");
    CHECK(zxsh_cap_parse("exec,read") == (ZXSH_CAP_EXEC | ZXSH_CAP_READ), "cap parse");
    CHECK(zxsh_cap_parse("exec,nope") == 0, "cap parse unknown");
    run("both > f");
    CHECK(L() == 'F', "redirects on builtins refused");

    /* why */
    run("con");
    run("why");
    CHECK(strstr(outbuf, "both/neutral/manifest -> escalate") && L() == 'B', "why explains");

    /* exit */
    CHECK(run("exit 7") == ZXSH_EXIT && sh.exit_code == 7, "exit code");
    fresh(true);
    CHECK(run("exit both") == ZXSH_EXIT && sh.exit_code == 2, "exit verdict");
    fresh(true);
    CHECK(run("say a; exit; say b") == ZXSH_EXIT && !strcmp(outbuf, "a\n"), "exit stops");

    /* robustness: long and odd input does not crash */
    fresh(true);
    static char big[20000];
    memset(big, 'a', sizeof big - 1);
    big[sizeof big - 1] = 0;
    CHECK(run(big) == ZXSH_OK && L() == 'F', "huge word refused, not overflowed");
    static char many[8000];
    many[0] = 0;
    for (int i = 0; i < 1500; i++) cat(many, sizeof many, "ok;", 3);
    CHECK(run(many) == ZXSH_SYNTAX && nrun == 0, "too many tokens");
    CHECK(run("say ${unterminated") == ZXSH_OK && L() == 'F', "bad ${ refused");
    CHECK(run("ok | ok | ok | ok | ok | ok | ok | ok | ok") == ZXSH_SYNTAX, "too many stages");

    printf("%d passed, %d failed\n", pass, fail);
    return fail ? 1 : 0;
}
