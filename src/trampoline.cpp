#include <jit.h>

extern "C" {

void methodTrampoline(JITRuntime::TMethodFunction function, TContext* context, TReturnValue& result)
{
    result = function(context);
}

void blockTrampoline(JITRuntime::TBlockFunction function, TBlock* block, TReturnValue& result)
{
    result = function(block);
}

TReturnValue sendMessage(TContext* callingContext, TSymbol* message, TObjectArray* arguments, TClass* receiverClass, uint32_t callSiteIndex)
{
    TReturnValue result;
    JITRuntime::Instance()->sendMessage(callingContext, message, arguments, receiverClass, callSiteIndex, result);
    return result;
}

TReturnValue invokeBlock(TBlock* block, TContext* callingContext)
{
    TReturnValue result;
    JITRuntime::Instance()->invokeBlock(block, callingContext, result, false);
    return result;
}

}
