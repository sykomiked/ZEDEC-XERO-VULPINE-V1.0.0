/-
Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
SPDX-License-Identifier: Apache-2.0

# Rational bounds: the two rational types of kernel/src/rmag

Machine-checked with Lean 4 (core only, no Mathlib). Pinned toolchain:
see `proofs/lean-toolchain`. Run: `lean RationalBounds.lean`.

These are hand transcriptions of C functions into Lean, with fixed-width
integers modelled explicitly (`I64`, `U64`); they are not a proof about the
compiled C code. Each definition names the C function it models.

## Part A: `rational_t {int64 num, den}` (include/m5_types.h, rmag_core.c)

* `euclid_eq_gcd`: the `m5_gcd` loop computes `Nat.gcd` of the magnitudes.
* `normalize_den_ne_zero`: `rational_normalize` never returns `den == 0`.
* `normalize_den_pos`: for `den != 0` it returns `den > 0`, and
  `normalize_value`: the same rational value (cross products equal).
* `normalize_x_over_zero`: for `den == 0` it returns `sign(num)/1`. That is
  the root of the division bug: `divOld_by_zero` shows the old
  `rmag_div_quotas(5/1, 0/1)` returned `1/1`. The fixed C function `divQ`
  returns `0/1` (`div_by_zero_detected`) and `rmag_div_quotas_checked`
  (`divChecked`) reports it; `divChecked_sound`: when it succeeds the
  quotient is exact, has `den > 0`, and every intermediate fits int64.
* `addQ_comm`, `mulQ_comm`: add and multiply are commutative, and their
  results are normalized with `den > 0` (`addQ_den_pos`, `mulQ_den_pos`).
* FINDING (not fixed, reported): `rmag_add_quotas`, `rmag_sub_quotas`,
  `rmag_mul_quotas` and unchecked `rmag_div_quotas` do NOT check int64
  overflow of their cross products; `addQ_overflow_witness` exhibits inputs
  (2^62/1 + 2^62/1) whose C evaluation overflows int64 (undefined behaviour).
  The commutativity above is about the mathematical integers the model
  uses; it says nothing about overflowing inputs.

## Part B: `rmag_rational_t {u64 numerator, denominator; bool negative}` (rmag.c)

* `magCmp_spec`: the multiplication-free continued-fraction comparison
  `mag_cmp` returns exactly the sign of `an*bd - bn*ad` (no overflow is
  possible: it never multiplies), and `ratCmp_spec`: `rat_cmp` returns the
  sign of the difference of the two signed values. So `rmag_consume`'s
  limit check `rmag_rational_less_than(limit, new)` is exact.
* `mulU_exact`: `rmag_rational_multiply` either saturates to UINT64_MAX/1
  (overflow detected by `__builtin_mul_overflow`) or returns exactly
  `a * b` with both words < 2^64 and `den > 0`.
* `addU_comm`: `rmag_rational_add` is commutative for all inputs, including
  the saturating path. That needed the C fix in rmag.c: before it,
  `x + (-x)` with an overflowing common denominator saturated to
  `+UINT64_MAX` or `-UINT64_MAX` depending on argument order.
* `reduceU_den_ne_zero`: `rmag_rational_reduce` never returns `den == 0`;
  `reduceU_exact`, `reduceU_num_le`: it keeps the value and never grows the
  numerator.

## Part C: pay_ledger widths

* `pay_ledger_no_overflow`: with the bound checks `pay_ledger_post` has
  (balances < 2^62, |line delta| <= 2^59, <= 8 lines), every balance update
  and every L1 line sum fits int64.

Q16.16: the only Q16.16 helper in this subsystem, `trit_to_ell_q16`
(include/m5_types.h), is a constant table with values in 0..65536; there is
no arithmetic to prove, so it is not modelled.
-/

namespace RationalBounds
namespace RationalBounds

/-! ## Fixed-width integers -/

def I64 (x : Int) : Prop := -(2 ^ 63) ≤ x ∧ x < 2 ^ 63
def INT64_MIN : Int := -(2 ^ 63)

instance (x : Int) : Decidable (I64 x) := by unfold I64; exact inferInstance

/-! ## Part A: m5_types.h rational_t -/

/-- The `m5_gcd` loop on magnitudes: `while (b) { t = b; b = a % b; a = t; }`. -/
def euclid (a b : Nat) : Nat :=
  if h : b = 0 then a else euclid b (a % b)
termination_by b
decreasing_by exact Nat.mod_lt _ (Nat.pos_of_ne_zero h)

theorem euclid_eq_gcd : ∀ b a, euclid a b = Nat.gcd a b := by
  intro b
  induction b using Nat.strongRecOn with
  | _ b ih =>
    intro a
    rw [euclid]
    split
    · rename_i h; subst h; simp
    · rename_i h
      rw [ih (a % b) (Nat.mod_lt _ (Nat.pos_of_ne_zero h)) b]
      have e : Nat.gcd a b = Nat.gcd (a % b) b := by rw [Nat.gcd_comm, Nat.gcd_rec]
      rw [e, Nat.gcd_comm]

/-- `m5_gcd(a, b)` (it negates negative arguments first, then runs the loop;
`euclid_eq_gcd` lets the model use `Nat.gcd`). -/
def m5gcd (a b : Int) : Int := (Nat.gcd a.natAbs b.natAbs : Nat)

structure R64 where
  num : Int
  den : Int
deriving DecidableEq, Repr

/-- `rational_normalize`, step 1: `if (r.den < 0) { r.num = -r.num; r.den = -r.den; }` -/
def flipS (r : R64) : R64 := if r.den < 0 then ⟨-r.num, -r.den⟩ else r
/-- step 2: `g = m5_gcd(r.num, r.den); if (g > 1) { r.num /= g; r.den /= g; }`
(C `/` on int64 truncates toward zero: `Int.tdiv`). -/
def divS (r : R64) : R64 :=
  if m5gcd r.num r.den > 1 then ⟨r.num.tdiv (m5gcd r.num r.den), r.den.tdiv (m5gcd r.num r.den)⟩
  else r
/-- step 3: `if (r.den == 0) r.den = 1;` -/
def fixS (r : R64) : R64 := if r.den = 0 then ⟨r.num, 1⟩ else r

/-- `rational_normalize`. -/
def normalize (r : R64) : R64 := fixS (divS (flipS r))

theorem fixS_den_ne_zero (r : R64) : (fixS r).den ≠ 0 := by
  unfold fixS; split
  · simp
  · assumption

theorem fixS_id (r : R64) (h : r.den ≠ 0) : fixS r = r := by
  unfold fixS; simp [h]

theorem normalize_den_ne_zero (r : R64) : (normalize r).den ≠ 0 := fixS_den_ne_zero _

theorem flipS_props (r : R64) (h : r.den ≠ 0) :
    0 < (flipS r).den ∧ (flipS r).num * r.den = r.num * (flipS r).den := by
  unfold flipS
  split
  · rename_i hl
    simp only
    refine ⟨by omega, ?_⟩
    rw [Int.neg_mul, Int.mul_neg, Int.mul_comm]
  · exact ⟨by omega, rfl⟩

theorem divS_props (r : R64) (hd : 0 < r.den) :
    0 < (divS r).den ∧ (divS r).num * r.den = r.num * (divS r).den := by
  unfold divS
  split
  · rename_i hg
    simp only
    unfold m5gcd at hg ⊢
    have hGn : ((Nat.gcd r.num.natAbs r.den.natAbs : Nat) : Int) ∣ r.num :=
      Int.ofNat_dvd_left.mpr (Nat.gcd_dvd_left _ _)
    have hGd : ((Nat.gcd r.num.natAbs r.den.natAbs : Nat) : Int) ∣ r.den :=
      Int.ofNat_dvd_left.mpr (Nat.gcd_dvd_right _ _)
    generalize ((Nat.gcd r.num.natAbs r.den.natAbs : Nat) : Int) = G at *
    have hG0 : G ≠ 0 := by omega
    obtain ⟨n', hn⟩ := hGn
    obtain ⟨d', hd'⟩ := hGd
    have hpos : 0 < G * d' := by rw [← hd']; exact hd
    rw [hn, hd']
    rw [Int.mul_tdiv_cancel_left _ hG0, Int.mul_tdiv_cancel_left _ hG0]
    refine ⟨?_, by ac_rfl⟩
    by_cases hp : 0 < d'
    · exact hp
    · have : G * d' ≤ 0 := Int.mul_nonpos_of_nonneg_of_nonpos (by omega) (by omega)
      omega
  · exact ⟨hd, rfl⟩

theorem normalize_den_pos (r : R64) (h : r.den ≠ 0) : 0 < (normalize r).den := by
  have ⟨h1, _⟩ := flipS_props r h
  have ⟨h2, _⟩ := divS_props _ h1
  unfold normalize
  rw [fixS_id _ (by omega)]
  exact h2

theorem normalize_value (r : R64) (h : r.den ≠ 0) :
    (normalize r).num * r.den = r.num * (normalize r).den := by
  have ⟨h1, v1⟩ := flipS_props r h
  have ⟨h2, v2⟩ := divS_props _ h1
  unfold normalize
  rw [fixS_id _ (by omega)]
  generalize flipS r = r1 at *
  generalize divS r1 = r2 at *
  -- r2.num / r2.den = r1.num / r1.den = r.num / r.den, all denominators nonzero
  have e3 : (r2.num * r.den) * r1.den = (r.num * r2.den) * r1.den := by
    calc (r2.num * r.den) * r1.den = (r2.num * r1.den) * r.den := by ac_rfl
      _ = (r1.num * r2.den) * r.den := by rw [v2]
      _ = (r1.num * r.den) * r2.den := by ac_rfl
      _ = (r.num * r1.den) * r2.den := by rw [v1]
      _ = (r.num * r2.den) * r1.den := by ac_rfl
  exact Int.eq_of_mul_eq_mul_right (by omega) e3

/-- FINDING: x/0 normalizes to sign(x)/1 (three instances, by evaluation). -/
theorem normalize_x_over_zero_5 : normalize ⟨5, 0⟩ = ⟨1, 1⟩ := by decide
theorem normalize_x_over_zero_neg : normalize ⟨-7, 0⟩ = ⟨-1, 1⟩ := by decide
theorem normalize_x_over_zero_1 : normalize ⟨1, 0⟩ = ⟨1, 1⟩ := by decide

/-- `rmag_add_quotas` / `rmag_sub_quotas` / `rmag_mul_quotas`, on
mathematical integers (the C code does not check overflow; see below). -/
def addQ (a b : R64) : R64 := normalize ⟨a.num * b.den + b.num * a.den, a.den * b.den⟩
def subQ (a b : R64) : R64 := normalize ⟨a.num * b.den - b.num * a.den, a.den * b.den⟩
def mulQ (a b : R64) : R64 := normalize ⟨a.num * b.num, a.den * b.den⟩

theorem addQ_comm (a b : R64) : addQ a b = addQ b a := by
  unfold addQ; rw [Int.add_comm, Int.mul_comm a.den]

theorem mulQ_comm (a b : R64) : mulQ a b = mulQ b a := by
  unfold mulQ; rw [Int.mul_comm a.num, Int.mul_comm a.den]

theorem addQ_den_pos (a b : R64) (ha : a.den ≠ 0) (hb : b.den ≠ 0) : 0 < (addQ a b).den :=
  normalize_den_pos _ (Int.mul_ne_zero ha hb)

theorem mulQ_den_pos (a b : R64) (ha : a.den ≠ 0) (hb : b.den ≠ 0) : 0 < (mulQ a b).den :=
  normalize_den_pos _ (Int.mul_ne_zero ha hb)

/-- The sum is the exact rational sum (cross-multiplied with `a.den * b.den`). -/
theorem addQ_value (a b : R64) (ha : a.den ≠ 0) (hb : b.den ≠ 0) :
    (addQ a b).num * (a.den * b.den) = (a.num * b.den + b.num * a.den) * (addQ a b).den :=
  normalize_value ⟨_, _⟩ (Int.mul_ne_zero ha hb)

/-- FINDING: the C cross products are not overflow-checked. With
a = b = 2^62/1 the sum `a.num*b.den + b.num*a.den` is 2^63, outside int64. -/
theorem addQ_overflow_witness :
    ¬ I64 ((2 ^ 62 : Int) * 1 + (2 ^ 62 : Int) * 1) := by decide

/-- The OLD `rmag_div_quotas`: `{a.num*b.den, a.den*b.num}` normalized. -/
def divOld (a b : R64) : R64 := normalize ⟨a.num * b.den, a.den * b.num⟩

/-- FINDING (fixed in C): 5 / 0 returned 1. -/
theorem divOld_by_zero : divOld ⟨5, 1⟩ ⟨0, 1⟩ = ⟨1, 1⟩ := by decide

/-- The FIXED `rmag_div_quotas`. -/
def divQ (a b : R64) : R64 :=
  if b.num = 0 ∨ a.den = 0 ∨ b.den = 0 then ⟨0, 1⟩
  else normalize ⟨a.num * b.den, a.den * b.num⟩

/-- `rmag_div_quotas_checked`: `__builtin_mul_overflow` fails exactly when the
mathematical product is outside int64. -/
def divChecked (a b : R64) : Option R64 :=
  if b.num = 0 ∨ a.den = 0 ∨ b.den = 0 then none
  else if a.num = INT64_MIN ∨ a.den = INT64_MIN ∨ b.num = INT64_MIN ∨ b.den = INT64_MIN then none
  else if ¬ I64 (a.num * b.den) ∨ ¬ I64 (a.den * b.num) then none
  else if a.num * b.den = INT64_MIN ∨ a.den * b.num = INT64_MIN then none
  else some (normalize ⟨a.num * b.den, a.den * b.num⟩)

theorem div_by_zero_detected (a b : R64) (h : b.num = 0) :
    divQ a b = ⟨0, 1⟩ ∧ divChecked a b = none := by
  unfold divQ divChecked; simp [h]

theorem divQ_den_pos (a b : R64) : 0 < (divQ a b).den := by
  unfold divQ
  split
  · decide
  · rename_i h
    exact normalize_den_pos _ (Int.mul_ne_zero (by omega) (by omega))

theorem divQ_value (a b : R64) (h : b.num ≠ 0) (ha : a.den ≠ 0) (hb : b.den ≠ 0) :
    (divQ a b).num * (a.den * b.num) = (a.num * b.den) * (divQ a b).den := by
  unfold divQ
  simp only [h, ha, hb, or_self, if_false]
  exact normalize_value ⟨_, _⟩ (Int.mul_ne_zero ha h)

/-- Every step of normalize keeps the magnitudes from growing. -/
theorem normalize_natAbs_le (r : R64) (h : r.den ≠ 0) :
    (normalize r).num.natAbs ≤ r.num.natAbs ∧ (normalize r).den.natAbs ≤ r.den.natAbs := by
  have ⟨h1, _⟩ := flipS_props r h
  unfold normalize
  rw [fixS_id _ (by have := (divS_props _ h1).1; omega)]
  have f1 : (flipS r).num.natAbs = r.num.natAbs ∧ (flipS r).den.natAbs = r.den.natAbs := by
    unfold flipS; split <;> simp [Int.natAbs_neg]
  have f2 : (divS (flipS r)).num.natAbs ≤ (flipS r).num.natAbs ∧
      (divS (flipS r)).den.natAbs ≤ (flipS r).den.natAbs := by
    unfold divS; split
    · simp only [Int.natAbs_tdiv]; exact ⟨Nat.div_le_self _ _, Nat.div_le_self _ _⟩
    · exact ⟨Nat.le_refl _, Nat.le_refl _⟩
  omega

theorem divChecked_sound (a b q : R64) (h : divChecked a b = some q) :
    0 < q.den ∧ q.num * (a.den * b.num) = (a.num * b.den) * q.den ∧ I64 q.num ∧ I64 q.den := by
  unfold divChecked at h
  by_cases h0 : b.num = 0 ∨ a.den = 0 ∨ b.den = 0
  · simp [h0] at h
  · simp only [h0, if_false] at h
    by_cases h1 : a.num = INT64_MIN ∨ a.den = INT64_MIN ∨ b.num = INT64_MIN ∨ b.den = INT64_MIN
    · simp [h1] at h
    · simp only [h1, if_false] at h
      by_cases h2 : ¬ I64 (a.num * b.den) ∨ ¬ I64 (a.den * b.num)
      · simp [h2] at h
      · simp only [h2, if_false] at h
        by_cases h3 : a.num * b.den = INT64_MIN ∨ a.den * b.num = INT64_MIN
        · simp [h3] at h
        · simp only [h3, if_false, Option.some.injEq] at h
          subst h
          have hd : a.den * b.num ≠ 0 := Int.mul_ne_zero (by omega) (by omega)
          refine ⟨normalize_den_pos _ hd, normalize_value _ hd, ?_⟩
          have ⟨hn, hdd⟩ := normalize_natAbs_le ⟨a.num * b.den, a.den * b.num⟩ hd
          simp only [not_or, Decidable.not_not] at h2 h3
          unfold I64 at h2 ⊢
          unfold INT64_MIN at h3
          simp only at hn hdd
          omega

/-! ## Part B: rmag.c rmag_rational_t (u64 magnitude + sign) -/

/-- Three-way comparison of naturals as -1 / 0 / 1. -/
def cmpNat (x y : Nat) : Int := if x < y then -1 else if x = y then 0 else 1

theorem cmpNat_anti (x y : Nat) : cmpNat x y = - cmpNat y x := by
  unfold cmpNat
  by_cases h1 : x < y
  · simp [h1, show ¬ y < x by omega, show ¬ y = x by omega]
  · by_cases h2 : x = y
    · subst h2; simp
    · simp [h1, h2, show y < x by omega]

theorem cmpNat_shift (A p q : Nat) : cmpNat (A + p) (A + q) = cmpNat p q := by
  unfold cmpNat
  by_cases h1 : p < q
  · simp [h1]
  · by_cases h2 : p = q
    · subst h2; simp
    · simp [h1, h2, show ¬ A + p < A + q by omega, show ¬ A + p = A + q by omega]

/-- `mag_cmp(an, ad, bn, bd)`: compare an/ad with bn/bd by integer parts and
reciprocals of the remainders. `flip` is the C variable of the same name. -/
def magCmp (an ad bn bd : Nat) (flip : Int) : Int :=
  if h : ad = 0 ∨ bd = 0 then 0  -- unreachable from C: denominators are never 0
  else
    if an / ad ≠ bn / bd then (if an / ad < bn / bd then -flip else flip)
    else
      if an % ad = 0 ∨ bn % bd = 0 then
        (if an % ad = bn % bd then 0 else if an % ad = 0 then -flip else flip)
      else magCmp ad (an % ad) bd (bn % bd) (-flip)
termination_by ad
decreasing_by exact Nat.mod_lt _ (by omega)

theorem split_mul (an ad bd : Nat) :
    an * bd = an / ad * (ad * bd) + an % ad * bd := by
  have h := Nat.div_add_mod an ad
  conv => lhs; rw [← h]
  rw [Nat.add_mul]
  congr 1
  ac_rfl

theorem magCmp_spec : ∀ ad an bn bd (flip : Int), 0 < ad → 0 < bd →
    (flip = 1 ∨ flip = -1) → magCmp an ad bn bd flip = flip * cmpNat (an * bd) (bn * ad) := by
  intro ad
  induction ad using Nat.strongRecOn with
  | _ ad ih =>
    intro an bn bd flip had hbd hf
    rw [magCmp]
    have sa := split_mul an ad bd
    have sb := split_mul bn bd ad
    have hP : bd * ad = ad * bd := Nat.mul_comm _ _
    rw [hP] at sb
    have ra := Nat.mod_lt an had
    have rb := Nat.mod_lt bn hbd
    have rab : an % ad * bd < ad * bd := Nat.mul_lt_mul_of_pos_right ra hbd
    have rba : bn % bd * ad < ad * bd := by
      rw [Nat.mul_comm ad bd]; exact Nat.mul_lt_mul_of_pos_right rb had
    simp only [show ¬ (ad = 0 ∨ bd = 0) by omega, dif_neg, not_false_eq_true]
    split
    · rename_i hq
      have hlt_or : an / ad < bn / bd ∨ bn / bd < an / ad := by omega
      rcases hlt_or with hl | hl
      · have m : (an / ad + 1) * (ad * bd) ≤ bn / bd * (ad * bd) := Nat.mul_le_mul_right _ hl
        rw [Nat.succ_mul] at m
        simp only [hl, if_true]
        unfold cmpNat
        rcases hf with rfl | rfl <;> simp <;> omega
      · have m : (bn / bd + 1) * (ad * bd) ≤ an / ad * (ad * bd) := Nat.mul_le_mul_right _ hl
        rw [Nat.succ_mul] at m
        simp only [show ¬ (an / ad < bn / bd) by omega, if_false]
        unfold cmpNat
        rcases hf with rfl | rfl <;> simp <;> omega
    · rename_i hq
      have hq' : an / ad = bn / bd := by omega
      rw [hq'] at sa
      split
      · rename_i hr
        unfold cmpNat
        by_cases e : an % ad = bn % bd
        · have z1 : an % ad = 0 := by omega
          have z2 : bn % bd = 0 := by omega
          simp only [e, if_true]
          rw [z1, Nat.zero_mul] at sa
          rw [z2, Nat.zero_mul] at sb
          rcases hf with rfl | rfl <;> simp <;> omega
        · simp only [e, if_false]
          by_cases z : an % ad = 0
          · have pz : 0 < bn % bd := by omega
            have : 0 < bn % bd * ad := Nat.mul_pos pz had
            simp only [z, if_true]
            rw [z, Nat.zero_mul] at sa
            rcases hf with rfl | rfl <;> simp <;> omega
          · have z2 : bn % bd = 0 := by omega
            have pz : 0 < an % ad := by omega
            have : 0 < an % ad * bd := Nat.mul_pos pz hbd
            simp only [z, if_false]
            rw [z2, Nat.zero_mul] at sb
            rcases hf with rfl | rfl <;> simp <;> omega
      · rename_i hr
        have h1 : 0 < an % ad := by omega
        have h2 : 0 < bn % bd := by omega
        have hrec := ih (an % ad) ra ad bd (bn % bd) (-flip) h1 h2
          (by rcases hf with rfl | rfl <;> simp)
        have c1 : ad * (bn % bd) = bn % bd * ad := Nat.mul_comm _ _
        have c2 : bd * (an % ad) = an % ad * bd := Nat.mul_comm _ _
        rw [hrec, c1, c2, cmpNat_anti (bn % bd * ad), sa, sb, cmpNat_shift, Int.neg_mul_neg]

theorem magCmp_antisymm (an ad bn bd : Nat) (had : 0 < ad) (hbd : 0 < bd) :
    magCmp an ad bn bd 1 = - magCmp bn bd an ad 1 := by
  rw [magCmp_spec ad an bn bd 1 had hbd (Or.inl rfl), magCmp_spec bd bn an ad 1 hbd had (Or.inl rfl),
    cmpNat_anti (an * bd)]
  simp

structure U where
  num : Nat
  den : Nat
  neg : Bool
deriving DecidableEq, Repr

def U64 (x : Nat) : Prop := x < 2 ^ 64
def sval (a : U) : Int := if a.neg ∧ a.num ≠ 0 then -(a.num : Int) else (a.num : Int)
def fixDen (d : Nat) : Nat := if d = 0 then 1 else d
def ratCmp (a b : U) : Int :=
  let ad := fixDen a.den
  let bd := fixDen b.den
  let an := a.neg ∧ a.num ≠ 0
  let bn := b.neg ∧ b.num ≠ 0
  if an ≠ bn then (if an then -1 else 1)
  else
    let c := magCmp a.num ad b.num bd 1
    if an then -c else c

def cmpInt (x y : Int) : Int := if x < y then -1 else if x = y then 0 else 1

theorem fixDen_pos (d : Nat) : 0 < fixDen d := by unfold fixDen; split <;> omega

theorem cmpNat_cast (x y : Nat) : cmpNat x y = cmpInt x y := by
  unfold cmpNat cmpInt
  by_cases h1 : x < y
  · simp [h1, show (x:Int) < y by omega]
  · by_cases h2 : x = y
    · subst h2; simp
    · simp [h1, h2, show ¬ (x:Int) < y by omega, show ¬ (x:Int) = y by omega]

theorem cmpInt_neg (x y : Int) : cmpInt (-x) (-y) = - cmpInt x y := by
  unfold cmpInt
  by_cases h1 : x < y
  · simp [h1, show ¬ -x < -y by omega, show ¬ -x = -y by omega]
  · by_cases h2 : x = y
    · subst h2; simp
    · simp [h1, h2, show -x < -y by omega]

theorem ratCmp_spec (a b : U) :
    ratCmp a b = cmpInt (sval a * fixDen b.den) (sval b * fixDen a.den) := by
  have hp := fixDen_pos a.den
  have hq := fixDen_pos b.den
  unfold ratCmp sval
  simp only
  rw [magCmp_spec _ _ _ _ 1 hp hq (Or.inl rfl), cmpNat_cast, Int.one_mul,
    Int.natCast_mul, Int.natCast_mul]
  generalize fixDen a.den = A at *
  generalize fixDen b.den = B at *
  have pA : (0:Int) < A := by omega
  have pB : (0:Int) < B := by omega
  by_cases ha : a.num = 0
  · by_cases hb : b.num = 0
    · simp [ha, hb, cmpInt]
    · have y : (0:Int) < (b.num:Int) * A := Int.mul_pos (by omega) pA
      cases hbn : b.neg
      · simp [ha, hb, hbn]
      · simp [ha, hb, hbn, cmpInt, Int.neg_mul] <;> omega
  · have x : (0:Int) < (a.num:Int) * B := Int.mul_pos (by omega) pB
    by_cases hb : b.num = 0
    · cases han : a.neg
      · simp [ha, hb, han, cmpInt] <;> omega
      · simp [ha, hb, han, cmpInt, Int.neg_mul] <;> omega
    · have y : (0:Int) < (b.num:Int) * A := Int.mul_pos (by omega) pA
      cases han : a.neg <;> cases hbn : b.neg
      · simp [ha, hb, han, hbn]
      · simp [ha, hb, han, hbn, cmpInt, Int.neg_mul] <;> omega
      · simp [ha, hb, han, hbn, cmpInt, Int.neg_mul] <;> omega
      · simp [ha, hb, han, hbn, Int.neg_mul, cmpInt_neg]

theorem lessThan_exact (a b : U) :
    ratCmp a b < 0 ↔ sval a * fixDen b.den < sval b * fixDen a.den := by
  rw [ratCmp_spec]; unfold cmpInt
  by_cases h1 : sval a * fixDen b.den < sval b * fixDen a.den
  · simp [h1]
  · by_cases h2 : sval a * fixDen b.den = sval b * fixDen a.den
    · simp [h1, h2]
    · simp [h1, h2]
/-- `rmag_rational_reduce`. -/
def reduceU (r : U) : U :=
  if r.den = 0 then { r with den := 1 }
  else if r.num = 0 then ⟨0, 1, false⟩
  else
    let g := Nat.gcd r.num r.den
    if g > 1 then ⟨r.num / g, r.den / g, r.neg⟩ else r

theorem reduceU_den_ne_zero (r : U) : (reduceU r).den ≠ 0 := by
  unfold reduceU
  by_cases h0 : r.den = 0
  · simp [h0]
  · rw [if_neg h0]
    by_cases h1 : r.num = 0
    · simp [h1]
    · rw [if_neg h1]
      simp only
      by_cases hg : Nat.gcd r.num r.den > 1
      · rw [if_pos hg]
        have hd : Nat.gcd r.num r.den ∣ r.den := Nat.gcd_dvd_right _ _
        have hle : Nat.gcd r.num r.den ≤ r.den := Nat.le_of_dvd (by omega) hd
        have := (Nat.le_div_iff_mul_le (by omega : 0 < Nat.gcd r.num r.den)).mpr
          (by omega : 1 * Nat.gcd r.num r.den ≤ r.den)
        simp only
        omega
      · rw [if_neg hg]; exact h0

/-- Reduction never grows the numerator, so a numerator < 2^64 stays < 2^64. -/
theorem reduceU_num_le (r : U) (h : r.den ≠ 0) : (reduceU r).num ≤ r.num := by
  unfold reduceU
  rw [if_neg h]
  by_cases h1 : r.num = 0
  · simp [h1]
  · rw [if_neg h1]
    simp only
    by_cases hg : Nat.gcd r.num r.den > 1
    · rw [if_pos hg]; exact Nat.div_le_self _ _
    · rw [if_neg hg]; exact Nat.le_refl _

theorem reduceU_exact (r : U) (h : r.den ≠ 0) :
    (reduceU r).num * r.den = r.num * (reduceU r).den := by
  unfold reduceU
  rw [if_neg h]
  by_cases hz : r.num = 0
  · simp [hz]
  · rw [if_neg hz]
    simp only
    by_cases hg : Nat.gcd r.num r.den > 1
    · rw [if_pos hg]
      have hn := Nat.gcd_dvd_left r.num r.den
      have hd := Nat.gcd_dvd_right r.num r.den
      generalize Nat.gcd r.num r.den = g at *
      obtain ⟨n', hn'⟩ := hn
      obtain ⟨d', hd'⟩ := hd
      have g0 : 0 < g := by omega
      simp only
      rw [hn', hd', Nat.mul_div_cancel_left _ g0, Nat.mul_div_cancel_left _ g0]
      ac_rfl
    · rw [if_neg hg]

/-- `__builtin_mul_overflow` on u64: the product, and whether it overflowed. -/
def mulOv (x y : Nat) : Nat × Bool := ((x * y) % 2 ^ 64, decide (2 ^ 64 ≤ x * y))

def saturated (neg : Bool) : U := ⟨2 ^ 64 - 1, 1, neg⟩

/-- `rmag_rational_multiply`. -/
def mulU (a b : U) : U :=
  let ad := fixDen a.den
  let bd := fixDen b.den
  let neg := a.neg != b.neg
  if a.num = 0 ∨ b.num = 0 then ⟨0, 1, false⟩
  else
    let g1 := Nat.gcd a.num bd
    let g2 := Nat.gcd b.num ad
    let n := mulOv (a.num / g1) (b.num / g2)
    let d := mulOv (ad / g2) (bd / g1)
    if n.2 || d.2 then saturated neg
    else reduceU ⟨n.1, d.1, neg⟩

/-- Multiplication is exact or saturates: when it does not saturate, the
result is a*b exactly (cross-multiplied), with both words < 2^64 and den > 0. -/
theorem mulU_exact (a b : U) (ha : a.num ≠ 0) (hb : b.num ≠ 0)
    (hns : mulU a b ≠ saturated (a.neg != b.neg)) :
    (mulU a b).num * (fixDen a.den * fixDen b.den) = (a.num * b.num) * (mulU a b).den ∧
    U64 (mulU a b).num ∧ (mulU a b).den ≠ 0 := by
  have hp := fixDen_pos a.den
  have hq := fixDen_pos b.den
  unfold mulU at hns ⊢
  generalize fixDen a.den = A at *
  generalize fixDen b.den = B at *
  simp only [ha, hb, or_self, if_false] at hns ⊢
  split at hns
  · exact absurd rfl hns
  · rename_i hov
    simp only [Bool.or_eq_true, not_or, mulOv, decide_eq_true_eq] at hov
    simp only [show ¬ ((decide (2 ^ 64 ≤ a.num / Nat.gcd a.num B * (b.num / Nat.gcd b.num A)) ||
        decide (2 ^ 64 ≤ A / Nat.gcd b.num A * (B / Nat.gcd a.num B))) = true) by
        simp only [Bool.or_eq_true, decide_eq_true_eq, not_or]; exact hov, if_false, mulOv]
    have g1a := Nat.gcd_dvd_left a.num B
    have g1b := Nat.gcd_dvd_right a.num B
    have g2a := Nat.gcd_dvd_left b.num A
    have g2b := Nat.gcd_dvd_right b.num A
    have g1p : 0 < Nat.gcd a.num B := Nat.gcd_pos_of_pos_right _ hq
    have g2p : 0 < Nat.gcd b.num A := Nat.gcd_pos_of_pos_right _ hp
    generalize Nat.gcd a.num B = g1 at *
    generalize Nat.gcd b.num A = g2 at *
    obtain ⟨an', e1⟩ := g1a
    obtain ⟨bd', e2⟩ := g1b
    obtain ⟨bn', e3⟩ := g2a
    obtain ⟨ad', e4⟩ := g2b
    rw [e1, e3, e4, e2] at hov ⊢
    rw [Nat.mul_div_cancel_left _ g1p, Nat.mul_div_cancel_left _ g2p,
      Nat.mul_div_cancel_left _ g2p, Nat.mul_div_cancel_left _ g1p] at hov ⊢
    have n_lt : an' * bn' < 2 ^ 64 := by omega
    have d_lt : ad' * bd' < 2 ^ 64 := by omega
    rw [Nat.mod_eq_of_lt n_lt, Nat.mod_eq_of_lt d_lt]
    have dpos : ad' * bd' ≠ 0 := by
      intro hz
      rcases Nat.mul_eq_zero.mp hz with h | h
      · rw [h, Nat.mul_zero] at e4; omega
      · rw [h, Nat.mul_zero] at e2; omega
    have ex := reduceU_exact ⟨an' * bn', ad' * bd', a.neg != b.neg⟩ dpos
    have uv : U64 (reduceU ⟨an' * bn', ad' * bd', a.neg != b.neg⟩).num :=
      Nat.lt_of_le_of_lt (reduceU_num_le _ dpos) n_lt
    refine ⟨?_, uv, reduceU_den_ne_zero _⟩
    simp only at ex
    generalize reduceU ⟨an' * bn', ad' * bd', a.neg != b.neg⟩ = r at *
    -- r.num/r.den = an'bn'/(ad'bd') and (g1 an')(g2 bn') / ((g2 ad')(g1 bd')) is the same
    have key : r.num * (g2 * ad' * (g1 * bd')) = g1 * an' * (g2 * bn') * r.den := by
      calc r.num * (g2 * ad' * (g1 * bd')) = (r.num * (ad' * bd')) * (g1 * g2) := by ac_rfl
        _ = (an' * bn' * r.den) * (g1 * g2) := by rw [ex]
        _ = g1 * an' * (g2 * bn') * r.den := by ac_rfl
    exact key

/-- `rmag_rational_add` (after the fix: equal magnitudes of opposite sign cancel
to 0 on the saturating path). -/
def addU (a b : U) : U :=
  let ad := fixDen a.den
  let bd := fixDen b.den
  let g := Nat.gcd ad bd
  let cd := mulOv (ad / g) bd
  let at_ := mulOv a.num (bd / g)
  let bt := mulOv b.num (ad / g)
  let big := cd.2 || at_.2 || bt.2
  if a.neg = b.neg then
    -- __builtin_add_overflow(a_term, b_term): the wrapped sum and its carry
    let big2 := big || decide (2 ^ 64 ≤ at_.1 + bt.1)
    if big2 then saturated a.neg
    else reduceU ⟨(at_.1 + bt.1) % 2 ^ 64, cd.1, if (at_.1 + bt.1) % 2 ^ 64 = 0 then false else a.neg⟩
  else if big then
    let c := magCmp a.num ad b.num bd 1
    if c = 0 then ⟨0, 1, false⟩ else saturated (if c > 0 then a.neg else b.neg)
  else
    let r : U := if at_.1 ≥ bt.1 then ⟨at_.1 - bt.1, cd.1, a.neg⟩ else ⟨bt.1 - at_.1, cd.1, b.neg⟩
    reduceU ⟨r.num, r.den, if r.num = 0 then false else r.neg⟩

theorem gcd_cross (x y : Nat) : x / Nat.gcd x y * y = y / Nat.gcd y x * x := by
  rw [Nat.gcd_comm y x]
  have hx := Nat.gcd_dvd_left x y
  have hy := Nat.gcd_dvd_right x y
  rcases Nat.eq_zero_or_pos (Nat.gcd x y) with h0 | hp
  · rw [h0]; simp
  · generalize Nat.gcd x y = g at *
    obtain ⟨x', rfl⟩ := hx
    obtain ⟨y', rfl⟩ := hy
    rw [Nat.mul_div_cancel_left _ hp, Nat.mul_div_cancel_left _ hp]
    ac_rfl

/-- `rmag_rational_add` is commutative for every input, saturating or not. -/
theorem addU_comm (a b : U) : addU a b = addU b a := by
  have hp := fixDen_pos a.den
  have hq := fixDen_pos b.den
  unfold addU
  generalize fixDen a.den = A at *
  generalize fixDen b.den = B at *
  have hcd : A / Nat.gcd A B * B = B / Nat.gcd B A * A := gcd_cross A B
  simp only [Nat.gcd_comm B A, mulOv, hcd]
  have anti := magCmp_antisymm a.num A b.num B hp hq
  by_cases hs : a.neg = b.neg
  · simp only [hs, if_true, Nat.add_comm (a.num * (B / Nat.gcd A B) % 2 ^ 64),
      Bool.or_comm (decide (2 ^ 64 ≤ a.num * (B / Nat.gcd A B)))]
    simp only [Bool.or_assoc]
    rw [Bool.or_left_comm (decide (2 ^ 64 ≤ a.num * (B / Nat.gcd A B)))]
  · have hs' : ¬ (b.neg = a.neg) := fun h => hs h.symm
    simp only [hs, hs', if_false]
    have hb : (decide (2 ^ 64 ≤ B / Nat.gcd A B * A) || decide (2 ^ 64 ≤ a.num * (B / Nat.gcd A B)) ||
        decide (2 ^ 64 ≤ b.num * (A / Nat.gcd A B))) =
        (decide (2 ^ 64 ≤ B / Nat.gcd A B * A) || decide (2 ^ 64 ≤ b.num * (A / Nat.gcd A B)) ||
        decide (2 ^ 64 ≤ a.num * (B / Nat.gcd A B))) := by
      simp only [Bool.or_assoc, Bool.or_comm (decide (2 ^ 64 ≤ a.num * (B / Nat.gcd A B)))]
    rw [hb]
    split
    · rw [anti]
      generalize magCmp b.num B a.num A 1 = c
      by_cases c0 : c = 0
      · simp [c0]
      · have : -c ≠ 0 := by omega
        simp only [this, c0, if_false]
        congr 1
        cases ha : a.neg <;> cases hbn : b.neg <;> simp_all <;>
          (by_cases hc : 0 < c <;> simp [hc] <;> omega)
    · generalize a.num * (B / Nat.gcd A B) % 2 ^ 64 = x
      generalize b.num * (A / Nat.gcd A B) % 2 ^ 64 = y
      by_cases hxy : x = y
      · subst hxy; simp
      · rcases Nat.lt_or_gt_of_ne hxy with h | h
        · have : y - x ≠ 0 := by omega
          simp [show ¬ x ≥ y by omega, show y ≥ x by omega, this]
        · simp [show x ≥ y by omega, show ¬ y ≥ x by omega]

/-! ## Part C: pay_ledger_post integer widths (kernel/src/pay/pay_ledger.c)

`pay_ledger_post` keeps balances below `PAY_BAL_MAX = 2^62`, refuses any line
delta outside `[-2^59, 2^59]` (`LINE_MAX_DELTA`) and takes at most
`PAY_MAX_LINES = 8` lines. `pay_ledger_no_overflow`: then `debit + d_debit`
(and `credit + d_credit`) and every running sum `sd`, `sc` of the L1 loop
(a sum over a sub-list of at most 8 lines) fit int64, so none of these
additions is undefined behaviour. proofs/ledger_conservation.tla scales the
widths down and relies on this lemma for the real ones. -/

def sumL : List Int → Int
  | [] => 0
  | x :: xs => x + sumL xs

theorem sumL_bound (l : List Int) (h : ∀ x ∈ l, -(2 ^ 59) ≤ x ∧ x ≤ 2 ^ 59) :
    -((l.length : Int) * 2 ^ 59) ≤ sumL l ∧ sumL l ≤ (l.length : Int) * 2 ^ 59 := by
  induction l with
  | nil => simp [sumL]
  | cons x xs ih =>
    have hx := h x (List.mem_cons_self _ _)
    have ⟨i1, i2⟩ := ih (fun y hy => h y (List.mem_cons_of_mem _ hy))
    simp only [sumL, List.length_cons, Int.natCast_add, Int.add_mul]
    omega

theorem pay_ledger_no_overflow
    (bal : Int) (hb : 0 ≤ bal ∧ bal < 2 ^ 62) (d : Int) (hd : -(2 ^ 59) ≤ d ∧ d ≤ 2 ^ 59)
    (l : List Int) (hl : l.length ≤ 8) (hl2 : ∀ x ∈ l, -(2 ^ 59) ≤ x ∧ x ≤ 2 ^ 59) :
    I64 (bal + d) ∧ I64 (sumL l) := by
  have ⟨s1, s2⟩ := sumL_bound l hl2
  have : (l.length : Int) ≤ 8 := by omega
  have m : (l.length : Int) * 2 ^ 59 ≤ 8 * 2 ^ 59 := Int.mul_le_mul_of_nonneg_right this (by decide)
  unfold I64
  omega

end RationalBounds
