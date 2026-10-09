<!--
Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
SPDX-License-Identifier: Apache-2.0
-->
# Engagement without compulsion

How ZXV keeps people engaged, and the rules that stop that from turning into manipulation. The code is in `kernel/src/quest`. Each rule below is enforced in code and pinned by a test in `kernel/src/quest/test_quest.c`.

## 1. The idea in one paragraph

People come back to things that make them better at something, connect them with other people, and leave them in charge of their own time. They also come back to slot machines. The two can look alike on a usage chart, but they feel different, and they end differently. Quest is built only on the first kind. A person's standing grows from real work that someone else has checked. Groups work toward shared goals and share the rewards. Badges certify things that actually happened. Money is paid for value through the existing pay rails, never as a prize. The system also knows when to say "that's enough for today". There are no streaks, no loot boxes, no countdowns and no "we miss you" messages, and the guard rejects any mechanic that tries to add one.

## 2. Three needs, three kinds of mechanic

Self-determination theory (section 6.1) names three psychological needs. Meeting them produces motivation that lasts. Each one maps to a part of quest.

| Need | What it means | What quest does |
|---|---|---|
| **Mastery** (competence) | Feeling that you are getting better at something that matters | Eight skills that grow only from verified contributions, with clear progress to the next level and no decay |
| **Relatedness** | Feeling connected to, and useful to, other people | Co-op and syndicate goals where progress belongs to the group and rewards are shared by contribution |
| **Autonomy** | Feeling that you chose this | A time budget you set yourself, full opt-out with nothing lost, and notifications only when you asked for them |

## 3. The mechanics

### 3.1 Mastery paths (`quest.h` Q1 to Q3)

There are eight skills. Each grows from one or more kinds of event, and each event must come from a module that is allowed to report it.

| Skill | Grows from | Reported by | Unit | Points per unit |
|---|---|---|---|---|
| Compute | sharing compute | farm, devmesh | 2^20 token-cycles | 1 |
| Hosting | sharing storage / bandwidth | farm, devmesh | 2^30 byte-hours / bytes | 1 |
| Mentoring | helping a peer (the peer confirms) | feed, peer | one help | 3 |
| Reviewing | a review that other people endorsed | feed, market | one endorsement | 2 |
| Teaching | a correction the assistant accepted | assistant | one correction | 2 |
| Translating | an accepted translation | i18n, feed | one string | 1 |
| Verifying | a check whose result matched | farm, market, peer | one check | 2 |
| Trading | a delivery the buyer confirmed | market | one delivery | 2 |

Rules:

- **Verified, not claimed.** Every event names a verifier, and that verifier cannot be the contributor. An automated check, such as a `pay_farm` spot check, uses `QST_VERIFIER_SYSTEM`. Unverified and self-verified events are refused.
- **Once per piece of evidence.** Each event carries the id of the upstream record (a farm job id, a post id, a review id). The same evidence cannot be claimed twice for the same kind, by the same person or by anyone else.
- **No grind.** At most 21 points per skill per day count toward a level. Work beyond that still counts for co-op goals and is still paid through pay, so stopping costs you nothing and a marathon earns you no extra level.
- **Levels never decay.** Level thresholds are every other Fibonacci number: 0, 8, 21, 55, 144, 377, 987 and 2584 points. Being away for a day or a year costs nothing, and nothing in the module counts consecutive days. A level can drop for one reason only: evidence behind it was later found to be fraudulent (`qst_reject_evidence`). Rejection removes that record's points and rebuilds the person's set of distinct verifiers from the records that remain.

### 3.2 Cooperative goals and seasons (`quest_coop.h`)

- A goal belongs to a group, such as a co-op raising storage capacity or a syndicate verifying a batch of jobs. It counts verified units of the event kinds it names, from members only, toward a single target. Members are not ranked against each other. Each member's share of the units is recorded so the reward can be split fairly.
- A goal can carry a VFV pool: an existing pay account and an amount promised on completion. A goal without a pool rewards standing only.
- **Seasons renew goals, never standing.** A new season retires goals that are already settled. Levels, badges and lifetime contribution are untouched. An unfinished goal carries over with its progress intact. Nothing expires, so the end of a season is never a deadline. A goal that is complete but not yet paid stays owed.

### 3.3 Badges that mean something (`quest_badge.h`)

- **Earned.** A badge at level *n* needs the skill at level *n* and points from at least min(*n*, 8) distinct verifiers. One friendly verifier can certify nobody past level 1.
- **Signed.** A badge is a fixed 124-byte record. Its SHA3-256 digest, under the domain `ZXV-QUEST-BADGE-v1`, is signed by the issuer. `quest_sign_mldsa.h` binds it to ML-DSA-65 (FIPS 204) with the context string `zxv.quest.v1`.
- **Portable.** The record encodes and decodes the same way on every device. Anyone with the issuer's public key can check it offline. It carries an evidence root, a SHA3 chain over the evidence ids behind it, so a peer who holds those records can recompute it.
- **Revocable if fraudulent.** A revocation is itself a signed record from the same issuer. A revocation list accepts only revocations whose signature checks. `qst_badge_still_backed` tells an issuer when rejected evidence has left a badge without support.
- **Shown through reputation.** A valid, public badge is mirrored into the existing `reputation` module with `badge_award`. Badges held by minors are marked non-public and are not mirrored.

### 3.4 Rewards: VFV for value, through pay only (`quest_pay.h`)

Quest never mints money, never holds it and never draws lots. When a goal with a pool completes, quest pays it out using pay's own pieces:

1. **Split.** The pool is divided in proportion to verified units with `pay_commons_split`, the commons no-monopoly rule. No member takes more than the larger of 8/21 of the pool or an equal share. The same contributions always produce the same split. Whatever nobody may take stays in the pool.
2. **Tithe.** Each share pays the exact φ-percent tithe, `pay_tithe_phi(gross)` = floor((a + isqrt(5a²)) / 200), to the commons account.
3. **Post.** Each member's share is one `pay_ledger_post`: pool −gross, member +net, commons +tithe, all in VFV. Every posting has a deterministic idempotency key, so a settlement that fails halfway can be run again without paying anyone twice.
4. **Pool type.** Pools must hold VFV. A pool in fiat or any other asset is refused.
5. **Minors.** Money is not paid directly to a minor. It goes to the custodian account named by the guardian policy, or, if there is none, it is held in the pool and listed in the plan.
6. **Opted-out members.** They are paid exactly the same as everyone else.

### 3.5 Time well spent (`quest.h` Q4)

- Each person can set a daily minute budget. With no budget set there are no hints at all.
- At 80% of the budget there is one gentle hint. At 100% there is one more. Neither blocks anything or threatens anything. The text says progress is saved:
  - "You are close to the time you set for today."
  - "You have reached the time you set for today. Your progress is saved."
- For a minor whose guardian asked for a hard stop, the 100% signal is `QST_HINT_STOP`. It holds for the rest of the day and the UI ends the session: "That is today's time. Everything is saved for next time."

### 3.6 Minors (`quest.h` Q5)

An age-policy hook, supplied by identity or guardian settings, is asked when a person enrols. If a hook is installed but cannot give an answer, the person is treated as a minor. Minors get:

- a time budget no larger than the guardian's ceiling;
- no money paid to them directly;
- badges that are never shown publicly;
- through the guard (G10), no mechanic aimed at them that ranks people, pays money or charges to take part.

### 3.7 Full opt-out, nothing lost (`quest.h` Q6)

`qst_opt_out` turns the game layer off: no mastery tracking, no goals in the view, no notification requests and no new badges. Earned levels and badges stay exactly as they were. Verified work keeps counting toward co-op goals and keeps being paid, because that is money owed, not a game. `qst_opt_in` brings everything back as it was.

### 3.8 Notifications only on request (`quest.h` Q7)

Quest posts to the `zx_notify` bus in one case only: a member asked to be told when a particular goal completes. The request is used once. The note is LOW priority and is run through the calm-copy check. Quest has no reminder, no re-engagement message and no "come back" path.

### 3.9 A calm progress view (`quest.h` Q9)

`qst_view` fills a plain data model for the UI. For each skill it gives the level, points, the point range of the current level, progress in permille, points counted today against the daily cap, and the number of distinct verifiers. It also lists the person's goals, with progress and the person's own share, and their time budget. **It has no rank, no streak, no countdown and no unread-badge counter.** The fields do not exist, so the UI cannot show them by accident.

### 3.10 Inputs from other modules (`quest.h` Q8)

Other modules call `qst_record` with a `qst_event_t`. Quest never reaches into them.

| Module | Reports | Verifier |
|---|---|---|
| `pay/pay_farm` | compute / storage / bandwidth shared, after spot checks; checks that matched | `QST_VERIFIER_SYSTEM` (the spot check) |
| `devmesh` | compute or storage your devices served to others | `QST_VERIFIER_SYSTEM` or the receiving peer |
| `social` feed | peer help (the helped peer confirms), endorsed reviews, accepted translations | the peer or endorser |
| market | delivered trades (buyer confirms), endorsed reviews, matched checks | the buyer, endorser or checker |
| assistant | corrections it accepted | the assistant's acceptance |
| `i18n` | accepted translation strings | the reviewer who accepted them |

## 4. The guardrails, as code (`quest_guard.h`)

Every mechanic declares itself in a `qst_mechanic_t`. `qst_guard_check` returns a bitmask of the rules it breaks. Quest refuses to create a goal whose declared mechanic breaks any rule, or whose title fails the calm-copy check. A test checks that every mechanic quest runs passes the guard. Other modules (games, market, the swarm UI) can call the same check.

| Rule | Refused | Why |
|---|---|---|
| G1 | **Loot boxes**: paying for a chance at any reward | Paid random rewards are structurally the same as gambling, and their use is associated with problem gambling (6.4) |
| G2 | **Paid randomness**: a random outcome with money on either side | Same reason. Note that free, unpaid chance inside a game, such as where food spawns in `games`, is allowed |
| G3 | **Streak-loss punishment** | Streaks turn a habit into a fear of loss (loss aversion, 6.5). People end up returning to protect a number, not because they want to |
| G4 | **Variable-ratio or variable-interval money** | Unpredictable reward schedules produce the most persistent, extinction-resistant behaviour known, which is why slot machines use them (6.3) |
| G5 | **Countdown or scarcity on a purchase** | "Urgency" and "scarcity" are the most common dark patterns found on shopping sites (6.2) |
| G6 | **Pull notifications** | Notifications meant to bring someone back serve the platform, not the person (6.6) |
| G7 | **Paying for standing** | Standing and badges have to mean what they say. Bought standing is counterfeit standing |
| G8 | **Random or variable badge drops** | A badge is a certificate. A random one certifies nothing |
| G9 | **No stopping point**: endless feed or autoplay | Removing natural stopping cues is a core attention-capture technique (6.6) |
| G10 | **Ranking, money or paid entry aimed at minors** | Children are more vulnerable to these designs, and age-appropriate design codes ask for exactly this restraint (6.7) |
| G11 | **Hidden opt-out, or opt-out that forfeits earnings** | Making it hard to leave ("roach motel") or costly to leave is a classic dark pattern (6.2) |

`qst_copy_is_calm` rejects user-facing text that contains pressure phrases such as "hurry", "last chance", "ends in", "only 3 left", "streak", "you'll lose" or "we miss you". Matching ignores letter case. Every string quest shows or posts goes through it.

## 5. What we deliberately did not build

- **Leaderboards of individuals.** Groups have shared progress. People are never ranked against each other.
- **Daily login rewards or check-in bonuses.** They reward showing up, not contributing, and they create streak pressure under another name.
- **Random bonuses**, including "lucky" multipliers on real earnings.
- **Re-engagement campaigns.** A person who leaves is not chased.
- **Points as currency.** Mastery points cannot be spent, sold or converted to VFV. Money is paid only for value, through pay. Keeping the two apart also protects intrinsic motivation, because a reward that is expected and tied to the task tends to crowd out the reason people did the task in the first place (6.1).

## 6. The research, plainly

### 6.1 Self-determination theory

Edward Deci and Richard Ryan's self-determination theory (Ryan & Deci, 2000, *American Psychologist* 55(1)) holds that lasting, healthy motivation comes from meeting three needs: competence, autonomy and relatedness. Its sub-theory on rewards (cognitive evaluation theory) distinguishes two kinds of feedback. Rewards that *inform*, such as "you have got better at this", support motivation. Rewards that *control*, such as "do this to get that", tend to undermine it. A meta-analysis of 128 experiments (Deci, Koestner & Ryan, 1999, *Psychological Bulletin* 125(6)) found that expected, tangible rewards made contingent on doing a task reliably reduce intrinsic interest in it. Quest therefore treats levels and badges as information about real skill, and keeps money as payment for value, not as a lever to drive behaviour. Studies of game play (Ryan, Rigby & Przybylski, 2006, *Motivation and Emotion* 30) found the same three needs predict enjoyment and wanting to keep playing.

### 6.2 Dark-pattern taxonomies

- Harry Brignull coined the term "dark patterns" in 2010 and keeps a catalogue at deceptive.design. It includes "roach motel" (easy to get in, hard to get out), confirmshaming, fake urgency and fake scarcity.
- Gray, Kou, Battles, Hoggatt and Toombs (CHI 2018, "The Dark (Patterns) Side of UX Design") grouped them into five strategies: nagging, obstruction, sneaking, interface interference and forced action.
- Mathur et al. (CSCW 2019, "Dark Patterns at Scale") crawled about 11,000 shopping sites and found 1,818 instances of dark patterns. Countdown timers, limited-time messages and low-stock messages were among the most common.
- Regulators have adopted the same vocabulary: the US FTC staff report *Bringing Dark Patterns to Light* (2022), the OECD report on dark commercial patterns (2022), and Article 25 of the EU Digital Services Act, which bars online platforms from interface designs that deceive or manipulate users.

G5, G6, G9 and G11 and the calm-copy check come directly from these catalogues.

### 6.3 Reward schedules

Ferster and Skinner's work on schedules of reinforcement (*Schedules of Reinforcement*, 1957) showed that variable-ratio schedules, where the reward comes after an unpredictable number of actions, produce high, steady response rates that are very resistant to extinction. This is the mechanism of the slot machine. G4 keeps it away from money, and G8 keeps it away from badges.

### 6.4 Loot boxes and gambling

Zendle and Cairns (2018, *PLOS ONE* 13(11)), surveying more than 7,000 gamers, found that the amount people spent on loot boxes was linked to the severity of problem gambling. Later studies have replicated the link. In 2018 the Belgian Gaming Commission found that paid loot boxes in several games broke Belgian gambling law. G1 and G2 follow from this.

### 6.5 Loss aversion and streaks

Kahneman and Tversky's prospect theory (1979, *Econometrica* 47(2)) showed that losses weigh more heavily than equal gains. A streak takes advantage of this: once it exists, breaking it feels like a loss, so people return to avoid the loss rather than for the activity itself. Quest has no streaks, and its levels never decay (G3, Q3).

### 6.6 The Center for Humane Technology

The Center for Humane Technology, which grew out of Tristan Harris's "Time Well Spent" campaign, argues that technology should be designed around people's own goals and values rather than for maximum attention. In its public materials it names specific attention-capture techniques to avoid: intermittent variable rewards, endless feeds with no stopping cues, and notifications designed to pull people back. Its central measure is whether people feel the time they spent was well spent. The user-set budget, the gentle stop hints, the requested-only notifications and G9 come from this.

### 6.7 Children

The UK Information Commissioner's *Age Appropriate Design Code* (2020) sets 15 standards for online services likely to be used by children. These include high-privacy defaults, no nudge techniques that push children to weaken their protections or stay online longer, and taking the child's best interests into account. Quest's minor policy, its fail-safe default (age unknown means protected), and G10 follow the same direction. In the US, COPPA governs collecting data from children under 13. Quest collects no personal data beyond contribution records, but whether a given deployment complies with these regimes is a question for counsel.

### 6.8 Does gamification work?

A review of the empirical studies (Hamari, Koivisto & Sarsa, 2014, HICSS) found that gamification tends to have positive effects, but that these depend heavily on context and on the people involved, and that novelty effects fade. Quest therefore relies on mechanics that stay meaningful after the novelty wears off: getting genuinely better, working with real people, and being paid fairly. It does not rely on decorative points.

## 7. Building and testing

```sh
cd kernel
gcc -std=c11 -O2 -Wall -Werror -Wextra -DTEST_HOST -Iinclude -Isrc/quest -Isrc/social -Isrc/pay \
  -Isrc/swarm -Isrc/tensor -Isrc/mlkem -Isrc/pqsec -Isrc/reputation -Isrc/lpres -Isrc/surplus \
  -Isrc/edp_risk -Isrc/event_space -Isrc/modbind -Isrc/trispace \
  src/quest/test_quest.c src/quest/quest.c src/quest/quest_guard.c src/quest/quest_coop.c \
  src/quest/quest_pay.c src/quest/quest_badge.c src/quest/quest_sign_mldsa.c \
  src/social/zx_notify.c src/reputation/reputation.c src/pay/pay_ledger.c src/pay/pay_util.c \
  src/pay/pay_tithe.c src/swarm/swarm_market.c src/swarm/swarm_budget.c src/swarm/swarm_emotion.c \
  src/tensor/zt.c src/mlkem/keccak.c src/pqsec/pq_mldsa65.c src/pqsec/mldsa/*.c \
  -o /tmp/test_quest && /tmp/test_quest
```

The test runs 133 checks: every guard rule, mastery and the daily cap, no decay, fraud rejection, the time budget, minors, opt-out, requested-only notifications, co-op goals and seasons, VFV payouts through `pay_ledger` (ledger invariants and hash chain checked after settlement, idempotent reruns), and badges signed and verified with real ML-DSA-65.

## 8. Honest limits

- **Declarations, not detection.** The guard checks what a mechanic says about itself. It cannot see a mechanic that lies about itself or never registers. Code review and the pinned tests are what keep declarations honest.
- **Verifiers are trusted.** A verifier colluding with a contributor can mint points. The defences are the distinct-verifier rule for badges, single-use evidence, the daily cap, and later rejection of fraudulent records. None of them is a proof.
- **The caller supplies the day.** A caller that lies about the day can stretch the daily cap.
- **Bounded tables.** Quest holds 64 people, 1,024 records, 32 goals and 16 members per goal. When the record table is full, new events are refused until an operator archives.
- **English-only copy check.** The calm-copy check is a short English phrase list. Other locales need their own lists, and no phrase list catches every manipulative sentence.
- **Revoked badges stay in reputation.** `reputation.h` badge levels only ever rise, so a badge that was mirrored and later revoked stays in reputation's score until that module can lower a level. Quest keeps the authoritative signed record and revocation list.
- **Revocations travel slowly.** Revocations reach only the peers who exchange revocation lists.
- **Settlement is not atomic.** A goal is settled with one posting per member, not one atomic posting. Rerunning is safe. Fraud discovered after a payout is flagged for the operator to reverse in pay, and quest does not claw back money by itself.
- **Not yet wired in.** No module calls `qst_record` yet. The event table in 3.10 is the contract each module needs to implement.
- **Legal questions for counsel:** whether VFV shares are income, and how child-protection and consumer-protection law apply to a particular deployment.
