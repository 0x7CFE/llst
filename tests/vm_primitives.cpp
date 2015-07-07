#include <gtest/gtest.h>
#include "helpers/VMImage.h"
#include "patterns/InitVMImage.h"
#include <primitives.h>
#include <opcodes.h>

INSTANTIATE_TEST_CASE_P(_, P_InitVM_Image, ::testing::Values(std::string("VMPrimitives")) );

TEST_P(P_InitVM_Image, smallint)
{
    TObjectArray* args = m_image->newArray(2);
    {
        SCOPED_TRACE("1+2");
        args->putField(0, TInteger(1) );
        args->putField(1, TInteger(2) );
        bool primitiveFailed;
        TInteger result = callPrimitive(primitive::smallIntAdd, args, primitiveFailed);
        ASSERT_FALSE(primitiveFailed);
        ASSERT_EQ(3, result.getValue());
    }
    {
        SCOPED_TRACE("1-2");
        args->putField(0, TInteger(1) );
        args->putField(1, TInteger(2) );
        bool primitiveFailed;
        TInteger result = callPrimitive(primitive::smallIntSub, args, primitiveFailed);
        ASSERT_FALSE(primitiveFailed);
        ASSERT_EQ(-1, result.getValue());
    }
    {
        SCOPED_TRACE("1/0");
        args->putField(0, TInteger(1) );
        args->putField(1, TInteger(0) );
        bool primitiveFailed;
        callPrimitive(primitive::smallIntDiv, args, primitiveFailed);
        ASSERT_TRUE(primitiveFailed);
    }
    {
        SCOPED_TRACE("8/4");
        args->putField(0, TInteger(8) );
        args->putField(1, TInteger(4) );
        bool primitiveFailed;
        TInteger result = callPrimitive(primitive::smallIntDiv, args, primitiveFailed);
        ASSERT_FALSE(primitiveFailed);
        ASSERT_EQ(2, result.getValue());
    }
    {
        SCOPED_TRACE("1%0");
        args->putField(0, TInteger(1) );
        args->putField(1, TInteger(0) );
        bool primitiveFailed;
        callPrimitive(primitive::smallIntMod, args, primitiveFailed);
        ASSERT_TRUE(primitiveFailed);
    }
    {
        SCOPED_TRACE("3%2");
        args->putField(0, TInteger(3) );
        args->putField(1, TInteger(2) );
        bool primitiveFailed;
        TInteger result = callPrimitive(primitive::smallIntMod, args, primitiveFailed);
        ASSERT_FALSE(primitiveFailed);
        ASSERT_EQ(1, result.getValue());
    }
    {
        SCOPED_TRACE("2*3");
        args->putField(0, TInteger(2) );
        args->putField(1, TInteger(3) );
        bool primitiveFailed;
        TInteger result = callPrimitive(primitive::smallIntMul, args, primitiveFailed);
        ASSERT_FALSE(primitiveFailed);
        ASSERT_EQ(6, result.getValue());
    }
    {
        SCOPED_TRACE("1<2");
        args->putField(0, TInteger(1) );
        args->putField(1, TInteger(2) );
        bool primitiveFailed;
        TObject* result = callPrimitive(primitive::smallIntLess, args, primitiveFailed);
        ASSERT_FALSE(primitiveFailed);
        ASSERT_EQ(globals.trueObject, result);
    }
    {
        SCOPED_TRACE("1<1");
        args->putField(0, TInteger(1) );
        args->putField(1, TInteger(1) );
        bool primitiveFailed;
        TObject* result = callPrimitive(primitive::smallIntLess, args, primitiveFailed);
        ASSERT_FALSE(primitiveFailed);
        ASSERT_EQ(globals.falseObject, result);
    }
    {
        SCOPED_TRACE("3=3");
        args->putField(0, TInteger(3) );
        args->putField(1, TInteger(3) );
        bool primitiveFailed;
        TObject* result = callPrimitive(primitive::smallIntEqual, args, primitiveFailed);
        ASSERT_FALSE(primitiveFailed);
        ASSERT_EQ(globals.trueObject, result);
    }
    {
        SCOPED_TRACE("0=42");
        args->putField(0, TInteger(0) );
        args->putField(1, TInteger(42) );
        bool primitiveFailed;
        TObject* result = callPrimitive(primitive::smallIntEqual, args, primitiveFailed);
        ASSERT_FALSE(primitiveFailed);
        ASSERT_EQ(globals.falseObject, result);
    }
    {
        SCOPED_TRACE("1|2");
        args->putField(0, TInteger(1) );
        args->putField(1, TInteger(2) );
        bool primitiveFailed;
        TInteger result = callPrimitive(primitive::smallIntBitOr, args, primitiveFailed);
        ASSERT_FALSE(primitiveFailed);
        ASSERT_EQ(3, result.getValue());
    }
    {
        SCOPED_TRACE("14&3");
        args->putField(0, TInteger(14) );
        args->putField(1, TInteger(3) );
        bool primitiveFailed;
        TInteger result = callPrimitive(primitive::smallIntBitAnd, args, primitiveFailed);
        ASSERT_FALSE(primitiveFailed);
        ASSERT_EQ(2, result.getValue());
    }
    {
        SCOPED_TRACE("7>>1");
        args->putField(0, TInteger(7) );
        args->putField(1, TInteger(-1) );
        bool primitiveFailed;
        TInteger result = callPrimitive(primitive::smallIntBitShift, args, primitiveFailed);
        ASSERT_FALSE(primitiveFailed);
        ASSERT_EQ(3, result.getValue());
    }
    {
        SCOPED_TRACE("5<<1");
        args->putField(0, TInteger(5) );
        args->putField(1, TInteger(1) );
        bool primitiveFailed;
        TInteger result = callPrimitive(primitive::smallIntBitShift, args, primitiveFailed);
        ASSERT_FALSE(primitiveFailed);
        ASSERT_EQ(10, result.getValue());
    }
    {
        SCOPED_TRACE("1<<31");
        args->putField(0, TInteger(1) );
        args->putField(1, TInteger(31) );
        bool primitiveFailed;
        callPrimitive(primitive::smallIntBitShift, args, primitiveFailed);
        ASSERT_TRUE(primitiveFailed);
    }
    m_image->deleteObject(args);
}

TEST_P(P_InitVM_Image, stringAt)
{
    {
        SCOPED_TRACE("string replaced with smallint");
        std::string sample = "Hello world";
        TString* str = m_image->newString(sample);
        TObjectArray* args = m_image->newArray(2);
        args->putField(0, TInteger(0) );
        args->putField(1, TInteger(0) );
        bool primitiveFailed;
        callPrimitive(primitive::stringAt, args, primitiveFailed);
        ASSERT_TRUE(primitiveFailed);
        m_image->deleteObject(args);
        m_image->deleteObject(str);
    }
    {
        SCOPED_TRACE("string replaced with array");
        std::string sample = "Hello world";
        TString* str = m_image->newString(sample);
        TObjectArray* args = m_image->newArray(2);
        args->putField(0, m_image->newArray(42) );
        args->putField(1, TInteger(0) );
        bool primitiveFailed;
        callPrimitive(primitive::stringAt, args, primitiveFailed);
        ASSERT_TRUE(primitiveFailed);
        m_image->deleteObject(args->getField(0));
        m_image->deleteObject(args);
        m_image->deleteObject(str);
    }
    {
        SCOPED_TRACE("index is not a smallint");
        std::string sample = "Hello world";
        TString* str = m_image->newString(sample);
        TObjectArray* args = m_image->newArray(2);
        args->putField(0, TInteger(0) );
        args->putField(1, str );
        bool primitiveFailed;
        callPrimitive(primitive::stringAt, args, primitiveFailed);
        ASSERT_TRUE(primitiveFailed);
        m_image->deleteObject(args);
        m_image->deleteObject(str);
    }
    {
        SCOPED_TRACE("inbounds");
        std::string sample = "Hello world";
        TString* str = m_image->newString(sample);
        for (std::size_t i = 0; i < sample.size(); i++)
        {
            SCOPED_TRACE(i);
            TObjectArray* args = m_image->newArray(2);
            args->putField(0, str );
            args->putField(1, TInteger(i+1) );
            bool primitiveFailed;
            TInteger result = callPrimitive(primitive::stringAt, args, primitiveFailed);
            ASSERT_FALSE(primitiveFailed);
            ASSERT_EQ(sample[i], result.getValue());
            m_image->deleteObject(args);
        }
        m_image->deleteObject(str);
    }
    {
        SCOPED_TRACE("out of bounds");
        std::string sample = "Hello world";
        TString* str = m_image->newString(sample);
        TObjectArray* args = m_image->newArray(2);
        args->putField(0, str );
        args->putField(1, TInteger(42) );
        bool primitiveFailed;
        callPrimitive(primitive::stringAt, args, primitiveFailed);
        ASSERT_TRUE(primitiveFailed);
        m_image->deleteObject(args);
        m_image->deleteObject(str);
    }
}

TEST_P(P_InitVM_Image, stringAtPut)
{
    {
        SCOPED_TRACE("string replaced with smallint");
        std::string sample = "Hello world";
        TString* str = m_image->newString(sample);
        TObjectArray* args = m_image->newArray(3);
        args->putField(0, TInteger(0) );
        args->putField(1, TInteger(0) );
        args->putField(2, TInteger(0) );
        bool primitiveFailed;
        callPrimitive(primitive::stringAtPut, args, primitiveFailed);
        ASSERT_TRUE(primitiveFailed);
        m_image->deleteObject(args);
        m_image->deleteObject(str);
    }
    {
        SCOPED_TRACE("string replaced with array");
        std::string sample = "Hello world";
        TString* str = m_image->newString(sample);
        TObjectArray* args = m_image->newArray(3);
        args->putField(0, TInteger(0) );
        args->putField(1, m_image->newArray(42) );
        args->putField(2, TInteger(0) );
        bool primitiveFailed;
        callPrimitive(primitive::stringAtPut, args, primitiveFailed);
        ASSERT_TRUE(primitiveFailed);
        m_image->deleteObject(args->getField(1));
        m_image->deleteObject(args);
        m_image->deleteObject(str);
    }
    {
        SCOPED_TRACE("index is not a smallint");
        std::string sample = "Hello world";
        TString* str = m_image->newString(sample);
        TObjectArray* args = m_image->newArray(3);
        args->putField(0, TInteger(0) );
        args->putField(1, str );
        args->putField(2, str );
        bool primitiveFailed;
        callPrimitive(primitive::stringAtPut, args, primitiveFailed);
        ASSERT_TRUE(primitiveFailed);
        m_image->deleteObject(args);
        m_image->deleteObject(str);
    }
    {
        SCOPED_TRACE("value is not a smallint");
        std::string sample = "Hello world";
        TString* str = m_image->newString(sample);
        TObjectArray* args = m_image->newArray(3);
        args->putField(0, m_image->newArray(42) );
        args->putField(1, str );
        args->putField(2, TInteger(0) );
        bool primitiveFailed;
        callPrimitive(primitive::stringAtPut, args, primitiveFailed);
        ASSERT_TRUE(primitiveFailed);
        m_image->deleteObject(args->getField(0));
        m_image->deleteObject(args);
        m_image->deleteObject(str);
    }
    {
        SCOPED_TRACE("out of bounds");
        std::string sample = "Hello world";
        TString* str = m_image->newString(sample);
        TObjectArray* args = m_image->newArray(3);
        args->putField(0, str );
        args->putField(1, TInteger(42) );
        args->putField(2, TInteger(0) );
        bool primitiveFailed;
        callPrimitive(primitive::stringAtPut, args, primitiveFailed);
        ASSERT_TRUE(primitiveFailed);
        m_image->deleteObject(args);
        m_image->deleteObject(str);
    }
    {
        SCOPED_TRACE("inbounds");
        std::string sample = "Hello world ";
        TString* str = m_image->newString(sample);
        TObjectArray* args = m_image->newArray(3);
        args->putField(0, TInteger('!') );
        args->putField(1, str );
        args->putField(2, TInteger(12) );
        bool primitiveFailed;
        callPrimitive(primitive::stringAtPut, args, primitiveFailed);
        ASSERT_FALSE(primitiveFailed);
        ASSERT_EQ("Hello world!", std::string((const char*)str->getBytes(), str->getSize()));
        m_image->deleteObject(args);
        m_image->deleteObject(str);
    }
}
