#ifndef LLST_PATTERN_INIT_VM_IMAGE_INCLUDED
#define LLST_PATTERN_INIT_VM_IMAGE_INCLUDED

#include <gtest/gtest.h>
#include <vm.h>

class P_InitVM_Image : public ::testing::TestWithParam<std::string /*image name*/>
{
protected:
    std::auto_ptr<IMemoryManager> m_memoryManager;
public:
    std::auto_ptr<Image> m_image;
    std::auto_ptr<SmalltalkVM> m_vm;
    P_InitVM_Image() : m_memoryManager(), m_image(), m_vm() {}
    virtual ~P_InitVM_Image() {}

    virtual void SetUp()
    {
        bool is_parameterized_test = ::testing::UnitTest::GetInstance()->current_test_info()->value_param();
        if (!is_parameterized_test) {
            // Use TEST_P instead of TEST_F !
            abort();
        }
        ParamType image_name = GetParam();

        #if defined(LLVM)
            m_memoryManager.reset(new LLVMMemoryManager());
        #else
            m_memoryManager.reset(new BakerMemoryManager()),
        #endif
        m_memoryManager->initializeHeap(1024*1024, 1024*1024);
        m_image.reset(new Image(m_memoryManager.get()));
        m_image->loadImage(TESTS_DIR "./data/" + image_name + ".image");
        m_vm.reset(new SmalltalkVM(m_image.get(), m_memoryManager.get()));
    }
    virtual void TearDown() {
        m_vm.reset();
        m_image.reset();
        m_memoryManager.reset();
    }
};

#endif
