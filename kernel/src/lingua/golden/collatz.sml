(* The same three algorithms as collatz.f, collatz.bas and collatz.fs *)

fun steps (1, s) = s
  | steps (n, s) =
      if n mod 2 = 0 then steps (n div 2, s + 1)
      else steps (3 * n + 1, s + 1)

fun gcd (a, 0) = a
  | gcd (a, b) = gcd (b, a mod b)

fun fib (f, g, 0) = ()
  | fib (f, g, k) = (print (Int.toString f ^ "\n"); fib (g, f + g, k - 1))

fun table n =
  if n > 12 then ()
  else (print (Int.toString n ^ " " ^ Int.toString (steps (n, 0)) ^ "\n");
        table (n + 1))

val _ = table 1
val _ = print ("GCD " ^ Int.toString (gcd (1071, 462)) ^ "\n")
val _ = fib (0, 1, 15)
