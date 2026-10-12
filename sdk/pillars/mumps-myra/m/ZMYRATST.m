ZMYRATST ;mumps-myra M interop test: a real global, checked and posted ;0.1
 ;Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 ;SPDX-License-Identifier: Apache-2.0
 ;
 ;Synthetic data only: account names, references and memos are made up.
 ;make m runs this against a fresh database and compares its output with
 ;m/ZMYRATST.expected.
 new dir,h,g,st,ref,node,ent,bank,alice,bob,out,a,b,e,map
 set dir=$ztrnlnm("ZMYRA_DIR")
 kill ^ZXPAY
 set h=$$LOAD^ZMYRA(dir_"/examples/payment.myra")
 write "load payment.myra: ",$select(h<0:$$STNAME^ZMYRA(h),1:"handle "_h),!
 set g=$$LOAD^ZMYRA(dir_"/examples/payment.gmap")
 write "load payment.gmap: ",$select(g<0:$$STNAME^ZMYRA(g),1:"handle "_g),!
 set st=$$LOAD^ZMYRA(dir_"/examples/broken.myra")
 write "load broken.myra: ",$$STNAME^ZMYRA(st),!
 ;
 ;the ledger: one issuer and two holders in NGN
 write "ledger: ",$$STNAME^ZMYRA($&zmyra.ledger("NGN,JPY")),!
 set bank=$&zmyra.open("bank","NGN",1),alice=$&zmyra.open("alice","NGN",0)
 set bob=$&zmyra.open("bob","NGN",0)
 write "accounts: bank ",bank,", alice ",alice,", bob ",bob,!
 ;
 ;the global: a batch header, seven payments, their index and a stray node
 set ^ZXPAY(7)="67854^7^^TEST BATCH SEVEN"
 do pay(1,"FUND-ALICE^1000^NGN^"_bank_"^"_alice_"^67854^N")
 do pay(2,"PAY-0001^250.25^NGN^"_alice_"^"_bob_"^67854^N^^synthetic memo")
 do pay(3,"PAY-0002^5000^NGN^"_alice_"^"_bob_"^67854^N")
 do pay(4,"PAY-0001^1^NGN^"_alice_"^"_bob_"^67854^N")
 do pay(5,"PAY-0003^12.50^NGN^"_alice_"^"_bob_"^67854^N")
 do pay(6,"PAY-0004^1.005^NGN^"_alice_"^"_bob_"^67854^N")
 do pay(7,"PAY-0005^10^USD^"_alice_"^"_bob_"^67854^N")
 set ^ZXPAY(7,"X")="not a payment"
 ;
 ;walk the global in M order ($QUERY): check every node with the MYRA
 ;schema and the global map, and post every valid payment
 set map="amount,from,to,ccy,ref"
 set ref="^ZXPAY"
 for  set ref=$query(@ref) quit:ref=""  do
 . set st=$$CHECK^ZMYRA(h,ref),node=$$NODE^ZMYRA(h,ref)
 . write ref," node ",$select(node<0:"-",1:node)," ",$$STNAME^ZMYRA(st)
 . if $$CHECK^ZMYRA(g,ref)'=st write " (the global map disagrees)"
 . if st'=0 write ": ",$$ERR^ZMYRA()
 . write !
 . quit:st'=0  quit:node'=1
 . set st=$$POST^ZMYRA(h,ref,map,.ent)
 . write "  post ",$select(st=0:"entry "_ent,1:$$STNAME^ZMYRA(st)),!
 ;
 ;fields, exact values and canonical rewriting
 set st=$$GET^ZMYRA(h,"^ZXPAY(7,2)","amount",.out) write "amount of ^ZXPAY(7,2): ",out,!
 set st=$$GET^ZMYRA(h,"^ZXPAY(7,2)","value-date",.out) write "value-date: ",out," (M says ",$zdate(67854,"YEAR-MM-DD"),")",!
 set st=$$GET^ZMYRA(h,"^ZXPAY(""B"",""PAY-0001"",7,2)","ref",.out) write "index key ref: ",out,!
 set st=$$PUT^ZMYRA(h,"^ZXPAY(7,1)","status","P") write "put status P: ",$$STNAME^ZMYRA(st)," -> ",^ZXPAY(7,1),!
 set st=$$PUT^ZMYRA(h,"^ZXPAY(7,2)","amount","0250.250") write "put amount 0250.250: ",$$STNAME^ZMYRA(st)," -> ",^ZXPAY(7,2),!
 set st=$$PUT^ZMYRA(h,"^ZXPAY(7,2)","amount","1/3") write "put amount 1/3: ",$$STNAME^ZMYRA(st)," (",$$ERR^ZMYRA(),")",!
 set st=$$PUT^ZMYRA(h,"^ZXPAY(7,2)","memo","a^b") write "put memo a^b: ",$$STNAME^ZMYRA(st),!
 set st=$$PUT^ZMYRA(h,"^ZXPAY(7,5)","status","P") write "put on the refused ^ZXPAY(7,5): ",$$STNAME^ZMYRA(st),!
 ;M's own unary + makes the number canonical; then the node is valid
 set $piece(^ZXPAY(7,5),"^",2)=+$piece(^ZXPAY(7,5),"^",2)
 write "after +$piece: ",^ZXPAY(7,5)," ",$$STNAME^ZMYRA($$CHECK^ZMYRA(h,"^ZXPAY(7,5)")),!
 set st=$$POST^ZMYRA(h,"^ZXPAY(7,5)",map,.ent) write "  post ",$select(st=0:"entry "_ent,1:$$STNAME^ZMYRA(st)),!
 ;
 ;the node's own reference as the ledger reference
 set ^ZXPAY(8,1)="X-1^3^NGN^"_alice_"^"_bob
 set st=$$POST^ZMYRA(h,"^ZXPAY(8,1)","amount,from,to,ccy,",.ent) write "post ^ZXPAY(8,1) by its reference: ",$select(st=0:"entry "_ent,1:$$STNAME^ZMYRA(st)),!
 ;
 ;M collation and canonical numbers, through libzmyra
 write "collate 10 vs 9: ",$&zmyra.collate(10,9),", ""10"" vs ""9A"": ",$&zmyra.collate("10","9A"),", -1 vs """": ",$&zmyra.collate(-1,""),!
 write "canonic .5 ",$&zmyra.canonic(".5"),", 0.5 ",$&zmyra.canonic("0.5"),", 1E3 ",$&zmyra.canonic("1E3"),!
 ;
 ;balances, A = L + E, and a revoked capability
 set (a,b,e)=""
 set st=$&zmyra.equity(bank,.a),st=$&zmyra.equity(alice,.b),st=$&zmyra.equity(bob,.e)
 write "equity (kobo): bank ",a,", alice ",b,", bob ",e,!
 write "ledger check: ",$$STNAME^ZMYRA($&zmyra.lcheck()),!
 write "revoke the poster cap: ",$$STNAME^ZMYRA($&zmyra.revoke()),!
 set ^ZXPAY(8,2)="X-2^1^NGN^"_alice_"^"_bob
 set st=$$POST^ZMYRA(h,"^ZXPAY(8,2)",map,.ent) write "post after revoke: ",$$STNAME^ZMYRA(st),!
 kill ^ZXPAY
 write "done",!
 quit
 ;
pay(seq,v) ;store one payment and its "B" cross-reference
 set ^ZXPAY(7,seq)=v
 set ^ZXPAY("B",$piece(v,"^"),7,seq)=""
 quit
