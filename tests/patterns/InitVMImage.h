#ifndef LLST_PATTERN_INIT_VM_IMAGE_INCLUDED
#define LLST_PATTERN_INIT_VM_IMAGE_INCLUDED

#include <gtest/gtest.h>
#include "../helpers/VMImage.h"

class P_InitVM_Image : public ::testing::TestWithParam<std::string /*image name*/>
{
protected:
    H_VMImage* m_image;
public:
    virtual ~P_InitVM_Image() {}

    virtual void SetUp()
    {
        bool is_parameterized_test = ::testing::UnitTest::GetInstance()->current_test_info()->value_param();
        if (!is_parameterized_test) {
            // Use TEST_P instead of TEST_F !
            abort();
        }
        ParamType image_name = GetParam();
        m_image = new H_VMImage(image_name);
    }
    virtual void TearDown() {
        delete m_image;
    }
};

#endif
