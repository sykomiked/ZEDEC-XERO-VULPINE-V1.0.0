/-
Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
SPDX-License-Identifier: Apache-2.0

# Headroom convergence: the INTEGER recurrences the kernel runs

Machine-checked with Lean 4 (core only, no Mathlib). Pinned toolchain:
see `proofs/lean-toolchain`. Run: `lean HeadroomConvergence.lean`.

These are models of C functions, transcribed by hand; they are not a proof
about the compiled C code. Each definition names the C function it models.
What is proved is about the integer arithmetic the code performs, NOT about
the real-valued ISF formula `Q_{t+1} = (1 - delta) Q_t + eta S_t - C_t` of
`kernel/src/surplus/surplus.h`: no Lyapunov or real-analysis claim is made.

1. `swarm_budget_consume` (kernel/src/swarm/swarm_budget.c). The headroom
   of a model in an open cycle is `left = allotted - used` (u64). A caller
   that keeps calling consume with requests of at least `eps >= 1` tokens
   while `left > 0` (prov_swarm_request, vna_gate_export do one call each;
   any loop over them is such a caller) makes at most `ceil(left0 / eps)`
   calls: `drainCalls_le`. Every call keeps `used <= allotted`, so the u64
   subtraction `allotted - used` never wraps: `consume_used_le`.
2. `zt_isf_step` (kernel/src/tensor/zt_isf.c), the Q16 integer ISF ledger
   `Q' = max(0, Q - ((Q*delta) >> 16) + ((eta*S) >> 16) - C)`.
   * `isf_nonneg`: Q' >= 0 always.
   * `isf_no_growth`: when `zt_isf_can_grow` is false, Q' <= Q.
   * `isf_decrease`: when the cost beats the input by a margin `eps >= 1`
     (`((eta*S)>>16) + eps <= ((Q*delta)>>16) + C`), Q' = 0 or Q' + eps <= Q.
   * `isf_reaches_zero`: a run whose every step has that margin is at 0
     after `ceil(Q0 / eps)` steps (well-founded descent on the naturals).
   * `isf_mul_fits`: `Q * delta` fits int64 when 0 <= Q < 2^47 and
     0 <= delta <= 2^16. zt_isf_step and zt_isf_init do NOT enforce these
     bounds (finding, docs/FORMAL_INVARIANTS.md).
3. `ct_budget_headroom` / `ct_budget_step` (kernel/src/cotier/ct_budget.c)
   compute the decay as `(Q >> 16) * delta + (((Q & 0xFFFF) * delta) >> 16)`.
   `ct_decay_exact`: this equals `floor(Q * delta / 2^16)` exactly, and
   `ct_decay_fits`: it is < 2^63 for every 0 <= Q < 2^63, 0 <= delta <= 2^16,
   so the split form never overflows int64, unlike the plain product.

`>> 16` on a signed value is modelled as floor division by 65536 (`Int./`
with a positive divisor), which is what GCC and Clang do on the targets
the kernel builds for (arithmetic shift).
-/

namespace Headroom

/-! ## Generic descent lemma -/

/-- `ceil (h / eps)` for `eps > 0`, written as the C idiom. -/
def ceilDiv (h eps : Nat) : Nat := (h + eps - 1) / eps

theorem ceilDiv_mul_ge (h eps : Nat) (he : 0 < eps) : h ≤ ceilDiv h eps * eps := by
  unfold ceilDiv
  have h1 := Nat.div_add_mod (h + eps - 1) eps
  have h2 := Nat.mod_lt (h + eps - 1) he
  have h3 : eps * ((h + eps - 1) / eps) = (h + eps - 1) / eps * eps := Nat.mul_comm _ _
  omega

/-- A sequence of naturals in which every positive term is followed by 0 or by
a term at least `eps` smaller, and 0 is followed by 0, is 0 from step
`ceil (q 0 / eps)` on. -/
theorem descent (q : Nat → Nat) (eps : Nat) (he : 0 < eps)
    (step : ∀ n, q (n + 1) = 0 ∨ q (n + 1) + eps ≤ q n) :
    ∀ n, q n = 0 ∨ q n + n * eps ≤ q 0 := by
  intro n
  induction n with
  | zero => right; simp
  | succ k ih =>
    rcases step k with h | h
    · left; exact h
    · rcases ih with h' | h'
      · left; omega
      · right
        have : (k + 1) * eps = k * eps + eps := Nat.succ_mul k eps
        omega

theorem descent_zero (q : Nat → Nat) (eps : Nat) (he : 0 < eps)
    (step : ∀ n, q (n + 1) = 0 ∨ q (n + 1) + eps ≤ q n) :
    ∀ n, ceilDiv (q 0) eps ≤ n → q n = 0 := by
  intro n hn
  have hc := ceilDiv_mul_ge (q 0) eps he
  have hm : ceilDiv (q 0) eps * eps ≤ n * eps := Nat.mul_le_mul_right eps hn
  rcases descent q eps he step n with h | h
  · exact h
  · omega

/-! ## 1. swarm_budget_consume -/

/-- `a / c` is monotone in `a` (core has no named lemma for it in this toolchain). -/
theorem div_mono {a b : Nat} (h : a ≤ b) (c : Nat) : a / c ≤ b / c := by
  rcases Nat.eq_zero_or_pos c with hc | hc
  · subst hc; simp
  · exact (Nat.le_div_iff_mul_le hc).mpr (Nat.le_trans (Nat.div_mul_le_self a c) h)

/-- The grant of `swarm_budget_consume`:
`left = allotted - used; g = (requested < left) ? requested : left`. -/
def grant (allotted used req : Nat) : Nat :=
  if req < allotted - used then req else allotted - used

/-- `used` after the call: `s->used += g`. -/
def usedAfter (allotted used req : Nat) : Nat := used + grant allotted used req

theorem grant_le (a u r : Nat) : grant a u r ≤ a - u ∧ grant a u r ≤ r := by
  unfold grant; split <;> omega

/-- The grant never exceeds the headroom, so `used` never passes `allotted`
and the u64 subtraction `allotted - used` never wraps; `used` stays below
2^64 whenever `allotted` does (allotted only grows inside a cycle). -/
theorem consume_used_le (a u r : Nat) (h : u ≤ a) : usedAfter a u r ≤ a := by
  have := grant_le a u r
  unfold usedAfter; omega

theorem consume_used_lt_u64 (a u r : Nat) (h : u ≤ a) (ha : a < 2 ^ 64) :
    usedAfter a u r < 2 ^ 64 := by
  have := consume_used_le a u r h
  omega

/-- The real decrement: with `req >= eps`, the new headroom is 0 or at
least `eps` below the old one. -/
theorem consume_decrease (a u r eps : Nat) (h : u ≤ a) (hr : eps ≤ r) :
    a - usedAfter a u r = 0 ∨ a - usedAfter a u r + eps ≤ a - u := by
  unfold usedAfter grant; split <;> omega

/-- A caller that calls consume with `reqs k` on its k-th call while the
headroom is positive; returns the number of calls. A zero grant ends the
loop: with a request of 0 a `while (remaining > 0)` caller would spin
forever, which is why prov_swarm_request refuses tokens == 0, and why the
theorem below assumes every request is at least `eps >= 1`. -/
def drainCalls (reqs : Nat → Nat) (allotted used k : Nat) : Nat :=
  if h : used < allotted then
    if hg : grant allotted used (reqs k) = 0 then 0
    else 1 + drainCalls reqs allotted (usedAfter allotted used (reqs k)) (k + 1)
  else 0
termination_by allotted - used
decreasing_by
  have := grant_le allotted used (reqs k)
  simp_wf
  unfold usedAfter
  omega

/-- Termination with the bound: at most `ceil(left0 / eps)` calls. -/
theorem drainCalls_le (reqs : Nat → Nat) (eps : Nat) (he : 0 < eps)
    (hr : ∀ k, eps ≤ reqs k) :
    ∀ L allotted used k, allotted - used = L →
      drainCalls reqs allotted used k ≤ ceilDiv L eps := by
  intro L
  induction L using Nat.strongRecOn with
  | _ L ih =>
    intro allotted used k hL
    rw [drainCalls]
    split
    · rename_i hlt
      split
      · exact Nat.zero_le _
      · rename_i hg
        have hk := hr k
        have hdec := consume_decrease allotted used (reqs k) eps (by omega) hk
        have hgl := grant_le allotted used (reqs k)
        rcases hdec with h0 | hstep
        · -- the call took the whole headroom: the next call stops
          have hz : drainCalls reqs allotted (usedAfter allotted used (reqs k)) (k + 1) = 0 := by
            rw [drainCalls]
            simp only [show ¬ (usedAfter allotted used (reqs k) < allotted) by omega,
              dif_neg, not_false_eq_true]
          rw [hz]
          unfold ceilDiv
          have := (Nat.le_div_iff_mul_le he).mpr (by omega : 1 * eps ≤ L + eps - 1)
          omega
        · -- the headroom fell by at least eps
          have hrec := ih (allotted - usedAfter allotted used (reqs k)) (by omega) allotted
            (usedAfter allotted used (reqs k)) (k + 1) rfl
          have key : 1 + ceilDiv (allotted - usedAfter allotted used (reqs k)) eps
              ≤ ceilDiv L eps := by
            unfold ceilDiv
            have hle : allotted - usedAfter allotted used (reqs k) + eps - 1 + eps
                ≤ L + eps - 1 := by omega
            have hdiv := Nat.add_div_right
              (allotted - usedAfter allotted used (reqs k) + eps - 1) he
            have hmono := div_mono hle eps
            omega
          omega
    · exact Nat.zero_le _

/-! ## 2. zt_isf_step (Q16 integer ISF ledger) -/

/-- `x >> 16` on int64, arithmetic shift = floor division by 2^16. -/
def shr16 (x : Int) : Int := x / 65536

/-- `zt_isf_step`. -/
def isfStep (Q delta eta S C : Int) : Int :=
  let q := Q - shr16 (Q * delta) + shr16 (eta * S) - C
  if q < 0 then 0 else q

/-- `zt_isf_can_grow`: `((eta*S)>>16) > ((Q*delta)>>16) + C`. -/
def isfCanGrow (Q delta eta S C : Int) : Prop :=
  shr16 (eta * S) > shr16 (Q * delta) + C

theorem isf_nonneg (Q delta eta S C : Int) : 0 ≤ isfStep Q delta eta S C := by
  unfold isfStep; simp only; split <;> omega

/-- Decay is between 0 and Q when 0 <= delta <= 1.0 (Q16). -/
theorem shr16_decay_bounds (Q delta : Int) (hQ : 0 ≤ Q) (hd0 : 0 ≤ delta)
    (hd1 : delta ≤ 65536) : 0 ≤ shr16 (Q * delta) ∧ shr16 (Q * delta) ≤ Q := by
  unfold shr16
  have h0 : 0 ≤ Q * delta := Int.mul_nonneg hQ hd0
  have h1 : Q * delta ≤ Q * 65536 := Int.mul_le_mul_of_nonneg_left hd1 hQ
  omega

theorem isf_no_growth (Q delta eta S C : Int) (hQ : 0 ≤ Q)
    (h : ¬ isfCanGrow Q delta eta S C) : isfStep Q delta eta S C ≤ Q := by
  unfold isfCanGrow at h
  unfold isfStep; simp only; split <;> omega

theorem isf_decrease (Q delta eta S C eps : Int)
    (h : shr16 (eta * S) + eps ≤ shr16 (Q * delta) + C) :
    isfStep Q delta eta S C = 0 ∨ isfStep Q delta eta S C + eps ≤ Q := by
  unfold isfStep; simp only; split <;> omega

/-- A run of zt_isf_step from Q0 >= 0 in which every step has the cost
margin `eps >= 1` is at 0 after `ceil(Q0 / eps)` steps. -/
theorem isf_reaches_zero (Qs : Nat → Int) (delta eta C S : Nat → Int) (eps : Nat)
    (he : 0 < eps) (h0 : 0 ≤ Qs 0)
    (hstep : ∀ n, Qs (n + 1) = isfStep (Qs n) (delta n) (eta n) (S n) (C n))
    (hmargin : ∀ n, shr16 (eta n * S n) + eps ≤ shr16 (Qs n * delta n) + C n) :
    ∀ n, ceilDiv (Qs 0).toNat eps ≤ n → Qs n = 0 := by
  have hnn : ∀ n, 0 ≤ Qs n := by
    intro n; cases n with
    | zero => exact h0
    | succ k => rw [hstep k]; exact isf_nonneg _ _ _ _ _
  let q : Nat → Nat := fun n => (Qs n).toNat
  have hq : ∀ n, q (n + 1) = 0 ∨ q (n + 1) + eps ≤ q n := by
    intro n
    have hd := isf_decrease (Qs n) (delta n) (eta n) (S n) (C n) eps (hmargin n)
    rw [← hstep n] at hd
    have a := hnn n
    have b := hnn (n + 1)
    simp only [q]
    omega
  intro n hn
  have := descent_zero q eps he hq n hn
  have a := hnn n
  simp only [q] at this
  omega

/-- int64 range. -/
def I64 (x : Int) : Prop := -(2 ^ 63) ≤ x ∧ x < 2 ^ 63

/-- `Q * delta` in zt_isf_step fits int64 when Q < 2^47 and 0 <= delta <= 2^16;
`eta * S` always fits (two int32 values). -/
theorem isf_mul_fits (Q delta eta S : Int) (hQ0 : 0 ≤ Q) (hQ : Q < 2 ^ 47)
    (hd0 : 0 ≤ delta) (hd1 : delta ≤ 65536)
    (he : -(2 ^ 31) ≤ eta ∧ eta < 2 ^ 31) (hs : -(2 ^ 31) ≤ S ∧ S < 2 ^ 31) :
    I64 (Q * delta) ∧ I64 (eta * S) := by
  constructor
  · have h1 : Q * delta ≤ Q * 65536 := Int.mul_le_mul_of_nonneg_left hd1 hQ0
    have h0 : 0 ≤ Q * delta := Int.mul_nonneg hQ0 hd0
    unfold I64; omega
  · -- |eta * S| <= 2^31 * 2^31 = 2^62
    have ha : eta.natAbs ≤ 2 ^ 31 := by omega
    have hb : S.natAbs ≤ 2 ^ 31 := by omega
    have hm : (eta * S).natAbs ≤ 2 ^ 31 * 2 ^ 31 := by
      rw [Int.natAbs_mul]; exact Nat.mul_le_mul ha hb
    unfold I64; omega

/-! ## 3. ct_budget decay: the split product is exact and never overflows -/

/-- `(Q >> 16) * delta + (((Q & 0xFFFF) * delta) >> 16)` for Q >= 0. -/
def ctDecay (Q delta : Nat) : Nat := (Q / 65536) * delta + (Q % 65536 * delta) / 65536

theorem ct_decay_exact (Q delta : Nat) : ctDecay Q delta = Q * delta / 65536 := by
  unfold ctDecay
  have hQ : Q = Q % 65536 + 65536 * (Q / 65536) := (Nat.mod_add_div Q 65536).symm
  have : Q * delta = Q % 65536 * delta + 65536 * (Q / 65536 * delta) := by
    conv => lhs; rw [hQ]
    rw [Nat.add_mul, Nat.mul_assoc]
  rw [this, Nat.add_mul_div_left _ _ (by decide : 0 < 65536)]
  omega

theorem ct_decay_fits (Q delta : Nat) (hQ : Q < 2 ^ 63) (hd : delta ≤ 65536) :
    ctDecay Q delta < 2 ^ 63 ∧ ctDecay Q delta ≤ Q := by
  have hhi : Q / 65536 < 2 ^ 47 := by omega
  have hm1 : Q / 65536 * delta ≤ Q / 65536 * 65536 := Nat.mul_le_mul_left _ hd
  have hm2 : Q % 65536 * delta ≤ Q % 65536 * 65536 := Nat.mul_le_mul_left _ hd
  have hlo : Q % 65536 * delta / 65536 ≤ Q % 65536 := by
    have := div_mono hm2 65536
    rwa [Nat.mul_div_cancel _ (by decide : 0 < 65536)] at this
  have hsplit := Nat.mod_add_div Q 65536
  unfold ctDecay
  constructor <;> omega

end Headroom
