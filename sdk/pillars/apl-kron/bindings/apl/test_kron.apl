⍝ Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
⍝ SPDX-License-Identifier: Apache-2.0
⍝ test_kron.apl - the GNU APL native function KRON. Run through
⍝ run_test.sh (make bindings-apl GNUAPL=... GNUAPL_SRC=...), which
⍝ defines LIB, the path of libzkron_apl.so, before these lines.
⎕IO←1
N←0
F←0
∇ok T msg
 N←N+1
 →ok/0
 F←F+1
 ⍞←'FAIL ',msg,⎕UCS 10
∇
∇Z←ERR S
 ⍝ the error S raises (the first line of ⎕EM), or 'none'
 Z←'none'
 Z←'⎕EM[1;]' ⎕EA 'Q←',S,' ⋄ ''none'''
 Z←(⌽∨\⌽Z≠' ')/Z
∇
∇Z←MORE S
 ⍝ KRON's diagnostic for the program S, which )MORE shows after the error
 Z←6 KRON S
∇
X←LIB ⎕FX 'KRON'
(X≡'KRON') T 'KRON is fixed'

⍝ exact values as APL arrays
(1≡KRON '+/1r3 1r6 1r2') T 'a rational sum that is whole'
((2 3⍴0 1 2 3 4 5)≡KRON '2 3⍴⍳6') T 'a matrix; KRON is ⎕IO 0 inside'
((1 0 0)≡KRON '1 2 3<2') T 'truth values as booleans'
(45≡KRON '+/⍳10') T 'iota'
(¯2≡KRON '-/1 2 3 4') T 'pure ASCII is read as APL here: a right fold'
((0⍴0)≡KRON '0⍴5') T 'an empty vector'
(9223372036854775807≡KRON '9223372036854775806+1') T 'int64 edge'
(6≡KRON 'kron',(⎕UCS 10),'(mul 2 3)') T 'canonical text'

⍝ texts
('1r2'≡0 KRON '+/1r3 1r6') T 'value text'
('(vec 1r3 2)'≡0 KRON '1r3 2') T 'vector text'
('(tvec true false false)'≡0 KRON '1 2 3<2') T 'truth text'
(('0 1 2',(⎕UCS 10),'3 4 5')≡1 KRON '2 3⍴⍳6') T 'display'
P←2 KRON 'x←1 ⋄ x+1'
(P≡'kron',(⎕UCS 10),'(let x 1)',(⎕UCS 10),'(add x 1)',⎕UCS 10) T 'canonical program'
D←3 KRON '+/1 2 3'
(64=⍴D) T 'digest length'
(D≡3 KRON 'kron',(⎕UCS 10),'(reduce add (vec 1 2 3))') T 'the APL text and its canonical form'
(~D≡3 KRON '-/1 2 3') T 'different programs differ'
((1 2)≡4 KRON '1r3 2') T 'numerators'
((3 1)≡5 KRON '1r3 2') T 'denominators'
((2 2⍴1 3 2 1)≡4 KRON '2 2⍴1r2 3 2 1') T 'numerators keep the shape'
((1 2)≡4 KRON '1 2<2 1') T 'truth codes'
((0 0)≡5 KRON '1 2<2 1') T 'truth denominators are 0'

⍝ refusals are APL errors; )MORE carries KRON's diagnostic
('DOMAIN ERROR'≡ERR 'KRON ''1÷0''') T 'division by zero'
(∨/'KR_E_DIVZERO'⍷MORE '1÷0') T 'the status in )MORE'
('DOMAIN ERROR'≡ERR 'KRON ''9223372036854775807+1''') T 'overflow'
(∨/'ZXV_E_RANGE'⍷MORE '9223372036854775807+1') T 'ZXV_E_RANGE in )MORE'
(('line 1, column 2: '≡18↑MORE '1÷0')) T 'line and column'
(''≡MORE '1+1') T '6 KRON is empty when the program runs'
('none'≡ERR '6 KRON ''1÷0''') T '6 KRON never raises'
('LENGTH ERROR'≡ERR 'KRON ''1 2+1 2 3''') T 'length'
('RANK ERROR'≡ERR 'KRON ''2 2 2⍴1''') T 'rank: KRON stops at matrices'
('INDEX ERROR'≡ERR 'KRON ''(⍳3)[5]''') T 'index'
('SYNTAX ERROR'≡ERR 'KRON ''1e3''') T 'no exponents'
('DOMAIN ERROR'≡ERR 'KRON ''1r3 2''') T 'a fraction is not an integer'
('DOMAIN ERROR'≡ERR 'KRON ''y+1''') T 'a free name'
('DOMAIN ERROR'≡ERR '9 KRON ''1''') T 'bad selector'
('none'≡ERR 'KRON ''1+1''') T 'ERR sees no error'
('DOMAIN ERROR'≡ERR 'KRON 1 2 3') T 'not characters'
('RANK ERROR'≡ERR 'KRON 2 2⍴''1+1 ''') T 'not a vector'
(2≡KRON '1+1') T 'works after refusals'

⍝ the same text through GNU APL itself (⎕IO 0, integer results): KRON
⍝ keeps APL's meaning, glyph for glyph
∇Z←SAME S
 ⎕IO←0
 Z←(⍎S)≡KRON S
 ⎕IO←1
∇
CROSS←'+/⍳10' '-/1 2 3 4' '×/1+⍳10' '⌈/3 1 4 1 5' '⌊/3 1 4 1 5' '+\1 2 3 4' '-\1 2 3 4'
CROSS←CROSS,'2 3⍴⍳6' '⍉2 3⍴⍳6' '⌽⍳5' '⊖3 2⍴⍳6' '+⌿3 2⍴⍳6' '+/3 2⍴⍳6' '(⍳3)∘.×⍳4'
CROSS←CROSS,'(2 3⍴⍳6)+.×3 2⍴⍳6' '2↑⍳5' '¯2↑⍳5' '2↓⍳5' '¯2↓⍳5' '7↑1 2 3' '2⌽⍳5' '¯1⌽⍳5'
CROSS←CROSS,'⍋3 1 4 1 5' '⍒3 1 4 1 5' '3 1 4⍳4 9' '(⍳5)[3 1]' '(2 3⍴⍳6)[1;2]' '|¯3 0 3'
CROSS←CROSS,'×¯3 0 3' '3|10 ¯10' '¯3|10 ¯10' '2*10' '5⍴1 2' '1 2 3,4 5' '(2 2⍴⍳4)⍪9 8'
CROSS←CROSS,'(2 2⍴⍳4),9 8' '1 2 3≡1 2 3' '≢⍳7' '⍴2 3⍴0' ',2 2⍴⍳4' '3⌈⌊/5 7' '-⍳4'
R←SAME¨CROSS
(∧/R) T 'KRON and GNU APL agree on ',(⍕⍴CROSS),' programs'
⍞←(~R)/CROSS

⍝ many calls
R←{KRON '+/⍳',⍕⍵}¨⍳500
(R≡{+/¯1+⍳⍵}¨⍳500) T 'repeated calls'

⍞←'test_kron.apl: ',(⍕N),' checks, ',(⍕F),' failed',⎕UCS 10
)OFF
