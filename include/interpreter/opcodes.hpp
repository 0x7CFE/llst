#pragma once

#include <stdint.h>

namespace Interpreter
{

class Runtime;

class UsualOpcode {
public:
    virtual void execute(Runtime& runtime, const uint8_t arg, const uint16_t extra) = 0;
    virtual ~UsualOpcode() {}
};

class SpecialOpcode {
public:
    virtual void execute(Runtime& runtime, const uint16_t extra) = 0;
    virtual ~SpecialOpcode() {}
};

class PrimitiveOpcode {
public:
    virtual void execute(Runtime& runtime, const uint8_t arg) = 0;
    virtual ~PrimitiveOpcode() {}
};

} // namespace
