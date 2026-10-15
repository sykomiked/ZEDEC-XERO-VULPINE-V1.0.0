(* Lists: patterns, recursion, the List functions and structural equality *)

let show l =
  let rec items = function
    | [] -> ()
    | [x] -> print_int x
    | x :: rest -> print_int x; print_string ";"; items rest
  in
  print_string "["; items l; print_string "]\n"

let rec range a b = if a > b then [] else a :: range (a + 1) b

let rec sum = function [] -> 0 | x :: xs -> x + sum xs

let rec filter_even l =
  match l with
  | [] -> []
  | x :: xs -> if x mod 2 = 0 then x :: filter_even xs else filter_even xs

(* insertion sort *)
let rec insert x l =
  match l with
  | [] -> [x]
  | y :: ys -> if x <= y then x :: y :: ys else y :: insert x ys
let rec sort = function [] -> [] | x :: xs -> insert x (sort xs)

let rec pairs = function
  | [] -> 0
  | [_] -> 0
  | _ :: _ :: rest -> 1 + pairs rest

let xs = range 1 10
let () = show xs
let () = Printf.printf "%d %d %d\n" (sum xs) (List.length xs) (pairs xs)
let () = show (filter_even xs)
let () = show (List.rev xs)
let () = show (sort [5; -3; 8; 0; -3; 12; 7])
let () = show ([1; 2] @ [3] @ [])
let () = Printf.printf "%d %d\n" (List.hd xs) (List.hd (List.tl xs))
let () = Printf.printf "%b %b %b\n" ([1; 2; 3] = range 1 3) ([[1]; [2; 3]] = [[1]; [2]]) ([] <> [true])

(* a list of lists *)
let rec lengths = function [] -> [] | l :: ls -> List.length l :: lengths ls
let () = show (lengths [[]; [1]; [1; 2]; range 1 7])

(* the elements are built right to left, as ocaml evaluates them *)
let say n = print_int n; n
let () = show [say 1; say 2; say 3]
let () = show (say 4 :: say 5 :: [])
