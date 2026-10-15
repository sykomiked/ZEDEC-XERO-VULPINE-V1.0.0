(* OCaml integer subset: every statement form once *)

let show n = print_int n; print_newline ()

(* truncating division, and the sign of mod *)
let () = Printf.printf "%d %d %d %d\n" (7 / 2) (-7 / 2) (7 / -2) (-7 / -2)
let () = Printf.printf "%d %d %d %d\n" (7 mod 2) (-7 mod 2) (7 mod -2) (-7 mod -2)
let () = Printf.printf "%d %d %d %d %d\n" (abs (-12)) (min 3 (-4)) (max 3 (-4)) (succ 9) (pred 0)

(* 63-bit integers wrap around *)
let () = Printf.printf "%d %d\n" max_int min_int
let () = Printf.printf "%d %d %d\n" (max_int + 1) (min_int - 1) (- min_int)
let () = Printf.printf "%d %d\n" (max_int * 3) (123456789123 * 987654321987)
let () = Printf.printf "%d %d\n" (min_int / -1) (abs min_int)

(* booleans *)
let between lo x hi = lo <= x && x <= hi
let () = Printf.printf "%b %b %b\n" (between 1 5 9) (between 1 0 9) (not (3 = 4) || 1 / 0 = 0)
let () = print_endline (string_of_bool (true < false) ^ " " ^ string_of_bool (max true false))

(* let ... in, and nested functions that read their parent's variables *)
let power b e =
  let rec go k acc = if k = 0 then acc else go (k - 1) (acc * b) in
  go e 1
let () = show (power 3 13)

(* match on a tuple, with a guard *)
let compare a b =
  match a, b with
  | x, y when x < y -> -1
  | x, y when x = y -> 0
  | _ -> 1
let () = print_string (string_of_int (compare 1 2) ^ string_of_int (compare 2 2) ^ string_of_int (compare 3 2) ^ "\n")

(* function, and mutual recursion *)
let rec even = function 0 -> true | n -> odd (n - 1)
and odd = function 0 -> false | n -> even (n - 1)
let () = Printf.printf "%b %b\n" (even 10) (odd 7)

(* a long loop: a tail call runs in constant space *)
let rec count n s = if n = 0 then s else count (n - 1) (s + n mod 7)
let () = show (count 200000 0)

(* if without else, sequences, tuples taken apart *)
let () = if 1 < 2 then print_string "a"; print_string "b"; print_newline ()
let (q, r) = (17 / 5, 17 mod 5)
let () = Printf.printf "%d,%d%%\n" q r
;;
show (let x = 6 in let y = x * 7 in y)
