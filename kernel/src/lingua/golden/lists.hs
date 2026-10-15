-- Lists: patterns, recursion, the Prelude list functions, ranges and
-- structural equality and order

sumList :: [Int] -> Int
sumList [] = 0
sumList (x:xs) = x + sumList xs

filterEven :: [Int] -> [Int]
filterEven [] = []
filterEven (x:xs)
  | even x = x : filterEven xs
  | otherwise = filterEven xs

-- insertion sort
insert :: Int -> [Int] -> [Int]
insert x [] = [x]
insert x (y:ys)
  | x <= y = x : y : ys
  | otherwise = y : insert x ys

sort :: [Int] -> [Int]
sort [] = []
sort (x:xs) = insert x (sort xs)

pairs :: [a] -> Int
pairs [] = 0
pairs [_] = 0
pairs (_:_:rest) = 1 + pairs rest

lengths :: [[a]] -> [Int]
lengths [] = []
lengths (l:ls) = length l : lengths ls

-- a pattern of fixed length
middle :: [Int] -> Int
middle [_, m, _] = m
middle _ = 0

main :: IO ()
main = do
  let xs = [1 .. 10]
  print xs
  print (sumList xs, length xs, pairs xs)
  print (filterEven xs)
  print (reverse xs)
  print (sort [5, -3, 8, 0, -3, 12, 7])
  print ([1, 2] ++ [3] ++ [])
  print (head xs, head (tail xs), last xs, init [1, 2, 3], xs !! 4)
  print ([1, 2, 3] == [1 .. 3], [[1], [2, 3]] == [[1], [2]], [] /= [True])
  print ([1, 2] < [1, 3], [2] > [1, 9], compare' [] [0])
  print (lengths [[], [1], [1, 2], [1 .. 7]])
  print (sum xs, product [1 .. 10], maximum [3, 9, 2], minimum [3, 9, 2])
  print (take 3 xs, drop 7 xs, take 20 [1, 2], drop (-1) [5])
  print (elem 4 xs, notElem 4 xs, null xs, null [])
  print (replicate 4 0, replicate 0 True, replicate (-1) 7)
  print ([10, 8 .. 1], [1, 4 .. 20], [5 .. head xs], concat [[1], [], [2, 3]])
  print (and [True, False], or [False, True], and [])
  print (middle [1, 2, 3], middle [1, 2])
  putStrLn ("sorted: " ++ show (sort (reverse xs)))
  where
    compare' a b = if a < b then -1 else if a == b then 0 else 1
