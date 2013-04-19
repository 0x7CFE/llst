#ifndef LLST_PRIMITIVES_H_INCLUDED
#define LLST_PRIMITIVES_H_INCLUDED

#include <types.h>

TObject* callPrimitive(uint8_t opcode, TObjectArray* arguments, bool& primitiveFailed);

#endif