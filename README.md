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
In order to compile LLST you have to install the following packages (along with cmake and gcc):

```
$ sudo apt-get install ia32-libs g++-multilib libreadline-dev:i386
```

Then you may clone the repository and build the binaries.

```
~ $ git clone https://github.com/0x7CFE/llst.git
~ $ cd llst

~/llst $ mkdir build & cd build
~/llst/build $ cmake ..
~/llst/build $ make
~/llst/build $ ./llst
```

If you now wish to build LLST with LLVM support, you should pass -DLLVM=ON parameter to the cmake:
```
~/llst/build $ rm CMakeCache.txt
~/llst/build $ cmake -DLLVM=ON ..
~/llst/build $ make
```

You should have LLVM installed and llvm-config be accessible from your environment. You may override the LLVM version by passing -DLLVM_VERSION=x.y parameter to cmake (where x.y should be replaced with actual version to use).

License
=======

Current version is provided under the terms of GNU GPL v3. If you wish to use the code in your project and this license does not suit you, feel free to contact us.

Bugs
====

Please report any bugs and wishes to the LLST tracker: https://github.com/0x7CFE/llst/issues/
