# Regenerate pq_kat_vectors.h from a checkout of NIST's ACVP-Server
# (vectors used: commit 975de31eb83d87039ec88934fdc47d8c312b892d):
#   python3 gen_pq_kat.py pq_kat_vectors.h <ACVP-Server>/gen-val/json-files
import json, hashlib, sys
D=sys.argv[2].rstrip('/')+'/'
def L(d): return json.load(open(D+d+'/internalProjection.json'))['testGroups']
def grp(d, ps, **kw):
    for g in L(d):
        if g['parameterSet']==ps and all(g.get(k)==v for k,v in kw.items()):
            # shortest messages first keeps the header small; the fixed-size
            # API cannot express wrong-length signatures, so skip those.
            g['tests']=sorted([t for t in g['tests'] if 'too ' not in t.get('reason','')],
                              key=lambda t: len(t.get('message','')))
            return g
    raise SystemExit('no group %s %s %s'%(d,ps,kw))
def h(x): return hashlib.sha3_256(bytes.fromhex(x)).hexdigest()
def carr(name, hx):
    b=bytes.fromhex(hx)
    if not b: return 'static const uint8_t %s[1] = {0};\n'%name
    rows=[', '.join('0x%02x'%c for c in b[i:i+16]) for i in range(0,len(b),16)]
    return 'static const uint8_t %s[%d] = {\n    %s\n};\n'%(name,len(b),',\n    '.join(rows))
out=['/* pq_kat_vectors.h — generated from NIST ACVP-Server gen-val/json-files',
     ' * (github.com/usnistgov/ACVP-Server, internalProjection.json). Large',
     ' * expected outputs are stored as their SHA3-256 digest. Do not edit. */',
     '#include <stdint.h>','']
cases=[]
def add(kind, fields, meta):
    i=len(cases); s=''
    for k,v in fields.items(): s+=carr('k%d_%s'%(i,k), v)
    out.append(s); cases.append((kind,meta,list(fields.keys()),{k:len(bytes.fromhex(v)) for k,v in fields.items()}))
# ML-DSA-65 keyGen: seed -> H(pk), H(sk)
for t in grp('ML-DSA-keyGen-FIPS204','ML-DSA-65')['tests'][:5]:
    add('MLDSA_KEYGEN', {'seed':t['seed'],'hpk':h(t['pk']),'hsk':h(t['sk'])}, t['tcId'])
# sigGen deterministic pure, and hedged pure
for t in grp('ML-DSA-sigGen-FIPS204','ML-DSA-65',deterministic=True,signatureInterface='external',preHash='pure')['tests'][:3]:
    add('MLDSA_SIGN', {'sk':t['sk'],'msg':t['message'],'ctx':t['context'],'rnd':'','hsig':h(t['signature'])}, t['tcId'])
for t in grp('ML-DSA-sigGen-FIPS204','ML-DSA-65',deterministic=False,signatureInterface='external',preHash='pure')['tests'][:2]:
    add('MLDSA_SIGN', {'sk':t['sk'],'msg':t['message'],'ctx':t['context'],'rnd':t['rnd'],'hsig':h(t['signature'])}, t['tcId'])
# sigVer: pick 2 pass + 3 fail
g=grp('ML-DSA-sigVer-FIPS204','ML-DSA-65',signatureInterface='external',preHash='pure')['tests']
sel=[t for t in g if t['testPassed']][:2]+[t for t in g if not t['testPassed']][:3]
for t in sel:
    add('MLDSA_VERIFY', {'pk':t['pk'],'msg':t['message'],'ctx':t['context'],'sig':t['signature']}, (t['tcId'], int(t['testPassed']), t['reason']))
# SLH keyGen
for t in grp('SLH-DSA-keyGen-FIPS205','SLH-DSA-SHAKE-128s')['tests'][:5]:
    add('SLH_KEYGEN', {'seed':t['skSeed']+t['skPrf']+t['pkSeed'],'pk':t['pk'],'sk':t['sk']}, t['tcId'])
# SLH sigGen: 1 deterministic, 1 hedged (each takes a while)
t=grp('SLH-DSA-sigGen-FIPS205','SLH-DSA-SHAKE-128s',deterministic=True,signatureInterface='external',preHash='pure')['tests'][0]
add('SLH_SIGN', {'sk':t['sk'],'msg':t['message'],'ctx':t['context'],'rnd':'','hsig':h(t['signature'])}, t['tcId'])
t=grp('SLH-DSA-sigGen-FIPS205','SLH-DSA-SHAKE-128s',deterministic=False,signatureInterface='external',preHash='pure')['tests'][0]
add('SLH_SIGN', {'sk':t['sk'],'msg':t['message'],'ctx':t['context'],'rnd':t['additionalRandomness'],'hsig':h(t['signature'])}, t['tcId'])
g=grp('SLH-DSA-sigVer-FIPS205','SLH-DSA-SHAKE-128s',signatureInterface='external',preHash='pure')['tests']
sel=[t for t in g if t['testPassed']][:1]+[t for t in g if not t['testPassed']][:3]
for t in sel:
    add('SLH_VERIFY', {'pk':t['pk'],'msg':t['message'],'ctx':t['context'],'sig':t['signature']}, (t['tcId'], int(t['testPassed']), t['reason']))
# table
out.append('typedef enum { MLDSA_KEYGEN, MLDSA_SIGN, MLDSA_VERIFY, SLH_KEYGEN, SLH_SIGN, SLH_VERIFY } kat_kind_t;')
out.append('typedef struct { kat_kind_t kind; uint32_t tc; int32_t expect; const char *why;\n    const uint8_t *a, *b, *c, *d, *e; uint32_t na, nb, nc, nd, ne; } kat_t;')
out.append('static const kat_t KATS[] = {')
for i,(kind,meta,keys,lens) in enumerate(cases):
    tc,exp,why = (meta,1,'') if not isinstance(meta,tuple) else meta
    names=['k%d_%s'%(i,k) for k in keys]+['0']*(5-len(keys))
    ls=[str(lens[k]) for k in keys]+['0']*(5-len(keys))
    out.append('    { %s, %d, %d, "%s", %s, %s },'%(kind,tc,exp,why.replace('"',"'"),', '.join(names),', '.join(ls)))
out.append('};')
out.append('#define KAT_COUNT (sizeof(KATS) / sizeof(KATS[0]))')
open(sys.argv[1],'w').write('\n'.join(out)+'\n')
print(len(cases))
