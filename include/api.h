#ifndef API_H_INCLUDED
#define API_H_INCLUDED

#include <types.h>

typedef TObject* (TObject::*PNativeMethod)();
typedef TObject* (TObject::*PNativeMethod1)(TObject* arg1);
typedef TObject* (TObject::*PNativeMethod2)(TObject* arg1, TObject* arg2);
typedef TObject* (TObject::*PNativeMethod3)(TObject* arg1, TObject* arg2, TObject* arg3);
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

struct TNativeMethodInfo {
    const char* selector;
    TNativeMethodBase* method;
};

#endif
