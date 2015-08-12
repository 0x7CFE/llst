#!/bin/bash

if [[ "${CC}" == "gcc" ]]; then
    export CC=gcc-4.8
    export CXX=g++-4.8
fi

function brew_upgrade { brew outdated $1 || brew upgrade $1; }

brew update
brew install readline
brew install llvm33
