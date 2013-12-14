#ifndef API_H_INCLUDED
#define API_H_INCLUDED

#include <types.h>

typedef TObject* (TObject::*PNativeMethod)();
typedef TObject* (TObject::*PNativeMethod1)(TObject* arg1);
typedef TObject* (TObject::*PNativeMethod2)(TObject* arg1, TObject* arg2);
typedef TObject* (TObject::*PNativeMethodA)(TObjectArray* args);

class TNativeMethodBase {
public:
    enum TMethodType {
        mtNoArg = 0, // method do not accept arguments
        mtOneArg,    // method accepts one TObject* argument
        mtTwoArg,    // method accepts two TObject* arguments
        mtArgArray   // method accepts TObjectArray* with arguments
    };

    TMethodType getType() { return m_type; }
    TNativeMethodBase(TMethodType type) : m_type(type) { }
protected:
    TMethodType m_type;

    TNativeMethodBase(const TNativeMethodBase&);
    TNativeMethodBase& operator=(const TNativeMethodBase&);
};

template<typename T, TNativeMethodBase::TMethodType type>
class TNativeMethodPointer : public TNativeMethodBase {
private:
    T m_method;

public:
    template<typename M> TNativeMethodPointer(M method) : TNativeMethodBase(type) {
        m_method = static_cast<T>(method);
    }

    T get() { return m_method; }
};

typedef TNativeMethodPointer<PNativeMethod,  TNativeMethodBase::mtNoArg>    TNativeMethod;
typedef TNativeMethodPointer<PNativeMethod1, TNativeMethodBase::mtOneArg>   TNativeMethod1;
typedef TNativeMethodPointer<PNativeMethod2, TNativeMethodBase::mtTwoArg>   TNativeMethod2;
typedef TNativeMethodPointer<PNativeMethodA, TNativeMethodBase::mtArgArray> TNativeMethodA;

// This structure is used when forming method binding tables for native methods
// Selector is defined as char* to allow convinient brace initializer lists.
struct TNativeMethodInfo {
    const char* selector;
    TNativeMethodBase* method;
};

// Outer template with method pointer's type as parameter
template <typename T>
struct WrapperOuter;

// Partial specialization of outer template for empty parameter case
template <typename C, typename R>
struct WrapperOuter<R* (C::*)()>
{
    // Inner template with method pointer's type as parameter
    // Method pointers could not be template parameters because
    // their values are always known at compile time
    template <R* (C::*PM)()>
    struct WrapperInner
    {
        // Producing template function which sets up wrapper
        static TNativeMethodBase* createNativeMethod()
        {
            return new TNativeMethodPointer<PNativeMethod, TNativeMethodBase::mtNoArg>(
                &TObject::methodTrampoline<C, R, PM>
            );
        }
    };
};

// Partial specialization of outer template for 1 parameter case
template <typename C, typename R, typename A1>
struct WrapperOuter<R* (C::*)(A1*)>
{
    template <R* (C::*PM)(A1*)>
    struct WrapperInner
    {
        static TNativeMethodBase* createNativeMethod()
        {
            return new TNativeMethodPointer<PNativeMethod1, TNativeMethodBase::mtOneArg>(
                &TObject::methodTrampoline1<C, R, A1, PM>
            );
        }
    };
};

// Partial specialization of outer template for 2 parameters case
template <typename C, typename R, typename A1, typename A2>
struct WrapperOuter<R* (C::*)(A1*, A2*)>
{
    template <R* (C::*PM)(A1*, A2*)>
    struct WrapperInner
    {
        static TNativeMethodBase* createNativeMethod()
        {
            return new TNativeMethodPointer<PNativeMethod2, TNativeMethodBase::mtTwoArg>(
                &TObject::methodTrampoline2<C, R, A1, A2, PM>
            );
        }
    };
};

// Partial specialization of outer template for arguments array case
template <typename C, typename R>
struct WrapperOuter<R* (C::*)(TObjectArray*)>
{
    template <R* (C::*PM)(TObjectArray*)>
    struct WrapperInner
    {
        static TNativeMethodBase* createNativeMethod()
        {
            return new TNativeMethodPointer<PNativeMethodA, TNativeMethodBase::mtArgArray>(
                &TObject::methodTrampolineA<C, R, PM>
            );
        }
    };
};

// Producing function which performs type deduction and returns wrapped method pointer
template <typename PMType, PMType PM>
TNativeMethodBase* createNativeMethod()
{
    return WrapperOuter<PMType>::template WrapperInner<PM>::createNativeMethod();
}

// Helper macro which should be used when registering method in native method tables
#define NATIVE_METHOD(pm) createNativeMethod<decltype(pm), (pm)>()

#endif
