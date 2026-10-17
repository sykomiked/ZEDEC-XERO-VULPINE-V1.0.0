# The myra decoding reference — real blocks from the built apps

## cache.myra

```
^ZXSIGNPOST(seq:i9) = class:i1! ^ addr:i10! ^ stored:i11! ^ ttl:i10!
```

## entry.myra

```
^ZXWIT(seq:i18) = prev:i18! ^ kind:i1! ^ stamp:i15!
```

## entry.myra

```
^ZXHOARD(folder:i9, seq:i9) = size:i18! ^ mtime:i12!
```

## memo.myra

```
^ZXBIRDSONG(memo:i9) = rate:i5! ^ channels:i1! ^ frames:i9! ^ mtime:i12!
```

## schedule.myra

```
^ZXQUIET(schedule:i3) = mode:i1! ^ days:i3! ^ opens:i4! ^ closes:i4! ^ allowed:i3!
```

## entry.myra

```
^ZXWELL(day:i6, kind:i1) = amount:i9!
```

## dock.myra

```
^ZXDOCK(place:i2) = app:i3! ^ pinned:i1! ^ added:i12!
```

## seat.myra

```
^ZXSEAT(seat:i4) = role:i1! ^ lock:i1! ^ color:i10! ^ wait:i7!
```

## item.myra

```
^ZXGLINT(folder:i9, seq:i9) = kind:i3! ^ size:i18!
```

## history.myra

```
^ZXFLINT(seq:i9) = stamp:i12! ^ status:i3! ^ bytes:i4!
```

## task.myra

```
^ZXTREADLE(task:i3) = kind:i1! ^ start:i4! ^ period:i4! ^ cost:i9! ^ approved:i1!
```

## payment.myra

```
^ZXFEE(batch:i9, seq:i9) = amount:n15.2! ^ fee:n9.2! ^ from:i10! ^ to:i10!
```

## service.myra

```
^ZXWARREN(engine:i2) = enabled:i1! ^ regulated:i1! ^ restarts:i3! ^ changed:i15!
```

## row.myra

```
^ZXGLEAN(row:i3) = label:i2! ^ source:i2! ^ date:i8! ^ pii:i1!
```
