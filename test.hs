
module Main where
import System.Cmd
import Control.Monad

tests :: [String]
tests = ["1", "2"]

main = mapM (system . ("echo " ++ )) tests