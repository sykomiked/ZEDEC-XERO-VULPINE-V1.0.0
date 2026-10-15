-- Haskell subset: every statement and expression form once
module Main (main) where

{- floored div and mod, truncating quot and rem;
   {- block comments nest -} -}
divisions :: IO ()
divisions = do
  print (7 `div` 2, (-7) `div` 2, 7 `div` (-2), (-7) `div` (-2))
  print (7 `mod` 2, (-7) `mod` 2, 7 `mod` (-2), (-7) `mod` (-2))
  print (quot 7 (-2), rem (-7) 2, abs (-12), signum (-3), negate 4)
  print (min 3 (-4), max 3 (-4), succ 9, pred 0, gcd 12 18, lcm 4 6)
  print (2 ^ 62, 3 ^ 0, - 2 ^ 2, (-2) ^ 3)

-- Int is 64 bits: the largest values still print; past them the run stops
limits :: IO ()
limits = do
  print (9223372036854775807, -9223372036854775807 - 1)
  print (3037000499 * 3037000499)

-- guards, where, and booleans (&& and || do not evaluate their right side
-- when the left decides)
between :: Int -> Int -> Int -> Bool
between lo x hi = lo <= x && x <= hi

classify :: Int -> IO ()
classify n
  | n < lo = putStrLn "small"
  | n < hi = putStrLn "medium"
  | otherwise = putStrLn "large"
  where
    lo = 10
    hi = lo * 10

-- let ... in, and a local function that reads its parent's variables
power :: Int -> Int -> Int
power b e =
  let go 0 acc = acc
      go k acc = go (k - 1) (acc * b)
  in go e 1

-- case with guards in the alternatives
compare' :: Int -> Int -> Int
compare' a b = case a - b of
  d | d < 0 -> -1
    | d == 0 -> 0
  _ -> 1

-- mutual recursion
isEven, isOdd :: Int -> Bool
isEven 0 = True
isEven n = isOdd (n - 1)
isOdd 0 = False
isOdd n = isEven (n - 1)

-- a long loop: a tail call runs in constant space
count :: Int -> Int -> Int
count 0 s = s
count n s = count (n - 1) (s + n `mod` 7)

-- a where binding is evaluated only when it is used
safeDiv :: Int -> Int -> Int
safeDiv a b
  | b == 0 = 0
  | otherwise = q
  where q = a `div` b

main :: IO ()
main = do
  divisions
  print (between 1 5 9, between 1 0 9, not (3 == 4) || 1 `div` 0 == 0)
  print (True < False, max True False, False && undefined', isEven 10, isOdd 7)
  classify 3
  classify 42
  classify 420
  print $ power 3 13
  putStrLn (show (compare' 1 2) ++ show (compare' 2 2) ++ show (compare' 3 2))
  print (count 200000 0)
  print (safeDiv 7 0, safeDiv (-7) 2)
  let x = 6
      y = x * 7
  if y > 40 then putStrLn "big" else putStrLn "small"
  print (let z = y + 1 in z * z)
  putStr "no newline, "
  putStrLn "then one"
  limits
  where
    undefined' = 1 `div` 0 == 0
