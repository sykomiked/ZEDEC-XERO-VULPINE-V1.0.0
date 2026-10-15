(* Standard ML integer subset: every statement form once *)

fun show n = print (Int.toString n ^ "\n")

(* clauses, literals and wildcards *)
fun sign 0 = 0
  | sign n = if n < 0 then ~1 else 1

(* floored and truncating division *)
val _ = print (Int.toString (7 div 2) ^ " " ^ Int.toString (~7 div 2) ^ " " ^
               Int.toString (7 div ~2) ^ " " ^ Int.toString (~7 div ~2) ^ "\n")
val _ = print (Int.toString (7 mod 2) ^ " " ^ Int.toString (~7 mod 2) ^ " " ^
               Int.toString (7 mod ~2) ^ " " ^ Int.toString (~7 mod ~2) ^ "\n")
val _ = print (Int.toString (Int.quot (~7, 2)) ^ " " ^ Int.toString (Int.rem (~7, 2)) ^ "\n")
val _ = print (Int.toString (abs ~12) ^ " " ^ Int.toString (Int.min (3, ~4)) ^ " " ^
               Int.toString (Int.max (3, ~4)) ^ " " ^ Int.toString (sign ~9) ^ "\n")

(* the extremes of a 32-bit int *)
val big = 2147483647
val small = ~2147483648
val _ = print (Int.toString big ^ " " ^ Int.toString small ^ " " ^ Int.toString (small + 1) ^ "\n")

(* booleans *)
fun between (lo, x, hi) = lo <= x andalso x <= hi
val _ = print (Bool.toString (between (1, 5, 9)) ^ " " ^ Bool.toString (between (1, 0, 9)) ^ " " ^
               Bool.toString (not (3 = 4) orelse 1 div 0 = 0) ^ "\n")

(* let, nested functions that read their parent's variables *)
fun power (b, e) =
  let
    fun go (0, acc) = acc
      | go (k, acc) = go (k - 1, acc * b)
  in
    go (e, 1)
  end
val _ = show (power (3, 13))

(* curried parameters and a case on a tuple *)
fun compare a b =
  case (a < b, a = b) of
      (true, _) => ~1
    | (_, true) => 0
    | _ => 1
val _ = print (Int.toString (compare 1 2) ^ Int.toString (compare 2 2) ^ Int.toString (compare 3 2) ^ "\n")

(* mutual recursion *)
fun even 0 = true
  | even n = odd (n - 1)
and odd 0 = false
  | odd n = even (n - 1)
val _ = print (Bool.toString (even 10) ^ " " ^ Bool.toString (odd 7) ^ "\n")

(* a long loop: a tail call runs in constant space *)
fun count (0, s) = s
  | count (n, s) = count (n - 1, s + n mod 7)
val _ = show (count (200000, 0))

(* sequencing, unit and a top-level expression *)
val () = (print "a"; print "b"; print "\n")
val (q, r) = (17 div 5, 17 mod 5)
val _ = print (Int.toString q ^ "," ^ Int.toString r ^ "\n");
show (let val x = 6 val y = x * 7 in y end);
