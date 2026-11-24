module ChirpGate

import Data.Bits
import Data.List
import Data.Maybe
import Data.String
import System
import System.File
import Reference
import ScalarGate

%default covering

record ComplexBounds where
  constructor Rect
  real, imaginary : Bounds

precision : Integer
precision = 256

referenceBase : Integer
referenceBase = shiftL 1 384

fixture : Integer -> Integer -> (Integer,Integer)
fixture 1 index = (if index==0 then 8 else 0,0)
fixture 2 index = (0,if index==7 then -8 else 0)
fixture 3 index = if index==31 then (7,10) else (0,0)
fixture 4 index = (index `mod` 7-3,(index*5) `mod` 11-5)
fixture 5 index = (if index==0 then 16 else 0,0)
fixture _ _ = (0,0)

referenceRoots : List (Integer,ComplexBounds)
referenceRoots =
  let pi = piBounds referenceBase 416 in
  map (\index => let (real,imaginary) = rootBounds referenceBase 416 pi index 16 in
                    (index,Rect real imaginary)) (the (List Integer) [0..15])

scaleSigned : Bounds -> Integer -> Bounds
scaleSigned value numerator =
  if numerator>=0 then scaled value numerator 16
  else subtract (Interval 0 0) (scaled value (-numerator) 16)

referenceCell : List (Integer,ComplexBounds) -> Integer -> Integer -> Maybe ComplexBounds
referenceCell roots example target = do
  terms <- traverse term (the (List Integer) [0..31])
  let Rect real imaginary = foldl addTerms (Rect (Interval 0 0) (Interval 0 0)) terms
  pure (Rect (scaled real 1 32) (scaled imaginary 1 32))
  where
    -- Direct Q F_t matrix entry, with positive sign and permuted frequency.
    -- The reference does not use the candidate's chirp, twists or convolution.
    term : Integer -> Maybe ComplexBounds
    term source = do
      let numerator = (12*(target `div` 8)*(source `div` 8)
                      +10*(target `mod` 8)*(source `mod` 8)) `mod` 16
      Rect rootReal rootImaginary <- lookup numerator roots
      let (inputReal,inputImaginary) = fixture example source
      pure (Rect (subtract (scaleSigned rootReal inputReal) (scaleSigned rootImaginary inputImaginary))
                 (add (scaleSigned rootImaginary inputReal) (scaleSigned rootReal inputImaginary)))
    addTerms : ComplexBounds -> ComplexBounds -> ComplexBounds
    addTerms first second = Rect (add first.real second.real) (add first.imaginary second.imaginary)

valueValid : List (Integer,ComplexBounds) -> Integer -> Integer -> List String -> Bool
valueValid roots example index ["value",test,address,realText,imaginaryText] = fromMaybe False $ do
  expected <- referenceCell roots example index
  real <- hexadecimal precision realText
  imaginary <- hexadecimal precision imaginaryText
  let unit = shiftL 1 128
  let grid = shiftL 1 256
  let realError = distance (real*unit) expected.real.lower expected.real.upper
  let imaginaryError = distance (imaginary*unit) expected.imaginary.lower expected.imaginary.upper
  pure (test==show example && address==show index && real*real+imaginary*imaginary<=grid*grid &&
        realError*realError+imaginaryError*imaginaryError<(1024*unit)*(1024*unit))
valueValid _ _ _ _ = False

checkExamples : List (Integer,ComplexBounds) -> List Integer -> List (List String) -> Bool
checkExamples _ [] [["refusals","pass"]] = True
checkExamples roots (example::rest) (["trace",test,"5",motifs,"16","512"]::rows) =
  let (values,remaining) = splitAt 32 rows in
  test==show example && maybe False (>0) (parseInteger motifs) && length values==32 &&
  all (\(index,value) => valueValid roots example index value) (zip [0..31] values) &&
  checkExamples roots rest remaining
checkExamples _ _ _ = False

main : IO ()
main = do
  [_,actualPath] <- getArgs | _ => exitWith (ExitFailure 9)
  Right contents <- readFile actualPath | _ => exitWith (ExitFailure 9)
  if checkExamples referenceRoots [0..5] (map words (lines contents))
    then putStrLn "PASS 192 directed matrix comparisons, observed motif/packed-product stages and refusal checks"
    else do
      putStrLn "FAIL chirp value, grid, stage or refusal"
      exitWith (ExitFailure 9)
