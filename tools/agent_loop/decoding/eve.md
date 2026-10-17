# The eve decoding reference — real blocks from the built apps

## name.eve

```
eve signpost
label_length = 1 .. 63
name_length = 1 .. 253
bad_bytes = 0 .. 0

name_label {
  label: label_length
  total: name_length
  bad: bad_bytes
}
```

## use.eve

```
eve gatehouse
flag_yes = 1 .. 1
flag_no = 0 .. 0

capability_use {
  asked: flag_yes
  granted: flag_yes
  revoked: flag_no
}
```

## earned.eve

```
eve talisman
margin = 0 .. 999999999999999999
uptime_margin = 0 .. 10000

tier_earned {
  cred: margin
  bond: margin
  uptime: uptime_margin
}
```

## limit.eve

```
eve shepherd
minute_of_day = 0 .. 1439
daily_cap = -1 .. 1440

limit_rule {
  minutes: daily_cap
  from: minute_of_day
  to: minute_of_day
}
```

## transfer.eve

```
eve coffer
minor_amount = 1 .. 9007199254740991
minor_left = 0 .. 999999999999999999
minor_gap = 0 .. 0

transfer_rule {
  amount: minor_amount
  after: minor_left
  room: minor_left
  gap: minor_gap
}
```

## limits.eve

```
eve birdsong
sample_rate = 8000 .. 48000
channel_count = 1 .. 2
frame_count = 0 .. 480000

recording {
  rate: sample_rate
  channels: channel_count
  frames: frame_count
}
```

## schedule.eve

```
eve stillwater
quiet_mode = 0 .. 2
day_mask = 0 .. 127
minute_of_day = 0 .. 1439

quiet_schedule {
  mode: quiet_mode
  days: day_mask
  opens: minute_of_day
  closes: minute_of_day
}
```

## heat_ok.eve

```
eve brazier
calm_tenths = 0 .. 450

heat_calm {
  temp: calm_tenths
}
```

## heat_safe.eve

```
eve brazier
safe_tenths = -200 .. 600

heat_short_of_critical {
  temp: safe_tenths
}
```

## entry.eve

```
eve wellspring
log_day = 0 .. 999999
log_kind = 1 .. 4
log_amount = 0 .. 999999999

log_entry {
  day: log_day
  kind: log_kind
  amount: log_amount
}
```

## safe.eve

```
eve wellspring
pulse_range = 40 .. 100
sleep_range = 420 .. 540
water_range = 1500 .. 4000

safe_day {
  bpm: pulse_range
  sleep: sleep_range
  water: water_range
}
```

## window.eve

```
eve hollow
window_width = 160 .. 16384
window_height = 100 .. 16384
on_screen = 32 .. 16384

window {
  width: window_width
  height: window_height
  across: on_screen
  down: on_screen
}
```

## size.eve

```
eve quillon
doc_bytes = 0 .. 1048576

doc_limits {
  bytes: doc_bytes
}
```

## phrase.eve

```
eve threshold
pass_length = 12 .. 256

passphrase {
  length: pass_length
}
```
