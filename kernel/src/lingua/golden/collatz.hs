-- The same three algorithms as collatz.f, collatz.bas and collatz.fs

steps :: Int -> Int -> Int
steps 1 s = s
steps n s
  | even n = steps (n `div` 2) (s + 1)
  | otherwise = steps (3 * n + 1) (s + 1)

gcd' :: Int -> Int -> Int
gcd' a 0 = a
gcd' a b = gcd' b (a `mod` b)

fib :: Int -> Int -> Int -> IO ()
fib f g k =
  if k > 0
    then do
      print f
      fib g (f + g) (k - 1)
    else return ()

table :: Int -> IO ()
table n
  | n <= 12 = do
      putStrLn (show n ++ " " ++ show (steps n 0))
      table (n + 1)
  | otherwise = return ()

main :: IO ()
main = do
  table 1
  putStrLn ("GCD " ++ show (gcd' 1071 462))
  fib 0 1 15
