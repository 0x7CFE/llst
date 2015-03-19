#!/bin/sh

sudo add-apt-repository --yes ppa:ubuntu-toolchain-r/test
sudo apt-get update -qq

sudo apt-get install -y binutils:i386 llvm-3.3-dev:i386 libreadline-dev:i386

if [ "$CXX" = "clang++" ]; then
    sudo apt-get install -y g++-4.8-multilib
fi
if [ "$CXX" = "g++" ]; then
    sudo apt-get install -y gcc:i386 g++:i386 cpp:i386 g++-4.6:i386 gcc-4.6:i386
fi

