# the same three algorithms as collatz.bas: collatz steps, euclid's gcd,
# the fibonacci ladder. The Lingua AWK subset has no '/' (it floats), so
# halving is the subtraction loop and gcd takes '%' directly.
BEGIN {
    for (n = 1; n <= 12; n++) {
        k = n
        s = 0
        while (k != 1) {
            if (k % 2) {
                k = 3 * k + 1
            } else {
                q = 0
                while (k >= 2) {
                    k -= 2
                    q++
                }
                k = q
            }
            s++
        }
        print n " " s
    }
    a = 1071
    b = 462
    while (b != 0) {
        t = a % b
        a = b
        b = t
    }
    print "GCD " a
    f = 0
    g = 1
    for (i = 1; i <= 15; i++) {
        print f
        t = f + g
        f = g
        g = t
    }
}
