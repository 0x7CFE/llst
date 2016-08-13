#include <gtest/gtest.h>
#include "patterns/InitVMImage.h"
#include <inference.h>

class P_Inference : public P_InitVM_Image
{
public:
    type::TypeSystem* m_type_system;

    P_Inference() : P_InitVM_Image(), m_type_system(0) {}
    virtual ~P_Inference() {}

    virtual void SetUp()
    {
        P_InitVM_Image::SetUp();
        m_type_system = new type::TypeSystem(*m_vm);
    }
    virtual void TearDown() {
        P_InitVM_Image::TearDown();
        delete m_type_system;
    }

    type::InferContext* inferMessage(
        TClass* const objectClass,
        const std::string& methodName,
        const type::Type& args,
        type::TContextStack* parent,
        bool sendToSuper = false
    ) {
        EXPECT_NE(static_cast<TClass*>(0), objectClass) << "Class is null";
        TMethod* const method = objectClass->methods->find<TMethod>(methodName.c_str());
        EXPECT_NE(static_cast<TMethod*>(0), method) << "Method is null";

        TSymbol* const selector = method->name;
        return m_type_system->inferMessage(selector, args, parent, sendToSuper);
    }

    type::InferContext* inferMessage(
        const std::string& className,
        const std::string& methodName,
        const type::Type& args,
        type::TContextStack* parent,
        bool sendToSuper = false
    ) {
        TClass* const objectClass = m_image->getGlobal<TClass>(className.c_str());
        return this->inferMessage(objectClass, methodName, args, parent, sendToSuper);
    }

};

INSTANTIATE_TEST_CASE_P(_, P_Inference, ::testing::Values(std::string("Inference")) );

template<typename T, size_t N>
std::vector<T> convert_array_to_vector(const T (&source_array)[N]) {
    return std::vector<T>(source_array, source_array+N);
}

TEST_P(P_Inference, new)
{
    std::string types_array[] = {"Array", "List", "True", "False", "Dictionary"};
    std::vector<std::string> types = convert_array_to_vector<>(types_array);

    for(std::vector<std::string>::const_iterator it = types.begin(); it != types.end(); ++it) {
        const std::string& type = *it;
        SCOPED_TRACE(type);
        TClass* const klass = m_image->getGlobal<TClass>(type.c_str());
        ASSERT_TRUE(klass != 0) << "could not find class for " << type;

        TClass* const metaClass = klass->getClass();
        type::Type args(globals.arrayClass, type::Type::tkArray);
        args.pushSubType(type::Type(klass, type::Type::tkLiteral));

        type::InferContext* const inferContext = this->inferMessage(metaClass, "new", args, 0, true);
        ASSERT_EQ( type::Type(klass), inferContext->getReturnType() );
    }
}

TEST_P(P_Inference, Object__isNil)
{
    {
        SCOPED_TRACE("nil");
        type::Type args(globals.arrayClass, type::Type::tkArray);
        args.pushSubType(type::Type(globals.nilObject));

        type::InferContext* const inferContext = this->inferMessage("Object", "isNil", args, 0);
        ASSERT_EQ( type::Type(globals.trueObject), inferContext->getReturnType() );
    }
    {
        SCOPED_TRACE("not nil");
        type::Type args(globals.arrayClass, type::Type::tkArray);
        args.pushSubType(type::Type(globals.trueObject));

        type::InferContext* const inferContext = this->inferMessage("Object", "isNil", args, 0);
        ASSERT_EQ( type::Type(globals.falseObject), inferContext->getReturnType() );
    }
}

TEST_P(P_Inference, Object__notNil)
{
    {
        SCOPED_TRACE("nil");
        type::Type args(globals.arrayClass, type::Type::tkArray);
        args.pushSubType(type::Type(globals.nilObject));

        type::InferContext* const inferContext = this->inferMessage("Object", "notNil", args, 0);
        ASSERT_EQ( type::Type(globals.falseObject), inferContext->getReturnType() );
    }
    {
        SCOPED_TRACE("not nil");
        type::Type args(globals.arrayClass, type::Type::tkArray);
        args.pushSubType(type::Type(globals.trueObject));

        type::InferContext* const inferContext = this->inferMessage("Object", "notNil", args, 0);
        ASSERT_EQ( type::Type(globals.trueObject), inferContext->getReturnType() );
    }
}

TEST_P(P_Inference, Object__class)
{
    TClass* classes_array[] = {globals.smallIntClass, globals.stringClass, globals.arrayClass, globals.blockClass};
    std::vector<TClass*> classes = convert_array_to_vector<>(classes_array);

    for(std::vector<TClass*>::const_iterator it = classes.begin(); it != classes.end(); ++it) {
        TClass* klass = *it;
        SCOPED_TRACE(klass->name->toString());
        type::Type args(globals.arrayClass, type::Type::tkArray);
        args.pushSubType(type::Type(klass));

        type::InferContext* const inferContext = this->inferMessage("Object", "class", args, 0);
        ASSERT_EQ( type::Type(klass, type::Type::tkLiteral), inferContext->getReturnType() ) ;
    }
}

TEST_P(P_Inference, Object__isMemberOf)
{
    {
        SCOPED_TRACE("42 isMemberOf: SmallInt");
        type::Type args(globals.arrayClass, type::Type::tkArray);
        args.pushSubType(type::Type(TInteger(42)));
        args.pushSubType(type::Type(globals.smallIntClass, type::Type::tkLiteral));

        type::InferContext* const inferContext = this->inferMessage("Object", "isMemberOf:", args, 0);
        ASSERT_EQ( type::Type(globals.trueObject), inferContext->getReturnType() );
    }
    {
        SCOPED_TRACE("42 isMemberOf: (SmallInt)");
        type::Type args(globals.arrayClass, type::Type::tkArray);
        args.pushSubType(type::Type(TInteger(42)));
        args.pushSubType(type::Type(globals.smallIntClass));

        type::InferContext* const inferContext = this->inferMessage("Object", "isMemberOf:", args, 0);
        ASSERT_EQ( type::Type(m_image->getGlobal<TClass>("Boolean")), inferContext->getReturnType() );
    }
}

TEST_P(P_Inference, Object__isKindOf)
{
    {
        SCOPED_TRACE("42 isKindOf: SmallInt");
        type::Type args(globals.arrayClass, type::Type::tkArray);
        args.pushSubType(type::Type(TInteger(42)));
        args.pushSubType(type::Type(globals.smallIntClass, type::Type::tkLiteral));

        type::InferContext* const inferContext = this->inferMessage("Object", "isKindOf:", args, 0);
        ASSERT_EQ( type::Type(globals.trueObject), inferContext->getReturnType() );
    }
    {
        SCOPED_TRACE("42 isKindOf: Number");
        type::Type args(globals.arrayClass, type::Type::tkArray);
        args.pushSubType(type::Type(TInteger(42)));
        args.pushSubType(type::Type(globals.smallIntClass));

        type::InferContext* const inferContext = this->inferMessage("Object", "isKindOf:", args, 0);
        ASSERT_EQ( type::Type(m_image->getGlobal<TClass>("Boolean")), inferContext->getReturnType().fold() );
    }
}

TEST_P(P_Inference, Object__respondsTo)
{
    {
        SCOPED_TRACE("42 respondsTo: #<");
        type::Type args(globals.arrayClass, type::Type::tkArray);
        args.pushSubType(type::Type(TInteger(42)));
        args.pushSubType(type::Type(globals.binaryMessages[0]));

        type::InferContext* const inferContext = this->inferMessage("Object", "respondsTo:", args, 0);
        ASSERT_EQ( type::Type(type::Type::tkPolytype), inferContext->getReturnType() ); // FIXME
    }
}

TEST_P(P_Inference, Collection__includes)
{
    {
        SCOPED_TRACE("Array new includes: 42");
        type::Type args(globals.arrayClass, type::Type::tkArray);
        args.pushSubType(type::Type(globals.arrayClass));
        args.pushSubType(type::Type(TInteger(42)));

        type::InferContext* const inferContext = this->inferMessage("Collection", "includes:", args, 0);
        ASSERT_EQ( type::Type(m_image->getGlobal<TClass>("Boolean")), inferContext->getReturnType().fold() );
    }
}

TEST_P(P_Inference, OrderedArray)
{
    {
        SCOPED_TRACE("OrderedArray new location: 42");
        type::Type args(globals.arrayClass, type::Type::tkArray);
        args.pushSubType(type::Type(m_image->getGlobal<TClass>("OrderedArray")));
        args.pushSubType(type::Type(TInteger(42)));

        type::InferContext* const inferContext = this->inferMessage("OrderedArray", "location:", args, 0);
        ASSERT_EQ( type::Type(globals.smallIntClass), inferContext->getReturnType().fold() );
    }
}

TEST_P(P_Inference, True)
{
    {
        SCOPED_TRACE("True>>not");
        type::Type args(globals.arrayClass, type::Type::tkArray);
        args.pushSubType(type::Type(globals.trueObject));

        type::InferContext* const inferContext = this->inferMessage("True", "not", args, 0);
        ASSERT_EQ( type::Type(globals.falseObject), inferContext->getReturnType() );
    }
    {
        SCOPED_TRACE("True>>and:");
        type::Type args(globals.arrayClass, type::Type::tkArray);
        args.pushSubType(type::Type(globals.trueObject));
        args.pushSubType(type::Type(globals.blockClass));

        type::InferContext* const inferContext = this->inferMessage("True", "and:", args, 0);
        ASSERT_EQ( type::Type(m_image->getGlobal<TClass>("Boolean")), inferContext->getReturnType().fold() );
    }
    {
        SCOPED_TRACE("True>>or:");
        type::Type args(globals.arrayClass, type::Type::tkArray);
        args.pushSubType(type::Type(globals.trueObject));
        args.pushSubType(type::Type(globals.blockClass));

        type::InferContext* const inferContext = this->inferMessage("True", "or:", args, 0);
        ASSERT_EQ( type::Type(globals.trueObject), inferContext->getReturnType() );
    }
}

TEST_P(P_Inference, False)
{
    {
        SCOPED_TRACE("False>>not");
        type::Type args(globals.arrayClass, type::Type::tkArray);
        args.pushSubType(type::Type(globals.falseObject));

        type::InferContext* const inferContext = this->inferMessage("False", "not", args, 0);
        ASSERT_EQ( type::Type(globals.trueObject), inferContext->getReturnType() );
    }
    {
        SCOPED_TRACE("False>>and:");
        type::Type args(globals.arrayClass, type::Type::tkArray);
        args.pushSubType(type::Type(globals.falseObject));
        args.pushSubType(type::Type(globals.blockClass));

        type::InferContext* const inferContext = this->inferMessage("False", "and:", args, 0);
        ASSERT_EQ( type::Type(globals.falseObject), inferContext->getReturnType() );
    }
    {
        SCOPED_TRACE("False>>or:");
        type::Type args(globals.arrayClass, type::Type::tkArray);
        args.pushSubType(type::Type(globals.falseObject));
        args.pushSubType(type::Type(globals.blockClass));

        type::InferContext* const inferContext = this->inferMessage("False", "or:", args, 0);
        ASSERT_EQ( type::Type(m_image->getGlobal<TClass>("Boolean")), inferContext->getReturnType().fold() );
    }
}

TEST_P(P_Inference, Boolean)
{
    {
        SCOPED_TRACE("not");
        {
            SCOPED_TRACE("False");
            type::Type args(globals.arrayClass, type::Type::tkArray);
            args.pushSubType(type::Type(globals.falseObject));

            type::InferContext* const inferContext = this->inferMessage("Boolean", "not", args, 0, true);
            ASSERT_EQ( type::Type(globals.trueObject), inferContext->getReturnType() );
        }
        {
            SCOPED_TRACE("False");
            type::Type args(globals.arrayClass, type::Type::tkArray);
            args.pushSubType(type::Type(globals.trueObject));

            type::InferContext* const inferContext = this->inferMessage("Boolean", "not", args, 0, true);
            ASSERT_EQ( type::Type(globals.falseObject), inferContext->getReturnType() );
        }
    }
    {
        SCOPED_TRACE("and:");
        {
            SCOPED_TRACE("False + block");
            type::Type args(globals.arrayClass, type::Type::tkArray);
            args.pushSubType(type::Type(globals.falseObject));
            args.pushSubType(type::Type(globals.blockClass));

            type::InferContext* const inferContext = this->inferMessage("Boolean", "and:", args, 0, true);
            ASSERT_EQ( type::Type(globals.falseObject), inferContext->getReturnType() );
        }
        {
            SCOPED_TRACE("True + block");
            type::Type args(globals.arrayClass, type::Type::tkArray);
            args.pushSubType(type::Type(globals.trueObject));
            args.pushSubType(type::Type(globals.blockClass));

            type::InferContext* const inferContext = this->inferMessage("Boolean", "and:", args, 0, true);
            ASSERT_EQ( type::Type(m_image->getGlobal<TClass>("Boolean")), inferContext->getReturnType().fold() );
        }
    }
    {
        SCOPED_TRACE("or:");
        {
            SCOPED_TRACE("False + block");
            type::Type args(globals.arrayClass, type::Type::tkArray);
            args.pushSubType(type::Type(globals.falseObject));
            args.pushSubType(type::Type(globals.blockClass));

            type::InferContext* const inferContext = this->inferMessage("Boolean", "or:", args, 0, true);
            ASSERT_EQ( type::Type(m_image->getGlobal<TClass>("Boolean")), inferContext->getReturnType().fold() );
        }
        {
            SCOPED_TRACE("True + block");
            type::Type args(globals.arrayClass, type::Type::tkArray);
            args.pushSubType(type::Type(globals.trueObject));
            args.pushSubType(type::Type(globals.blockClass));

            type::InferContext* const inferContext = this->inferMessage("Boolean", "or:", args, 0, true);
            ASSERT_EQ( type::Type(globals.trueObject), inferContext->getReturnType() );
        }
    }
}

TEST_P(P_Inference, SmallInt)
{
    {
        SCOPED_TRACE("SmallInt>>asSmallInt");
        type::Type args(globals.arrayClass, type::Type::tkArray);
        args.pushSubType(type::Type(TInteger(42)));

        type::InferContext* const inferContext = this->inferMessage("SmallInt", "asSmallInt", args, 0);
        ASSERT_EQ( type::Type(TInteger(42)), inferContext->getReturnType() );
    }
    {
        SCOPED_TRACE("SmallInt>>+ SmallInt");
        type::Type args(globals.arrayClass, type::Type::tkArray);
        args.pushSubType(type::Type(TInteger(40)));
        args.pushSubType(type::Type(TInteger(2)));

        type::InferContext* const inferContext = this->inferMessage("SmallInt", "+", args, 0);
        ASSERT_EQ( type::Type(TInteger(42)), inferContext->getReturnType() );
    }
}

TEST_P(P_Inference, Number)
{
    {
        SCOPED_TRACE("Number::new");
        type::Type args(globals.arrayClass, type::Type::tkArray);
        TClass* const metaNumberClass = m_image->getGlobal<TClass>("Number")->getClass();
        args.pushSubType(type::Type(metaNumberClass));

        type::InferContext* const inferContext = this->inferMessage(metaNumberClass, "new", args, 0);
        ASSERT_EQ( type::Type(TInteger(0)), inferContext->getReturnType() );
    }
    {
        SCOPED_TRACE("Number>>factorial");
        type::Type args(globals.arrayClass, type::Type::tkArray);
        args.pushSubType(type::Type(TInteger(4)));

        type::InferContext* const inferContext = this->inferMessage("Number", "factorial", args, 0);
        ASSERT_EQ( type::Type(TInteger(24)), inferContext->getReturnType() );
    }
    {
        SCOPED_TRACE("Number>>negative");
        {
            SCOPED_TRACE("-SmallInt");
            type::Type args(globals.arrayClass, type::Type::tkArray);
            args.pushSubType(type::Type(TInteger(-1)));

            type::InferContext* const inferContext = this->inferMessage("Number", "negative", args, 0);
            ASSERT_EQ( type::Type(globals.trueObject), inferContext->getReturnType() );
        }
        {
            SCOPED_TRACE("+SmallInt");
            type::Type args(globals.arrayClass, type::Type::tkArray);
            args.pushSubType(type::Type(TInteger(1)));

            type::InferContext* const inferContext = this->inferMessage("Number", "negative", args, 0);
            ASSERT_EQ( type::Type(globals.falseObject), inferContext->getReturnType() );
        }
    }
    {
        SCOPED_TRACE("Number>>negated");
        {
            SCOPED_TRACE("-SmallInt");
            type::Type args(globals.arrayClass, type::Type::tkArray);
            args.pushSubType(type::Type(TInteger(-1)));

            type::InferContext* const inferContext = this->inferMessage("Number", "negated", args, 0);
            ASSERT_EQ( type::Type(TInteger(1)), inferContext->getReturnType() );
        }
        {
            SCOPED_TRACE("+SmallInt");
            type::Type args(globals.arrayClass, type::Type::tkArray);
            args.pushSubType(type::Type(TInteger(1)));

            type::InferContext* const inferContext = this->inferMessage("Number", "negated", args, 0);
            ASSERT_EQ( type::Type(TInteger(-1)), inferContext->getReturnType() );
        }
    }
    {
        SCOPED_TRACE("Number>>absolute");
        {
            SCOPED_TRACE("-SmallInt");
            type::Type args(globals.arrayClass, type::Type::tkArray);
            args.pushSubType(type::Type(TInteger(-1)));

            type::InferContext* const inferContext = this->inferMessage("Number", "absolute", args, 0);
            ASSERT_EQ( type::Type(TInteger(1)), inferContext->getReturnType() );
        }
        {
            SCOPED_TRACE("+SmallInt");
            type::Type args(globals.arrayClass, type::Type::tkArray);
            args.pushSubType(type::Type(TInteger(1)));

            type::InferContext* const inferContext = this->inferMessage("Number", "absolute", args, 0);
            ASSERT_EQ( type::Type(TInteger(1)), inferContext->getReturnType() );
        }
    }
    {
        SCOPED_TRACE("Number>>to:do:");
        type::Type args(globals.arrayClass, type::Type::tkArray);
        args.pushSubType(type::Type(TInteger(1)));
        args.pushSubType(type::Type(TInteger(100)));
        args.pushSubType(type::Type(globals.blockClass));

        type::InferContext* const inferContext = this->inferMessage("Number", "to:do:", args, 0);
        ASSERT_EQ( type::Type(TInteger(1)), inferContext->getReturnType() );
    }
}

TEST_P(P_Inference, String)
{
    {
        SCOPED_TRACE("String>>words");
        type::Type args(globals.arrayClass, type::Type::tkArray);
        args.pushSubType(type::Type(globals.stringClass));

        type::InferContext* const inferContext = this->inferMessage("String", "words", args, 0);
        ASSERT_EQ( type::Type(m_image->getGlobal<TClass>("List")), inferContext->getReturnType() );
    }
}

TEST_P(P_Inference, Array)
{
    {
        SCOPED_TRACE("Array>>sort:");
        type::Type args(globals.arrayClass, type::Type::tkArray);
        args.pushSubType(type::Type(globals.arrayClass));
        args.pushSubType(type::Type(globals.blockClass));

        type::InferContext* const inferContext = this->inferMessage("Array", "sort:", args, 0);
        ASSERT_EQ( type::Type(globals.arrayClass), inferContext->getReturnType() );
    }
    {
        SCOPED_TRACE("Array>>size");
        type::Type args(globals.arrayClass, type::Type::tkArray);
        args.pushSubType(type::Type(globals.arrayClass));

        type::InferContext* const inferContext = this->inferMessage("Array", "size", args, 0);
        ASSERT_EQ( type::Type(globals.smallIntClass), inferContext->getReturnType() );
    }
}

TEST_P(P_Inference, Char)
{
    TClass* const charClass = m_image->getGlobal<TClass>("Char");
    ASSERT_TRUE(charClass != 0) << "could not find class for Char";
    TClass* const metaCharClass = charClass->getClass();

    {
        SCOPED_TRACE("Char::basicNew:");

        type::Type argsBasicNew(globals.arrayClass, type::Type::tkArray);
        argsBasicNew.pushSubType(type::Type(charClass, type::Type::tkLiteral));
        argsBasicNew.pushSubType(type::Type(TInteger(33)));

        type::InferContext* const inferBasicNewContext = this->inferMessage(metaCharClass, "basicNew:", argsBasicNew, 0);
        ASSERT_EQ( type::Type(charClass), inferBasicNewContext->getReturnType() );

        /*{
            SCOPED_TRACE("Char>>value");
            type::Type args(globals.arrayClass, type::Type::tkArray);
            args.pushSubType(type::Type(inferBasicNewContext->getReturnType()));

            type::InferContext* const inferContext = this->inferMessage(charClass, "value", args, 0);
            ASSERT_EQ( type::Type(globals.smallIntClass), inferContext->getReturnType() );
        }*/
    }
    {
        SCOPED_TRACE("Char::new:");

        type::Type args(globals.arrayClass, type::Type::tkArray);
        args.pushSubType(type::Type(charClass, type::Type::tkLiteral));
        args.pushSubType(type::Type(TInteger(42)));

        type::InferContext* const inferBasicNewContext = this->inferMessage(metaCharClass, "new:", args, 0);
        ASSERT_EQ( type::Type(type::Type::tkPolytype), inferBasicNewContext->getReturnType() );
    }
}

TEST_P(P_Inference, DISABLED_Includes)
{
    {
        SCOPED_TRACE("Dictionary new includes: #asd");
        type::Type args(globals.arrayClass, type::Type::tkArray);
        args.pushSubType(type::Type(m_image->getGlobal<TClass>("Dictionary")));
        args.pushSubType(type::Type(m_image->getGlobal<TClass>("Symbol")));

        type::InferContext* const inferContext = this->inferMessage("Collection", "includes:", args, 0);
        ASSERT_EQ( type::Type(m_image->getGlobal<TClass>("Boolean")), inferContext->getReturnType().fold() );
    }
    {
        SCOPED_TRACE("OrderedArray new includes: 42");
        type::Type args(globals.arrayClass, type::Type::tkArray);
        args.pushSubType(type::Type(m_image->getGlobal<TClass>("OrderedArray")));
        args.pushSubType(type::Type(TInteger(42)));

        type::InferContext* const inferContext = this->inferMessage("Collection", "includes:", args, 0);
        ASSERT_EQ( type::Type(m_image->getGlobal<TClass>("Boolean")), inferContext->getReturnType().fold() );
    }
}
