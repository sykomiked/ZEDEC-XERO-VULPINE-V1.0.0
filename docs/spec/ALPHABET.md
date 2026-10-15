<!-- Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# The alphabet (booklet 000)

The alphabet is the set of Lingua IR operations. Every program in every
language Lingua reads (each with its AI twin) compiles to this IR, and
booklet 000 of the matrix core holds it. The rows live in
`kernel/src/lingua/alphabet.tsv`: opcode order, family, stack effect, the
projections that take each op, and what it does. `zl_run` is the one
definition of what each operation does; nothing on this page changes that.

## 1. Families

| Family | Ops | What they do |
|---|---|---|
| stop | 2 | HALT, TRAP: end the program |
| permute | 6 | PUSH a constant; DUP, DROP, SWAP, OVER, ROT |
| state | 10 | slots, arrays, addresses and frames; ADDRF, BOUND |
| arith | 18 | integer arithmetic, sign, range, shifts, WRAP |
| compare | 11 | comparisons and the bitwise and logical joins |
| branch | 2 | JZ, JNZ |
| recur | 1 | JMP |
| compose | 4 | CALL, RET, CALLI, JMPI |
| resource | 6 | frames (ENTER, LEAVE, LINK) and handlers (CATCH, UNCATCH, THROW) |
| data | 14 | pairs, vectors and strings on the heap |
| effect | 9 | input and output |

`alphabet.tsv` is the authority for the counts; `test_zl_proj` checks every
row against the IR.

## 2. Projections

`zl_project` writes a verified program out as Forth (3), C (101) or x86-64
assembly (121); the numbers are Alexandria classification language numbers.
C and x86-64 run on `zxp_rt` and must match `zl_run` exactly (output, fuel,
trap and the trapping instruction); Forth must print the same. The
`targets` column says which projections take each op (`c`, `f`, `x`).
`make test-lingua-proj` checks them.

## 3. The Dhātu view

Each operation also has a name in Pāṇini's terms and a 64-bit sutra cell
(`kernel/src/lingua/zl_sutra.h`):

- **dhātu**: the verbal root naming the action (√grah take, √dhṛ hold,
  √gam go). 37 roots cover the 83 operations.
- **upasarga**: one of the 22 Pāṇinian prefixes, directing the root (ADD is
  √kal with sam-, SUB is √kal with vi-).
- **pratyaya**: the variant (slot, array, address or frame; truncating or
  flooring quotient).
- **kāraka**: the six roles, and where each operand comes from.
- **lpres**: the four-valued evidence tag, N/T/F/B encoded as `lpres`
  encodes them. lpres combines them as min and max on the chain
  F < N < B < T, not by Belnap's lattice, so a contradiction plus a gap never
  gives T.

The view is a naming of the IR. There is one cell per operation and one
operation per cell, and `test_zl_sutra` checks it both ways for all four
evidence tags. It adds no operation and no opcode. An action that has no
single op (for example "grant") is a CALL of a verified block, and its name
belongs to that block, not to the alphabet.

### 3.1 The cell

Low bit first. Fields are read and written with shifts and masks, never
packed bitfields, so the layout is the same on every compiler and platform.

| Bits | Field | Width | Values |
|---|---|---|---|
| 0..11 | dhātu | 12 | 0 no root; 1..4095 a root (the table's index plus 1) |
| 12..16 | upasarga | 5 | 0 none; 1..22 |
| 17..24 | pratyaya | 8 | 0 none; the variant |
| 25..48 | kāraka | 24 | six 4-bit operand codes; role r at bit 25 + 4r |
| 49..59 | sandhi | 11 | how two cells join; reserved, 0 |
| 60..61 | lpres | 2 | 0 N, 1 T, 2 F, 3 B |
| 62..63 | svara | 2 | reserved, 0 |

The widths add to 64. Dhātu 0 is reserved, so no operation's cell is 0 and
0 can mean "no cell". `zl_sutra_op` refuses a cell whose sandhi or svara is
set, or whose fields are not exactly one operation's.

The roles, in cell order: kartṛ (agent), karman (object), karaṇa
(instrument), sampradāna (recipient), apādāna (source), adhikaraṇa (locus).
Kartṛ is always empty here: the agent of an IR operation is the running
program.

| Code | Letter | Operand |
|---|---|---|
| 0 | `-` | the role is absent |
| 1 | `1` | the top of the stack |
| 2 | `2` | the second |
| 3 | `3` | the third |
| 4 | `*` | stack cells, how many depends on the operands |
| 5 | `t` | the new top of the stack (a result) |
| 6 | `k` | the instruction's constant |
| 7 | `s` | a named slot |
| 8 | `a` | an array cell |
| 9 | `p` | a cell through an address |
| 10 | `f` | a frame cell |
| 11 | `c` | a place in the program |
| 12 | `i` | the input |
| 13 | `o` | the output |
| 14 | `h` | a pair, vector or string |
| 15 | `x` | an exception handler |

For every operation with a fixed stack effect, the test checks that the
kāraka names exactly the cells it pops (1 up to pops, each once), and that it
has a result (`t`) exactly when it pushes.

Fixed answer: ADD with evidence T is `10000402a0004005`.

### 3.2 Readings of common actions

| Action | Reading |
|---|---|
| √gam, route | JMP, JZ, JNZ, JMPI (√gam), RET (prati-√gam) |
| √yu, bind | CONS and SCAT (√yu) |
| √jñā, cognize | ISA (√jñā, kind) |
| √dhṛ, pin | STORE and its variants (√dhṛ) |
| √dā, grant | no single op: a CALL of the verified block that grants |

### 3.3 What the view does and does not claim

- **The same CID means the same canonical IR.** Two programs written in
  different languages get the same block, and so the same CID, only when
  they compile to byte-identical IR (`zl_prog_equal`). That is checked, not
  assumed.
- **Equivalence beyond that is a witness, not a proof.** Whether two
  different programs always behave the same is undecidable in general. Lingua
  runs them side by side on the same inputs (a differential witness). Where
  they agree, that is evidence for those inputs. Where they disagree, both
  answers are kept and the result is tagged B; neither is thrown away.
- **Translation between human languages is not lossless.** Concept nodes can
  carry a meaning across languages, but not every nuance survives, and the
  view does not claim it does.
- **First contact is speculative.** Using this alphabet to talk to a mind
  that shares none of our languages is an idea to explore, not a property of
  the code.

### 3.4 Where it lives and how it is hashed

The alphabet and its Dhātu view are core: booklet 000 of the matrix, with
Lingua, not a separate layer. Blocks are named by raw sha2-256 CIDs, like
every other block in the matrix. A hash detects corruption and lets the
exact block be fetched again; it does not make the content safe.

## 4. The number view

Each root also has a number, by Kaṭapayādi (`kernel/src/lingua/zl_kpy.h`).
Every consonant gives a digit:

| Digit | Consonants |
|---|---|
| 1 | k ṭ p y |
| 2 | kh ṭh ph r |
| 3 | g ḍ b l |
| 4 | gh ḍh bh v |
| 5 | ṅ ṇ m ś |
| 6 | c t ṣ |
| 7 | ch th s |
| 8 | j d h |
| 9 | jh dh |
| 0 | ñ n |

A name is read one consonant group at a time. In a group, only the last
consonant counts (kṣ counts as ṣ), and a vowel with no consonant before it
counts 0. Vowels, anusvāra and visarga give no digit of their own. The digits
run right to left in place value (ankānām vāmato gatiḥ), so the first digit
is the units: √gam is 3 then 5, the number 53. The verse rule skips a final
consonant with no vowel after it; a root is named in its bare form, so here
that consonant counts. √kṛ is 1, because ṛ is a vowel. The table in
section 5 gives each root's number.

The numbers are labels. Many names share a number, so a number does not
give a name back. `zl_kpy_spell` writes one canonical spelling (1 ka,
2 kha, 3 ga, 4 gha, 5 ma, 6 ca, 7 cha, 8 ja, 9 jha, 0 na) that reads back
to the same number, leading zeros kept. `test_zl_kpy` checks 200,000 of
those round trips.

**Digital roots and residues.** `zl_dr9` gives the digital root, and
`zl_dr9_be` and `zl_mod_be` read a big-endian byte string, such as a CID's
32 hash bytes, as one number and give its digital root or its residue mod
12, 144 or 108. These are quick checks that a hand-copied number was copied
right. They do not place anything. Where a block goes is MATRIX.tsv's job
and is checked by its CID; a block is never placed by its hash's residue.

**No root-9 seal.** Making a block's digital root come out 9 by adding a
nonce proves nothing. Any number reaches 9 within 8 steps, and one random
block in 9 is there already, so it costs nothing to forge and filters
nothing. The test measures both. A block is admitted by its CID matching its
bytes and, where it is signed, by its ML-DSA signature.

## 5. The table

Generated by `test_zl_sutra kernel/src/lingua/alphabet.tsv TABLE.MD`. The
cell column shows each op's cell with evidence N.

| Op | Family | Root | Sense | Kaṭapayādi | Upasarga | Pratyaya | Kāraka | Cell (N) |
|---|---|---|---|---|---|---|---|---|
| HALT | stop | √sthā | stand / stop | 7 |  |  | `------` | `0000000000000001` |
| PUSH | permute | √dhā | place | 9 |  | const | `-tk---` | `0000000ca0120002` |
| LOAD | state | √grah | take | 82 |  | slot | `-t--s-` | `00000e00a0020003` |
| STORE | state | √dhṛ | hold | 9 |  | slot | `-s--1-` | `00000200e0020004` |
| LOADX | state | √grah | take | 82 |  | array | `-t--a1` | `00003000a0040003` |
| STOREX | state | √dhṛ | hold | 9 |  | array | `-a--12` | `0000420100040004` |
| ADD | arith | √kal | reckon | 31 | sam |  | `-t1-2-` | `00000402a0004005` |
| SUB | arith | √kal | reckon | 31 | vi |  | `-t1-2-` | `00000402a000b005` |
| MUL | arith | √guṇ | multiply | 53 |  |  | `-t1-2-` | `00000402a0000006` |
| DIVT | arith | √hṛ | take away | 8 |  | trunc-quotient | `-t1-2-` | `00000402a0140007` |
| MODT | arith | √hṛ | take away | 8 |  | trunc-remainder | `-t1-2-` | `00000402a0160007` |
| DIVF | arith | √hṛ | take away | 8 |  | floor-quotient | `-t1-2-` | `00000402a0180007` |
| MODF | arith | √hṛ | take away | 8 |  | floor-remainder | `-t1-2-` | `00000402a01a0007` |
| DIVX | arith | √hṛ | take away | 8 |  | exact-quotient | `-t1-2-` | `00000402a01c0007` |
| POW | arith | √vṛdh | grow | 94 |  |  | `-t1-2-` | `00000402a0000008` |
| NEG | arith | √vṛt | turn | 64 | pari | numeric | `-t--1-` | `00000200a0435009` |
| ABS | arith | √mā | measure | 5 |  | magnitude | `-t--1-` | `00000200a052000a` |
| SGN | arith | √lakṣ | mark | 63 |  |  | `-t--1-` | `00000200a000000b` |
| MIN | arith | √kṣi | diminish | 6 |  |  | `-t1-2-` | `00000402a000000c` |
| MAX | arith | √bṛh | grow great | 83 |  |  | `-t1-2-` | `00000402a000000d` |
| EQ | compare | √tul | weigh | 36 |  | eq | `-t1-2-` | `00000402a01e000e` |
| NE | compare | √tul | weigh | 36 |  | ne | `-t1-2-` | `00000402a020000e` |
| LT | compare | √tul | weigh | 36 |  | lt | `-t1-2-` | `00000402a022000e` |
| LE | compare | √tul | weigh | 36 |  | le | `-t1-2-` | `00000402a024000e` |
| GT | compare | √tul | weigh | 36 |  | gt | `-t1-2-` | `00000402a026000e` |
| GE | compare | √tul | weigh | 36 |  | ge | `-t1-2-` | `00000402a028000e` |
| AND | compare | √yuj | yoke | 81 | sam |  | `-t1-2-` | `00000402a000400f` |
| OR | compare | √kḷp | arrange | 11 | vi |  | `-t1-2-` | `00000402a000b010` |
| XOR | compare | √yuj | yoke | 81 | vi |  | `-t1-2-` | `00000402a000b00f` |
| INVERT | compare | √vṛt | turn | 64 | pari | bitwise | `-t--1-` | `00000200a0455009` |
| NOT | compare | √sidh | ward off | 97 | ni |  | `-t--1-` | `00000200a000d011` |
| DUP | permute | √kṛ | make | 1 | anu |  | `-t--1-` | `00000200a0005012` |
| DROP | permute | √tyaj | abandon | 81 |  |  | `-1----` | `0000000020000013` |
| SWAP | permute | √mī | exchange | 5 | vi |  | `-t1-2-` | `00000402a000b014` |
| OVER | permute | √kṛ | make | 1 | anu | second | `-t1-2-` | `00000402a0465012` |
| ROT | permute | √vṛt | turn | 64 | ā | three | `-t1-23` | `00006402a048c009` |
| JMP | recur | √gam | go | 53 |  |  | `-c----` | `0000000160000015` |
| JZ | branch | √gam | go | 53 |  | zero | `-c1---` | `00000003602a0015` |
| JNZ | branch | √gam | go | 53 |  | nonzero | `-c1---` | `00000003602c0015` |
| CALL | compose | √hve | call | 4 | ā |  | `-c----` | `000000016000c016` |
| RET | compose | √gam | go | 53 | prati |  | `-*----` | `0000000080014015` |
| OUTI | effect | √likh | write | 23 |  | integer | `-1-o--` | `000001a020320017` |
| OUTS | effect | √likh | write | 23 |  | string | `-k-o--` | `000001a0c0100017` |
| OUTC | effect | √likh | write | 23 |  | byte | `-1-o--` | `000001a020340017` |
| NL | effect | √likh | write | 23 |  | newline | `---o--` | `000001a0003a0017` |
| TAB | effect | √likh | write | 23 |  | tab | `---o--` | `000001a0003c0017` |
| IN | effect | √śru | hear | 2 |  |  | `-t--i-` | `00001800a0000018` |
| LOADA | state | √grah | take | 82 |  | address | `-t--p1` | `00003200a0060003` |
| STOREA | state | √dhṛ | hold | 9 |  | address | `-p--12` | `0000420120060004` |
| TRAP | stop | √sthā | stand / stop | 7 | ava | status | `--k---` | `0000000c00506001` |
| ENTER | resource | √viś | enter | 54 | pra |  | `-f*---` | `0000000940001019` |
| LEAVE | resource | √kram | step | 52 | nis |  | `-f----` | `000000014000701a` |
| LINK | resource | √bandh | bind | 93 |  |  | `-t--f-` | `00001400a000001b` |
| LOADF | state | √grah | take | 82 |  | frame | `-t--f*` | `00009400a0080003` |
| STOREF | state | √dhṛ | hold | 9 |  | frame | `-f--**` | `0000880140080004` |
| ADDRF | state | √diś | point out | 58 |  | frame | `-t--f*` | `00009400a008001c` |
| BOUND | state | √mā | measure | 5 | pari | index | `-t*---` | `00000008a055500a` |
| CONS | data | √yu | join | 1 |  |  | `-h*---` | `00000009c000001d` |
| CAR | data | √grah | take | 82 |  | pair-first | `-t--h-` | `00001c00a00a0003` |
| CDR | data | √grah | take | 82 |  | pair-second | `-t--h-` | `00001c00a00c0003` |
| SETCAR | data | √dhṛ | hold | 9 |  | pair-first | `-h-*--` | `00000081c00a0004` |
| SETCDR | data | √dhṛ | hold | 9 |  | pair-second | `-h-*--` | `00000081c00c0004` |
| ISA | data | √jñā | know | 0 |  | kind | `-t--*-` | `00000800a04a001e` |
| OUTV | effect | √likh | write | 23 |  | value | `-*-o--` | `000001a080360017` |
| VNEW | data | √sṛj | emit | 87 |  | vector | `-h*---` | `00000009c00e001f` |
| VLEN | data | √mā | measure | 5 |  | length | `-t--h-` | `00001c00a040000a` |
| VREF | data | √grah | take | 82 |  | vector | `-t--h*` | `00009c00a00e0003` |
| VSET | data | √dhṛ | hold | 9 |  | vector | `-h--**` | `00008801c00e0004` |
| SCAT | data | √yu | join | 1 |  | string | `-h*-*-` | `00000809c010001d` |
| SSUB | data | √chid | cut | 87 |  | string | `-h*-h-` | `00001c09c0100020` |
| SCMP | data | √tul | weigh | 36 |  | string | `-t*-*-` | `00000808a010000e` |
| SCONV | data | √ṇam | bend | 55 | pari |  | `-t*-*-` | `00000808a0015021` |
| CALLI | compose | √hve | call | 4 | ā | indirect | `-c*---` | `00000009602ec016` |
| JMPI | compose | √gam | go | 53 |  | indirect-tail | `-c*---` | `0000000960300015` |
| CATCH | resource | √śri | resort | 2 | ā |  | `-x*---` | `00000009e000c022` |
| UNCATCH | resource | √śri | resort | 2 | apa |  | `-x----` | `00000001e0003022` |
| THROW | resource | √kṣip | throw | 16 |  |  | `-x*---` | `00000009e0000023` |
| RANGE | arith | √mā | measure | 5 |  | range | `-t--1-` | `00000200a04c000a` |
| INEOF | effect | √śru | hear | 2 |  | end-of-input | `-t--i-` | `00001800a03e0018` |
| SHL | arith | √cal | move | 36 | pra |  | `-t1-2-` | `00000402a0001024` |
| SHR | arith | √cal | move | 36 | apa |  | `-t1-2-` | `00000402a0003024` |
| WRAP | arith | √veṣṭ | wrap | 14 |  | width | `-t--1-` | `00000200a04e0025` |
| OUTD | effect | √likh | write | 23 |  | fixed-point | `-1-o--` | `000001a020380017` |
