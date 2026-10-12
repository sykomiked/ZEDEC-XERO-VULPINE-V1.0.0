ZMYRAGLD ;mumps-myra golden files and cross-checks from the M engine ;0.1
 ;Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 ;SPDX-License-Identifier: Apache-2.0
 ;
 ;The M engine is the reference here: these entry points write what GT.M
 ;(or YottaDB) itself decides, and make m compares it with libzmyra.
 ;  sort^ZMYRAGLD    stdin lines -> one local array -> $ORDER (collate.m-expect)
 ;  canon^ZMYRAGLD   stdin lines -> "1 x" if x=+x else "0 x"  (canonic.m-expect)
 ;  dates^ZMYRAGLD   $H days -> $ZDATE(h,"YEAR-MM-DD")         (dates.m-expect)
 ;  cross^ZMYRAGLD   generated strings and every $H day, compared with the
 ;                   libzmyra external calls; prints the differences
 quit
 ;
sort ;M collation of the lines of stdin (duplicates collapse, as in M)
 new x,line
 for  read line quit:$zeof  if line'="" set x(line)=""
 set line="" for  set line=$order(x(line)) quit:line=""  write line,!
 quit
 ;
canon ;M's canonical-number test on each line of stdin
 new line
 for  read line quit:$zeof  write $$isnum(line)," ",line,!
 quit
 ;
isnum(x) ;1 if M treats x as a canonical number (an overflow is not one)
 new $etrap
 set $etrap="set $ecode="""" quit 0"
 quit x=+x
 ;
dates ;$ZDATE of chosen $H days: the edges, leap days and a stride
 new h
 for h=0,1,2,59,60,61,365,366,21608,21609,21914,21915,21975,21976,47117,58073,58074,58132,58133,67854,2980012,2980013 write h," ",$zdate(h,"YEAR-MM-DD"),!
 for h=0:9973:2980013 write h," ",$zdate(h,"YEAR-MM-DD"),!
 quit
 ;
cross ;compare M with libzmyra on generated data
 new seed,i,x,y,n,k,bad,c,d,h,s
 set seed=20261011,bad=0,n=0,k=0
 ;1. canonical numbers: 200000 number-like strings
 for i=1:1:200000 set x=$$numlike() set n=n+1,k=k+$$isnum(x) if $$isnum(x)'=$&zmyra.canonic(x) set bad=bad+1 if bad<20 write "canonic differs: ",$zwrite(x),!
 write "canonic: ",n," strings, ",k," of them canonical numbers",!
 ;2. collation: 100000 mixed subscripts in one array, walked in M order
 kill s
 for i=1:1:100000 set x=$select($$rand(3)=0:$$text(),1:$$numlike()) if x'="" set s(x)=""
 set x="",y="",c=0
 for  set x=$order(s(x)) quit:x=""  do  set y=x,c=c+1
 . if y'="" set d=$&zmyra.collate(y,x) if d'=-1 set bad=bad+1 if bad<20 write "collation differs: ",$zwrite(y)," before ",$zwrite(x)," gives ",d,!
 write "collate: ",c," distinct subscripts in M order",!
 ;3. every $H day from 0 to 9999-12-31
 new out
 for h=0:1:2980013 set out="" if $&zmyra.hdate(h,.out)!(out'=$zdate(h,"YEAR-MM-DD")) set bad=bad+1 if bad<20 write "date differs: ",h," ",out," ",$zdate(h,"YEAR-MM-DD"),!
 write "dates: 2980014 days",!
 write "cross-check: ",bad," differences",!
 quit
 ;
rand(m) ;0..m-1 from a fixed-seed MINSTD generator (exact in M's 18 digits)
 set seed=seed*48271#2147483647
 quit seed\128#m
 ;
numlike() ;a string that often is, and often is almost, a canonical number
 new x,k,j
 set x=$select($$rand(3)=0:"-",1:"")
 set k=$$rand(25) if k>20 set k=$$rand(50)
 for j=1:1:k set x=x_$select(j=1&($$rand(4)>0):$$rand(9)+1,1:$$rand(10))
 if $$rand(2) set x=x_"." set k=$$rand(20) if k>17 set k=$$rand(48) for j=1:1:k set x=x_$$rand(10)
 if $$rand(40)=0 set x=x_"E"_$$rand(5)
 if $$rand(60)=0 set x=" "_x
 if $$rand(30)=0 set x=x_"0"
 quit x
 ;
text() ;a short printable string
 new x,k,j
 set x="",k=$$rand(6)+1
 for j=1:1:k set x=x_$extract("09.-AZaz ~B",$$rand(11)+1)
 quit x
