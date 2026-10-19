# SPDX-License-Identifier: Apache-2.0
"""Python Fraction oracle for both native-wide and forced narrow C backends."""
import ctypes
from fractions import Fraction
import random
import sys

class Rat(ctypes.Structure):
    _fields_=[('num',ctypes.c_int64),('den',ctypes.c_int64),('valid',ctypes.c_bool)]
    def __init__(self,num=0,den=1,valid=True):
        super().__init__(num,den,valid)

lib=ctypes.CDLL(sys.argv[1])
for name in ('make','add','sub','mul','div','cmp'):
    fn=getattr(lib,'rat_'+name)
    fn.argtypes=[ctypes.c_int64,ctypes.c_int64] if name=='make' else [Rat,Rat]
    fn.restype=ctypes.c_int if name=='cmp' else Rat
checks=0

def assert_value(actual,expected):
    global checks
    assert actual.valid and actual.den>0, (actual.num,actual.den,expected)
    assert Fraction(actual.num,actual.den)==expected, (actual.num,actual.den,expected)
    assert actual.num==expected.numerator and actual.den==expected.denominator
    checks+=1

rng=random.Random(0x5a5856)
for _ in range(10000):
    a=Fraction(rng.randint(-100000,100000),rng.randint(1,100000))
    b=Fraction(rng.randint(-100000,100000),rng.randint(1,100000))
    ca=Rat(a.numerator,a.denominator); cb=Rat(b.numerator,b.denominator)
    assert_value(lib.rat_add(ca,cb),a+b)
    assert_value(lib.rat_sub(ca,cb),a-b)
    assert_value(lib.rat_mul(ca,cb),a*b)
    if b: assert_value(lib.rat_div(ca,cb),a/b)
    assert lib.rat_cmp(ca,cb)==((a>b)-(a<b));checks+=1
M=(1<<63)-1
for a,b in ((Fraction(M,M-1),Fraction(M-1,M)),(Fraction(-M,M-1),Fraction(-M+1,M)),(Fraction(0),Fraction(-M)),(Fraction(-1,3),Fraction(-1,2))):
    assert lib.rat_cmp(Rat(a.numerator,a.denominator),Rat(b.numerator,b.denominator))==((a>b)-(a<b));checks+=1
for n,d in ((1,0),(-(1<<63),1),(1,-(1<<63))):
    assert not lib.rat_make(n,d).valid;checks+=1
assert not lib.rat_add(Rat(M,1),Rat(1,1)).valid;checks+=1
assert not lib.rat_mul(Rat(M,1),Rat(2,1)).valid;checks+=1
assert not lib.rat_div(Rat(1,1),Rat(0,1)).valid;checks+=1
# Negative denominators normalize once at construction.
assert_value(lib.rat_make(6,-8),Fraction(-3,4))
print(f'rational differential: {checks} Fraction oracle checks passed')
