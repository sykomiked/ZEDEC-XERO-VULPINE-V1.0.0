(* Lists: patterns, recursion, the basis functions and structural equality *)

fun show l =
  let
    fun items [] = ()
      | items [x] = print (Int.toString x)
      | items (x :: rest) = (print (Int.toString x ^ ","); items rest)
  in
    (print "["; items l; print "]\n")
  end

fun range (a, b) = if a > b then [] else a :: range (a + 1, b)

fun sum [] = 0
  | sum (x :: xs) = x + sum xs

fun filter_even [] = []
  | filter_even (x :: xs) =
      if x mod 2 = 0 then x :: filter_even xs else filter_even xs

(* insertion sort *)
fun insert (x, []) = [x]
  | insert (x, y :: ys) = if x <= y then x :: y :: ys else y :: insert (x, ys)
fun sort [] = []
  | sort (x :: xs) = insert (x, sort xs)

(* length works on any list *)
fun pairs [] = 0
  | pairs [_] = 0
  | pairs (_ :: _ :: rest) = 1 + pairs rest

val xs = range (1, 10)
val _ = show xs
val _ = print (Int.toString (sum xs) ^ " " ^ Int.toString (length xs) ^ " " ^
               Int.toString (pairs xs) ^ "\n")
val _ = show (filter_even xs)
val _ = show (rev xs)
val _ = show (sort [5, ~3, 8, 0, ~3, 12, 7])
val _ = show ([1, 2] @ [3] @ [])
val _ = print (Int.toString (hd xs) ^ " " ^ Int.toString (hd (tl xs)) ^ " " ^
               Bool.toString (null []) ^ " " ^ Bool.toString (null xs) ^ "\n")
val _ = print (Bool.toString ([1, 2, 3] = range (1, 3)) ^ " " ^
               Bool.toString ([[1], [2, 3]] = [[1], [2]]) ^ " " ^
               Bool.toString (length [true, false] = 2) ^ "\n")

(* a list of lists *)
fun lengths [] = []
  | lengths (l :: ls) = length l :: lengths ls
val _ = show (lengths [[], [1], [1, 2], range (1, 7)])

(* overflow is checked at 32 bits in a list program too *)
val _ = print (Int.toString (sum [2147483647, ~2147483648, 1]) ^ "\n")
