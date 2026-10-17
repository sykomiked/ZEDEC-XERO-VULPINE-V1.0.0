# The sutra decoding reference — real blocks from the built apps

## reach.sutra

```
record tier-reach
  have: 9(9)
  need: 9(9)
  reach: 9(13)
procedure
  compute reach = have * 10000 / need
  if reach > 10000
    answer 10000
  end-if
  answer reach
```

## chain.sutra

```
record chain-link
  seq: 9(18)
  prev: 9(18)
  last: 9(18)
procedure
  answer prev = last and seq > last
```

## used.sutra

```
record used-share
  total: 9(18)
  left: 9(18)
  used: 9(5)
procedure
  compute used = (total - left) * 10000 / total
  answer used
```

## left.sutra

```
record minutes-left
  minutes: 9(4)
  extra: 9(4)
  used: 9(9)
procedure
  if used >= minutes + extra
    answer 0
  end-if
  answer minutes + extra - used
```

## balance.sutra

```
record coffer-balance
  before: s9(18)
  moved: s9(18)
  balance: s9(18)
procedure
  compute balance = before + moved
  answer balance
```

## transfer.sutra

```
record coffer-transfer
  amount: s9(18)
  after: s9(18)
  room: s9(18)
  gap: s9(18)
procedure
  answer amount >= 1 and amount <= 9007199254740991 and after >= 0 and room >= 0 and gap = 0
```

## cpu.sutra

```
record cpu-share
  cpu: 9(18)
  ms: 9(18)
  share: 9(9)
procedure
  compute share = cpu * 10 / ms
  answer share
```

## let_through.sutra

```
record let-through
  quiet: 9
  allowed: 9
  gap: s9(9)
  emergency: 9
procedure
  if emergency > 0
    answer 0 = 0
  end-if
  answer quiet < 1 or allowed > 0 or gap >= 0 and gap <= 180
```

## quiet_now.sutra

```
record quiet-now
  opens: 9(4)
  closes: 9(4)
  now: 9(4)
procedure
  if opens > 1439 or closes > 1439 or now > 1439
    answer 0 = 1
  end-if
  if opens = closes
    answer 0 = 0
  end-if
  if opens < closes
    answer now >= opens and now < closes
  end-if
  answer now >= opens or now < closes
```

## minutes.sutra

```
record minutes-left
  charge: 9(9)
  draw: 9(9)
  minutes: 9(11)
procedure
  compute minutes = charge * 60 / draw
  answer minutes
```

## progress.sutra

```
record goal-progress
  done: 9(9)
  goal: 9(9)
  progress: 9(13)
procedure
  compute progress = done * 10000 / goal
  answer progress
```

## shown.sutra

```
record shown
  pos: s9(5)
  size: 9(5)
  span: 9(5)
  lo: 9(5)
  hi: s9(6)
procedure
  compute lo = 0
  if pos > 0
    compute lo = pos
  end-if
  compute hi = pos + size
  if hi > span
    compute hi = span
  end-if
  if hi <= lo
    answer 0
  end-if
  answer hi - lo
```

## rights.sutra

```
record seat-rights
  role: 9
  own: 9
  target: 9
procedure
  if target < 1 or target > 2 or own > 1
    answer 0 = 1
  end-if
  answer role = 1 or role = 2 and own = 1
```

## copies_ok.sutra

```
record copies-ok
  copies: s9(9)
  most: s9(9)
procedure
  answer copies >= 1 and copies <= most
```
