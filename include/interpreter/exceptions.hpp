#pragma once

namespace Interpreter
{

class exception {
public:
    exception() throw() {}
    virtual ~exception() throw() {}
    virtual const char* what() const throw() = 0;
};
class halt_execution : exception {
public:
    halt_execution() {}
    virtual const char* what() const throw() { return "You must stop execution of the current Process because of exceptions"; }
};
class out_of_memory : exception {
public:
    out_of_memory() {}
    virtual const char* what() const throw() { return "Memory manager failed to allocate memory"; }
};

}
