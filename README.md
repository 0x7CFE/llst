[![Build Status](https://travis-ci.org/0x7CFE/llst.svg?branch=develop)](https://travis-ci.org/0x7CFE/llst)
[![Coverage Status](https://coveralls.io/repos/0x7CFE/llst/badge.svg?branch=develop)](https://coveralls.io/r/0x7CFE/llst?branch=develop)

Overview
=================
LLST stands for LLVM Smalltalk or Low Level Smalltalk (which intends it's use in embedded environments).

This project aims to create a simple yet powerful VM compatible with Little Smalltalk. VM tries to take advantage of LLVM to make it fast. Other goal is to interface the Qt library to the VM, making it possible to use modern looking GUI widgets.

Current implementation is fully compliant with original Little Smalltalk written by Timothy Budd. The only difference from the user's point of view is additional primitive set.

Little Smalltalk is written in C, whereas LLST uses C++ with templates.

Our main goal was to rewrite Little Smalltalk from scratch in C++ in order to make source code readable and easy to use. We really tried our best to make it possible. We analyzed the original source code and reorganized it, in a way that Smalltalk classes are represented by usual C++ classes.

We provided a comprehensive set of templates and template functions that help programmer easily interact with Smalltalk. Instantiation, access and operation with Smalltalk objects is now simple and very intuitive. Field and space counting is no longer required.

Please note that both Little Smalltalk and LLST are NOT Smalltalk-80 compatible.

Features
========

* Binary compliant with original Little Smalltalk. Generic image may be loaded directly.
* **Small** size. Dynamically linked software implementation of VM takes around 46 KB.
* LLVM Support. Experimental yet working JIT runtime is provided. JIT runtime is completely transparent and is able to execute the same code as soft VM gaining up to x24 performace speed comparing to soft VM execution.

Usage
=====
LLST is a 32-bit software. In order to compile it on a 64-bit OS you have to install the following 32-bit versions of packages (along with cmake and gcc):

```
$ sudo apt-get install ia32-libs g++-multilib libreadline-dev:i386
```

32-bit OS do not require ia32-libs because it is a compatibility package. Then you may clone the repository and build the binaries.

```
~ $ git clone https://github.com/0x7CFE/llst.git
~ $ cd llst

~/llst $ mkdir build && cd build
~/llst/build $ cmake ..
~/llst/build $ make llst
~/llst/build $ ./llst
```

**Note**: Don't forget about make's ```-jN``` parameter. It allows parallel compilation on a multicore system where N represents the number of parallel tasks that make should handle. Typically N is defined as number of cores +1. So, for quad core system ```make -j5``` will be fine.

LLVM
====

By default LLST is built without LLVM support. If you wish to enable it, you should pass -DUSE_LLVM=ON parameter to the cmake:
```
~/llst/build $ rm CMakeCache.txt
~/llst/build $ cmake -DUSE_LLVM=ON ..
~/llst/build $ make
```

You should have LLVM 3.1 installed and llvm-config or llvm-config-3.1 be accessible from your environment.

License
=======

Current version is provided under the terms of GNU GPL v3. If you wish to use the code in your project and this license does not suit you, feel free to contact us.

Bugs
====

Please report any bugs and wishes to the LLST tracker: https://github.com/0x7CFE/llst/issues/
