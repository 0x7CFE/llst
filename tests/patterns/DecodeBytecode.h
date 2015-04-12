#ifndef LLST_PATTERN_DECODE_BYTECODE_INCLUDED
#define LLST_PATTERN_DECODE_BYTECODE_INCLUDED

#include <gtest/gtest.h>
#include <instructions.h>
#include <analysis.h>

void __assert_fail(const char *__assertion, const char *__file, unsigned int __line, const char */*__function*/)
{
    std::stringstream ss;
    ss << "Assertion '" << __assertion << "' failed in '" << __file << "' at: " << __line;
    throw ss.str();
}

class P_DecodeBytecode : public ::testing::TestWithParam<std::tr1::tuple<std::string /*name*/, std::string /*bytecode*/> >
{
public:
    virtual ~P_DecodeBytecode() {}
    TMethod* m_method;
    std::string m_method_name;
    st::ParsedMethod* m_parsedMethod;
    st::ControlGraph* m_cfg;
    virtual void SetUp()
    {
        bool is_parameterized_test = ::testing::UnitTest::GetInstance()->current_test_info()->value_param();
        if (!is_parameterized_test) {
            // Use TEST_P instead of TEST_F !
            abort();
        }
        ParamType params = GetParam();
        m_method_name = std::tr1::get<0>(params);
        std::string bytecode = std::tr1::get<1>(params);
        m_method = (new ( calloc(4, sizeof(TMethod)) ) TObject(sizeof(TMethod) / sizeof(TObject*) - 2, 0))->cast<TMethod>();
        TByteObject* byteCodes = new ( calloc(4, 4096) ) TByteObject(bytecode.length(), static_cast<TClass*>(0));
        memcpy(byteCodes->getBytes(), bytecode.c_str() , byteCodes->getSize());
        m_method->byteCodes = byteCodes;

        m_parsedMethod = new st::ParsedMethod(m_method);
        m_cfg = new st::ControlGraph(m_parsedMethod);
        try {
            m_cfg->buildGraph();
        } catch (std::string& e) {
            FAIL() << e;
        }
    }
    virtual void TearDown() {
        free(m_method->byteCodes);
        free(m_method);
        delete m_cfg;
        delete m_parsedMethod;
    }
};

#endif
