ZMYRA ;MYRA schemas for M globals, through libzmyra ;pillar API 0.1
 ;Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 ;SPDX-License-Identifier: Apache-2.0
 ;
 ;Extrinsic wrappers over the external calls in m/zmyra.xc.in. Each one
 ;returns 0 or a negative status: $$STNAME^ZMYRA(st) names it and
 ;$$ERR^ZMYRA() gives the detail. A global reference is passed as text,
 ;as $NAME and $QUERY write it: "^ZXPAY(7,1)".
 ;
 ;NOT A CLINICAL OR REGULATED-SYSTEM COMPONENT: no HIPAA, clinical-safety
 ;or certification claim. It checks the shape of data, not its meaning.
 ;Any clinical use is advisory only and needs a qualified human's approval.
 quit
 ;
LOAD(path) ;a global map or MYRA file -> a handle (0..15) or a status
 quit $&zmyra.load(path)
 ;
CHECK(h,gref) ;is the value at gref exactly valid for its node?
 quit $&zmyra.check(h,gref,$get(@gref))
 ;
NODE(h,gref) ;the 1-based node of the schema that gref belongs to
 quit $&zmyra.node(h,gref)
 ;
GET(h,gref,field,out) ;out = the field's display text (keys too)
 new st
 set out=""
 set st=$&zmyra.get(h,gref,$get(@gref),field,.out)
 quit st
 ;
PUT(h,gref,field,text) ;set one piece from display text; store the canonical value
 new st,v
 set v=""
 set st=$&zmyra.put(h,gref,$get(@gref),field,text,.v)
 if st=0 set @gref=v
 quit st
 ;
CANON(h,gref) ;rewrite the value at gref in canonical form
 new st,v
 set v=""
 set st=$&zmyra.canon(h,gref,$get(@gref),.v)
 if st=0 set @gref=v
 quit st
 ;
POST(h,gref,map,entry) ;post the node at gref to the ledger (see zmyra_xc.c)
 new st
 set entry=""
 set st=$&zmyra.post(h,gref,$get(@gref),map,.entry)
 quit st
 ;
STNAME(st) ;the name of a status
 new n
 set n=""
 if $&zmyra.status(st,.n)
 quit n
 ;
ERR() ;the detail of the last refusal
 new e
 set e=""
 if $&zmyra.error(.e)
 quit e
