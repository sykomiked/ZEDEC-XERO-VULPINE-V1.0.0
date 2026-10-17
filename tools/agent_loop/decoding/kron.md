# The kron decoding reference — real blocks from the built apps

## addr_class.kron

```
ka:1-(0|1&a)|0|1&0-a
kb:1-(0|1&a-127)|0|1&127-a
kc:1-(0|1&a-10)|0|1&10-a
kd:(1-(0|1&a-172)|0|1&172-a)*1-(0|1&b-31)|0|1&16-b
ke:(1-(0|1&a-192)|0|1&192-a)*1-(0|1&b-168)|0|1&168-b
kf:(1-(0|1&a-169)|0|1&169-a)*1-(0|1&b-254)|0|1&254-b
kg:(1-(0|1&a-100)|0|1&100-a)*1-(0|1&b-127)|0|1&64-b
kh:1-(0|1&a-239)|0|1&224-a
kj:(0|1&a-254)*(0|1&b-254)*(0|1&c-254)*0|1&d-254
ki:(0|1&a-239)-kj
ka+(2*kb)+(3*kc+kd+ke)+(4*kf)+(5*kg)+(6*kh)+(7*ki)+8*kj
```

## name_fault.kron

```
fl:(0|1&1-label)|0|1&label-63
ft:(0|1&label-total)|0|1&total-253
fb:0|1&bad
rb:3*fb
rt:rb+ft*2-rb
rt+fl*1-rt
```

## name_ok.kron

```
(label>0)&(label<64)&(total>0)&(total<254)&(bad=0)&(total-label)>-1
```

## ttl_left.kron

```
0|stored+ttl-now
```

## holders.kron

```
+/0|1&held
```

## use.kron

```
(asked>0)&(granted>0)&revoked<1
```

## use_allowed.kron

```
(0|1&asked)*(0|1&granted)*1-0|1&revoked
```

## earned.kron

```
(cred>-1)&(bond>-1)&uptime>-1
```

## earned_n.kron

```
(0|1&1+cred)*(0|1&1+bond)*0|1&1+uptime
```

## chain.kron

```
(prev=last)&seq>last
```

## count.kron

```
+/(0|1&1+kind-+/want)*0|1&1+(+/want)-kind
```

## share.kron

```
_10000*size%1|+/total
```

## used.kron

```
_10000*(total-left)%total
```

## left.kron

```
0|minutes+extra-used
```
