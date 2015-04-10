#!/bin/sh

sudo apt-get update -qq

alias apt_install='sudo apt-get -qy --no-install-suggests --no-install-recommends install'

apt_install "g++-multilib" gcc-multilib linux-libc-dev:i386 # 32bit compilers
apt_install libreadline-dev:i386

if [ "$USE_LLVM" = "On" ]; then
    apt_install llvm-3.3:i386 llvm-3.3-dev:i386 libllvm3.3:i386 llvm-3.3-runtime:i386
fi
