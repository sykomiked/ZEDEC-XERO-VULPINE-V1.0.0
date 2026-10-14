/ Count, for each payment, the sources that confirm it and the sources
/ that contradict it. The Playground turns each pair of counts into
/ four-valued truth: confirmed only = TRUE, contradicted only = FALSE,
/ both = BOTH (held for a person), no report = NEITHER.
/ 1 settled, -1 not settled, 0 no report: (x+abs x)%2 is 1 only for a 1.
yes:((sender+abs sender)+(receiver+abs receiver)+rtgs+abs rtgs)%2
no:(((abs sender)-sender)+((abs receiver)-receiver)+(abs rtgs)-rtgs)%2
(2,#yes)#yes,no
