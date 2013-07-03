#ifndef LLST_PRIMITIVES_H_INCLUDED
#define LLST_PRIMITIVES_H_INCLUDED

#include <types.h>

TObject* callPrimitive(uint8_t opcode, TObjectArray* arguments, bool& primitiveFailed);
TObject* callSmallIntPrimitive(uint8_t opcode, int32_t leftOperand, int32_t rightOperand, bool& primitiveFailed);
TObject* callIOPrimitive(uint8_t opcode, TObjectArray& args, bool& primitiveFailed);

#endif