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
        SCOPED_TRACE("2*3");
        args->putField(0, TInteger(2) );
        args->putField(1, TInteger(3) );
        bool primitiveFailed;
        TInteger result = callPrimitive(primitive::smallIntMul, args, primitiveFailed);
        ASSERT_FALSE(primitiveFailed);
        ASSERT_EQ(6, result.getValue());
    }
    m_image->deleteObject(args);
}
