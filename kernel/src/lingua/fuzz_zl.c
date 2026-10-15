/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* fuzz_zl.c — mutation fuzzer for the three Lingua front-ends and the
 * runtime. Every input is offered to every language. For each one that
 * compiles: the IR verifies, it runs to an ok status or a trap within a
 * small fuel budget, its twin compiles in twin mode to the same IR, and the
 * twin of the twin is the twin. Any input accepted in twin mode must equal
 * its own twin byte for byte. Every accepted program packs and reads back as
 * the same IR, and a randomly damaged pack is refused or still verifies.
 * Run under ASan + UBSan.
 *
 * Usage: fuzz_zl [iterations] [seed] */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "zl.h"

static const char *const seeds[] = {
    "10 FOR I = 1 TO 10 STEP 2\n20 IF INT(I/3)*3 = I THEN 40\n30 PRINT I; \"X\",\n40 NEXT I\n50 "
    "DIM A(4)\n"
    "60 LET A(2) = -2^3\n70 READ B, C\n80 DATA 4, -5\n90 GOSUB 200\n100 ON B-3 GOTO 110, 120\n110 "
    "PRINT FNA(B)\n"
    "120 DEF FNA(X) = X*X + 1\n130 STOP\n200 PRINT ABS(C); SGN(C)\n210 RETURN\n220 END\n",
    "C     FUZZ SEED\n      PROGRAM F\n      INTEGER A(0:5), K\n      PARAMETER (K = 3)\n      DO "
    "10 I = 1, K\n"
    "      A(I) = MOD(I ** 2, 7) - ISIGN(I, -1)\n   10 CONTINUE\n      IF (A(1) .GT. 0 .AND. .NOT. "
    "A(2) .EQ. 3) THEN\n"
    "        PRINT *, 'Y', A(1), MAX(A(2), A(3), 0)\n      ELSE IF (K == 3) THEN\n        GOTO "
    "20\n      ELSE\n"
    "        PRINT *\n      END IF\n   20 IF (K - 3) 30, 30, 30\n   30 GO TO (40, 40), K\n   40 J "
    "= 1\n"
    "      DO WHILE (J .LT. 99)\n        J = J * 2 + 1\n      END DO\n      WRITE (*, *) J, 'Z'\n  "
    "    END\n",
    "\\ fuzz seed\n: SQ ( n -- n ) DUP * ; : T 10 0 DO I SQ . 3 +LOOP ; T CR\nVARIABLE V 5 V ! 3 V "
    "+! V ? 7 CONSTANT K\n"
    "CREATE B 4 CELLS ALLOT K B 2 CELLS + ! B 2 CELLS + @ .\n: F DUP 1 > IF DUP 1- RECURSE * THEN "
    "; 10 F .\n"
    ": W BEGIN DUP WHILE DUP . 1- REPEAT DROP ; 4 W .\" done\" 42 EMIT\n: U 0 BEGIN 1+ DUP 5 = "
    "UNTIL ; U . 5 ?DUP . .\n",
    "{$mode iso}\nprogram fz(input, output);\nconst k = -3; t = true;\nvar i, n: integer; b: "
    "boolean; a: array[-2..4] of integer;\nprocedure p(x: integer; var y: integer);\nvar w: "
    "array[0..2] of integer;\n  procedure q; begin y := y + x; w[1] := y end;\nbegin q; q; "
    "writeln(y:4, w[1] mod 5) end;\nfunction f(m: integer): integer;\nbegin if m <= 0 then f "
    ":= 1 else f := m * f(m - 1) end;\nbegin\n  read(n); for i := -2 to 4 do a[i] := sqr(i) "
    "div 2;\n  i := 0; repeat i := succ(i) until (i > 3) or odd(i * k);\n  while n > 0 do begin "
    "p(n, a[n]); n := n - 2 end;\n  case abs(k) of 1, 2: b := t; 3: b := not t and (n < 0) "
    "end;\n  if b then writeln('yes', f(5):6, -maxint div 7) else write(ord(b), pred(i));\n  "
    "(* done *) writeln(a[4], true:3, 'end':2)\nend.\n",
    "begin comment fuzz seed; integer i, s; Boolean b; integer array a[0:4];\n"
    "  integer procedure f(x, y); value x; integer x, y;\n"
    "    f := if x > 0 then x * y + f(x - 1, y) else 0;\n"
    "  procedure p(n); value n; integer n;\n"
    "  begin integer array c[1:n]; c[n] := n; s := s + c[n] end;\n"
    "  for i := 0 step 1 until 4, 7 while i > 9 do a[i] := i ^ 2 % 3 - 1;\n"
    "  b := !(s = 0) | a[1] <= 2 -> s != 3 == true; ininteger(0, s);\n"
    "  L: if b & s < 10 then begin s := s + f(2, i); goto L end else p(3);\n"
    "  outinteger(1, s); outstring(1, \"ok\\n\")\n"
    "end\n",
};

static const char *const dict[] = {
    " ",
    "\n",
    "(",
    ")",
    ",",
    ";",
    "=",
    "+",
    "-",
    "*",
    "/",
    "^",
    "**",
    ".",
    "'",
    "\"",
    ":",
    "\t",
    "     &",
    "   10 ",
    "10 ",
    "GOTO ",
    "GOSUB ",
    "RETURN",
    "IF ",
    "THEN ",
    "ELSE",
    "END",
    "DO ",
    "LOOP",
    "FOR ",
    "NEXT ",
    "PRINT ",
    "LET ",
    "INTEGER ",
    "DIMENSION ",
    "PARAMETER ",
    ".EQ.",
    ".AND.",
    ".NOT.",
    "CONTINUE",
    "STOP",
    "DUP ",
    "DROP ",
    "SWAP ",
    "I ",
    "J ",
    "LEAVE ",
    "+LOOP ",
    "BEGIN ",
    "UNTIL ",
    "WHILE ",
    "REPEAT ",
    "RECURSE ",
    "EXIT ",
    "VARIABLE ",
    "CONSTANT ",
    "CREATE ",
    "ALLOT ",
    "@ ",
    "! ",
    "999999999999 ",
    "-9223372036854775808",
    "2147483648",
    "0",
    "-1",
    "DATA ",
    "READ ",
    "DEF FNA(X)=",
    "ON ",
    "OPTION BASE 1\n",
    "C",
    "*",
    "!",
    "ENDDO",
    "ENDIF",
    "ELSEIF(",
    "READ *, I",
    "MOD(",
    "MAX(",
    "A(",
    "(1:2)",
    "EMIT ",
    ".\" x\" ",
};

static uint64_t rng;
static uint32_t rnd(uint32_t n)
{
    rng ^= rng << 13;
    rng ^= rng >> 7;
    rng ^= rng << 17;
    return n ? (uint32_t) (rng % n) : 0;
}

static zl_work_t work;
static zl_prog_t p1, p2;
static zl_run_t run;
static char buf[1 << 15], tw1[1 << 16], tw2[1 << 16], outb[4096], pk[1 << 19];

static void die(const char *what, int lang, size_t n)
{
    printf("FUZZ FAILURE: %s (language %d)\ninput (%zu bytes):\n", what, lang, n);
    fwrite(buf, 1, n, stdout);
    printf("\n");
    exit(1);
}

static size_t mutate(size_t n)
{
    int rounds = rnd(4) == 0 ? 2 + (int) rnd(5) : 1;
    while (rounds--) {
        uint32_t k = rnd(6);
        if (k == 0 && n) { /* flip a byte */
            buf[rnd((uint32_t) n)] = (char) rnd(256);
        } else if (k == 1 && n) { /* delete a span */
            size_t a = rnd((uint32_t) n), l = 1 + rnd(12);
            if (a + l > n) l = n - a;
            memmove(buf + a, buf + a + l, n - a - l);
            n -= l;
        } else if (k == 2 && n) { /* duplicate a span */
            size_t a = rnd((uint32_t) n), l = 1 + rnd(40);
            if (a + l > n) l = n - a;
            if (n + l < sizeof buf) {
                memmove(buf + a + l, buf + a, n - a);
                n += l;
            }
        } else { /* insert a dictionary token */
            const char *t = dict[rnd(sizeof dict / sizeof dict[0])];
            size_t l = strlen(t), a = n ? rnd((uint32_t) n + 1) : 0;
            if (n + l < sizeof buf) {
                memmove(buf + a + l, buf + a, n - a);
                memcpy(buf + a, t, l);
                n += l;
            }
        }
    }
    return n;
}

int main(int argc, char **argv)
{
    long iters = argc > 1 ? atol(argv[1]) : 20000;
    rng = argc > 2 ? strtoull(argv[2], 0, 10) * 0x9E3779B97F4A7C15ull + 1 : 1;
    long accepted = 0, twin_ok = 0, ran_ok = 0, trapped = 0;
    for (long it = 0; it < iters; it++) {
        const char *seed = seeds[rnd(sizeof seeds / sizeof seeds[0])];
        size_t n = strlen(seed);
        memcpy(buf, seed, n);
        if (rnd(8) == 0) { /* sometimes pure noise */
            n = rnd(200);
            for (size_t i = 0; i < n; i++) buf[i] = (char) rnd(256);
        } else {
            n = mutate(n);
        }
        for (int lang = 1; lang < ZL_LANG__COUNT; lang++) {
            zl_diag_t d;
            size_t l1 = 0, l2 = 0;
            int s = zl_compile(lang, buf, n, &p1, &work, &d);
            if (s > 0 || (s != ZL_OK && (d.status != s || !d.msg)))
                die("bad status or diagnostic", lang, n);
            if (s != ZL_OK) {
                if (p1.ncode || p1.nslots) die("refused program not zeroed", lang, n);
                if (zl_compile(lang | ZL_TWIN, buf, n, &p2, &work, &d) == ZL_OK)
                    die("twin accepts what the language refuses", lang, n);
                continue;
            }
            accepted++;
            if (zl_verify(&p1, &d) != ZL_OK) die("compiled program fails verification", lang, n);
            memset(&run, 0, sizeof run);
            int64_t in[4] = {3, -2, 7, 0};
            run.in = in;
            run.nin = 4;
            run.out = outb;
            run.out_cap = sizeof outb;
            run.fuel = 20000;
            s = zl_run(&p1, &run);
            if (s == ZL_OK)
                ran_ok++;
            else if (s <= -2620 && s >= -2630)
                trapped++;
            else
                die("run returned a non-trap status", lang, n);
            if (run.out_len > sizeof outb || run.used > 20000)
                die("run overstepped its limits", lang, n);
            size_t pn = 0;
            if (zl_pack(&p1, pk, sizeof pk, &pn) != ZL_OK)
                die("accepted program does not pack", lang, n);
            if (zl_unpack(pk, pn, &p2, &d) != ZL_OK || !zl_prog_equal(&p1, &p2))
                die("packed program does not read back", lang, n);
            pk[rnd((uint32_t) pn)] =
                (char) rnd(256); /* a damaged pack is refused or still verifies */
            if (zl_unpack(pk, pn, &p2, &d) == ZL_OK && zl_verify(&p2, &d) != ZL_OK)
                die("damaged pack accepted without verifying", lang, n);
            s = zl_twin(lang, buf, n, tw1, sizeof tw1, &l1, &work, &d);
            if (s == ZL_E_LIMIT) continue;
            if (s != ZL_OK) die("accepted program has no twin", lang, n);
            s = zl_compile(lang | ZL_TWIN, tw1, l1, &p2, &work, &d);
            if (s != ZL_OK) die("twin not accepted as canonical", lang, n);
            if (!zl_prog_equal(&p1, &p2)) die("twin compiles to different IR", lang, n);
            s = zl_twin(lang, tw1, l1, tw2, sizeof tw2, &l2, &work, &d);
            if (s != ZL_OK || l1 != l2 || memcmp(tw1, tw2, l1))
                die("twin of the twin differs", lang, n);
            s = zl_compile(lang | ZL_TWIN, buf, n, &p2, &work, &d);
            if (s == ZL_OK && (l1 != n || memcmp(tw1, buf, n)))
                die("twin mode accepted a non-canonical text", lang, n);
            if (s != ZL_OK && l1 == n && memcmp(tw1, buf, n) == 0)
                die("canonical text refused in twin mode", lang, n);
            twin_ok++;
        }
    }
    printf("%ld iterations, %ld accepted, %ld twins checked, %ld ran ok, %ld trapped\n", iters,
           accepted, twin_ok, ran_ok, trapped);
    return 0;
}
