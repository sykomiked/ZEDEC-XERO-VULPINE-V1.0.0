# The trebago decoding reference — real blocks from the built apps

## holders.tb

```
trebago holders
  in  held(n)
  out holders
  holders = sum(i, held(i))
end
```

## earned_n.tb

```
trebago earned
  in  cred(n), bond(n), uptime(n)
  out earned(n)
  earned(i) = min(1, max(0, cred(i) + 1)) * min(1, max(0, 999999999999999999 - cred(i) + 1)) &
            * min(1, max(0, bond(i) + 1)) * min(1, max(0, 999999999999999999 - bond(i) + 1)) &
            * min(1, max(0, uptime(i) + 1)) * min(1, max(0, 10000 - uptime(i) + 1))
end
```

## reach.tb

```
trebago reach
  in  have(n), need(n)
  out reach(n)
  reach(i) = sum(k = 1:10000, min(1, max(0, 10000 * have(i) - k * need(i) + 1)))
end
```

## count.tb

```
trebago kind_count
  in  kind(n), want
  out count
  count = sum(i, max(0, min(1, 1 + kind(i) - want)) * max(0, min(1, 1 + want - kind(i))))
end
```

## total.tb

```
trebago total
  in  size(n)
  out total
  total = sum(i, size(i))
end
```

## week.tb

```
trebago week
  in  minutes(n)
  out week
  week = sum(i, minutes(i))
end
```

## balance.tb

```
trebago balance
  in  before(n), moved(n)
  out balance(n)
  balance(i) = before(i) + moved(i)
end
```

## holdings.tb

```
trebago holdings
  in  balance(n)
  out holdings
  holdings = sum(i, balance(i))
end
```

## score.tb

```
trebago score
  in  name(n), folder(n), count(n)
  out score
  score = sum(i, 1000 * name(i) + 100 * folder(i) * (1 - name(i)) + min(99, count(i)))
end
```

## clip_ms.tb

```
trebago clipms
  in  samples(n), rate
  out ms(n)
  ms(i) = samples(i) * 1000 / rate
end
```

## count.tb

```
trebago count
  in  level(n), bar
  out count
  count = sum(i, max(0, min(1, 1 + level(i) - bar)))
end
```

## week.tb

```
trebago week
  in  amount(n)
  out week
  week = sum(i, amount(i))
end
```

## total.tb

```
trebago total
  in  amount(n), fee(n)
  out total
  total = sum(i, amount(i) + fee(i))
end
```
