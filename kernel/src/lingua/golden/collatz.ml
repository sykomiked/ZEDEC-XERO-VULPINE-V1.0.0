(* The same three algorithms as collatz.f, collatz.bas and collatz.fs *)

let rec steps n s =
  if n = 1 then s
  else if n mod 2 = 0 then steps (n / 2) (s + 1)
  else steps (3 * n + 1) (s + 1)

let rec gcd a b = if b = 0 then a else gcd b (a mod b)

let rec fib f g k =
  if k > 0 then begin
    print_int f;
    print_newline ();
    fib g (f + g) (k - 1)
  end

let rec table n =
  if n <= 12 then begin
    Printf.printf "%d %d\n" n (steps n 0);
    table (n + 1)
  end

let () = table 1
let () = print_string ("GCD " ^ string_of_int (gcd 1071 462) ^ "\n")
let () = fib 0 1 15
