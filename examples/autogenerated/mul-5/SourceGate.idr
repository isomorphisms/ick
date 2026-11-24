module SourceGate

import Data.Bits
import Data.List
import Data.Maybe
import Data.String
import System
import System.File
import Reference
import ScalarGate

%default covering

precision : Integer
precision = 256

validSource : Integer -> Bool
validSource index = index `div` 8<3 && index `mod` 8<5

fixture : Integer -> Integer -> (Integer,Integer)
fixture 1 index = (if index==0 then 8 else 0,0)
fixture 2 index = (0,if index==12 then -8 else 0)
fixture 3 index = if index==20 then (6,4) else (0,0)
fixture 4 index = (index `mod` 7-3,(index*5) `mod` 11-5)
fixture _ _ = (0,0)

referenceRoots : List (Integer,(Bounds,Bounds))
referenceRoots =
  let base = shiftL 1 384
      pi = piBounds base 416 in
  map (\index => (index,rootBounds base 416 pi index 15)) (the (List Integer) [0..14])

scaleSigned : Bounds -> Integer -> Bounds
scaleSigned value numerator =
  if numerator>=0 then scaled value numerator 16
  else subtract (Interval 0 0) (scaled value (-numerator) 16)

referenceCell : List (Integer,(Bounds,Bounds)) -> Integer -> Integer -> Integer -> Maybe (Bounds,Bounds)
referenceCell roots example opposite target = do
  terms <- traverse term (filter validSource (the (List Integer) [0..31]))
  let (real,imaginary) = foldl addTerms (Interval 0 0,Interval 0 0) terms
  pure (scaled real 1 15,scaled imaginary 1 15)
  where
    -- P_s reads frequency t*j on each source axis. The direct matrix uses
    -- denominator 15, with no chirp, target tensor or resampling operations.
    term : Integer -> Maybe (Bounds,Bounds)
    term source = do
      let frequency = 20*(target `div` 8)*(source `div` 8)+24*(target `mod` 8)*(source `mod` 8)
      let signed = if opposite==0 then -frequency else frequency
      let numerator = ((signed `mod` 15)+15) `mod` 15
      (rootReal,rootImaginary) <- lookup numerator roots
      let (inputReal,inputImaginary) = fixture example source
      pure (subtract (scaleSigned rootReal inputReal) (scaleSigned rootImaginary inputImaginary),
            add (scaleSigned rootImaginary inputReal) (scaleSigned rootReal inputImaginary))
    addTerms : (Bounds,Bounds) -> (Bounds,Bounds) -> (Bounds,Bounds)
    addTerms (firstReal,firstImaginary) (secondReal,secondImaginary) =
      (add firstReal secondReal,add firstImaginary secondImaginary)

valueValid : List (Integer,(Bounds,Bounds)) -> Integer -> Integer -> Integer -> List String -> Bool
valueValid roots example opposite index ["source",test,direction,address,realText,imaginaryText] =
  fromMaybe False $ do
    real <- hexadecimal precision realText
    imaginary <- hexadecimal precision imaginaryText
    let correctAddress = test==show example && direction==show opposite && address==show index
    if not (validSource index) then pure (correctAddress && real==0 && imaginary==0) else do
      (expectedReal,expectedImaginary) <- referenceCell roots example opposite index
      let unit = shiftL 1 128
      let grid = shiftL 1 256
      let bound = shiftL (4*precision*precision+1024) 144 * unit
      let realError = distance (real*unit) expectedReal.lower expectedReal.upper
      let imaginaryError = distance (imaginary*unit) expectedImaginary.lower expectedImaginary.upper
      pure (correctAddress && real*real+imaginary*imaginary<=grid*grid &&
            realError*realError+imaginaryError*imaginaryError<bound*bound)
valueValid _ _ _ _ _ = False

checkExamples : List (Integer,(Bounds,Bounds)) -> List (Integer,Integer) -> List (List String) -> Bool
checkExamples _ [] [["refusals","pass"]] = True
checkExamples roots ((example,opposite)::rest) (["trace",test,direction,"8","24",motifs,"16",scalar]::rows) =
  let (values,remaining) = splitAt 32 rows in
  test==show example && direction==show opposite && maybe False (>0) (parseInteger motifs) &&
  maybe False (>512) (parseInteger scalar) && length values==32 &&
  all (\(index,value) => valueValid roots example opposite index value) (zip [0..31] values) &&
  checkExamples roots rest remaining
checkExamples _ _ _ = False

main : IO ()
main = do
  [_,actualPath] <- getArgs | _ => exitWith (ExitFailure 9)
  Right contents <- readFile actualPath | _ => exitWith (ExitFailure 9)
  let cases = [(example,direction) | example <- the (List Integer) [0..4], direction <- [0,1]]
  if checkExamples referenceRoots cases (map words (lines contents))
    then putStrLn "PASS 150 directed source values, 170 exact padding records, stages and refusals"
    else do
      putStrLn "FAIL source transform value, padding, stages or refusal"
      exitWith (ExitFailure 9)
